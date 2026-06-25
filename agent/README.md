# Moce Hardware Agent

`agent/` 是 Moce SDK 的本地 Web 智能硬件开发 agent。首版目标是把当前“写 prompt -> 调用大模型 -> 在 `project/` 下生成应用”的流程产品化，并扩展到机器人/智能硬件的需求、任务拆解、器件选型、资源规划、代码生成和调试闭环。

## 当前能力

- 扫描当前 SDK：`components/`、`bsp/`、`boards/`、`examples/`、`project/`、prompt 约束。
- GUI 工作台：需求输入、skillset 流程导航、规划输出、框图、器件/资源、代码草稿、Q&A、工具调用。
- LLM provider：OpenAI-compatible、OpenAI、DeepSeek、OpenRouter、Anthropic、Gemini、Ollama、Custom。
- 无 API key 时自动使用本地 fallback 规划和代码脚手架。
- 代码写入时强制限制在 `project/` 下。
- Build、Flash、Monitor 会按运行平台调用仓库脚本：Linux/macOS 使用 `tools/*.sh`，Windows 使用 `tools/*.ps1`。Monitor 默认采集 15 秒后返回，非交互环境使用串口采集器。
- SDK / 工具页支持受控执行文件或程序：`/api/tools/exec` 会把 cwd 限制在 SDK 根目录内，默认不走 shell，并带超时、输出截断和程序白名单。
- physical agent 暂无仓库实现，首版提供占位适配层。

## 启动

在仓库根目录执行：

```powershell
cd agent
npm.cmd start
```

默认地址：

```text
http://127.0.0.1:4173
```

PowerShell 默认可能禁止 `npm.ps1`，Windows 下使用 `npm.cmd`。

## 配置大模型

可以直接在 GUI 里选择 provider、model、base URL 和 API key。API key 只发送到本机后端，不会写入仓库。

也可以复制配置模板：

```powershell
Copy-Item config.example.json agent.config.json
```

Agent 启动时会自动读取 `agent/.env` 和 `agent/.env.local`。这两个文件已被 git 忽略，适合保存本地 API Key：

```text
DEEPSEEK_API_KEY=...
OPENAI_API_KEY=...
```

也可以直接设置系统环境变量，例如：

```powershell
$env:OPENAI_API_KEY="..."
$env:DEEPSEEK_API_KEY="..."
$env:ANTHROPIC_API_KEY="..."
$env:GEMINI_API_KEY="..."
```

## 执行配置

通用执行入口默认开启，可在 `agent.config.json` 中调整：

```json
{
  "execution": {
    "enabled": true,
    "defaultCwd": ".",
    "timeoutMs": 30000,
    "maxTimeoutMs": 300000,
    "maxOutputBytes": 200000,
    "allowedPrograms": ["bash", "cmd", "node", "npm", "npx", "powershell", "pwsh", "python", "python3", "py", "idf.py", "git"]
  }
}
```

## 设计边界

- agent 可以读取 SDK 摘要。
- agent 只能向 `project/` 写入生成工程。
- agent 可以在 SDK 根目录内执行允许的程序或仓库内脚本，但不会用通用执行入口直接执行 inline shell 命令。
- agent 不修改 `components/`、`boards/`、`tools/`、`env/`、`third_party/`、`examples/`。
- 原理图、PCB、datasheet 和量产设计功能首版只提供 workflow 和接口预留，不能替代人工硬件审核。

## 后续扩展

- 将静态 GUI 替换为 React/Vite，同时复用当前 REST API。
- 接入 physical agent 的 serial/TCP/UDP 自动测试协议。
- 增加 datasheet PDF 抽取、元件库、网表生成和 ERC 检查。
- 增加会话 diff、文件级审查和一键 build/flash/monitor 的实时日志流。
