# CH32 dynamic CAN-to-SPI gateway

This firmware keeps CH32V203 as a generic transport bridge:

`ESP32 application -> CAN -> CH32 SPI gateway -> downstream SPI peripheral`

It contains no peripheral-specific register, command, WHO_AM_I or business
logic. ESP32 owns device identification and builds the raw SPI transaction.

## Hardware

- SPI1 CS: PA4
- SPI1 SCK: PA5
- SPI1 MISO: PA6
- SPI1 MOSI: PA7
- CAN RX: PA11
- CAN TX: PA12
- CAN bitrate: 500 kbit/s

Version 1 supports one software-controlled CS, 8-bit master full-duplex SPI,
Mode 0..3, MSB/LSB first and clock divisors 2..256. The default is Mode 0,
MSB first, active-low CS, divider 64 and dummy byte `0xFF`.

## Dynamic discovery

The gateway uses `device_type=0x03` and the shared F0/F1/F2 layout on CAN ID
`0x000`. ESP32 assigns SPI nodes only in `0x21..0x30`. Identity is
`device_type + UID-derived token`; the runtime node ID is not persistent.

Unassigned inventory responses use token/sequence-derived time slots. F2 is
required before the node is ready. Routine rediscovery must use zero-token F0,
not broadcast F3. Targeted release is:

`[F3, AA, 55, 00, token_lo, token_hi, 03, 01]`

## CAN IDs after assignment

- Status/RX result: `0x100 + node`
- Command/START/EXECUTE: `0x200 + node`
- TX DATA fragments: `0x300 + node`
- ACK: `0x500 + node`
- HELLO: `0x700 + node`

All 16-bit fields use little-endian byte order.

## Configuration command

Send on `0x200 + node`, DLC 8:

| Byte | Field |
|---|---|
| 0 | `0x01` CONFIG |
| 1 | SPI mode, 0..3 |
| 2 | divider code: 0=/2, 1=/4, ... 7=/256 |
| 3 | bit order: 0=MSB, 1=LSB |
| 4 | CS active low: 0=no, 1=yes |
| 5 | dummy byte used after supplied TX bytes |
| 6 | reserved, send 0 |
| 7 | protocol version, currently 1 |

CH32 returns CONFIG status on `0x100 + node` and ACK phase `0x40` on
`0x500 + node`. CONFIG status is:

`[0x01, result, mode, divider_code, bit_order, cs_active_low, dummy, error]`

## Raw SPI transaction

SPI is full duplex. The protocol describes total clock bytes rather than a
misleading separate write/read operation. ESP32 also selects which leading RX
bytes to discard.

### 1. START

Send on `0x200 + node`, DLC 8:

| Byte | Field |
|---|---|
| 0 | `0x02` START |
| 1 | transfer ID |
| 2..3 | supplied TX length |
| 4..5 | total SPI clock length, 1..512 |
| 6..7 | RX skip length, 0..total length |

CH32 returns ACK phase `0x41`. START never asserts CS.

### 2. DATA

Send exactly `tx_length` bytes on `0x300 + node`:

| Byte | Field |
|---|---|
| 0 | transfer ID |
| 1..2 | fragment sequence, starting at 0 |
| 3..7 | 1..5 payload bytes |

After the final TX fragment CH32 returns ACK phase `0x42`. If `tx_length=0`,
this phase is skipped. Bytes from `tx_length` to `total_length` are generated
locally with the configured dummy byte.

### 3. EXECUTE

Send on `0x200 + node`, DLC 8:

| Byte | Field |
|---|---|
| 0 | `0x03` EXECUTE |
| 1 | transfer ID |
| 2..3 | CRC16/CCITT-FALSE of supplied TX bytes |
| 4..6 | reserved, send 0 |
| 7 | protocol version, currently 1 |

Only after length and CRC validation does CH32 assert CS and execute the whole
SPI operation locally. It never holds CS low while waiting for CAN fragments.

### 4. Result

RX chunks use `0x100 + node`:

`[0x02, transfer_id, sequence_lo, sequence_hi, data0..data3]`

Only bytes in `[rx_skip, total_length)` are returned. DONE is:

`[0x03, transfer_id, result, rx_len_lo, rx_len_hi, crc_lo, crc_hi, error]`

The final ACK phase is `0x43`. Every ACK uses:

`[phase, result, node, 0x03, transfer_id, processed_lo, processed_hi, error]`

## Error values

- `0`: none
- `1`: invalid command or field
- `2`: another transfer is active
- `3`: fragment sequence mismatch
- `4`: invalid or incomplete length
- `5`: TX CRC mismatch
- `6`: CAN assembly timeout
- `7`: SPI hardware timeout/failure
- `8`: completed transfer ID was reused for a new START
- `9`: SPI transport is not initialized or no matching session exists

## Retry and ambiguity rules

- Repeating the same active START descriptor only re-sends its ACK.
- A duplicate EXECUTE for the most recently completed transfer replays cached
  RX/DONE/ACK and does not perform SPI again.
- Do not restart a completed transfer with the same transfer ID. Use the next
  ID for new work.
- A transfer timeout resets only the CAN assembly session. CS has not yet been
  asserted at that point.
- If a hardware SPI timeout occurs after execution began, the result is cached
  as failed and a duplicate EXECUTE does not replay the physical operation.

These rules prevent a lost CAN response from blindly repeating an SPI write or
other potentially non-idempotent peripheral action.

## Important SPI limitations

- SPI has no generic address ACK and cannot be safely scanned like I2C.
- A floating MISO value such as `0xFF` is not proof that a device exists.
- ESP32 must identify a peripheral by an explicit module-specific transaction
  and expected response from its module card or datasheet.
- Version 1 supports one CS and at most 512 clock bytes per transaction.
- Large display/storage/ADC traffic is limited primarily by classic CAN, not
  by the SPI clock.

## Build

```powershell
python scripts\build.py --board ch32v203g6u6 --app CH32_SPI_gateway_dynamic
```
