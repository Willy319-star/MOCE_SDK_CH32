#include "mcu_port_spi.h"
#include "ch32v20x_gpio.h"
#include "ch32v20x_rcc.h"
#include "ch32v20x_spi.h"

#define MCU_PORT_SPI_TIMEOUT_LOOPS 100000U

static uint8_t spi_mode_values(mcu_port_spi_mode_t mode,
                               uint16_t *clock_polarity,
                               uint16_t *clock_phase)
{
    if (clock_polarity == 0 || clock_phase == 0) {
        return 0U;
    }

    switch (mode) {
    case MCU_PORT_SPI_MODE_0:
        *clock_polarity = SPI_CPOL_Low;
        *clock_phase = SPI_CPHA_1Edge;
        return 1U;
    case MCU_PORT_SPI_MODE_1:
        *clock_polarity = SPI_CPOL_Low;
        *clock_phase = SPI_CPHA_2Edge;
        return 1U;
    case MCU_PORT_SPI_MODE_2:
        *clock_polarity = SPI_CPOL_High;
        *clock_phase = SPI_CPHA_1Edge;
        return 1U;
    case MCU_PORT_SPI_MODE_3:
        *clock_polarity = SPI_CPOL_High;
        *clock_phase = SPI_CPHA_2Edge;
        return 1U;
    default:
        return 0U;
    }
}

static uint8_t spi_divider_value(mcu_port_spi_divider_t divider,
                                 uint16_t *prescaler)
{
    if (prescaler == 0) {
        return 0U;
    }

    switch (divider) {
    case MCU_PORT_SPI_DIV_2:
        *prescaler = SPI_BaudRatePrescaler_2;
        return 1U;
    case MCU_PORT_SPI_DIV_4:
        *prescaler = SPI_BaudRatePrescaler_4;
        return 1U;
    case MCU_PORT_SPI_DIV_8:
        *prescaler = SPI_BaudRatePrescaler_8;
        return 1U;
    case MCU_PORT_SPI_DIV_16:
        *prescaler = SPI_BaudRatePrescaler_16;
        return 1U;
    case MCU_PORT_SPI_DIV_32:
        *prescaler = SPI_BaudRatePrescaler_32;
        return 1U;
    case MCU_PORT_SPI_DIV_64:
        *prescaler = SPI_BaudRatePrescaler_64;
        return 1U;
    case MCU_PORT_SPI_DIV_128:
        *prescaler = SPI_BaudRatePrescaler_128;
        return 1U;
    case MCU_PORT_SPI_DIV_256:
        *prescaler = SPI_BaudRatePrescaler_256;
        return 1U;
    default:
        return 0U;
    }
}

static void spi_set_chip_select(mcu_port_spi_t *spi, uint8_t selected)
{
    uint8_t drive_high;

    drive_high = spi->cs_active_low ? (uint8_t)!selected : selected;
    if (drive_high) {
        GPIO_SetBits(spi->cs_port, spi->cs_pin);
    } else {
        GPIO_ResetBits(spi->cs_port, spi->cs_pin);
    }
}

static uint8_t spi_wait_flag(mcu_port_spi_t *spi,
                             uint16_t flag,
                             FlagStatus expected)
{
    uint32_t guard = MCU_PORT_SPI_TIMEOUT_LOOPS;

    while (SPI_I2S_GetFlagStatus(spi->spi, flag) != expected) {
        if (--guard == 0U) {
            return 0U;
        }
    }

    return 1U;
}

static void spi_clear_receive_state(mcu_port_spi_t *spi)
{
    volatile uint16_t data;

    while (SPI_I2S_GetFlagStatus(spi->spi, SPI_I2S_FLAG_RXNE) != RESET) {
        data = SPI_I2S_ReceiveData(spi->spi);
        (void)data;
    }
    SPI_I2S_ClearFlag(spi->spi, SPI_I2S_FLAG_OVR);
}

static uint8_t spi_exchange_byte(mcu_port_spi_t *spi,
                                 uint8_t tx_byte,
                                 uint8_t *rx_byte)
{
    uint8_t received;

    if (!spi_wait_flag(spi, SPI_I2S_FLAG_TXE, SET)) {
        return 0U;
    }

    SPI_I2S_SendData(spi->spi, tx_byte);
    if (!spi_wait_flag(spi, SPI_I2S_FLAG_RXNE, SET)) {
        return 0U;
    }

    received = (uint8_t)SPI_I2S_ReceiveData(spi->spi);
    if (rx_byte != 0) {
        *rx_byte = received;
    }
    return 1U;
}

uint8_t mcu_port_spi_init(mcu_port_spi_t *spi)
{
    GPIO_InitTypeDef gpio = {0};
    SPI_InitTypeDef init = {0};
    uint16_t clock_polarity;
    uint16_t clock_phase;
    uint16_t prescaler;

    if (spi == 0 || spi->spi == 0 || spi->cs_port == 0 ||
        spi->sck_port == 0 || spi->miso_port == 0 || spi->mosi_port == 0 ||
        (spi->bit_order != MCU_PORT_SPI_MSB_FIRST &&
         spi->bit_order != MCU_PORT_SPI_LSB_FIRST) ||
        (spi->clock_bus != MCU_PORT_SPI_CLOCK_APB1 &&
         spi->clock_bus != MCU_PORT_SPI_CLOCK_APB2) ||
        !spi_mode_values(spi->mode, &clock_polarity, &clock_phase) ||
        !spi_divider_value(spi->divider, &prescaler)) {
        return 0U;
    }

    RCC_APB2PeriphClockCmd(spi->gpio_clock | RCC_APB2Periph_AFIO, ENABLE);
    if (spi->clock_bus == MCU_PORT_SPI_CLOCK_APB2) {
        RCC_APB2PeriphClockCmd(spi->spi_clock, ENABLE);
    } else {
        RCC_APB1PeriphClockCmd(spi->spi_clock, ENABLE);
    }

    gpio.GPIO_Pin = spi->cs_pin;
    gpio.GPIO_Mode = GPIO_Mode_Out_PP;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(spi->cs_port, &gpio);
    spi_set_chip_select(spi, 0U);

    gpio.GPIO_Pin = spi->sck_pin;
    gpio.GPIO_Mode = GPIO_Mode_AF_PP;
    GPIO_Init(spi->sck_port, &gpio);

    gpio.GPIO_Pin = spi->mosi_pin;
    gpio.GPIO_Mode = GPIO_Mode_AF_PP;
    GPIO_Init(spi->mosi_port, &gpio);

    gpio.GPIO_Pin = spi->miso_pin;
    gpio.GPIO_Mode = GPIO_Mode_IN_FLOATING;
    GPIO_Init(spi->miso_port, &gpio);

    SPI_I2S_DeInit(spi->spi);
    init.SPI_Direction = SPI_Direction_2Lines_FullDuplex;
    init.SPI_Mode = SPI_Mode_Master;
    init.SPI_DataSize = SPI_DataSize_8b;
    init.SPI_CPOL = clock_polarity;
    init.SPI_CPHA = clock_phase;
    init.SPI_NSS = SPI_NSS_Soft;
    init.SPI_BaudRatePrescaler = prescaler;
    init.SPI_FirstBit = spi->bit_order == MCU_PORT_SPI_MSB_FIRST
                            ? SPI_FirstBit_MSB
                            : SPI_FirstBit_LSB;
    init.SPI_CRCPolynomial = 7U;
    SPI_Init(spi->spi, &init);
    SPI_Cmd(spi->spi, ENABLE);
    spi_clear_receive_state(spi);

    spi->initialized = 1U;
    return 1U;
}

uint8_t mcu_port_spi_configure(mcu_port_spi_t *spi,
                               mcu_port_spi_mode_t mode,
                               mcu_port_spi_divider_t divider,
                               mcu_port_spi_bit_order_t bit_order)
{
    if (spi == 0) {
        return 0U;
    }

    spi->mode = mode;
    spi->divider = divider;
    spi->bit_order = bit_order;
    spi->initialized = 0U;
    return mcu_port_spi_init(spi);
}

uint8_t mcu_port_spi_transaction(mcu_port_spi_t *spi,
                                 const uint8_t *tx_data,
                                 uint8_t *rx_data,
                                 uint16_t length,
                                 uint8_t dummy_byte)
{
    uint16_t index;
    uint8_t tx_byte;
    uint8_t *rx_byte;

    if (spi == 0 || length == 0U || (tx_data == 0 && rx_data == 0)) {
        return 0U;
    }
    if (!spi->initialized && !mcu_port_spi_init(spi)) {
        return 0U;
    }

    spi_clear_receive_state(spi);
    spi_set_chip_select(spi, 1U);

    for (index = 0U; index < length; ++index) {
        tx_byte = tx_data == 0 ? dummy_byte : tx_data[index];
        rx_byte = rx_data == 0 ? 0 : &rx_data[index];
        if (!spi_exchange_byte(spi, tx_byte, rx_byte)) {
            spi_set_chip_select(spi, 0U);
            spi_clear_receive_state(spi);
            return 0U;
        }
    }

    if (!spi_wait_flag(spi, SPI_I2S_FLAG_BSY, RESET)) {
        spi_set_chip_select(spi, 0U);
        spi_clear_receive_state(spi);
        return 0U;
    }

    spi_set_chip_select(spi, 0U);
    return 1U;
}

void mcu_port_spi_deinit(mcu_port_spi_t *spi)
{
    if (spi == 0 || spi->spi == 0 || spi->cs_port == 0) {
        return;
    }

    spi_set_chip_select(spi, 0U);
    SPI_Cmd(spi->spi, DISABLE);
    SPI_I2S_DeInit(spi->spi);
    spi->initialized = 0U;
}
