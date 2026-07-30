#include "common_can_node.h"
#include "board_pins.h"
#include "mcu_port_i2c.h"
#include "vibe_runtime.h"

#include <stdint.h>
#include <string.h>

#ifndef NODE_ID
#define NODE_ID            1U
#endif

#define DEVICE_TYPE        DEVICE_TYPE_I2C
#define FW_VERSION         2U

#define CAP_SCAN           0x01U
#define CAP_PROBE          0x02U
#define CAP_REG_IO         0x04U
#define CAP_RAW_WRITE      0x08U
#define CAP_CHUNK_READ     0x10U
#define CAP_WRITE_READ     0x20U
#define CAP_SET_SPEED      0x40U
#define CAPABILITY_FLAGS   (CAP_SCAN | CAP_PROBE | CAP_REG_IO | CAP_RAW_WRITE | CAP_CHUNK_READ | CAP_WRITE_READ | CAP_SET_SPEED)

#define HEARTBEAT_MS       1000U
#define CAN_POLL_MS        5U

/* Legacy commands kept for compatibility. */
#define I2C_CMD_SCAN        0x01U
#define I2C_CMD_PROBE       0x02U
#define I2C_CMD_WRITE_REG   0x03U
#define I2C_CMD_READ_REGS   0x04U

/* Generic transport extensions. */
#define I2C_CMD_WRITE_RAW   0x05U
#define I2C_CMD_WRITE_READ  0x06U
#define I2C_CMD_SET_SPEED   0x07U

#define I2C_STATUS_SCAN        0x01U
#define I2C_STATUS_PROBE       0x02U
#define I2C_STATUS_WRITE       0x03U
#define I2C_STATUS_READ        0x04U
#define I2C_STATUS_READ_CHUNK  0x05U
#define I2C_STATUS_READ_DONE   0x06U
#define I2C_STATUS_RAW_WRITE   0x07U
#define I2C_STATUS_SPEED       0x08U

#define I2C_MAX_WRITE_PAYLOAD  5U
#define I2C_MAX_REG_PAYLOAD    4U
#define I2C_MAX_READ_LEN       32U
#define I2C_READ_CHUNK_LEN     4U

#define I2C_SPEED_100K         0U
#define I2C_SPEED_400K         1U

static uint8_t can_ready;

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

static void send_status(uint8_t status_type, const uint8_t *payload, uint8_t payload_len)
{
    uint8_t data[8] = {0};
    uint8_t copy_len = payload_len;

    data[0] = status_type;
    if (copy_len > 7U) {
        copy_len = 7U;
    }
    if (payload != 0 && copy_len > 0U) {
        memcpy(&data[1], payload, copy_len);
    }

    (void)bsp_can_send_std(CAN_ID_STATUS(NODE_ID), data, sizeof(data));
}

static void send_scan_result(uint8_t found_count, const uint8_t *addresses)
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

static void send_legacy_read_result(uint8_t addr, uint8_t reg, uint8_t len, uint8_t ok, const uint8_t *read_data)
{
    uint8_t payload[7] = {0};
    uint8_t report_len;
    uint8_t i;

    report_len = ok ? len : 0U;
    if (report_len > I2C_MAX_REG_PAYLOAD) {
        report_len = I2C_MAX_REG_PAYLOAD;
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
            (void)bsp_can_send_std(CAN_ID_STATUS(NODE_ID), data, sizeof(data));
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

    for (addr = 0x03U; addr <= 0x77U; ++addr) {
        if (mcu_port_i2c_is_ready(&i2c_bus, addr)) {
            if (report_count < sizeof(found)) {
                found[report_count] = addr;
                report_count++;
            }
            found_count++;
        }
    }

    send_scan_result(found_count, found);
    (void)node_send_ack(NODE_ID, DEVICE_TYPE, I2C_CMD_SCAN, 1U);
}

static void handle_probe(const bsp_can_frame_t *frame)
{
    uint8_t addr;
    uint8_t ok;

    if (frame == 0 || frame->dlc < 2U) {
        (void)node_send_ack(NODE_ID, DEVICE_TYPE, I2C_CMD_PROBE, 0U);
        return;
    }

    addr = frame->data[1];
    ok = mcu_port_i2c_is_ready(&i2c_bus, addr);
    send_probe_result(addr, ok);
    (void)node_send_ack(NODE_ID, DEVICE_TYPE, I2C_CMD_PROBE, ok);
}

static void handle_write_reg(const bsp_can_frame_t *frame)
{
    uint8_t addr;
    uint8_t reg;
    uint8_t len;
    uint8_t tx_buf[(uint8_t)(I2C_MAX_REG_PAYLOAD + 1U)] = {0};
    uint8_t ok;

    if (frame == 0 || frame->dlc < 4U) {
        (void)node_send_ack(NODE_ID, DEVICE_TYPE, I2C_CMD_WRITE_REG, 0U);
        return;
    }

    addr = frame->data[1];
    reg = frame->data[2];
    len = frame->data[3];
    if (len == 0U || len > I2C_MAX_REG_PAYLOAD || frame->dlc < (uint8_t)(4U + len)) {
        send_write_result(I2C_STATUS_WRITE, addr, reg, len, 0U);
        (void)node_send_ack(NODE_ID, DEVICE_TYPE, I2C_CMD_WRITE_REG, 0U);
        return;
    }

    tx_buf[0] = reg;
    memcpy(&tx_buf[1], &frame->data[4], len);
    ok = mcu_port_i2c_write(&i2c_bus, addr, tx_buf, (uint16_t)(len + 1U));
    send_write_result(I2C_STATUS_WRITE, addr, reg, len, ok);
    (void)node_send_ack(NODE_ID, DEVICE_TYPE, I2C_CMD_WRITE_REG, ok);
}

static void handle_read_regs(const bsp_can_frame_t *frame)
{
    uint8_t addr;
    uint8_t reg;
    uint8_t len;
    uint8_t request_id;
    uint8_t rx_buf[I2C_MAX_READ_LEN] = {0};
    uint8_t ok;

    if (frame == 0 || frame->dlc < 4U) {
        (void)node_send_ack(NODE_ID, DEVICE_TYPE, I2C_CMD_READ_REGS, 0U);
        return;
    }

    addr = frame->data[1];
    reg = frame->data[2];
    len = frame->data[3];
    request_id = (frame->dlc >= 5U) ? frame->data[4] : 0U;
    if (len == 0U || len > I2C_MAX_READ_LEN) {
        send_legacy_read_result(addr, reg, len, 0U, rx_buf);
        send_read_chunks(request_id, addr, reg, len, 0U, rx_buf);
        (void)node_send_ack(NODE_ID, DEVICE_TYPE, I2C_CMD_READ_REGS, 0U);
        return;
    }

    ok = mcu_port_i2c_read_regs(&i2c_bus, addr, reg, rx_buf, len);
    if (len <= I2C_MAX_REG_PAYLOAD) {
        send_legacy_read_result(addr, reg, len, ok, rx_buf);
    }
    if (len > I2C_MAX_REG_PAYLOAD || request_id != 0U) {
        send_read_chunks(request_id, addr, reg, len, ok, rx_buf);
    }
    (void)node_send_ack(NODE_ID, DEVICE_TYPE, I2C_CMD_READ_REGS, ok);
}

static void handle_write_raw(const bsp_can_frame_t *frame)
{
    uint8_t addr;
    uint8_t len;
    uint8_t ok;

    if (frame == 0 || frame->dlc < 3U) {
        (void)node_send_ack(NODE_ID, DEVICE_TYPE, I2C_CMD_WRITE_RAW, 0U);
        return;
    }

    addr = frame->data[1];
    len = frame->data[2];
    if (len == 0U || len > I2C_MAX_WRITE_PAYLOAD || frame->dlc < (uint8_t)(3U + len)) {
        send_write_result(I2C_STATUS_RAW_WRITE, addr, 0U, len, 0U);
        (void)node_send_ack(NODE_ID, DEVICE_TYPE, I2C_CMD_WRITE_RAW, 0U);
        return;
    }

    ok = mcu_port_i2c_write(&i2c_bus, addr, &frame->data[3], len);
    send_write_result(I2C_STATUS_RAW_WRITE, addr, 0U, len, ok);
    (void)node_send_ack(NODE_ID, DEVICE_TYPE, I2C_CMD_WRITE_RAW, ok);
}

static void handle_write_read(const bsp_can_frame_t *frame)
{
    uint8_t addr;
    uint8_t write_len;
    uint8_t read_len;
    uint8_t request_id;
    uint8_t reg;
    uint8_t rx_buf[I2C_MAX_READ_LEN] = {0};
    uint8_t ok;

    if (frame == 0 || frame->dlc < 6U) {
        (void)node_send_ack(NODE_ID, DEVICE_TYPE, I2C_CMD_WRITE_READ, 0U);
        return;
    }

    addr = frame->data[1];
    write_len = frame->data[2];
    read_len = frame->data[3];
    request_id = frame->data[4];
    if (write_len != 1U || read_len == 0U || read_len > I2C_MAX_READ_LEN || frame->dlc < 6U) {
        send_read_chunks(request_id, addr, 0U, read_len, 0U, rx_buf);
        (void)node_send_ack(NODE_ID, DEVICE_TYPE, I2C_CMD_WRITE_READ, 0U);
        return;
    }

    reg = frame->data[5];
    ok = mcu_port_i2c_read_regs(&i2c_bus, addr, reg, rx_buf, read_len);
    if (read_len <= I2C_MAX_REG_PAYLOAD) {
        send_legacy_read_result(addr, reg, read_len, ok, rx_buf);
    }
    if (read_len > I2C_MAX_REG_PAYLOAD || request_id != 0U) {
        send_read_chunks(request_id, addr, reg, read_len, ok, rx_buf);
    }
    (void)node_send_ack(NODE_ID, DEVICE_TYPE, I2C_CMD_WRITE_READ, ok);
}

static void handle_set_speed(const bsp_can_frame_t *frame)
{
    uint8_t speed_code;
    uint8_t ok = 0U;
    uint8_t payload[4] = {0};

    if (frame == 0 || frame->dlc < 2U) {
        (void)node_send_ack(NODE_ID, DEVICE_TYPE, I2C_CMD_SET_SPEED, 0U);
        return;
    }

    speed_code = frame->data[1];
    if (speed_code == I2C_SPEED_100K) {
        i2c_bus.speed_hz = 100000U;
        ok = mcu_port_i2c_init(&i2c_bus);
    } else if (speed_code == I2C_SPEED_400K) {
        i2c_bus.speed_hz = 400000U;
        ok = mcu_port_i2c_init(&i2c_bus);
    }

    payload[0] = speed_code;
    payload[1] = ok ? 1U : 0U;
    payload[2] = (uint8_t)(i2c_bus.speed_hz / 1000U);
    send_status(I2C_STATUS_SPEED, payload, sizeof(payload));
    (void)node_send_ack(NODE_ID, DEVICE_TYPE, I2C_CMD_SET_SPEED, ok);
}

static void heartbeat_task(void)
{
    if (can_ready) {
        (void)node_send_hello(NODE_ID, DEVICE_TYPE, FW_VERSION, CAPABILITY_FLAGS);
    }
}

static void can_poll_task(void)
{
    bsp_can_frame_t frame;

    if (!can_ready) {
        return;
    }

    while (bsp_can_receive(&frame)) {
        if (frame.id != CAN_ID_I2C_CMD(NODE_ID) || frame.dlc < 1U) {
            continue;
        }

        switch (frame.data[0]) {
        case I2C_CMD_SCAN:
            handle_scan();
            break;
        case I2C_CMD_PROBE:
            handle_probe(&frame);
            break;
        case I2C_CMD_WRITE_REG:
            handle_write_reg(&frame);
            break;
        case I2C_CMD_READ_REGS:
            handle_read_regs(&frame);
            break;
        case I2C_CMD_WRITE_RAW:
            handle_write_raw(&frame);
            break;
        case I2C_CMD_WRITE_READ:
            handle_write_read(&frame);
            break;
        case I2C_CMD_SET_SPEED:
            handle_set_speed(&frame);
            break;
        default:
            (void)node_send_ack(NODE_ID, DEVICE_TYPE, frame.data[0], 0U);
            break;
        }
    }
}

void app_setup(void)
{
    (void)mcu_port_i2c_init(&i2c_bus);
    can_ready = bsp_can_init_50k();

    heartbeat_task();
    (void)vibe_task_every_ms(HEARTBEAT_MS, heartbeat_task);
    (void)vibe_task_every_ms(CAN_POLL_MS, can_poll_task);
}

