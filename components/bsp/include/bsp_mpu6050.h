#pragma once

#include <stdint.h>

typedef struct {
    int16_t accel_x;
    int16_t accel_y;
    int16_t accel_z;
    int16_t temperature;
    int16_t gyro_x;
    int16_t gyro_y;
    int16_t gyro_z;
} bsp_mpu6050_raw_t;

typedef struct {
    int32_t accel_x_mg;
    int32_t accel_y_mg;
    int32_t accel_z_mg;
    int32_t temp_centi_c;
    int32_t gyro_x_mdps;
    int32_t gyro_y_mdps;
    int32_t gyro_z_mdps;
} bsp_mpu6050_scaled_t;

uint8_t bsp_mpu6050_init(void);
uint8_t bsp_mpu6050_read_raw(bsp_mpu6050_raw_t *sample);
uint8_t bsp_mpu6050_scale_raw(const bsp_mpu6050_raw_t *raw, bsp_mpu6050_scaled_t *sample);
uint8_t bsp_mpu6050_read_scaled(bsp_mpu6050_scaled_t *sample);
uint8_t bsp_mpu6050_whoami(uint8_t *whoami);
uint8_t bsp_mpu6050_is_address_ready(uint8_t address);
uint8_t bsp_mpu6050_debug_read_reg(uint8_t reg, uint8_t *value);
uint8_t bsp_mpu6050_debug_write_reg(uint8_t reg, uint8_t value);
