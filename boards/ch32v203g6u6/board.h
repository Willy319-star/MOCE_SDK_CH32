#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void board_init(void);
void board_clock_init(void);
void board_gpio_init(void);
void board_uart_init(uint32_t baudrate);
void board_uart_write(const uint8_t *data, uint16_t size);
uint8_t board_uart_read_byte(uint8_t *data);
void board_error_handler(void);

#ifdef __cplusplus
}
#endif
