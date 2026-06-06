#pragma once

#include <stdint.h>

void bsp_serial_begin(uint32_t baudrate);
void bsp_serial_write(const char *text);
void bsp_serial_writeln(const char *text);
