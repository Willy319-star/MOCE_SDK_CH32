#include "bsp_vl53l0x.h"

#include "board_pins.h"
#include "mcu_port_time.h"
#include "ch32v20x_gpio.h"
#include "ch32v20x_rcc.h"

#define VL53L0X_SYSRANGE_START                         0x00U
#define VL53L0X_SYSTEM_SEQUENCE_CONFIG                 0x01U
#define VL53L0X_SYSTEM_INTERRUPT_CONFIG_GPIO           0x0AU
#define VL53L0X_SYSTEM_INTERRUPT_CLEAR                 0x0BU
#define VL53L0X_RESULT_INTERRUPT_STATUS                0x13U
#define VL53L0X_RESULT_RANGE_STATUS                    0x14U
#define VL53L0X_GLOBAL_CONFIG_SPAD_ENABLES_REF_0       0xB0U
#define VL53L0X_DYNAMIC_SPAD_NUM_REQUESTED_REF_SPAD    0x4EU
#define VL53L0X_DYNAMIC_SPAD_REF_EN_START_OFFSET       0x4FU
#define VL53L0X_GLOBAL_CONFIG_REF_EN_START_SELECT      0xB6U
#define VL53L0X_GPIO_HV_MUX_ACTIVE_HIGH                0x84U
#define VL53L0X_MSRC_CONFIG_CONTROL                    0x60U
#define VL53L0X_FINAL_RANGE_CONFIG_MIN_COUNT_RATE_RTN  0x44U
#define VL53L0X_VHV_CONFIG_PAD_SCL_SDA_EXTSUP_HV       0x89U
#define VL53L0X_MSRC_CONFIG_TIMEOUT_MACROP             0x46U
#define VL53L0X_PRE_RANGE_CONFIG_VCSEL_PERIOD          0x50U
#define VL53L0X_PRE_RANGE_CONFIG_TIMEOUT_MACROP_HI     0x51U
#define VL53L0X_FINAL_RANGE_CONFIG_VCSEL_PERIOD        0x70U
#define VL53L0X_FINAL_RANGE_CONFIG_TIMEOUT_MACROP_HI   0x71U
#define VL53L0X_INIT_DELAY_MS                          300U
#define VL53L0X_CALIBRATION_TIMEOUT_MS                 100U
#define VL53L0X_MEASUREMENT_BUDGET_US                  100000U

typedef struct {
    uint8_t tcc;
    uint8_t dss;
    uint8_t msrc;
    uint8_t pre_range;
    uint8_t final_range;
} vl53l0x_sequence_enables_t;

typedef struct {
    uint16_t pre_range_mclks;
    uint16_t final_range_mclks;
    uint32_t msrc_dss_tcc_us;
    uint32_t pre_range_us;
} vl53l0x_sequence_timeouts_t;

typedef struct {
    uint8_t reg;
    uint8_t value;
} vl53l0x_reg_value_t;

static uint8_t vl53l0x_stop_variable;
static uint8_t vl53l0x_measurement_pending;
static uint8_t vl53l0x_status_code;
static uint8_t vl53l0x_model_id_value;
static uint8_t vl53l0x_range_status_value;
static uint8_t vl53l0x_last_result[12];
static uint16_t vl53l0x_raw_distance_mm;
static uint32_t vl53l0x_errors;
static uint8_t vl53l0x_soft_i2c_initialized;

static void soft_i2c_delay(void)
{
    for (volatile uint32_t i = 0U; i < 120U; ++i) {
    }
}

static void soft_i2c_enable_gpio_clock(GPIO_TypeDef *port)
{
#ifdef GPIOA
    if (port == GPIOA) {
        RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);
    }
#endif
#ifdef GPIOB
    if (port == GPIOB) {
        RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);
    }
#endif
#ifdef GPIOC
    if (port == GPIOC) {
        RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOC, ENABLE);
    }
#endif
#ifdef GPIOD
    if (port == GPIOD) {
        RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOD, ENABLE);
    }
#endif
}

static void soft_i2c_init(void)
{
    GPIO_InitTypeDef gpio = {0};

    if (vl53l0x_soft_i2c_initialized) {
        return;
    }

    soft_i2c_enable_gpio_clock(BOARD_VL53L0X_SCL_GPIO_PORT);
    soft_i2c_enable_gpio_clock(BOARD_VL53L0X_SDA_GPIO_PORT);

    gpio.GPIO_Mode = GPIO_Mode_Out_OD;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    gpio.GPIO_Pin = BOARD_VL53L0X_SCL_GPIO_PIN;
    GPIO_Init(BOARD_VL53L0X_SCL_GPIO_PORT, &gpio);
    gpio.GPIO_Pin = BOARD_VL53L0X_SDA_GPIO_PIN;
    GPIO_Init(BOARD_VL53L0X_SDA_GPIO_PORT, &gpio);

    GPIO_SetBits(BOARD_VL53L0X_SCL_GPIO_PORT, BOARD_VL53L0X_SCL_GPIO_PIN);
    GPIO_SetBits(BOARD_VL53L0X_SDA_GPIO_PORT, BOARD_VL53L0X_SDA_GPIO_PIN);
    soft_i2c_delay();

    vl53l0x_soft_i2c_initialized = 1U;
}

static void soft_i2c_scl(uint8_t high)
{
    if (high) {
        GPIO_SetBits(BOARD_VL53L0X_SCL_GPIO_PORT, BOARD_VL53L0X_SCL_GPIO_PIN);
    } else {
        GPIO_ResetBits(BOARD_VL53L0X_SCL_GPIO_PORT, BOARD_VL53L0X_SCL_GPIO_PIN);
    }
    soft_i2c_delay();
}

static void soft_i2c_sda(uint8_t high)
{
    if (high) {
        GPIO_SetBits(BOARD_VL53L0X_SDA_GPIO_PORT, BOARD_VL53L0X_SDA_GPIO_PIN);
    } else {
        GPIO_ResetBits(BOARD_VL53L0X_SDA_GPIO_PORT, BOARD_VL53L0X_SDA_GPIO_PIN);
    }
    soft_i2c_delay();
}

static uint8_t soft_i2c_read_sda(void)
{
    return (GPIO_ReadInputDataBit(BOARD_VL53L0X_SDA_GPIO_PORT, BOARD_VL53L0X_SDA_GPIO_PIN) != Bit_RESET) ? 1U : 0U;
}

static void soft_i2c_start(void)
{
    soft_i2c_sda(1U);
    soft_i2c_scl(1U);
    soft_i2c_sda(0U);
    soft_i2c_scl(0U);
}

static void soft_i2c_stop(void)
{
    soft_i2c_sda(0U);
    soft_i2c_scl(1U);
    soft_i2c_sda(1U);
}

static uint8_t soft_i2c_write_byte(uint8_t value)
{
    uint8_t ack;

    for (uint8_t bit = 0U; bit < 8U; ++bit) {
        soft_i2c_sda((value & 0x80U) ? 1U : 0U);
        soft_i2c_scl(1U);
        soft_i2c_scl(0U);
        value <<= 1;
    }

    soft_i2c_sda(1U);
    soft_i2c_scl(1U);
    ack = soft_i2c_read_sda() ? 0U : 1U;
    soft_i2c_scl(0U);
    return ack;
}

static uint8_t soft_i2c_read_byte(uint8_t ack)
{
    uint8_t value = 0U;

    soft_i2c_sda(1U);
    for (uint8_t bit = 0U; bit < 8U; ++bit) {
        value <<= 1;
        soft_i2c_scl(1U);
        if (soft_i2c_read_sda()) {
            value |= 1U;
        }
        soft_i2c_scl(0U);
    }

    soft_i2c_sda(ack ? 0U : 1U);
    soft_i2c_scl(1U);
    soft_i2c_scl(0U);
    soft_i2c_sda(1U);
    return value;
}

static uint8_t bus_init(void)
{
    soft_i2c_init();
    return 1U;
}

static uint8_t bus_is_ready(uint8_t address)
{
    uint8_t ok;

    soft_i2c_init();
    soft_i2c_start();
    ok = soft_i2c_write_byte((uint8_t)(address << 1));
    soft_i2c_stop();
    return ok;
}

static uint8_t bus_write_reg(uint8_t address, uint8_t reg, uint8_t value)
{
    uint8_t ok;

    soft_i2c_start();
    ok = soft_i2c_write_byte((uint8_t)(address << 1));
    ok = (uint8_t)(ok && soft_i2c_write_byte(reg));
    ok = (uint8_t)(ok && soft_i2c_write_byte(value));
    soft_i2c_stop();
    return ok;
}

static uint8_t bus_read_regs(uint8_t address, uint8_t reg, uint8_t *data, uint16_t length)
{
    uint16_t i;
    uint8_t ok;

    if (data == 0 || length == 0U) {
        return 0U;
    }

    soft_i2c_start();
    ok = soft_i2c_write_byte((uint8_t)(address << 1));
    ok = (uint8_t)(ok && soft_i2c_write_byte(reg));
    soft_i2c_start();
    ok = (uint8_t)(ok && soft_i2c_write_byte((uint8_t)((address << 1) | 0x01U)));
    if (!ok) {
        soft_i2c_stop();
        return 0U;
    }

    for (i = 0U; i < length; ++i) {
        data[i] = soft_i2c_read_byte((uint8_t)((i + 1U) < length));
    }

    soft_i2c_stop();
    return 1U;
}

static const vl53l0x_reg_value_t vl53l0x_tuning_settings[] = {
    {0xFF, 0x01}, {0x00, 0x00}, {0xFF, 0x00}, {0x09, 0x00},
    {0x10, 0x00}, {0x11, 0x00}, {0x24, 0x01}, {0x25, 0xFF},
    {0x75, 0x00}, {0xFF, 0x01}, {0x4E, 0x2C}, {0x48, 0x00},
    {0x30, 0x20}, {0xFF, 0x00}, {0x30, 0x09}, {0x54, 0x00},
    {0x31, 0x04}, {0x32, 0x03}, {0x40, 0x83}, {0x46, 0x25},
    {0x60, 0x00}, {0x27, 0x00}, {0x50, 0x06}, {0x51, 0x00},
    {0x52, 0x96}, {0x56, 0x08}, {0x57, 0x30}, {0x61, 0x00},
    {0x62, 0x00}, {0x64, 0x00}, {0x65, 0x00}, {0x66, 0xA0},
    {0xFF, 0x01}, {0x22, 0x32}, {0x47, 0x14}, {0x49, 0xFF},
    {0x4A, 0x00}, {0xFF, 0x00}, {0x7A, 0x0A}, {0x7B, 0x00},
    {0x78, 0x21}, {0xFF, 0x01}, {0x23, 0x34}, {0x42, 0x00},
    {0x44, 0xFF}, {0x45, 0x26}, {0x46, 0x05}, {0x40, 0x40},
    {0x0E, 0x06}, {0x20, 0x1A}, {0x43, 0x40}, {0xFF, 0x00},
    {0x34, 0x03}, {0x35, 0x44}, {0xFF, 0x01}, {0x31, 0x04},
    {0x4B, 0x09}, {0x4C, 0x05}, {0x4D, 0x04}, {0xFF, 0x00},
    {0x44, 0x00}, {0x45, 0x20}, {0x47, 0x08}, {0x48, 0x28},
    {0x67, 0x00}, {0x70, 0x04}, {0x71, 0x01}, {0x72, 0xFE},
    {0x76, 0x00}, {0x77, 0x00}, {0xFF, 0x01}, {0x0D, 0x01},
    {0xFF, 0x00}, {0x80, 0x01}, {0x01, 0xF8}, {0xFF, 0x01},
    {0x8E, 0x01}, {0x00, 0x01}, {0xFF, 0x00}, {0x80, 0x00}
};

static uint8_t write_reg(uint8_t reg, uint8_t value)
{
    if (!bus_write_reg(BOARD_VL53L0X_I2C_ADDR, reg, value)) {
        vl53l0x_errors++;
        return 0U;
    }
    return 1U;
}

static uint8_t read_reg(uint8_t reg, uint8_t *value)
{
    if (value == 0 || !bus_read_regs(BOARD_VL53L0X_I2C_ADDR, reg, value, 1U)) {
        vl53l0x_errors++;
        return 0U;
    }
    return 1U;
}

static uint8_t read_regs(uint8_t reg, uint8_t *data, uint16_t length)
{
    uint16_t i;

    if (data == 0 || length == 0U) {
        return 0U;
    }

    for (i = 0U; i < length; ++i) {
        if (!bus_read_regs(BOARD_VL53L0X_I2C_ADDR, (uint8_t)(reg + i), &data[i], 1U)) {
            vl53l0x_errors++;
            return 0U;
        }
    }

    return 1U;
}

static uint8_t write_regs(uint8_t reg, const uint8_t *data, uint16_t length)
{
    uint16_t i;

    if (data == 0 || length == 0U) {
        return 0U;
    }

    for (i = 0U; i < length; ++i) {
        if (!bus_write_reg(BOARD_VL53L0X_I2C_ADDR, (uint8_t)(reg + i), data[i])) {
            vl53l0x_errors++;
            return 0U;
        }
    }

    return 1U;
}

static uint8_t write_u16(uint8_t reg, uint16_t value)
{
    uint8_t data[2];

    data[0] = (uint8_t)(value >> 8);
    data[1] = (uint8_t)value;
    return write_regs(reg, data, sizeof(data));
}

static uint8_t read_u16(uint8_t reg, uint16_t *value)
{
    uint8_t data[2];

    if (value == 0 || !read_regs(reg, data, sizeof(data))) {
        return 0U;
    }

    *value = (uint16_t)(((uint16_t)data[0] << 8U) | data[1]);
    return 1U;
}

static uint16_t decode_timeout(uint16_t reg_value)
{
    return (uint16_t)(((uint16_t)(reg_value & 0x00FFU) << (reg_value >> 8U)) + 1U);
}

static uint16_t encode_timeout(uint16_t timeout_mclks)
{
    uint32_t ls_byte;
    uint16_t ms_byte = 0U;

    if (timeout_mclks == 0U) {
        return 0U;
    }

    ls_byte = (uint32_t)timeout_mclks - 1U;
    while (ls_byte > 255U) {
        ls_byte >>= 1U;
        ms_byte++;
    }

    return (uint16_t)((ms_byte << 8U) | (uint16_t)(ls_byte & 0xFFU));
}

static uint32_t calc_macro_period_ns(uint8_t vcsel_period_pclks)
{
    return (uint32_t)(((uint32_t)2304U * vcsel_period_pclks * 1655U + 500U) / 1000U);
}

static uint8_t get_vcsel_period_pclks(uint8_t reg, uint8_t *period_pclks)
{
    uint8_t value;

    if (period_pclks == 0 || !read_reg(reg, &value)) {
        return 0U;
    }

    *period_pclks = (uint8_t)((value + 1U) << 1U);
    return 1U;
}

static uint32_t timeout_mclks_to_us(uint16_t timeout_mclks, uint8_t vcsel_period_pclks)
{
    return (uint32_t)(((uint32_t)timeout_mclks * calc_macro_period_ns(vcsel_period_pclks) + 500U) / 1000U);
}

static uint16_t timeout_us_to_mclks(uint32_t timeout_us, uint8_t vcsel_period_pclks)
{
    uint32_t macro_period_ns = calc_macro_period_ns(vcsel_period_pclks);
    return (uint16_t)(((timeout_us * 1000U) + (macro_period_ns / 2U)) / macro_period_ns);
}

static uint8_t get_sequence_step_enables(vl53l0x_sequence_enables_t *enables)
{
    uint8_t sequence_config;

    if (enables == 0 || !read_reg(VL53L0X_SYSTEM_SEQUENCE_CONFIG, &sequence_config)) {
        return 0U;
    }

    enables->tcc = (uint8_t)((sequence_config >> 4U) & 0x01U);
    enables->dss = (uint8_t)((sequence_config >> 3U) & 0x01U);
    enables->msrc = (uint8_t)((sequence_config >> 2U) & 0x01U);
    enables->pre_range = (uint8_t)((sequence_config >> 6U) & 0x01U);
    enables->final_range = (uint8_t)((sequence_config >> 7U) & 0x01U);
    return 1U;
}

static uint8_t get_sequence_step_timeouts(vl53l0x_sequence_timeouts_t *timeouts)
{
    uint8_t pre_range_vcsel_period;
    uint8_t final_range_vcsel_period;
    uint8_t msrc_raw;
    uint16_t timeout_reg;

    if (timeouts == 0 ||
        !get_vcsel_period_pclks(VL53L0X_PRE_RANGE_CONFIG_VCSEL_PERIOD, &pre_range_vcsel_period) ||
        !get_vcsel_period_pclks(VL53L0X_FINAL_RANGE_CONFIG_VCSEL_PERIOD, &final_range_vcsel_period) ||
        !read_reg(VL53L0X_MSRC_CONFIG_TIMEOUT_MACROP, &msrc_raw) ||
        !read_u16(VL53L0X_PRE_RANGE_CONFIG_TIMEOUT_MACROP_HI, &timeout_reg)) {
        return 0U;
    }

    timeouts->msrc_dss_tcc_us = timeout_mclks_to_us((uint16_t)msrc_raw + 1U, pre_range_vcsel_period);
    timeouts->pre_range_mclks = decode_timeout(timeout_reg);
    timeouts->pre_range_us = timeout_mclks_to_us(timeouts->pre_range_mclks, pre_range_vcsel_period);

    if (!read_u16(VL53L0X_FINAL_RANGE_CONFIG_TIMEOUT_MACROP_HI, &timeout_reg)) {
        return 0U;
    }

    timeouts->final_range_mclks = decode_timeout(timeout_reg);
    if (timeouts->final_range_mclks > timeouts->pre_range_mclks) {
        timeouts->final_range_mclks = (uint16_t)(timeouts->final_range_mclks - timeouts->pre_range_mclks);
    }

    (void)final_range_vcsel_period;
    return 1U;
}

static uint8_t set_measurement_timing_budget(uint32_t budget_us)
{
    const uint32_t start_overhead_us = 1910U;
    const uint32_t end_overhead_us = 960U;
    const uint32_t msrc_overhead_us = 660U;
    const uint32_t tcc_overhead_us = 590U;
    const uint32_t dss_overhead_us = 690U;
    const uint32_t pre_range_overhead_us = 660U;
    const uint32_t final_range_overhead_us = 550U;
    vl53l0x_sequence_enables_t enables;
    vl53l0x_sequence_timeouts_t timeouts;
    uint8_t final_range_vcsel_period;
    uint32_t used_budget_us;
    uint32_t final_range_timeout_us;
    uint16_t final_range_timeout_mclks;

    if (budget_us < 20000U ||
        !get_sequence_step_enables(&enables) ||
        !get_sequence_step_timeouts(&timeouts) ||
        !get_vcsel_period_pclks(VL53L0X_FINAL_RANGE_CONFIG_VCSEL_PERIOD, &final_range_vcsel_period)) {
        return 0U;
    }

    used_budget_us = start_overhead_us + end_overhead_us;
    if (enables.tcc) {
        used_budget_us += timeouts.msrc_dss_tcc_us + tcc_overhead_us;
    }
    if (enables.dss) {
        used_budget_us += (2U * (timeouts.msrc_dss_tcc_us + dss_overhead_us));
    } else if (enables.msrc) {
        used_budget_us += timeouts.msrc_dss_tcc_us + msrc_overhead_us;
    }
    if (enables.pre_range) {
        used_budget_us += timeouts.pre_range_us + pre_range_overhead_us;
    }

    if (!enables.final_range || used_budget_us + final_range_overhead_us >= budget_us) {
        return 0U;
    }

    final_range_timeout_us = budget_us - used_budget_us - final_range_overhead_us;
    final_range_timeout_mclks = timeout_us_to_mclks(final_range_timeout_us, final_range_vcsel_period);
    if (enables.pre_range) {
        final_range_timeout_mclks = (uint16_t)(final_range_timeout_mclks + timeouts.pre_range_mclks);
    }

    return write_u16(VL53L0X_FINAL_RANGE_CONFIG_TIMEOUT_MACROP_HI, encode_timeout(final_range_timeout_mclks));
}

static uint8_t get_spad_info(uint8_t *count, uint8_t *type_is_aperture)
{
    uint8_t tmp;
    uint8_t ready = 0U;

    if (!write_reg(0x80U, 0x01U) || !write_reg(0xFFU, 0x01U) || !write_reg(0x00U, 0x00U)) {
        return 0U;
    }
    if (!write_reg(0xFFU, 0x06U) || !read_reg(0x83U, &tmp) || !write_reg(0x83U, (uint8_t)(tmp | 0x04U))) {
        return 0U;
    }
    if (!write_reg(0xFFU, 0x07U) || !write_reg(0x81U, 0x01U) || !write_reg(0x80U, 0x01U) ||
        !write_reg(0x94U, 0x6BU) || !write_reg(0x83U, 0x00U)) {
        return 0U;
    }

    for (uint16_t timeout = 0U; timeout < 1000U; timeout++) {
        if (!read_reg(0x83U, &tmp)) {
            return 0U;
        }
        if (tmp != 0U) {
            ready = 1U;
            break;
        }
    }

    if (!ready) {
        vl53l0x_errors++;
        return 0U;
    }

    if (!write_reg(0x83U, 0x01U) || !read_reg(0x92U, &tmp)) {
        return 0U;
    }

    *count = (uint8_t)(tmp & 0x7FU);
    *type_is_aperture = (tmp >> 7) & 0x01U;

    if (!write_reg(0x81U, 0x00U) || !write_reg(0xFFU, 0x06U) || !read_reg(0x83U, &tmp) ||
        !write_reg(0x83U, (uint8_t)(tmp & (uint8_t)~0x04U)) || !write_reg(0xFFU, 0x01U) ||
        !write_reg(0x00U, 0x01U) || !write_reg(0xFFU, 0x00U) || !write_reg(0x80U, 0x00U)) {
        return 0U;
    }

    return 1U;
}

static uint8_t configure_spads(void)
{
    uint8_t spad_count;
    uint8_t spad_type_is_aperture;
    uint8_t ref_spad_map[6];
    uint8_t spads_enabled = 0U;
    uint8_t first_spad_to_enable;

    if (!get_spad_info(&spad_count, &spad_type_is_aperture)) {
        return 0U;
    }

    if (!read_regs(VL53L0X_GLOBAL_CONFIG_SPAD_ENABLES_REF_0, ref_spad_map, sizeof(ref_spad_map))) {
        return 0U;
    }

    if (!write_reg(0xFFU, 0x01U) ||
        !write_reg(VL53L0X_DYNAMIC_SPAD_REF_EN_START_OFFSET, 0x00U) ||
        !write_reg(VL53L0X_DYNAMIC_SPAD_NUM_REQUESTED_REF_SPAD, 0x2CU) ||
        !write_reg(0xFFU, 0x00U) ||
        !write_reg(VL53L0X_GLOBAL_CONFIG_REF_EN_START_SELECT, 0xB4U)) {
        return 0U;
    }

    first_spad_to_enable = spad_type_is_aperture ? 12U : 0U;
    for (uint8_t i = 0U; i < 48U; i++) {
        if (i < first_spad_to_enable || spads_enabled == spad_count) {
            ref_spad_map[i / 8U] &= (uint8_t)~(1U << (i % 8U));
        } else if ((ref_spad_map[i / 8U] >> (i % 8U)) & 0x01U) {
            spads_enabled++;
        }
    }

    return write_regs(VL53L0X_GLOBAL_CONFIG_SPAD_ENABLES_REF_0, ref_spad_map, sizeof(ref_spad_map));
}

static uint8_t apply_tuning_settings(void)
{
    for (uint16_t i = 0U; i < (uint16_t)(sizeof(vl53l0x_tuning_settings) / sizeof(vl53l0x_tuning_settings[0])); i++) {
        if (!write_reg(vl53l0x_tuning_settings[i].reg, vl53l0x_tuning_settings[i].value)) {
            return 0U;
        }
    }

    return 1U;
}

static uint8_t perform_ref_calibration(uint8_t vhv_init_byte)
{
    uint32_t started_ms;
    uint8_t status = 0U;

    if (!write_reg(VL53L0X_SYSRANGE_START, (uint8_t)(0x01U | vhv_init_byte))) {
        return 0U;
    }

    started_ms = mcu_port_millis();
    do {
        if (!read_reg(VL53L0X_RESULT_INTERRUPT_STATUS, &status)) {
            return 0U;
        }
        if ((status & 0x07U) != 0U) {
            break;
        }
    } while ((uint32_t)(mcu_port_millis() - started_ms) < VL53L0X_CALIBRATION_TIMEOUT_MS);

    if ((status & 0x07U) == 0U) {
        vl53l0x_errors++;
        return 0U;
    }

    return write_reg(VL53L0X_SYSTEM_INTERRUPT_CLEAR, 0x01U) && write_reg(VL53L0X_SYSRANGE_START, 0x00U);
}

uint8_t bsp_vl53l0x_begin(void)
{
    uint8_t value;

    vl53l0x_errors = 0U;
    vl53l0x_status_code = 1U;
    vl53l0x_model_id_value = 0U;
    vl53l0x_range_status_value = 0U;
    vl53l0x_raw_distance_mm = 0U;
    vl53l0x_measurement_pending = 0U;
    for (uint8_t i = 0U; i < (uint8_t)sizeof(vl53l0x_last_result); i++) {
        vl53l0x_last_result[i] = 0U;
    }

    if (!bus_init()) {
        vl53l0x_status_code = 10U;
        return 0U;
    }

    mcu_port_delay_ms(VL53L0X_INIT_DELAY_MS);

    if (!bus_is_ready(BOARD_VL53L0X_I2C_ADDR)) {
        vl53l0x_status_code = 11U;
        vl53l0x_errors++;
        return 0U;
    }

    if (!read_reg(0xC0U, &value)) {
        vl53l0x_status_code = 12U;
        return 0U;
    }
    vl53l0x_model_id_value = value;
    if (value != 0xEEU) {
        vl53l0x_status_code = 13U;
        return 0U;
    }
    if (!read_reg(VL53L0X_VHV_CONFIG_PAD_SCL_SDA_EXTSUP_HV, &value) ||
        !write_reg(VL53L0X_VHV_CONFIG_PAD_SCL_SDA_EXTSUP_HV, (uint8_t)(value | 0x01U))) {
        vl53l0x_status_code = 14U;
        return 0U;
    }

    vl53l0x_status_code = 20U;
    if (!write_reg(0x88U, 0x00U) || !write_reg(0x80U, 0x01U) || !write_reg(0xFFU, 0x01U) ||
        !write_reg(0x00U, 0x00U) || !read_reg(0x91U, &vl53l0x_stop_variable) ||
        !write_reg(0x00U, 0x01U) || !write_reg(0xFFU, 0x00U) || !write_reg(0x80U, 0x00U)) {
        return 0U;
    }

    vl53l0x_status_code = 21U;
    if (!read_reg(VL53L0X_MSRC_CONFIG_CONTROL, &value) ||
        !write_reg(VL53L0X_MSRC_CONFIG_CONTROL, (uint8_t)(value | 0x12U)) ||
        !write_u16(VL53L0X_FINAL_RANGE_CONFIG_MIN_COUNT_RATE_RTN, 13U) ||
        !write_reg(VL53L0X_SYSTEM_SEQUENCE_CONFIG, 0xFFU)) {
        return 0U;
    }

    vl53l0x_status_code = 22U;
    if (!configure_spads() || !apply_tuning_settings()) {
        return 0U;
    }

    vl53l0x_status_code = 23U;
    if (!write_reg(VL53L0X_SYSTEM_INTERRUPT_CONFIG_GPIO, 0x04U) ||
        !read_reg(VL53L0X_GPIO_HV_MUX_ACTIVE_HIGH, &value) ||
        !write_reg(VL53L0X_GPIO_HV_MUX_ACTIVE_HIGH, (uint8_t)(value & (uint8_t)~0x10U)) ||
        !write_reg(VL53L0X_SYSTEM_INTERRUPT_CLEAR, 0x01U)) {
        return 0U;
    }

    vl53l0x_status_code = 24U;
    if (!write_reg(VL53L0X_SYSTEM_SEQUENCE_CONFIG, 0xE8U) ||
        !set_measurement_timing_budget(VL53L0X_MEASUREMENT_BUDGET_US) ||
        !write_reg(VL53L0X_SYSTEM_SEQUENCE_CONFIG, 0x01U) || !perform_ref_calibration(0x40U) ||
        !write_reg(VL53L0X_SYSTEM_SEQUENCE_CONFIG, 0x02U) || !perform_ref_calibration(0x00U) ||
        !write_reg(VL53L0X_SYSTEM_SEQUENCE_CONFIG, 0xE8U)) {
        return 0U;
    }

    vl53l0x_status_code = 0U;
    return 1U;
}

uint8_t bsp_vl53l0x_start_measurement(void)
{
    uint32_t started_ms;
    uint8_t start = 0U;

    if (vl53l0x_measurement_pending) {
        return 1U;
    }

    if (!write_reg(0x80U, 0x01U) || !write_reg(0xFFU, 0x01U) || !write_reg(0x00U, 0x00U) ||
        !write_reg(0x91U, vl53l0x_stop_variable) || !write_reg(0x00U, 0x01U) ||
        !write_reg(0xFFU, 0x00U) || !write_reg(0x80U, 0x00U) ||
        !write_reg(VL53L0X_SYSRANGE_START, 0x01U)) {
        return 0U;
    }

    started_ms = mcu_port_millis();
    do {
        if (!read_reg(VL53L0X_SYSRANGE_START, &start)) {
            return 0U;
        }
        if ((start & 0x01U) == 0U) {
            break;
        }
    } while ((uint32_t)(mcu_port_millis() - started_ms) < 150U);

    if ((start & 0x01U) != 0U) {
        (void)write_reg(VL53L0X_SYSRANGE_START, 0x00U);
        vl53l0x_errors++;
        return 0U;
    }

    vl53l0x_measurement_pending = 1U;
    return 1U;
}

uint8_t bsp_vl53l0x_is_measurement_ready(void)
{
    uint8_t status;

    if (!vl53l0x_measurement_pending) {
        return 0U;
    }

    if (!read_reg(VL53L0X_RESULT_INTERRUPT_STATUS, &status)) {
        return 0U;
    }

    return ((status & 0x07U) != 0U) ? 1U : 0U;
}

uint8_t bsp_vl53l0x_read_distance_mm(uint16_t *distance_mm)
{
    uint8_t data[12];
    uint16_t distance;

    if (distance_mm == 0 || !vl53l0x_measurement_pending) {
        return 0U;
    }

    if (!bsp_vl53l0x_is_measurement_ready()) {
        return 0U;
    }

    if (!read_regs(VL53L0X_RESULT_RANGE_STATUS, data, sizeof(data)) ||
        !write_reg(VL53L0X_SYSTEM_INTERRUPT_CLEAR, 0x01U)) {
        return 0U;
    }

    for (uint8_t i = 0U; i < (uint8_t)sizeof(data); i++) {
        vl53l0x_last_result[i] = data[i];
    }

    vl53l0x_range_status_value = (uint8_t)((data[0] >> 3) & 0x0FU);
    distance = (uint16_t)(((uint16_t)data[10] << 8) | data[11]);
    vl53l0x_raw_distance_mm = distance;
    vl53l0x_measurement_pending = 0U;

    if (distance < 20U || distance >= 8190U) {
        return 0U;
    }

    *distance_mm = distance;
    return 1U;
}

uint32_t bsp_vl53l0x_error_count(void)
{
    return vl53l0x_errors;
}

uint8_t bsp_vl53l0x_status(void)
{
    return vl53l0x_status_code;
}

uint8_t bsp_vl53l0x_model_id(void)
{
    return vl53l0x_model_id_value;
}

uint8_t bsp_vl53l0x_range_status(void)
{
    return vl53l0x_range_status_value;
}

uint16_t bsp_vl53l0x_raw_distance_mm(void)
{
    return vl53l0x_raw_distance_mm;
}

uint8_t bsp_vl53l0x_probe_address(uint8_t address)
{
    if (!bus_init()) {
        return 0U;
    }

    return bus_is_ready(address);
}

void bsp_vl53l0x_reset_state(void)
{
    (void)write_reg(VL53L0X_SYSTEM_INTERRUPT_CLEAR, 0x01U);
    (void)write_reg(VL53L0X_SYSRANGE_START, 0x00U);
    vl53l0x_measurement_pending = 0U;
    vl53l0x_range_status_value = 0U;
    vl53l0x_raw_distance_mm = 0U;
}

uint8_t bsp_vl53l0x_last_result_byte(uint8_t index)
{
    if (index >= (uint8_t)sizeof(vl53l0x_last_result)) {
        return 0U;
    }

    return vl53l0x_last_result[index];
}


