import path from 'node:path';
import { promises as fs } from 'node:fs';

async function ensureDataDir(agentRoot) {
  const dataDir = path.join(agentRoot, 'data');
  await fs.mkdir(dataDir, { recursive: true });
  return dataDir;
}

async function readJsonSafe(filePath, fallback) {
  try {
    return JSON.parse(await fs.readFile(filePath, 'utf8'));
  } catch (error) {
    if (error.code === 'ENOENT') {
      return fallback;
    }
    throw error;
  }
}

export async function loadSessions(agentRoot) {
  const dataDir = await ensureDataDir(agentRoot);
  const sessions = await readJsonSafe(path.join(dataDir, 'sessions.json'), []);
  return sessions.map(normalizeSession).map((session) => ({
    id: session.id,
    title: session.title,
    updatedAt: session.updatedAt,
    createdAt: session.createdAt,
    stage: session.snapshot?.stage || '',
    eventCount: session.timeline.length
  }));
}

export async function loadSession(agentRoot, sessionId) {
  const dataDir = await ensureDataDir(agentRoot);
  const filePath = path.join(dataDir, 'sessions.json');
  const sessions = await readJsonSafe(filePath, []);
  const session = sessions.map(normalizeSession).find((item) => item.id === sessionId);
  if (!session) {
    return null;
  }
  return session;
}

function normalizeSession(session) {
  const now = new Date().toISOString();
  const snapshot = session.snapshot || {
    requirement: session.requirement || '',
    plan: session.plan || '',
    files: Array.isArray(session.files) ? session.files : []
  };
  return {
    ...session,
    title: session.title || 'Untitled hardware agent session',
    snapshot,
    timeline: Array.isArray(session.timeline) ? session.timeline : [],
    updatedAt: session.updatedAt || now,
    createdAt: session.createdAt || now
  };
}

function eventFromInput(eventInput, snapshot) {
  const now = new Date().toISOString();
  return {
    id: eventInput.id || `evt_${Date.now()}_${Math.random().toString(16).slice(2, 8)}`,
    time: eventInput.time || now,
    type: eventInput.type || 'snapshot_saved',
    stage: eventInput.stage || snapshot?.stage || '',
    title: eventInput.title || '保存进度',
    summary: eventInput.summary || '',
    payload: eventInput.payload || {},
    snapshot
  };
}

export async function saveSession(agentRoot, sessionInput) {
  const dataDir = await ensureDataDir(agentRoot);
  const filePath = path.join(dataDir, 'sessions.json');
  const sessions = (await readJsonSafe(filePath, [])).map(normalizeSession);
  const now = new Date().toISOString();
  const snapshot = sessionInput.snapshot || {
    requirement: sessionInput.requirement || '',
    refinement: sessionInput.refinement || '',
    componentSelectionNotes: sessionInput.componentSelectionNotes || '',
    resourcePlanNotes: sessionInput.resourcePlanNotes || '',
    analysis: sessionInput.analysis || '',
    analysisAccepted: !!sessionInput.analysisAccepted,
    componentSelection: sessionInput.componentSelection || '',
    componentSelectionAccepted: !!sessionInput.componentSelectionAccepted,
    resourcePlan: sessionInput.resourcePlan || '',
    resourcePlanAccepted: !!sessionInput.resourcePlanAccepted,
    plan: sessionInput.plan || '',
    files: Array.isArray(sessionInput.files) ? sessionInput.files : []
  };
  const existing = sessions.find((item) => item.id === sessionInput.id);
  const session = {
    id: sessionInput.id || `session_${Date.now()}`,
    title: sessionInput.title || 'Untitled hardware agent session',
    snapshot,
    timeline: existing?.timeline || [],
    updatedAt: now,
    createdAt: existing?.createdAt || sessionInput.createdAt || now
  };

  if (sessionInput.event) {
    session.timeline = [
      eventFromInput(sessionInput.event, snapshot),
      ...session.timeline
    ].slice(0, 200);
  }

  const index = sessions.findIndex((item) => item.id === session.id);
  if (index >= 0) {
    sessions[index] = session;
  } else {
    sessions.unshift(session);
  }
  await fs.writeFile(filePath, JSON.stringify(sessions.slice(0, 50), null, 2), 'utf8');
  return session;
}

export async function appendSessionEvent(agentRoot, sessionId, eventInput) {
  const dataDir = await ensureDataDir(agentRoot);
  const filePath = path.join(dataDir, 'sessions.json');
  const sessions = (await readJsonSafe(filePath, [])).map(normalizeSession);
  const index = sessions.findIndex((item) => item.id === sessionId);
  if (index < 0) {
    return null;
  }
  const session = sessions[index];
  const snapshot = eventInput.snapshot || session.snapshot;
  session.snapshot = snapshot;
  session.timeline = [
    eventFromInput(eventInput, snapshot),
    ...session.timeline
  ].slice(0, 200);
  session.updatedAt = new Date().toISOString();
  sessions[index] = session;
  await fs.writeFile(filePath, JSON.stringify(sessions, null, 2), 'utf8');
  return session;
}

export async function deleteSession(agentRoot, sessionId) {
  const dataDir = await ensureDataDir(agentRoot);
  const filePath = path.join(dataDir, 'sessions.json');
  const sessions = (await readJsonSafe(filePath, [])).map(normalizeSession);
  const next = sessions.filter((item) => item.id !== sessionId);
  if (next.length === sessions.length) {
    return false;
  }
  await fs.writeFile(filePath, JSON.stringify(next, null, 2), 'utf8');
  return true;
}

export async function deleteSessionEvent(agentRoot, sessionId, eventId) {
  const dataDir = await ensureDataDir(agentRoot);
  const filePath = path.join(dataDir, 'sessions.json');
  const sessions = (await readJsonSafe(filePath, [])).map(normalizeSession);
  const index = sessions.findIndex((item) => item.id === sessionId);
  if (index < 0) {
    return null;
  }
  const session = sessions[index];
  session.timeline = session.timeline.filter((event) => event.id !== eventId);
  session.updatedAt = new Date().toISOString();
  sessions[index] = session;
  await fs.writeFile(filePath, JSON.stringify(sessions, null, 2), 'utf8');
  return session;
}

export async function clearSessionTimeline(agentRoot, sessionId) {
  const dataDir = await ensureDataDir(agentRoot);
  const filePath = path.join(dataDir, 'sessions.json');
  const sessions = (await readJsonSafe(filePath, [])).map(normalizeSession);
  const index = sessions.findIndex((item) => item.id === sessionId);
  if (index < 0) {
    return null;
  }
  const session = sessions[index];
  session.timeline = [];
  session.updatedAt = new Date().toISOString();
  sessions[index] = session;
  await fs.writeFile(filePath, JSON.stringify(sessions, null, 2), 'utf8');
  return session;
}
