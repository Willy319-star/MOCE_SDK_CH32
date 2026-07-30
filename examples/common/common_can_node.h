#pragma once

#include "bsp_can.h"
#include <stdint.h>

#define DEVICE_TYPE_I2C      0x01U
#define DEVICE_TYPE_MOTOR    0x02U
#define DEVICE_TYPE_SERVO    0x03U

#define CAN_ID_STATUS(node_id)     ((uint16_t)(0x100U + (uint16_t)(node_id)))
#define CAN_ID_I2C_CMD(node_id)    ((uint16_t)(0x200U + (uint16_t)(node_id)))
#define CAN_ID_MOTOR_CMD(node_id)  ((uint16_t)(0x300U + (uint16_t)(node_id)))
#define CAN_ID_SERVO_CMD(node_id)  ((uint16_t)(0x400U + (uint16_t)(node_id)))
#define CAN_ID_ACK(node_id)        ((uint16_t)(0x500U + (uint16_t)(node_id)))
#define CAN_ID_HELLO(node_id)      ((uint16_t)(0x700U + (uint16_t)(node_id)))

static inline void node_put_u16_le(uint8_t *data, uint8_t off, uint16_t value)
{
    data[off] = (uint8_t)(value & 0xFFU);
    data[(uint8_t)(off + 1U)] = (uint8_t)((value >> 8U) & 0xFFU);
}

static inline uint16_t node_get_u16_le(const uint8_t *data, uint8_t off)
{
    return (uint16_t)((uint16_t)data[off] | ((uint16_t)data[(uint8_t)(off + 1U)] << 8U));
}

static inline uint8_t node_send_hello(uint8_t node_id,
                                      uint8_t device_type,
                                      uint8_t fw_version,
                                      uint8_t capability_flags)
{
    uint8_t data[8] = {0};

    data[0] = device_type;
    data[1] = node_id;
    data[2] = fw_version;
    data[3] = capability_flags;
    return bsp_can_send_std(CAN_ID_HELLO(node_id), data, sizeof(data));
}

static inline uint8_t node_send_ack(uint8_t node_id,
                                    uint8_t device_type,
                                    uint8_t command_type,
                                    uint8_t result)
{
    uint8_t data[8] = {0};

    data[0] = command_type;
    data[1] = result;
    data[2] = node_id;
    data[3] = device_type;
    return bsp_can_send_std(CAN_ID_ACK(node_id), data, sizeof(data));
}
