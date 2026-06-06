# MOCE SDK CH32 porting notes

This framework keeps the same user-facing style as MOCE_SDK_STM32:

```text
examples/project -> user_api -> bsp -> mcu_port -> board -> WCH standard peripheral library
```

Differences from the STM32 SDK:

- Uses WCH standard peripheral library, not STM32 HAL.
- Uses `components/mcu_port` instead of `components/hal_port`.
- Uses a RISC-V GCC toolchain, not `arm-none-eabi-gcc`.
- Uses CH32V20x startup/linker/system files from the WCH EVT SDK.
- The default board is `ch32v203g6u6`.

Before building:

1. Install a RISC-V bare-metal GCC toolchain.
2. Ensure one of these command prefixes exists in PATH:
   - `riscv-none-elf-gcc`
   - or set `RISCV_TOOLCHAIN_PREFIX`, for example `riscv-none-embed`.
3. Place the WCH CH32V20x EVT SDK under `third_party/ch32v20x`.
4. Update `boards/ch32v203g6u6/board_pins.h` from your board schematic.
5. Check `linker/CH32V203G6U6_FLASH.ld` against your exact chip memory size.

Build example:

```bash
python scripts/build.py --board ch32v203g6u6 --app led_blink
```

Flash example:

```bash
python scripts/flash.py --board ch32v203g6u6 --app led_blink --tool minichlink
```
