import path from 'node:path';
import { promises as fs } from 'node:fs';

import { loadBoardResourcesFromDefines, loadComponentBusResourcesFromDefines } from './sdkResources.js';

async function exists(filePath) {
  try {
    await fs.access(filePath);
    return true;
  } catch {
    return false;
  }
}

async function listDirectories(root) {
  try {
    const entries = await fs.readdir(root, { withFileTypes: true });
    return entries.filter((entry) => entry.isDirectory()).map((entry) => entry.name).sort();
  } catch {
    return [];
  }
}

async function listFiles(root, predicate = () => true) {
  const result = [];
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
      } else if (predicate(fullPath)) {
        result.push(fullPath);
      }
    }
  }
  await walk(root);
  return result.sort();
}

function parseFunctions(headerText) {
  const functionPattern = /^\s*(?!typedef\b|#)(?:[A-Za-z_][A-Za-z0-9_]*\s+)*(?:const\s+)?(?:[A-Za-z_][A-Za-z0-9_]*|\*)[\s*]+([A-Za-z_][A-Za-z0-9_]*)\s*\([^;{}]*\)\s*;/gm;
  const names = [];
  let match = functionPattern.exec(headerText);
  while (match) {
    names.push(match[1]);
    match = functionPattern.exec(headerText);
  }
  return names;
}

function parseFunctionPrototypes(headerText) {
  const functionPattern = /^\s*((?!typedef\b|#)(?:[A-Za-z_][A-Za-z0-9_]*\s+)*(?:const\s+)?(?:[A-Za-z_][A-Za-z0-9_]*|\*)[\s*]+([A-Za-z_][A-Za-z0-9_]*)\s*\([^;{}]*\)\s*;)/gm;
  const prototypes = [];
  let match = functionPattern.exec(headerText);
  while (match) {
    prototypes.push(match[1].replace(/\s+/g, ' ').trim());
    match = functionPattern.exec(headerText);
  }
  return prototypes;
}

function parseTypeDeclarations(headerText) {
  const declarations = [];
  const enumPattern = /typedef\s+enum\s*(?:[A-Za-z_][A-Za-z0-9_]*)?\s*\{[\s\S]*?\}\s*[A-Za-z_][A-Za-z0-9_]*\s*;/g;
  const structPattern = /typedef\s+struct\s*(?:[A-Za-z_][A-Za-z0-9_]*)?\s*\{[\s\S]*?\}\s*[A-Za-z_][A-Za-z0-9_]*\s*;/g;
  const definePattern = /^\s*#define\s+(DRIVER_[A-Z0-9_]+|SERVICE_[A-Z0-9_]+)\b.*$/gm;

  for (const pattern of [enumPattern, structPattern, definePattern]) {
    let match = pattern.exec(headerText);
    while (match) {
      declarations.push(match[0].replace(/\s+\n/g, '\n').trim());
      match = pattern.exec(headerText);
    }
  }
  return declarations;
}

function parsePublicDefines(headerText) {
  const defines = {};
  const definePattern = /^\s*#define\s+([A-Z][A-Z0-9_]+)\s+(.+?)\s*(?:\/\*.*)?$/gm;
  let match = definePattern.exec(headerText);
  while (match) {
    defines[match[1]] = match[2].trim();
    match = definePattern.exec(headerText);
  }
  return defines;
}

function parseBoardDefines(boardText) {
  const defines = {};
  const definePattern = /^\s*#define\s+(BOARD_[A-Z0-9_]+)\s+(.+?)\s*(?:\/\*.*)?$/gm;
  let match = definePattern.exec(boardText);
  while (match) {
    defines[match[1]] = match[2].trim();
    match = definePattern.exec(boardText);
  }
  return defines;
}

function parseBoardMacroRefs(text) {
  return [...new Set(String(text || '').match(/\bBOARD_[A-Z0-9_]+\b/g) || [])].sort();
}

function parseCmakeRequires(cmakeText) {
  const match = String(cmakeText || '').match(/idf_component_register\s*\(([\s\S]*?)\)/);
  if (!match) return [];
  const body = match[1].replace(/#[^\n]*/g, ' ');
  const requires = [];
  for (const keyword of ['REQUIRES', 'PRIV_REQUIRES']) {
    const requiresMatch = body.match(new RegExp(`\\b${keyword}\\b([\\s\\S]*?)(?:\\bREQUIRES\\b|\\bPRIV_REQUIRES\\b|\\bSRCS\\b|\\bINCLUDE_DIRS\\b|\\bPRIV_INCLUDE_DIRS\\b|$)`));
    if (!requiresMatch) continue;
    requires.push(...requiresMatch[1]
      .split(/\s+/)
      .map((item) => item.trim())
      .filter(Boolean)
      .filter((item) => !/^["')]+$/.test(item)));
  }
  return [...new Set(requires)].sort();
}

async function scanComponent(root, name) {
  const includeRoot = path.join(root, name, 'include');
  const headers = await listFiles(includeRoot, (filePath) => filePath.endsWith('.h'));
  const api = [];
  const publicDefines = {};
  for (const header of headers) {
    const text = await fs.readFile(header, 'utf8');
    Object.assign(publicDefines, parsePublicDefines(text));
    api.push({
      header: path.relative(root, header).replaceAll('\\', '/'),
      functions: parseFunctions(text),
      prototypes: parseFunctionPrototypes(text),
      declarations: parseTypeDeclarations(text)
    });
  }
  const sourceFiles = await listFiles(path.join(root, name), (filePath) => /\.(?:c|h|hpp|cpp|cc)$/.test(filePath));
  const sourceTexts = await Promise.all(sourceFiles.map((filePath) => fs.readFile(filePath, 'utf8').catch(() => '')));
  const cmakePath = path.join(root, name, 'CMakeLists.txt');
  const cmakeText = (await exists(cmakePath)) ? await fs.readFile(cmakePath, 'utf8') : '';
  return {
    name,
    api,
    requires: parseCmakeRequires(cmakeText),
    requiredBoardMacros: [...new Set(sourceTexts.flatMap((text) => parseBoardMacroRefs(text)))].sort(),
    busResources: loadComponentBusResourcesFromDefines(name, publicDefines)
  };
}

export async function scanSdk(sdkRoot) {
  const componentsRoot = path.join(sdkRoot, 'components');
  const bspRoot = path.join(sdkRoot, 'bsp');
  const boardsRoot = path.join(sdkRoot, 'boards');
  const examplesRoot = path.join(sdkRoot, 'examples');
  const projectRoot = path.join(sdkRoot, 'project');

  const componentNames = await listDirectories(componentsRoot);
  const components = [];
  for (const component of componentNames) {
    components.push(await scanComponent(componentsRoot, component));
  }

  const boards = [];
  for (const board of await listDirectories(boardsRoot)) {
    const boardHeader = path.join(boardsRoot, board, 'board.h');
    const boardPinsHeader = path.join(boardsRoot, board, 'board_pins.h');
    const defaults = path.join(boardsRoot, board, 'sdkconfig.defaults');
    const boardText = [
      (await exists(boardHeader)) ? await fs.readFile(boardHeader, 'utf8') : '',
      (await exists(boardPinsHeader)) ? await fs.readFile(boardPinsHeader, 'utf8') : ''
    ].join('\n');
    const defines = parseBoardDefines(boardText);
    boards.push({
      name: board,
      hasBoardHeader: await exists(boardHeader),
      hasBoardPinsHeader: await exists(boardPinsHeader),
      hasSdkconfigDefaults: await exists(defaults),
      defines,
      resources: loadBoardResourcesFromDefines(defines)
    });
  }

  const prompt0 = path.join(sdkRoot, 'prompt', 'prompt0.md');
  const sdkDevelop = path.join(sdkRoot, 'SDK_prompt', 'sdk_develop.md');

  return {
    scannedAt: new Date().toISOString(),
    sdkRoot,
    components,
    bsp: await listDirectories(bspRoot),
    boards,
    examples: await listDirectories(examplesRoot),
    projects: await listDirectories(projectRoot),
    docs: (await listFiles(path.join(sdkRoot, 'docs'))).map((filePath) => path.relative(sdkRoot, filePath).replaceAll('\\', '/')),
    prompts: {
      hasPrompt0: await exists(prompt0),
      prompt0: (await exists(prompt0)) ? await fs.readFile(prompt0, 'utf8') : '',
      hasSdkDevelop: await exists(sdkDevelop),
      sdkDevelop: (await exists(sdkDevelop)) ? await fs.readFile(sdkDevelop, 'utf8') : ''
    }
  };
}
