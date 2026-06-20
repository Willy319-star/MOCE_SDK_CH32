#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int32_t temperature_centi_c;
    uint32_t pressure_pa;
    uint32_t humidity_centi_rh;
} bsp_bme280_sample_t;

typedef struct {
    uint8_t normal_mode0_id;
    uint8_t normal_mode3_id;
    uint8_t swapped_mode0_id;
    uint8_t swapped_mode3_id;
} bsp_bme280_debug_t;

uint8_t bsp_bme280_init(void);
uint8_t bsp_bme280_chip_id(uint8_t *chip_id);
uint8_t bsp_bme280_init_stage(void);
uint8_t bsp_bme280_debug_probe(bsp_bme280_debug_t *debug);
uint8_t bsp_bme280_read_reg(uint8_t reg, uint8_t *value);
uint8_t bsp_bme280_read_sample(bsp_bme280_sample_t *sample);

#ifdef __cplusplus
}
#endif
