import http from 'node:http';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { promises as fs } from 'node:fs';
import { spawn } from 'node:child_process';

import { loadConfig } from './lib/config.js';
import { callLlm, listProviderTemplates } from './lib/llm.js';
import { scanSdk } from './lib/sdkScanner.js';
import {
  cleanMonitorLine,
  extractMonitorDiagnostics,
  extractToolDiagnostics,
  isMonitorDiagnosticLine
} from './lib/monitorDiagnostics.js';
import {
  buildChatPrompt,
  buildCodegenPrompt,
  buildComponentSelectionPrompt,
  buildDebugFixPrompt,
  buildHardwareBuildPrompt,
  buildHardwareResourcePlanningPrompt,
  buildRequirementAnalysisPrompt,
  fallbackChat,
  fallbackCodegen,
  fallbackComponentSelection,
  fallbackDebugFix,
  fallbackHardwareBuild,
  fallbackHardwareResourcePlanning,
  fallbackRequirementAnalysis,
  normalizeAgentText,
  parseGeneratedFiles,
  validateGeneratedFiles
} from './lib/workflows.js';
import { getPhysicalStatus } from './lib/physical.js';
import {
  appendSessionEvent,
  clearSessionTimeline,
  deleteSession,
  deleteSessionEvent,
  loadSession,
  loadSessions,
  saveSession
} from './lib/storage.js';

const __filename = fileURLToPath(import.meta.url);
const __dirname = path.dirname(__filename);
const agentRoot = path.resolve(__dirname, '..');
const sdkRoot = path.resolve(agentRoot, '..');
const publicRoot = path.join(agentRoot, 'public');
const mermaidRoot = path.join(agentRoot, 'node_modules', 'mermaid', 'dist');
const config = await loadConfig(agentRoot);

const contentTypes = {
  '.html': 'text/html; charset=utf-8',
  '.css': 'text/css; charset=utf-8',
  '.js': 'application/javascript; charset=utf-8',
  '.mjs': 'application/javascript; charset=utf-8',
  '.json': 'application/json; charset=utf-8',
  '.svg': 'image/svg+xml',
  '.png': 'image/png',
  '.ico': 'image/x-icon'
};

let sdkSummaryCache = null;
let sdkSummaryCacheSignature = '';
let globalPromptCache = null;
const skillCache = new Map();
const monitorSessions = new Map();
const pidTuneSessions = new Map();
const promptRoot = path.join(sdkRoot, 'prompt');
const skillsRoot = path.join(agentRoot, 'skills');
const physicalAgentTemplateRoot = path.join(sdkRoot, 'physical_agent_config', 'template');
const planningSkillFile = 'requirement-analysis-hardware-breakdown.md';
const componentSelectionSkillFile = 'component-selection.md';
const hardwareResourcePlanningSkillFile = 'hardware-resource-planning.md';
const firmwareDraftSkillFile = 'firmware-draft-generation.md';
const physicalAgentIntegrationSkillFile = 'physical-agent-integration.md';

function sendJson(res, status, payload) {
  const body = JSON.stringify(payload, null, 2);
  res.writeHead(status, {
    'content-type': 'application/json; charset=utf-8',
    'cache-control': 'no-store'
  });
  res.end(body);
}

function sendError(res, status, message, details = undefined) {
  sendJson(res, status, { ok: false, error: message, details });
}

async function readJson(req) {
  const chunks = [];
  for await (const chunk of req) {
    chunks.push(chunk);
  }
  if (chunks.length === 0) {
    return {};
  }
  const raw = Buffer.concat(chunks).toString('utf8');
  if (!raw.trim()) {
    return {};
  }
  return JSON.parse(raw);
}

function safeProjectPath(projectPath) {
  if (typeof projectPath !== 'string' || projectPath.trim() === '') {
    throw new Error('projectPath is required');
  }

  const normalized = projectPath.replaceAll('\\', '/').replace(/^\/+/, '');
  if (!normalized.startsWith('project/')) {
    throw new Error('projectPath must be under project/');
  }

  const absolute = path.resolve(sdkRoot, normalized);
  const projectRoot = path.resolve(sdkRoot, 'project');
  if (absolute !== projectRoot && !absolute.startsWith(projectRoot + path.sep)) {
    throw new Error('projectPath escapes project/');
  }
  return { normalized, absolute };
}

function safeOutputFile(filePath) {
  if (typeof filePath !== 'string' || filePath.trim() === '') {
    throw new Error('file path is required');
  }

  const normalized = filePath.replaceAll('\\', '/').replace(/^\/+/, '');
  const absolute = path.resolve(sdkRoot, normalized);
  const projectRoot = path.resolve(sdkRoot, 'project');
  const physicalConfigRoot = path.resolve(sdkRoot, 'physical_agent_config', 'config');
  const underProject = absolute === projectRoot || absolute.startsWith(projectRoot + path.sep);
  const underPhysicalConfig = absolute === physicalConfigRoot || absolute.startsWith(physicalConfigRoot + path.sep);
  if (!normalized.startsWith('project/') && !normalized.startsWith('physical_agent_config/config/')) {
    throw new Error(`refusing to write outside allowed output roots: ${filePath}`);
  }
  if (!underProject && !underPhysicalConfig) {
    throw new Error(`path escapes allowed output roots: ${filePath}`);
  }

  return { normalized, absolute };
}

function isInsidePath(parent, child) {
  const relative = path.relative(parent, child);
  return relative === '' || (!!relative && !relative.startsWith('..') && !path.isAbsolute(relative));
}

function safeWorkspacePath(workspacePath = '.') {
  const raw = String(workspacePath || '.').trim() || '.';
  const absolute = path.isAbsolute(raw)
    ? path.resolve(raw)
    : path.resolve(sdkRoot, raw);

  if (!isInsidePath(sdkRoot, absolute)) {
    throw new Error('cwd must stay under SDK root');
  }
  return absolute;
}

function isPromptDocument(filePath) {
  const ext = path.extname(filePath).toLowerCase();
  return ['.md', '.markdown', '.txt'].includes(ext);
}

function safePromptPath(promptPath) {
  if (typeof promptPath !== 'string' || promptPath.trim() === '') {
    throw new Error('promptPath is required');
  }

  const normalized = promptPath.replaceAll('\\', '/').replace(/^\/+/, '');
  const absolute = path.resolve(promptRoot, normalized);
  if (absolute !== promptRoot && !absolute.startsWith(promptRoot + path.sep)) {
    throw new Error('promptPath escapes prompt/');
  }
  if (!isPromptDocument(absolute)) {
    throw new Error('promptPath must be a markdown or text file');
  }
  return { normalized, absolute };
}

async function listPromptDocuments() {
  const files = [];
  async function walk(current) {
    let entries = [];
    try {
      entries = await fs.readdir(current, { withFileTypes: true });
    } catch (error) {
      if (error.code === 'ENOENT') {
        return;
      }
      throw error;
    }

    for (const entry of entries) {
      const absolute = path.join(current, entry.name);
      if (entry.isDirectory()) {
        await walk(absolute);
        continue;
      }
      if (!entry.isFile() || !isPromptDocument(absolute)) {
        continue;
      }
      const relative = path.relative(promptRoot, absolute).replaceAll('\\', '/');
      if (relative === 'prompt0.md') {
        continue;
      }
      const stat = await fs.stat(absolute);
      files.push({
        path: relative,
        name: path.basename(relative),
        directory: path.dirname(relative) === '.' ? '' : path.dirname(relative),
        size: stat.size,
        updatedAt: stat.mtime.toISOString()
      });
    }
  }

  await walk(promptRoot);
  return files.sort((a, b) => a.path.localeCompare(b.path));
}

async function readPromptDocument(promptPath) {
  const { normalized, absolute } = safePromptPath(promptPath);
  const content = await fs.readFile(absolute, 'utf8');
  return { path: normalized, content };
}

async function readProjectFiles(projectPath) {
  const { normalized, absolute } = safeProjectPath(projectPath);
  const files = [];
  async function walk(current) {
    let entries = [];
    try {
      entries = await fs.readdir(current, { withFileTypes: true });
    } catch (error) {
      if (error.code === 'ENOENT') return;
      throw error;
    }
    for (const entry of entries) {
      const fullPath = path.join(current, entry.name);
      const relative = path.relative(sdkRoot, fullPath).replaceAll('\\', '/');
      if (entry.isDirectory()) {
        if (entry.name === 'build' || entry.name.startsWith('build.') || entry.name.startsWith('.')) continue;
        await walk(fullPath);
        continue;
      }
      if (!entry.isFile()) continue;
      if (!/\.(c|h|cpp|hpp|txt)$/.test(entry.name) && entry.name !== 'CMakeLists.txt') continue;
      const stat = await fs.stat(fullPath);
      if (stat.size > 120000) continue;
      files.push({ path: relative, content: await fs.readFile(fullPath, 'utf8') });
    }
  }
  await walk(absolute);
  return files.sort((a, b) => a.path.localeCompare(b.path));
}

function mergeProjectFiles(diskFiles, draftFiles) {
  const byPath = new Map();
  for (const file of diskFiles || []) {
    if (file?.path?.startsWith('project/')) byPath.set(file.path, file);
  }
  for (const file of draftFiles || []) {
    if (file?.path?.startsWith('project/') && typeof file.content === 'string') {
      byPath.set(file.path, { path: file.path, content: file.content });
    }
  }
  return [...byPath.values()].sort((a, b) => a.path.localeCompare(b.path));
}

function extractToolErrors(toolResult = {}) {
  if (Array.isArray(toolResult.diagnostics?.keyLines) && toolResult.diagnostics.keyLines.length > 0) {
    const keyLines = toolResult.diagnostics.keyLines.slice(0, 80);
    const locationLines = Array.isArray(toolResult.diagnostics.locations)
      ? toolResult.diagnostics.locations.slice(0, 40).map((location) => {
        const pos = [location.file, location.line, location.column].filter((part) => part !== null && part !== undefined && part !== '').join(':');
        return `定位: ${String(location.severity || 'error').toUpperCase()} ${pos} ${location.message || ''}`.trim();
      }).filter((line) => !keyLines.includes(line))
      : [];
    return [...locationLines, ...keyLines].slice(0, 80);
  }

  const diagnostics = extractToolDiagnostics(toolResult);
  if (diagnostics.keyLines.length > 0) {
    return diagnostics.keyLines.slice(0, 80);
  }

  const text = [
    String(toolResult.stdout || ''),
    String(toolResult.stderr || ''),
    String(toolResult.error || '')
  ].join('\n');
  const lines = text.split(/\r?\n/);
  const picked = [];
  const seen = new Set();

  function push(line) {
    const clean = String(line || '').trim();
    if (!clean || seen.has(clean)) return;
    seen.add(clean);
    picked.push(clean);
  }

  for (let index = 0; index < lines.length; index += 1) {
    const line = lines[index];
    const textLine = line.trim();
    if (!textLine) continue;
    if (/^(FAILED:|ninja:|CMake Error|error:|fatal error:)/i.test(textLine)
      || /\bfatal error:|\berror:/i.test(textLine)
      || /ninja failed with exit code|output of the command is in/i.test(textLine)) {
      push(textLine);
      const next = lines[index + 1]?.trim();
      if (next && /compilation terminated|from .*:|note:|warning:/i.test(next)) push(next);
    }
  }

  return picked.slice(0, 80);
}

function toolContextText(body) {
  const tool = body.tool || body.lastTool?.tool || 'unknown';
  const command = body.command || body.lastTool?.command || '';
  const result = body.result || body.lastTool?.result || {};
  const errors = extractToolErrors(result);
  const summary = String(body.summary || '').trim();
  return [
    `tool: ${tool}`,
    command ? `command: ${command}` : '',
    `exitCode: ${result.code ?? 'unknown'}`,
    result.timedOut ? 'timedOut: true' : '',
    result.truncated ? 'outputTruncated: true' : '',
    'keyErrors:',
    errors.length ? errors.map((line) => `- ${line}`).join('\n') : '- 未提取到关键错误',
    summary ? `frontendSummary:\n${summary}` : ''
  ].filter(Boolean).join('\n');
}

async function requirementFromBody(body) {
  const typed = String(body.requirement || '').trim();
  if (typed || !body.promptPath) {
    return typed;
  }
  return (await readPromptDocument(body.promptPath)).content.trim();
}

async function getGlobalPrompt() {
  if (globalPromptCache !== null) {
    return globalPromptCache;
  }

  try {
    globalPromptCache = (await fs.readFile(path.join(promptRoot, 'prompt0.md'), 'utf8')).trim();
  } catch (error) {
    if (error.code !== 'ENOENT') {
      throw error;
    }
    globalPromptCache = '';
  }
  return globalPromptCache;
}

function applyGlobalPrompt(messages, globalPrompt) {
  if (!globalPrompt) {
    return messages;
  }
  return [
    {
      role: 'system',
      content: [
        '以下是 ./prompt/prompt0.md 中的全局开发约束，所有规划、问答和代码生成都必须默认遵守，用户无需主动选择：',
        globalPrompt
      ].join('\n\n')
    },
    ...messages
  ];
}

function safeSkillPath(skillFile) {
  if (typeof skillFile !== 'string' || skillFile.trim() === '') {
    throw new Error('skill file is required');
  }

  const normalized = skillFile.replaceAll('\\', '/').replace(/^\/+/, '');
  const absolute = path.resolve(skillsRoot, normalized);
  if (absolute !== skillsRoot && !absolute.startsWith(skillsRoot + path.sep)) {
    throw new Error('skill file escapes agent/skills/');
  }
  if (path.extname(absolute).toLowerCase() !== '.md') {
    throw new Error('skill file must be markdown');
  }
  return { normalized, absolute };
}

async function getSkillText(skillFile) {
  const { normalized, absolute } = safeSkillPath(skillFile);
  if (skillCache.has(normalized)) {
    return skillCache.get(normalized);
  }

  try {
    const text = (await fs.readFile(absolute, 'utf8')).trim();
    skillCache.set(normalized, text);
    return text;
  } catch (error) {
    if (error.code !== 'ENOENT') {
      throw error;
    }
    skillCache.set(normalized, '');
    return '';
  }
}

async function getPhysicalAgentTemplateText() {
  const templateFiles = ['physical_driver.yaml', 'driver.py', 'physical-agent.yaml'];
  const blocks = [];
  for (const name of templateFiles) {
    const absolute = path.join(physicalAgentTemplateRoot, name);
    try {
      const content = (await fs.readFile(absolute, 'utf8')).trim();
      blocks.push(`===== TEMPLATE: physical_agent_config/template/${name} =====\n${content}`);
    } catch (error) {
      if (error.code !== 'ENOENT') {
        throw error;
      }
    }
  }
  if (blocks.length === 0) {
    return '';
  }
  return [
    '以下是 Physical Agent 上位机配置模板。生成 physical_agent_config/config/ 下文件时必须以这些模板为基础替换占位符：',
    blocks.join('\n\n')
  ].join('\n\n');
}

function applySkillPrompt(messages, skillName, skillText) {
  if (!skillText) {
    return messages;
  }
  return [
    {
      role: 'system',
      content: [
        `以下是 agent/skills/${skillName}，本轮必须使用该 skill：`,
        skillText
      ].join('\n\n')
    },
    ...messages
  ];
}

function shouldUsePhysicalAgentSkill(text) {
  return /physical[\s_-]*agent|service_physical_agent|上位机|执行器|物理\s*agent|接入\s*physical|wifi\s*(?:通信|控制|执行器)|wi-fi\s*(?:通信|控制|执行器)/i.test(String(text || ''));
}

async function getSdkSummary() {
  const signature = await sdkScanSignature();
  if (!sdkSummaryCache || sdkSummaryCacheSignature !== signature) {
    sdkSummaryCache = await scanSdk(sdkRoot);
    sdkSummaryCacheSignature = signature;
  }
  return sdkSummaryCache;
}

async function sdkScanSignature() {
  const roots = ['boards', 'components', 'bsp'].map((dir) => path.join(sdkRoot, dir));
  const parts = [];
  async function walk(current) {
    let entries = [];
    try {
      entries = await fs.readdir(current, { withFileTypes: true });
    } catch {
      return;
    }
    for (const entry of entries) {
      const fullPath = path.join(current, entry.name);
      if (entry.isDirectory()) {
        await walk(fullPath);
        continue;
      }
      if (!/\.(?:h|c|hpp|cpp|cc|txt)$/.test(entry.name) && entry.name !== 'CMakeLists.txt') {
        continue;
      }
      const stat = await fs.stat(fullPath);
      parts.push(`${path.relative(sdkRoot, fullPath)}:${stat.mtimeMs}:${stat.size}`);
    }
  }
  for (const root of roots) {
    await walk(root);
  }
  return parts.sort().join('|');
}

function loadSdkResourcesForBoard(sdkSummary, boardName = defaultBoardName()) {
  const board = (sdkSummary.boards || []).find((item) => item.name === boardName)
    || (sdkSummary.boards || [])[0]
    || null;
  return {
    tool: 'sdkResourceLoader',
    board: board?.name || boardName,
    boardResources: board?.resources || [],
    componentBusResources: (sdkSummary.components || []).flatMap((component) => component.busResources || []),
    defines: board?.defines || {}
  };
}

async function serveStaticFromRoot(res, root, requested) {
  const absolute = path.resolve(root, requested.replace(/^\/+/, ''));

  if (absolute !== root && !absolute.startsWith(root + path.sep)) {
    sendError(res, 403, 'forbidden');
    return;
  }

  try {
    const stat = await fs.stat(absolute);
    if (!stat.isFile()) {
      sendError(res, 404, 'not found');
      return;
    }

    const ext = path.extname(absolute);
    res.writeHead(200, {
      'content-type': contentTypes[ext] || 'application/octet-stream',
      'cache-control': 'no-cache'
    });
    res.end(await fs.readFile(absolute));
  } catch (error) {
    if (error.code === 'ENOENT') {
      sendError(res, 404, 'not found');
      return;
    }
    sendError(res, 500, 'failed to serve asset', error.message);
  }
}

async function serveStatic(req, res, pathname) {
  const requested = pathname === '/' ? '/index.html' : pathname;
  await serveStaticFromRoot(res, publicRoot, requested);
}

async function handleHardwareBuild(req, res) {
  const body = await readJson(req);
  const requirement = await requirementFromBody(body);
  const sdkSummary = await getSdkSummary();
  const prompt = buildHardwareBuildPrompt({
      requirement,
      sdkSummary,
      boardName: body.board || body.boardName || defaultBoardName(),
      analysis: body.analysis || '',
      componentSelection: body.componentSelection || '',
      resourcePlan: body.resourcePlan || ''
    });
  const messages = applyGlobalPrompt(prompt.messages, await getGlobalPrompt());

  if (body.useLlm) {
    try {
      const text = await callLlm({
        config,
        providerRequest: body.provider || {},
        messages,
        temperature: body.temperature ?? 0.2
      });
      sendJson(res, 200, {
        ok: true,
        mode: 'llm',
        result: normalizeAgentText(text),
        sdkSummary
      });
      return;
    } catch (error) {
      const fallback = fallbackHardwareBuild(requirement, body.analysis || '', body.componentSelection || '', body.resourcePlan || '', sdkSummary);
      sendJson(res, 200, {
        ok: true,
        mode: 'fallback',
        warning: error.message,
        result: fallback,
        sdkSummary
      });
      return;
    }
  }

  sendJson(res, 200, {
    ok: true,
    mode: 'fallback',
    result: fallbackHardwareBuild(requirement, body.analysis || '', body.componentSelection || '', body.resourcePlan || '', sdkSummary),
    sdkSummary
  });
}

async function handleAnalyze(req, res) {
  const body = await readJson(req);
  const requirement = await requirementFromBody(body);
  const sdkSummary = await getSdkSummary();
  const prompt = buildRequirementAnalysisPrompt({
    requirement,
    sdkSummary,
    boardName: body.board || body.boardName || defaultBoardName()
  });
  const messages = applyGlobalPrompt(
    applySkillPrompt(prompt.messages, planningSkillFile, await getSkillText(planningSkillFile)),
    await getGlobalPrompt()
  );

  if (body.useLlm) {
    try {
      const text = await callLlm({
        config,
        providerRequest: body.provider || {},
        messages,
        temperature: body.temperature ?? 0.15
      });
      sendJson(res, 200, {
        ok: true,
        mode: 'llm',
        result: normalizeAgentText(text),
        sdkSummary
      });
      return;
    } catch (error) {
      sendJson(res, 200, {
        ok: true,
        mode: 'fallback',
        warning: error.message,
        result: fallbackRequirementAnalysis(requirement, sdkSummary),
        sdkSummary
      });
      return;
    }
  }

  sendJson(res, 200, {
    ok: true,
    mode: 'fallback',
    result: fallbackRequirementAnalysis(requirement, sdkSummary),
    sdkSummary
  });
}

async function handleComponentSelection(req, res) {
  const body = await readJson(req);
  const requirement = await requirementFromBody(body);
  const sdkSummary = await getSdkSummary();
  const prompt = buildComponentSelectionPrompt({
    requirement,
    analysis: body.analysis || '',
    selectionNotes: body.selectionNotes || '',
    sdkSummary,
    boardName: body.board || body.boardName || defaultBoardName()
  });
  const messages = applyGlobalPrompt(
    applySkillPrompt(prompt.messages, componentSelectionSkillFile, await getSkillText(componentSelectionSkillFile)),
    await getGlobalPrompt()
  );

  if (body.useLlm) {
    try {
      const text = await callLlm({
        config,
        providerRequest: body.provider || {},
        messages,
        temperature: body.temperature ?? 0.15
      });
      sendJson(res, 200, {
        ok: true,
        mode: 'llm',
        result: normalizeAgentText(text),
        sdkSummary
      });
      return;
    } catch (error) {
      sendJson(res, 200, {
        ok: true,
        mode: 'fallback',
        warning: error.message,
        result: fallbackComponentSelection(requirement, body.analysis || '', body.selectionNotes || '', sdkSummary),
        sdkSummary
      });
      return;
    }
  }

  sendJson(res, 200, {
    ok: true,
    mode: 'fallback',
    result: fallbackComponentSelection(requirement, body.analysis || '', body.selectionNotes || '', sdkSummary),
    sdkSummary
  });
}

async function handleHardwareResourcePlanning(req, res) {
  const body = await readJson(req);
  const requirement = await requirementFromBody(body);
  const sdkSummary = await getSdkSummary();
  const boardName = body.board || body.boardName || defaultBoardName();
  const sdkResources = loadSdkResourcesForBoard(sdkSummary, boardName);
  const prompt = buildHardwareResourcePlanningPrompt({
    requirement,
    analysis: body.analysis || '',
    componentSelection: body.componentSelection || '',
    resourceNotes: body.resourceNotes || '',
    sdkSummary,
    boardName,
    sdkResources
  });
  const messages = applyGlobalPrompt(
    applySkillPrompt(prompt.messages, hardwareResourcePlanningSkillFile, await getSkillText(hardwareResourcePlanningSkillFile)),
    await getGlobalPrompt()
  );

  if (body.useLlm) {
    try {
      const text = await callLlm({
        config,
        providerRequest: body.provider || {},
        messages,
        temperature: body.temperature ?? 0.12
      });
      sendJson(res, 200, {
        ok: true,
        mode: 'llm',
        result: normalizeAgentText(text),
        sdkSummary,
        sdkResources
      });
      return;
    } catch (error) {
      sendJson(res, 200, {
        ok: true,
        mode: 'fallback',
        warning: error.message,
        result: fallbackHardwareResourcePlanning(requirement, body.analysis || '', body.componentSelection || '', body.resourceNotes || '', sdkSummary),
        sdkSummary,
        sdkResources
      });
      return;
    }
  }

  sendJson(res, 200, {
    ok: true,
    mode: 'fallback',
    result: fallbackHardwareResourcePlanning(requirement, body.analysis || '', body.componentSelection || '', body.resourceNotes || '', sdkSummary),
    sdkSummary,
    sdkResources
  });
}

async function handleChat(req, res) {
  const body = await readJson(req);
  const requirement = await requirementFromBody(body);
  const sdkSummary = await getSdkSummary();
  const prompt = buildChatPrompt({
    question: body.question || '',
    requirement,
    currentPlan: body.currentPlan || '',
    sdkSummary
  });
  const messages = applyGlobalPrompt(prompt.messages, await getGlobalPrompt());

  if (body.useLlm) {
    try {
      const text = await callLlm({
        config,
        providerRequest: body.provider || {},
        messages,
        temperature: body.temperature ?? 0.2
      });
      sendJson(res, 200, {
        ok: true,
        mode: 'llm',
        answer: normalizeAgentText(text)
      });
      return;
    } catch (error) {
      sendJson(res, 200, {
        ok: true,
        mode: 'fallback',
        warning: error.message,
        answer: fallbackChat(body.question || '', sdkSummary)
      });
      return;
    }
  }

  sendJson(res, 200, {
    ok: true,
    mode: 'fallback',
    answer: fallbackChat(body.question || '', sdkSummary)
  });
}

async function handleCodegen(req, res) {
  const body = await readJson(req);
  const requirement = await requirementFromBody(body);
  const sdkSummary = await getSdkSummary();
  const globalPrompt = await getGlobalPrompt();
  const projectName = body.projectName || '';
  const projectPath = body.projectPath || `project/${projectName || 'robot_agent_app'}`;
  const projectFiles = mergeProjectFiles(await readProjectFiles(projectPath), body.files || []);
  const prompt = buildCodegenPrompt({
    requirement,
    projectName,
    plan: body.plan || '',
    analysis: body.analysis || '',
    sdkSummary,
    globalPrompt,
    boardName: body.board || body.boardName || defaultBoardName(),
    projectFiles
  });
  let skillMessages = applySkillPrompt(prompt.messages, firmwareDraftSkillFile, await getSkillText(firmwareDraftSkillFile));
  if (shouldUsePhysicalAgentSkill(`${requirement}\n${body.plan || ''}`)) {
    const physicalSkillText = [
      await getSkillText(physicalAgentIntegrationSkillFile),
      await getPhysicalAgentTemplateText()
    ].filter(Boolean).join('\n\n');
    skillMessages = applySkillPrompt(skillMessages, physicalAgentIntegrationSkillFile, physicalSkillText);
  }
  const messages = applyGlobalPrompt(skillMessages, globalPrompt);

  if (body.useLlm) {
    try {
      const text = await callLlm({
        config,
        providerRequest: body.provider || {},
        messages,
        temperature: body.temperature ?? 0.15
      });
      const fallback = fallbackCodegen(requirement, body.projectName || '', sdkSummary);
      const parsedFiles = parseGeneratedFiles(text);
      const validation = validateGeneratedFiles(parsedFiles, sdkSummary, body.board || body.boardName || defaultBoardName());
      const useParsedFiles = parsedFiles.length > 0 && validation.errors.length === 0;
      sendJson(res, 200, {
        ok: true,
        mode: 'llm',
        projectName: fallback.projectName,
        result: normalizeAgentText(text),
        files: useParsedFiles ? parsedFiles : fallback.files,
        validation: useParsedFiles ? validation : validateGeneratedFiles(fallback.files, sdkSummary, body.board || body.boardName || defaultBoardName()),
        note: useParsedFiles
          ? 'LLM file blocks were parsed into editable files.'
          : parsedFiles.length > 0
            ? 'LLM file blocks were parsed, but static validation found SDK API risks. Fallback files are included as a safe editable scaffold.'
            : 'No parsable LLM file blocks were found. Fallback files are included as a safe editable scaffold.'
      });
      return;
    } catch (error) {
      const fallback = fallbackCodegen(requirement, body.projectName || '', sdkSummary);
      sendJson(res, 200, {
        ok: true,
        mode: 'fallback',
        warning: error.message,
        validation: validateGeneratedFiles(fallback.files, sdkSummary, body.board || body.boardName || defaultBoardName()),
        ...fallback
      });
      return;
    }
  }

  sendJson(res, 200, {
    ok: true,
    mode: 'fallback',
    validation: validateGeneratedFiles(fallbackCodegen(requirement, body.projectName || '', sdkSummary).files, sdkSummary, body.board || body.boardName || defaultBoardName()),
    ...fallbackCodegen(requirement, body.projectName || '', sdkSummary)
  });
}

async function handleDebugFix(req, res) {
  const body = await readJson(req);
  const requirement = await requirementFromBody(body);
  const sdkSummary = await getSdkSummary();
  const globalPrompt = await getGlobalPrompt();
  const projectName = body.projectName || '';
  const projectPath = body.projectPath || `project/${projectName || 'robot_agent_app'}`;
  const projectFiles = mergeProjectFiles(await readProjectFiles(projectPath), body.files || []);
  const toolContext = toolContextText(body);
  const prompt = buildDebugFixPrompt({
    requirement,
    projectName,
    plan: body.plan || '',
    analysis: body.analysis || '',
    sdkSummary,
    toolContext,
    projectFiles,
    globalPrompt,
    boardName: body.board || body.boardName || defaultBoardName()
  });
  const messages = applyGlobalPrompt(prompt.messages, globalPrompt);

  if (body.useLlm) {
    try {
      const text = await callLlm({
        config,
        providerRequest: body.provider || {},
        messages,
        temperature: body.temperature ?? 0.1
      });
      const parsedFiles = parseGeneratedFiles(text);
      const validation = validateGeneratedFiles(mergeProjectFiles(projectFiles, parsedFiles), sdkSummary, body.board || body.boardName || defaultBoardName());
      const useParsedFiles = parsedFiles.length > 0 && validation.errors.length === 0;
      const fallbackFiles = fallbackDebugFix(projectFiles);
      const fallbackValidation = validateGeneratedFiles(mergeProjectFiles(projectFiles, fallbackFiles), sdkSummary, body.board || body.boardName || defaultBoardName());
      sendJson(res, 200, {
        ok: true,
        mode: 'llm',
        result: normalizeAgentText(text),
        toolContext,
        files: useParsedFiles ? parsedFiles : fallbackFiles,
        validation: useParsedFiles ? validation : fallbackValidation,
        note: useParsedFiles
          ? '调试修复已生成 project/ 文件，可确认写入后重新 Build。'
          : parsedFiles.length > 0
            ? 'LLM 修复未通过静态校验，已尝试提供安全 fallback。'
            : 'LLM 未输出可解析文件块，已尝试提供安全 fallback。'
      });
      return;
    } catch (error) {
      const fallbackFiles = fallbackDebugFix(projectFiles);
      const validation = validateGeneratedFiles(mergeProjectFiles(projectFiles, fallbackFiles), sdkSummary, body.board || body.boardName || defaultBoardName());
      sendJson(res, 200, {
        ok: true,
        mode: 'fallback',
        warning: error.message,
        result: toolContext,
        toolContext,
        files: fallbackFiles,
        validation,
        note: fallbackFiles.length > 0
          ? 'LLM 不可用，已应用内置规则生成候选修复。'
          : 'LLM 不可用，内置规则无法确定安全修复，请查看关键错误。'
      });
      return;
    }
  }

  const fallbackFiles = fallbackDebugFix(projectFiles);
  const validation = validateGeneratedFiles(mergeProjectFiles(projectFiles, fallbackFiles), sdkSummary, body.board || body.boardName || defaultBoardName());
  sendJson(res, 200, {
    ok: true,
    mode: 'fallback',
    result: toolContext,
    toolContext,
    files: fallbackFiles,
    validation,
    note: fallbackFiles.length > 0
      ? '已应用内置规则生成候选修复。'
      : '内置规则无法确定安全修复，请开启 LLM 或查看关键错误。'
  });
}

async function handleProjectWrite(req, res) {
  const body = await readJson(req);
  const files = Array.isArray(body.files) ? body.files : [];
  if (files.length === 0) {
    sendError(res, 400, 'files must be a non-empty array');
    return;
  }

  const written = [];
  try {
    for (const file of files) {
      const { normalized, absolute } = safeOutputFile(file.path);
      const content = String(file.content ?? '');
      await fs.mkdir(path.dirname(absolute), { recursive: true });
      await fs.writeFile(absolute, content, 'utf8');
      const stat = await fs.stat(absolute);
      const saved = await fs.readFile(absolute, 'utf8');
      if (saved !== content) {
        throw new Error(`write verification failed: ${normalized}`);
      }
      written.push({
        path: normalized,
        bytes: stat.size
      });
    }
  } catch (error) {
    sendError(res, 400, error.message);
    return;
  }

  sendJson(res, 200, { ok: true, written });
}

function appendOutput(current, chunk, tracker) {
  if (!tracker.maxBytes || tracker.maxBytes <= 0) {
    return current + chunk.toString();
  }

  const remaining = tracker.maxBytes - tracker.bytes;
  if (remaining <= 0) {
    tracker.truncated = true;
    return current;
  }

  if (chunk.length > remaining) {
    tracker.truncated = true;
    tracker.bytes = tracker.maxBytes;
    return current + chunk.subarray(0, remaining).toString();
  }

  tracker.bytes += chunk.length;
  return current + chunk.toString();
}

function runTool(tool, args, cwd, timeoutMs = 0, options = {}) {
  return new Promise((resolve) => {
    const startedAt = new Date().toISOString();
    const outputTracker = {
      bytes: 0,
      maxBytes: Number(options.maxOutputBytes || 0),
      truncated: false
    };
    const child = spawn(tool, args, {
      cwd,
      shell: false,
      env: process.env,
      ...(options.spawnOptions || {})
    });

    let stdout = '';
    let stderr = '';
    let timedOut = false;
    let timer = null;
    if (timeoutMs > 0) {
      timer = setTimeout(() => {
        timedOut = true;
        child.kill('SIGTERM');
      }, timeoutMs);
    }
    child.stdout?.on('data', (chunk) => {
      stdout = appendOutput(stdout, chunk, outputTracker);
    });
    child.stderr?.on('data', (chunk) => {
      stderr = appendOutput(stderr, chunk, outputTracker);
    });
    child.on('error', (error) => {
      if (timer) clearTimeout(timer);
      resolve({ ok: false, code: -1, startedAt, stdout, stderr, truncated: outputTracker.truncated, error: error.message });
    });
    child.on('close', (code) => {
      if (timer) clearTimeout(timer);
      const ok = code === 0 || (timedOut && options.okOnTimeout === true);
      resolve({ ok, code, timedOut, truncated: outputTracker.truncated, startedAt, finishedAt: new Date().toISOString(), stdout, stderr });
    });
  });
}

function waitForChildClose(child, timeoutMs = 800) {
  return new Promise((resolve) => {
    if (!child || child.exitCode !== null || child.signalCode !== null) {
      resolve(true);
      return;
    }
    const timer = setTimeout(() => {
      cleanup();
      resolve(false);
    }, timeoutMs);
    function cleanup() {
      clearTimeout(timer);
      child.off('close', onClose);
      child.off('exit', onClose);
    }
    function onClose() {
      cleanup();
      resolve(true);
    }
    child.once('close', onClose);
    child.once('exit', onClose);
  });
}

async function stopChildProcess(child) {
  if (!child || child.exitCode !== null || child.signalCode !== null) {
    return true;
  }

  if (process.platform === 'win32') {
    await new Promise((resolve) => {
      const killer = spawn('taskkill', ['/PID', String(child.pid), '/T', '/F'], {
        shell: false,
        windowsHide: true
      });
      killer.on('close', () => resolve());
      killer.on('error', () => resolve());
    });
    return waitForChildClose(child, 1200);
  }

  child.kill('SIGTERM');
  const stopped = await waitForChildClose(child, 800);
  if (stopped) {
    return true;
  }
  child.kill('SIGKILL');
  return waitForChildClose(child, 800);
}

function appendSessionLines(session, chunk, stream) {
  const text = chunk.toString();
  session[stream] += text;
  const lines = text.split(/\r?\n/);
  for (const line of lines) {
    const clean = cleanMonitorLine(line);
    if (!clean.trim()) continue;
    const target = isMonitorDiagnosticLine(clean) ? session.errorLines : session.logLines;
    target.push(clean);
  }
  if (session.logLines.length > 300) {
    session.logLines.splice(0, session.logLines.length - 300);
    session.logsTrimmed = true;
  }
  if (session.errorLines.length > 160) {
    session.errorLines.splice(0, session.errorLines.length - 160);
    session.errorsTrimmed = true;
  }
}

function monitorSessionPayload(session) {
  const result = {
    ok: session.status === 'running' || session.code === 0,
    code: session.code,
    timedOut: false,
    startedAt: session.startedAt,
    finishedAt: session.finishedAt || null,
    stdout: session.stdout,
    stderr: session.stderr,
    error: session.error || '',
    truncated: session.logsTrimmed || session.errorsTrimmed
  };
  const diagnostics = extractMonitorDiagnostics(result);
  return {
    id: session.id,
    status: session.status,
    command: session.command,
    result,
    diagnostics,
    logs: session.logLines.slice(-160),
    errors: diagnostics.keyLines.length ? diagnostics.keyLines : session.errorLines.slice(-80),
    logsTrimmed: session.logsTrimmed,
    errorsTrimmed: session.errorsTrimmed
  };
}

function numberOrDefault(value, fallback) {
  const numeric = Number(value);
  return Number.isFinite(numeric) ? numeric : fallback;
}

function pidSessionPayload(session) {
  return {
    id: session.id,
    status: session.status,
    command: session.command,
    startedAt: session.startedAt,
    finishedAt: session.finishedAt || null,
    code: session.code,
    result: session.result || null,
    samples: session.samples.slice(-240),
    logs: session.logs.slice(-160),
    errors: session.errors.slice(-80),
    tx: session.tx.slice(-80),
    sampleCount: session.samples.length,
    logsTrimmed: session.logsTrimmed,
    samplesTrimmed: session.samplesTrimmed
  };
}

function appendPidOutput(session, chunk, stream) {
  const text = chunk.toString();
  session[stream] += text;
  const lines = text.split(/\r?\n/);
  for (const raw of lines) {
    const line = raw.trim();
    if (!line) continue;
    let event = null;
    try {
      event = JSON.parse(line);
    } catch {
      session.logs.push(line);
      continue;
    }
    if (event.event === 'sample') {
      session.samples.push(event);
    } else if (event.event === 'tx') {
      session.tx.push(event);
    } else if (event.event === 'result') {
      session.result = event;
      session.logs.push(event.ok
        ? `PI result: Kp=${Number(event.kp).toFixed(4)}, Ki=${Number(event.ki).toFixed(4)}, Ku=${Number(event.ku).toFixed(4)}, Pu=${Number(event.pu).toFixed(4)}`
        : `PI tune failed: ${event.message || 'no result'}`);
    } else if (event.event === 'error') {
      session.errors.push(event.message || line);
    } else {
      session.logs.push(event.message || event.line || line);
    }
  }
  if (session.samples.length > 1000) {
    session.samples.splice(0, session.samples.length - 1000);
    session.samplesTrimmed = true;
  }
  if (session.logs.length > 400) {
    session.logs.splice(0, session.logs.length - 400);
    session.logsTrimmed = true;
  }
  if (session.tx.length > 200) {
    session.tx.splice(0, session.tx.length - 200);
  }
  if (session.errors.length > 160) {
    session.errors.splice(0, session.errors.length - 160);
  }
}

function bashPath(filePath) {
  return filePath.replaceAll('\\', '/');
}

function splitArgs(argsText) {
  const text = String(argsText || '');
  const args = [];
  let current = '';
  let quote = '';
  let escaped = false;

  for (const char of text) {
    if (escaped) {
      current += char;
      escaped = false;
      continue;
    }
    if (quote === '"' && char === '\\') {
      escaped = true;
      continue;
    }
    if (quote) {
      if (char === quote) {
        quote = '';
      } else {
        current += char;
      }
      continue;
    }
    if (char === '"' || char === "'") {
      quote = char;
      continue;
    }
    if (/\s/.test(char)) {
      if (current) {
        args.push(current);
        current = '';
      }
      continue;
    }
    current += char;
  }

  if (escaped) {
    current += '\\';
  }
  if (quote) {
    throw new Error('unterminated quote in arguments');
  }
  if (current) {
    args.push(current);
  }
  return args;
}

function execArgsFromBody(body) {
  if (Array.isArray(body.args)) {
    return body.args.map((arg) => String(arg));
  }
  if (typeof body.argsText === 'string') {
    return splitArgs(body.argsText);
  }
  if (typeof body.args === 'string') {
    return splitArgs(body.args);
  }
  return [];
}

function normalizedProgramName(program) {
  return path.basename(String(program || '')).toLowerCase().replace(/\.(exe|cmd|bat)$/, '');
}

function allowedProgramSet() {
  const allowed = Array.isArray(config.execution?.allowedPrograms)
    ? config.execution.allowedPrograms
    : [];
  return new Set(allowed.map((program) => String(program).toLowerCase()));
}

function isAllowedProgram(program) {
  const allowed = allowedProgramSet();
  const raw = String(program || '').toLowerCase();
  const base = normalizedProgramName(program);
  return allowed.has(raw) || allowed.has(base);
}

function assertAllowedProgram(program) {
  if (!isAllowedProgram(program)) {
    throw new Error(`program is not allowed by agent execution config: ${program}`);
  }
}

function assertNoInlineShell(program, args) {
  const base = normalizedProgramName(program);
  const shellPrograms = new Set(['bash', 'sh', 'zsh', 'fish', 'cmd', 'powershell', 'pwsh']);
  if (!shellPrograms.has(base)) {
    return;
  }

  const hasInlineFlag = args.some((arg) => {
    const flag = String(arg).toLowerCase();
    if (base === 'cmd') {
      return flag === '/c';
    }
    if (base === 'powershell' || base === 'pwsh') {
      return flag === '-command' || flag === '-encodedcommand' || flag === '-c';
    }
    return /^-[a-z]*c[a-z]*$/.test(flag);
  });

  if (hasInlineFlag) {
    throw new Error('inline shell commands are disabled; run a script file instead');
  }
}

function pathList() {
  const rawPath = process.env.PATH || process.env.Path || process.env.path || '';
  return rawPath.split(path.delimiter).filter(Boolean);
}

function windowsPathExts() {
  const rawExts = process.env.PATHEXT || '.COM;.EXE;.BAT;.CMD;.PS1';
  return rawExts
    .split(';')
    .map((ext) => ext.trim().toLowerCase())
    .filter(Boolean);
}

async function isFile(filePath) {
  try {
    return (await fs.stat(filePath)).isFile();
  } catch {
    return false;
  }
}

async function findProgramOnPath(program) {
  const raw = String(program || '').trim();
  if (!raw || raw.includes('/') || raw.includes('\\') || path.isAbsolute(raw)) {
    return null;
  }

  const hasExt = path.extname(raw) !== '';
  const candidates = [];
  for (const dir of pathList()) {
    if (process.platform === 'win32' && !hasExt) {
      for (const ext of windowsPathExts()) {
        candidates.push(path.join(dir, `${raw}${ext}`));
      }
      candidates.push(path.join(dir, raw));
    } else {
      candidates.push(path.join(dir, raw));
    }
  }

  const seen = new Set();
  for (const candidate of candidates) {
    const key = process.platform === 'win32' ? candidate.toLowerCase() : candidate;
    if (seen.has(key)) {
      continue;
    }
    seen.add(key);
    if (await isFile(candidate)) {
      return candidate;
    }
  }
  return null;
}

function cmdScriptArg(value) {
  const text = String(value);
  if (/[\0\r\n"]/.test(text)) {
    throw new Error('cmd script arguments cannot contain control characters or double quotes');
  }
  return `"${text.replaceAll('%', '%%')}"`;
}

function wrapWindowsCommandScript(filePath, args, commandParts) {
  const commandLine = `call ${[filePath, ...args].map(cmdScriptArg).join(' ')}`;
  return {
    tool: process.env.ComSpec || 'cmd.exe',
    args: ['/d', '/c', commandLine],
    spawnOptions: { windowsVerbatimArguments: true },
    commandParts
  };
}

function formatCommandPart(part) {
  const text = String(part);
  return /\s/.test(text) ? JSON.stringify(text) : text;
}

function formatCommand(parts) {
  return parts.map(formatCommandPart).join(' ');
}

function execTimeoutMs(input) {
  const defaultTimeoutMs = Number(config.execution?.timeoutMs || 30000);
  const maxTimeoutMs = Number(config.execution?.maxTimeoutMs || 300000);
  const requested = Number(input || defaultTimeoutMs);
  const timeoutMs = Number.isFinite(requested) && requested > 0 ? requested : defaultTimeoutMs;
  return Math.min(Math.max(timeoutMs, 1000), maxTimeoutMs);
}

async function resolveExecutableFile(absolute, args, displayPath = absolute) {
  const stat = await fs.stat(absolute);
  if (!stat.isFile()) {
    throw new Error(`executable path is not a file: ${displayPath}`);
  }

  const ext = path.extname(absolute).toLowerCase();
  if (ext === '.sh') {
    assertAllowedProgram('bash');
    return {
      tool: 'bash',
      args: [bashPath(absolute), ...args],
      commandParts: ['bash', bashPath(displayPath), ...args]
    };
  }
  if (ext === '.py') {
    const python = process.env.PYTHON || 'python';
    assertAllowedProgram(python);
    return {
      tool: python,
      args: [absolute, ...args],
      commandParts: [python, displayPath, ...args]
    };
  }
  if (ext === '.js' || ext === '.mjs' || ext === '.cjs') {
    assertAllowedProgram('node');
    return {
      tool: process.execPath,
      args: [absolute, ...args],
      commandParts: ['node', displayPath, ...args]
    };
  }
  if (ext === '.ps1') {
    const powershell = process.env.PWSH || process.env.POWERSHELL || (process.platform === 'win32' ? 'powershell' : 'pwsh');
    assertAllowedProgram(powershell);
    return {
      tool: powershell,
      args: ['-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', absolute, ...args],
      commandParts: [powershell, '-NoProfile', '-File', displayPath, ...args]
    };
  }
  if (process.platform === 'win32' && (ext === '.cmd' || ext === '.bat')) {
    return wrapWindowsCommandScript(absolute, args, [displayPath, ...args]);
  }

  return { tool: absolute, args, commandParts: [displayPath, ...args] };
}

async function resolveExecutable(command, cwd, args) {
  const raw = String(command || '').trim();
  if (!raw) {
    throw new Error('command is required');
  }
  if (/[\0\r\n]/.test(raw)) {
    throw new Error('command contains invalid control characters');
  }

  const isPathCommand = raw.startsWith('.') || raw.includes('/') || raw.includes('\\') || path.isAbsolute(raw);
  if (!isPathCommand) {
    assertAllowedProgram(raw);
    assertNoInlineShell(raw, args);
    const resolvedProgram = await findProgramOnPath(raw);
    if (resolvedProgram) {
      return resolveExecutableFile(resolvedProgram, args, raw);
    }
    return { tool: raw, args, commandParts: [raw, ...args] };
  }

  const absolute = path.isAbsolute(raw) ? path.resolve(raw) : path.resolve(cwd, raw);
  if (!isInsidePath(sdkRoot, absolute)) {
    throw new Error('executable path must stay under SDK root');
  }

  return resolveExecutableFile(absolute, args);
}

function publicExecutionConfig() {
  return {
    enabled: config.execution?.enabled !== false,
    defaultCwd: config.execution?.defaultCwd || '.',
    timeoutMs: Number(config.execution?.timeoutMs || 30000),
    maxTimeoutMs: Number(config.execution?.maxTimeoutMs || 300000),
    maxOutputBytes: Number(config.execution?.maxOutputBytes || 200000),
    allowedPrograms: Array.isArray(config.execution?.allowedPrograms) ? config.execution.allowedPrograms : []
  };
}

function defaultSerialPort() {
  return process.platform === 'win32' ? 'COM3' : '/dev/ttyUSB0';
}

function defaultBoardName() {
  return 'ch32v203g6u6';
}

function defaultTargetName() {
  return 'ch32';
}

function safeAppName(value, fallback = 'led_blink') {
  const app = String(value || fallback).trim();
  if (!/^[A-Za-z0-9_-]+$/.test(app)) {
    throw new Error('app name may only contain letters, numbers, underscore, and dash');
  }
  return app;
}

async function fileExists(filePath) {
  try {
    await fs.access(filePath);
    return true;
  } catch {
    return false;
  }
}

async function ch32ScriptRunner(scriptName, args) {
  const script = path.join(sdkRoot, 'scripts', `${scriptName}.py`);
  if (!(await fileExists(script))) {
    return null;
  }
  const python = process.env.PYTHON || (process.platform === 'win32' ? 'python' : 'python3');
  const minichlinkDir = path.join(sdkRoot, 'tools', 'ch32fun', 'minichlink');
  const env = { ...process.env };
  if (await fileExists(path.join(minichlinkDir, process.platform === 'win32' ? 'minichlink.exe' : 'minichlink'))) {
    env.PATH = `${minichlinkDir}${path.delimiter}${env.PATH || ''}`;
  }
  return {
    tool: python,
    args: [script, ...args],
    commandParts: [python, script, ...args],
    spawnOptions: { env }
  };
}

function scriptRunner(scriptName, args) {
  if (process.platform === 'win32') {
    const powershell = process.env.PWSH || process.env.POWERSHELL || 'powershell';
    const script = path.join(sdkRoot, 'tools', `${scriptName}.ps1`);
    return {
      tool: powershell,
      args: ['-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', script, ...args],
      commandParts: [powershell, '-NoProfile', '-File', script, ...args],
      spawnOptions: undefined
    };
  }

  const script = path.join(sdkRoot, 'tools', `${scriptName}.sh`);
  const bashArgs = [bashPath(script), ...args.map((arg) => bashPath(arg))];
  return {
    tool: 'bash',
    args: bashArgs,
    commandParts: ['bash', ...bashArgs],
    spawnOptions: undefined
  };
}

async function handleExec(req, res) {
  if (config.execution?.enabled === false) {
    sendError(res, 403, 'agent execution is disabled');
    return;
  }

  const body = await readJson(req);
  try {
    const cwd = safeWorkspacePath(body.cwd || config.execution?.defaultCwd || '.');
    const args = execArgsFromBody(body);
    const resolved = await resolveExecutable(body.command || body.file, cwd, args);
    const timeoutMs = execTimeoutMs(body.timeoutMs);
    const result = await runTool(resolved.tool, resolved.args, cwd, timeoutMs, {
      maxOutputBytes: Number(config.execution?.maxOutputBytes || 200000),
      okOnTimeout: false,
      spawnOptions: resolved.spawnOptions
    });
    const relativeCwd = path.relative(sdkRoot, cwd) || '.';
    sendJson(res, 200, {
      ok: true,
      tool: 'exec',
      toolOk: result.ok,
      cwd: relativeCwd,
      command: formatCommand(resolved.commandParts),
      timeoutMs,
      result
    });
  } catch (error) {
    sendError(res, 400, error.message);
  }
}

async function handleTool(req, res, kind) {
  const body = await readJson(req);
  const { normalized } = safeProjectPath(body.projectPath || `project/${body.projectName || 'led_blink'}`);
  const target = body.target || defaultTargetName();
  const board = body.board || defaultBoardName();
  const port = body.port || defaultSerialPort();
  const projectAbsolute = path.join(sdkRoot, normalized);
  const durationSeconds = Math.ceil(Number(body.timeoutMs || 15000) / 1000);
  const args = [projectAbsolute];
  let resolved = null;

  if (kind === 'build') {
    const app = safeAppName(body.app || body.projectName || path.basename(normalized));
    resolved = await ch32ScriptRunner('build', ['--board', board, '--app', app]);
    if (!resolved) {
      args.push(target, board);
    }
  } else if (kind === 'flash') {
    const app = safeAppName(body.app || body.projectName || path.basename(normalized));
    resolved = await ch32ScriptRunner('flash', ['--board', board, '--app', app]);
    if (!resolved) {
      args.push('--target', target, '--board', board, '--port', port);
    }
  } else {
    args.push(port);
    if (kind === 'monitor') {
      args.push(String(durationSeconds));
    }
  }

  resolved ||= scriptRunner(kind, args);
  const timeoutMs = kind === 'monitor' ? Number(body.timeoutMs || 15000) : 0;
  const result = await runTool(resolved.tool, resolved.args, sdkRoot, timeoutMs, {
    okOnTimeout: kind === 'monitor',
    maxOutputBytes: Number(config.execution?.maxOutputBytes || 200000),
    spawnOptions: resolved.spawnOptions
  });
  const diagnostics = kind === 'monitor'
    ? extractMonitorDiagnostics(result)
    : extractToolDiagnostics(result);
  sendJson(res, 200, {
    ok: true,
    tool: kind,
    toolOk: kind === 'monitor' ? !diagnostics.hasErrors : result.ok,
    command: formatCommand(resolved.commandParts),
    result,
    diagnostics
  });
}

async function handleMonitorDiagnose(req, res) {
  const body = await readJson(req);
  const { normalized } = safeProjectPath(body.projectPath || 'project/led_effects_demo');
  const port = body.port || defaultSerialPort();
  const timeoutMs = Number(body.timeoutMs || 15000);
  const resolved = scriptRunner('monitor', [
    path.join(sdkRoot, normalized),
    port,
    String(Math.ceil(timeoutMs / 1000))
  ]);
  const result = await runTool(resolved.tool, resolved.args, sdkRoot, timeoutMs, {
    okOnTimeout: true,
    maxOutputBytes: Number(config.execution?.maxOutputBytes || 200000),
    spawnOptions: resolved.spawnOptions
  });
  const diagnostics = extractMonitorDiagnostics(result);
  sendJson(res, 200, {
    ok: true,
    tool: 'monitor-diagnose',
    toolOk: !diagnostics.hasErrors,
    command: formatCommand(resolved.commandParts),
    result,
    diagnostics
  });
}

async function handleMonitorSessionStart(req, res) {
  const body = await readJson(req);
  const { normalized } = safeProjectPath(body.projectPath || 'project/led_effects_demo');
  const port = body.port || defaultSerialPort();
  const durationSeconds = Math.max(1, Math.ceil(Number(body.timeoutMs || 60000) / 1000));
  const resolved = scriptRunner('monitor', [
    path.join(sdkRoot, normalized),
    port,
    String(durationSeconds)
  ]);
  const id = `${Date.now().toString(36)}-${Math.random().toString(36).slice(2, 8)}`;
  const command = formatCommand(resolved.commandParts);
  const child = spawn(resolved.tool, resolved.args, {
    cwd: sdkRoot,
    shell: false,
    env: process.env,
    ...(resolved.spawnOptions || {})
  });
  const session = {
    id,
    child,
    command,
    status: 'running',
    code: null,
    startedAt: new Date().toISOString(),
    finishedAt: null,
    stdout: '',
    stderr: '',
    error: '',
    logLines: [],
    errorLines: [],
    logsTrimmed: false,
    errorsTrimmed: false
  };
  monitorSessions.set(id, session);

  child.stdout?.on('data', (chunk) => appendSessionLines(session, chunk, 'stdout'));
  child.stderr?.on('data', (chunk) => appendSessionLines(session, chunk, 'stderr'));
  child.on('error', (error) => {
    session.error = error.message;
    session.errorLines.push(`ERROR: ${error.message}`);
    session.status = 'failed';
    session.code = -1;
    session.finishedAt = new Date().toISOString();
  });
  child.on('close', (code) => {
    session.status = session.status === 'stopped' ? 'stopped' : 'completed';
    session.code = code;
    session.finishedAt = new Date().toISOString();
  });

  sendJson(res, 200, {
    ok: true,
    tool: 'monitor-session',
    ...monitorSessionPayload(session)
  });
}

async function handleMonitorSessionStatus(req, res) {
  const body = await readJson(req);
  const id = String(body.id || '').trim();
  const session = monitorSessions.get(id);
  if (!session) {
    sendError(res, 404, 'monitor session not found');
    return;
  }
  sendJson(res, 200, {
    ok: true,
    tool: 'monitor-session',
    ...monitorSessionPayload(session)
  });
}

async function handleMonitorSessionStop(req, res) {
  const body = await readJson(req);
  const id = String(body.id || '').trim();
  const session = monitorSessions.get(id);
  if (!session) {
    sendError(res, 404, 'monitor session not found');
    return;
  }
  if (session.status === 'running') {
    session.status = 'stopped';
    const stopped = await stopChildProcess(session.child);
    if (!stopped) {
      session.errors.push('Failed to stop monitor process');
    }
  }
  sendJson(res, 200, {
    ok: true,
    tool: 'monitor-session',
    ...monitorSessionPayload(session)
  });
}

async function handlePidTuneStart(req, res) {
  const body = await readJson(req);
  const port = body.port || defaultSerialPort();
  const script = path.join(sdkRoot, 'tools', 'pid_tune.py');
  const python = process.env.PYTHON || (process.platform === 'win32' ? 'python' : 'python3');
  const args = [
    script,
    '--port', port,
    '--baud', String(Math.max(1, Math.round(numberOrDefault(body.baud, 115200)))),
    '--setpoint', String(numberOrDefault(body.setpoint, 100)),
    '--bias', String(numberOrDefault(body.bias, 0)),
    '--relay', String(numberOrDefault(body.relay, 30)),
    '--duration', String(Math.max(1, numberOrDefault(body.duration, 30))),
    '--sample-ms', String(Math.max(20, Math.round(numberOrDefault(body.sampleMs, 100)))),
    '--command-template', String(body.commandTemplate || 'LOOP {value_int}'),
    '--speed-regex', String(body.speedRegex || 'speed[:=]\\s*(?P<speed>-?\\d+(?:\\.\\d+)?)')
  ];

  const id = `${Date.now().toString(36)}-${Math.random().toString(36).slice(2, 8)}`;
  const command = formatCommand([python, ...args]);
  const child = spawn(python, args, {
    cwd: sdkRoot,
    shell: false,
    env: process.env
  });
  const session = {
    id,
    child,
    command,
    status: 'running',
    code: null,
    startedAt: new Date().toISOString(),
    finishedAt: null,
    stdout: '',
    stderr: '',
    logs: [],
    errors: [],
    tx: [],
    samples: [],
    result: null,
    logsTrimmed: false,
    samplesTrimmed: false
  };
  pidTuneSessions.set(id, session);

  child.stdout?.on('data', (chunk) => appendPidOutput(session, chunk, 'stdout'));
  child.stderr?.on('data', (chunk) => appendPidOutput(session, chunk, 'stderr'));
  child.on('error', (error) => {
    session.errors.push(error.message);
    session.status = 'failed';
    session.code = -1;
    session.finishedAt = new Date().toISOString();
  });
  child.on('close', (code) => {
    session.status = session.status === 'stopped' ? 'stopped' : 'completed';
    session.code = code;
    session.finishedAt = new Date().toISOString();
  });

  sendJson(res, 200, { ok: true, tool: 'pid-tune', ...pidSessionPayload(session) });
}

async function handlePidTuneStatus(req, res) {
  const body = await readJson(req);
  const id = String(body.id || '').trim();
  const session = pidTuneSessions.get(id);
  if (!session) {
    sendError(res, 404, 'pid tune session not found');
    return;
  }
  sendJson(res, 200, { ok: true, tool: 'pid-tune', ...pidSessionPayload(session) });
}

async function handlePidTuneStop(req, res) {
  const body = await readJson(req);
  const id = String(body.id || '').trim();
  const session = pidTuneSessions.get(id);
  if (!session) {
    sendError(res, 404, 'pid tune session not found');
    return;
  }
  if (session.status === 'running') {
    session.status = 'stopped';
    const stopped = await stopChildProcess(session.child);
    if (!stopped) {
      session.errors.push('Failed to stop PID tune process');
    }
  }
  sendJson(res, 200, { ok: true, tool: 'pid-tune', ...pidSessionPayload(session) });
}

async function handleSerialPorts(_req, res) {
  const python = process.env.PYTHON || (process.platform === 'win32' ? 'python' : 'python3');
  const code = [
    'import json',
    'try:',
    '    from serial.tools import list_ports',
    '    ports = [{"path": p.device, "description": p.description or "", "hwid": p.hwid or ""} for p in list_ports.comports()]',
    '    print(json.dumps({"ok": True, "ports": ports}))',
    'except Exception as exc:',
    '    print(json.dumps({"ok": False, "error": str(exc), "ports": []}))'
  ].join('\n');
  const result = await runTool(python, ['-c', code], sdkRoot, 5000, {
    maxOutputBytes: 20000
  });
  try {
    const payload = JSON.parse(String(result.stdout || '').trim() || '{}');
    sendJson(res, 200, {
      ok: true,
      ports: Array.isArray(payload.ports) ? payload.ports : [],
      warning: payload.ok === false ? payload.error : undefined,
      defaultSerialPort: defaultSerialPort()
    });
  } catch (error) {
    sendJson(res, 200, {
      ok: true,
      ports: [],
      warning: result.stderr || result.error || error.message,
      defaultSerialPort: defaultSerialPort()
    });
  }
}

async function handleApi(req, res, pathname) {
  try {
    if (req.method === 'GET' && pathname === '/api/health') {
      sendJson(res, 200, {
        ok: true,
        version: '0.1.0',
        sdkRoot,
        agentRoot,
        platform: process.platform,
        defaultSerialPort: defaultSerialPort(),
        defaultProvider: config.defaultProvider,
        providers: listProviderTemplates(config),
        execution: publicExecutionConfig(),
        physical: getPhysicalStatus()
      });
      return;
    }

    if (req.method === 'GET' && pathname === '/api/sdk/summary') {
      sendJson(res, 200, { ok: true, summary: await getSdkSummary() });
      return;
    }

    if (req.method === 'GET' && pathname === '/api/sdk/resources') {
      const url = new URL(req.url, 'http://localhost');
      const sdkSummary = await getSdkSummary();
      const boardName = url.searchParams.get('board') || url.searchParams.get('boardName') || defaultBoardName();
      const sdkResources = loadSdkResourcesForBoard(sdkSummary, boardName);
      sendJson(res, 200, {
        ok: true,
        board: sdkResources.board,
        resources: sdkResources.boardResources,
        componentBusResources: sdkResources.componentBusResources,
        defines: sdkResources.defines
      });
      return;
    }

    if (req.method === 'GET' && pathname === '/api/prompts') {
      sendJson(res, 200, { ok: true, prompts: await listPromptDocuments() });
      return;
    }

    if (req.method === 'POST' && pathname === '/api/prompts/read') {
      const body = await readJson(req);
      sendJson(res, 200, { ok: true, prompt: await readPromptDocument(body.promptPath) });
      return;
    }

    if (req.method === 'POST' && pathname === '/api/sdk/refresh') {
      sdkSummaryCache = await scanSdk(sdkRoot);
      sdkSummaryCacheSignature = await sdkScanSignature();
      sendJson(res, 200, { ok: true, summary: sdkSummaryCache });
      return;
    }

    if (req.method === 'GET' && pathname === '/api/sessions') {
      sendJson(res, 200, { ok: true, sessions: await loadSessions(agentRoot) });
      return;
    }

    const sessionMatch = pathname.match(/^\/api\/sessions\/([^/]+)\/?$/);
    if (req.method === 'GET' && sessionMatch) {
      const session = await loadSession(agentRoot, sessionMatch[1]);
      if (!session) {
        sendError(res, 404, 'session not found');
        return;
      }
      sendJson(res, 200, { ok: true, session });
      return;
    }

    if (req.method === 'DELETE' && sessionMatch) {
      const deleted = await deleteSession(agentRoot, sessionMatch[1]);
      if (!deleted) {
        sendError(res, 404, 'session not found');
        return;
      }
      sendJson(res, 200, { ok: true });
      return;
    }

    if (req.method === 'POST' && pathname === '/api/sessions') {
      const body = await readJson(req);
      const session = await saveSession(agentRoot, body);
      sendJson(res, 200, { ok: true, session });
      return;
    }

    const eventMatch = pathname.match(/^\/api\/sessions\/([^/]+)\/events\/?$/);
    if (req.method === 'POST' && eventMatch) {
      const body = await readJson(req);
      const session = await appendSessionEvent(agentRoot, eventMatch[1], body);
      if (!session) {
        sendError(res, 404, 'session not found');
        return;
      }
      sendJson(res, 200, { ok: true, session });
      return;
    }

    if (req.method === 'DELETE' && eventMatch) {
      const session = await clearSessionTimeline(agentRoot, eventMatch[1]);
      if (!session) {
        sendError(res, 404, 'session not found');
        return;
      }
      sendJson(res, 200, { ok: true, session });
      return;
    }

    const singleEventMatch = pathname.match(/^\/api\/sessions\/([^/]+)\/events\/([^/]+)\/?$/);
    if (req.method === 'DELETE' && singleEventMatch) {
      const session = await deleteSessionEvent(agentRoot, singleEventMatch[1], singleEventMatch[2]);
      if (!session) {
        sendError(res, 404, 'session not found');
        return;
      }
      sendJson(res, 200, { ok: true, session });
      return;
    }

    if (req.method === 'POST' && pathname === '/api/agent/hardware-build') {
      await handleHardwareBuild(req, res);
      return;
    }

    if (req.method === 'POST' && pathname === '/api/agent/analyze') {
      await handleAnalyze(req, res);
      return;
    }

    if (req.method === 'POST' && pathname === '/api/agent/component-selection') {
      await handleComponentSelection(req, res);
      return;
    }

    if (req.method === 'POST' && pathname === '/api/agent/hardware-resource-planning') {
      await handleHardwareResourcePlanning(req, res);
      return;
    }

    if (req.method === 'POST' && pathname === '/api/agent/chat') {
      await handleChat(req, res);
      return;
    }

    if (req.method === 'POST' && pathname === '/api/agent/codegen') {
      await handleCodegen(req, res);
      return;
    }

    if (req.method === 'POST' && pathname === '/api/agent/debug-fix') {
      await handleDebugFix(req, res);
      return;
    }

    if (req.method === 'POST' && pathname === '/api/project/write') {
      await handleProjectWrite(req, res);
      return;
    }

    if (req.method === 'POST' && pathname === '/api/tools/build') {
      await handleTool(req, res, 'build');
      return;
    }

    if (req.method === 'POST' && pathname === '/api/tools/flash') {
      await handleTool(req, res, 'flash');
      return;
    }

    if (req.method === 'POST' && pathname === '/api/tools/monitor') {
      await handleTool(req, res, 'monitor');
      return;
    }

    if (req.method === 'POST' && pathname === '/api/tools/monitor-diagnose') {
      await handleMonitorDiagnose(req, res);
      return;
    }

    if (req.method === 'POST' && pathname === '/api/tools/monitor-session/start') {
      await handleMonitorSessionStart(req, res);
      return;
    }

    if (req.method === 'POST' && pathname === '/api/tools/monitor-session/status') {
      await handleMonitorSessionStatus(req, res);
      return;
    }

    if (req.method === 'POST' && pathname === '/api/tools/monitor-session/stop') {
      await handleMonitorSessionStop(req, res);
      return;
    }

    if (req.method === 'POST' && pathname === '/api/tools/pid-tune/start') {
      await handlePidTuneStart(req, res);
      return;
    }

    if (req.method === 'POST' && pathname === '/api/tools/pid-tune/status') {
      await handlePidTuneStatus(req, res);
      return;
    }

    if (req.method === 'POST' && pathname === '/api/tools/pid-tune/stop') {
      await handlePidTuneStop(req, res);
      return;
    }

    if (req.method === 'GET' && pathname === '/api/tools/serial-ports') {
      await handleSerialPorts(req, res);
      return;
    }

    if (req.method === 'POST' && pathname === '/api/tools/exec') {
      await handleExec(req, res);
      return;
    }

    if (req.method === 'GET' && pathname === '/api/physical/status') {
      sendJson(res, 200, { ok: true, physical: getPhysicalStatus() });
      return;
    }

    sendError(res, 404, 'api route not found');
  } catch (error) {
    sendError(res, 500, error.message, error.stack);
  }
}

const server = http.createServer(async (req, res) => {
  const url = new URL(req.url, `http://${req.headers.host || 'localhost'}`);
  const pathname = decodeURIComponent(url.pathname);

  if (pathname.startsWith('/api/')) {
    await handleApi(req, res, pathname);
    return;
  }

  if (pathname.startsWith('/vendor/mermaid/')) {
    await serveStaticFromRoot(res, mermaidRoot, pathname.slice('/vendor/mermaid/'.length));
    return;
  }

  await serveStatic(req, res, pathname);
});

server.on('error', (error) => {
  if (error.code === 'EADDRINUSE') {
    console.error(`Moce Hardware Agent failed to start: ${config.host}:${config.port} is already in use.`);
    console.error('Close the existing agent process, or start this one on another port:');
    console.error(`  MOCE_AGENT_PORT=${Number(config.port) + 1} npm --prefix agent start`);
    process.exit(1);
  }
  throw error;
});

server.listen(config.port, config.host, () => {
  console.log(`Moce Hardware Agent running at http://${config.host}:${config.port}`);
  console.log(`SDK root: ${sdkRoot}`);
});
