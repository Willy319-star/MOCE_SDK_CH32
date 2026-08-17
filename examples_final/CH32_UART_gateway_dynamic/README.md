# CH32 dynamic CAN-to-UART gateway

ESP32 通过 CAN ID `0x000` 上的 F0/F1/F2 协议为 UART 网关动态分配
`node_id`。设备类型为 `0x04`，运行 ID 使用 `0x31..0x40`。CH32 以工厂
UID 派生的 token 作为稳定身份，节点 ID 只保存在 RAM 中。

多个 CH32 同时收到库存查询时，会根据 token 与查询序号进入不同的 2 ms
响应槽，降低 CAN ID `0x000` 回包碰撞概率。已分配节点回 F2，未分配节点回
F0；ESP32 按 token 分配并通过 F1 下发运行 ID。

UART 固定为 USART1 PA9/PA10、9600 8N1。透明传输协议：

- START: `0x200 + node_id`
- DATA: `0x300 + node_id`，每帧 1–5 字节
- UART RX: `0x400 + node_id`
- ACK: `0x500 + node_id`
- HELLO: `0x700 + node_id`

CH32 仅在 START ACK 成功后接收 DATA，完成后校验 CRC16/CCITT-FALSE，
再把完整字节流写到 UART。完成 ACK 中的 processed length 是实际写出的长度。
ESP32 在 START ACK 后连续发送 CAN DATA，不在每个分片间加入固定延时；总
超时由 ACK 预算与按波特率计算的物理串口发送时间共同决定。这样正常路径不
被人为节流，串口自身的实际发送时间仍保留在稳定性预算内。

```powershell
python scripts\build.py --board ch32v203g6u6 --app CH32_UART_gateway_dynamic
```
