#include "bsp_can.h"
#include "board_pins.h"
#include "ch32v20x_gpio.h"
#include "ch32v20x_misc.h"
#include "ch32v20x_rcc.h"
#include "ch32v20x_usart.h"
#include "vibe_runtime.h"
#include "vibe_api.h"

#include <string.h>

/* ── fixed CAN IDs ── */
#define CAN_ID_START     0x200U
#define CAN_ID_DATA      0x201U
#define CAN_ID_ACK       0x500U

/* ── UART ── */
#define SYN_BAUD          9600U
#define UART_SPIN_LIMIT   2000000UL

/* ── protocol ── */
#define FRAME_BUF_SIZE    256U
#define PAYLOAD_PER_FRAME 5U
#define FRAG_TIMEOUT_MS   500U

/* ── ACK phase codes ── */
#define PHASE_START    0x30U
#define PHASE_COMPLETE 0x31U

/* ── detail codes ── */
#define DETAIL_OK               0U
#define DETAIL_DLC_ERROR        1U
#define DETAIL_LENGTH_ERROR     2U
#define DETAIL_BUSY             3U
#define DETAIL_CRC_ERROR        8U
#define DETAIL_UART_TIMEOUT     9U

static uint8_t  frame_buf[FRAME_BUF_SIZE];
static uint8_t  active;       /* 1 during transfer */
static uint8_t  tid;
static uint16_t total_len;
static uint16_t exp_crc;
static uint16_t rcvd;
static uint16_t exp_seq;
static uint32_t last_ms;

/* ── helpers ── */
static uint16_t get16(const uint8_t *d, uint8_t o) {
    return (uint16_t)d[o] | ((uint16_t)d[o+1] << 8);
}
static void put16(uint8_t *d, uint8_t o, uint16_t v) {
    d[o] = (uint8_t)(v & 0xFF); d[o+1] = (uint8_t)(v >> 8);
}
static uint16_t crc16(const uint8_t *d, uint16_t n) {
    uint16_t c = 0xFFFF; uint16_t i; uint8_t b;
    for (i = 0; i < n; i++) {
        c ^= (uint16_t)d[i] << 8;
        for (b = 0; b < 8; b++)
            c = (c & 0x8000) ? (uint16_t)((c << 1) ^ 0x1021) : (uint16_t)(c << 1);
    }
    return c;
}

/* ── UART (TX-only, PA2=USART2_TX) ── */
static void uart_init(void) {
    GPIO_InitTypeDef g = {0};
    USART_InitTypeDef u = {0};
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_USART2, ENABLE);
    g.GPIO_Pin   = GPIO_Pin_2;
    g.GPIO_Speed = GPIO_Speed_50MHz;
    g.GPIO_Mode  = GPIO_Mode_AF_PP;
    GPIO_Init(GPIOA, &g);
    USART_StructInit(&u);
    u.USART_BaudRate            = SYN_BAUD;
    u.USART_WordLength          = USART_WordLength_8b;
    u.USART_StopBits            = USART_StopBits_1;
    u.USART_Parity              = USART_Parity_No;
    u.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    u.USART_Mode                = USART_Mode_Tx;
    USART_Init(USART2, &u);
    USART_Cmd(USART2, ENABLE);
}

static uint8_t uart_write(const uint8_t *d, uint16_t n) {
    uint16_t i; uint32_t s;
    for (i = 0; i < n; i++) {
        s = 0;
        while (USART_GetFlagStatus(USART2, USART_FLAG_TXE) == RESET)
            if (++s >= UART_SPIN_LIMIT) return 0;
        USART_SendData(USART2, d[i]);
    }
    s = 0;
    while (USART_GetFlagStatus(USART2, USART_FLAG_TC) == RESET)
        if (++s >= UART_SPIN_LIMIT) return 0;
    return 1;
}

/* ── ACK ── */
static void ack(uint8_t phase, uint8_t result, uint8_t detail, uint16_t plen) {
    uint8_t d[8] = {0};
    d[0] = phase; d[1] = result; d[2] = detail;
    put16(d, 3, plen);
    bsp_can_send_std(CAN_ID_ACK, d, 5);
}

static void fail(uint8_t phase, uint8_t detail) {
    uint16_t saved = rcvd;
    active = 0;
    ack(phase, 0, detail, saved);
}

/* ── CAN frame handlers ── */
static void handle_start(const bsp_can_frame_t *f) {
    if (f->dlc < 5)  { ack(PHASE_START, 0, DETAIL_DLC_ERROR, 0); return; }
    if (active)      { ack(PHASE_START, 0, DETAIL_BUSY, rcvd); return; }

    uint8_t  t = f->data[0];
    uint16_t l = get16(f->data, 1);
    uint16_t c = get16(f->data, 3);

    if (l > FRAME_BUF_SIZE) { ack(PHASE_START, 0, DETAIL_LENGTH_ERROR, 0); return; }

    active = 1; tid = t; total_len = l; exp_crc = c;
    rcvd = 0; exp_seq = 0; last_ms = vibe_millis();
    ack(PHASE_START, 1, DETAIL_OK, 0);
}

static void handle_data(const bsp_can_frame_t *f) {
    if (!active || f->dlc < 3) return;

    uint8_t  t = f->data[0];
    uint16_t s = get16(f->data, 1);
    uint8_t  plen = (uint8_t)(f->dlc - 3U);

    if (t != tid || s != exp_seq) { fail(PHASE_COMPLETE, DETAIL_LENGTH_ERROR); return; }

    uint16_t rem = total_len - rcvd;
    uint16_t copy = (plen < rem) ? plen : rem;
    memcpy(&frame_buf[rcvd], &f->data[3], copy);
    rcvd += copy; exp_seq++; last_ms = vibe_millis();

    if (rcvd < total_len) return;

    /* all data received — verify CRC, write UART, ack */
    if (crc16(frame_buf, rcvd) != exp_crc) { fail(PHASE_COMPLETE, DETAIL_CRC_ERROR); return; }

    uint8_t ok = uart_write(frame_buf, rcvd);
    active = 0;
    ack(PHASE_COMPLETE, ok ? 1 : 0, ok ? DETAIL_OK : DETAIL_UART_TIMEOUT, rcvd);
}

/* ── main task (called every 1ms) ── */
static void can_task(void) {
    /* fragment timeout */
    if (active && (vibe_millis() - last_ms) >= FRAG_TIMEOUT_MS)
        fail(PHASE_COMPLETE, DETAIL_BUSY);

    bsp_can_frame_t f;
    while (bsp_can_receive(&f)) {
        if (f.id == CAN_ID_START) handle_start(&f);
        else if (f.id == CAN_ID_DATA) handle_data(&f);
    }
}

/* ── entry ── */
void app_setup(void) {
    memset(frame_buf, 0, sizeof(frame_buf));
    active = 0;

    if (bsp_can_init(BSP_CAN_BITRATE_500K)) {
        uart_init();
        (void)vibe_task_every_ms(1, can_task);
    }
}
