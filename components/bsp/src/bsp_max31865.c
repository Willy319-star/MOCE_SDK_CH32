#include "bsp_max31865.h"

#include "board_pins.h"
#include "ch32v20x_gpio.h"
#include "ch32v20x_rcc.h"
#include "ch32v20x_spi.h"
#include "mcu_port_time.h"

#define MAX31865_REG_CONFIG       0x00U
#define MAX31865_REG_RTD_MSB      0x01U
#define MAX31865_REG_FAULT_STATUS 0x07U
#define MAX31865_WRITE_MASK       0x80U
#define MAX31865_CONFIG_VBIAS     0x80U
#define MAX31865_CONFIG_1SHOT     0x20U
#define MAX31865_CONFIG_FAULT_CLR 0x02U
#define MAX31865_CONFIG_BASE      (MAX31865_CONFIG_VBIAS | MAX31865_CONFIG_FAULT_CLR)
#define MAX31865_CONFIG_ONESHOT   (MAX31865_CONFIG_VBIAS | MAX31865_CONFIG_1SHOT | MAX31865_CONFIG_FAULT_CLR)
#define MAX31865_SPI_TIMEOUT      50000U

static uint8_t initialized;
static uint8_t ready;

static void small_delay(void)
{
    volatile uint16_t i;

    for (i = 0U; i < 80U; ++i) {
    }
}

static void cs_high(void)
{
    while (SPI_I2S_GetFlagStatus(BOARD_SPI1_INSTANCE, SPI_I2S_FLAG_BSY) != RESET) {
    }
    GPIO_SetBits(BOARD_SPI1_CS_GPIO_PORT, BOARD_SPI1_CS_GPIO_PIN);
    small_delay();
}

static void cs_low(void)
{
    GPIO_ResetBits(BOARD_SPI1_CS_GPIO_PORT, BOARD_SPI1_CS_GPIO_PIN);
    small_delay();
}

static uint8_t spi_transfer(uint8_t tx, uint8_t *rx)
{
    uint32_t timeout = 0U;
    uint8_t dummy;

    while ((SPI_I2S_GetFlagStatus(BOARD_SPI1_INSTANCE, SPI_I2S_FLAG_TXE) == RESET) &&
           (timeout < MAX31865_SPI_TIMEOUT)) {
        ++timeout;
    }
    if (timeout >= MAX31865_SPI_TIMEOUT) {
        return 0U;
    }

    SPI_I2S_SendData(BOARD_SPI1_INSTANCE, tx);

    timeout = 0U;
    while ((SPI_I2S_GetFlagStatus(BOARD_SPI1_INSTANCE, SPI_I2S_FLAG_RXNE) == RESET) &&
           (timeout < MAX31865_SPI_TIMEOUT)) {
        ++timeout;
    }
    if (timeout >= MAX31865_SPI_TIMEOUT) {
        return 0U;
    }

    dummy = (uint8_t)SPI_I2S_ReceiveData(BOARD_SPI1_INSTANCE);
    if (rx != 0) {
        *rx = dummy;
    }

    return 1U;
}

static uint8_t read_reg_raw(uint8_t reg, uint8_t *value)
{
    if (value == 0) {
        return 0U;
    }

    cs_low();
    if (!spi_transfer((uint8_t)(reg & 0x7FU), 0) ||
        !spi_transfer(0xFFU, value)) {
        cs_high();
        return 0U;
    }
    cs_high();

    return 1U;
}

static uint8_t write_reg_raw(uint8_t reg, uint8_t value)
{
    cs_low();
    if (!spi_transfer((uint8_t)(reg | MAX31865_WRITE_MASK), 0) ||
        !spi_transfer(value, 0)) {
        cs_high();
        return 0U;
    }
    cs_high();

    return 1U;
}

static void gpio_spi_init(void)
{
    GPIO_InitTypeDef gpio = {0};
    SPI_InitTypeDef spi = {0};
    uint8_t clear_rx;

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_AFIO | BOARD_SPI1_GPIO_CLK | BOARD_SPI1_CLK, ENABLE);

    gpio.GPIO_Speed = GPIO_Speed_50MHz;

    gpio.GPIO_Pin = BOARD_SPI1_CS_GPIO_PIN;
    gpio.GPIO_Mode = GPIO_Mode_Out_PP;
    GPIO_Init(BOARD_SPI1_CS_GPIO_PORT, &gpio);
    GPIO_SetBits(BOARD_SPI1_CS_GPIO_PORT, BOARD_SPI1_CS_GPIO_PIN);

    gpio.GPIO_Pin = BOARD_SPI1_SCK_GPIO_PIN | BOARD_SPI1_MOSI_GPIO_PIN;
    gpio.GPIO_Mode = GPIO_Mode_AF_PP;
    GPIO_Init(BOARD_SPI1_SCK_GPIO_PORT, &gpio);

    gpio.GPIO_Pin = BOARD_SPI1_MISO_GPIO_PIN;
    gpio.GPIO_Mode = GPIO_Mode_IN_FLOATING;
    GPIO_Init(BOARD_SPI1_MISO_GPIO_PORT, &gpio);

    SPI_I2S_DeInit(BOARD_SPI1_INSTANCE);
    spi.SPI_Direction = SPI_Direction_2Lines_FullDuplex;
    spi.SPI_Mode = SPI_Mode_Master;
    spi.SPI_DataSize = SPI_DataSize_8b;
    spi.SPI_CPOL = SPI_CPOL_Low;
    spi.SPI_CPHA = SPI_CPHA_2Edge;
    spi.SPI_NSS = SPI_NSS_Soft;
    spi.SPI_BaudRatePrescaler = SPI_BaudRatePrescaler_256;
    spi.SPI_FirstBit = SPI_FirstBit_MSB;
    spi.SPI_CRCPolynomial = 7U;
    SPI_Init(BOARD_SPI1_INSTANCE, &spi);
    SPI_Cmd(BOARD_SPI1_INSTANCE, ENABLE);

    while (SPI_I2S_GetFlagStatus(BOARD_SPI1_INSTANCE, SPI_I2S_FLAG_RXNE) != RESET) {
        clear_rx = (uint8_t)SPI_I2S_ReceiveData(BOARD_SPI1_INSTANCE);
        (void)clear_rx;
    }
}

static uint8_t config_is_stable(uint8_t expected_mask)
{
    uint8_t a = 0U;
    uint8_t b = 0U;

    if (!read_reg_raw(MAX31865_REG_CONFIG, &a) ||
        !read_reg_raw(MAX31865_REG_CONFIG, &b)) {
        return 0U;
    }

    return ((a == b) && ((a & expected_mask) == expected_mask)) ? 1U : 0U;
}

uint8_t bsp_max31865_init(void)
{
    ready = 0U;
    gpio_spi_init();
    initialized = 1U;
    mcu_port_delay_ms(5U);

    if (!write_reg_raw(MAX31865_REG_CONFIG, MAX31865_CONFIG_BASE)) {
        return 0U;
    }
    mcu_port_delay_ms(20U);

    if (!config_is_stable(MAX31865_CONFIG_VBIAS)) {
        return 0U;
    }

    ready = 1U;
    return 1U;
}

uint8_t bsp_max31865_is_ready(void)
{
    return ready;
}

uint8_t bsp_max31865_active_mode(void)
{
    return 1U;
}

uint8_t bsp_max31865_probe(bsp_max31865_probe_t *probe)
{
    uint8_t before = 0U;
    uint8_t after = 0U;

    if (probe == 0) {
        return 0U;
    }

    if (!initialized) {
        gpio_spi_init();
        initialized = 1U;
    }

    (void)read_reg_raw(MAX31865_REG_CONFIG, &before);
    (void)write_reg_raw(MAX31865_REG_CONFIG, MAX31865_CONFIG_BASE);
    mcu_port_delay_ms(20U);
    (void)read_reg_raw(MAX31865_REG_CONFIG, &after);

    probe->mode0_before = 0U;
    probe->mode0_after = 0U;
    probe->mode1_before = before;
    probe->mode1_after = after;
    probe->mode2_before = 0U;
    probe->mode2_after = 0U;
    probe->mode3_before = 0U;
    probe->mode3_after = 0U;

    return 1U;
}

uint8_t bsp_max31865_read_reg(uint8_t reg, uint8_t *value)
{
    if (value == 0) {
        return 0U;
    }

    if (!initialized && !bsp_max31865_init()) {
        return 0U;
    }

    return read_reg_raw(reg, value);
}

uint8_t bsp_max31865_write_reg(uint8_t reg, uint8_t value)
{
    if (!initialized && !bsp_max31865_init()) {
        return 0U;
    }

    return write_reg_raw(reg, value);
}

uint8_t bsp_max31865_read_status(bsp_max31865_status_t *status)
{
    uint8_t rtd_msb = 0U;
    uint8_t rtd_lsb = 0U;

    if (status == 0) {
        return 0U;
    }

    if (!ready) {
        return 0U;
    }

    if (!read_reg_raw(MAX31865_REG_CONFIG, &status->config_before)) {
        return 0U;
    }

    if (!write_reg_raw(MAX31865_REG_CONFIG, MAX31865_CONFIG_BASE)) {
        return 0U;
    }
    mcu_port_delay_ms(20U);

    if (!write_reg_raw(MAX31865_REG_CONFIG, MAX31865_CONFIG_ONESHOT)) {
        return 0U;
    }
    mcu_port_delay_ms(80U);

    if (!read_reg_raw(MAX31865_REG_CONFIG, &status->config_after) ||
        !read_reg_raw(MAX31865_REG_FAULT_STATUS, &status->fault_status) ||
        !read_reg_raw(MAX31865_REG_RTD_MSB, &rtd_msb) ||
        !read_reg_raw((uint8_t)(MAX31865_REG_RTD_MSB + 1U), &rtd_lsb)) {
        return 0U;
    }

    if ((status->config_after == 0x00U) || (status->config_after == 0xFFU)) {
        ready = 0U;
        return 0U;
    }

    status->rtd_raw = (uint16_t)((((uint16_t)rtd_msb << 8) | rtd_lsb) >> 1);
    return 1U;
}
