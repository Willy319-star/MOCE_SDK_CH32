#pragma once

#include "ch32v20x.h"
#include <stdint.h>

typedef struct {
    GPIO_TypeDef *port;
    uint16_t pin;
    uint32_t clock;
    uint8_t active_low;
} mcu_port_gpio_t;

void mcu_port_gpio_configure_output(const mcu_port_gpio_t *gpio);
void mcu_port_gpio_configure_input(const mcu_port_gpio_t *gpio, uint8_t pull_up);
void mcu_port_gpio_write(const mcu_port_gpio_t *gpio, uint8_t active);
uint8_t mcu_port_gpio_read(const mcu_port_gpio_t *gpio);
void mcu_port_gpio_toggle(const mcu_port_gpio_t *gpio);
