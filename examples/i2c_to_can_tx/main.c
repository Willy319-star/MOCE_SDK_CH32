#include "bsp_can.h"
#include "bsp_mpu6050.h"
#include "mcu_port_time.h"
#include "vibe_api.h"
#include "vibe_runtime.h"

#include <stdio.h>

#define CAN_ID_MPU_ACCEL 0x350U
#define CAN_ID_MPU_GYRO  0x351U
#define CAN_ID_MPU_TEMP  0x352U

static uint8_t mpu_ready;
static uint8_t can_ready;
static uint16_t packet_count;

static void put_u16_le(uint8_t *data, uint8_t offset, uint16_t value)
{
    data[offset] = (uint8_t)(value & 0xFFU);
    data[(uint8_t)(offset + 1U)] = (uint8_t)((value >> 8U) & 0xFFU);
}

static void put_i16_le(uint8_t *data, uint8_t offset, int16_t value)
{
    put_u16_le(data, offset, (uint16_t)value);
}

static void print_can_status(void)
{
    bsp_can_status_t status;
    char line[128];

    bsp_can_get_status(&status);
    (void)snprintf(line, sizeof(line),
                   "CAN status tx_err=%u rx_err=%u lec=0x%02X ew=%u ep=%u bo=%u init_stage=%u rx_sample=%u",
                   status.tx_error_count,
                   status.rx_error_count,
                   status.last_error_code,
                   status.error_warning,
                   status.error_passive,
                   status.bus_off,
                   status.init_stage,
                   status.rx_sample);
    vibe_println(line);
}

static uint8_t send_frame(uint16_t can_id, const uint8_t *data)
{
    char line[144];
    uint8_t ok = bsp_can_send_std(can_id, data, 8U);

    (void)snprintf(line, sizeof(line),
                   "%s id=0x%03X data=%02X %02X %02X %02X %02X %02X %02X %02X",
                   ok ? "CAN TX ok" : "CAN TX failed",
                   can_id,
                   data[0], data[1], data[2], data[3],
                   data[4], data[5], data[6], data[7]);
    vibe_println(line);

    return ok;
}

static void i2c_to_can_task(void)
{
    bsp_mpu6050_raw_t raw;
    uint8_t accel_frame[8];
    uint8_t gyro_frame[8];
    uint8_t temp_frame[8];
    char line[160];

    if (!mpu_ready) {
        vibe_println("I2C sensor not ready: MPU6050 init failed");
        return;
    }

    if (!can_ready) {
        vibe_println("CAN not ready");
        print_can_status();
        return;
    }

    if (!bsp_mpu6050_read_raw(&raw)) {
        vibe_println("I2C read failed: MPU6050 raw sample not available");
        return;
    }

    put_u16_le(accel_frame, 0U, packet_count);
    put_i16_le(accel_frame, 2U, raw.accel_x);
    put_i16_le(accel_frame, 4U, raw.accel_y);
    put_i16_le(accel_frame, 6U, raw.accel_z);

    put_u16_le(gyro_frame, 0U, packet_count);
    put_i16_le(gyro_frame, 2U, raw.gyro_x);
    put_i16_le(gyro_frame, 4U, raw.gyro_y);
    put_i16_le(gyro_frame, 6U, raw.gyro_z);

    put_u16_le(temp_frame, 0U, packet_count);
    put_i16_le(temp_frame, 2U, raw.temperature);
    temp_frame[4] = 0x49U;
    temp_frame[5] = 0x32U;
    temp_frame[6] = 0x43U;
    temp_frame[7] = 0x41U;

    (void)snprintf(line, sizeof(line),
                   "I2C MPU6050 count=%u ax=%d ay=%d az=%d temp=%d gx=%d gy=%d gz=%d",
                   packet_count,
                   raw.accel_x,
                   raw.accel_y,
                   raw.accel_z,
                   raw.temperature,
                   raw.gyro_x,
                   raw.gyro_y,
                   raw.gyro_z);
    vibe_println(line);

    (void)send_frame(CAN_ID_MPU_ACCEL, accel_frame);
    (void)send_frame(CAN_ID_MPU_GYRO, gyro_frame);
    (void)send_frame(CAN_ID_MPU_TEMP, temp_frame);

    ++packet_count;
    vibe_led_toggle();
}

void app_setup(void)
{
    uint8_t whoami = 0U;
    char line[96];

    vibe_serial_begin(115200U);
    mcu_port_delay_ms(300U);
    vibe_println("MOCE SDK CH32V203G6U6 I2C to CAN TX");
    vibe_println("I2C1: MPU6050 SCL=PB6 SDA=PB7 addr=0x68");
    vibe_println("CAN1: TX=PA12 RX=PA11 bitrate=50k");
    vibe_println("CAN frames: 0x350 accel, 0x351 gyro, 0x352 temp/tag");

    mpu_ready = bsp_mpu6050_init();
    if (bsp_mpu6050_whoami(&whoami)) {
        (void)snprintf(line, sizeof(line), "MPU6050 WHO_AM_I=0x%02X", whoami);
        vibe_println(line);
    } else {
        vibe_println("MPU6050 WHO_AM_I read failed");
    }
    vibe_println(mpu_ready ? "MPU6050 init ok" : "MPU6050 init failed");

    can_ready = bsp_can_init_50k();
    vibe_println(can_ready ? "CAN init ok" : "CAN init failed");
    if (!can_ready) {
        print_can_status();
    }

    (void)vibe_task_every_ms(500U, i2c_to_can_task);
}
