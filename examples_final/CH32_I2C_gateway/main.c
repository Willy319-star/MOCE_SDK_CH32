#include "common_can_node.h"
#include "board_pins.h"
#include "bsp_mpu6050.h"
#include "mcu_port_i2c.h"
#include "mcu_port_time.h"
#include "vibe_api.h"
#include "vibe_runtime.h"

#include <stdio.h>
#include <string.h>

#define NODE_ID            1U
#define DEVICE_TYPE        DEVICE_TYPE_I2C
#define FW_VERSION         1U
#define CAPABILITY_FLAGS   0x07U

#define HEARTBEAT_MS       1000U
#define CAN_POLL_MS        20U
#define MPU_REPORT_MS      500U

#define I2C_SCAN_CMD       0x01U
#define I2C_WRITE_CMD      0x02U
#define I2C_READ_CMD       0x03U
#define OLED_INIT_CMD      0x10U
#define OLED_CLEAR_CMD     0x11U
#define OLED_TEXT_CMD      0x12U
#define MPU_INIT_CMD       0x20U
#define MPU_READ_CMD       0x21U

#define I2C_STATUS_SCAN    0x01U
#define MPU_STATUS_ACCEL   0x20U
#define MPU_STATUS_GYRO    0x21U
#define MPU_STATUS_TEMP    0x22U

#define OLED_ADDR8         0x78U
#define OLED_ADDR7         (OLED_ADDR8 >> 1)

static uint8_t can_ready;
static uint8_t oled_ready;
static uint8_t mpu_ready;
static uint16_t mpu_sample_count;

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

static void put_i16_le(uint8_t *data, uint8_t off, int16_t value)
{
    node_put_u16_le(data, off, (uint16_t)value);
}

static void print_node_status(void)
{
    char line[96];

    vibe_println("CH32 I2C gateway");
    (void)snprintf(line, sizeof(line), "NODE_ID = %u", NODE_ID); vibe_println(line);
    (void)snprintf(line, sizeof(line), "DEVICE_TYPE = %u", DEVICE_TYPE); vibe_println(line);
    (void)snprintf(line, sizeof(line), "CAN hello id = 0x%03X", CAN_ID_HELLO(NODE_ID)); vibe_println(line);
    (void)snprintf(line, sizeof(line), "ACK id = 0x%03X", CAN_ID_ACK(NODE_ID)); vibe_println(line);
    vibe_println(can_ready ? "CAN init ok" : "CAN init failed");
    (void)snprintf(line, sizeof(line), "OLED ready=%u MPU6050 ready=%u", oled_ready, mpu_ready); vibe_println(line);
}

static void heartbeat_task(void)
{
    if (can_ready) {
        (void)node_send_hello(NODE_ID, DEVICE_TYPE, FW_VERSION, CAPABILITY_FLAGS);
    }
    print_node_status();
}

static uint8_t oled_write(uint8_t control, const uint8_t *data, uint16_t len)
{
    uint8_t buffer[17];
    uint16_t offset = 0U;
    uint16_t chunk;

    while (offset < len) {
        chunk = (uint16_t)(len - offset);
        if (chunk > 16U) {
            chunk = 16U;
        }
        buffer[0] = control;
        memcpy(&buffer[1], &data[offset], chunk);
        if (!mcu_port_i2c_write(&i2c_bus, OLED_ADDR7, buffer, (uint16_t)(chunk + 1U))) {
            return 0U;
        }
        offset = (uint16_t)(offset + chunk);
    }
    return 1U;
}

static uint8_t oled_cmd(uint8_t cmd)
{
    return oled_write(0x00U, &cmd, 1U);
}

static uint8_t oled_data(const uint8_t *data, uint16_t len)
{
    return oled_write(0x40U, data, len);
}

static void oled_set_pos(uint8_t page, uint8_t col)
{
    (void)oled_cmd((uint8_t)(0xB0U | (page & 0x07U)));
    (void)oled_cmd((uint8_t)(0x00U | (col & 0x0FU)));
    (void)oled_cmd((uint8_t)(0x10U | ((col >> 4U) & 0x0FU)));
}

static void oled_clear(void)
{
    uint8_t zeros[128];
    uint8_t page;

    if (!oled_ready) {
        return;
    }

    memset(zeros, 0, sizeof(zeros));
    for (page = 0U; page < 8U; ++page) {
        oled_set_pos(page, 0U);
        (void)oled_data(zeros, sizeof(zeros));
    }
}

static const uint8_t font_space[5] = {0, 0, 0, 0, 0};
static const uint8_t font_digits[10][5] = {
    {0x3E,0x51,0x49,0x45,0x3E},{0x00,0x42,0x7F,0x40,0x00},
    {0x42,0x61,0x51,0x49,0x46},{0x21,0x41,0x45,0x4B,0x31},
    {0x18,0x14,0x12,0x7F,0x10},{0x27,0x45,0x45,0x45,0x39},
    {0x3C,0x4A,0x49,0x49,0x30},{0x01,0x71,0x09,0x05,0x03},
    {0x36,0x49,0x49,0x49,0x36},{0x06,0x49,0x49,0x29,0x1E},
};
static const uint8_t font_upper[26][5] = {
    {0x7E,0x11,0x11,0x11,0x7E},{0x7F,0x49,0x49,0x49,0x36},
    {0x3E,0x41,0x41,0x41,0x22},{0x7F,0x41,0x41,0x22,0x1C},
    {0x7F,0x49,0x49,0x49,0x41},{0x7F,0x09,0x09,0x09,0x01},
    {0x3E,0x41,0x49,0x49,0x7A},{0x7F,0x08,0x08,0x08,0x7F},
    {0x00,0x41,0x7F,0x41,0x00},{0x20,0x40,0x41,0x3F,0x01},
    {0x7F,0x08,0x14,0x22,0x41},{0x7F,0x40,0x40,0x40,0x40},
    {0x7F,0x02,0x0C,0x02,0x7F},{0x7F,0x04,0x08,0x10,0x7F},
    {0x3E,0x41,0x41,0x41,0x3E},{0x7F,0x09,0x09,0x09,0x06},
    {0x3E,0x41,0x51,0x21,0x5E},{0x7F,0x09,0x19,0x29,0x46},
    {0x46,0x49,0x49,0x49,0x31},{0x01,0x01,0x7F,0x01,0x01},
    {0x3F,0x40,0x40,0x40,0x3F},{0x1F,0x20,0x40,0x20,0x1F},
    {0x3F,0x40,0x38,0x40,0x3F},{0x63,0x14,0x08,0x14,0x63},
    {0x07,0x08,0x70,0x08,0x07},{0x61,0x51,0x49,0x45,0x43},
};

static const uint8_t *font_get(char c)
{
    if (c >= '0' && c <= '9') return font_digits[(uint8_t)(c - '0')];
    if (c >= 'A' && c <= 'Z') return font_upper[(uint8_t)(c - 'A')];
    if (c >= 'a' && c <= 'z') return font_upper[(uint8_t)(c - 'a')];
    return font_space;
}

static void oled_char(char c)
{
    uint8_t data[6];
    const uint8_t *g = font_get(c);

    data[0] = g[0]; data[1] = g[1]; data[2] = g[2];
    data[3] = g[3]; data[4] = g[4]; data[5] = 0U;
    (void)oled_data(data, sizeof(data));
}

static void oled_string(uint8_t page, uint8_t col, const char *text)
{
    if (!oled_ready) {
        return;
    }

    oled_set_pos(page, col);
    while (*text != '\0') {
        oled_char(*text++);
    }
}

static uint8_t oled_init(void)
{
    static const uint8_t init_cmds[] = {
        0xAE,0x20,0x02,0xB0,0xC8,0x00,0x10,0x40,
        0x81,0x7F,0xA1,0xA6,0xA8,0x3F,0xA4,0xD3,
        0x00,0xD5,0x80,0xD9,0xF1,0xDA,0x12,0xDB,
        0x40,0x8D,0x14,0xAF,
    };
    uint8_t i;

    if (!mcu_port_i2c_init(&i2c_bus)) return 0U;
    mcu_port_delay_ms(80U);
    if (!mcu_port_i2c_is_ready(&i2c_bus, OLED_ADDR7)) return 0U;
    for (i = 0U; i < sizeof(init_cmds); ++i) {
        if (!oled_cmd(init_cmds[i])) return 0U;
    }
    oled_ready = 1U;
    oled_clear();
    return 1U;
}

static void i2c_send_scan_result(uint8_t found_count, const uint8_t *addresses)
{
    uint8_t data[8] = {0};
    uint8_t i;

    data[0] = I2C_STATUS_SCAN;
    data[1] = found_count;
    for (i = 0U; (i < 6U) && (i < found_count); ++i) {
        data[(uint8_t)(2U + i)] = addresses[i];
    }
    (void)bsp_can_send_std(CAN_ID_STATUS(NODE_ID), data, sizeof(data));
}

static void i2c_scan(void)
{
    uint8_t found[6] = {0};
    uint8_t found_count = 0U;
    uint8_t report_count = 0U;
    uint8_t addr;
    char line[64];

    vibe_println("I2C scan requested");
    if (!mcu_port_i2c_init(&i2c_bus)) {
        vibe_println("I2C scan init failed, try recovery");
        (void)mcu_port_i2c_recover_bus(&i2c_bus);
    }

    for (addr = 0x03U; addr <= 0x77U; ++addr) {
        if (mcu_port_i2c_is_ready(&i2c_bus, addr)) {
            (void)snprintf(line, sizeof(line), "I2C found addr=0x%02X", addr);
            vibe_println(line);
            if (report_count < sizeof(found)) {
                found[report_count] = addr;
                report_count++;
            }
            found_count++;
        }
    }

    (void)snprintf(line, sizeof(line), "I2C scan found_count=%u", found_count);
    vibe_println(line);
    i2c_send_scan_result(found_count, found);
    (void)node_send_ack(NODE_ID, DEVICE_TYPE, I2C_SCAN_CMD, 1U);
}

static void mpu_send_sample(void)
{
    bsp_mpu6050_raw_t raw;
    uint8_t frame[8] = {0};

    if (!can_ready || !mpu_ready) {
        return;
    }

    if (!bsp_mpu6050_read_raw(&raw)) {
        vibe_println("MPU6050 read failed");
        return;
    }

    frame[0] = MPU_STATUS_ACCEL;
    frame[1] = (uint8_t)(mpu_sample_count & 0xFFU);
    put_i16_le(frame, 2U, raw.accel_x);
    put_i16_le(frame, 4U, raw.accel_y);
    put_i16_le(frame, 6U, raw.accel_z);
    (void)bsp_can_send_std(CAN_ID_STATUS(NODE_ID), frame, sizeof(frame));

    frame[0] = MPU_STATUS_GYRO;
    frame[1] = (uint8_t)(mpu_sample_count & 0xFFU);
    put_i16_le(frame, 2U, raw.gyro_x);
    put_i16_le(frame, 4U, raw.gyro_y);
    put_i16_le(frame, 6U, raw.gyro_z);
    (void)bsp_can_send_std(CAN_ID_STATUS(NODE_ID), frame, sizeof(frame));

    frame[0] = MPU_STATUS_TEMP;
    frame[1] = (uint8_t)(mpu_sample_count & 0xFFU);
    put_i16_le(frame, 2U, raw.temperature);
    frame[4] = 'M'; frame[5] = 'P'; frame[6] = 'U'; frame[7] = 'T';
    (void)bsp_can_send_std(CAN_ID_STATUS(NODE_ID), frame, sizeof(frame));

    mpu_sample_count++;
}

static void handle_oled_text(const bsp_can_frame_t *frame)
{
    char text[6];
    uint8_t i;

    if (frame->dlc < 4U) {
        (void)node_send_ack(NODE_ID, DEVICE_TYPE, OLED_TEXT_CMD, 0U);
        return;
    }

    for (i = 0U; i < 5U; ++i) {
        text[i] = (char)frame->data[(uint8_t)(3U + i)];
        if (text[i] == '\0') {
            break;
        }
    }
    text[5] = '\0';
    oled_string(frame->data[1], frame->data[2], text);
    (void)node_send_ack(NODE_ID, DEVICE_TYPE, OLED_TEXT_CMD, oled_ready);
}

static void can_command_task(void)
{
    bsp_can_frame_t frame;
    uint8_t whoami = 0U;

    if (!can_ready) {
        return;
    }

    while (bsp_can_receive(&frame)) {
        if (frame.id != CAN_ID_I2C_CMD(NODE_ID)) {
            continue;
        }

        if (frame.dlc < 1U) {
            (void)node_send_ack(NODE_ID, DEVICE_TYPE, 0U, 0U);
            continue;
        }

        switch (frame.data[0]) {
        case I2C_SCAN_CMD:
            i2c_scan();
            break;
        case OLED_INIT_CMD:
            vibe_println("OLED init requested");
            oled_ready = oled_init();
            vibe_println(oled_ready ? "OLED init ok" : "OLED init failed");
            (void)node_send_ack(NODE_ID, DEVICE_TYPE, OLED_INIT_CMD, oled_ready);
            break;
        case OLED_CLEAR_CMD:
            vibe_println("OLED clear requested");
            oled_clear();
            (void)node_send_ack(NODE_ID, DEVICE_TYPE, OLED_CLEAR_CMD, oled_ready);
            break;
        case OLED_TEXT_CMD:
            vibe_println("OLED text requested");
            handle_oled_text(&frame);
            break;
        case MPU_INIT_CMD:
            vibe_println("MPU6050 init requested");
            mpu_ready = bsp_mpu6050_init();
            if (bsp_mpu6050_whoami(&whoami)) {
                char line[64];
                (void)snprintf(line, sizeof(line), "MPU6050 ready=%u WHO_AM_I=0x%02X", mpu_ready, whoami);
                vibe_println(line);
            } else {
                vibe_println("MPU6050 init failed, WHO_AM_I read failed");
            }
            (void)node_send_ack(NODE_ID, DEVICE_TYPE, MPU_INIT_CMD, mpu_ready);
            break;
        case MPU_READ_CMD:
            vibe_println("MPU6050 read requested");
            mpu_send_sample();
            (void)node_send_ack(NODE_ID, DEVICE_TYPE, MPU_READ_CMD, mpu_ready);
            break;
        case I2C_WRITE_CMD:
        case I2C_READ_CMD:
            vibe_println("Generic I2C write/read command not implemented yet");
            (void)node_send_ack(NODE_ID, DEVICE_TYPE, frame.data[0], 0U);
            break;
        default:
            (void)node_send_ack(NODE_ID, DEVICE_TYPE, frame.data[0], 0U);
            break;
        }
    }
}

void app_setup(void)
{
    uint8_t whoami = 0U;
    char line[80];

    vibe_serial_begin(115200U);
    mcu_port_delay_ms(300U);
    (void)mcu_port_i2c_init(&i2c_bus);

    can_ready = bsp_can_init_50k();
    oled_ready = oled_init();
    vibe_println(oled_ready ? "OLED init ok" : "OLED init failed");
    mpu_ready = bsp_mpu6050_init();
    if (bsp_mpu6050_whoami(&whoami)) {
        (void)snprintf(line, sizeof(line), "MPU6050 ready=%u WHO_AM_I=0x%02X", mpu_ready, whoami);
        vibe_println(line);
    } else {
        vibe_println("MPU6050 init failed, WHO_AM_I read failed");
    }

    heartbeat_task();

    (void)vibe_task_every_ms(HEARTBEAT_MS, heartbeat_task);
    (void)vibe_task_every_ms(CAN_POLL_MS, can_command_task);
    (void)vibe_task_every_ms(MPU_REPORT_MS, mpu_send_sample);
}


