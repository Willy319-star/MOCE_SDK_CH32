function trimTrailingSlash(value) {
  return String(value || '').replace(/\/+$/, '');
}

function resolveProvider(config, providerRequest = {}) {
  const name = providerRequest.name || config.defaultProvider || 'openai';
  const template = config.providers[name] || config.providers.custom;
  if (!template) {
    throw new Error(`unknown LLM provider: ${name}`);
  }

  return {
    name,
    kind: providerRequest.kind || template.kind || 'openai-compatible',
    baseUrl: providerRequest.baseUrl || template.baseUrl,
    model: providerRequest.model || template.model,
    apiKey: providerRequest.apiKey || process.env[template.apiKeyEnv || ''] || '',
    apiKeyEnv: template.apiKeyEnv || ''
  };
}

export function listProviderTemplates(config) {
  return Object.entries(config.providers).map(([name, provider]) => ({
    name,
    kind: provider.kind,
    baseUrl: provider.baseUrl,
    model: provider.model,
    apiKeyEnv: provider.apiKeyEnv || ''
  }));
}

async function postJson(url, headers, body) {
  let response;
  try {
    response = await fetch(url, {
      method: 'POST',
      headers: {
        'content-type': 'application/json',
        ...headers
      },
      body: JSON.stringify(body)
    });
  } catch (error) {
    const reason = error.cause?.code || error.cause?.message || error.message;
    throw new Error(`LLM transport failed: ${reason}`);
  }

  const text = await response.text();
  let data = null;
  try {
    data = text ? JSON.parse(text) : null;
  } catch {
    data = { raw: text };
  }

  if (!response.ok) {
    const message = data?.error?.message || data?.message || text || response.statusText;
    throw new Error(`LLM request failed (${response.status}): ${message}`);
  }
  return data;
}

function splitSystem(messages) {
  const system = messages
    .filter((message) => message.role === 'system')
    .map((message) => message.content)
    .join('\n\n');
  const conversation = messages.filter((message) => message.role !== 'system');
  return { system, conversation };
}

async function callOpenAiCompatible(provider, messages, temperature) {
  if (!provider.apiKey && !provider.baseUrl.includes('127.0.0.1') && !provider.baseUrl.includes('localhost')) {
    throw new Error(`missing API key for ${provider.name}. Set ${provider.apiKeyEnv} or enter a key in the GUI.`);
  }

  const data = await postJson(
    `${trimTrailingSlash(provider.baseUrl)}/chat/completions`,
    provider.apiKey ? { authorization: `Bearer ${provider.apiKey}` } : {},
    {
      model: provider.model,
      messages,
      temperature
    }
  );

  return data?.choices?.[0]?.message?.content || '';
}

async function callAnthropic(provider, messages, temperature) {
  if (!provider.apiKey) {
    throw new Error(`missing API key for ${provider.name}. Set ${provider.apiKeyEnv} or enter a key in the GUI.`);
  }

  const { system, conversation } = splitSystem(messages);
  const data = await postJson(
    `${trimTrailingSlash(provider.baseUrl)}/v1/messages`,
    {
      'x-api-key': provider.apiKey,
      'anthropic-version': '2023-06-01'
    },
    {
      model: provider.model,
      max_tokens: 4096,
      temperature,
      system,
      messages: conversation.map((message) => ({
        role: message.role === 'assistant' ? 'assistant' : 'user',
        content: message.content
      }))
    }
  );

  return (data?.content || [])
    .filter((part) => part.type === 'text')
    .map((part) => part.text)
    .join('\n');
}

async function callGemini(provider, messages, temperature) {
  if (!provider.apiKey) {
    throw new Error(`missing API key for ${provider.name}. Set ${provider.apiKeyEnv} or enter a key in the GUI.`);
  }

  const { system, conversation } = splitSystem(messages);
  const data = await postJson(
    `${trimTrailingSlash(provider.baseUrl)}/v1beta/models/${encodeURIComponent(provider.model)}:generateContent?key=${encodeURIComponent(provider.apiKey)}`,
    {},
    {
      system_instruction: system ? { parts: [{ text: system }] } : undefined,
      contents: conversation.map((message) => ({
        role: message.role === 'assistant' ? 'model' : 'user',
        parts: [{ text: message.content }]
      })),
      generationConfig: { temperature }
    }
  );

  return (data?.candidates?.[0]?.content?.parts || [])
    .map((part) => part.text || '')
    .join('\n');
}

async function callOllama(provider, messages, temperature) {
  const data = await postJson(`${trimTrailingSlash(provider.baseUrl)}/api/chat`, {}, {
    model: provider.model,
    messages,
    stream: false,
    options: { temperature }
  });
  return data?.message?.content || '';
}

export async function callLlm({ config, providerRequest, messages, temperature = 0.2 }) {
  const provider = resolveProvider(config, providerRequest);
  if (!provider.baseUrl || !provider.model) {
    throw new Error(`provider ${provider.name} is missing baseUrl or model`);
  }

  if (provider.kind === 'anthropic') {
    return callAnthropic(provider, messages, temperature);
  }
  if (provider.kind === 'gemini') {
    return callGemini(provider, messages, temperature);
  }
  if (provider.kind === 'ollama') {
    return callOllama(provider, messages, temperature);
  }
  return callOpenAiCompatible(provider, messages, temperature);
}
