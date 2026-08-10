# CH32 dynamic-node CAN-to-UART gateway

This application lets an ESP32 dynamically assign this CH32 a CAN `node_id`,
then receives an opaque UART frame over 500 kbit/s CAN, validates and
reassembles it, and emits the original bytes from USART1 PA9 at 9600 8N1 in
TX/RX mode. Opaque response bytes received on USART1 PA10 are grouped by the
USART IDLE condition and forwarded to ESP32 over CAN.

The CH32 reads its six-byte factory UID at startup and uses its CRC-16 as a
stable 16-bit node token. Dynamic commissioning uses the same CH32-initiated
management protocol as the I2C gateway. All management frames use CAN ID
`0x000`, DLC 8:

- `F0 04 01 8F seq token_le16 00`: CH32 REQUEST_ID
- `F1 node AA 55 token_le16 04 01`: host ASSIGN_ID
- `F2 node AA 55 token_le16 04 01`: CH32 ID_ACK
- `F3 AA 55 00 00 00 00 00`: host RELEASE_ID

Device type `0x04` identifies a CAN-to-UART gateway. Capabilities `0x8F`
identify opaque UART forwarding, buffered CRC reassembly, synchronized
PREPARE/TRIGGER, UART RX-to-CAN forwarding, and dynamic-node support.

Node ID `0x00` means unassigned and IDs `0x01..0x7F` are valid. The assignment
is stored only in RAM, so it must be performed again after every CH32 power
cycle. While unassigned, CH32 sends REQUEST_ID immediately and every 1000 ms.
A valid RELEASE_ID clears the assignment and any incomplete or prepared UART
transfer, then immediately sends another REQUEST_ID.

After accepting ASSIGN_ID, CH32 sends ID_ACK three times on CAN ID `0x000`
and three times on `0x100 + node_id`, then sends HELLO on
`0x700 + node_id`. HELLO payload is
`04 node 01 8F 00 token_lo token_hi 00` and is repeated every 1000 ms while
assigned. This fixed-function version always uses UART `9600, 8N1`; it does
not implement runtime baud switching or per-transfer TX baud overrides.

After assigning node `N`, the UART transfer uses:

- `0x200 + N`: transfer START
- `0x300 + N`: one-to-five-byte DATA fragments
- `0x500 + N`: START and completion ACK
- maximum frame size: 4096 bytes
- inter-fragment timeout: 500 ms

UART receive traffic uses `0x400 + N`, DLC 3 through 8:

- byte 0: burst ID
- byte 1: sequence starting at zero
- byte 2: flags; START `0x01`, END `0x02`, OVERFLOW `0x04`
- bytes 3 through 7: zero to five opaque UART bytes

PA10 RXNE and IDLE interrupts feed a 128-entry ring. The 1 ms application task
emits five-byte chunks; IDLE closes a burst. An exact-five-byte final chunk is
followed by a DLC-3 END marker. A failed CAN frame is retried on up to three
later task invocations. Ring overflow or exhausted CAN retries discard the
partial burst and eventually emit a marker-only START|END|OVERFLOW report.
No RX frames are emitted before a node ID has been assigned.

START byte 6 bit 0 selects PREPARE_ONLY. In this mode the CH32 validates and
keeps the complete frame without writing PA9, then returns phase `0x32`
PREPARED. A matching transfer `0x7F2` TRIGGER with session byte zero starts
PA9 and produces
phase `0x33` TRIGGER_COMPLETE after USART TC. `0x7F2` CANCEL clears the
prepared frame without output. A prepared frame does not expire automatically;
it is cleared only by matching TRIGGER/CANCEL or a valid RELEASE_ID. This
guarantees that an early node remains ready while later nodes are prepared.

Because every CH32 receives the same trigger broadcast, multiple PA9 outputs
start within the 1 ms CAN polling window. Independent SYN6288E modules can
still have different internal synthesis or speaker latency.

Until assignment succeeds, operational frames are ignored and PA9 emits no
payload bytes. The legacy fixed IDs `0x430/0x431` are not accepted.

Build from the workspace root:

```powershell
python scripts\build.py --board ch32v203g6u6 --app CH32_UART_gateway
```

Connect the downstream module TXD to PA10, its RXD to PA9, and share ground.
The application treats PA10 bytes as opaque data; it does not interpret
Ready#/Busy or downstream response semantics. It prints no startup text.
Flashing is always a separate explicit operation.

> Compatibility warning: the current ESP32 UART gateway allocator still uses
> the retired `0x7F0/0x7F1` UID discovery protocol and cannot commission this
> firmware. Update ESP32 to the `0x000 / F0..F3` protocol and use synchronized
> session zero before integration testing. ESP32 must also implement the new
> `0x400 + node_id` receiver before it can consume PA10 responses. Fixed-ID
> `0x430/0x431` firmware is also incompatible.
