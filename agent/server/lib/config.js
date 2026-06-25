import path from 'node:path';
import { promises as fs } from 'node:fs';

const defaultConfig = {
  host: process.env.MOCE_AGENT_HOST || '127.0.0.1',
  port: Number(process.env.MOCE_AGENT_PORT || 4173),
  defaultProvider: 'custom',
  execution: {
    enabled: process.env.MOCE_AGENT_EXEC_ENABLED !== '0',
    defaultCwd: process.env.MOCE_AGENT_EXEC_CWD || '.',
    timeoutMs: Number(process.env.MOCE_AGENT_EXEC_TIMEOUT_MS || 30000),
    maxTimeoutMs: Number(process.env.MOCE_AGENT_EXEC_MAX_TIMEOUT_MS || 300000),
    maxOutputBytes: Number(process.env.MOCE_AGENT_EXEC_MAX_OUTPUT_BYTES || 200000),
    allowedPrograms: [
      'bash',
      'cmd',
      'node',
      'npm',
      'npx',
      'powershell',
      'pwsh',
      'python',
      'python3',
      'py',
      'idf.py',
      'git'
    ]
  },
  providers: {
    openai: {
      kind: 'openai-compatible',
      baseUrl: 'https://api.openai.com/v1',
      apiKeyEnv: 'OPENAI_API_KEY',
      model: process.env.OPENAI_MODEL || 'gpt-4.1'
    },
    deepseek: {
      kind: 'openai-compatible',
      baseUrl: 'https://api.deepseek.com/v1',
      apiKeyEnv: 'DEEPSEEK_API_KEY',
      model: process.env.DEEPSEEK_MODEL || 'deepseek-chat'
    },
    openrouter: {
      kind: 'openai-compatible',
      baseUrl: 'https://openrouter.ai/api/v1',
      apiKeyEnv: 'OPENROUTER_API_KEY',
      model: process.env.OPENROUTER_MODEL || 'openai/gpt-4.1'
    },
    anthropic: {
      kind: 'anthropic',
      baseUrl: 'https://api.anthropic.com',
      apiKeyEnv: 'ANTHROPIC_API_KEY',
      model: process.env.ANTHROPIC_MODEL || 'claude-3-7-sonnet-latest'
    },
    gemini: {
      kind: 'gemini',
      baseUrl: 'https://generativelanguage.googleapis.com',
      apiKeyEnv: 'GEMINI_API_KEY',
      model: process.env.GEMINI_MODEL || 'gemini-2.5-pro'
    },
    ollama: {
      kind: 'ollama',
      baseUrl: 'http://127.0.0.1:11434',
      model: process.env.OLLAMA_MODEL || 'qwen2.5-coder:14b'
    },
    custom: {
      kind: 'openai-compatible',
      baseUrl: process.env.CUSTOM_LLM_BASE_URL || 'https://api.ofox.ai/v1',
      apiKeyEnv: 'CUSTOM_LLM_API_KEY',
      model: process.env.CUSTOM_LLM_MODEL || 'openai/gpt-5.4-mini'
    }
  }
};

function parseEnvFile(text) {
  const parsed = {};
  const lines = text.replace(/^\uFEFF/, '').split(/\r?\n/);
  for (const rawLine of lines) {
    const line = rawLine.trim();
    if (!line || line.startsWith('#')) {
      continue;
    }

    const eq = line.indexOf('=');
    if (eq <= 0) {
      continue;
    }

    const key = line.slice(0, eq).trim();
    let value = line.slice(eq + 1).trim();
    if ((value.startsWith('"') && value.endsWith('"')) || (value.startsWith("'") && value.endsWith("'"))) {
      value = value.slice(1, -1);
    }
    parsed[key] = value;
  }
  return parsed;
}

async function loadLocalEnv(agentRoot) {
  const envFiles = ['.env', '.env.local'];
  for (const envFile of envFiles) {
    const filePath = path.join(agentRoot, envFile);
    try {
      const env = parseEnvFile(await fs.readFile(filePath, 'utf8'));
      for (const [key, value] of Object.entries(env)) {
        if (process.env[key] === undefined) {
          process.env[key] = value;
        }
      }
    } catch (error) {
      if (error.code !== 'ENOENT') {
        throw error;
      }
    }
  }
}

function mergeConfig(base, override) {
  return {
    ...base,
    ...override,
    execution: {
      ...base.execution,
      ...(override.execution || {})
    },
    providers: {
      ...base.providers,
      ...(override.providers || {})
    }
  };
}

export async function loadConfig(agentRoot) {
  await loadLocalEnv(agentRoot);

  const configPath = path.join(agentRoot, 'agent.config.json');
  try {
    const raw = await fs.readFile(configPath, 'utf8');
    return mergeConfig(defaultConfig, JSON.parse(raw));
  } catch (error) {
    if (error.code === 'ENOENT') {
      return defaultConfig;
    }
    throw error;
  }
}
