#pragma once

#include <stdint.h>

uint8_t bsp_vl53l0x_begin(void);
uint8_t bsp_vl53l0x_start_measurement(void);
uint8_t bsp_vl53l0x_is_measurement_ready(void);
uint8_t bsp_vl53l0x_read_distance_mm(uint16_t *distance_mm);
uint32_t bsp_vl53l0x_error_count(void);
uint8_t bsp_vl53l0x_status(void);
uint8_t bsp_vl53l0x_model_id(void);
uint8_t bsp_vl53l0x_range_status(void);
uint16_t bsp_vl53l0x_raw_distance_mm(void);
uint8_t bsp_vl53l0x_probe_address(uint8_t address);
void bsp_vl53l0x_reset_state(void);
uint8_t bsp_vl53l0x_last_result_byte(uint8_t index);
