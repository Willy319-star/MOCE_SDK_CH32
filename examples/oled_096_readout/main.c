#include "board_pins.h"
#include "mcu_port_i2c.h"
#include "mcu_port_time.h"
#include "vibe_api.h"
#include "vibe_runtime.h"

#include <stdio.h>
#include <string.h>

#define OLED_I2C_ADDR_8BIT 0x7AU
#define OLED_I2C_ADDR_7BIT (OLED_I2C_ADDR_8BIT >> 1)
#define OLED_TEXT_PERIOD_MS 1000U

static mcu_port_i2c_t oled_i2c = {
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

static uint8_t oled_ready;
static uint32_t oled_count;

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
        if (!mcu_port_i2c_write(&oled_i2c, OLED_I2C_ADDR_7BIT, buffer, (uint16_t)(chunk + 1U))) {
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

    memset(zeros, 0, sizeof(zeros));
    for (page = 0U; page < 8U; ++page) {
        oled_set_pos(page, 0U);
        (void)oled_data(zeros, sizeof(zeros));
    }
}

static const uint8_t font_space[5] = {0x00, 0x00, 0x00, 0x00, 0x00};
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
    const uint8_t *glyph = font_get(c);
    data[0] = glyph[0]; data[1] = glyph[1]; data[2] = glyph[2];
    data[3] = glyph[3]; data[4] = glyph[4]; data[5] = 0x00U;
    (void)oled_data(data, sizeof(data));
}

static void oled_string(uint8_t page, uint8_t col, const char *text)
{
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

    if (!mcu_port_i2c_init(&oled_i2c)) return 0U;
    mcu_port_delay_ms(80U);
    if (!mcu_port_i2c_is_ready(&oled_i2c, OLED_I2C_ADDR_7BIT)) return 0U;
    for (i = 0U; i < sizeof(init_cmds); ++i) {
        if (!oled_cmd(init_cmds[i])) return 0U;
    }
    oled_clear();
    return 1U;
}

static void oled_task(void)
{
    char line[24];

    if (!oled_ready) {
        vibe_println("OLED not ready");
        return;
    }

    oled_clear();
    oled_string(0U, 0U, "MOCE OLED");
    oled_string(2U, 0U, "ADDR 0X7A");
    oled_string(4U, 0U, "I2C PB6 PB7");
    (void)snprintf(line, sizeof(line), "COUNT %lu", (unsigned long)oled_count);
    oled_string(6U, 0U, line);
    vibe_println(line);
    ++oled_count;
    vibe_led_toggle();
}

void app_setup(void)
{
    vibe_serial_begin(115200U);
    mcu_port_delay_ms(300U);
    vibe_println("MOCE SDK CH32V203G6U6 0.96 OLED readout");
    vibe_println("I2C1: SCL=PB6 SDA=PB7, OLED addr8=0x7A addr7=0x3D");

    oled_ready = oled_init();
    vibe_println(oled_ready ? "OLED init ok" : "OLED init failed");
    (void)vibe_task_every_ms(OLED_TEXT_PERIOD_MS, oled_task);
}
