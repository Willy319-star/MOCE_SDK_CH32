# MOCE SDK CH32

MOCE SDK CH32 is a small CH32V203-oriented SDK framework modeled after the
MOCE STM32 SDK style. It targets WCH CH32 standard peripheral library
development, not HAL.

The default board is:

```text
ch32v203g6u6
```

## Repository Layout

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

## Windows Setup

Install these host tools first:

```text
Git for Windows
Python 3
CMake
Ninja
```

Then run:

```powershell
powershell -ExecutionPolicy Bypass -File scripts\setup_windows.ps1
```

The setup script downloads or clones:

```text
tools/xpack-riscv-none-elf-gcc-15.2.0-1/
third_party/ch32v20x_repo/
tools/ch32fun/
```

These generated/downloaded folders are intentionally ignored by Git.

## Build

Build an example:

```powershell
python scripts\build.py --board ch32v203g6u6 --app led_blink
```

Other examples:

```powershell
python scripts\build.py --board ch32v203g6u6 --app mpu6050_readout
python scripts\build.py --board ch32v203g6u6 --app vl53l0x_readout
```

The build output is placed under:

```text
build/<board>/<app>/
```

## Flash

After building `led_blink`, flash it with:

```powershell
tools\ch32fun\minichlink\minichlink.exe -w build\ch32v203g6u6\led_blink\examples\led_blink\led_blink.bin flash -b
```

For another app, replace the app name in the path. For example:

```powershell
tools\ch32fun\minichlink\minichlink.exe -w build\ch32v203g6u6\mpu6050_readout\examples\mpu6050_readout\mpu6050_readout.bin flash -b
```

## Supported Board

### CH32V203G6U6

Current board-level peripheral mapping:

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

## Adding A New Peripheral

Recommended structure:

```text
components/mcu_port/    MCU-level protocol helpers, if needed
components/bsp/         Device driver, board-aware BSP
examples/<name>/        Minimal serial-printing example
```

For I2C sensors, prefer a conservative first version:

```text
1. Check device address ACK.
2. Read chip ID register.
3. Wake/configure the device.
4. Read raw registers.
5. Print raw and scaled values over UART.
```

For custom boards/modules, document power, pullups, address pins, and known
read timing quirks in `docs/` or in the example README.

## GitHub Workflow

This repository is designed for the following workflow:

```powershell
git init
git add .
git commit -m "Initial MOCE SDK CH32"
git branch -M main
git remote add origin https://github.com/<your-name>/MOCE_SDK_CH32.git
git push -u origin main
```

The `.gitignore` keeps build products, downloaded SDKs, and downloaded
toolchains out of the repository. A fresh clone can restore them by running
`scripts\setup_windows.ps1`.

## Troubleshooting

If CMake cannot find `riscv-none-elf-gcc`, run setup again:

```powershell
powershell -ExecutionPolicy Bypass -File scripts\setup_windows.ps1
```

If flashing fails, check:

```text
WCH-LinkE driver is installed
WCH-LinkE is connected
Target board is powered
No serial/debug tool is holding the device
```

If `minichlink.exe` is missing, build or reinstall ch32fun:

```powershell
powershell -ExecutionPolicy Bypass -File scripts\setup_windows.ps1 -SkipToolchain -SkipSdk -Force
```
