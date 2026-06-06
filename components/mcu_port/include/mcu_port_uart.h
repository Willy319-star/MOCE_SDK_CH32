#pragma once

#include <stdint.h>

void mcu_port_uart_begin(uint32_t baudrate);
void mcu_port_uart_write(const uint8_t *data, uint16_t size);
uint8_t mcu_port_uart_read_byte(uint8_t *data);
