import mermaid from '/vendor/mermaid/mermaid.esm.mjs';

mermaid.initialize({
  startOnLoad: false,
  securityLevel: 'strict',
  theme: 'neutral',
  flowchart: {
    htmlLabels: false,
    curve: 'basis'
  }
});

let diagramRenderSequence = 0;
let stateMachineRenderSequence = 0;

const state = {
  health: null,
  sdk: null,
  analysis: '',
  analysisAccepted: false,
  analysisRefinementDirty: false,
  componentSelection: '',
  componentSelectionAccepted: false,
  componentSelectionDirty: false,
  resourcePlan: '',
  resourcePlanAccepted: false,
  resourcePlanDirty: false,
  plan: '',
  files: [],
  validation: null,
  selectedFilePath: '',
  implementationFeedbackApplied: false,
  implementationFeedbackDirty: false,
  currentSessionId: null,
  activity: [],
  prompts: [],
  tools: {
    build: null,
    flash: null,
    monitor: null
  },
  lastToolResult: null,
  debugFix: null,
  sdkResources: null,
  sessions: [],
  timeline: [],
  autosave: true
};

let monitorPollTimer = null;
let activeMonitorId = '';
let pidTunePollTimer = null;
let activePidTuneId = '';
let requirementInputTimer = null;
let lightweightRefreshTimer = null;

const defaults = {
  projectName: 'oled_096_readout',
  targetName: 'ch32',
  boardName: 'ch32v203g6u6'
};

const steps = [
  '提出产品需求',
  '功能分析',
  '器件选型',
  '硬件资源规划',
  '硬件搭建',
  '嵌入式开发',
  '测试调试',
  '实现情况反馈'
];

const reservedSteps = [
  '接入 physical',
  '结构设计装配',
  '整机联调',
  'physical vibe code',
  '电路板量产设计'
];

const hardwareKeywords = [
  ['ToF', /tof|vl53|测距|distance/i],
  ['OLED', /oled|display|显示|屏/i],
  ['Servo', /servo|舵机|steer|scan/i],
  ['Motor', /motor|tb6612|电机|马达|小车|驱动轮/i],
  ['Encoder', /encoder|编码器|码盘|测速|ab\s*相/i],
  ['Button', /button|按键|按钮/i],
  ['WiFi', /wifi|tcp|udp|上位机|host/i],
  ['LED', /(^|[^a-z])led([^a-z]|$)|灯/i]
];

const $ = (selector) => document.querySelector(selector);

function escapeHtml(value) {
  return String(value || '')
    .replace(/&/g, '&amp;')
    .replace(/</g, '&lt;')
    .replace(/>/g, '&gt;')
    .replace(/"/g, '&quot;')
    .replace(/'/g, '&#39;');
}

function escapeAttr(value) {
  return escapeHtml(value);
}

function renderInlineMarkdown(value) {
  return escapeHtml(value)
    .replace(/&lt;u&gt;([^<]+)&lt;\/u&gt;/gi, '<u>$1</u>')
    .replace(/&lt;ins&gt;([^<]+)&lt;\/ins&gt;/gi, '<ins>$1</ins>')
    .replace(/`([^`]+)`/g, '<code>$1</code>')
    .replace(/\*\*([^*]+)\*\*/g, '<strong>$1</strong>')
    .replace(/__([^_]+)__/g, '<strong>$1</strong>')
    .replace(/\*([^*\n]+)\*/g, '<em>$1</em>')
    .replace(/_([^_\n]+)_/g, '<em>$1</em>')
    .replace(/\[([^\]]+)\]\((https?:\/\/[^)\s]+)\)/g, '<a href="$2" target="_blank" rel="noreferrer">$1</a>');
}

function isMarkdownTableStart(lines, index) {
  return index + 1 < lines.length
    && lines[index].includes('|')
    && /^\s*\|?\s*:?-{3,}/.test(lines[index + 1]);
}

function renderMarkdownTable(tableLines) {
  const rows = tableLines
    .filter((line, index) => index !== 1)
    .map((line) => line.trim().replace(/^\|/, '').replace(/\|$/, '').split('|').map((cell) => cell.trim()));
  if (rows.length === 0) return '';
  const head = rows[0];
  const body = rows.slice(1);
  return `
    <table>
      <thead><tr>${head.map((cell) => `<th>${renderInlineMarkdown(cell)}</th>`).join('')}</tr></thead>
      <tbody>${body.map((row) => `<tr>${row.map((cell) => `<td>${renderInlineMarkdown(cell)}</td>`).join('')}</tr>`).join('')}</tbody>
    </table>
  `;
}

function markdownToHtml(markdown) {
  const lines = String(markdown || '').replace(/\r\n/g, '\n').split('\n');
  const html = [];
  let index = 0;

  function isBlockStart(line, cursor) {
    return !line.trim()
      || /^#{1,6}\s+/.test(line)
      || /^```/.test(line)
      || /^>\s?/.test(line)
      || /^\s*[-*+]\s+/.test(line)
      || /^\s*\d+\.\s+/.test(line)
      || isMarkdownTableStart(lines, cursor);
  }

  while (index < lines.length) {
    const line = lines[index];
    if (!line.trim()) {
      index += 1;
      continue;
    }

    const fence = line.match(/^```\s*([A-Za-z0-9_-]*)\s*$/);
    if (fence) {
      const language = fence[1] || 'text';
      const codeLines = [];
      index += 1;
      while (index < lines.length && !/^```\s*$/.test(lines[index])) {
        codeLines.push(lines[index]);
        index += 1;
      }
      if (index < lines.length) index += 1;
      html.push(`<pre class="code-block language-${escapeAttr(language)}"><code>${escapeHtml(codeLines.join('\n'))}</code></pre>`);
      continue;
    }

    if (isMarkdownTableStart(lines, index)) {
      const tableLines = [lines[index], lines[index + 1]];
      index += 2;
      while (index < lines.length && lines[index].includes('|')) {
        tableLines.push(lines[index]);
        index += 1;
      }
      html.push(renderMarkdownTable(tableLines));
      continue;
    }

    const heading = line.match(/^(#{1,6})\s+(.+)$/);
    if (heading) {
      const level = Math.min(heading[1].length, 6);
      html.push(`<h${level}>${renderInlineMarkdown(heading[2].trim())}</h${level}>`);
      index += 1;
      continue;
    }

    if (/^>\s?/.test(line)) {
      const quoteLines = [];
      while (index < lines.length && /^>\s?/.test(lines[index])) {
        quoteLines.push(lines[index].replace(/^>\s?/, ''));
        index += 1;
      }
      html.push(`<blockquote>${markdownToHtml(quoteLines.join('\n'))}</blockquote>`);
      continue;
    }

    const unordered = line.match(/^\s*[-*+]\s+(.+)$/);
    const ordered = line.match(/^\s*\d+\.\s+(.+)$/);
    if (unordered || ordered) {
      const tag = unordered ? 'ul' : 'ol';
      const items = [];
      const itemPattern = unordered ? /^\s*[-*+]\s+(.+)$/ : /^\s*\d+\.\s+(.+)$/;
      while (index < lines.length) {
        const item = lines[index].match(itemPattern);
        if (!item) break;
        items.push(`<li>${renderInlineMarkdown(item[1].trim())}</li>`);
        index += 1;
      }
      html.push(`<${tag}>${items.join('')}</${tag}>`);
      continue;
    }

    const paragraph = [line.trim()];
    index += 1;
    while (index < lines.length && !isBlockStart(lines[index], index)) {
      paragraph.push(lines[index].trim());
      index += 1;
    }
    html.push(`<p>${renderInlineMarkdown(paragraph.join(' '))}</p>`);
  }

  return html.join('');
}

function setStatus(text, kind = '') {
  const el = $('#healthLine');
  el.className = kind ? `status-${kind}` : '';
  el.textContent = text;
}

function logActivity(message, kind = '') {
  const at = new Date().toLocaleTimeString();
  state.activity.unshift({ at, message, kind });
  state.activity = state.activity.slice(0, 40);
  $('#logCount').textContent = String(state.activity.length);
  $('#activityLog').innerHTML = state.activity.map((entry) => `
    <div class="log-entry ${entry.kind}">
      <strong>${entry.at}</strong> ${escapeHtml(entry.message)}
    </div>
  `).join('');
}

async function api(path, options = {}) {
  const response = await fetch(path, {
    headers: {
      'content-type': 'application/json',
      ...(options.headers || {})
    },
    ...options,
    body: options.body ? JSON.stringify(options.body) : undefined
  });
  const data = await response.json();
  if (!response.ok || data.ok === false) {
    throw new Error(data.error || response.statusText);
  }
  return data;
}

function providerRequest() {
  const selected = state.health?.providers?.find((item) => item.name === $('#providerName').value);
  return {
    name: $('#providerName').value,
    kind: selected?.kind || 'openai-compatible',
    model: $('#modelName').value.trim(),
    baseUrl: $('#baseUrl').value.trim(),
    apiKey: $('#apiKey').value.trim()
  };
}

function currentPayload() {
  return {
    requirement: $('#requirement').value.trim(),
    promptPath: $('#promptPath')?.value || '',
    projectName: $('#projectName').value.trim(),
    target: $('#targetName').value.trim(),
    board: $('#boardName').value.trim(),
    port: $('#portName').value.trim(),
    useLlm: $('#useLlm').checked,
    provider: providerRequest()
  };
}

function projectNameFromRequirement(text) {
  const match = String(text || '').match(/\bproject\/([A-Za-z0-9_-]+)\//);
  return match?.[1] || '';
}

function preferredProjectName() {
  return projectNameFromRequirement($('#requirement')?.value || '') || $('#projectName').value.trim();
}

function sessionTitle() {
  return preferredProjectName() || 'Moce hardware session';
}

function currentStage() {
  if (state.tools.build?.ok && state.tools.flash?.ok && state.tools.monitor?.ok && state.implementationFeedbackApplied && !state.implementationFeedbackDirty) return '实现情况反馈';
  if (state.tools.build?.ok && state.tools.flash?.ok && state.tools.monitor?.ok) return '测试调试';
  if (state.files.length > 0) return '嵌入式开发';
  if (state.plan) return '硬件搭建';
  if (state.resourcePlanAccepted) return '硬件资源规划';
  if (state.componentSelectionAccepted) return '器件选型';
  if (state.analysisAccepted) return '功能分析';
  return $('#requirement')?.value.trim() ? '提出产品需求' : '未开始';
}

function snapshotState() {
  const payload = currentPayload();
  return {
    ...payload,
    requirement: $('#requirement')?.value || '',
    refinement: $('#analysisRefinement')?.value || '',
    componentSelectionNotes: $('#componentSelectionNotes')?.value || '',
    resourcePlanNotes: $('#resourcePlanNotes')?.value || '',
    stage: currentStage(),
    analysis: state.analysis,
    analysisAccepted: state.analysisAccepted,
    analysisRefinementDirty: state.analysisRefinementDirty,
    componentSelection: state.componentSelection,
    componentSelectionAccepted: state.componentSelectionAccepted,
    componentSelectionDirty: state.componentSelectionDirty,
    resourcePlan: state.resourcePlan,
    resourcePlanAccepted: state.resourcePlanAccepted,
    resourcePlanDirty: state.resourcePlanDirty,
    plan: state.plan,
    files: state.files,
    validation: state.validation,
    selectedFilePath: state.selectedFilePath,
    implementationFeedback: $('#implementationFeedback')?.value || '',
    implementationFeedbackApplied: state.implementationFeedbackApplied,
    implementationFeedbackDirty: state.implementationFeedbackDirty,
    tools: state.tools,
    lastToolResult: state.lastToolResult,
    debugFix: state.debugFix
  };
}

function restoreSnapshot(snapshot = {}) {
  $('#projectName').value = snapshot.projectName || defaults.projectName;
  $('#targetName').value = snapshot.target || defaults.targetName;
  $('#boardName').value = snapshot.board || defaults.boardName;
  $('#portName').value = snapshot.port || state.health?.defaultSerialPort || '/dev/ttyUSB0';
  $('#requirement').value = snapshot.requirement || '';
  if ($('#analysisRefinement')) $('#analysisRefinement').value = snapshot.refinement || '';
  if ($('#componentSelectionNotes')) $('#componentSelectionNotes').value = snapshot.componentSelectionNotes || '';
  if ($('#resourcePlanNotes')) $('#resourcePlanNotes').value = snapshot.resourcePlanNotes || '';
  if ($('#implementationFeedback')) $('#implementationFeedback').value = snapshot.implementationFeedback || '';
  state.analysis = snapshot.analysis || '';
  state.analysisAccepted = !!snapshot.analysisAccepted;
  state.analysisRefinementDirty = !!snapshot.analysisRefinementDirty;
  state.componentSelection = snapshot.componentSelection || '';
  state.componentSelectionAccepted = !!snapshot.componentSelectionAccepted;
  state.componentSelectionDirty = !!snapshot.componentSelectionDirty;
  state.resourcePlan = snapshot.resourcePlan || '';
  state.resourcePlanAccepted = !!snapshot.resourcePlanAccepted;
  state.resourcePlanDirty = !!snapshot.resourcePlanDirty;
  state.plan = snapshot.plan || '';
  state.files = Array.isArray(snapshot.files) ? snapshot.files : [];
  state.validation = snapshot.validation || null;
  state.selectedFilePath = snapshot.selectedFilePath || '';
  state.implementationFeedbackApplied = !!snapshot.implementationFeedbackApplied;
  state.implementationFeedbackDirty = !!snapshot.implementationFeedbackDirty;
  state.tools = snapshot.tools || { build: null, flash: null, monitor: null };
  state.lastToolResult = snapshot.lastToolResult || null;
  state.debugFix = snapshot.debugFix || null;
  renderAnalysis();
  renderComponentSelection();
  renderResources();
  renderPlan();
  renderDiagram();
  renderFiles();
  renderFeedback();
  renderDebugFix();
  renderSteps();
  updateWorkflowControls();
}

function refinedRequirementText() {
  const base = $('#requirement').value.trim();
  const refinement = $('#analysisRefinement')?.value.trim() || '';
  if (!refinement) {
    return base;
  }
  return [
    base,
    '',
    '【进一步细化需求】',
    refinement
  ].filter(Boolean).join('\n');
}

function firmwareRequirementText() {
  const base = refinedRequirementText();
  const feedback = $('#implementationFeedback')?.value.trim() || '';
  if (!feedback) {
    return base;
  }
  return [
    base,
    '',
    '【实现情况反馈：本段是调试后用户新增的功能细化要求。固件生成约束保持不变，在满足 SDK/API/板级资源约束的前提下调整之前生成的固件。】',
    feedback
  ].filter(Boolean).join('\n');
}

function updateWorkflowControls() {
  const analysisReady = state.analysisAccepted && !!state.analysis && !state.analysisRefinementDirty;
  const selectionReady = state.componentSelectionAccepted && !!state.componentSelection && !state.componentSelectionDirty;
  const resourceReady = state.resourcePlanAccepted && !!state.resourcePlan && !state.resourcePlanDirty;
  $('#componentSelectionBtn').disabled = !analysisReady;
  $('#resourcePlanBtn').disabled = !selectionReady;
  $('#hardwareBuildBtn').disabled = !resourceReady;
  const codeReady = resourceReady && !!state.plan;
  $('#codeBtn').disabled = !codeReady;
  $('#runCodeBtn').disabled = !codeReady;
  const feedbackText = $('#implementationFeedback')?.value.trim() || '';
  $('#applyFeedbackBtn').disabled = !codeReady || state.files.length === 0 || !feedbackText;
  const decision = $('#analysisDecision');
  if (decision) {
    decision.classList.toggle('hidden', !state.analysis || state.analysisAccepted);
  }
  const refinement = $('#analysisRefinementPanel');
  if (refinement) {
    refinement.classList.toggle('hidden', !state.analysis || state.analysisAccepted);
  }
  const selectionDecision = $('#componentSelectionDecision');
  if (selectionDecision) {
    selectionDecision.classList.toggle('hidden', !state.componentSelection || state.componentSelectionAccepted);
  }
  const selectionRefinement = $('#componentSelectionRefinementPanel');
  if (selectionRefinement) {
    selectionRefinement.classList.toggle('hidden', !state.componentSelection || state.componentSelectionAccepted);
  }
  const resourceDecision = $('#resourcePlanDecision');
  if (resourceDecision) {
    resourceDecision.classList.toggle('hidden', !state.resourcePlan || state.resourcePlanAccepted);
  }
  const resourceRefinement = $('#resourcePlanRefinementPanel');
  if (resourceRefinement) {
    resourceRefinement.classList.toggle('hidden', !state.resourcePlan || state.resourcePlanAccepted);
  }
}

function clearTestTools() {
  clearMonitorPoll();
  activeMonitorId = '';
  state.tools = {
    build: null,
    flash: null,
    monitor: null
  };
  state.lastToolResult = null;
  state.debugFix = null;
  $('#disconnectMonitorBtn').disabled = true;
  setToolOutputs({ monitorStatus: '未连接' });
  renderDebugFix();
}

function resetTestToolStateOnly() {
  state.tools = {
    build: null,
    flash: null,
    monitor: null
  };
  state.lastToolResult = null;
  state.debugFix = null;
}

function renderSteps() {
  let completed = 0;
  const testsOk = state.tools.build?.ok && state.tools.flash?.ok && state.tools.monitor?.ok;
  if ($('#requirement')?.value.trim()) completed = 1;
  if (state.analysisAccepted) completed = 2;
  if (state.componentSelectionAccepted) completed = 3;
  if (state.resourcePlanAccepted) completed = 4;
  if (state.plan) completed = 5;
  if (state.files.length > 0) completed = 6;
  if (testsOk) completed = 7;
  if (testsOk && state.implementationFeedbackApplied && !state.implementationFeedbackDirty) completed = 8;
  $('#flowSummary').textContent = `${completed} / ${steps.length}`;
  const activeSteps = steps.map((step, index) => {
    const done = index < completed;
    const active = index === completed || (completed === steps.length && index === steps.length - 1);
    const stateText = done ? 'done' : active ? 'now' : 'todo';
    return `
      <div class="step ${done ? 'done' : ''} ${active ? 'active' : ''}">
        <strong>${index + 1}</strong>
        <span>${step}</span>
        <em class="step-state">${stateText}</em>
      </div>
    `;
  });
  const futureSteps = reservedSteps.map((step, index) => `
    <div class="step reserved">
      <strong>${steps.length + index + 1}</strong>
      <span>${step}</span>
      <em class="step-state">reserved</em>
    </div>
  `);
  $('#steps').innerHTML = [...activeSteps, ...futureSteps].join('');
}

function renderProviders() {
  const providers = state.health?.providers || [];
  $('#providerName').innerHTML = providers.map((provider) => `
    <option value="${provider.name}">${provider.name}</option>
  `).join('');
  const preferred = providers.find((provider) => provider.name === state.health?.defaultProvider)
    || providers[0];
  if (preferred) {
    $('#providerName').value = preferred.name;
    $('#modelName').value = preferred.model || '';
    $('#baseUrl').value = preferred.baseUrl || '';
    updateModelChip();
  }
}

function renderPromptOptions() {
  const select = $('#promptPath');
  if (!select) return;

  if (state.prompts.length === 0) {
    select.innerHTML = '<option value="">prompt/ 中暂无需求文档</option>';
    $('#loadPromptBtn').disabled = true;
    $('#promptStatus').textContent = 'prompt0 默认生效';
    return;
  }

  select.innerHTML = [
    '<option value="">手动输入</option>',
    ...state.prompts.map((prompt) => {
      const label = prompt.directory ? `${prompt.directory}/${prompt.name}` : prompt.name;
      return `<option value="${escapeAttr(prompt.path)}">${escapeHtml(label)}</option>`;
    })
  ].join('');
  $('#loadPromptBtn').disabled = false;
  $('#promptStatus').textContent = `${state.prompts.length} docs, prompt0 默认生效`;
}

function updateModelChip() {
  const model = $('#modelName')?.value || '';
  const provider = $('#providerName')?.value || '';
  const label = provider ? `${provider}${model ? ` / ${model}` : ''}` : '未连接';
  $('#modelChip').textContent = `Model: ${label}`;
  $('#providerStatus').textContent = $('#useLlm')?.checked ? 'LLM' : 'Fallback';
}

function renderSdk() {
  const sdkGrid = $('#sdkGrid');
  if (!sdkGrid) {
    return;
  }
  if (!state.sdk) {
    sdkGrid.innerHTML = '<div class="sdk-card"><h4>SDK</h4><p>尚未扫描。</p></div>';
    return;
  }

  const components = state.sdk.components || [];
  const boards = state.sdk.boards || [];
  sdkGrid.innerHTML = `
    <div class="sdk-card">
      <h4>Components</h4>
      <p>${components.length} 个组件</p>
      <div class="sdk-list">${components.map((item) => `<span class="pill">${item.name}</span>`).join('')}</div>
    </div>
    <div class="sdk-card">
      <h4>BSP</h4>
      <p>${escapeHtml((state.sdk.bsp || []).join(', ') || '无')}</p>
    </div>
    <div class="sdk-card">
      <h4>Boards</h4>
      <div class="sdk-list">${boards.map((item) => `<span class="pill">${item.name}</span>`).join('')}</div>
    </div>
    <div class="sdk-card">
      <h4>Examples</h4>
      <div class="sdk-list">${(state.sdk.examples || []).map((name) => `<span class="pill">${name}</span>`).join('')}</div>
    </div>
  `;
}

function extractMermaid(markdown) {
  const text = String(markdown || '').replace(/\r\n/g, '\n');
  const match = text.match(/```\s*mermaid\s*([\s\S]*?)```/i);
  if (match) return match[1].trim();

  const lines = text.split('\n');
  const start = lines.findIndex((line) => /^\s*(flowchart|graph)\s+(TB|TD|BT|RL|LR)\b/i.test(line));
  if (start < 0) return '';
  const collected = [];
  for (let index = start; index < lines.length; index += 1) {
    const line = lines[index];
    if (index > start && /^#{1,6}\s+/.test(line)) break;
    if (index > start && line.trim() === '' && collected.length > 1) break;
    collected.push(line);
  }
  return collected.join('\n').trim();
}

function extractStateMachineMermaid(markdown) {
  const text = String(markdown || '').replace(/\r\n/g, '\n');
  const fencedBlocks = [...text.matchAll(/```\s*[a-zA-Z0-9_-]*\s*([\s\S]*?)```/gi)]
    .map((match) => match[1].trim());
  const fencedState = fencedBlocks.find((block) => /\bstateDiagram(?:-v2)?\b/i.test(block));
  if (fencedState) {
    const lines = fencedState.split('\n');
    const start = lines.findIndex((line) => /\bstateDiagram(?:-v2)?\b/i.test(line));
    return lines.slice(Math.max(0, start)).join('\n').trim();
  }

  const lines = text.split('\n');
  const start = lines.findIndex((line) => /\bstateDiagram(?:-v2)?\b/i.test(line));
  if (start < 0) return stateMachineFromTransitionTable(text);
  const collected = [];
  for (let index = start; index < lines.length; index += 1) {
    const line = lines[index];
    if (index > start && /^#{1,6}\s+/.test(line)) break;
    if (index > start && line.trim() === '' && collected.length > 1) break;
    collected.push(line);
  }
  return collected.join('\n').trim();
}

function stateMachineFromNamedStates(markdown) {
  const text = String(markdown || '');
  if (/\bSAFE\b/.test(text) && /\bALERT\b/.test(text)) {
    return [
      'flowchart LR',
      '  START([START]) --> BOOT[STARTUP]',
      '  BOOT -->|ready| SAFE[SAFE]',
      '  SAFE -->|trigger| ALERT[ALERT]',
      '  ALERT -->|clear| SAFE'
    ].join('\n');
  }
  return '';
}

function normalizeStateName(value, fallback) {
  const raw = String(value || '').trim();
  if (!raw || raw === '-' || raw === '暂无') return fallback;
  return raw.replace(/[`*_|]/g, '').trim() || fallback;
}

function parseMarkdownRow(line) {
  return String(line || '')
    .trim()
    .replace(/^\|/, '')
    .replace(/\|$/, '')
    .split('|')
    .map((cell) => cell.trim());
}

function stateMachineFromTransitionTable(markdown) {
  const text = String(markdown || '').replace(/\r\n/g, '\n');
  const sectionMatch = text.match(/(?:^|\n)#{2,5}\s*(?:状态转移条件|状态转换条件|状态迁移|转移条件)[^\n]*\n([\s\S]*?)(?=\n#{2,5}\s+|$)/i);
  const section = sectionMatch ? sectionMatch[1] : text;
  const lines = section.split('\n');

  for (let index = 0; index < lines.length - 1; index += 1) {
    if (!lines[index].includes('|') || !/^\s*\|?\s*:?-{3,}/.test(lines[index + 1])) continue;
    const headers = parseMarkdownRow(lines[index]).map((header) => header.toLowerCase());
    const fromIndex = headers.findIndex((header) => /^(from|当前|起始|源|原状态|开始)/i.test(header));
    const toIndex = headers.findIndex((header) => /^(to|目标|下一|新状态|结束)/i.test(header));
    const conditionIndex = headers.findIndex((header) => /(条件|事件|触发|condition|event)/i.test(header));
    if (fromIndex < 0 || toIndex < 0) continue;

    const transitions = [];
    const stateIds = new Map();
    const getStateId = (label) => {
      if (!stateIds.has(label)) {
        stateIds.set(label, `S${stateIds.size + 1}`);
      }
      return stateIds.get(label);
    };
    for (let cursor = index + 2; cursor < lines.length && lines[cursor].includes('|'); cursor += 1) {
      const cells = parseMarkdownRow(lines[cursor]);
      const from = normalizeStateName(cells[fromIndex], '');
      const to = normalizeStateName(cells[toIndex], '');
      if (!from || !to || from === '暂无' || to === '暂无') continue;
      getStateId(from);
      getStateId(to);
      const condition = String(cells[conditionIndex] || '').replace(/[|`]/g, '').trim();
      transitions.push({ from, to, condition });
    }

    if (transitions.length > 0) {
      const declarations = [...stateIds.entries()].map(([label, id]) => `  ${id}: ${label.slice(0, 24)}`);
      const first = getStateId(transitions[0].from);
      const body = transitions.map(({ from, to, condition }) => {
        const label = condition && condition !== '-' ? `: ${condition.slice(0, 28)}` : '';
        return `  ${getStateId(from)} --> ${getStateId(to)}${label}`;
      });
      return ['stateDiagram-v2', ...declarations, `  [*] --> ${first}`, ...body].join('\n');
    }
  }

  return stateMachineFromNamedStates(text);
}

function hardwareBuildDetails(markdown) {
  return String(markdown || '')
    .replace(/\r\n/g, '\n')
    .replace(/(?:^|\n)#{2,4}\s+模块连接框图\s*\n+(?:```\s*mermaid\s*\n[\s\S]*?\n```\s*)?/i, '\n')
    .trim();
}

function compactMermaidLabels(source) {
  return String(source || '')
    .replaceAll('BOARD_I2C_SDA_GPIO', 'SDA')
    .replaceAll('BOARD_I2C_SCL_GPIO', 'SCL')
    .replaceAll('BOARD_I2C_FREQUENCY_HZ', 'FREQ')
    .replaceAll('BOARD_OLED_I2C_ADDRESS', 'A')
    .replaceAll('BOARD_UART_TX_GPIO', 'TX')
    .replaceAll('BOARD_UART_RX_GPIO', 'RX')
    .replaceAll('BOARD_UART_BAUD_RATE', 'BAUD')
    .replaceAll('BOARD_BUTTON_GPIO', 'BTN')
    .replaceAll('BOARD_BUTTON_ACTIVE_LEVEL', 'LEVEL')
    .replaceAll('BOARD_LED_GPIO', 'LED')
    .replaceAll('BOARD_LED_PWM_CHANNEL', 'CH')
    .replaceAll('BOARD_LED_PWM_FREQUENCY_HZ', 'FREQ')
    .replaceAll('BOARD_SERVO_GPIO_0', 'S0')
    .replaceAll('BOARD_SERVO_GPIO_1', 'S1')
    .replaceAll('BOARD_SERVO_PWM_CHANNEL_0', 'CH0')
    .replaceAll('BOARD_SERVO_PWM_CHANNEL_1', 'CH1')
    .replaceAll('BOARD_SERVO_PWM_FREQUENCY_HZ', 'FREQ')
    .replaceAll('DRIVER_TOF2000C_VL53L0X_I2C_ADDR_DEFAULT', 'A')
    .replace(/\bI2C address\s+/g, 'A=')
    .replace(/\bBOARD_MOTOR_/g, 'M_')
    .replace(/\bBOARD_ENCODER_/g, 'ENC_')
    .replace(/\bI2C:\s*/g, '')
    .replace(/\bUART:\s*/g, '')
    .replace(/\bLED:\s*/g, '')
    .replace(/\bServo:\s*/g, '')
    .replace(/\/(?:3\.3V|5V|VCC|供电)\/GND/g, '')
    .replace(/,\s*FREQ=[^;"]+/g, '')
    .replace(/,\s*BAUD=([^;"]+)/g, ' @ $1');
}

function simplifyDiagramPowerNetwork(source) {
  const text = String(source || '');
  const powerNodeIds = new Set();
  const nodePattern = /([A-Za-z][A-Za-z0-9_]*)\s*\[([^\]]+)\]/g;
  let match = nodePattern.exec(text);
  while (match) {
    if (/^\s*(?:GND\b|VCC\b|VDD\b|3\.3V\b|5V\b|电源|供电|共地)/i.test(match[2])) {
      powerNodeIds.add(match[1]);
    }
    match = nodePattern.exec(text);
  }

  if (powerNodeIds.size === 0) return text;
  let removedConnection = false;
  const kept = text.split('\n').filter((line) => {
    const linkText = line.replace(/\[[^\]]*\]/g, '').replace(/"[^"]*"/g, '');
    const mentionsPowerNode = [...powerNodeIds].some((id) => new RegExp(`(?<![A-Za-z0-9_/])${id}(?![A-Za-z0-9_/])`).test(linkText));
    const isConnection = /-->|---|-.->|==>/.test(line);
    const isDefinition = [...powerNodeIds].some((id) => new RegExp(`^\\s*${id}\\s*\\[`).test(line));
    if (mentionsPowerNode && (isConnection || isDefinition)) {
      removedConnection ||= isConnection;
      return false;
    }
    return true;
  });

  if (!removedConnection) return text;
  kept.push('  MOCE_POWER["供电 / 共地：统一连接，详见接线清单"]');
  return kept.join('\n');
}

function markdownPreview(markdown, maxLength = 1200) {
  const text = String(markdown || '').trim();
  if (!text) return '';
  if (text.length <= maxLength) return text;
  const sliced = text.slice(0, maxLength);
  const lastBreak = Math.max(sliced.lastIndexOf('\n## '), sliced.lastIndexOf('\n\n'));
  return `${sliced.slice(0, lastBreak > 320 ? lastBreak : maxLength).trim()}\n\n...`;
}

function updateOutput(target, markdown, emptyText, options = {}) {
  const element = $(target);
  if (!element) return;
  const text = options.preview ? markdownPreview(markdown, options.maxLength) : String(markdown || '');
  element.className = options.className || (text ? 'markdown-body' : 'markdown-body empty');
  element.innerHTML = text ? markdownToHtml(text) : emptyText;
}

function renderPlan() {
  const planText = state.plan || '';
  const detailText = hardwareBuildDetails(planText);
  $('#planOutput').className = detailText ? 'markdown-body' : 'markdown-body empty';
  $('#planOutput').innerHTML = detailText ? markdownToHtml(detailText) : '等待生成硬件搭建文档。';
  $('#planMeta').textContent = planText ? '已生成' : '等待生成';
  $('#planSummary').textContent = planText ? '已生成硬件搭建文档，可查看模块连接框图和接线清单。' : '尚未生成硬件搭建文档。';

  renderHardwareCoverage();
  renderSteps();
  updateWorkflowControls();
}

function renderAnalysis() {
  const analysisText = state.analysis || '';
  updateOutput('#analysisPreview', analysisText, '等待功能分析。', {
    preview: true,
    maxLength: 1100,
    className: analysisText ? 'markdown-body compact-preview' : 'markdown-body compact-preview empty'
  });
  updateOutput('#analysisOutput', analysisText, '等待功能分析。', {
    className: analysisText ? 'markdown-body reading-body' : 'markdown-body reading-body empty'
  });
  const meta = analysisText
    ? state.analysisAccepted ? '已接受' : state.analysisRefinementDirty ? '已补充，待重新分析' : '待确认'
    : '等待确认';
  $('#analysisMeta').textContent = meta;
  $('#analysisFullMeta').textContent = analysisText ? `${analysisText.length} chars / ${meta}` : meta;
  $('#analysisSummary').textContent = analysisText
    ? state.analysisAccepted ? '功能分析已接受，可进入器件选型。' : state.analysisRefinementDirty ? '需求已补充，请再次功能分析。' : '功能分析完成，可接受或继续细化需求。'
    : '尚未进行功能分析。';
  renderStateMachineDiagram();
  renderHardwareCoverage();
  renderSteps();
  updateWorkflowControls();
}

function renderComponentSelection() {
  const selectionText = state.componentSelection || '';
  updateOutput('#componentSelectionPreview', selectionText, '等待器件选型。', {
    preview: true,
    maxLength: 1100,
    className: selectionText ? 'markdown-body compact-preview' : 'markdown-body compact-preview empty'
  });
  updateOutput('#componentSelectionOutput', selectionText, '等待器件选型。', {
    className: selectionText ? 'markdown-body reading-body' : 'markdown-body reading-body empty'
  });
  const meta = selectionText
    ? state.componentSelectionAccepted ? '已接受' : state.componentSelectionDirty ? '已调整，待再次检索' : '待确认'
    : '等待确认';
  $('#componentSelectionMeta').textContent = meta;
  $('#componentSelectionFullMeta').textContent = selectionText ? `${selectionText.length} chars / ${meta}` : meta;
  $('#componentSelectionSummary').textContent = selectionText
    ? state.componentSelectionAccepted ? '器件选型已接受，可进入硬件资源规划。' : state.componentSelectionDirty ? '选型要求已调整，请再次检索。' : '器件选型完成，可接受或继续调整。'
    : '尚未进行器件选型。';
  renderSteps();
  updateWorkflowControls();
}

async function renderDiagram() {
  const diagram = simplifyDiagramPowerNetwork(compactMermaidLabels(extractMermaid(state.plan)));

  const sequence = ++diagramRenderSequence;
  const targets = ['#diagramGrid', '#diagramPreview'].map((selector) => $(selector)).filter(Boolean);
  if (!diagram) {
    for (const el of targets) {
      el.className = 'diagram-canvas empty';
      el.textContent = '等待生成框图。';
    }
    return;
  }

  for (const el of targets) {
    el.className = 'diagram-canvas rendering';
    el.textContent = '正在渲染框图...';
  }

  try {
    await mermaid.parse(diagram);
    const rendered = await Promise.all(targets.map((el, index) => mermaid.render(`moceDiagram${sequence}_${index}`, diagram, el)));
    if (sequence !== diagramRenderSequence) return;
    rendered.forEach(({ svg, bindFunctions }, index) => {
      const el = targets[index];
      el.className = 'diagram-canvas mermaid-diagram';
      el.innerHTML = svg;
      bindFunctions?.(el);
    });
  } catch (error) {
    if (sequence !== diagramRenderSequence) return;
    const message = error?.message?.split('\n')[0] || 'Mermaid 语法无法解析。';
    for (const el of targets) {
      el.className = 'diagram-canvas diagram-error';
      el.innerHTML = `<strong>框图渲染失败</strong><p>${escapeHtml(message)}</p>`;
    }
  }
}

async function renderStateMachineDiagram() {
  const diagram = extractStateMachineMermaid(state.analysis);
  const sequence = ++stateMachineRenderSequence;
  const targets = ['#stateMachinePreview', '#stateMachineDiagram'].map((selector) => $(selector)).filter(Boolean);
  if (!state.analysis) {
    for (const el of targets) {
      el.className = 'diagram-canvas empty state-machine-canvas';
      el.textContent = '等待功能分析。';
    }
    return;
  }
  if (!diagram) {
    for (const el of targets) {
      el.className = 'diagram-canvas empty state-machine-canvas';
      el.textContent = '未识别到需要渲染的状态机。';
    }
    return;
  }

  for (const el of targets) {
    el.className = 'diagram-canvas rendering state-machine-canvas';
    el.textContent = '正在渲染状态机...';
  }

  try {
    await renderStateMachineMermaidToTargets(diagram, targets, sequence, `moceStateMachine${sequence}`);
  } catch (error) {
    if (sequence !== stateMachineRenderSequence) return;
    const fallbacks = [
      stateDiagramToFlowchart(diagram),
      stateMachineFromNamedStates(state.analysis)
    ].filter((item, index, array) => item && item !== diagram && array.indexOf(item) === index);
    for (let index = 0; index < fallbacks.length; index += 1) {
      const fallback = fallbacks[index];
      try {
        await renderStateMachineMermaidToTargets(fallback, targets, sequence, `moceStateMachine${sequence}_fallback${index}`);
        return;
      } catch {
        // Try the next fallback.
      }
    }
    const message = error?.message?.split('\n')[0] || 'Mermaid 状态机语法无法解析。';
    for (const el of targets) {
      el.className = 'diagram-canvas diagram-error state-machine-canvas';
      el.innerHTML = `<strong>状态机渲染失败</strong><p>${escapeHtml(message)}</p>`;
    }
  }
}

function stateDiagramToFlowchart(diagram) {
  const source = String(diagram || '').replace(/\r\n/g, '\n');
  if (!/\bstateDiagram(?:-v2)?\b/i.test(source)) {
    return '';
  }

  const labels = new Map();
  const edges = [];
  const nodeIds = new Map();
  let nextId = 1;
  const safeNodeId = (name) => {
    const raw = String(name || '').trim();
    if (!raw || raw === '[*]') return raw === '[*]' ? 'START' : '';
    if (!nodeIds.has(raw)) {
      nodeIds.set(raw, `N${nextId}`);
      nextId += 1;
    }
    return nodeIds.get(raw);
  };

  for (const rawLine of source.split('\n')) {
    const line = rawLine.trim();
    if (!line || /^stateDiagram(?:-v2)?\b/i.test(line)) continue;
    const declaration = line.match(/^([A-Za-z0-9_]+)\s*:\s*(.+)$/);
    if (declaration) {
      labels.set(declaration[1], declaration[2].trim());
      safeNodeId(declaration[1]);
      continue;
    }
    const transition = line.match(/^(\[\*\]|[A-Za-z0-9_]+)\s*-->\s*(\[\*\]|[A-Za-z0-9_]+)(?:\s*:\s*(.+))?$/);
    if (transition) {
      const from = transition[1];
      const to = transition[2];
      safeNodeId(from);
      safeNodeId(to);
      edges.push({ from, to, label: (transition[3] || '').trim() });
    }
  }

  if (edges.length === 0) return '';
  const lines = ['flowchart LR'];
  for (const [raw, id] of nodeIds.entries()) {
    if (raw === '[*]') continue;
    const label = (labels.get(raw) || raw).replace(/["[\]]/g, '').slice(0, 32);
    lines.push(`  ${id}[${label}]`);
  }
  for (const edge of edges) {
    const from = safeNodeId(edge.from);
    const to = safeNodeId(edge.to);
    const label = edge.label.replace(/[|"[\]]/g, '').slice(0, 28);
    const connector = label ? ` -->|${label}| ` : ' --> ';
    lines.push(`  ${from}${connector}${to}`);
  }
  return lines.join('\n');
}

async function renderStateMachineMermaidToTargets(diagram, targets, sequence, renderKey) {
  await mermaid.parse(diagram);
  for (let index = 0; index < targets.length; index += 1) {
    const el = targets[index];
    const { svg, bindFunctions } = await mermaid.render(`${renderKey}_${index}`, diagram, el);
    if (sequence !== stateMachineRenderSequence) return;
    el.className = 'diagram-canvas mermaid-diagram state-machine-canvas';
    el.innerHTML = svg;
    bindFunctions?.(el);
  }
}

function parseMarkdownTables(markdown) {
  const lines = String(markdown || '').split(/\r?\n/);
  const tables = [];
  for (let index = 0; index < lines.length - 1; index += 1) {
    if (!lines[index].includes('|') || !/^\s*\|?\s*:?-{3,}/.test(lines[index + 1])) continue;
    const tableLines = [lines[index], lines[index + 1]];
    let cursor = index + 2;
    while (cursor < lines.length && lines[cursor].includes('|')) {
      tableLines.push(lines[cursor]);
      cursor += 1;
    }
    tables.push(tableLines);
    index = cursor;
  }
  return tables;
}

function tableToHtml(tableLines) {
  const rows = tableLines
    .filter((line, index) => index !== 1)
    .map((line) => line.trim().replace(/^\|/, '').replace(/\|$/, '').split('|').map((cell) => cell.trim()));
  if (rows.length === 0) return '';
  const head = rows[0];
  const body = rows.slice(1);
  return `
    <table>
      <thead><tr>${head.map((cell) => `<th>${escapeHtml(cell)}</th>`).join('')}</tr></thead>
      <tbody>${body.map((row) => `<tr>${row.map((cell) => `<td>${escapeHtml(cell)}</td>`).join('')}</tr>`).join('')}</tbody>
    </table>
  `;
}

function boardResourceRows() {
  const boardName = $('#boardName')?.value || defaults.boardName;
  const board = (state.sdk?.boards || []).find((item) => item.name === boardName)
    || (state.sdk?.boards || [])[0];
  const resources = board?.resources?.length ? board.resources : resourcesFromBoardDefines(board?.defines || {});
  const rows = resources.map((resource) => [
    resource.name || '-',
    resource.interface || '-',
    resource.pins || '-',
    resource.sdk || 'board',
    resource.source || 'board.h'
  ]);
  const busRows = (state.sdk?.components || [])
    .flatMap((component) => component.busResources || [])
    .filter((resource) => resource.bus === 'I2C')
    .map((resource) => [
      resource.component || '-',
      resource.bus || '-',
      `${compactComponentBusName(resource.name, resource.component)}=${resource.resolvedValue || resource.value || '-'}`,
      resource.component || 'component',
      resource.source || 'component header'
    ]);
  const allRows = [...rows, ...busRows];
  return `
    <table>
      <thead><tr><th>资源</th><th>接口</th><th>引脚</th><th>SDK</th><th>来源</th></tr></thead>
      <tbody>${allRows.length
        ? allRows.map((row) => `<tr>${row.map((cell) => `<td>${escapeHtml(cell)}</td>`).join('')}</tr>`).join('')
        : '<tr><td colspan="5">未扫描到 board.h 资源。</td></tr>'}</tbody>
    </table>
  `;
}

function resourcesFromBoardDefines(defines) {
  const groups = new Map();
  for (const macro of Object.keys(defines || {}).sort()) {
    if (!macro.startsWith('BOARD_')) continue;
    const family = macro.match(/^BOARD_([A-Z0-9]+)_/)?.[1] || 'BOARD';
    const parts = macro.replace(/^BOARD_/, '').split('_');
    const side = parts.includes('LEFT') ? 'L' : parts.includes('RIGHT') ? 'R' : (parts.find((part) => /^\d+$/.test(part)) || '');
    const key = side ? `${family}:${side}` : family;
    if (!groups.has(key)) {
      groups.set(key, { family, side, macros: [] });
    }
    groups.get(key).macros.push(macro);
  }
  const order = ['LED', 'BUTTON', 'SERVO', 'I2C', 'UART', 'MOTOR', 'ENCODER'];
  const labels = { LED: 'LED', BUTTON: 'Button', SERVO: 'Servo', I2C: 'I2C', UART: 'UART', MOTOR: 'Motor', ENCODER: 'Encoder' };
  const sdks = { LED: 'driver_led', BUTTON: 'driver_button', SERVO: 'driver_servo', I2C: 'bsp_i2c', UART: 'bsp_uart', MOTOR: 'driver_motor', ENCODER: 'driver_encoder' };
  return [...groups.values()]
    .sort((a, b) => (order.indexOf(a.family) === -1 ? 99 : order.indexOf(a.family)) - (order.indexOf(b.family) === -1 ? 99 : order.indexOf(b.family)))
    .map((group) => {
      const name = `${labels[group.family] || group.family}${group.side ? ` ${group.side}` : ''}`;
      const pins = group.macros
        .filter((macro) => /GPIO|CHANNEL|ADDRESS|BAUD_RATE|FREQUENCY_HZ|PORT|ACTIVE_LEVEL/.test(macro))
        .map((macro) => `${compactBoardMacroName(macro, group.family)}=${defines[macro]}`)
        .join(' / ');
      return {
        name,
        family: group.family,
        interface: group.family === 'I2C' || group.family === 'UART'
          ? group.family
          : group.family === 'ENCODER'
            ? 'PCNT/GPIO'
            : group.macros.some((macro) => /PWM|CHANNEL|TIMER|FREQUENCY/.test(macro))
              ? group.macros.some((macro) => /GPIO/.test(macro)) ? 'GPIO/PWM' : 'PWM'
              : 'GPIO',
        pins: pins || '-',
        sdk: sdks[group.family] || 'board',
        source: 'board.h'
      };
    });
}

function compactBoardMacroName(macro, family) {
  return String(macro || '')
    .replace(/^BOARD_/, '')
    .replace(new RegExp(`^${family}_`), '')
    .replace(/^(LEFT|RIGHT)_/, '')
    .replace(/_GPIO$/, '')
    .replace(/_CHANNEL$/, '_CH')
    .replace(/_FREQUENCY_HZ$/, '_FREQ')
    .replace(/^BAUD_RATE$/, 'BAUD')
    .replace(/_ADDRESS$/, '_ADDR');
}

function compactComponentBusName(macro, component) {
  return String(macro || '')
    .replace(/^DRIVER_/, '')
    .replace(new RegExp(`^${String(component || '').replace(/^driver_/, '').toUpperCase()}_`), '')
    .replace(/^DEFAULT_I2C_ADDR$/, 'ADDR_DEFAULT')
    .replace(/^I2C_ADDR_/, 'ADDR_')
    .replace(/_I2C_ADDR$/, '_ADDR')
    .replace(/I2C_ADDR/g, 'ADDR');
}

function renderResources() {
  const resourceText = state.resourcePlan || '';
  updateOutput('#resourcePlanOutput', resourceText, '等待硬件资源规划。', {
    className: resourceText ? 'markdown-body reading-body' : 'markdown-body reading-body empty'
  });
  const resourceMeta = resourceText
    ? state.resourcePlanAccepted ? '已接受' : state.resourcePlanDirty ? '已调整，待再次规划' : '待确认'
    : '等待确认';
  $('#resourceMeta').textContent = resourceText ? `${resourceText.length} chars / ${resourceMeta}` : '来自规划或板级定义';
  $('#resourcePlanSummary').textContent = resourceText
    ? state.resourcePlanAccepted ? '硬件资源规划已接受，可进入硬件搭建。' : state.resourcePlanDirty ? '资源要求已调整，请再次规划。' : '硬件资源规划完成，可接受或继续调整。'
    : '尚未进行硬件资源规划。';

  const tables = parseMarkdownTables(resourceText || state.plan);
  const resourceTable = tables.find((table) => /GPIO|I2C|PWM|UART|接口|引脚|资源|ESP32|Servo|OLED|VL53/i.test(table.join('\n')));
  if (resourceTable) {
    $('#resourceTable').className = 'resource-table';
    $('#resourceTable').innerHTML = tableToHtml(resourceTable);
  } else if (state.sdk) {
    if (!resourceText) {
      $('#resourceMeta').textContent = '来自 boards/ 定义';
    }
    $('#resourceTable').className = 'resource-table';
    $('#resourceTable').innerHTML = boardResourceRows();
  } else {
    $('#resourceTable').className = 'resource-table empty';
    $('#resourceTable').textContent = '等待生成资源表。';
  }
  renderSteps();
  updateWorkflowControls();
}

function renderHardwareCoverage() {
  const text = `${$('#requirement')?.value || ''}\n${$('#analysisRefinement')?.value || ''}\n${$('#componentSelectionNotes')?.value || ''}\n${$('#resourcePlanNotes')?.value || ''}\n${$('#implementationFeedback')?.value || ''}\n${state.analysis || ''}\n${state.componentSelection || ''}\n${state.resourcePlan || ''}\n${state.plan || ''}`;
  const hits = hardwareKeywords.map(([name, pattern]) => ({ name, hit: pattern.test(text) }));
  $('#hardwareCoverage').innerHTML = hits.map((item) => `
    <span class="pill ${item.hit ? '' : 'warn'}">${item.hit ? 'OK' : 'MISS'} ${item.name}</span>
  `).join('');
}

function renderValidation() {
  const validation = state.validation;
  if (!validation) {
    $('#validationBadge').textContent = '未生成';
    $('#validationPanel').className = 'validation-list empty';
    $('#validationPanel').textContent = '等待代码生成。';
    return;
  }

  $('#validationBadge').textContent = validation.ok ? '通过' : '需处理';
  const items = [];
  if (validation.ok) items.push({ kind: 'ok', text: '静态校验通过' });
  for (const error of validation.errors || []) items.push({ kind: 'error', text: error });
  for (const warning of validation.warnings || []) items.push({ kind: 'warn', text: warning });
  $('#validationPanel').className = 'validation-list';
  $('#validationPanel').innerHTML = items.map((item) => `
    <div class="validation-item ${item.kind}">${escapeHtml(item.text)}</div>
  `).join('');
}

function renderFeedback() {
  const text = $('#implementationFeedback')?.value.trim() || '';
  const testsOk = state.tools.build?.ok && state.tools.flash?.ok && state.tools.monitor?.ok;
  if (!text) {
    $('#feedbackMeta').textContent = testsOk ? '可填写反馈' : '等待调试';
    $('#feedbackSummary').textContent = testsOk
      ? '调试完成后，可输入未满足的功能细节并重新生成固件。'
      : '等待调试后反馈实现差异。';
  } else if (state.implementationFeedbackDirty) {
    $('#feedbackMeta').textContent = '有新反馈';
    $('#feedbackSummary').textContent = '实现反馈已更新，可重新生成固件草稿。';
  } else if (state.implementationFeedbackApplied) {
    $('#feedbackMeta').textContent = '已用于生成';
    $('#feedbackSummary').textContent = '实现反馈已合入固件生成上下文。';
  } else {
    $('#feedbackMeta').textContent = '待应用';
    $('#feedbackSummary').textContent = '已填写实现反馈，等待重新生成固件。';
  }
  updateWorkflowControls();
}

function isGeneratedOutputFile(file) {
  return file?.path?.startsWith('project/') || file?.path?.startsWith('physical_agent_config/config/');
}

function renderFiles() {
  const outputFiles = state.files.filter(isGeneratedOutputFile);
  const projectFiles = outputFiles.filter((file) => file.path.startsWith('project/'));
  if (outputFiles.length === 0) {
    $('#fileTree').className = 'file-tree empty';
    $('#fileTree').textContent = '等待生成文件。';
    $('#selectedFileTitle').textContent = '未选择文件';
    $('#selectedFileContent').textContent = '生成代码后，在左侧选择文件查看。';
    $('#codeStatus').textContent = '尚未生成代码。';
    $('#codeSummary').textContent = '尚未生成代码。';
    $('#writeBtn').disabled = true;
    $('#buildBtn').disabled = true;
    $('#flashBtn').disabled = true;
    $('#monitorBtn').disabled = true;
    $('#runBuildBtn').disabled = true;
    $('#runFlashBtn').disabled = true;
    renderValidation();
    renderSteps();
    updateWorkflowControls();
    return;
  }

  if (!state.selectedFilePath || !outputFiles.some((file) => file.path === state.selectedFilePath)) {
    state.selectedFilePath = outputFiles[0].path;
  }

  $('#codeStatus').textContent = `${outputFiles.length} 个可写入文件`;
  $('#codeSummary').textContent = state.validation?.ok === false
    ? '代码已生成，但静态校验发现问题。'
    : `已生成 ${projectFiles.length} 个 project/ 文件，${outputFiles.length - projectFiles.length} 个配置文件。`;
  $('#writeBtn').disabled = false;
  $('#buildBtn').disabled = projectFiles.length === 0;
  $('#flashBtn').disabled = projectFiles.length === 0;
  $('#monitorBtn').disabled = projectFiles.length === 0;
  $('#runBuildBtn').disabled = projectFiles.length === 0;
  $('#runFlashBtn').disabled = projectFiles.length === 0;

  $('#fileTree').className = 'file-tree';
  $('#fileTree').innerHTML = outputFiles.map((file) => `
    <button class="file-item ${file.path === state.selectedFilePath ? 'active' : ''}" data-file="${escapeHtml(file.path)}">
      ${escapeHtml(file.path)}
    </button>
  `).join('');

  const selected = outputFiles.find((file) => file.path === state.selectedFilePath);
  $('#selectedFileTitle').textContent = selected?.path || '未选择文件';
  $('#selectedFileContent').textContent = selected?.content || '';
  renderValidation();
  renderSteps();
  updateWorkflowControls();
}

function mergeProjectFileList(baseFiles, updatedFiles) {
  const byPath = new Map();
  for (const file of baseFiles || []) {
    if (file?.path && typeof file.content === 'string') {
      byPath.set(file.path, file);
    }
  }
  for (const file of updatedFiles || []) {
    if (file?.path && typeof file.content === 'string') {
      byPath.set(file.path, file);
    }
  }
  return [...byPath.values()].sort((a, b) => a.path.localeCompare(b.path));
}

function switchTab(name) {
  document.querySelectorAll('.tab').forEach((tab) => {
    tab.classList.toggle('active', tab.dataset.tab === name);
  });
  document.querySelectorAll('.view').forEach((panel) => {
    panel.classList.toggle('active', panel.id === `tab-${name}`);
  });
}

function selectedPidPort() {
  const manual = $('#pidPortManual')?.value.trim();
  if (manual) return manual;
  return $('#pidPortSelect')?.value || $('#portName')?.value.trim() || state.health?.defaultSerialPort || '';
}

async function loadSerialPorts() {
  if (!$('#pidPortSelect')) return;
  const data = await api('/api/tools/serial-ports');
  const ports = data.ports || [];
  const current = selectedPidPort();
  const fallback = state.health?.defaultSerialPort || data.defaultSerialPort || '';
  const options = ports.map((port) => {
    const label = [port.path, port.description].filter(Boolean).join(' - ');
    return `<option value="${escapeAttr(port.path)}">${escapeHtml(label || port.path)}</option>`;
  });
  if (fallback && !ports.some((port) => port.path === fallback)) {
    options.unshift(`<option value="${escapeAttr(fallback)}">${escapeHtml(`${fallback} - 默认`)}</option>`);
  }
  $('#pidPortSelect').innerHTML = options.join('') || '<option value="">未发现串口</option>';
  const next = ports.some((port) => port.path === current) || current === fallback ? current : fallback;
  if (next) $('#pidPortSelect').value = next;
  if (data.warning) {
    logActivity(`Serial port scan warning: ${data.warning}`, 'warn');
  }
}

async function loadHealth() {
  state.health = await api('/api/health');
  renderProviders();
  if (!$('#portName').value.trim() || $('#portName').value === '/dev/ttyUSB0') {
    $('#portName').value = state.health.defaultSerialPort || $('#portName').value;
  }
  if ($('#pidPortManual') && !$('#pidPortManual').value.trim()) {
    $('#pidPortManual').placeholder = state.health.defaultSerialPort || '例如 /dev/ttyUSB0 或 COM3';
  }
  const execution = state.health.execution || {};
  if (execution.defaultCwd) {
    $('#execCwd').value = execution.defaultCwd;
  }
  $('#execBtn').disabled = execution.enabled === false;
  $('#physicalOutput').textContent = JSON.stringify(state.health.physical, null, 2);
  setStatus(`本地 agent 已连接：${state.health.sdkRoot}`, 'ok');
  logActivity('Agent connected', 'ok');
  await loadSerialPorts();
}

async function scanSdk(refresh = false) {
  setStatus('正在扫描 SDK...', 'warn');
  const data = refresh
    ? await api('/api/sdk/refresh', { method: 'POST', body: {} })
    : await api('/api/sdk/summary');
  state.sdk = data.summary;
  renderSdk();
  renderResources();
  setStatus(`SDK 已扫描：${state.sdk.components.length} components, ${state.sdk.examples.length} examples`, 'ok');
  logActivity(`SDK scanned: ${state.sdk.components.length} components`, 'ok');
}

async function loadPromptDocuments() {
  const data = await api('/api/prompts');
  state.prompts = data.prompts || [];
  renderPromptOptions();
  logActivity(`Prompt docs scanned: ${state.prompts.length}`, state.prompts.length ? 'ok' : 'warn');
}

async function loadSelectedPrompt() {
  const promptPath = $('#promptPath').value;
  if (!promptPath) return;
  $('#promptStatus').textContent = '读取中...';
  const data = await api('/api/prompts/read', {
    method: 'POST',
    body: { promptPath }
  });
  $('#requirement').value = data.prompt.content || '';
  $('#promptStatus').textContent = data.prompt.path;
  const promptProjectName = projectNameFromRequirement(data.prompt.content || '');
  if (promptProjectName) {
    $('#projectName').value = promptProjectName;
  }
  state.analysis = '';
  state.analysisRefinementDirty = false;
  state.componentSelection = '';
  state.componentSelectionAccepted = false;
  state.componentSelectionDirty = false;
  if ($('#analysisRefinement')) {
    $('#analysisRefinement').value = '';
  }
  if ($('#componentSelectionNotes')) {
    $('#componentSelectionNotes').value = '';
  }
  invalidateAnalysis();
  renderHardwareCoverage();
  logActivity(`Loaded prompt/${data.prompt.path}`, 'ok');
}

function invalidateAnalysis() {
  state.analysisAccepted = false;
  state.analysisRefinementDirty = false;
  state.componentSelection = '';
  state.componentSelectionAccepted = false;
  state.componentSelectionDirty = false;
  clearResourcePlan();
  if ($('#componentSelectionNotes')) {
    $('#componentSelectionNotes').value = '';
  }
  state.plan = '';
  state.files = [];
  state.validation = null;
  clearTestTools();
  renderAnalysis();
  renderComponentSelection();
  renderPlan();
  renderDiagram();
  renderResources();
  renderFiles();
}

function invalidateAnalysisForTyping() {
  state.analysis = '';
  state.analysisAccepted = false;
  state.analysisRefinementDirty = false;
  state.componentSelection = '';
  state.componentSelectionAccepted = false;
  state.componentSelectionDirty = false;
  state.resourcePlan = '';
  state.resourcePlanAccepted = false;
  state.resourcePlanDirty = false;
  state.plan = '';
  state.files = [];
  state.validation = null;
  state.tools = { build: null, flash: null, monitor: null };
  state.lastToolResult = null;
  state.debugFix = null;
  clearTimeout(requirementInputTimer);
  requirementInputTimer = setTimeout(() => {
    $('#analysisSummary').textContent = $('#requirement')?.value.trim()
      ? '需求已修改，请重新进行功能分析。'
      : '尚未进行功能分析。';
    $('#codeSummary').textContent = '尚未生成代码。';
    $('#writeBtn').disabled = true;
    $('#buildBtn').disabled = true;
    $('#flashBtn').disabled = true;
    $('#monitorBtn').disabled = true;
    $('#runBuildBtn').disabled = true;
    $('#runFlashBtn').disabled = true;
    renderSteps();
    updateWorkflowControls();
  }, 180);
}

function scheduleLightweightWorkflowRefresh(update = () => {}) {
  clearTimeout(lightweightRefreshTimer);
  lightweightRefreshTimer = setTimeout(() => {
    update();
    renderSteps();
    updateWorkflowControls();
  }, 160);
}

function clearResourcePlan() {
  state.resourcePlan = '';
  state.resourcePlanAccepted = false;
  state.resourcePlanDirty = false;
  if ($('#resourcePlanNotes')) {
    $('#resourcePlanNotes').value = '';
  }
}

async function generateAnalysis() {
  const payload = {
    ...currentPayload(),
    requirement: refinedRequirementText()
  };
  $('#analysisOutput').className = 'markdown-body empty';
  $('#analysisOutput').textContent = '正在进行功能分析...';
  state.analysisAccepted = false;
  state.analysisRefinementDirty = false;
  switchTab('analysis');
  logActivity('Analyzing hardware functions...', 'warn');
  const data = await api('/api/agent/analyze', {
    method: 'POST',
    body: payload
  });
  state.analysis = data.warning ? `${data.result}\n\n[Fallback reason] ${data.warning}` : data.result;
  state.plan = '';
  state.componentSelection = '';
  state.componentSelectionAccepted = false;
  state.componentSelectionDirty = false;
  clearResourcePlan();
  if ($('#componentSelectionNotes')) {
    $('#componentSelectionNotes').value = '';
  }
  state.files = [];
  state.validation = null;
  clearTestTools();
  if (data.sdkSummary) {
    state.sdk = data.sdkSummary;
    renderSdk();
  }
  renderAnalysis();
  renderComponentSelection();
  renderPlan();
  renderDiagram();
  renderResources();
  renderFiles();
  logActivity(data.mode === 'llm' ? 'Function analysis complete' : 'Fallback analysis complete', data.warning ? 'warn' : 'ok');
  await recordMemoryEvent('analysis_generated', '生成功能分析', state.analysis.slice(0, 120), { mode: data.mode });
}

function acceptAnalysis() {
  if (!state.analysis) return;
  if (state.analysisRefinementDirty) {
    setStatus('需求已补充，请先点击“再次功能分析”。', 'warn');
    return;
  }
  state.analysisAccepted = true;
  state.componentSelection = '';
  state.componentSelectionAccepted = false;
  state.componentSelectionDirty = false;
  clearResourcePlan();
  state.plan = '';
  state.files = [];
  state.validation = null;
  clearTestTools();
  if ($('#componentSelectionNotes')) {
    $('#componentSelectionNotes').value = '';
  }
  renderAnalysis();
  renderComponentSelection();
  renderPlan();
  renderFiles();
  logActivity('Function analysis accepted', 'ok');
  recordMemoryEvent('analysis_accepted', '接受功能分析', '功能分析已确认').catch(showError);
}

function componentSelectionNotesText() {
  return $('#componentSelectionNotes')?.value.trim() || '';
}

async function generateComponentSelection() {
  if (!state.analysisAccepted) {
    setStatus('请先完成并接受功能分析。', 'warn');
    switchTab('overview');
    return;
  }
  const payload = {
    ...currentPayload(),
    requirement: refinedRequirementText(),
    analysis: state.analysis,
    selectionNotes: componentSelectionNotesText()
  };
  $('#componentSelectionOutput').className = 'markdown-body empty';
  $('#componentSelectionOutput').textContent = '正在检索并生成器件选型...';
  state.componentSelectionAccepted = false;
  state.componentSelectionDirty = false;
  switchTab('selection');
  logActivity('Selecting components...', 'warn');
  const data = await api('/api/agent/component-selection', {
    method: 'POST',
    body: payload
  });
  state.componentSelection = data.warning ? `${data.result}\n\n[Fallback reason] ${data.warning}` : data.result;
  clearResourcePlan();
  state.plan = '';
  state.files = [];
  state.validation = null;
  clearTestTools();
  if (data.sdkSummary) {
    state.sdk = data.sdkSummary;
    renderSdk();
  }
  renderComponentSelection();
  renderPlan();
  renderDiagram();
  renderResources();
  renderFiles();
  logActivity(data.mode === 'llm' ? 'Component selection complete' : 'Fallback component selection complete', data.warning ? 'warn' : 'ok');
  await recordMemoryEvent('component_selection_generated', '生成器件选型', state.componentSelection.slice(0, 120), { mode: data.mode });
}

function acceptComponentSelection() {
  if (!state.componentSelection) return;
  if (state.componentSelectionDirty) {
    setStatus('选型要求已调整，请先点击“再次检索”。', 'warn');
    return;
  }
  state.componentSelectionAccepted = true;
  clearResourcePlan();
  renderComponentSelection();
  renderResources();
  logActivity('Component selection accepted', 'ok');
  recordMemoryEvent('component_selection_accepted', '接受器件选型', '器件选型已确认').catch(showError);
}

function resourcePlanNotesText() {
  return $('#resourcePlanNotes')?.value.trim() || '';
}

async function generateResourcePlan() {
  if (!state.componentSelectionAccepted) {
    setStatus('请先完成并接受器件选型。', 'warn');
    switchTab('overview');
    return;
  }
  const payload = {
    ...currentPayload(),
    requirement: refinedRequirementText(),
    analysis: state.analysis,
    componentSelection: state.componentSelection,
    resourceNotes: resourcePlanNotesText()
  };
  $('#resourcePlanOutput').className = 'markdown-body reading-body empty';
  $('#resourcePlanOutput').textContent = '正在进行硬件资源规划...';
  state.resourcePlanAccepted = false;
  state.resourcePlanDirty = false;
  switchTab('resources');
  logActivity('Planning hardware resources...', 'warn');
  const data = await api('/api/agent/hardware-resource-planning', {
    method: 'POST',
    body: payload
  });
  state.resourcePlan = data.warning ? `${data.result}\n\n[Fallback reason] ${data.warning}` : data.result;
  state.plan = '';
  state.files = [];
  state.validation = null;
  clearTestTools();
  if (data.sdkSummary) {
    state.sdk = data.sdkSummary;
    renderSdk();
  }
  if (data.sdkResources) {
    state.sdkResources = data.sdkResources;
    logActivity(`SDK resources loaded: ${data.sdkResources.board}`, 'ok');
  }
  renderResources();
  renderPlan();
  renderDiagram();
  renderFiles();
  logActivity(data.mode === 'llm' ? 'Hardware resource planning complete' : 'Fallback resource planning complete', data.warning ? 'warn' : 'ok');
  await recordMemoryEvent('resource_plan_generated', '生成硬件资源规划', state.resourcePlan.slice(0, 120), { mode: data.mode });
}

function acceptResourcePlan() {
  if (!state.resourcePlan) return;
  if (state.resourcePlanDirty) {
    setStatus('资源规划要求已调整，请先点击“再次规划”。', 'warn');
    return;
  }
  state.resourcePlanAccepted = true;
  renderResources();
  logActivity('Hardware resource plan accepted', 'ok');
  recordMemoryEvent('resource_plan_accepted', '接受硬件资源规划', '硬件资源规划已确认').catch(showError);
}

async function generateHardwareBuild() {
  if (!state.resourcePlanAccepted) {
    setStatus('请先完成并接受硬件资源规划。', 'warn');
    switchTab('overview');
    return;
  }
  const payload = currentPayload();
  $('#planOutput').className = 'markdown-body empty';
  $('#planOutput').textContent = '正在生成硬件搭建文档...';
  switchTab('plan');
  logActivity('Generating hardware build diagram...', 'warn');
  const data = await api('/api/agent/hardware-build', {
    method: 'POST',
    body: {
      ...payload,
      requirement: refinedRequirementText(),
      analysis: state.analysis,
      componentSelection: state.componentSelection,
      resourcePlan: state.resourcePlan
    }
  });
  state.plan = data.warning ? `${data.result}\n\n[Fallback reason] ${data.warning}` : data.result;
  if (data.sdkSummary) {
    state.sdk = data.sdkSummary;
    renderSdk();
  }
  renderPlan();
  renderDiagram();
  renderResources();
  logActivity(data.mode === 'llm' ? 'Hardware build complete' : 'Fallback hardware build complete', data.warning ? 'warn' : 'ok');
  await recordMemoryEvent('hardware_build_generated', '生成硬件搭建', state.plan.slice(0, 120), { mode: data.mode });
}

async function generateCode() {
  if (!state.resourcePlanAccepted) {
    setStatus('请先完成并接受硬件资源规划。', 'warn');
    switchTab('overview');
    return;
  }
  if (!state.plan) {
    setStatus('请先完成硬件搭建，生成模块连接框图。', 'warn');
    switchTab('overview');
    return;
  }
  const payload = currentPayload();
  $('#codeStatus').textContent = '正在生成固件草稿...';
  switchTab('code');
  logActivity('Generating firmware files...', 'warn');
  const data = await api('/api/agent/codegen', {
    method: 'POST',
    body: {
      ...payload,
      requirement: firmwareRequirementText(),
      analysis: state.analysis,
      componentSelection: state.componentSelection,
      resourcePlan: state.resourcePlan,
      plan: state.plan,
      projectPath: `project/${payload.projectName || 'robot_agent_app'}`,
      files: state.files
    }
  });
  state.validation = data.validation || null;
  state.files = data.files || [];
  state.selectedFilePath = '';
  clearTestTools();
  if ($('#implementationFeedback')?.value.trim()) {
    state.implementationFeedbackApplied = true;
    state.implementationFeedbackDirty = false;
  }
  renderFiles();
  renderFeedback();
  logActivity(data.note || 'Code generation complete', state.validation?.ok === false ? 'warn' : 'ok');
  await recordMemoryEvent('firmware_draft_generated', '生成固件草稿', data.note || '', { validation: state.validation });
}

async function writeProject() {
  const files = state.files.filter(isGeneratedOutputFile);
  if (files.length === 0) {
    throw new Error('没有可写入文件，请先生成固件草稿。');
  }
  $('#writeBtn').disabled = true;
  $('#codeStatus').textContent = `正在写入 ${files.length} 个文件...`;
  try {
    const data = await api('/api/project/write', {
      method: 'POST',
      body: { files }
    });
    const written = data.written || [];
    $('#codeStatus').textContent = `已写入并校验 ${written.length} 个文件`;
    logActivity(`Wrote and verified ${written.length} generated files`, 'ok');
    await recordMemoryEvent('project_written', '写入生成文件', `已写入并校验 ${written.length} 个文件`, { written });
  } finally {
    renderFiles();
  }
}

function renderToolResult(data) {
  const result = data.result || {};
  const summary = data.diagnostics?.text || summarizeToolOutput(result);
  const lines = [
    data.cwd ? `cwd: ${data.cwd}` : '',
    `$ ${data.command}`,
    '',
    summary || '未提取到错误信息。',
    result.timedOut ? 'timed out: true' : '',
    result.truncated ? 'output truncated: true' : '',
    `exit code: ${result.code}`
  ].filter(Boolean);
  setToolOutputs({
    errors: data.diagnostics?.keyLines || (result.ok === false ? lines : []),
    logs: data.diagnostics?.keyLines ? [`$ ${data.command}`, `exit code: ${result.code}`] : lines,
    monitorStatus: data.tool === 'monitor-session' ? data.status : ''
  });
}

function trimOutputLines(lines, limit) {
  const input = Array.isArray(lines) ? lines : String(lines || '').split(/\r?\n/);
  const filtered = input.map((line) => String(line || '').trimEnd()).filter((line) => line.trim());
  if (filtered.length <= limit) return filtered;
  return [`... 已清理 ${filtered.length - limit} 行旧日志 ...`, ...filtered.slice(-limit)];
}

function setScrollableText(element, lines, emptyText, limit) {
  if (!element) return;
  const trimmed = trimOutputLines(lines, limit);
  element.textContent = trimmed.length ? trimmed.join('\n') : emptyText;
  element.scrollTop = element.scrollHeight;
}

function setToolOutputs({ errors = [], logs = [], monitorStatus = '' } = {}) {
  setScrollableText($('#toolErrorOutput'), errors, '尚未提取错误。', 120);
  setScrollableText($('#toolLogOutput'), logs, '尚未运行工具。', 220);
  if (monitorStatus) {
    $('#monitorStatus').textContent = monitorStatus;
  }
}

function clearPidTunePoll() {
  if (pidTunePollTimer) {
    clearTimeout(pidTunePollTimer);
    pidTunePollTimer = null;
  }
}

function renderPidTune(data = null) {
  if (!$('#pidTuneStatus')) return;
  if (!data) {
    $('#pidTuneStatus').textContent = activePidTuneId ? '运行中' : '未运行';
    return;
  }

  $('#pidTuneStatus').textContent = data.status === 'running' ? '运行中' : data.status || '未运行';
  $('#pidTuneStartBtn').disabled = data.status === 'running';
  $('#pidTuneStopBtn').disabled = data.status !== 'running';

  const result = data.result;
  if (result?.ok) {
    $('#pidTuneResult').textContent = [
      `Kp: ${Number(result.kp).toFixed(5)}`,
      `Ki: ${Number(result.ki).toFixed(5)}`,
      `Kd: ${Number(result.kd || 0).toFixed(5)}`,
      '',
      `Ku: ${Number(result.ku).toFixed(5)}`,
      `Pu: ${Number(result.pu).toFixed(5)} s`,
      `振幅: ${Number(result.amplitude).toFixed(3)}`,
      `样本数: ${data.sampleCount}`
    ].join('\n');
  } else if (result && result.ok === false) {
    $('#pidTuneResult').textContent = `自整定失败：${result.message || '没有获得稳定振荡数据'}\n样本数: ${data.sampleCount || 0}`;
  } else {
    $('#pidTuneResult').textContent = `等待自整定结果。\n样本数: ${data.sampleCount || 0}`;
  }

  const sampleLines = (data.samples || []).slice(-80).map((sample) => (
    `${Number(sample.t).toFixed(2)}s speed=${Number(sample.speed).toFixed(3)} input=${Number(sample.control).toFixed(3)}`
  ));
  const txLines = (data.tx || []).slice(-12).map((item) => `TX ${Number(item.value).toFixed(3)}: ${item.command}`);
  const logLines = [...(data.errors || []).map((line) => `ERROR: ${line}`), ...txLines, ...sampleLines, ...(data.logs || []).slice(-20)];
  setScrollableText($('#pidTuneLog'), logLines, '等待串口样本。', 160);
}

async function startPidTune() {
  await stopPidTune(false);
  const port = selectedPidPort();
  if (!port) {
    setStatus('请选择或输入 PID 调试串口。', 'warn');
    return;
  }
  $('#pidTuneStartBtn').disabled = true;
  $('#pidTuneStopBtn').disabled = false;
  $('#pidTuneStatus').textContent = '启动中';
  $('#pidTuneResult').textContent = '正在进行 PI 自整定...';
  $('#pidTuneLog').textContent = '等待串口样本。';
  switchTab('sdk');
  const data = await api('/api/tools/pid-tune/start', {
    method: 'POST',
    body: {
      port,
      baud: $('#pidBaud').value,
      setpoint: $('#pidSetpoint').value,
      bias: $('#pidBias').value,
      relay: $('#pidRelay').value,
      duration: $('#pidDuration').value,
      sampleMs: $('#pidSampleMs').value,
      commandTemplate: $('#pidCommandTemplate').value,
      speedRegex: $('#pidSpeedRegex').value
    }
  });
  activePidTuneId = data.id;
  renderPidTune(data);
  pollPidTune();
  logActivity('PID tune started', 'warn');
}

async function pollPidTune() {
  clearPidTunePoll();
  if (!activePidTuneId) return;
  pidTunePollTimer = setTimeout(async () => {
    try {
      const data = await api('/api/tools/pid-tune/status', {
        method: 'POST',
        body: { id: activePidTuneId }
      });
      renderPidTune(data);
      if (data.status === 'running') {
        pollPidTune();
        return;
      }
      activePidTuneId = '';
      $('#pidTuneStartBtn').disabled = false;
      $('#pidTuneStopBtn').disabled = true;
      setStatus(data.result?.ok ? 'PI 自整定完成' : 'PI 自整定未得到有效结果', data.result?.ok ? 'ok' : 'warn');
      logActivity(data.result?.ok ? 'PID tune completed' : 'PID tune ended without valid result', data.result?.ok ? 'ok' : 'warn');
      await recordMemoryEvent('pid_tune_completed', 'PID 自整定', $('#pidTuneResult').textContent, {
        result: data.result,
        sampleCount: data.sampleCount
      });
    } catch (error) {
      activePidTuneId = '';
      $('#pidTuneStartBtn').disabled = false;
      $('#pidTuneStopBtn').disabled = true;
      showError(error);
    }
  }, 1000);
}

async function stopPidTune(updateStatus = true) {
  clearPidTunePoll();
  if (!activePidTuneId) return;
  $('#pidTuneStopBtn').disabled = true;
  $('#pidTuneStatus').textContent = '停止中';
  const data = await api('/api/tools/pid-tune/stop', {
    method: 'POST',
    body: { id: activePidTuneId }
  });
  renderPidTune(data);
  activePidTuneId = '';
  $('#pidTuneStartBtn').disabled = false;
  $('#pidTuneStopBtn').disabled = true;
  if (updateStatus) {
    setStatus('PID 自整定已停止', 'ok');
    logActivity('PID tune stopped', 'ok');
  }
}

function renderDebugFix() {
  const panel = $('#debugFixOutput');
  const button = $('#debugFixBtn');
  if (!panel || !button) return;
  button.disabled = !state.lastToolResult || state.lastToolResult.result?.ok === true;
  const fix = state.debugFix;
  if (!fix) {
    panel.className = 'validation-list empty';
    panel.textContent = state.lastToolResult
      ? '检测到最近一次工具失败，可发给 agent 自动诊断修复。'
      : '等待 Build / Flash / Monitor 失败输出。';
    return;
  }
  const items = [];
  items.push({ kind: fix.validation?.ok === false ? 'warn' : 'ok', text: fix.note || '已生成调试修复结果。' });
  if (fix.warning) items.push({ kind: 'warn', text: fix.warning });
  for (const error of fix.validation?.errors || []) items.push({ kind: 'error', text: error });
  for (const warning of fix.validation?.warnings || []) items.push({ kind: 'warn', text: warning });
  if ((fix.files || []).length > 0) {
    items.push({ kind: 'ok', text: `已生成 ${fix.files.length} 个候选 project/ 文件，确认后点击“写入 project/”。` });
  }
  panel.className = 'validation-list';
  panel.innerHTML = items.map((item) => `
    <div class="validation-item ${item.kind}">${escapeHtml(item.text)}</div>
  `).join('');
}

function renderMemory() {
  const sessionList = $('#sessionList');
  const timeline = $('#timelineList');
  const memoryMeta = $('#memoryMeta');
  if (!sessionList || !timeline || !memoryMeta) return;
  memoryMeta.textContent = state.currentSessionId ? `当前 ${state.timeline.length}` : '未保存';
  $('#deleteSessionBtn').disabled = !state.currentSessionId;
  $('#clearTimelineBtn').disabled = !state.currentSessionId || state.timeline.length === 0;
  if (state.sessions.length === 0) {
    sessionList.className = 'memory-list empty';
    sessionList.textContent = '暂无会话。';
  } else {
    sessionList.className = 'memory-list';
    sessionList.innerHTML = state.sessions.slice(0, 8).map((session) => `
      <div class="memory-item ${session.id === state.currentSessionId ? 'active' : ''}" data-session-id="${escapeAttr(session.id)}">
        <button type="button" class="memory-open" data-memory-action="open-session">
          <strong>${escapeHtml(session.title)}</strong>
          <span>${escapeHtml(session.stage || '未记录')} / ${new Date(session.updatedAt).toLocaleString()}</span>
        </button>
        <button type="button" class="memory-mini" data-memory-action="delete-session">删除</button>
      </div>
    `).join('');
  }

  if (state.timeline.length === 0) {
    timeline.className = 'memory-list empty';
    timeline.textContent = '暂无历史事件。';
  } else {
    timeline.className = 'memory-list';
    timeline.innerHTML = state.timeline.slice(0, 12).map((event) => `
      <div class="memory-item" data-event-id="${escapeAttr(event.id)}">
        <button type="button" class="memory-open" data-memory-action="restore-event">
          <strong>${escapeHtml(event.title || event.type)}</strong>
          <span>${escapeHtml(event.stage || '')} / ${new Date(event.time).toLocaleString()}</span>
          ${event.summary ? `<em>${escapeHtml(event.summary)}</em>` : ''}
        </button>
        <button type="button" class="memory-mini" data-memory-action="delete-event">删除</button>
      </div>
    `).join('');
  }
}

function summarizeToolOutput(result) {
  if (Array.isArray(result.diagnostics?.keyLines) && result.diagnostics.keyLines.length > 0) {
    return result.diagnostics.keyLines.join('\n');
  }

  const lines = [
    ...String(result.stdout || '').split(/\r?\n/),
    ...String(result.stderr || '').split(/\r?\n/)
  ];
  const picked = [];
  const seen = new Set();

  function push(line) {
    const text = String(line || '').trimEnd();
    if (!text.trim() || seen.has(text)) return;
    seen.add(text);
    picked.push(text);
  }

  if (result.error) {
    push(`ERROR: ${result.error}`);
  }

  for (let index = 0; index < lines.length; index += 1) {
    const line = lines[index];
    const text = line.trim();
    if (!text) continue;

    if (/Build directory exists but is not a valid CMake build directory|Moving it aside:/.test(text)) {
      push(text);
      continue;
    }

    if (/^(FAILED:|ninja:|CMake Error|error:|fatal error:)/i.test(text) || /\bfatal error:/i.test(text)) {
      push(text);
      continue;
    }

    if (/ninja failed with exit code|output of the command is in/i.test(text)) {
      push(text);
    }
  }

  if (picked.length === 0) {
    const tail = lines.map((line) => line.trimEnd()).filter((line) => line.trim()).slice(-24);
    return tail.join('\n') || '工具没有输出。';
  }

  return picked.slice(0, 80).join('\n');
}

async function runTool(kind) {
  const payload = currentPayload();
  const projectPath = `project/${payload.projectName || 'robot_agent_app'}`;
  if (kind === 'monitor') {
    await runMonitorDiagnosisAfterFlash(payload, projectPath);
    return;
  }
  setToolOutputs({ logs: [`正在运行 ${kind}...`], monitorStatus: kind === 'monitor' ? '连接中' : '' });
  switchTab('sdk');
  logActivity(`Running ${kind}...`, 'warn');
  const endpoint = kind === 'monitor' ? '/api/tools/monitor-diagnose' : `/api/tools/${kind}`;
  const data = await api(endpoint, {
    method: 'POST',
    body: {
      projectPath,
      target: payload.target,
      board: payload.board,
      port: payload.port,
      timeoutMs: kind === 'monitor' ? 15000 : 0
    }
  });
  const diagnostics = data.diagnostics || null;
  const effectiveResult = diagnostics
    ? { ...(data.result || {}), ok: !diagnostics.hasErrors, diagnostics }
    : (data.result || {});
  renderToolResult({ ...data, result: effectiveResult, diagnostics });
  const ok = effectiveResult.ok === true;
  state.lastToolResult = {
    tool: kind,
    command: data.command,
    result: effectiveResult,
    summary: diagnostics?.text || summarizeToolOutput(effectiveResult)
  };
  state.debugFix = null;
  state.tools[kind] = {
    ok,
    code: effectiveResult.code ?? null,
    ranAt: new Date().toISOString()
  };
  renderDebugFix();
  renderSteps();
  renderFeedback();
  setStatus(`${kind} finished with code ${effectiveResult.code}`, ok ? 'ok' : 'error');
  logActivity(`${kind} finished with code ${effectiveResult.code}`, ok ? 'ok' : 'error');
  await recordMemoryEvent(`${kind}_${ok ? 'passed' : 'failed'}`, `${kind} ${ok ? '通过' : '失败'}`, state.lastToolResult.summary, {
    tool: kind,
    code: effectiveResult.code
  });

  if (kind === 'flash' && ok) {
    await runMonitorDiagnosisAfterFlash(payload, projectPath);
  }
}

function openFlashPanel() {
  switchTab('sdk');
  setToolOutputs({
    logs: ['已进入 Flash 调试界面。确认端口、目标芯片和板级配置后，点击“开始烧写”。'],
    monitorStatus: '等待烧写'
  });
  setStatus('请在测试调试界面点击“开始烧写”。', 'warn');
  logActivity('Flash panel opened, waiting for confirmation.', 'warn');
  $('#runFlashBtn').focus();
}

function openBuildPanel() {
  switchTab('sdk');
  setToolOutputs({
    logs: ['已进入 Build 调试界面。确认项目文件已写入后，点击“开始 Build”。'],
    monitorStatus: '等待 Build'
  });
  setStatus('请在测试调试界面点击“开始 Build”。', 'warn');
  logActivity('Build panel opened, waiting for confirmation.', 'warn');
  $('#runBuildBtn').focus();
}

function openCodePanel() {
  switchTab('code');
  setStatus('请在固件草稿界面点击“开始生成固件草稿”。', 'warn');
  logActivity('Firmware draft panel opened, waiting for confirmation.', 'warn');
  $('#runCodeBtn').focus();
}

async function runMonitorDiagnosisAfterFlash(payload, projectPath) {
  await stopMonitorSession(false);
  setToolOutputs({ logs: ['Flash 完成，正在连接 monitor...'], monitorStatus: '连接中' });
  switchTab('sdk');
  logActivity('Starting monitor session after flash...', 'warn');
  const data = await api('/api/tools/monitor-session/start', {
    method: 'POST',
    body: {
      projectPath,
      target: payload.target,
      board: payload.board,
      port: payload.port,
      timeoutMs: 60000
    }
  });
  activeMonitorId = data.id;
  $('#disconnectMonitorBtn').disabled = false;
  updateMonitorSessionView(data);
  pollMonitorSession(projectPath);
}

function updateMonitorSessionView(data) {
  const diagnostics = data.diagnostics || { hasErrors: false, keyLines: [], text: '' };
  const logs = data.logs || [];
  const errors = data.errors || diagnostics.keyLines || [];
  setToolOutputs({
    errors,
    logs,
    monitorStatus: data.status === 'running' ? '已连接' : data.status || '未连接'
  });

  const result = {
    ...(data.result || {}),
    ok: !diagnostics.hasErrors,
    diagnostics
  };
  state.lastToolResult = {
    tool: 'monitor',
    command: data.command,
    result,
    summary: diagnostics.text || errors.join('\n') || 'Monitor 未提取到错误信息。'
  };
  state.tools.monitor = {
    ok: !diagnostics.hasErrors,
    code: data.result?.code ?? null,
    ranAt: new Date().toISOString()
  };
  renderDebugFix();
  renderSteps();
  renderFeedback();
}

function clearMonitorPoll() {
  if (monitorPollTimer) {
    clearTimeout(monitorPollTimer);
    monitorPollTimer = null;
  }
}

async function pollMonitorSession(projectPath) {
  clearMonitorPoll();
  if (!activeMonitorId) return;
  monitorPollTimer = setTimeout(async () => {
    try {
      const data = await api('/api/tools/monitor-session/status', {
        method: 'POST',
        body: { id: activeMonitorId }
      });
      updateMonitorSessionView(data);
      const hasErrors = data.diagnostics?.hasErrors === true || (data.errors || []).length > 0;
      if (hasErrors) {
        clearMonitorPoll();
        $('#disconnectMonitorBtn').disabled = true;
        setStatus('Monitor 发现错误，正在交给 agent 重新生成固件...', 'warn');
        logActivity('Monitor diagnostics found errors; requesting firmware fix...', 'warn');
        await stopMonitorSession(false);
        await recordMemoryEvent('monitor_failed', 'monitor 发现错误', state.lastToolResult.summary, {
          tool: 'monitor-session',
          errorCount: data.errors?.length || 0
        });
        await debugFixLastTool();
        return;
      }
      if (data.status === 'running') {
        pollMonitorSession(projectPath);
        return;
      }
      activeMonitorId = '';
      $('#disconnectMonitorBtn').disabled = true;
      setStatus('Monitor 已结束，未提取到错误信息。', 'ok');
      logActivity('Monitor session ended with no errors', 'ok');
      await recordMemoryEvent('monitor_passed', 'monitor 未发现错误', state.lastToolResult.summary, {
        tool: 'monitor-session'
      });
    } catch (error) {
      activeMonitorId = '';
      $('#disconnectMonitorBtn').disabled = true;
      setStatus(error.message, 'error');
      logActivity(error.message, 'error');
    }
  }, 1000);
}

async function stopMonitorSession(updateStatus = true) {
  clearMonitorPoll();
  if (!activeMonitorId) return;
  const data = await api('/api/tools/monitor-session/stop', {
    method: 'POST',
    body: { id: activeMonitorId }
  });
  updateMonitorSessionView(data);
  activeMonitorId = '';
  $('#disconnectMonitorBtn').disabled = true;
  if (updateStatus) {
    setStatus('已断开 monitor 连接。', 'ok');
    logActivity('Monitor disconnected', 'ok');
  }
}

async function debugFixLastTool() {
  if (!state.lastToolResult) {
    setStatus('请先运行 Build / Flash / Monitor 并获得失败输出。', 'warn');
    return;
  }
  const payload = currentPayload();
  const projectPath = `project/${payload.projectName || 'robot_agent_app'}`;
  $('#debugFixOutput').className = 'validation-list empty';
  $('#debugFixOutput').textContent = '正在发给 agent 诊断并生成修复...';
  $('#debugFixBtn').disabled = true;
  switchTab('sdk');
  logActivity('Debug fix requested', 'warn');
  try {
    const data = await api('/api/agent/debug-fix', {
      method: 'POST',
      body: {
        ...payload,
        projectPath,
        requirement: refinedRequirementText(),
        analysis: state.analysis,
        componentSelection: state.componentSelection,
        resourcePlan: state.resourcePlan,
        plan: state.plan,
        files: state.files,
        lastTool: state.lastToolResult,
        tool: state.lastToolResult.tool,
        command: state.lastToolResult.command,
        result: state.lastToolResult.result,
        summary: state.lastToolResult.summary
      }
    });
    state.debugFix = data;
    if ((data.files || []).length > 0) {
      state.files = mergeProjectFileList(state.files, data.files);
      state.validation = data.validation || null;
      state.selectedFilePath = '';
      renderFiles();
      switchTab('code');
    }
    renderDebugFix();
    setStatus(data.note || '调试修复完成', data.validation?.ok === false ? 'warn' : 'ok');
    logActivity(data.note || 'Debug fix complete', data.validation?.ok === false ? 'warn' : 'ok');
    await recordMemoryEvent('debug_fix_generated', 'Agent 自动修复', data.note || '', {
      validation: data.validation,
      fileCount: (data.files || []).length
    });
  } finally {
    renderDebugFix();
  }
}

async function runExec() {
  const command = $('#execCommand').value.trim();
  if (!command) return;
  setToolOutputs({ logs: [`Running ${command}...`] });
  switchTab('sdk');
  logActivity(`Running exec: ${command}`, 'warn');
  const data = await api('/api/tools/exec', {
    method: 'POST',
    body: {
      command,
      argsText: $('#execArgs').value,
      cwd: $('#execCwd').value,
      timeoutMs: 30000
    }
  });
  renderToolResult(data);
  const ok = data.result?.ok === true;
  setStatus(`exec finished with code ${data.result?.code}`, ok ? 'ok' : 'error');
  logActivity(`exec finished with code ${data.result?.code}`, ok ? 'ok' : 'error');
}

async function askQuestion() {
  const question = $('#question').value.trim();
  if (!question) return;
  const log = $('#chatLog');
  log.insertAdjacentHTML('beforeend', `<div class="message user"><strong>你</strong>${escapeHtml(question)}</div>`);
  $('#question').value = '';
  const payload = currentPayload();
  logActivity('Q&A request sent', 'warn');
  const data = await api('/api/agent/chat', {
    method: 'POST',
    body: {
      ...payload,
      question,
      currentPlan: state.plan
    }
  });
  log.insertAdjacentHTML('beforeend', `<div class="message"><strong>Agent</strong><div class="markdown-body compact">${markdownToHtml(data.answer)}</div></div>`);
  log.scrollTop = log.scrollHeight;
  logActivity('Q&A answered', data.warning ? 'warn' : 'ok');
}

async function loadSessions() {
  const data = await api('/api/sessions');
  state.sessions = data.sessions || [];
  renderMemory();
}

async function loadSession(sessionId) {
  const data = await api(`/api/sessions/${encodeURIComponent(sessionId)}`);
  const session = data.session;
  state.currentSessionId = session.id;
  state.timeline = session.timeline || [];
  restoreSnapshot(session.snapshot || {});
  await loadSessions();
  setStatus(`已恢复会话：${session.title}`, 'ok');
  logActivity(`Session restored: ${session.title}`, 'ok');
}

async function deleteCurrentSession(sessionId = state.currentSessionId) {
  if (!sessionId) return;
  await api(`/api/sessions/${encodeURIComponent(sessionId)}`, { method: 'DELETE' });
  if (sessionId === state.currentSessionId) {
    state.currentSessionId = null;
    state.timeline = [];
  }
  await loadSessions();
  renderMemory();
  setStatus('会话已删除', 'ok');
  logActivity('Session deleted', 'ok');
}

async function clearCurrentTimeline() {
  if (!state.currentSessionId) return;
  const data = await api(`/api/sessions/${encodeURIComponent(state.currentSessionId)}/events`, { method: 'DELETE' });
  state.timeline = data.session?.timeline || [];
  await loadSessions();
  renderMemory();
  setStatus('历史已清空', 'ok');
  logActivity('Timeline cleared', 'ok');
}

async function deleteTimelineEvent(eventId) {
  if (!state.currentSessionId || !eventId) return;
  const data = await api(`/api/sessions/${encodeURIComponent(state.currentSessionId)}/events/${encodeURIComponent(eventId)}`, { method: 'DELETE' });
  state.timeline = data.session?.timeline || [];
  await loadSessions();
  renderMemory();
  setStatus('历史事件已删除', 'ok');
}

async function saveSession(event = null, options = {}) {
  const derivedProjectName = preferredProjectName();
  if (derivedProjectName && $('#projectName').value.trim() !== derivedProjectName) {
    $('#projectName').value = derivedProjectName;
  }
  const title = sessionTitle();
  const data = await api('/api/sessions', {
    method: 'POST',
    body: {
      id: state.currentSessionId,
      title,
      snapshot: snapshotState(),
      event
    }
  });
  state.currentSessionId = data.session.id;
  state.timeline = data.session.timeline || [];
  await loadSessions();
  if (!options.silent) {
    setStatus(`会话已保存：${data.session.title}`, 'ok');
    logActivity(`Session saved: ${data.session.title}`, 'ok');
  }
}

async function recordMemoryEvent(type, title, summary = '', payload = {}) {
  if (!state.autosave) return;
  await saveSession({
    type,
    stage: currentStage(),
    title,
    summary,
    payload
  }, { silent: true });
}

function bindEvents() {
  document.querySelectorAll('.tab').forEach((tab) => {
    tab.addEventListener('click', () => switchTab(tab.dataset.tab));
  });
  document.querySelectorAll('[data-jump-tab]').forEach((button) => {
    button.addEventListener('click', () => switchTab(button.dataset.jumpTab));
  });
  $('#providerName').addEventListener('change', () => {
    const provider = state.health.providers.find((item) => item.name === $('#providerName').value);
    $('#modelName').value = provider?.model || '';
    $('#baseUrl').value = provider?.baseUrl || '';
    updateModelChip();
  });
  $('#modelName').addEventListener('input', updateModelChip);
  $('#useLlm').addEventListener('change', updateModelChip);
  $('#autosaveToggle').addEventListener('change', () => {
    state.autosave = $('#autosaveToggle').checked;
    renderMemory();
  });
  $('#refreshSessionsBtn').addEventListener('click', () => loadSessions().catch(showError));
  $('#deleteSessionBtn').addEventListener('click', () => deleteCurrentSession().catch(showError));
  $('#clearTimelineBtn').addEventListener('click', () => clearCurrentTimeline().catch(showError));
  $('#sessionList').addEventListener('click', (event) => {
    const item = event.target.closest('[data-session-id]');
    const action = event.target.closest('[data-memory-action]')?.dataset.memoryAction;
    if (!item || !action) return;
    if (action === 'delete-session') {
      deleteCurrentSession(item.dataset.sessionId).catch(showError);
      return;
    }
    loadSession(item.dataset.sessionId).catch(showError);
  });
  $('#timelineList').addEventListener('click', (event) => {
    const itemElement = event.target.closest('[data-event-id]');
    const action = event.target.closest('[data-memory-action]')?.dataset.memoryAction;
    if (!itemElement || !action) return;
    if (action === 'delete-event') {
      deleteTimelineEvent(itemElement.dataset.eventId).catch(showError);
      return;
    }
    const item = state.timeline.find((entry) => entry.id === itemElement.dataset.eventId);
    if (!item?.snapshot) return;
    restoreSnapshot(item.snapshot);
    setStatus(`已恢复到历史节点：${item.title}`, 'ok');
    logActivity(`Restored event: ${item.title}`, 'ok');
  });
  $('#requirement').addEventListener('input', () => {
    invalidateAnalysisForTyping();
  });
  $('#analysisRefinement').addEventListener('input', () => {
    if (!state.analysis) return;
    state.analysisAccepted = false;
    state.analysisRefinementDirty = $('#analysisRefinement').value.trim().length > 0;
    state.componentSelection = '';
    state.componentSelectionAccepted = false;
    state.componentSelectionDirty = false;
    clearResourcePlan();
    if ($('#componentSelectionNotes')) {
      $('#componentSelectionNotes').value = '';
    }
    state.plan = '';
    state.files = [];
    state.validation = null;
    resetTestToolStateOnly();
    scheduleLightweightWorkflowRefresh(() => {
      $('#analysisMeta').textContent = state.analysisRefinementDirty ? '已补充，待重新分析' : '待确认';
      $('#analysisFullMeta').textContent = state.analysis ? `${state.analysis.length} chars / ${$('#analysisMeta').textContent}` : '等待确认';
      $('#analysisSummary').textContent = state.analysisRefinementDirty ? '需求已补充，请再次功能分析。' : '功能分析完成，可接受或继续细化需求。';
      $('#componentSelectionSummary').textContent = '尚未进行器件选型。';
      $('#resourcePlanSummary').textContent = '尚未进行硬件资源规划。';
      $('#planSummary').textContent = '尚未生成硬件搭建文档。';
      $('#codeSummary').textContent = '尚未生成代码。';
    });
  });
  $('#componentSelectionNotes').addEventListener('input', () => {
    if (!state.componentSelection) return;
    state.componentSelectionAccepted = false;
    state.componentSelectionDirty = $('#componentSelectionNotes').value.trim().length > 0;
    clearResourcePlan();
    state.plan = '';
    state.files = [];
    state.validation = null;
    resetTestToolStateOnly();
    scheduleLightweightWorkflowRefresh(() => {
      $('#componentSelectionMeta').textContent = state.componentSelectionDirty ? '已调整，待再次检索' : '待确认';
      $('#componentSelectionFullMeta').textContent = state.componentSelection ? `${state.componentSelection.length} chars / ${$('#componentSelectionMeta').textContent}` : '等待确认';
      $('#componentSelectionSummary').textContent = state.componentSelectionDirty ? '选型要求已调整，请再次检索。' : '器件选型完成，可接受或继续调整。';
      $('#resourcePlanSummary').textContent = '尚未进行硬件资源规划。';
      $('#planSummary').textContent = '尚未生成硬件搭建文档。';
      $('#codeSummary').textContent = '尚未生成代码。';
    });
  });
  $('#resourcePlanNotes').addEventListener('input', () => {
    if (!state.resourcePlan) return;
    state.resourcePlanAccepted = false;
    state.resourcePlanDirty = $('#resourcePlanNotes').value.trim().length > 0;
    state.plan = '';
    state.files = [];
    state.validation = null;
    resetTestToolStateOnly();
    scheduleLightweightWorkflowRefresh(() => {
      const resourceMeta = state.resourcePlanDirty ? '已调整，待再次规划' : '待确认';
      $('#resourceMeta').textContent = state.resourcePlan ? `${state.resourcePlan.length} chars / ${resourceMeta}` : '来自规划或板级定义';
      $('#resourcePlanSummary').textContent = state.resourcePlanDirty ? '资源要求已调整，请再次规划。' : '硬件资源规划完成，可接受或继续调整。';
      $('#planSummary').textContent = '尚未生成硬件搭建文档。';
      $('#codeSummary').textContent = '尚未生成代码。';
    });
  });
  $('#implementationFeedback').addEventListener('input', () => {
    const hasFeedback = $('#implementationFeedback').value.trim().length > 0;
    state.implementationFeedbackDirty = hasFeedback;
    if (hasFeedback) {
      state.implementationFeedbackApplied = false;
    } else {
      state.implementationFeedbackApplied = false;
    }
    renderFeedback();
    renderSteps();
  });
  $('#loadPromptBtn').addEventListener('click', () => loadSelectedPrompt().catch(showError));
  $('#promptPath').addEventListener('change', () => {
    const selected = $('#promptPath').value;
    $('#promptStatus').textContent = selected || 'manual';
  });
  $('#fileTree').addEventListener('click', (event) => {
    const button = event.target.closest('.file-item');
    if (!button) return;
    state.selectedFilePath = button.dataset.file;
    renderFiles();
  });
  $('#scanBtn').addEventListener('click', () => scanSdk(true).catch(showError));
  $('#analysisBtn').addEventListener('click', () => generateAnalysis().catch(showError));
  $('#rerunAnalysisBtn').addEventListener('click', () => generateAnalysis().catch(showError));
  $('#acceptAnalysisBtn').addEventListener('click', acceptAnalysis);
  $('#componentSelectionBtn').addEventListener('click', () => generateComponentSelection().catch(showError));
  $('#rerunComponentSelectionBtn').addEventListener('click', () => generateComponentSelection().catch(showError));
  $('#acceptComponentSelectionBtn').addEventListener('click', acceptComponentSelection);
  $('#resourcePlanBtn').addEventListener('click', () => generateResourcePlan().catch(showError));
  $('#rerunResourcePlanBtn').addEventListener('click', () => generateResourcePlan().catch(showError));
  $('#acceptResourcePlanBtn').addEventListener('click', acceptResourcePlan);
  $('#hardwareBuildBtn').addEventListener('click', () => generateHardwareBuild().catch(showError));
  $('#codeBtn').addEventListener('click', openCodePanel);
  $('#runCodeBtn').addEventListener('click', () => generateCode().catch(showError));
  $('#writeBtn').addEventListener('click', () => writeProject().catch(showError));
  $('#buildBtn').addEventListener('click', openBuildPanel);
  $('#runBuildBtn').addEventListener('click', () => runTool('build').catch(showError));
  $('#flashBtn').addEventListener('click', openFlashPanel);
  $('#runFlashBtn').addEventListener('click', () => runTool('flash').catch(showError));
  $('#monitorBtn').addEventListener('click', () => runTool('monitor').catch(showError));
  $('#disconnectMonitorBtn').addEventListener('click', () => stopMonitorSession(true).catch(showError));
  $('#debugFixBtn').addEventListener('click', () => debugFixLastTool().catch(showError));
  $('#applyFeedbackBtn').addEventListener('click', () => generateCode().catch(showError));
  $('#pidPortRefreshBtn').addEventListener('click', () => loadSerialPorts().catch(showError));
  $('#pidTuneStartBtn').addEventListener('click', () => startPidTune().catch(showError));
  $('#pidTuneStopBtn').addEventListener('click', () => stopPidTune(true).catch(showError));
  $('#execBtn').addEventListener('click', () => runExec().catch(showError));
  $('#askBtn').addEventListener('click', () => askQuestion().catch(showError));
  $('#question').addEventListener('keydown', (event) => {
    if (event.key === 'Enter') askQuestion().catch(showError);
  });
  $('#saveBtn').addEventListener('click', () => saveSession({
    type: 'manual_save',
    stage: currentStage(),
    title: '手动保存进度',
    summary: currentStage()
  }).catch(showError));
}

function showError(error) {
  setStatus(error.message, 'error');
  logActivity(error.message, 'error');
  console.error(error);
}

async function boot() {
  renderSteps();
  renderSdk();
  renderAnalysis();
  renderComponentSelection();
  renderPlan();
  renderDiagram();
  renderResources();
  renderFiles();
  renderFeedback();
  renderDebugFix();
  renderMemory();
  bindEvents();
  await loadSessions();
  await loadHealth();
  await loadPromptDocuments();
  await scanSdk();
}

boot().catch(showError);
