# Physical Agent 接入 Skill

运行时调用：`/api/agent/codegen` 在“嵌入式开发：固件草稿”阶段检测到用户要求接入 Physical Agent、上位机控制、WiFi 执行器协议或 `service_physical_agent` 时，会自动读取本文件，并将其注入到 LLM 的 system messages 中。本 skill 是 `firmware-draft-generation.md` 的附加约束，不替代固件草稿生成约束。

目标：让 ESP32 固件作为 **Physical Agent 的 WiFi 执行器** 被上位机 watch 进程控制。固件必须暴露稳定的通信协议、能力清单、状态观测和动作执行入口；同时生成后续可提交给上位机工程的 Physical Agent 配置/driver 草稿。

- Physical Agent 的核心边界是：`agent -> ACTIONS.md -> watch safety gate -> driver.execute(action) -> 硬件`。
- ESP32 固件提供执行器接口：等待上位机连接，接收动作，执行或更新状态机，返回结果/状态。
- 上位机 Physical Agent 侧应通过 Python driver 实现 `PhysicalDriver.connect()`、`health()`、`observe()`、`capabilities()`、`execute(action)`。

适用场景：

- 用户要求“接入 physical agent”、“上位机 agent 控制 ESP32”、“通过 WiFi 通信”、“ESP32 作为执行器”。
- 固件需要支持 Physical Agent/watch 通过 WiFi 下发动作，例如 `stop`、`drive`、`turn`、`set_light`、`set_mode`。
- 需要在生成固件的同时生成上位机 Physical Agent 可使用的 `physical_driver.yaml`、`driver.py`、`physical-agent.yaml` 草稿。
- 生成上位机配置时必须参考 `physical_agent_config/template/` 下的模板，并把实际生成结果放到 `physical_agent_config/config/`。

输入：

- 用户需求、实现情况反馈和硬件搭建文档。
- 当前 SDK 扫描摘要，尤其是 `service_wifi` 与 `service_physical_agent`。
- 目标板卡资源和已选择的硬件模块。
- 上位机 Physical Agent driver contract：`PhysicalDriver`、`Action`、`ActionResult`、`Capability`、`HealthStatus`、`Observation`。
- 本 skill 下方定义的上位机 driver 类、方法、动作和返回值约定。

上位机 driver contract：

- `PhysicalDriver` 是上位机 watch 侧加载的 Python driver 基类。
- `connect()`：初始化与 ESP32 执行器的连接。
- `disconnect()`：关闭连接并释放资源。
- `health()`：返回 `HealthStatus(ok, message, details)`，用于说明 ESP32 是否在线、协议版本、端口和最近错误。
- `observe()`：返回 `Observation(summary, robots, objects, environment, raw)`，用于把 ESP32 当前状态写入上位机世界状态。
- `capabilities()`：返回 `Capability` 列表，每个能力包含 `name`、`description`、`params_schema`、可选 `returns_schema`、`constraints`、`requires_approval`、`timeout_s`。
- `execute(action)`：接收 `Action(id, robot, capability, params, reason, depends_on)`，把动作转换成 ESP32 WiFi RPC，并返回 `ActionResult(status, message, result, artifacts)`。
- `ActionResult.status` 只能使用 `completed`、`failed`、`cancelled`。
- 上位机 driver 不直接操作 ESP32 外设；它只做协议桥接、参数校验、超时处理和结果转换。

核心原则：

1. **ESP32 执行器接口**
   - 固件提供通信协议和硬件能力入口：`health`、`capabilities`、`observe`、`execute`、`stop`。
   - 动作来源是上位机 Physical Agent/watch；固件按请求执行动作或更新本地状态机。

2. **必须使用 SDK 组件接口**
   - 若接入 Physical Agent，必须优先使用 `service_physical_agent` 组件。
   - 固件必须 `#include "service_physical_agent.h"`，并在 `main/CMakeLists.txt` 的 `REQUIRES` 中加入 `service_physical_agent`。
   - 如果需要 WiFi STA 连接，必须使用 `service_wifi`，并在 `REQUIRES` 中加入 `service_wifi`。
   - 禁止在应用层重新手写 socket server、JSON-RPC server 或绕过 `service_physical_agent` 直接做协议解析。
   - 若 SDK 扫描结果中没有 `service_physical_agent` 或 `service_wifi`，只能生成 TODO/占位，不要 include 或调用不存在组件。

3. **execute 必须非阻塞**
   - `service_physical_agent` 的 `on_execute` 回调运行在协议服务任务中。
   - `on_execute` 只能更新状态机目标、投递队列、设置控制变量或触发短操作；必须尽快返回。
   - 禁止在 `on_execute` 中长时间循环等待电机到位、等待传感器条件或执行完整长动作。
   - 长动作应由应用自己的控制任务执行；Physical Agent 通过 `observe` 轮询 `busy`、`last_action_id`、`status` 等字段判断进度。

4. **stop 必须高优先级**
   - 必须注册 `on_stop`。
   - `on_stop` 应尽快置位急停状态、清空目标、停止电机/舵机/PWM 或进入安全态。
   - `execute("stop")` 和 `method:"stop"` 都应映射到同一急停逻辑。
   - 不要把 stop 实现成普通长动作，不要等待其他动作完成。

5. **协议语义**
   - 固件监听 TCP newline JSON-RPC，默认端口 `8080`。
   - 请求示例：

```json
{"id":1,"method":"health","params":{}}
{"id":2,"method":"capabilities","params":{}}
{"id":3,"method":"observe","params":{}}
{"id":4,"method":"execute","params":{"capability":"drive","args":{"speed":40,"duration_ms":1000}}}
{"id":5,"method":"stop","params":{}}
```

   - 成功响应：

```json
{"id":4,"ok":true,"result":{"status":"accepted","message":"drive target updated"}}
```

   - 失败响应：

```json
{"id":4,"ok":false,"error":{"code":"INVALID_PARAM","message":"ESP_ERR_INVALID_ARG"}}
```

6. **capabilities 必须能被上位机 driver 映射**
   - `capabilities_json` 必须是 JSON 数组字符串。
   - 每个 capability 至少包含 `name`、`description`、`params_schema`。
   - `params_schema` 使用 JSON Schema 风格，便于 Physical Agent safety gate 校验。
   - 推荐至少提供：
     - `observe`
     - `stop`
     - 与需求相关的动作，例如 `drive`、`turn`、`set_light`、`set_mode`、`set_speed`、`set_servo`、`say`。

7. **observe 必须返回执行器状态**
   - `on_observe` 应返回当前硬件状态，建议字段：
     - `status`: `idle` / `busy` / `error` / `stopped`
     - `busy`: boolean
     - `emergency_stop`: boolean
     - `last_action`: string
     - `last_action_id`: string 或 number
     - `sensors`: object
     - `actuators`: object
   - 传感器字段要稳定，不要每次生成改名。

8. **上位机 Physical Agent 模板与生成位置**
   - 模板目录固定为 `physical_agent_config/template/`：
     - `physical_agent_config/template/physical_driver.yaml`
     - `physical_agent_config/template/driver.py`
     - `physical_agent_config/template/physical-agent.yaml`
   - Moce Designer Agent 生成出的上位机配置文件必须放到 `physical_agent_config/config/`：
     - `physical_agent_config/config/physical_driver.yaml`
     - `physical_agent_config/config/driver.py`
     - `physical_agent_config/config/physical-agent.yaml`
     - 可选：`physical_agent_config/config/README.md`
   - 这是接入 Physical Agent 时仅针对上位机配置草稿的输出例外；ESP-IDF 固件工程文件仍必须放在 `project/<project_name>/`。
   - 生成时应以模板结构为基础替换占位符，并根据当前固件能力更新 `capabilities_yaml`、`capabilities_python`、`robot_id`、`driver_name`、`driver_class`、`esp32_host` 等字段。
   - 不要把上位机配置草稿生成到 `project/<project_name>/physical_agent/`。

9. **上位机 driver 草稿要求**
   - `driver.py` 必须实现一个继承 `PhysicalDriver` 的类。
   - `connect()` 连接 ESP32 TCP 服务。
   - `health()` 调用 `health` RPC。
   - `observe()` 调用 `observe` RPC，并转换为 `Observation`。
   - `capabilities()` 返回与固件 `capabilities_json` 一致的 `Capability` 列表。
   - `execute(action)` 把 Physical Agent 的 `Action` 转为固件 `execute` RPC；`stop` 使用 `method:"stop"` 或 `execute stop`。
   - driver 必须保留 `mode: mock`，便于 Physical Agent 在没有硬件时加载和验证。
   - driver 仅负责协议桥接、参数校验、超时处理和结果转换。

10. **固件文件要求**
    - `main.c` 中必须先连接 WiFi，再启动 `service_physical_agent_start()`。
    - `main/CMakeLists.txt` 必须包含 `service_physical_agent`；若连接 WiFi，还必须包含 `service_wifi`。
    - 若未提供 WiFi 凭据，使用 `CONFIG_*` 或清晰 TODO，不要硬编码真实密码。
    - 回调返回的 JSON 字段片段必须是合法 JSON object fields，不带最外层 `{}`。
    - 不要把复杂 JSON 拼接写进小缓冲区；使用足够大的缓冲区和 `snprintf` 精度限制。

推荐固件结构：

```c
static volatile bool s_emergency_stop;
static volatile bool s_busy;
static char s_last_action[32];

static esp_err_t physical_health_cb(char *out, size_t out_size, void *user_ctx);
static esp_err_t physical_observe_cb(char *out, size_t out_size, void *user_ctx);
static esp_err_t physical_execute_cb(const char *capability, const char *args_json, char *out, size_t out_size, void *user_ctx);
static esp_err_t physical_stop_cb(char *out, size_t out_size, void *user_ctx);
static void control_task(void *arg);
```

推荐输出文件：

```text
===== FILE: project/<project_name>/CMakeLists.txt =====
<ESP-IDF 工程入口>

===== FILE: project/<project_name>/main/CMakeLists.txt =====
<REQUIRES service_wifi service_physical_agent 以及实际硬件组件>

===== FILE: project/<project_name>/main/main.c =====
<固件主程序，包含 WiFi 和 Physical Agent 协议服务>

===== FILE: physical_agent_config/config/physical_driver.yaml =====
<上位机 driver manifest 草稿>

===== FILE: physical_agent_config/config/driver.py =====
<上位机 PhysicalDriver 草稿>

===== FILE: physical_agent_config/config/physical-agent.yaml =====
<上位机项目配置片段或完整示例>

===== FILE: physical_agent_config/config/README.md =====
<如何提交到上位机 Physical Agent 工程并运行 watch/run 的说明>
```

上位机配置草稿示例：

```yaml
schema: physical-agent/driver/v1
name: moce_wifi_executor
version: 0.1.0
entrypoint:
  module: driver
  class: MoceWifiExecutorDriver
robot:
  kind: executor
  supports_simulation: true
config_schema:
  type: object
  properties:
    mode:
      type: string
      enum: [mock, wifi]
      default: mock
    host:
      type: string
    port:
      type: integer
      minimum: 1
      maximum: 65535
      default: 8080
    timeout_s:
      type: number
      minimum: 0.1
      default: 3
  required: [mode]
  additionalProperties: false
dependencies:
  python: []
capability_contract:
  source: runtime
```

质量检查：

- ESP32 固件是否明确作为执行器等待上位机指令。
- 是否使用 `service_physical_agent` 而不是手写协议服务。
- `on_execute` 是否非阻塞。
- `on_stop` 是否高优先级、短路径、安全。
- `capabilities_json` 是否与上位机 `driver.py` 的 `capabilities()` 一致。
- `observe` 是否能让上位机判断当前动作状态。
- 上位机配置是否参考 `physical_agent_config/template/`，并输出到 `physical_agent_config/config/`。
- 固件约束、组件存在性检查、头文件真实性和 CMake 依赖约束是否仍完全遵守。
