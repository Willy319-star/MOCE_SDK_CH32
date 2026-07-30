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
byte1: new node id, 1..127
byte2: 0xAA
byte3: 0x55
byte4: token low
byte5: token high
byte6: device type or reserved by ESP32-side protocol
byte7: firmware version or reserved by ESP32-side protocol
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

ESP32 can send release request `0xF3` on `0x000` before discovery so a previously assigned board can re-enter assignment mode.

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

## Filtering and Retry Behavior

The BSP CAN driver is not modified. This app reconfigures CAN acceptance filtering after `bsp_can_init_50k()`:

- Before assignment: accept only discovery/control frame `0x000`.
- After assignment: accept discovery/control frame `0x000` and this node's command frame `0x200 + node`.

Existing retry behavior remains:

- Unassigned CH32 sends discovery request `0xF0` every heartbeat period.
- ESP32-side dynamic assignment may retry assignment if ACK is not received.

This app adds two conservative retries:

- Assignment ACK `0xF2` is sent three times to reduce loss during bus startup.
- I2C read-style bridge commands retry the physical I2C read twice. Write commands are not blindly retried, because duplicate writes can be unsafe for some devices.

## Verified With VL53L0X

This firmware has been verified with the ESP32-WROOM example `example_final/tof_test0_final` and VL53L0X address `0x29`.

Known-good ESP32-side serial milestones:

```text
dynamic discovery accepted token=<token> seq=<seq> fw=3 caps=0xFF
assignment accepted node=1 token=<token>
gateway hello wait result=OK
tof init result=OK
tof distance=<number> mm raw=<number> result=OK
```

## Build

Use app name `CH32_I2C_gateway_dynamic` with board `ch32v203g6u6`.

The output ELF path is resolved by the repository build/flash scripts. If the launcher asks for an app name, enter `CH32_I2C_gateway_dynamic`, not its numeric list index.
