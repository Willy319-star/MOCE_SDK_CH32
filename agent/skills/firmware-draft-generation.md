# 固件草稿生成 Skill

运行时调用：`/api/agent/codegen` 在“嵌入式开发：固件草稿”阶段会自动读取本文件，并将其注入到 LLM 的 system messages 中。用户无需在界面中手动选择该 skill。

目标：基于已接受的需求分析、器件选型、硬件资源规划和硬件搭建文档，生成只位于 `project/` 下的 ESP-IDF 应用工程草稿。代码必须优先复用当前 SDK 已存在的 `components/`、`bsp/`、`boards/` 能力，参考`examples/`中的例程,避免臆造头文件、函数、组件或底层硬件访问。

适用场景：

- 已完成硬件搭建，需要生成 `project/<project_name>/` 下的固件草稿。
- 需要把硬件资源规划中的模块、接口、地址和板级宏落实到应用初始化与业务状态机。
- 需要在不修改 SDK 的前提下组合已有 driver/service 组件。
- 需要确保生成代码可被当前构建系统解析，并能进入后续 Build / Flash / Monitor 测试调试步骤。
- 生成的代码需要针对各个外设有充足的日志信息打印，便于monitor排查问题。

输入：

- 用户原始需求和补充需求。
- 已接受的功能分析，尤其是 `状态机需求判断`、`状态机设计`、`状态转移条件`、`状态机完整性检查` 和 `状态机状态转移图`。
- 已接受的硬件搭建文档，包括模块连接框图、接线清单、接口分组和待确认项。
- `./prompt/prompt0.md` 全局开发约束。
- 当前 SDK 扫描摘要，包括 `components/`、`bsp/`、`boards/`、`examples/`。
- 目标板卡名称和 `boards/<board>/board.h` 中的 `BOARD_*` 资源定义。
- 可用 API 清单，包括真实存在的公共头文件、函数原型、类型、结构体字段、枚举和宏。

核心原则：

1. **先检查组件存在性**
   - 生成任何 `#include "driver_xxx.h"` 或 `#include "service_xxx.h"` 前，必须确认 `components/` 扫描结果中存在对应组件。
   - 如果硬件搭建文档提到某个模块，但 `components/` 中没有对应 driver/service，只能在 `project/` 里降级为 TODO、日志或状态占位，不要引用不存在的组件。
   - 组件存在后，还必须检查目标板卡 `board.h` 是否定义该组件及其 CMake 传递依赖使用的全部 `BOARD_*` 宏。
   - 缺少任意必需 `BOARD_*` 宏时，禁止 include、调用或在 `REQUIRES` 中加入该组件；只能生成 TODO、日志或最小可编译降级。
   - 例如 `driver_motor` 会传递依赖 `driver_tb6612` 和 `driver_encoder`，必须同时具备 `BOARD_MOTOR_*` 与 `BOARD_ENCODER_*` 宏，否则禁止使用电机驱动组件。
   - 不允许臆造组件名、头文件名、函数名、类型名、枚举名或结构体字段。

2. **优先调用 components 的公共接口**
   - 已有 `driver_*` 或 `service_*` 能力时，优先调用组件公共 API。
   - 不要绕过组件直接操作 I2C/UART/PWM/GPIO 底层 BSP，除非当前 SDK 没有上层组件且可用 API 明确提供了 BSP 公共接口。
   - 优先使用 `*_init_default()`、`*_get_default_config()`、`*_init_profile()` 等组件提供的默认初始化路径。

3. **头文件必须真实且依赖匹配**
   - 只能引用“可用 API”和“BSP 公共头文件”中列出的真实公共头文件，或 ESP-IDF/FreeRTOS 标准头。
   - ESP-IDF/FreeRTOS 标准头可以使用尖括号或双引号，例如 `<stdio.h>`、`"esp_log.h"`、`"freertos/FreeRTOS.h"`、`"freertos/task.h"`。
   - 板级宏头文件是 `board.h`，不是 `bsp_board.h`。
   - 引用组件头文件时，`project/<name>/main/CMakeLists.txt` 的 `REQUIRES` 必须包含对应组件名。
   - 引用 `board.h` 时，`REQUIRES` 必须包含 `bsp_board`。
   - 不要因为包含 ESP-IDF/FreeRTOS 标准头而把头文件名加入 `REQUIRES`。例如包含 `"esp_log.h"` 不代表可以写 `REQUIRES esp_log`，包含 `"freertos/task.h"` 也不代表要写 `REQUIRES task`。
   - `REQUIRES` 只能写当前 SDK 扫描到的 `components/`、`bsp/` 组件名，以及确实存在且被调用的 ESP-IDF 组件；禁止加入 `esp_log`、`esp_err`、`sdkconfig`、`FreeRTOS`、`task` 等头文件名。

4. **只生成 project/ 应用工程**
   - 严格遵守 `./prompt/prompt0.md`：只允许创建、修改或覆盖 `project/` 下文件。
   - 禁止输出或建议修改 `components/`、`boards/`、`bsp/`、`tools/`、`env/`、`third_party/`、`examples/`。
   - 如发现 SDK 能力不足，只能在应用代码中保留 TODO 或最小可编译降级，不要修改 SDK。

5. **CMake 必须符合当前 ESP-IDF 写法**
   - 根 `project/<name>/CMakeLists.txt` 必须是 ESP-IDF 工程入口，不是组件入口。
   - 根 `CMakeLists.txt` 第一条有效 CMake 命令必须包含 `cmake_minimum_required(VERSION 3.16)` 或更高兼容版本。
   - 根 `CMakeLists.txt` 必须设置 `EXTRA_COMPONENT_DIRS` 到 `../../bsp` 和 `../../components`，也可以使用 `${CMAKE_CURRENT_LIST_DIR}/../../bsp` 和 `${CMAKE_CURRENT_LIST_DIR}/../../components`。
   - 根 `CMakeLists.txt` 必须 `include($ENV{IDF_PATH}/tools/cmake/project.cmake)` 并调用 `project(<name>)`。
   - 根 `CMakeLists.txt` 禁止出现 `idf_component_register()`、`register_component()`、`SRCS`、`REQUIRES` 等组件级写法。
   - `project/<name>/main/CMakeLists.txt` 才能使用 `idf_component_register(SRCS "main.c" REQUIRES ...)`。
   - `REQUIRES` 中不要重复写同一个组件。
   - `REQUIRES` 中不要加入仅由标准头文件带来的伪组件，例如 `esp_log`、`esp_err`、`task`。
   - 禁止使用 `register_component()`、`COMPONENT_SRCS`、`COMPONENT_REQUIRES`、`COMPONENT_ADD_INCLUDEDIRS` 等旧式写法。

6. **代码行为保守可编译**
   - 不把 `void` 返回值当成 `esp_err_t`。
   - 对返回 `esp_err_t` 的函数检查错误；对 `void` 函数直接调用。
   - 声明 `static const char *TAG` 或 `#define TAG` 时，必须至少有一处 `ESP_LOGI/W/E/D(TAG, ...)` 使用；如果不输出日志，就不要声明 `TAG`，也不要包含 `esp_log.h`。
   - 使用 `snprintf()` 写入固定长度显示行或小缓冲区时，所有来自数组/状态字段的 `%s` 都必须带精度限制，例如 `"I2C:%.19s"`；或者目标缓冲区必须足够容纳前缀、最长源字符串和结尾 `\0`。
   - 不允许把较长数组字符串直接通过无界 `%s` 写入更小的数组，例如 `char line[24]` 接收 `char scan[48]`。
   - 配置结构体字段必须完全匹配扫描到的头文件定义；例如 `bsp_uart_config_t` 使用 `tx_gpio`、`rx_gpio`，禁止臆造 `tx_pin`、`rx_pin`。
   - 不要留下未调用的 `static` helper 函数、未使用的局部变量或未使用的全局状态；如果只是预留逻辑，写 TODO 注释而不是生成死代码。
   - 未确认的硬件参数、地址、阈值或业务策略使用清晰的 `TODO` 注释或保守默认值。
   - FreeRTOS 循环必须包含合理 `vTaskDelay()`，避免忙等。

7. **必须落实已接受的应用状态机**
   - 如果功能分析中 `状态机需求判断` 为“是”，固件必须按已接受的状态机实现，不要另起一套状态、命名、触发条件或业务流程。
   - 必须把 `状态机设计` 中的状态落实为清晰的 `enum`、状态变量或等价结构；状态名可以转换为 C 命名风格，但语义必须一致。
   - 必须把 `状态转移条件` 中的每条 From/To/条件映射为代码中的判断分支、事件处理或状态转移函数。
   - 必须把 `状态机完整性检查` 中标为“风险 / 待确认”的项目转成代码中的 TODO、日志或保守降级，不要静默忽略。
   - 进入状态时执行一次性的副作用，例如 OLED 刷新、TTS 播报、启动动作；循环中执行持续采样或控制；离开状态时清理必要标志，避免重复播报、重复刷新或重复启动执行器。
   - 如果多个事件同时触发，必须按功能分析中的优先级处理；若分析未明确，使用保守安全优先级：急停/错误 > 安全保护/报警 > 用户命令 > 普通周期刷新，并写清楚注释。
   - 如果功能分析中 `状态机需求判断` 为“否”，仍需保持最小主循环/初始化流程清晰，不要生成无意义的复杂状态机。

生成前自检：

1. 从硬件搭建文档提取需要用到的 SDK 组件。
2. 对照当前 `components/` 扫描结果，列出“可用组件”和“缺失组件”。
3. 对照目标板卡 `board.h`，检查可用组件及其传递依赖所需的 `BOARD_*` 宏是否全部已定义。
4. 只为“组件存在且板级宏完整”的组件生成 `#include`、函数调用和 `REQUIRES`。
5. 对照可用 API 原型检查每个函数调用的参数、返回值、类型和枚举。
6. 确认每个引用的头文件都真实存在，且 CMake 依赖完整。
7. 确认输出文件全部在 `project/<project_name>/` 下。
8. 确认 `main/CMakeLists.txt` 的 `REQUIRES` 没有把 ESP-IDF/FreeRTOS 标准头误写成组件名。
9. 确认根 `CMakeLists.txt` 和 `main/CMakeLists.txt` 没有写反：根文件必须包含 `cmake_minimum_required()`、`include(project.cmake)`、`project()`；组件文件必须包含 `idf_component_register()`。
10. 若功能分析要求状态机，确认 `main.c` 中的状态枚举、状态变量、转移函数/分支、进入副作用和持续任务与功能分析中的状态机一致。

输出格式：

```text
===== FILE: project/<project_name>/CMakeLists.txt =====
<完整文件内容>

===== FILE: project/<project_name>/main/CMakeLists.txt =====
<完整文件内容>

===== FILE: project/<project_name>/main/main.c =====
<完整文件内容>
```

如有额外源文件或头文件，继续使用相同 `===== FILE:` 文件块格式。不要输出解释文字、Markdown 包裹、diff 或 SDK 目录文件。

质量检查：

- 是否严格遵守 `prompt/prompt0.md` 的目录边界。
- 是否先检查 `components/` 中存在相应组件。
- 是否只引用真实公共头文件。
- 是否优先调用 driver/service 组件接口，而不是直接访问底层 BSP。
- 是否为每个组件头文件配置了对应 `REQUIRES`。
- 是否避免 `bsp_board.h`、不存在的 include、旧式 CMake 和错误返回值处理。
- 如果功能分析包含状态机，代码是否逐条落实状态、转移条件、事件优先级、进入/退出副作用和完整性检查结论。
