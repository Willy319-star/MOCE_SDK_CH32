# MOCE_SDK_CH32

CH32V203G6U6 microcontroller SDK with I2C, CAN, MPU6050, VL53L0X support.

## Build

```bash
cmake -B build -DAPP=<app> -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-riscv.cmake -G "Ninja"
ninja -C build
tools/ch32fun/minichlink/minichlink.exe -w build/examples/<app>/<app>.bin 0x08000000
```

### Examples

| App | 说明 |
|---|---|
| `led_blink` | LED blink 测试 |
| `smoke_test` | 串口打印 + LED 心跳 |
| `mpu6050_readout` | I2C 读取 MPU6050 六轴数据 |
| `vl53l0x_readout` | I2C 读取 VL53L0X 测距 |
| `can_ping_pong` | 双板 CAN 通信 (NODE_A 发 PING, NODE_B 回 PONG) |
| `can_test` | 单板 CAN 回环自测 (不需收发器) |
| `i2c_to_can` | I2C→CAN 桥接 (MPU6050→CAN 帧广播) |

## Hardware notes

- **No external crystal** — clock is HSI→PLL→96MHz (`system_ch32v20x.c`)
- **CAN requires SN65HVD230 transceiver** on PA11/PA12
- **I2C1**: PB6=SCL, PB7=SDA
- **UART1**: PA9=TX, PA10=RX (115200 baud)

## Toolchain

- RISC-V GCC: `tools/xpack-riscv-none-elf-gcc-15.2.0-1/`
- Flasher: `tools/ch32fun/minichlink/minichlink.exe`
- CMake toolchain: `cmake/toolchain-riscv.cmake`



















<!-- cloude-code-toolbox:mcp-skills-awareness-begin -->

### MCP & Skills awareness (Cloude Code ToolBox)

_Last synced: 2026-07-23T02:54:34.676Z._

- **Full report:** `.claude/cloude-code-toolbox-mcp-skills-awareness.md` in this workspace (auto-overwritten on each scan). Use it as ground truth for configured servers and skill folders.
- **MCP:** For **live tools** in Claude Code, enable the matching server via `/mcp`. Servers are configured in `~/.claude.json` (user) and `.mcp.json` (project).
- **When the user’s task matches a server** (e.g. Confluence work and a **Confluence** / **Atlassian** MCP is listed), **prefer that server id** and plan on tool use—not only file search.
- **Skills:** Folders below contain `SKILL.md`; attach or cite paths in chat when relevant.

#### Workspace MCP

- `d:\MOCE_SDK_CH32\.mcp.json` _(workspace: MOCE_SDK_CH32)_ — _file missing_

_No active workspace servers in mcp.json._

#### User MCP

- `C:\Users\EDY\.claude.json` — _no servers defined_

_No active user-scoped servers in mcp.json._

#### Project skills

_None found (or no workspace open)._

#### User skills

_None found._

<!-- cloude-code-toolbox:mcp-skills-awareness-end -->
