#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint8_t config_before;
    uint8_t config_after;
    uint8_t fault_status;
    uint16_t rtd_raw;
} bsp_max31865_status_t;

typedef struct {
    uint8_t mode0_before;
    uint8_t mode0_after;
    uint8_t mode1_before;
    uint8_t mode1_after;
    uint8_t mode2_before;
    uint8_t mode2_after;
    uint8_t mode3_before;
    uint8_t mode3_after;
} bsp_max31865_probe_t;

uint8_t bsp_max31865_init(void);
uint8_t bsp_max31865_is_ready(void);
uint8_t bsp_max31865_active_mode(void);
uint8_t bsp_max31865_probe(bsp_max31865_probe_t *probe);
uint8_t bsp_max31865_read_reg(uint8_t reg, uint8_t *value);
uint8_t bsp_max31865_write_reg(uint8_t reg, uint8_t value);
uint8_t bsp_max31865_read_status(bsp_max31865_status_t *status);

#ifdef __cplusplus
}
#endif
