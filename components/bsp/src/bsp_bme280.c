#include "bsp_bme280.h"

#include "board_pins.h"
#include "ch32v20x_gpio.h"
#include "ch32v20x_rcc.h"
#include "ch32v20x_spi.h"
#include "mcu_port_time.h"

#define BME280_REG_CHIP_ID      0xD0U
#define BME280_REG_RESET        0xE0U
#define BME280_REG_CTRL_HUM     0xF2U
#define BME280_REG_STATUS       0xF3U
#define BME280_REG_CTRL_MEAS    0xF4U
#define BME280_REG_CONFIG       0xF5U
#define BME280_REG_PRESS_MSB    0xF7U
#define BME280_CHIP_ID          0x60U
#define BME280_RESET_CMD        0xB6U
#define BME280_SPI_READ_MASK    0x80U
#define BME280_SPI_WRITE_MASK   0x7FU
#define BME280_SPI_TIMEOUT      50000U
#define BME280_SPI_DELAY_LOOPS  20U

typedef struct {
    uint16_t dig_t1;
    int16_t dig_t2;
    int16_t dig_t3;
    uint16_t dig_p1;
    int16_t dig_p2;
    int16_t dig_p3;
    int16_t dig_p4;
    int16_t dig_p5;
    int16_t dig_p6;
    int16_t dig_p7;
    int16_t dig_p8;
    int16_t dig_p9;
    uint8_t dig_h1;
    int16_t dig_h2;
    uint8_t dig_h3;
    int16_t dig_h4;
    int16_t dig_h5;
    int8_t dig_h6;
} bme280_calib_t;

static bme280_calib_t calib;
static int32_t t_fine;
static uint8_t initialized;
static uint8_t init_stage;
static uint8_t spi_mode3;
static uint8_t use_bitbang_spi;

static uint16_t read_u16_le(const uint8_t *data)
{
    return (uint16_t)(((uint16_t)data[1] << 8) | data[0]);
}

static int16_t read_i16_le(const uint8_t *data)
{
    return (int16_t)read_u16_le(data);
}

static int16_t sign_extend_12(uint16_t value)
{
    if ((value & 0x0800U) != 0U) {
        value |= 0xF000U;
    }
    return (int16_t)value;
}

static void cs_high(void)
{
    GPIO_SetBits(BOARD_SPI1_CS_GPIO_PORT, BOARD_SPI1_CS_GPIO_PIN);
}

static void cs_low(void)
{
    GPIO_ResetBits(BOARD_SPI1_CS_GPIO_PORT, BOARD_SPI1_CS_GPIO_PIN);
}

static void sck_set(uint8_t high)
{
    if (high) {
        GPIO_SetBits(BOARD_SPI1_SCK_GPIO_PORT, BOARD_SPI1_SCK_GPIO_PIN);
    } else {
        GPIO_ResetBits(BOARD_SPI1_SCK_GPIO_PORT, BOARD_SPI1_SCK_GPIO_PIN);
    }
}

static void mosi_set(uint8_t high)
{
    if (high) {
        GPIO_SetBits(BOARD_SPI1_MOSI_GPIO_PORT, BOARD_SPI1_MOSI_GPIO_PIN);
    } else {
        GPIO_ResetBits(BOARD_SPI1_MOSI_GPIO_PORT, BOARD_SPI1_MOSI_GPIO_PIN);
    }
}

static uint8_t miso_read(void)
{
    return (GPIO_ReadInputDataBit(BOARD_SPI1_MISO_GPIO_PORT, BOARD_SPI1_MISO_GPIO_PIN) != Bit_RESET) ? 1U : 0U;
}

static void spi_delay(void)
{
    volatile uint16_t i;

    for (i = 0U; i < BME280_SPI_DELAY_LOOPS; ++i) {
    }
}

static uint8_t bitbang_transfer_mode0(uint8_t tx)
{
    uint8_t rx = 0U;
    uint8_t mask;

    for (mask = 0x80U; mask != 0U; mask >>= 1U) {
        mosi_set((tx & mask) != 0U);
        spi_delay();
        sck_set(1U);
        spi_delay();
        if (miso_read()) {
            rx |= mask;
        }
        sck_set(0U);
        spi_delay();
    }

    return rx;
}

static uint8_t bitbang_transfer_mode3(uint8_t tx)
{
    uint8_t rx = 0U;
    uint8_t mask;

    for (mask = 0x80U; mask != 0U; mask >>= 1U) {
        sck_set(0U);
        mosi_set((tx & mask) != 0U);
        spi_delay();
        sck_set(1U);
        spi_delay();
        if (miso_read()) {
            rx |= mask;
        }
        spi_delay();
    }

    return rx;
}

static void swapped_mosi_set(uint8_t high)
{
    if (high) {
        GPIO_SetBits(BOARD_SPI1_MISO_GPIO_PORT, BOARD_SPI1_MISO_GPIO_PIN);
    } else {
        GPIO_ResetBits(BOARD_SPI1_MISO_GPIO_PORT, BOARD_SPI1_MISO_GPIO_PIN);
    }
}

static uint8_t swapped_miso_read(void)
{
    return (GPIO_ReadInputDataBit(BOARD_SPI1_MOSI_GPIO_PORT, BOARD_SPI1_MOSI_GPIO_PIN) != Bit_RESET) ? 1U : 0U;
}

static uint8_t swapped_transfer(uint8_t tx, uint8_t mode3)
{
    uint8_t rx = 0U;
    uint8_t mask;

    for (mask = 0x80U; mask != 0U; mask >>= 1U) {
        if (mode3) {
            sck_set(0U);
            swapped_mosi_set((tx & mask) != 0U);
            spi_delay();
            sck_set(1U);
            spi_delay();
        } else {
            swapped_mosi_set((tx & mask) != 0U);
            spi_delay();
            sck_set(1U);
            spi_delay();
        }

        if (swapped_miso_read()) {
            rx |= mask;
        }

        if (!mode3) {
            sck_set(0U);
            spi_delay();
        }
    }

    return rx;
}

static void swapped_gpio_init(uint8_t mode3)
{
    GPIO_InitTypeDef gpio = {0};

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_AFIO | BOARD_SPI1_GPIO_CLK, ENABLE);

    gpio.GPIO_Speed = GPIO_Speed_50MHz;

    gpio.GPIO_Pin = BOARD_SPI1_CS_GPIO_PIN;
    gpio.GPIO_Mode = GPIO_Mode_Out_PP;
    GPIO_Init(BOARD_SPI1_CS_GPIO_PORT, &gpio);
    cs_high();

    gpio.GPIO_Pin = BOARD_SPI1_SCK_GPIO_PIN;
    gpio.GPIO_Mode = GPIO_Mode_Out_PP;
    GPIO_Init(BOARD_SPI1_SCK_GPIO_PORT, &gpio);

    gpio.GPIO_Pin = BOARD_SPI1_MISO_GPIO_PIN;
    gpio.GPIO_Mode = GPIO_Mode_Out_PP;
    GPIO_Init(BOARD_SPI1_MISO_GPIO_PORT, &gpio);

    gpio.GPIO_Pin = BOARD_SPI1_MOSI_GPIO_PIN;
    gpio.GPIO_Mode = GPIO_Mode_IN_FLOATING;
    GPIO_Init(BOARD_SPI1_MOSI_GPIO_PORT, &gpio);

    swapped_mosi_set(0U);
    sck_set(mode3 ? 1U : 0U);
}

static uint8_t swapped_read_chip_id(uint8_t mode3)
{
    uint8_t value;

    swapped_gpio_init(mode3);
    cs_low();
    (void)swapped_transfer((uint8_t)(BME280_REG_CHIP_ID | BME280_SPI_READ_MASK), mode3);
    value = swapped_transfer(0x00U, mode3);
    cs_high();

    return value;
}

static uint8_t spi_transfer(uint8_t tx, uint8_t *rx)
{
    uint32_t timeout = 0U;
    uint8_t value;

    if (use_bitbang_spi) {
        value = spi_mode3 ? bitbang_transfer_mode3(tx) : bitbang_transfer_mode0(tx);
        if (rx != 0) {
            *rx = value;
        }
        return 1U;
    }

    while ((SPI_I2S_GetFlagStatus(BOARD_SPI1_INSTANCE, SPI_I2S_FLAG_TXE) == RESET) &&
           (timeout < BME280_SPI_TIMEOUT)) {
        ++timeout;
    }
    if (timeout >= BME280_SPI_TIMEOUT) {
        return 0U;
    }

    SPI_I2S_SendData(BOARD_SPI1_INSTANCE, tx);

    timeout = 0U;
    while ((SPI_I2S_GetFlagStatus(BOARD_SPI1_INSTANCE, SPI_I2S_FLAG_RXNE) == RESET) &&
           (timeout < BME280_SPI_TIMEOUT)) {
        ++timeout;
    }
    if (timeout >= BME280_SPI_TIMEOUT) {
        return 0U;
    }

    if (rx != 0) {
        *rx = (uint8_t)SPI_I2S_ReceiveData(BOARD_SPI1_INSTANCE);
    } else {
        (void)SPI_I2S_ReceiveData(BOARD_SPI1_INSTANCE);
    }

    return 1U;
}

static uint8_t read_regs(uint8_t reg, uint8_t *data, uint8_t len)
{
    uint8_t i;

    if ((data == 0) || (len == 0U)) {
        return 0U;
    }

    cs_low();
    if (!spi_transfer((uint8_t)(reg | BME280_SPI_READ_MASK), 0)) {
        cs_high();
        return 0U;
    }
    for (i = 0U; i < len; ++i) {
        if (!spi_transfer(0x00U, &data[i])) {
            cs_high();
            return 0U;
        }
    }
    cs_high();

    return 1U;
}

static uint8_t write_reg(uint8_t reg, uint8_t value)
{
    cs_low();
    if (!spi_transfer((uint8_t)(reg & BME280_SPI_WRITE_MASK), 0) ||
        !spi_transfer(value, 0)) {
        cs_high();
        return 0U;
    }
    cs_high();

    return 1U;
}

static void spi_gpio_init(void)
{
    GPIO_InitTypeDef gpio = {0};

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_AFIO | BOARD_SPI1_GPIO_CLK | BOARD_SPI1_CLK, ENABLE);

    gpio.GPIO_Pin = BOARD_SPI1_CS_GPIO_PIN;
    gpio.GPIO_Mode = GPIO_Mode_Out_PP;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(BOARD_SPI1_CS_GPIO_PORT, &gpio);
    cs_high();

    gpio.GPIO_Pin = BOARD_SPI1_SCK_GPIO_PIN;
    gpio.GPIO_Mode = use_bitbang_spi ? GPIO_Mode_Out_PP : GPIO_Mode_AF_PP;
    GPIO_Init(BOARD_SPI1_SCK_GPIO_PORT, &gpio);

    gpio.GPIO_Pin = BOARD_SPI1_MOSI_GPIO_PIN;
    gpio.GPIO_Mode = use_bitbang_spi ? GPIO_Mode_Out_PP : GPIO_Mode_AF_PP;
    GPIO_Init(BOARD_SPI1_MOSI_GPIO_PORT, &gpio);

    gpio.GPIO_Pin = BOARD_SPI1_MISO_GPIO_PIN;
    gpio.GPIO_Mode = GPIO_Mode_IN_FLOATING;
    GPIO_Init(BOARD_SPI1_MISO_GPIO_PORT, &gpio);

    mosi_set(0U);
    sck_set(spi_mode3 ? 1U : 0U);
}

static void spi_init(uint8_t use_mode3)
{
    SPI_InitTypeDef spi = {0};

    spi_mode3 = use_mode3;
    spi_gpio_init();

    if (use_bitbang_spi) {
        sck_set(spi_mode3 ? 1U : 0U);
        return;
    }

    SPI_I2S_DeInit(BOARD_SPI1_INSTANCE);
    spi.SPI_Direction = SPI_Direction_2Lines_FullDuplex;
    spi.SPI_Mode = SPI_Mode_Master;
    spi.SPI_DataSize = SPI_DataSize_8b;
    spi.SPI_CPOL = use_mode3 ? SPI_CPOL_High : SPI_CPOL_Low;
    spi.SPI_CPHA = use_mode3 ? SPI_CPHA_2Edge : SPI_CPHA_1Edge;
    spi.SPI_NSS = SPI_NSS_Soft;
    spi.SPI_BaudRatePrescaler = SPI_BaudRatePrescaler_64;
    spi.SPI_FirstBit = SPI_FirstBit_MSB;
    spi.SPI_CRCPolynomial = 7U;
    SPI_Init(BOARD_SPI1_INSTANCE, &spi);
    SPI_Cmd(BOARD_SPI1_INSTANCE, ENABLE);
}

static uint8_t read_calibration(void)
{
    uint8_t t_p[26];
    uint8_t h[7];

    if (!read_regs(0x88U, t_p, (uint8_t)sizeof(t_p)) ||
        !read_regs(0xE1U, h, (uint8_t)sizeof(h))) {
        return 0U;
    }

    calib.dig_t1 = read_u16_le(&t_p[0]);
    calib.dig_t2 = read_i16_le(&t_p[2]);
    calib.dig_t3 = read_i16_le(&t_p[4]);
    calib.dig_p1 = read_u16_le(&t_p[6]);
    calib.dig_p2 = read_i16_le(&t_p[8]);
    calib.dig_p3 = read_i16_le(&t_p[10]);
    calib.dig_p4 = read_i16_le(&t_p[12]);
    calib.dig_p5 = read_i16_le(&t_p[14]);
    calib.dig_p6 = read_i16_le(&t_p[16]);
    calib.dig_p7 = read_i16_le(&t_p[18]);
    calib.dig_p8 = read_i16_le(&t_p[20]);
    calib.dig_p9 = read_i16_le(&t_p[22]);
    calib.dig_h1 = t_p[25];
    calib.dig_h2 = read_i16_le(&h[0]);
    calib.dig_h3 = h[2];
    calib.dig_h4 = sign_extend_12((uint16_t)(((uint16_t)h[3] << 4) | (h[4] & 0x0FU)));
    calib.dig_h5 = sign_extend_12((uint16_t)(((uint16_t)h[5] << 4) | (h[4] >> 4)));
    calib.dig_h6 = (int8_t)h[6];

    return 1U;
}

static int32_t compensate_temperature(int32_t adc_t)
{
    int32_t var1;
    int32_t var2;

    var1 = ((((adc_t >> 3) - ((int32_t)calib.dig_t1 << 1))) * (int32_t)calib.dig_t2) >> 11;
    var2 = (((((adc_t >> 4) - (int32_t)calib.dig_t1) *
              ((adc_t >> 4) - (int32_t)calib.dig_t1)) >> 12) *
            (int32_t)calib.dig_t3) >> 14;
    t_fine = var1 + var2;

    return (t_fine * 5 + 128) >> 8;
}

static uint32_t compensate_pressure(int32_t adc_p)
{
    int64_t var1;
    int64_t var2;
    int64_t pressure;

    var1 = (int64_t)t_fine - 128000;
    var2 = var1 * var1 * (int64_t)calib.dig_p6;
    var2 = var2 + ((var1 * (int64_t)calib.dig_p5) << 17);
    var2 = var2 + (((int64_t)calib.dig_p4) << 35);
    var1 = ((var1 * var1 * (int64_t)calib.dig_p3) >> 8) + ((var1 * (int64_t)calib.dig_p2) << 12);
    var1 = (((((int64_t)1) << 47) + var1)) * (int64_t)calib.dig_p1 >> 33;

    if (var1 == 0) {
        return 0U;
    }

    pressure = 1048576 - adc_p;
    pressure = (((pressure << 31) - var2) * 3125) / var1;
    var1 = ((int64_t)calib.dig_p9 * (pressure >> 13) * (pressure >> 13)) >> 25;
    var2 = ((int64_t)calib.dig_p8 * pressure) >> 19;
    pressure = ((pressure + var1 + var2) >> 8) + (((int64_t)calib.dig_p7) << 4);

    return (uint32_t)(pressure >> 8);
}

static uint32_t compensate_humidity(int32_t adc_h)
{
    int32_t v;

    v = t_fine - 76800;
    v = (((((adc_h << 14) - ((int32_t)calib.dig_h4 << 20) -
             ((int32_t)calib.dig_h5 * v)) + 16384) >> 15) *
         (((((((v * (int32_t)calib.dig_h6) >> 10) *
              (((v * (int32_t)calib.dig_h3) >> 11) + 32768)) >> 10) +
            2097152) * (int32_t)calib.dig_h2 + 8192) >> 14));
    v = v - (((((v >> 15) * (v >> 15)) >> 7) * (int32_t)calib.dig_h1) >> 4);

    if (v < 0) {
        v = 0;
    }
    if (v > 419430400) {
        v = 419430400;
    }

    return (uint32_t)((v >> 12) * 100L / 1024L);
}

uint8_t bsp_bme280_chip_id(uint8_t *chip_id)
{
    if (chip_id == 0) {
        return 0U;
    }

    if (!initialized) {
        spi_gpio_init();
        spi_init(spi_mode3);
    }

    return read_regs(BME280_REG_CHIP_ID, chip_id, 1U);
}

uint8_t bsp_bme280_init_stage(void)
{
    return init_stage;
}

uint8_t bsp_bme280_debug_probe(bsp_bme280_debug_t *debug)
{
    if (debug == 0) {
        return 0U;
    }

    use_bitbang_spi = 1U;
    initialized = 1U;

    spi_init(0U);
    if (!bsp_bme280_chip_id(&debug->normal_mode0_id)) {
        debug->normal_mode0_id = 0xEEU;
    }

    spi_init(1U);
    if (!bsp_bme280_chip_id(&debug->normal_mode3_id)) {
        debug->normal_mode3_id = 0xEEU;
    }

    debug->swapped_mode0_id = swapped_read_chip_id(0U);
    debug->swapped_mode3_id = swapped_read_chip_id(1U);

    use_bitbang_spi = 1U;
    spi_init(0U);

    return 1U;
}

uint8_t bsp_bme280_read_reg(uint8_t reg, uint8_t *value)
{
    if (value == 0) {
        return 0U;
    }

    if (!initialized) {
        spi_gpio_init();
        spi_init(spi_mode3);
    }

    return read_regs(reg, value, 1U);
}

uint8_t bsp_bme280_init(void)
{
    uint8_t chip_id = 0U;
    uint8_t mode;
    uint8_t bitbang;

    init_stage = 0U;

    for (bitbang = 0U; bitbang < 2U; ++bitbang) {
        use_bitbang_spi = bitbang;
        for (mode = 0U; mode < 2U; ++mode) {
            spi_init(mode);
            initialized = 1U;
            mcu_port_delay_ms(10U);

            if (bsp_bme280_chip_id(&chip_id) && (chip_id == BME280_CHIP_ID)) {
                break;
            }
        }
        if (chip_id == BME280_CHIP_ID) {
            break;
        }
    }

    if (chip_id != BME280_CHIP_ID) {
        init_stage = 1U;
        return 0U;
    }

    (void)write_reg(BME280_REG_RESET, BME280_RESET_CMD);
    mcu_port_delay_ms(50U);

    if (!read_calibration()) {
        init_stage = 2U;
        return 0U;
    }

    if (!write_reg(BME280_REG_CTRL_HUM, 0x01U) ||
        !write_reg(BME280_REG_CONFIG, 0x00U) ||
        !write_reg(BME280_REG_CTRL_MEAS, 0x24U)) {
        init_stage = 3U;
        return 0U;
    }

    init_stage = 4U;
    return 1U;
}

uint8_t bsp_bme280_read_sample(bsp_bme280_sample_t *sample)
{
    uint8_t data[8];
    uint8_t status = 0U;
    uint8_t tries;
    int32_t adc_p;
    int32_t adc_t;
    int32_t adc_h;

    if ((sample == 0) || !initialized) {
        return 0U;
    }

    if (!write_reg(BME280_REG_CTRL_HUM, 0x01U) ||
        !write_reg(BME280_REG_CTRL_MEAS, 0x25U)) {
        return 0U;
    }

    for (tries = 0U; tries < 20U; ++tries) {
        if (!read_regs(BME280_REG_STATUS, &status, 1U)) {
            return 0U;
        }
        if ((status & 0x08U) == 0U) {
            break;
        }
        mcu_port_delay_ms(2U);
    }

    if (!read_regs(BME280_REG_PRESS_MSB, data, (uint8_t)sizeof(data))) {
        return 0U;
    }

    adc_p = (int32_t)((((uint32_t)data[0]) << 12) | (((uint32_t)data[1]) << 4) | (data[2] >> 4));
    adc_t = (int32_t)((((uint32_t)data[3]) << 12) | (((uint32_t)data[4]) << 4) | (data[5] >> 4));
    adc_h = (int32_t)((((uint32_t)data[6]) << 8) | data[7]);

    sample->temperature_centi_c = compensate_temperature(adc_t);
    sample->pressure_pa = compensate_pressure(adc_p);
    sample->humidity_centi_rh = compensate_humidity(adc_h);

    return 1U;
}
