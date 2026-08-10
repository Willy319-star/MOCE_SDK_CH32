#include "common_can_node.h"
#include "board.h"
#include "board_pins.h"
#include "ch32v20x_can.h"
#include "ch32v20x_misc.h"
#include "ch32v20x_rcc.h"
#include "ch32v20x_usart.h"
#include "mcu_port_time.h"
#include "vibe_runtime.h"

#include <stdint.h>
#include <string.h>

/* ── gateway identity ── */
#define DEVICE_TYPE             DEVICE_TYPE_UART
#define FW_VERSION              3U
#define PROTOCOL_VERSION        1U

#define CAP_TX_FRAGMENT         0x02U
#define CAP_RX_FORWARD          0x04U
#define CAP_DYNAMIC_NODE        0x40U
#define CAPABILITY_FLAGS        (CAP_TX_FRAGMENT | CAP_RX_FORWARD | CAP_DYNAMIC_NODE)

/* ── timing ── */
#define HEARTBEAT_MS            1000U
#define CAN_POLL_MS             1U

/* ── UART ── */
#define DATA_PAYLOAD_SIZE       5U
#define SYN_FRAME_MAX           4096U
#define TRANSFER_TIMEOUT_MS     500U

/* ── ACK phases ── */
#define ACK_START               0x30U
#define ACK_COMPLETE            0x31U

/* ── CAN ID bases (must match ESP32 ch32_uart_dynamic_gateway_final.c) ── */
#define CAN_ID_START_BASE       0x200U
#define CAN_ID_DATA_BASE        0x300U

/* ── shared state ── */
static uint8_t  can_ready;
static uint8_t  my_node_id = NODE_ID_UNASSIGNED;
static uint8_t  id_assigned;
static uint16_t node_token;
static uint8_t  request_seq;

/* ── transfer state ── */
typedef struct {
    uint8_t  active;
    uint8_t  tid;
    uint16_t total_len;
    uint16_t expected_crc;
    uint16_t received_len;
    uint16_t expected_seq;
    uint32_t last_ms;
    uint8_t  buf[SYN_FRAME_MAX];
} transfer_t;
static transfer_t tfer;

/* ── UART init ── */
static void uart_init(void)
{
    board_uart_init(9600U);
}

/* ── ACK: must match ESP32 parse_transfer_ack() format
   [0]=phase [1-2]=source_id(LE) [3]=result(1=OK) [4]=tid [5]=0 [6-7]=plen(LE) ── */
static void send_ack(uint8_t phase, uint16_t source_id, uint8_t result,
                     uint8_t tid, uint16_t plen)
{
    uint8_t d[8] = {0};
    d[0] = phase;
    node_put_u16_le(d, 1U, source_id);
    d[3] = result;
    d[4] = tid;
    d[5] = 0U;
    node_put_u16_le(d, 6U, plen);
    (void)bsp_can_send_std(CAN_ID_ACK(my_node_id), d, sizeof(d));
}

/* ── reset transfer ── */
static void reset_transfer(void) { memset(&tfer, 0, sizeof(tfer)); }

/* ── discovery ── */
static void handle_discovery(const bsp_can_frame_t *f)
{
    /* Pass PROTOCOL_VERSION as the version field – ESP32 checks this exact value. */
    (void)node_handle_discovery(f, DEVICE_TYPE, PROTOCOL_VERSION,
                                CAPABILITY_FLAGS, PROTOCOL_VERSION,
                                node_token, &my_node_id, &id_assigned,
                                &request_seq);
}

static void heartbeat(void)
{
    if (!can_ready) return;
    if (id_assigned) {
        (void)node_send_hello(my_node_id, DEVICE_TYPE, PROTOCOL_VERSION,
                              CAPABILITY_FLAGS);
    } else {
        node_send_id_request(DEVICE_TYPE, PROTOCOL_VERSION, CAPABILITY_FLAGS,
                             &request_seq, node_token);
    }
}

/* ── START frame handler ── */
static void handle_start(const bsp_can_frame_t *f)
{
    /* ESP32 format: [0]=tid [1-2]=len(LE) [3-4]=crc(LE) [5]=ver [6]=opts [7]=0 */
    uint8_t  tid = (f->dlc > 0U) ? f->data[0] : 0U;
    uint16_t start_id = CAN_ID_START_BASE + (uint16_t)my_node_id;

    if (tfer.active) {
        send_ack(ACK_START, start_id, 0U, tid, tfer.received_len);
        return;
    }
    if (f->dlc < 7U) {
        send_ack(ACK_START, start_id, 0U, tid, 0U);
        return;
    }

    uint16_t len = node_get_u16_le(f->data, 1U);
    if (len > SYN_FRAME_MAX) {
        send_ack(ACK_START, start_id, 0U, tid, 0U);
        return;
    }

    tfer.active       = 1U;
    tfer.tid          = tid;
    tfer.total_len    = len;
    tfer.expected_crc = node_get_u16_le(f->data, 3U);
    tfer.received_len = 0U;
    tfer.expected_seq = 0U;
    tfer.last_ms      = mcu_port_millis();

    send_ack(ACK_START, start_id, 1U, tid, 0U);
}

/* ── DATA frame handler ── */
static void handle_data(const bsp_can_frame_t *f)
{
    /* ESP32 format: [0]=tid [1-2]=seq(LE) [3..]=payload */
    uint16_t data_id = CAN_ID_DATA_BASE + (uint16_t)my_node_id;

    if (!tfer.active) {
        uint8_t tid = (f->dlc > 0U) ? f->data[0] : 0U;
        send_ack(ACK_COMPLETE, data_id, 0U, tid, 0U);
        return;
    }

    uint8_t tid = (f->dlc > 0U) ? f->data[0] : 0U;
    if (tid != tfer.tid || f->dlc < 3U) {
        reset_transfer();
        send_ack(ACK_COMPLETE, data_id, 0U, tid, 0U);
        return;
    }

    uint16_t seq = node_get_u16_le(f->data, 1U);
    if (seq != tfer.expected_seq) {
        reset_transfer();
        send_ack(ACK_COMPLETE, data_id, 0U, tid, tfer.received_len);
        return;
    }

    uint8_t  pl  = (uint8_t)(f->dlc - 3U);
    uint16_t rem = tfer.total_len - tfer.received_len;
    if (pl > rem) pl = (uint8_t)rem;

    memcpy(&tfer.buf[tfer.received_len], &f->data[3U], pl);
    tfer.received_len = (uint16_t)(tfer.received_len + pl);
    tfer.expected_seq++;
    tfer.last_ms = mcu_port_millis();

    if (tfer.received_len >= tfer.total_len) {
        uint8_t ok = 0U;
        uint16_t cc = node_crc16_ccitt(tfer.buf, tfer.received_len);
        if (cc == tfer.expected_crc) {
            board_uart_write(tfer.buf, tfer.received_len);
            ok = 1U;
        }
        reset_transfer();
        send_ack(ACK_COMPLETE, data_id, ok, tid,
                 ok ? tfer.received_len : 0U);
    }
}

/* ── transfer timeout ── */
static void check_transfer_timeout(void)
{
    if (!tfer.active) return;
    if ((uint32_t)(mcu_port_millis() - tfer.last_ms) < TRANSFER_TIMEOUT_MS) return;
    uint16_t data_id = CAN_ID_DATA_BASE + (uint16_t)my_node_id;
    send_ack(ACK_COMPLETE, data_id, 0U, tfer.tid, tfer.received_len);
    reset_transfer();
}

/* ── main poll ── */
static void can_poll(void)
{
    bsp_can_frame_t f;
    if (!can_ready) return;
    check_transfer_timeout();
    while (bsp_can_receive(&f)) {
        if (f.id == CAN_ID_DISCOVERY) {
            handle_discovery(&f);
        } else if (id_assigned) {
            if (f.id == CAN_ID_CMD(my_node_id))       handle_start(&f);
            else if (f.id == CAN_ID_DATA(my_node_id)) handle_data(&f);
        }
    }
}

/* ── entry ── */
void app_setup(void)
{
    node_token = node_read_token(DEVICE_TYPE);
    uart_init();
    can_ready = bsp_can_init(BSP_CAN_BITRATE_500K);
    reset_transfer();

    if (can_ready) {
        heartbeat();
        (void)vibe_task_every_ms(HEARTBEAT_MS, heartbeat);
        (void)vibe_task_every_ms(CAN_POLL_MS, can_poll);
    }
}
