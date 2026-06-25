#include "board_pins.h"
#include "mcu_port_uart.h"
#include "vibe_api.h"
#include "vibe_runtime.h"

#include <stdint.h>
#include <stdio.h>

#define VC02_UART_BAUDRATE       115200U
#define VC02_RX_POLL_MS          10U
#define VC02_STATUS_PERIOD_MS    1000U
#define VC02_RX_IDLE_FLUSH_MS    80U
#define VC02_RX_BUFFER_SIZE      32U

/* Set to 1 only when the VC-02 firmware is known to accept ASCII input. */
#define VC02_ENABLE_ASCII_PROBE  0U

static uint8_t vc02_rx_buffer[VC02_RX_BUFFER_SIZE];
static uint8_t vc02_rx_len;
static uint32_t vc02_last_rx_ms;
static uint32_t vc02_rx_packet_count;
static uint32_t vc02_rx_byte_count;
static uint32_t vc02_status_count;

static char hex_digit(uint8_t value)
{
    value &= 0x0FU;
    if (value < 10U) {
        return (char)('0' + value);
    }
    return (char)('A' + value - 10U);
}

static void append_hex_byte(char *line, uint16_t *pos, uint16_t size, uint8_t value)
{
    if ((*pos + 3U) >= size) {
        return;
    }

    line[*pos] = hex_digit((uint8_t)(value >> 4U));
    *pos = (uint16_t)(*pos + 1U);
    line[*pos] = hex_digit(value);
    *pos = (uint16_t)(*pos + 1U);
    line[*pos] = ' ';
    *pos = (uint16_t)(*pos + 1U);
    line[*pos] = '\0';
}

static char printable_ascii(uint8_t value)
{
    if (value >= 0x20U && value <= 0x7EU) {
        return (char)value;
    }
    return '.';
}

static void vc02_flush_rx(void)
{
    char line[160];
    uint16_t pos = 0U;
    uint8_t i;

    if (vc02_rx_len == 0U) {
        return;
    }

    (void)snprintf(line, sizeof(line), "VC02 RX len=%u hex=", (unsigned)vc02_rx_len);
    while (line[pos] != '\0' && pos < (sizeof(line) - 1U)) {
        ++pos;
    }

    for (i = 0U; i < vc02_rx_len; ++i) {
        append_hex_byte(line, &pos, sizeof(line), vc02_rx_buffer[i]);
    }

    if ((pos + 8U) < sizeof(line)) {
        line[pos++] = 'a';
        line[pos++] = 's';
        line[pos++] = 'c';
        line[pos++] = 'i';
        line[pos++] = 'i';
        line[pos++] = '=';
        line[pos++] = '"';
        line[pos] = '\0';
    }

    for (i = 0U; i < vc02_rx_len && (pos + 2U) < sizeof(line); ++i) {
        line[pos++] = printable_ascii(vc02_rx_buffer[i]);
        line[pos] = '\0';
    }

    if ((pos + 2U) < sizeof(line)) {
        line[pos++] = '"';
        line[pos] = '\0';
    }

    vibe_println(line);
    ++vc02_rx_packet_count;
    vc02_rx_len = 0U;
}

static void vc02_rx_task(void)
{
    uint8_t byte;
    uint32_t now = vibe_millis();

    while (mcu_port_uart_read_byte(&byte)) {
        if (vc02_rx_len < VC02_RX_BUFFER_SIZE) {
            vc02_rx_buffer[vc02_rx_len++] = byte;
        }
        ++vc02_rx_byte_count;
        vc02_last_rx_ms = now;

        if (vc02_rx_len >= VC02_RX_BUFFER_SIZE) {
            vc02_flush_rx();
        }
    }

    if (vc02_rx_len != 0U && (now - vc02_last_rx_ms) >= VC02_RX_IDLE_FLUSH_MS) {
        vc02_flush_rx();
    }
}

static void vc02_status_task(void)
{
    char line[96];

    ++vc02_status_count;
    vibe_led_toggle();

    (void)snprintf(line, sizeof(line),
                   "VC02 alive=%lu rx_packets=%lu rx_bytes=%lu",
                   (unsigned long)vc02_status_count,
                   (unsigned long)vc02_rx_packet_count,
                   (unsigned long)vc02_rx_byte_count);
    vibe_println(line);

#if VC02_ENABLE_ASCII_PROBE
    mcu_port_uart_write((const uint8_t *)"VC02_PING\r\n", 11U);
#endif
}

void app_setup(void)
{
    vibe_serial_begin(VC02_UART_BAUDRATE);
    vibe_wait_ms(300U);

    vibe_println("MOCE VC-02 UART test");
    vibe_println("VC-02 manual: UART1 115200 bps, 3.6-5V power, >500mA supply");
    vibe_println("Wire: CH32 PA9 TX -> VC-02 RX1/RXD_IN, CH32 PA10 RX <- VC-02 TX1/TXD_IN, GND common");
    vibe_println("Listening for VC-02 bytes; speak configured wake/command words and watch hex/ascii output");

    (void)vibe_task_every_ms(VC02_RX_POLL_MS, vc02_rx_task);
    (void)vibe_task_every_ms(VC02_STATUS_PERIOD_MS, vc02_status_task);
}
