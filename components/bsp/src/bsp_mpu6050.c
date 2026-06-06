#include "bsp_mpu6050.h"
#include "board_pins.h"
#include "mcu_port_i2c.h"
#include "mcu_port_time.h"

#define MPU6050_ADDR         0x68U
#define MPU6050_REG_SMPLRT   0x19U
#define MPU6050_REG_CONFIG   0x1AU
#define MPU6050_REG_GYRO_CFG 0x1BU
#define MPU6050_REG_ACCEL_CFG 0x1CU
#define MPU6050_REG_ACCEL_XH 0x3BU
#define MPU6050_REG_PWR_MGMT1 0x6BU
#define MPU6050_REG_WHO_AM_I 0x75U
#define MPU6050_WHO_AM_I_ID    0x68U
#define MPU6050_COMPAT_ID_0X74 0x74U

static mcu_port_i2c_t mpu_i2c = {
    .i2c = BOARD_MPU6050_I2C_INSTANCE,
    .scl_port = BOARD_MPU6050_SCL_GPIO_PORT,
    .scl_pin = BOARD_MPU6050_SCL_GPIO_PIN,
    .sda_port = BOARD_MPU6050_SDA_GPIO_PORT,
    .sda_pin = BOARD_MPU6050_SDA_GPIO_PIN,
    .gpio_clock = BOARD_MPU6050_I2C_GPIO_CLK,
    .i2c_clock = BOARD_MPU6050_I2C_CLK,
    .speed_hz = BOARD_MPU6050_I2C_SPEED_HZ,
    .initialized = 0U,
};

static int16_t read_i16_be(const uint8_t *data)
{
    return (int16_t)(((uint16_t)data[0] << 8) | data[1]);
}

static uint8_t is_supported_whoami(uint8_t whoami)
{
    return (whoami == MPU6050_WHO_AM_I_ID) || (whoami == MPU6050_COMPAT_ID_0X74);
}

uint8_t bsp_mpu6050_whoami(uint8_t *whoami)
{
    if (whoami == 0) {
        return 0U;
    }

    return mcu_port_i2c_read_reg(&mpu_i2c, MPU6050_ADDR, MPU6050_REG_WHO_AM_I, whoami);
}

uint8_t bsp_mpu6050_is_address_ready(uint8_t address)
{
    if (!mpu_i2c.initialized && !mcu_port_i2c_init(&mpu_i2c)) {
        return 0U;
    }

    return mcu_port_i2c_is_ready(&mpu_i2c, address);
}

uint8_t bsp_mpu6050_debug_read_reg(uint8_t reg, uint8_t *value)
{
    if (value == 0) {
        return 0U;
    }

    if (!mpu_i2c.initialized && !mcu_port_i2c_init(&mpu_i2c)) {
        return 0U;
    }

    return mcu_port_i2c_read_reg(&mpu_i2c, MPU6050_ADDR, reg, value);
}

uint8_t bsp_mpu6050_debug_write_reg(uint8_t reg, uint8_t value)
{
    if (!mpu_i2c.initialized && !mcu_port_i2c_init(&mpu_i2c)) {
        return 0U;
    }

    return mcu_port_i2c_write_reg(&mpu_i2c, MPU6050_ADDR, reg, value);
}

uint8_t bsp_mpu6050_init(void)
{
    uint8_t whoami = 0U;

    if (!mcu_port_i2c_init(&mpu_i2c)) {
        return 0U;
    }

    if (!bsp_mpu6050_whoami(&whoami) || !is_supported_whoami(whoami)) {
        return 0U;
    }

    if (!mcu_port_i2c_write_reg(&mpu_i2c, MPU6050_ADDR, MPU6050_REG_PWR_MGMT1, 0x00U)) {
        return 0U;
    }
    mcu_port_delay_ms(500U);

    (void)mcu_port_i2c_write_reg(&mpu_i2c, MPU6050_ADDR, MPU6050_REG_SMPLRT, 0x07U);
    (void)mcu_port_i2c_write_reg(&mpu_i2c, MPU6050_ADDR, MPU6050_REG_CONFIG, 0x03U);
    (void)mcu_port_i2c_write_reg(&mpu_i2c, MPU6050_ADDR, MPU6050_REG_GYRO_CFG, 0x00U);
    (void)mcu_port_i2c_write_reg(&mpu_i2c, MPU6050_ADDR, MPU6050_REG_ACCEL_CFG, 0x00U);

    return 1U;
}

uint8_t bsp_mpu6050_read_raw(bsp_mpu6050_raw_t *sample)
{
    uint8_t data[14];
    uint8_t i;

    if (sample == 0) {
        return 0U;
    }

    for (i = 0U; i < (uint8_t)sizeof(data); ++i) {
        if (!mcu_port_i2c_read_reg(&mpu_i2c, MPU6050_ADDR, (uint8_t)(MPU6050_REG_ACCEL_XH + i), &data[i])) {
            return 0U;
        }
    }

    sample->accel_x = read_i16_be(&data[0]);
    sample->accel_y = read_i16_be(&data[2]);
    sample->accel_z = read_i16_be(&data[4]);
    sample->temperature = read_i16_be(&data[6]);
    sample->gyro_x = read_i16_be(&data[8]);
    sample->gyro_y = read_i16_be(&data[10]);
    sample->gyro_z = read_i16_be(&data[12]);

    return 1U;
}

uint8_t bsp_mpu6050_scale_raw(const bsp_mpu6050_raw_t *raw, bsp_mpu6050_scaled_t *sample)
{
    if (raw == 0 || sample == 0) {
        return 0U;
    }

    sample->accel_x_mg = ((int32_t)raw->accel_x * 1000L) / 16384L;
    sample->accel_y_mg = ((int32_t)raw->accel_y * 1000L) / 16384L;
    sample->accel_z_mg = ((int32_t)raw->accel_z * 1000L) / 16384L;
    sample->temp_centi_c = (((int32_t)raw->temperature * 100L) / 340L) + 3653L;
    sample->gyro_x_mdps = ((int32_t)raw->gyro_x * 1000L) / 131L;
    sample->gyro_y_mdps = ((int32_t)raw->gyro_y * 1000L) / 131L;
    sample->gyro_z_mdps = ((int32_t)raw->gyro_z * 1000L) / 131L;

    return 1U;
}

uint8_t bsp_mpu6050_read_scaled(bsp_mpu6050_scaled_t *sample)
{
    bsp_mpu6050_raw_t raw;

    if (sample == 0 || !bsp_mpu6050_read_raw(&raw)) {
        return 0U;
    }

    return bsp_mpu6050_scale_raw(&raw, sample);
}
