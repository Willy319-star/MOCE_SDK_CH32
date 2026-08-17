#include "common_can_node.h"
#include "board_pins.h"
#include "mcu_port_i2c.h"
#include "mcu_port_time.h"
#include "vibe_runtime.h"
#include "ch32v20x_can.h"

#include <stdint.h>
#include <string.h>

#define DEVICE_TYPE        DEVICE_TYPE_I2C
#define FW_VERSION              5U
#define NODE_PROTOCOL_VERSION_V1 1U
#define NODE_PROTOCOL_VERSION_V2 2U

#define CAP_SCAN           0x01U
#define CAP_PROBE          0x02U
#define CAP_REG_IO         0x04U
#define CAP_RAW_WRITE      0x08U
#define CAP_CHUNK_READ     0x10U
#define CAP_WRITE_READ     0x20U
#define CAP_SET_SPEED      0x40U
#define CAP_DYNAMIC_NODE   0x80U
#define CAPABILITY_FLAGS   (CAP_SCAN | CAP_PROBE | CAP_REG_IO | CAP_RAW_WRITE | CAP_CHUNK_READ | CAP_WRITE_READ | CAP_SET_SPEED | CAP_DYNAMIC_NODE)

#define HEARTBEAT_MS       1000U
#define CAN_POLL_MS        1U
#define DISCOVERY_SERVICE_MS 1U
#define DISCOVERY_SLOT_MS    2U
#define DISCOVERY_SLOT_COUNT 32U
#define ID_ACK_REPEAT_GAP_MS 5U

#define NODE_ID_UNASSIGNED 0U
#define NODE_ID_MIN        1U
#define NODE_ID_MAX        127U

#define CAN_ID_DISCOVERY   0x000U

#define DYN_CMD_REQUEST_ID 0xF0U
#define DYN_CMD_ASSIGN_ID  0xF1U
#define DYN_CMD_ID_ACK     0xF2U
#define DYN_CMD_RELEASE_ID 0xF3U
#define DYN_MAGIC0         0xAAU
#define DYN_MAGIC1         0x55U
#define DYN_RELEASE_BROADCAST_MARKER 0xA5U

#define CH32_UID_BASE      0x1FFFF7E8UL

/* Legacy commands kept for compatibility after node assignment. */
#define I2C_CMD_SCAN        0x01U
#define I2C_CMD_PROBE       0x02U
#define I2C_CMD_WRITE_REG   0x03U
#define I2C_CMD_READ_REGS   0x04U

/* Generic transport extensions. */
#define I2C_CMD_WRITE_RAW   0x05U
#define I2C_CMD_WRITE_READ  0x06U
#define I2C_CMD_SET_SPEED   0x07U
#define I2C_CMD_WRITE_MULTI  0x08U

#define I2C_STATUS_SCAN        0x01U
#define I2C_STATUS_PROBE       0x02U
#define I2C_STATUS_WRITE       0x03U
#define I2C_STATUS_READ        0x04U
#define I2C_STATUS_READ_CHUNK  0x05U
#define I2C_STATUS_READ_DONE   0x06U
#define I2C_STATUS_RAW_WRITE   0x07U
#define I2C_STATUS_SPEED       0x08U
#define I2C_STATUS_WRITE_MULTI 0x09U
#define I2C_STATUS_DIAG        0x7EU

#define DIAG_BOOT              0x01U
#define DIAG_RX_CMD            0x02U
#define DIAG_SCAN_START        0x03U
#define DIAG_SCAN_DONE         0x04U
#define DIAG_UNKNOWN_CMD       0x05U
#define DIAG_ASSIGN_DONE       0x06U
#define DIAG_I2C_INIT          0x07U
#define SCAN_DIAG_MARKER       0xD1U

#define I2C_MAX_WRITE_PAYLOAD  5U
#define I2C_MAX_REG_PAYLOAD    4U
#define I2C_LEGACY_READ_MAX    3U
#define I2C_MAX_READ_LEN       32U
#define I2C_READ_CHUNK_LEN     4U
#define I2C_MULTI_CHUNK_MAX    4U
#define I2C_MULTI_BUFFER_LEN   136U
#define I2C_MULTI_FLAG_START   0x01U
#define I2C_MULTI_FLAG_END     0x02U
#define I2C_MULTI_TIMEOUT_MS   1000U
#define ID_ACK_REPEAT_COUNT    3U

#define I2C_SPEED_100K         0U
#define I2C_SPEED_400K         1U

static uint8_t can_ready;
static uint8_t i2c_ready;
static uint8_t my_node_id = NODE_ID_UNASSIGNED;
static uint8_t id_assigned;
static uint16_t node_token;
static uint8_t request_seq;
static uint8_t diag_seq;
static uint8_t active_protocol_version = NODE_PROTOCOL_VERSION_V1;

typedef enum {
    DISCOVERY_RESPONSE_NONE = 0,
    DISCOVERY_RESPONSE_F0,
    DISCOVERY_RESPONSE_F2,
} discovery_response_t;

static discovery_response_t pending_discovery_response;
static uint8_t pending_discovery_repeats;
static uint32_t pending_discovery_due_ms;

typedef struct {
    uint8_t cmd;
    uint8_t addr;
    uint8_t reg;
    uint8_t data_len;
    uint8_t read_len;
    uint8_t write_len;
    uint8_t speed_code;
    uint8_t request_id;
    uint8_t flags;
    const uint8_t *payload;
    uint8_t payload_len;
    uint8_t write_data[3];
} i2c_bridge_cmd_t;

typedef struct {
    uint8_t active;
    uint8_t addr;
    uint8_t len;
    uint8_t overflow;
    uint8_t data[I2C_MULTI_BUFFER_LEN];
} i2c_multi_write_session_t;

static i2c_multi_write_session_t multi_write_session;
static uint32_t multi_write_last_activity_ms;

static mcu_port_i2c_t i2c_bus = {
    .i2c = BOARD_I2C1_INSTANCE,
    .scl_port = BOARD_I2C1_SCL_GPIO_PORT,
    .scl_pin = BOARD_I2C1_SCL_GPIO_PIN,
    .sda_port = BOARD_I2C1_SDA_GPIO_PORT,
    .sda_pin = BOARD_I2C1_SDA_GPIO_PIN,
    .gpio_clock = BOARD_I2C1_GPIO_CLK,
    .i2c_clock = BOARD_I2C1_CLK,
    .speed_hz = 100000U,
    .initialized = 0U,
};

static uint16_t crc16_ccitt_false(const uint8_t *data, uint16_t len)
{
    uint16_t crc = 0xFFFFU;
    uint16_t i;
    uint8_t bit;

    for (i = 0U; i < len; ++i) {
        crc ^= (uint16_t)data[i] << 8U;
        for (bit = 0U; bit < 8U; ++bit) {
            crc = ((crc & 0x8000U) != 0U)
                      ? (uint16_t)((crc << 1U) ^ 0x1021U)
                      : (uint16_t)(crc << 1U);
        }
    }
    return crc;
}

static uint16_t read_node_token(void)
{
    const volatile uint8_t *uid = (const volatile uint8_t *)CH32_UID_BASE;
    uint8_t identity[13];
    uint8_t i;
    uint16_t token;

    for (i = 0U; i < 12U; ++i) {
        identity[i] = uid[i];
    }
    identity[12] = DEVICE_TYPE;
    token = crc16_ccitt_false(identity, (uint16_t)sizeof(identity));

    if (token == 0U || token == 0xFFFFU) {
        token = 0x4932U;
    }
    return token;
}

static uint8_t is_valid_node_id(uint8_t node_id)
{
    return (node_id >= NODE_ID_MIN && node_id <= NODE_ID_MAX) ? 1U : 0U;
}


static uint16_t can_filter_std16(uint16_t can_id)
{
    return (uint16_t)((can_id & 0x7FFU) << 5U);
}

static void app_can_filter_config(uint8_t assigned, uint8_t node_id)
{
    CAN_FilterInitTypeDef filter = {0};

    /*
     * Keep filtering open at the application layer.
     *
     * The dynamic gateway must always hear discovery/control frames and the
     * command frame for its assigned node. The previous 16-bit list filter was
     * too fragile on the CH32V20x CAN peripheral: nodes could transmit HELLO
     * after assignment, but did not receive normal I2C command frames such as
     * 0x201/0x202/0x203. The main loop still checks exact CAN IDs before
     * handling frames, so accepting all here is functionally safe and restores
     * multi-node command reception without changing the shared BSP CAN driver.
     */
    (void)assigned;
    (void)node_id;
    (void)can_filter_std16(CAN_ID_DISCOVERY);

    filter.CAN_FilterNumber = 0U;
    filter.CAN_FilterMode = CAN_FilterMode_IdMask;
    filter.CAN_FilterScale = CAN_FilterScale_32bit;
    filter.CAN_FilterIdHigh = 0U;
    filter.CAN_FilterIdLow = 0U;
    filter.CAN_FilterMaskIdHigh = 0U;
    filter.CAN_FilterMaskIdLow = 0U;
    filter.CAN_FilterFIFOAssignment = CAN_Filter_FIFO0;
    filter.CAN_FilterActivation = ENABLE;
    CAN_FilterInit(&filter);
}

static uint8_t parse_i2c_command(const bsp_can_frame_t *frame, i2c_bridge_cmd_t *cmd)
{
    if (frame == 0 || cmd == 0 || frame->dlc < 1U) {
        return 0U;
    }

    memset(cmd, 0, sizeof(*cmd));
    cmd->cmd = frame->data[0];

    switch (cmd->cmd) {
    case I2C_CMD_SCAN:
        return 1U;
    case I2C_CMD_PROBE:
        if (frame->dlc < 2U) return 0U;
        cmd->addr = frame->data[1];
        return 1U;
    case I2C_CMD_WRITE_REG:
        if (frame->dlc < 4U) return 0U;
        cmd->addr = frame->data[1];
        cmd->reg = frame->data[2];
        cmd->data_len = frame->data[3];
        cmd->payload = &frame->data[4];
        cmd->payload_len = (frame->dlc > 4U) ? (uint8_t)(frame->dlc - 4U) : 0U;
        return 1U;
    case I2C_CMD_READ_REGS:
        if (frame->dlc < 4U) return 0U;
        cmd->addr = frame->data[1];
        cmd->reg = frame->data[2];
        cmd->read_len = frame->data[3];
        cmd->request_id = (frame->dlc >= 5U) ? frame->data[4] : 0U;
        return 1U;
    case I2C_CMD_WRITE_RAW:
        if (frame->dlc < 3U) return 0U;
        cmd->addr = frame->data[1];
        cmd->data_len = frame->data[2];
        cmd->payload = &frame->data[3];
        cmd->payload_len = (frame->dlc > 3U) ? (uint8_t)(frame->dlc - 3U) : 0U;
        return 1U;
    case I2C_CMD_WRITE_READ:
        if (frame->dlc < 6U) return 0U;
        cmd->addr = frame->data[1];
        cmd->write_len = frame->data[2];
        cmd->read_len = frame->data[3];
        cmd->request_id = frame->data[4];
        if (cmd->write_len == 0U || cmd->write_len > 3U ||
            frame->dlc < (uint8_t)(5U + cmd->write_len)) return 0U;
        memcpy(cmd->write_data, &frame->data[5], cmd->write_len);
        cmd->reg = cmd->write_data[cmd->write_len - 1U];
        return 1U;
    case I2C_CMD_SET_SPEED:
        if (frame->dlc < 2U) return 0U;
        cmd->speed_code = frame->data[1];
        return 1U;
    case I2C_CMD_WRITE_MULTI:
        if (frame->dlc < 4U) return 0U;
        cmd->addr = frame->data[1];
        cmd->flags = frame->data[2];
        cmd->data_len = frame->data[3];
        cmd->payload = &frame->data[4];
        cmd->payload_len = (frame->dlc > 4U) ? (uint8_t)(frame->dlc - 4U) : 0U;
        return 1U;
    default:
        return 0U;
    }
}

static void send_discovery_frame(uint16_t can_id, uint8_t cmd)
{
    uint8_t data[8] = {0};

    data[0] = cmd;
    data[1] = DEVICE_TYPE;
    data[2] = FW_VERSION;
    data[3] = CAPABILITY_FLAGS;
    data[4] = request_seq;
    data[5] = (uint8_t)(node_token & 0xFFU);
    data[6] = (uint8_t)((node_token >> 8U) & 0xFFU);
    data[7] = my_node_id;
    (void)bsp_can_send_std(can_id, data, sizeof(data));
}

static void send_id_request(void)
{
    request_seq++;
    send_discovery_frame(CAN_ID_DISCOVERY, DYN_CMD_REQUEST_ID);
}

static void send_id_ack_once(void)
{
    uint8_t data[8] = {0};

    data[0] = DYN_CMD_ID_ACK;
    data[1] = my_node_id;
    data[2] = DYN_MAGIC0;
    data[3] = DYN_MAGIC1;
    data[4] = (uint8_t)(node_token & 0xFFU);
    data[5] = (uint8_t)((node_token >> 8U) & 0xFFU);
    data[6] = DEVICE_TYPE;
    data[7] = FW_VERSION;

    /* 与 UART 动态网关保持统一：F0/F1/F2 都使用发现 ID 0x000。
     * 多节点冲突由 token 退避槽和 ESP32 换 sequence 重询处理。 */
    (void)bsp_can_send_std(CAN_ID_DISCOVERY, data, sizeof(data));
}

static uint32_t discovery_slot_delay_ms(uint8_t sequence)
{
    uint16_t mixed = (uint16_t)(node_token ^ ((uint16_t)sequence * 0x45D9U));
    mixed ^= (uint16_t)(mixed >> 7U);
    return (uint32_t)(1U +
        ((uint32_t)(mixed % DISCOVERY_SLOT_COUNT) * DISCOVERY_SLOT_MS));
}

static void schedule_discovery_response(discovery_response_t response,
                                        uint8_t sequence,
                                        uint8_t repeat_count,
                                        uint8_t use_slot_delay)
{
    if (response == DISCOVERY_RESPONSE_F0 &&
        pending_discovery_response == DISCOVERY_RESPONSE_F2) {
        return;
    }

    pending_discovery_response = response;
    pending_discovery_repeats = repeat_count;
    pending_discovery_due_ms = mcu_port_millis() +
        (use_slot_delay ? discovery_slot_delay_ms(sequence)
                        : ID_ACK_REPEAT_GAP_MS);
}

static uint8_t time_reached(uint32_t now, uint32_t deadline)
{
    return ((int32_t)(now - deadline) >= 0) ? 1U : 0U;
}

static void discovery_response_task(void)
{
    uint32_t now;

    if (!can_ready || pending_discovery_response == DISCOVERY_RESPONSE_NONE) {
        return;
    }

    now = mcu_port_millis();
    if (!time_reached(now, pending_discovery_due_ms)) {
        return;
    }

    if (pending_discovery_response == DISCOVERY_RESPONSE_F0) {
        send_id_request();
        pending_discovery_response = DISCOVERY_RESPONSE_NONE;
        pending_discovery_repeats = 0U;
        return;
    }

    send_id_ack_once();
    if (pending_discovery_repeats > 1U) {
        pending_discovery_repeats--;
        pending_discovery_due_ms = now + ID_ACK_REPEAT_GAP_MS;
    } else {
        pending_discovery_response = DISCOVERY_RESPONSE_NONE;
        pending_discovery_repeats = 0U;
    }
}

static void send_node_ack(uint8_t command_type, uint8_t result)
{
    if (id_assigned) {
        (void)node_send_ack(my_node_id, DEVICE_TYPE, command_type, result);
    }
}

static void send_status(uint8_t status_type, const uint8_t *payload, uint8_t payload_len)
{
    uint8_t data[8] = {0};
    uint8_t copy_len = payload_len;

    if (!id_assigned) {
        return;
    }

    data[0] = status_type;
    if (copy_len > 7U) {
        copy_len = 7U;
    }
    if (payload != 0 && copy_len > 0U) {
        memcpy(&data[1], payload, copy_len);
    }

    (void)bsp_can_send_std(CAN_ID_STATUS(my_node_id), data, sizeof(data));
}

static void send_diag(uint8_t stage, uint8_t a, uint8_t b, uint8_t c, uint8_t d)
{
    uint8_t payload[7] = {0};

    payload[0] = stage;
    payload[1] = a;
    payload[2] = b;
    payload[3] = c;
    payload[4] = d;
    payload[5] = diag_seq++;
    payload[6] = my_node_id;
    send_status(I2C_STATUS_DIAG, payload, sizeof(payload));
}

static void send_scan_result(uint8_t found_count, const uint8_t *addresses,
                             uint8_t scl_before, uint8_t sda_before,
                             uint8_t recovered, uint8_t scl_after,
                             uint8_t sda_after, uint8_t gpio_ack_normal,
                             uint8_t gpio_ack_swapped)
{
    uint8_t payload[7] = {0};
    uint8_t i;
    uint8_t copy_count = found_count;

    if (copy_count > 6U) {
        copy_count = 6U;
    }

    payload[0] = found_count;
    for (i = 0U; i < copy_count; ++i) {
        payload[(uint8_t)(1U + i)] = addresses[i];
    }
    if (found_count == 0U) {
        payload[1] = SCAN_DIAG_MARKER;
        payload[2] = scl_before;
        payload[3] = sda_before;
        payload[4] = recovered;
        payload[5] = (uint8_t)((scl_after ? 0x01U : 0U) |
                               (sda_after ? 0x02U : 0U) |
                               (gpio_ack_normal ? 0x04U : 0U) |
                               (gpio_ack_swapped ? 0x08U : 0U));
    }

    send_status(I2C_STATUS_SCAN, payload, sizeof(payload));
}

static void send_probe_result(uint8_t addr, uint8_t ok)
{
    uint8_t payload[2] = {0};

    payload[0] = addr;
    payload[1] = ok ? 1U : 0U;
    send_status(I2C_STATUS_PROBE, payload, sizeof(payload));
}

static void send_write_result(uint8_t status_type, uint8_t addr, uint8_t reg, uint8_t len, uint8_t ok)
{
    uint8_t payload[4] = {0};

    payload[0] = addr;
    payload[1] = reg;
    payload[2] = len;
    payload[3] = ok ? 1U : 0U;
    send_status(status_type, payload, sizeof(payload));
}

static void send_multi_write_result(uint8_t addr, uint8_t flags, uint8_t buffered_len, uint8_t ok)
{
    uint8_t payload[4] = {0};

    payload[0] = addr;
    payload[1] = flags;
    payload[2] = buffered_len;
    payload[3] = ok ? 1U : 0U;
    send_status(I2C_STATUS_WRITE_MULTI, payload, sizeof(payload));
}

static void send_legacy_read_result(uint8_t addr, uint8_t reg, uint8_t len, uint8_t ok, const uint8_t *read_data)
{
    uint8_t payload[7] = {0};
    uint8_t report_len;
    uint8_t i;

    report_len = ok ? len : 0U;
    if (report_len > I2C_LEGACY_READ_MAX) {
        report_len = I2C_LEGACY_READ_MAX;
    }

    payload[0] = addr;
    payload[1] = reg;
    payload[2] = report_len;
    payload[3] = ok ? 1U : 0U;
    for (i = 0U; i < report_len; ++i) {
        payload[(uint8_t)(4U + i)] = read_data[i];
    }

    send_status(I2C_STATUS_READ, payload, sizeof(payload));
}

static void send_read_chunks(uint8_t request_id, uint8_t addr, uint8_t reg, uint8_t total_len, uint8_t ok, const uint8_t *read_data)
{
    uint8_t offset = 0U;

    if (!id_assigned) {
        return;
    }

    if (ok) {
        while (offset < total_len) {
            uint8_t data[8] = {0};
            uint8_t chunk_len = (uint8_t)(total_len - offset);
            uint8_t i;

            if (chunk_len > I2C_READ_CHUNK_LEN) {
                chunk_len = I2C_READ_CHUNK_LEN;
            }

            data[0] = I2C_STATUS_READ_CHUNK;
            data[1] = request_id;
            data[2] = offset;
            data[3] = chunk_len;
            for (i = 0U; i < chunk_len; ++i) {
                data[(uint8_t)(4U + i)] = read_data[(uint8_t)(offset + i)];
            }
            (void)bsp_can_send_std(CAN_ID_STATUS(my_node_id), data, sizeof(data));
            offset = (uint8_t)(offset + chunk_len);
        }
    }

    {
        uint8_t done[7] = {0};
        done[0] = request_id;
        done[1] = addr;
        done[2] = reg;
        done[3] = total_len;
        done[4] = ok ? 1U : 0U;
        done[5] = offset;
        send_status(I2C_STATUS_READ_DONE, done, sizeof(done));
    }
}

static void handle_scan(void)
{
    uint8_t found[6] = {0};
    uint8_t found_count = 0U;
    uint8_t report_count = 0U;
    uint8_t addr;
    uint8_t scl_before;
    uint8_t sda_before;
    uint8_t recovered;
    uint8_t scl_after;
    uint8_t sda_after;
    uint8_t gpio_ack_normal;
    uint8_t gpio_ack_swapped;

    send_diag(DIAG_SCAN_START, 0x03U, 0x77U, 0U, 0U);
    scl_before = GPIO_ReadInputDataBit(BOARD_I2C1_SCL_GPIO_PORT,
                                       BOARD_I2C1_SCL_GPIO_PIN) != Bit_RESET;
    sda_before = GPIO_ReadInputDataBit(BOARD_I2C1_SDA_GPIO_PORT,
                                       BOARD_I2C1_SDA_GPIO_PIN) != Bit_RESET;
    recovered = mcu_port_i2c_recover_bus(&i2c_bus);
    mcu_port_delay_ms(10U);
    scl_after = GPIO_ReadInputDataBit(BOARD_I2C1_SCL_GPIO_PORT,
                                      BOARD_I2C1_SCL_GPIO_PIN) != Bit_RESET;
    sda_after = GPIO_ReadInputDataBit(BOARD_I2C1_SDA_GPIO_PORT,
                                      BOARD_I2C1_SDA_GPIO_PIN) != Bit_RESET;
    gpio_ack_normal = mcu_port_i2c_gpio_probe(&i2c_bus, 0x29U, 0U);
    gpio_ack_swapped = mcu_port_i2c_gpio_probe(&i2c_bus, 0x29U, 1U);
    for (addr = 0x03U; addr <= 0x77U; ++addr) {
        if (mcu_port_i2c_is_ready(&i2c_bus, addr)) {
            if (report_count < sizeof(found)) {
                found[report_count] = addr;
                report_count++;
            }
            found_count++;
        }
    }

    send_diag(DIAG_SCAN_DONE, found_count, report_count, found[0], found[1]);
    send_scan_result(found_count, found, scl_before, sda_before, recovered,
                     scl_after, sda_after, gpio_ack_normal,
                     gpio_ack_swapped);
    send_node_ack(I2C_CMD_SCAN, 1U);
}

static void handle_probe(const i2c_bridge_cmd_t *cmd)
{
    uint8_t ok;

    if (cmd == 0) {
        send_node_ack(I2C_CMD_PROBE, 0U);
        return;
    }

    ok = mcu_port_i2c_is_ready(&i2c_bus, cmd->addr);
    send_probe_result(cmd->addr, ok);
    send_node_ack(I2C_CMD_PROBE, ok);
}

static void handle_write_reg(const i2c_bridge_cmd_t *cmd)
{
    uint8_t tx_buf[(uint8_t)(I2C_MAX_REG_PAYLOAD + 1U)] = {0};
    uint8_t ok;

    if (cmd == 0 || cmd->data_len == 0U || cmd->data_len > I2C_MAX_REG_PAYLOAD || cmd->payload_len < cmd->data_len) {
        if (cmd != 0) {
            send_write_result(I2C_STATUS_WRITE, cmd->addr, cmd->reg, cmd->data_len, 0U);
        }
        send_node_ack(I2C_CMD_WRITE_REG, 0U);
        return;
    }

    tx_buf[0] = cmd->reg;
    memcpy(&tx_buf[1], cmd->payload, cmd->data_len);
    ok = mcu_port_i2c_write(&i2c_bus, cmd->addr, tx_buf, (uint16_t)(cmd->data_len + 1U));
    send_write_result(I2C_STATUS_WRITE, cmd->addr, cmd->reg, cmd->data_len, ok);
    send_node_ack(I2C_CMD_WRITE_REG, ok);
}

static void handle_read_regs(const i2c_bridge_cmd_t *cmd)
{
    uint8_t rx_buf[I2C_MAX_READ_LEN] = {0};
    uint8_t ok = 0U;

    if (cmd == 0 || cmd->read_len == 0U || cmd->read_len > I2C_MAX_READ_LEN) {
        if (cmd != 0) {
            send_legacy_read_result(cmd->addr, cmd->reg, cmd->read_len, 0U, rx_buf);
            send_read_chunks(cmd->request_id, cmd->addr, cmd->reg, cmd->read_len, 0U, rx_buf);
        }
        send_node_ack(I2C_CMD_READ_REGS, 0U);
        return;
    }

    ok = mcu_port_i2c_read_regs(&i2c_bus, cmd->addr, cmd->reg, rx_buf,
                                cmd->read_len);
    if (cmd->request_id == 0U && cmd->read_len <= I2C_LEGACY_READ_MAX) {
        send_legacy_read_result(cmd->addr, cmd->reg, cmd->read_len, ok, rx_buf);
    } else {
        send_read_chunks(cmd->request_id, cmd->addr, cmd->reg, cmd->read_len, ok, rx_buf);
    }
    send_node_ack(I2C_CMD_READ_REGS, ok);
}

static void handle_write_raw(const i2c_bridge_cmd_t *cmd)
{
    uint8_t ok;

    if (cmd == 0 || cmd->data_len == 0U || cmd->data_len > I2C_MAX_WRITE_PAYLOAD || cmd->payload_len < cmd->data_len) {
        if (cmd != 0) {
            send_write_result(I2C_STATUS_RAW_WRITE, cmd->addr, 0U, cmd->data_len, 0U);
        }
        send_node_ack(I2C_CMD_WRITE_RAW, 0U);
        return;
    }

    ok = mcu_port_i2c_write(&i2c_bus, cmd->addr, cmd->payload, cmd->data_len);
    send_write_result(I2C_STATUS_RAW_WRITE, cmd->addr, 0U, cmd->data_len, ok);
    send_node_ack(I2C_CMD_WRITE_RAW, ok);
}

static void handle_write_read(const i2c_bridge_cmd_t *cmd)
{
    uint8_t rx_buf[I2C_MAX_READ_LEN] = {0};
    uint8_t ok = 0U;

    if (cmd == 0 || cmd->write_len == 0U || cmd->write_len > 3U ||
        cmd->read_len == 0U || cmd->read_len > I2C_MAX_READ_LEN) {
        if (cmd != 0) {
            send_read_chunks(cmd->request_id, cmd->addr, 0U, cmd->read_len, 0U, rx_buf);
        }
        send_node_ack(I2C_CMD_WRITE_READ, 0U);
        return;
    }

    ok = mcu_port_i2c_write_read(&i2c_bus, cmd->addr,
                                 cmd->write_data, cmd->write_len,
                                 rx_buf, cmd->read_len);
    if (cmd->request_id == 0U && cmd->read_len <= I2C_LEGACY_READ_MAX) {
        send_legacy_read_result(cmd->addr, cmd->reg, cmd->read_len, ok, rx_buf);
    } else {
        send_read_chunks(cmd->request_id, cmd->addr, cmd->reg, cmd->read_len, ok, rx_buf);
    }
    send_node_ack(I2C_CMD_WRITE_READ, ok);
}

static void handle_set_speed(const i2c_bridge_cmd_t *cmd)
{
    uint8_t speed_code;
    uint8_t ok = 0U;
    uint8_t payload[4] = {0};

    if (cmd == 0) {
        send_node_ack(I2C_CMD_SET_SPEED, 0U);
        return;
    }

    speed_code = cmd->speed_code;
    if (speed_code == I2C_SPEED_100K && i2c_ready &&
        i2c_bus.speed_hz == 100000U) {
        ok = 1U;
    } else if (speed_code == I2C_SPEED_100K) {
        i2c_bus.speed_hz = 100000U;
        i2c_ready = mcu_port_i2c_init(&i2c_bus);
        ok = i2c_ready;
    } else if (speed_code == I2C_SPEED_400K && i2c_ready &&
               i2c_bus.speed_hz == 400000U) {
        ok = 1U;
    } else if (speed_code == I2C_SPEED_400K) {
        i2c_bus.speed_hz = 400000U;
        i2c_ready = mcu_port_i2c_init(&i2c_bus);
        ok = i2c_ready;
    }

    payload[0] = speed_code;
    payload[1] = ok ? 1U : 0U;
    payload[2] = (uint8_t)((i2c_bus.speed_hz / 1000U) & 0xFFU);
    payload[3] = (uint8_t)(((i2c_bus.speed_hz / 1000U) >> 8U) & 0xFFU);
    send_status(I2C_STATUS_SPEED, payload, sizeof(payload));
    send_node_ack(I2C_CMD_SET_SPEED, ok);
}

static void reset_multi_write_session(void)
{
    memset(&multi_write_session, 0, sizeof(multi_write_session));
    multi_write_last_activity_ms = 0U;
}

static void handle_write_multi(const i2c_bridge_cmd_t *cmd)
{
    uint8_t ok = 0U;
    uint32_t now = mcu_port_millis();

    if (multi_write_session.active &&
        (uint32_t)(now - multi_write_last_activity_ms) > I2C_MULTI_TIMEOUT_MS) {
        send_multi_write_result(multi_write_session.addr, 0U, multi_write_session.len, 0U);
        send_node_ack(I2C_CMD_WRITE_MULTI, 0U);
        reset_multi_write_session();
    }

    if (cmd == 0 || cmd->data_len == 0U || cmd->data_len > I2C_MULTI_CHUNK_MAX || cmd->payload_len < cmd->data_len) {
        if (cmd != 0) {
            send_multi_write_result(cmd->addr, cmd->flags, multi_write_session.len, 0U);
        }
        send_node_ack(I2C_CMD_WRITE_MULTI, 0U);
        return;
    }

    if ((cmd->flags & I2C_MULTI_FLAG_START) != 0U) {
        reset_multi_write_session();
        multi_write_session.active = 1U;
        multi_write_session.addr = cmd->addr;
    }

    if (!multi_write_session.active || multi_write_session.addr != cmd->addr) {
        send_multi_write_result(cmd->addr, cmd->flags, multi_write_session.len, 0U);
        send_node_ack(I2C_CMD_WRITE_MULTI, 0U);
        return;
    }

    multi_write_last_activity_ms = now;

    if ((uint16_t)multi_write_session.len + cmd->data_len > I2C_MULTI_BUFFER_LEN) {
        multi_write_session.overflow = 1U;
        send_multi_write_result(cmd->addr, cmd->flags, multi_write_session.len, 0U);
        send_node_ack(I2C_CMD_WRITE_MULTI, 0U);
        reset_multi_write_session();
        return;
    }

    memcpy(&multi_write_session.data[multi_write_session.len], cmd->payload, cmd->data_len);
    multi_write_session.len = (uint8_t)(multi_write_session.len + cmd->data_len);

    if ((cmd->flags & I2C_MULTI_FLAG_END) != 0U) {
        if (multi_write_session.len > 0U) {
            ok = mcu_port_i2c_write(&i2c_bus, multi_write_session.addr, multi_write_session.data, multi_write_session.len);
        }
        send_multi_write_result(cmd->addr, cmd->flags, multi_write_session.len, ok);
        send_node_ack(I2C_CMD_WRITE_MULTI, ok);
        reset_multi_write_session();
    } else {
        /* Flow-control ACK: one fragment may be sent only after the previous
         * fragment has been accepted.  This prevents silent truncation when a
         * long write exceeds the CAN RX FIFO depth. */
        send_multi_write_result(cmd->addr, cmd->flags, multi_write_session.len, 1U);
        send_node_ack(I2C_CMD_WRITE_MULTI, 1U);
    }
}

static uint8_t frame_token_matches(const bsp_can_frame_t *frame)
{
    uint16_t token;

    if (frame == 0 || frame->dlc < 6U) {
        return 0U;
    }

    token = node_get_u16_le(frame->data, 4U);
    return (token == node_token) ? 1U : 0U;
}

static void handle_assign_id(const bsp_can_frame_t *frame)
{
    uint8_t new_node_id;

    if (frame == 0 || frame->dlc < 8U) {
        return;
    }
    if (frame->data[0] != DYN_CMD_ASSIGN_ID || frame->data[2] != DYN_MAGIC0 || frame->data[3] != DYN_MAGIC1) {
        return;
    }
    if (frame->data[6] != DEVICE_TYPE ||
        (frame->data[7] != NODE_PROTOCOL_VERSION_V1 &&
         frame->data[7] != NODE_PROTOCOL_VERSION_V2)) {
        return;
    }
    if (!frame_token_matches(frame)) {
        return;
    }

    new_node_id = frame->data[1];
    if (!is_valid_node_id(new_node_id)) {
        return;
    }

    my_node_id = new_node_id;
    active_protocol_version = frame->data[7];
    id_assigned = 1U;
    app_can_filter_config(1U, my_node_id);
    send_id_ack_once();
    schedule_discovery_response(DISCOVERY_RESPONSE_F2, request_seq,
                                (uint8_t)(ID_ACK_REPEAT_COUNT - 1U), 0U);
    send_diag(DIAG_ASSIGN_DONE, my_node_id, (uint8_t)(node_token & 0xFFU), (uint8_t)((node_token >> 8U) & 0xFFU), 0U);
    (void)node_send_hello(my_node_id, DEVICE_TYPE, FW_VERSION, CAPABILITY_FLAGS);
}

static void handle_discovery_frame(const bsp_can_frame_t *frame)
{
    if (frame == 0 || frame->dlc < 1U) {
        return;
    }

    if (frame->data[0] == DYN_CMD_RELEASE_ID) {
        uint8_t release_is_broadcast = 0U;
        uint8_t release_is_targeted = 0U;

        if (frame->dlc >= 8U && frame->data[1] == DYN_MAGIC0 &&
            frame->data[2] == DYN_MAGIC1 &&
            (frame->data[7] == NODE_PROTOCOL_VERSION_V1 ||
             frame->data[7] == NODE_PROTOCOL_VERSION_V2)) {
            release_is_broadcast =
                (frame->data[3] == DYN_RELEASE_BROADCAST_MARKER) ? 1U : 0U;
            release_is_targeted =
                (frame->data[3] == 0U && frame->data[6] == DEVICE_TYPE &&
                 node_get_u16_le(frame->data, 4U) == node_token) ? 1U : 0U;
        }
        if (release_is_broadcast || release_is_targeted) {
            my_node_id = NODE_ID_UNASSIGNED;
            id_assigned = 0U;
            active_protocol_version = NODE_PROTOCOL_VERSION_V1;
            pending_discovery_response = DISCOVERY_RESPONSE_NONE;
            pending_discovery_repeats = 0U;
            app_can_filter_config(0U, NODE_ID_UNASSIGNED);
            schedule_discovery_response(DISCOVERY_RESPONSE_F0,
                                        frame->data[4], 1U, 1U);
        }
    } else if (frame->data[0] == DYN_CMD_ASSIGN_ID) {
        handle_assign_id(frame);
    } else if (frame->data[0] == DYN_CMD_REQUEST_ID && frame->dlc >= 8U &&
               frame->data[1] == DEVICE_TYPE &&
               node_get_u16_le(frame->data, 5U) == 0U) {
        /* A zero-token F0 is an ESP32 inventory query.  Ignore nonzero-token
         * F0 frames from peer CH32 gateways to avoid a discovery feedback loop. */
        if (id_assigned) {
            schedule_discovery_response(DISCOVERY_RESPONSE_F2,
                                        frame->data[4],
                                        ID_ACK_REPEAT_COUNT, 1U);
        } else {
            schedule_discovery_response(DISCOVERY_RESPONSE_F0,
                                        frame->data[4], 1U, 1U);
        }
    }
}
static void heartbeat_task(void)
{
    if (!can_ready) {
        return;
    }

    if (id_assigned) {
        (void)node_send_hello(my_node_id, DEVICE_TYPE, FW_VERSION, CAPABILITY_FLAGS);
    } else {
        schedule_discovery_response(DISCOVERY_RESPONSE_F0,
                                    request_seq, 1U, 1U);
    }
}

static void handle_i2c_command(const bsp_can_frame_t *frame)
{
    i2c_bridge_cmd_t cmd;

    if (frame == 0 || frame->dlc < 1U) {
        return;
    }

    send_diag(DIAG_RX_CMD, frame->data[0], frame->dlc, (uint8_t)(frame->id & 0xFFU), (uint8_t)((frame->id >> 8U) & 0xFFU));
    if (!parse_i2c_command(frame, &cmd)) {
        send_diag(DIAG_UNKNOWN_CMD, frame->data[0], frame->dlc, 0U, 0U);
        send_node_ack(frame->data[0], 0U);
        return;
    }
    if (!i2c_ready && cmd.cmd != I2C_CMD_SET_SPEED) {
        send_diag(DIAG_I2C_INIT, cmd.cmd, 0U, 0U, 0U);
        send_node_ack(cmd.cmd, 0U);
        return;
    }

    switch (cmd.cmd) {
    case I2C_CMD_SCAN:
        handle_scan();
        break;
    case I2C_CMD_PROBE:
        handle_probe(&cmd);
        break;
    case I2C_CMD_WRITE_REG:
        handle_write_reg(&cmd);
        break;
    case I2C_CMD_READ_REGS:
        handle_read_regs(&cmd);
        break;
    case I2C_CMD_WRITE_RAW:
        handle_write_raw(&cmd);
        break;
    case I2C_CMD_WRITE_READ:
        handle_write_read(&cmd);
        break;
    case I2C_CMD_SET_SPEED:
        handle_set_speed(&cmd);
        break;
    case I2C_CMD_WRITE_MULTI:
        handle_write_multi(&cmd);
        break;
    default:
        send_diag(DIAG_UNKNOWN_CMD, cmd.cmd, frame->dlc, 0U, 0U);
        send_node_ack(cmd.cmd, 0U);
        break;
    }
}

static void can_poll_task(void)
{
    bsp_can_frame_t frame;

    if (!can_ready) {
        return;
    }

    while (bsp_can_receive(&frame)) {
        if (frame.id == CAN_ID_DISCOVERY) {
            handle_discovery_frame(&frame);
            continue;
        }

        if (!id_assigned) {
            continue;
        }

        if (frame.id == CAN_ID_I2C_CMD(my_node_id)) {
            handle_i2c_command(&frame);
        }
    }
}

void app_setup(void)
{
    node_token = read_node_token();
    i2c_ready = mcu_port_i2c_init(&i2c_bus);
    can_ready = bsp_can_init(BSP_CAN_BITRATE_500K);
    if (can_ready) {
        app_can_filter_config(0U, NODE_ID_UNASSIGNED);
    }

    heartbeat_task();
    (void)vibe_task_every_ms(DISCOVERY_SERVICE_MS, discovery_response_task);
    (void)vibe_task_every_ms(HEARTBEAT_MS, heartbeat_task);
    (void)vibe_task_every_ms(CAN_POLL_MS, can_poll_task);
}
