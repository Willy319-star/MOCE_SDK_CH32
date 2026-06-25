const ERROR_PATTERNS = [
  /\bE \(\d+\) /,
  /\bESP_ERR_[A-Z0-9_]+\b/,
  /\b(?:error|failed|failure|fatal|panic|abort|assert|exception|timeout|timed out)\b/i,
  /Guru Meditation/i,
  /Backtrace:/i,
  /Core \d+ register dump/i,
  /rst:0x[0-9a-f]+/i,
  /ELF file SHA256/i
];

const WARNING_PATTERNS = [
  /\bW \(\d+\) .*?(?:failed|error|timeout|invalid|not found|unsupported)/i,
  /\bwarning:\b/i
];

const CONTINUATION_PATTERNS = [
  /^\s+0x[0-9a-fA-F]+/,
  /^\s*0x[0-9a-fA-F]+:/,
  /^\s*#[0-9]+\s+/,
  /^\s*at\s+/,
  /^\s*from\s+/,
  /^\s*\[[0-9]+\/[0-9]+\]/,
  /^\s*\^+$/,
  /compilation terminated/i,
  /Backtrace:/i
];

const SOURCE_DIAGNOSTIC_PATTERN = /^((?:[A-Za-z]:[\\/])?[^:\n]+?):(\d+)(?::(\d+))?:\s*(fatal error|error|warning|note):\s*(.*)$/i;
const CMAKE_AT_PATTERN = /^(CMake\s+(?:Error|Warning))\s+at\s+(.+?):(\d+)(?:\s+\(([^)]+)\))?:\s*(.*)$/i;
const CMAKE_IN_PATTERN = /^(CMake\s+(?:Error|Warning))\s+in\s+(.+?):\s*(.*)$/i;

export function cleanMonitorLine(line) {
  return String(line || '')
    .replace(/\x1b\[[0-9;?]*[A-Za-z]/g, '')
    .trimEnd();
}

export function isMonitorDiagnosticLine(line) {
  const text = cleanMonitorLine(line).trim();
  if (!text) return false;
  return ERROR_PATTERNS.some((pattern) => pattern.test(text))
    || WARNING_PATTERNS.some((pattern) => pattern.test(text));
}

function isContinuationLine(line) {
  const text = cleanMonitorLine(line);
  return CONTINUATION_PATTERNS.some((pattern) => pattern.test(text));
}

function cleanToolLine(line) {
  return cleanMonitorLine(line).trimEnd();
}

function makeLocation({ file, line = null, column = null, severity = 'error', message = '', raw = '' }) {
  const normalizedSeverity = String(severity || 'error').toLowerCase();
  return {
    file,
    line: line == null ? null : Number(line),
    column: column == null ? null : Number(column),
    severity: normalizedSeverity,
    message: String(message || '').trim(),
    raw: String(raw || '').trim()
  };
}

function formatLocation(location) {
  const position = [
    location.file,
    location.line == null ? '' : location.line,
    location.column == null ? '' : location.column
  ].filter((part) => part !== '').join(':');
  const prefix = location.severity ? location.severity.toUpperCase() : 'ERROR';
  return `${prefix} ${position}${location.message ? ` ${location.message}` : ''}`;
}

function parseLocation(line) {
  const text = cleanToolLine(line).trim();
  let match = text.match(SOURCE_DIAGNOSTIC_PATTERN);
  if (match) {
    return makeLocation({
      file: match[1],
      line: match[2],
      column: match[3] || null,
      severity: match[4],
      message: match[5],
      raw: text
    });
  }

  match = text.match(CMAKE_AT_PATTERN);
  if (match) {
    const message = [match[5], match[4] ? `(${match[4]})` : ''].filter(Boolean).join(' ').trim();
    return makeLocation({
      file: match[2],
      line: match[3],
      severity: match[1].includes('Warning') ? 'warning' : 'error',
      message,
      raw: text
    });
  }

  match = text.match(CMAKE_IN_PATTERN);
  if (match) {
    return makeLocation({
      file: match[2],
      severity: match[1].includes('Warning') ? 'warning' : 'error',
      message: match[3],
      raw: text
    });
  }

  return null;
}

function isToolDiagnosticLine(line) {
  const text = cleanToolLine(line).trim();
  if (!text) return false;
  return parseLocation(text) !== null
    || /Build directory exists but is not a valid CMake build directory|Moving it aside:/i.test(text)
    || /^(FAILED:|ninja:|make:|CMake Error|CMake Warning|error:|fatal error:)/i.test(text)
    || /\bfatal error:|\berror:|\bundefined reference to\b/i.test(text)
    || /ninja failed with exit code|make failed with exit code|cmake failed with exit code|output of the command is in/i.test(text);
}

function isToolContinuationLine(line) {
  const raw = cleanToolLine(line);
  const text = raw.trim();
  return !!text && (
    /^\^+/.test(text)
    || /^~+\^?/.test(text)
    || /^[ \t]+/.test(raw)
    || /^note:/i.test(text)
    || /^warning:/i.test(text)
    || /^error:/i.test(text)
    || /^compilation terminated/i.test(text)
    || /^In file included from /i.test(text)
    || /^from /i.test(text)
    || /^Please look out for component/i.test(text)
    || /^Refer to /i.test(text)
    || /^No cmake_minimum_required command/i.test(text)
    || /^A line of code such as/i.test(text)
    || /^The version specified/i.test(text)
    || /^For more information/i.test(text)
    || /^Manually-specified variables were not used/i.test(text)
    || /^\s*\d+\s*\|/.test(text)
  );
}

export function extractToolDiagnostics(result = {}, options = {}) {
  const maxLines = Number(options.maxLines || 120);
  const rawLines = [
    ...String(result.stdout || '').split(/\r?\n/),
    ...String(result.stderr || '').split(/\r?\n/),
    String(result.error || '')
  ].map(cleanToolLine);

  const picked = [];
  const locations = [];
  const seen = new Set();
  const seenLocations = new Set();

  function push(line) {
    const text = cleanToolLine(line);
    if (!text.trim() || seen.has(text)) return;
    seen.add(text);
    picked.push(text);
  }

  function pushLocation(location) {
    const key = [
      location.file,
      location.line ?? '',
      location.column ?? '',
      location.severity,
      location.message
    ].join('|');
    if (seenLocations.has(key)) return;
    seenLocations.add(key);
    locations.push(location);
    push(`定位: ${formatLocation(location)}`);
  }

  for (let index = 0; index < rawLines.length; index += 1) {
    const line = rawLines[index];
    const location = parseLocation(line);
    if (location) {
      pushLocation(location);
      if (location.raw && location.raw !== formatLocation(location)) {
        push(location.raw);
      }
      for (let lookahead = 1; lookahead <= 4; lookahead += 1) {
        const next = rawLines[index + lookahead];
        if (!next || !isToolContinuationLine(next)) break;
        push(next);
      }
      continue;
    }

    if (!isToolDiagnosticLine(line)) {
      continue;
    }

    push(line);
    for (let lookahead = 1; lookahead <= 4; lookahead += 1) {
      const next = rawLines[index + lookahead];
      if (!next || !isToolContinuationLine(next)) break;
      const nextLocation = parseLocation(next);
      if (nextLocation) {
        pushLocation(nextLocation);
      } else {
        push(next);
      }
    }
  }

  const keyLines = picked.slice(0, maxLines);
  const errorLocations = locations.filter((location) => location.severity !== 'warning' && location.severity !== 'note');
  return {
    hasErrors: result.ok === false || errorLocations.length > 0 || keyLines.some((line) => /\b(error|failed|fatal)\b/i.test(line)),
    keyLines,
    locations: locations.slice(0, maxLines),
    text: keyLines.join('\n'),
    totalLines: rawLines.filter((line) => line.trim()).length,
    truncated: picked.length > keyLines.length || result.truncated === true
  };
}

export function extractMonitorDiagnostics(result = {}, options = {}) {
  const maxLines = Number(options.maxLines || 120);
  const rawLines = [
    ...String(result.stdout || '').split(/\r?\n/),
    ...String(result.stderr || '').split(/\r?\n/),
    String(result.error || '')
  ].map(cleanMonitorLine);

  const picked = [];
  const seen = new Set();

  function push(line) {
    const text = cleanMonitorLine(line);
    if (!text.trim() || seen.has(text)) return;
    seen.add(text);
    picked.push(text);
  }

  for (let index = 0; index < rawLines.length; index += 1) {
    const line = rawLines[index];
    if (!isMonitorDiagnosticLine(line)) {
      continue;
    }

    push(line);
    for (let lookahead = 1; lookahead <= 4; lookahead += 1) {
      const next = rawLines[index + lookahead];
      if (!next || !isContinuationLine(next)) break;
      push(next);
    }
  }

  const keyLines = picked.slice(0, maxLines);
  return {
    hasErrors: keyLines.length > 0,
    keyLines,
    text: keyLines.join('\n'),
    totalLines: rawLines.filter((line) => line.trim()).length,
    truncated: picked.length > keyLines.length || result.truncated === true
  };
}
