#include "bsp_mpu6050.h"
#include "mcu_port_time.h"
#include "vibe_api.h"
#include "vibe_runtime.h"

#include <stdio.h>

static uint8_t mpu_ready;
static int32_t gyro_x_bias;
static int32_t gyro_y_bias;
static int32_t gyro_z_bias;

static void print_reg8(const char *name, uint8_t reg)
{
    uint8_t value = 0U;
    char line[96];

    if (bsp_mpu6050_debug_read_reg(reg, &value)) {
        (void)snprintf(line, sizeof(line), "%s reg 0x%02X = 0x%02X", name, reg, value);
    } else {
        (void)snprintf(line, sizeof(line), "%s reg 0x%02X read failed", name, reg);
    }
    vibe_println(line);
}

static void print_mpu_diagnostics(void)
{
    uint8_t value = 0U;
    char line[96];

    vibe_println("MPU6050 fixed-address diagnostics start");
    vibe_println(bsp_mpu6050_is_address_ready(0x68U) ? "I2C address 0x68 ACK ok" : "I2C address 0x68 ACK failed");
    print_reg8("WHO_AM_I", 0x75U);
    print_reg8("FIFO_R_W", 0x74U);
    print_reg8("PWR_MGMT_1 before", 0x6BU);
    print_reg8("ACCEL_XOUT_H", 0x3BU);

    if (bsp_mpu6050_debug_write_reg(0x6BU, 0x00U)) {
        vibe_println("PWR_MGMT_1 write 0x00 ok");
    } else {
        vibe_println("PWR_MGMT_1 write 0x00 failed");
    }

    if (bsp_mpu6050_debug_read_reg(0x6BU, &value)) {
        (void)snprintf(line, sizeof(line), "PWR_MGMT_1 after reg 0x6B = 0x%02X", value);
        vibe_println(line);
    } else {
        vibe_println("PWR_MGMT_1 after read failed");
    }
    vibe_println("MPU6050 fixed-address diagnostics end");
}

static uint8_t calibrate_gyro_bias(void)
{
    enum { CALIBRATION_SAMPLES = 100 };
    int32_t sum_x = 0L;
    int32_t sum_y = 0L;
    int32_t sum_z = 0L;
    uint16_t count;
    bsp_mpu6050_raw_t raw;
    char line[128];

    vibe_println("Keep the MPU6050 still, calibrating gyro bias...");
    mcu_port_delay_ms(300U);

    for (count = 0U; count < CALIBRATION_SAMPLES; ++count) {
        if (!bsp_mpu6050_read_raw(&raw)) {
            vibe_println("Gyro bias calibration failed");
            return 0U;
        }

        sum_x += raw.gyro_x;
        sum_y += raw.gyro_y;
        sum_z += raw.gyro_z;
        mcu_port_delay_ms(5U);
    }

    gyro_x_bias = sum_x / CALIBRATION_SAMPLES;
    gyro_y_bias = sum_y / CALIBRATION_SAMPLES;
    gyro_z_bias = sum_z / CALIBRATION_SAMPLES;

    (void)snprintf(line, sizeof(line), "Gyro bias raw gx=%ld gy=%ld gz=%ld",
                   (long)gyro_x_bias, (long)gyro_y_bias, (long)gyro_z_bias);
    vibe_println(line);
    return 1U;
}

static void print_mpu_task(void)
{
    bsp_mpu6050_raw_t raw;
    bsp_mpu6050_scaled_t scaled;
    int32_t gyro_x_mdps;
    int32_t gyro_y_mdps;
    int32_t gyro_z_mdps;
    char line[192];

    if (!mpu_ready) {
        vibe_println("MPU6050 not ready");
        return;
    }

    if (!bsp_mpu6050_read_raw(&raw) || !bsp_mpu6050_scale_raw(&raw, &scaled)) {
        vibe_println("MPU6050 read failed");
        return;
    }

    gyro_x_mdps = ((int32_t)raw.gyro_x - gyro_x_bias) * 1000L / 131L;
    gyro_y_mdps = ((int32_t)raw.gyro_y - gyro_y_bias) * 1000L / 131L;
    gyro_z_mdps = ((int32_t)raw.gyro_z - gyro_z_bias) * 1000L / 131L;

    (void)snprintf(line, sizeof(line),
                   "raw ax=%d ay=%d az=%d temp=%d gx=%d gy=%d gz=%d",
                   raw.accel_x, raw.accel_y, raw.accel_z, raw.temperature,
                   raw.gyro_x, raw.gyro_y, raw.gyro_z);
    vibe_println(line);

    (void)snprintf(line, sizeof(line),
                   "scaled ax=%ldmg ay=%ldmg az=%ldmg temp=%ld.%02ldC gx=%ldmdps gy=%ldmdps gz=%ldmdps",
                   (long)scaled.accel_x_mg,
                   (long)scaled.accel_y_mg,
                   (long)scaled.accel_z_mg,
                   (long)(scaled.temp_centi_c / 100L),
                   (long)(scaled.temp_centi_c < 0 ? -(scaled.temp_centi_c % 100L) : (scaled.temp_centi_c % 100L)),
                   (long)gyro_x_mdps,
                   (long)gyro_y_mdps,
                   (long)gyro_z_mdps);
    vibe_println(line);
}

void app_setup(void)
{
    uint8_t whoami = 0U;
    char line[64];

    vibe_serial_begin(115200U);
    mcu_port_delay_ms(300U);
    vibe_println("MOCE SDK CH32V203G6U6 MPU6050 readout");
    vibe_println("I2C1: SCL=PB6 SDA=PB7, MPU6050 address=0x68");

    print_mpu_diagnostics();

    mpu_ready = bsp_mpu6050_init();
    if (bsp_mpu6050_whoami(&whoami)) {
        (void)snprintf(line, sizeof(line), "MPU6050 WHO_AM_I=0x%02X", whoami);
        vibe_println(line);
        if (whoami == 0x74U) {
            vibe_println("MPU6050 compatible ID 0x74 accepted");
        }
    } else {
        vibe_println("MPU6050 WHO_AM_I read failed");
    }

    vibe_println(mpu_ready ? "MPU6050 init ok" : "MPU6050 init failed");
    if (mpu_ready) {
        (void)calibrate_gyro_bias();
    }
    (void)vibe_task_every_ms(500U, print_mpu_task);
}
