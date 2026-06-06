#include "bsp_serial.h"
#include "mcu_port_uart.h"

static uint16_t str_len(const char *text)
{
    uint16_t len = 0U;

    if (text == 0) {
        return 0U;
    }

    while (text[len] != '\0') {
        ++len;
    }

    return len;
}

void bsp_serial_begin(uint32_t baudrate)
{
    mcu_port_uart_begin(baudrate);
}

void bsp_serial_write(const char *text)
{
    mcu_port_uart_write((const uint8_t *)text, str_len(text));
}

void bsp_serial_writeln(const char *text)
{
    bsp_serial_write(text);
    bsp_serial_write("\r\n");
}
