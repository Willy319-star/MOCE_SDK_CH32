#pragma once

#include <stdint.h>

#define BSP_CAN_MAX_DATA_LEN 8U

typedef struct {
    uint32_t id;
    uint8_t dlc;
    uint8_t data[BSP_CAN_MAX_DATA_LEN];
} bsp_can_frame_t;

typedef struct {
    uint8_t tx_error_count;
    uint8_t rx_error_count;
    uint8_t last_error_code;
    uint8_t error_warning;
    uint8_t error_passive;
    uint8_t bus_off;
    uint8_t init_stage;
    uint8_t rx_sample;
} bsp_can_status_t;

typedef enum {
    BSP_CAN_BITRATE_50K = 50000U,
    BSP_CAN_BITRATE_500K = 500000U,
} bsp_can_bitrate_t;

uint8_t bsp_can_init(bsp_can_bitrate_t bitrate);
uint8_t bsp_can_init_50k(void);
uint8_t bsp_can_send_std(uint16_t id, const uint8_t *data, uint8_t len);
uint8_t bsp_can_receive(bsp_can_frame_t *frame);
void bsp_can_get_status(bsp_can_status_t *status);
