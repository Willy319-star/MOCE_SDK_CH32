# CH32_I2C_gateway_dynamic

This CH32 app keeps CH32 as a generic CAN <-> I2C bridge and adds dynamic node ID assignment.
It does not contain device-specific OLED, MPU6050, VL53L0X, or VC02 logic.

The intended architecture is:

`ESP32-WROOM application logic -> CAN -> CH32 dynamic I2C bridge -> I2C downstream device`

The ESP32 decides which downstream module logic to run. CH32 only converts CAN bridge commands into I2C transactions and returns status/data frames.

## Discovery and Assignment Protocol

Before assignment, the CH32 node uses node id 0 and only accepts discovery and assignment frames.

CH32 periodically sends request frames:

```text
CAN id: 0x000
byte0: 0xF0
byte1: DEVICE_TYPE_I2C
byte2: FW_VERSION
byte3: capability flags
byte4: request sequence
byte5: token low
byte6: token high
byte7: current node id, 0 before assignment
```

ESP32 assigns an id by echoing the token:

```text
CAN id: 0x000
byte0: 0xF1
byte1: new node id (ESP32 I2C allocation policy: 1..0x20)
byte2: 0xAA
byte3: 0x55
byte4: token low
byte5: token high
byte6: DEVICE_TYPE_I2C
byte7: selected protocol version, 1 or 2
```

CH32 accepts the assignment only when the token matches. This prevents several unassigned CH32 boards from accepting the same node id.

CH32 returns assignment ACK:

```text
CAN id: 0x000
byte0: 0xF2
byte1: assigned node id
byte2: 0xAA
byte3: 0x55
byte4: token low
byte5: token high
byte6: DEVICE_TYPE_I2C
byte7: FW_VERSION
```

Release is intentionally explicit so routine rediscovery cannot reset every
gateway by accident. Both forms use CAN id `0x000`, DLC 8 and protocol version
the negotiated protocol version in byte 7:

- Targeted: `[F3, AA, 55, 00, token_lo, token_hi, DEVICE_TYPE_I2C, 01]`
- Broadcast: `[F3, AA, 55, A5, 00, 00, 00, 01]`

Routine rediscovery should use F0 and stable-token merging, not broadcast F3.

## Normal CAN IDs After Assignment

After assignment, normal traffic uses MOCE gateway CAN ids:

- I2C command: `CAN_ID_I2C_CMD(node) = 0x200 + node`
- I2C status/read data: `CAN_ID_STATUS(node) = 0x100 + node`
- ACK: `CAN_ID_ACK(node) = 0x500 + node`
- HELLO: `CAN_ID_HELLO(node) = 0x700 + node`

## Supported Bridge Commands After Assignment

- `0x01` scan I2C bus.
- `0x02` probe one 7-bit I2C address.
- `0x03` write register with payload bytes.
- `0x04` read registers, chunked when length is greater than one CAN frame can carry.
- `0x05` raw I2C write.
- `0x06` write one register address then read bytes.
- `0x07` set I2C speed, 0 for 100 kHz and 1 for 400 kHz.
- `0x08` buffered multi-byte raw write. ESP32 sends chunks with START/END flags; CH32 buffers the bytes and performs one I2C write when END arrives. This is intended for OLED-style command/data streams without putting OLED fonts or display logic into CH32.

Protocol version 1 returns status and ACK for every `0x08` fragment for
compatibility with older ESP32 code. Protocol version 2 accepts the fragments
as one bounded CAN stream and returns status plus ACK only for the END fragment.
Both versions perform exactly one downstream I2C write at END. ESP32 selects
version 2 only after seeing firmware version 4 or newer in discovery, so old
gateways retain the conservative behavior without slowing new gateways.

## Filtering and Retry Behavior

This app uses 500 kbit/s CAN through `bsp_can_init(BSP_CAN_BITRATE_500K)`. The CH32V20x hardware filter is kept open because the previous 16-bit list configuration intermittently dropped valid dynamic command IDs. The application dispatch layer still processes only discovery/control ID `0x000` and, after assignment, this node's command ID `0x200 + node`.

Retry behavior is bounded at the operation layer:

- Unassigned CH32 sends discovery request `0xF0` every heartbeat period.
- ESP32-side dynamic assignment may retry assignment if ACK is not received.

This app adds conservative multi-node collision handling:

- Unassigned F0 replies use a token-derived response slot. ESP32 inventory
  queries change sequence on retry, so gateways that collide in one slot can
  move to different slots on a later query.
- F2 keeps the same shared discovery ID `0x000` and byte layout used by the
  immutable UART dynamic-gateway reference. I2C adds token-derived scheduling
  for inventory replies, while ESP32 changes the query sequence on retry so a
  same-slot collision does not permanently hide every node.
- CH32 performs one physical I2C transaction per CAN command. It does not hide
  extra downstream retries inside the gateway; ESP32/module policy may retry a
  complete read operation when that is explicitly safe. Write commands are not
  blindly retried because duplicate writes can be unsafe.

Legacy single-frame reads carry at most three data bytes. Reads of four or more
bytes always use `STATUS_READ_CHUNK` followed by `STATUS_READ_DONE`, because a
status byte, four metadata bytes and four data bytes cannot fit in classic
CAN's eight-byte data field.

An ESP32 inventory query is an F0 frame for the I2C gateway type with a zero
token. CH32 gateways ignore nonzero-token F0 frames sent by peer gateways;
otherwise several unassigned gateways would trigger one another indefinitely.

`SET_SPEED` status reports `speed_khz` as little-endian bytes instead of a
single byte, so 400 kHz is represented correctly.

## Verified With VL53L0X

This firmware has been verified with the ESP32-WROOM example `example_final/tof_test0_final` and VL53L0X address `0x29`.

Known-good ESP32-side serial milestones:

```text
dynamic discovery accepted token=<token> seq=<seq> fw=4 caps=0xFF
assignment accepted node=1 token=<token>
gateway hello wait result=OK
tof init result=OK
tof distance=<number> mm raw=<number> result=OK
```

## Build

Use app name `CH32_I2C_gateway_dynamic` with board `ch32v203g6u6`.

The output ELF path is resolved by the repository build/flash scripts. If the launcher asks for an app name, enter `CH32_I2C_gateway_dynamic`, not its numeric list index.
