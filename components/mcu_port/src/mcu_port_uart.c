#include "mcu_port_uart.h"
#include "board.h"

void mcu_port_uart_begin(uint32_t baudrate)
{
    board_uart_init(baudrate);
}

void mcu_port_uart_write(const uint8_t *data, uint16_t size)
{
    board_uart_write(data, size);
}

uint8_t mcu_port_uart_read_byte(uint8_t *data)
{
    return board_uart_read_byte(data);
}
