# MOCE SDK CH32

MOCE SDK CH32 是一个面向 CH32V203 的轻量级 SDK 框架，整体风格参考
MOCE STM32 SDK。该工程面向 WCH CH32 标准外设库开发，不使用 HAL。

默认开发板为：

```text
ch32v203g6u6
```

## 仓库结构

```text
boards/                 Board startup, clock, pins, GPIO, UART
components/mcu_port/    Thin MCU port layer over WCH standard library
components/bsp/         Peripheral BSP drivers
user_api/               Vibe-style user API and runtime
examples/               Example applications
project/                User projects
toolchain/              CMake toolchain files
linker/                 Linker scripts
scripts/                Build/setup helper scripts
third_party/            Downloaded WCH SDK location
tools/                  Downloaded toolchain/flash tools location
```

## Windows 环境准备

请先安装以下主机工具：

```text
Git for Windows
Python 3
CMake
Ninja
```

然后运行：

```powershell
powershell -ExecutionPolicy Bypass -File scripts\setup_windows.ps1
```

该 setup 脚本会下载或克隆：

```text
tools/xpack-riscv-none-elf-gcc-15.2.0-1/
third_party/ch32v20x_repo/
tools/ch32fun/
```

这些自动下载生成的目录已经在 Git 中忽略，不会提交到仓库。

## 编译

编译一个 example：

```powershell
python scripts\build.py --board ch32v203g6u6 --app led_blink
```

其他 example：

```powershell
python scripts\build.py --board ch32v203g6u6 --app mpu6050_readout
python scripts\build.py --board ch32v203g6u6 --app vl53l0x_readout
```

编译产物会生成到：

```text
build/<board>/<app>/
```

## 烧录

编译 `led_blink` 后，可以使用下面的命令烧录：

```powershell
tools\ch32fun\minichlink\minichlink.exe -w build\ch32v203g6u6\led_blink\examples\led_blink\led_blink.bin flash -b
```

如果要烧录其他 app，请替换路径中的 app 名称。例如：

```powershell
tools\ch32fun\minichlink\minichlink.exe -w build\ch32v203g6u6\mpu6050_readout\examples\mpu6050_readout\mpu6050_readout.bin flash -b
```

## 支持的开发板

### CH32V203G6U6

当前开发板外设引脚映射：

```text
UART1 TX: PA9
UART1 RX: PA10
I2C1 SCL: PB6
I2C1 SDA: PB7
SPI1 CS : PA4
SPI1 SCK: PA5
SPI1 MISO: PA6
SPI1 MOSI: PA7
CAN RX  : PA11
CAN TX  : PA12
```

## 添加新的外设

推荐按下面的结构添加：

```text
components/mcu_port/    MCU-level protocol helpers, if needed
components/bsp/         Device driver, board-aware BSP
examples/<name>/        Minimal serial-printing example
```

对于 I2C 传感器，建议先写一个保守、容易调试的版本：

```text
1. Check device address ACK.
2. Read chip ID register.
3. Wake/configure the device.
4. Read raw registers.
5. Print raw and scaled values over UART.
```

对于自画板或自画模块，建议在 `docs/` 或 example 的 README 中记录供电、
上拉电阻、地址选择脚以及已知的读写时序问题。

## GitHub 工作流

本仓库建议使用下面的 GitHub 工作流：

```powershell
git init
git add .
git commit -m "Initial MOCE SDK CH32"
git branch -M main
git remote add origin https://github.com/<your-name>/MOCE_SDK_CH32.git
git push -u origin main
```

`.gitignore` 会将编译产物、下载的 SDK 和下载的工具链排除在仓库之外。
新的 clone 可以通过运行 `scripts\setup_windows.ps1` 恢复这些依赖。

## 常见问题

如果 CMake 找不到 `riscv-none-elf-gcc`，请重新运行 setup：

```powershell
powershell -ExecutionPolicy Bypass -File scripts\setup_windows.ps1
```

如果烧录失败，请检查：

```text
WCH-LinkE driver is installed
WCH-LinkE is connected
Target board is powered
No serial/debug tool is holding the device
```

如果缺少 `minichlink.exe`，可以重新构建或重新安装 ch32fun：

```powershell
powershell -ExecutionPolicy Bypass -File scripts\setup_windows.ps1 -SkipToolchain -SkipSdk -Force
```
