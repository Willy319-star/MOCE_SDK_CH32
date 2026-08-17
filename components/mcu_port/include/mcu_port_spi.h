#ifndef MCU_PORT_SPI_H__
#define MCU_PORT_SPI_H__

#include "ch32v20x.h"
#include <stdint.h>

typedef enum {
    MCU_PORT_SPI_MODE_0 = 0,
    MCU_PORT_SPI_MODE_1,
    MCU_PORT_SPI_MODE_2,
    MCU_PORT_SPI_MODE_3,
} mcu_port_spi_mode_t;

typedef enum {
    MCU_PORT_SPI_DIV_2 = 2,
    MCU_PORT_SPI_DIV_4 = 4,
    MCU_PORT_SPI_DIV_8 = 8,
    MCU_PORT_SPI_DIV_16 = 16,
    MCU_PORT_SPI_DIV_32 = 32,
    MCU_PORT_SPI_DIV_64 = 64,
    MCU_PORT_SPI_DIV_128 = 128,
    MCU_PORT_SPI_DIV_256 = 256,
} mcu_port_spi_divider_t;

typedef enum {
    MCU_PORT_SPI_MSB_FIRST = 0,
    MCU_PORT_SPI_LSB_FIRST,
} mcu_port_spi_bit_order_t;

typedef enum {
    MCU_PORT_SPI_CLOCK_APB1 = 0,
    MCU_PORT_SPI_CLOCK_APB2,
} mcu_port_spi_clock_bus_t;

typedef struct {
    SPI_TypeDef *spi;
    GPIO_TypeDef *cs_port;
    uint16_t cs_pin;
    GPIO_TypeDef *sck_port;
    uint16_t sck_pin;
    GPIO_TypeDef *miso_port;
    uint16_t miso_pin;
    GPIO_TypeDef *mosi_port;
    uint16_t mosi_pin;
    uint32_t gpio_clock;
    uint32_t spi_clock;
    mcu_port_spi_clock_bus_t clock_bus;
    mcu_port_spi_mode_t mode;
    mcu_port_spi_divider_t divider;
    mcu_port_spi_bit_order_t bit_order;
    uint8_t cs_active_low;
    uint8_t initialized;
} mcu_port_spi_t;

uint8_t mcu_port_spi_init(mcu_port_spi_t *spi);
uint8_t mcu_port_spi_configure(mcu_port_spi_t *spi,
                               mcu_port_spi_mode_t mode,
                               mcu_port_spi_divider_t divider,
                               mcu_port_spi_bit_order_t bit_order);
uint8_t mcu_port_spi_transaction(mcu_port_spi_t *spi,
                                 const uint8_t *tx_data,
                                 uint8_t *rx_data,
                                 uint16_t length,
                                 uint8_t dummy_byte);
void mcu_port_spi_deinit(mcu_port_spi_t *spi);

#endif
