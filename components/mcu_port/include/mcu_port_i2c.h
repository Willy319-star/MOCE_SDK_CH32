#pragma once

#include "ch32v20x.h"
#include <stdint.h>

typedef struct {
    I2C_TypeDef *i2c;
    GPIO_TypeDef *scl_port;
    uint16_t scl_pin;
    GPIO_TypeDef *sda_port;
    uint16_t sda_pin;
    uint32_t gpio_clock;
    uint32_t i2c_clock;
    uint32_t speed_hz;
    uint8_t initialized;
} mcu_port_i2c_t;

uint8_t mcu_port_i2c_init(mcu_port_i2c_t *i2c);
uint8_t mcu_port_i2c_is_ready(mcu_port_i2c_t *i2c, uint8_t address);
uint8_t mcu_port_i2c_write(mcu_port_i2c_t *i2c, uint8_t address, const uint8_t *data, uint16_t length);
uint8_t mcu_port_i2c_write_reg(mcu_port_i2c_t *i2c, uint8_t address, uint8_t reg, uint8_t value);
uint8_t mcu_port_i2c_read_reg(mcu_port_i2c_t *i2c, uint8_t address, uint8_t reg, uint8_t *value);
uint8_t mcu_port_i2c_read_regs(mcu_port_i2c_t *i2c, uint8_t address, uint8_t reg, uint8_t *data, uint16_t length);
