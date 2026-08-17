#pragma once

#include "bsp_can.h"
#include <stdint.h>
#include <string.h>

/* ———— device types ———— */
#define DEVICE_TYPE_I2C      0x01U
#define DEVICE_TYPE_SPI      0x03U
#define DEVICE_TYPE_UART     0x04U

/* ———— CAN ID allocation ———— */
#define CAN_ID_DISCOVERY         0x000U
#define CAN_ID_STATUS(node_id)   ((uint16_t)(0x100U + (uint16_t)(node_id)))
#define CAN_ID_CMD(node_id)      ((uint16_t)(0x200U + (uint16_t)(node_id)))
#define CAN_ID_DATA(node_id)     ((uint16_t)(0x300U + (uint16_t)(node_id)))
#define CAN_ID_ACK(node_id)      ((uint16_t)(0x500U + (uint16_t)(node_id)))
#define CAN_ID_HELLO(node_id)    ((uint16_t)(0x700U + (uint16_t)(node_id)))

/* convenience aliases (existing code may use these) */
#define CAN_ID_I2C_CMD(node_id)      CAN_ID_CMD(node_id)

/* ———— discovery protocol ———— */
#define DYN_CMD_REQUEST_ID      0xF0U
#define DYN_CMD_ASSIGN_ID       0xF1U
#define DYN_CMD_ID_ACK          0xF2U
#define DYN_CMD_RELEASE_ID      0xF3U
#define DYN_MAGIC0              0xAAU
#define DYN_MAGIC1              0x55U
#define ID_ACK_REPEAT_COUNT     3U

/* ———— node id range ———— */
#define NODE_ID_UNASSIGNED      0U
#define NODE_ID_MIN             1U
#define NODE_ID_MAX             127U

/* ———— CH32 unique id ———— */
#define CH32_UID_BASE           0x1FFFF7E8UL
#define CH32_UID_BYTES          12U

/* ———————————————————————————————————————————————— */
/*  static inline helpers                              */
/* ———————————————————————————————————————————————— */

static inline void node_put_u16_le(uint8_t *data, uint8_t off, uint16_t value)
{
    data[off] = (uint8_t)(value & 0xFFU);
    data[(uint8_t)(off + 1U)] = (uint8_t)((value >> 8U) & 0xFFU);
}

static inline uint16_t node_get_u16_le(const uint8_t *data, uint8_t off)
{
    return (uint16_t)((uint16_t)data[off] | ((uint16_t)data[(uint8_t)(off + 1U)] << 8U));
}

static inline uint8_t node_is_valid_id(uint8_t node_id)
{
    return (node_id >= NODE_ID_MIN && node_id <= NODE_ID_MAX) ? 1U : 0U;
}

/* CRC16-CCITT (0xFFFF init, 0x1021 poly, no xor-out), same as "CRC16_CCITT_FALSE" */
static inline uint16_t node_crc16_ccitt(const uint8_t *data, uint16_t len)
{
    uint16_t crc = 0xFFFFU;
    uint16_t i;
    uint8_t bit;
    for (i = 0U; i < len; ++i) {
        crc ^= (uint16_t)data[i] << 8U;
        for (bit = 0U; bit < 8U; ++bit)
            crc = (crc & 0x8000U) ? (uint16_t)((crc << 1U) ^ 0x1021U) : (uint16_t)(crc << 1U);
    }
    return crc;
}

/* Read a reproducible 16-bit token from the CH32 MCU unique ID + device type. */
static inline uint16_t node_read_token(uint8_t device_type)
{
    const volatile uint8_t *uid = (const volatile uint8_t *)CH32_UID_BASE;
    uint8_t identity[13];
    uint8_t i;
    uint16_t token;

    for (i = 0U; i < CH32_UID_BYTES; ++i) identity[i] = uid[i];
    identity[CH32_UID_BYTES] = device_type;
    token = node_crc16_ccitt(identity, (uint16_t)sizeof(identity));
    return (token == 0U || token == 0xFFFFU) ? 0x4932U : token;
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

/* Send a discovery-frame payload on the given CAN ID.
   [0]=cmd [1]=device_type [2]=fw_ver [3]=caps [4]=seq [5-6]=token_le [7]=node_id */
static inline void node_send_discovery(uint16_t can_id,
                                       uint8_t cmd,
                                       uint8_t device_type,
                                       uint8_t fw_version,
                                       uint8_t caps,
                                       uint8_t seq,
                                       uint16_t token,
                                       uint8_t node_id)
{
    uint8_t d[8] = {0};
    d[0] = cmd;
    d[1] = device_type;
    d[2] = fw_version;
    d[3] = caps;
    d[4] = seq;
    node_put_u16_le(d, 5U, token);
    d[7] = node_id;
    (void)bsp_can_send_std(can_id, d, sizeof(d));
}

/* Send a REQUEST_ID discovery frame. */
static inline void node_send_id_request(uint8_t device_type,
                                        uint8_t fw_version,
                                        uint8_t caps,
                                        uint8_t *seq,
                                        uint16_t token)
{
    (*seq)++;
    node_send_discovery(CAN_ID_DISCOVERY, DYN_CMD_REQUEST_ID,
                        device_type, fw_version, caps, *seq, token, 0U);
}

/* Send ID_ACK burst after assignment. */
static inline void node_send_id_ack(uint8_t node_id,
                                    uint8_t device_type,
                                    uint8_t fw_version,
                                    uint16_t token)
{
    uint8_t d[8] = {0};
    uint8_t i;
    d[0] = DYN_CMD_ID_ACK;
    d[1] = node_id;
    d[2] = DYN_MAGIC0;
    d[3] = DYN_MAGIC1;
    node_put_u16_le(d, 4U, token);
    d[6] = device_type;
    d[7] = fw_version;
    for (i = 0U; i < ID_ACK_REPEAT_COUNT; ++i)
        (void)bsp_can_send_std(CAN_ID_DISCOVERY, d, sizeof(d));
}

/* Handle a discovery frame on CAN_ID_DISCOVERY.
   Returns 0 if frame was not consumed (not a discovery frame).
   Returns 1 if consumed — caller should check *id_changed. */
static inline uint8_t node_handle_discovery(const bsp_can_frame_t *f,
                                            uint8_t device_type,
                                            uint8_t fw_version,
                                            uint8_t caps,
                                            uint8_t protocol_version,
                                            uint16_t token,
                                            uint8_t *my_node_id,
                                            uint8_t *id_assigned,
                                            uint8_t *seq)
{
    uint8_t new_id;

    if (f == 0 || f->dlc < 1U) return 1U;

    switch (f->data[0]) {

    case DYN_CMD_ASSIGN_ID:
        if (f->dlc < 8U) return 1U;
        if (f->data[2] != DYN_MAGIC0 || f->data[3] != DYN_MAGIC1) return 1U;
        if (f->data[6] != device_type || f->data[7] != protocol_version) return 1U;
        if (node_get_u16_le(f->data, 4U) != token) return 1U;
        new_id = f->data[1];
        if (!node_is_valid_id(new_id)) return 1U;
        *my_node_id = new_id;
        *id_assigned = 1U;
        node_send_id_ack(new_id, device_type, fw_version, token);
        (void)node_send_hello(new_id, device_type, fw_version, caps);
        return 1U;

    case DYN_CMD_RELEASE_ID:
        if (f->dlc >= 3U && f->data[1] == DYN_MAGIC0 && f->data[2] == DYN_MAGIC1) {
            *my_node_id = NODE_ID_UNASSIGNED;
            *id_assigned = 0U;
            node_send_id_request(device_type, fw_version, caps, seq, token);
        }
        return 1U;

    case DYN_CMD_REQUEST_ID:
        if (*id_assigned) {
            node_send_id_ack(*my_node_id, device_type, fw_version, token);
            (void)node_send_hello(*my_node_id, device_type, fw_version, caps);
        } else {
            node_send_id_request(device_type, fw_version, caps, seq, token);
        }
        return 1U;

    default:
        return 1U;
    }
}
