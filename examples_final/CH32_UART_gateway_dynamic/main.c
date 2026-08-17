#include "common_can_node.h"
#include "board.h"
#include "mcu_port_time.h"
#include "vibe_runtime.h"

#include <stdint.h>
#include <string.h>

#define DEVICE_TYPE             DEVICE_TYPE_UART
#define FW_VERSION              3U
#define PROTOCOL_VERSION        1U

#define CAP_TX_FRAGMENT         0x02U
#define CAP_RX_FORWARD          0x04U
#define CAP_DYNAMIC_NODE        0x40U
#define CAPABILITY_FLAGS        (CAP_TX_FRAGMENT | CAP_RX_FORWARD | CAP_DYNAMIC_NODE)

#define HEARTBEAT_MS            1000U
#define CAN_POLL_MS             1U
#define DISCOVERY_SERVICE_MS    1U
#define DISCOVERY_SLOT_MS       2U
#define DISCOVERY_SLOT_COUNT    32U
#define ID_ACK_REPEAT_GAP_MS    5U

#define UART_BAUD_RATE          9600U
#define UART_RX_POLL_MS         1U
#define UART_RX_IDLE_MS         4U
#define UART_RX_PAYLOAD_SIZE    5U
#define SYN_FRAME_MAX           4096U
#define TRANSFER_TIMEOUT_MS     500U

#define ACK_START               0x30U
#define ACK_COMPLETE            0x31U
#define CAN_ID_START_BASE       0x200U
#define CAN_ID_DATA_BASE        0x300U
#define CAN_ID_UART_RX_BASE     0x400U

static uint8_t can_ready;
static uint8_t my_node_id = NODE_ID_UNASSIGNED;
static uint8_t id_assigned;
static uint16_t node_token;
static uint8_t request_seq;

typedef struct {
    uint8_t active;
    uint8_t tid;
    uint16_t total_len;
    uint16_t expected_crc;
    uint16_t received_len;
    uint16_t expected_seq;
    uint32_t last_ms;
    uint8_t buf[SYN_FRAME_MAX];
} transfer_t;

typedef enum {
    DISCOVERY_RESPONSE_NONE = 0,
    DISCOVERY_RESPONSE_F0,
    DISCOVERY_RESPONSE_F2,
} discovery_response_t;

static transfer_t transfer;
static discovery_response_t pending_discovery_response;
static uint8_t pending_discovery_repeats;
static uint32_t pending_discovery_due_ms;
static uint8_t uart_rx_buffer[UART_RX_PAYLOAD_SIZE];
static uint8_t uart_rx_length;
static uint8_t uart_rx_burst_id;
static uint8_t uart_rx_sequence;
static uint32_t uart_rx_last_ms;

static void reset_transfer(void)
{
    memset(&transfer, 0, sizeof(transfer));
}

static void send_transfer_ack(uint8_t phase, uint16_t source_id,
                              uint8_t result, uint8_t tid,
                              uint16_t processed_len)
{
    uint8_t data[8] = {0};
    data[0] = phase;
    node_put_u16_le(data, 1U, source_id);
    data[3] = result;
    data[4] = tid;
    data[5] = 0U;
    node_put_u16_le(data, 6U, processed_len);
    (void)bsp_can_send_std(CAN_ID_ACK(my_node_id), data, sizeof(data));
}

static uint32_t discovery_slot_delay_ms(uint8_t sequence)
{
    uint16_t mixed = (uint16_t)(node_token ^ ((uint16_t)sequence * 0x45D9U));
    mixed ^= (uint16_t)(mixed >> 7U);
    return (uint32_t)(1U +
        ((uint32_t)(mixed % DISCOVERY_SLOT_COUNT) * DISCOVERY_SLOT_MS));
}

static void send_id_ack_once(void)
{
    uint8_t data[8] = {0};
    data[0] = DYN_CMD_ID_ACK;
    data[1] = my_node_id;
    data[2] = DYN_MAGIC0;
    data[3] = DYN_MAGIC1;
    node_put_u16_le(data, 4U, node_token);
    data[6] = DEVICE_TYPE;
    data[7] = PROTOCOL_VERSION;
    (void)bsp_can_send_std(CAN_ID_DISCOVERY, data, sizeof(data));
}

static void schedule_discovery_response(discovery_response_t response,
                                        uint8_t sequence,
                                        uint8_t repeat_count,
                                        uint8_t use_slot_delay)
{
    if (response == DISCOVERY_RESPONSE_F0 &&
        pending_discovery_response == DISCOVERY_RESPONSE_F2) {
        return;
    }
    pending_discovery_response = response;
    pending_discovery_repeats = repeat_count;
    pending_discovery_due_ms = mcu_port_millis() +
        (use_slot_delay ? discovery_slot_delay_ms(sequence)
                        : ID_ACK_REPEAT_GAP_MS);
}

static void discovery_response_task(void)
{
    uint32_t now = mcu_port_millis();

    if (!can_ready || pending_discovery_response == DISCOVERY_RESPONSE_NONE ||
        (int32_t)(now - pending_discovery_due_ms) < 0) {
        return;
    }
    if (pending_discovery_response == DISCOVERY_RESPONSE_F0) {
        node_send_id_request(DEVICE_TYPE, PROTOCOL_VERSION, CAPABILITY_FLAGS,
                             &request_seq, node_token);
        pending_discovery_response = DISCOVERY_RESPONSE_NONE;
        pending_discovery_repeats = 0U;
        return;
    }

    send_id_ack_once();
    if (pending_discovery_repeats > 1U) {
        pending_discovery_repeats--;
        pending_discovery_due_ms = now + ID_ACK_REPEAT_GAP_MS;
    } else {
        pending_discovery_response = DISCOVERY_RESPONSE_NONE;
        pending_discovery_repeats = 0U;
    }
}

static void handle_discovery(const bsp_can_frame_t *frame)
{
    if (frame == 0 || frame->dlc < 1U) {
        return;
    }
    if (frame->data[0] == DYN_CMD_ASSIGN_ID && frame->dlc >= 8U &&
        frame->data[2] == DYN_MAGIC0 && frame->data[3] == DYN_MAGIC1 &&
        frame->data[6] == DEVICE_TYPE &&
        frame->data[7] == PROTOCOL_VERSION &&
        node_get_u16_le(frame->data, 4U) == node_token &&
        node_is_valid_id(frame->data[1])) {
        my_node_id = frame->data[1];
        id_assigned = 1U;
        reset_transfer();
        send_id_ack_once();
        schedule_discovery_response(DISCOVERY_RESPONSE_F2, request_seq,
                                    (uint8_t)(ID_ACK_REPEAT_COUNT - 1U), 0U);
        (void)node_send_hello(my_node_id, DEVICE_TYPE, FW_VERSION,
                              CAPABILITY_FLAGS);
        return;
    }
    if (frame->data[0] == DYN_CMD_RELEASE_ID && frame->dlc >= 3U &&
        frame->data[1] == DYN_MAGIC0 && frame->data[2] == DYN_MAGIC1) {
        my_node_id = NODE_ID_UNASSIGNED;
        id_assigned = 0U;
        reset_transfer();
        schedule_discovery_response(DISCOVERY_RESPONSE_F0,
                                    frame->dlc > 4U ? frame->data[4]
                                                    : request_seq,
                                    1U, 1U);
        return;
    }
    if (frame->data[0] == DYN_CMD_REQUEST_ID && frame->dlc >= 8U &&
        frame->data[1] == DEVICE_TYPE &&
        node_get_u16_le(frame->data, 5U) == 0U) {
        schedule_discovery_response(id_assigned ? DISCOVERY_RESPONSE_F2
                                                : DISCOVERY_RESPONSE_F0,
                                    frame->data[4],
                                    id_assigned ? ID_ACK_REPEAT_COUNT : 1U,
                                    1U);
    }
}

static void heartbeat(void)
{
    if (!can_ready) {
        return;
    }
    if (id_assigned) {
        (void)node_send_hello(my_node_id, DEVICE_TYPE, FW_VERSION,
                              CAPABILITY_FLAGS);
    } else {
        schedule_discovery_response(DISCOVERY_RESPONSE_F0, request_seq,
                                    1U, 1U);
    }
}

static void handle_start(const bsp_can_frame_t *frame)
{
    uint8_t tid = frame->dlc > 0U ? frame->data[0] : 0U;
    uint16_t start_id = CAN_ID_START_BASE + (uint16_t)my_node_id;
    uint16_t length;

    if (transfer.active || frame->dlc < 7U) {
        send_transfer_ack(ACK_START, start_id, 0U, tid,
                          transfer.received_len);
        return;
    }
    length = node_get_u16_le(frame->data, 1U);
    if (length == 0U || length > SYN_FRAME_MAX ||
        frame->data[5] != PROTOCOL_VERSION) {
        send_transfer_ack(ACK_START, start_id, 0U, tid, 0U);
        return;
    }

    transfer.active = 1U;
    transfer.tid = tid;
    transfer.total_len = length;
    transfer.expected_crc = node_get_u16_le(frame->data, 3U);
    transfer.last_ms = mcu_port_millis();
    send_transfer_ack(ACK_START, start_id, 1U, tid, 0U);
}

static void handle_data(const bsp_can_frame_t *frame)
{
    uint16_t data_id = CAN_ID_DATA_BASE + (uint16_t)my_node_id;
    uint8_t tid = frame->dlc > 0U ? frame->data[0] : 0U;
    uint16_t sequence;
    uint8_t payload_len;
    uint16_t remaining;

    if (!transfer.active || frame->dlc < 4U || tid != transfer.tid) {
        reset_transfer();
        send_transfer_ack(ACK_COMPLETE, data_id, 0U, tid, 0U);
        return;
    }
    sequence = node_get_u16_le(frame->data, 1U);
    if (sequence != transfer.expected_seq) {
        uint16_t received_len = transfer.received_len;
        reset_transfer();
        send_transfer_ack(ACK_COMPLETE, data_id, 0U, tid, received_len);
        return;
    }

    payload_len = (uint8_t)(frame->dlc - 3U);
    remaining = (uint16_t)(transfer.total_len - transfer.received_len);
    if (payload_len > remaining) {
        payload_len = (uint8_t)remaining;
    }
    memcpy(&transfer.buf[transfer.received_len], &frame->data[3], payload_len);
    transfer.received_len = (uint16_t)(transfer.received_len + payload_len);
    transfer.expected_seq++;
    transfer.last_ms = mcu_port_millis();

    if (transfer.received_len == transfer.total_len) {
        uint16_t completed_len = transfer.received_len;
        uint8_t ok = node_crc16_ccitt(transfer.buf, completed_len) ==
                     transfer.expected_crc ? 1U : 0U;
        if (ok) {
            board_uart_write(transfer.buf, completed_len);
        }
        reset_transfer();
        send_transfer_ack(ACK_COMPLETE, data_id, ok, tid,
                          ok ? completed_len : 0U);
    }
}

static void check_transfer_timeout(void)
{
    if (transfer.active &&
        (uint32_t)(mcu_port_millis() - transfer.last_ms) >=
            TRANSFER_TIMEOUT_MS) {
        uint8_t tid = transfer.tid;
        uint16_t received_len = transfer.received_len;
        reset_transfer();
        send_transfer_ack(ACK_COMPLETE,
                          CAN_ID_DATA_BASE + (uint16_t)my_node_id,
                          0U, tid, received_len);
    }
}

static void uart_send_rx_fragment(uint8_t flags)
{
    uint8_t data[8] = {0};
    data[0] = uart_rx_burst_id;
    data[1] = uart_rx_sequence++;
    data[2] = flags;
    memcpy(&data[3], uart_rx_buffer, uart_rx_length);
    (void)bsp_can_send_std(CAN_ID_UART_RX_BASE + (uint16_t)my_node_id,
                           data, (uint8_t)(3U + uart_rx_length));
    uart_rx_length = 0U;
}

static void uart_rx_task(void)
{
    uint8_t byte;
    uint32_t now = mcu_port_millis();

    if (!id_assigned) {
        while (board_uart_read_byte(&byte)) {
        }
        uart_rx_length = 0U;
        uart_rx_sequence = 0U;
        return;
    }
    while (board_uart_read_byte(&byte)) {
        if (uart_rx_length == 0U && uart_rx_sequence == 0U) {
            uart_rx_burst_id++;
        }
        uart_rx_buffer[uart_rx_length++] = byte;
        uart_rx_last_ms = now;
        if (uart_rx_length == UART_RX_PAYLOAD_SIZE && id_assigned) {
            uart_send_rx_fragment(uart_rx_sequence == 0U ? 0x01U : 0U);
        }
    }
    if (uart_rx_sequence > 0U && uart_rx_length == 0U &&
        (uint32_t)(now - uart_rx_last_ms) >= UART_RX_IDLE_MS && id_assigned) {
        uart_send_rx_fragment(0x02U);
        uart_rx_sequence = 0U;
    } else if (uart_rx_length > 0U &&
               (uint32_t)(now - uart_rx_last_ms) >= UART_RX_IDLE_MS &&
               id_assigned) {
        uart_send_rx_fragment((uart_rx_sequence == 0U ? 0x01U : 0U) | 0x02U);
        uart_rx_sequence = 0U;
    }
}

static void can_poll(void)
{
    bsp_can_frame_t frame;

    if (!can_ready) {
        return;
    }
    check_transfer_timeout();
    while (bsp_can_receive(&frame)) {
        if (frame.id == CAN_ID_DISCOVERY) {
            handle_discovery(&frame);
        } else if (id_assigned && frame.id == CAN_ID_CMD(my_node_id)) {
            handle_start(&frame);
        } else if (id_assigned && frame.id == CAN_ID_DATA(my_node_id)) {
            handle_data(&frame);
        }
    }
}

void app_setup(void)
{
    node_token = node_read_token(DEVICE_TYPE);
    board_uart_init(UART_BAUD_RATE);
    can_ready = bsp_can_init(BSP_CAN_BITRATE_500K);
    reset_transfer();

    if (can_ready) {
        heartbeat();
        (void)vibe_task_every_ms(HEARTBEAT_MS, heartbeat);
        (void)vibe_task_every_ms(CAN_POLL_MS, can_poll);
        (void)vibe_task_every_ms(DISCOVERY_SERVICE_MS,
                                 discovery_response_task);
        (void)vibe_task_every_ms(UART_RX_POLL_MS, uart_rx_task);
    }
}
