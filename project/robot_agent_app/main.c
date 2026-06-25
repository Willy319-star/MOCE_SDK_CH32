#include "board_pins.h"
#include "mcu_port_i2c.h"
#include "mcu_port_time.h"
#include "vibe_api.h"
#include "vibe_runtime.h"

/*
 * OLED SSD1306 0.96" I2C
 * Address: 0x3C (7-bit) -> 0x78 (8-bit write)
 * Based on examples/oled_096_readout
 */

#define OLED_ADDR_7BIT 0x3CU
#define OLED_ADDR_WRITE (OLED_ADDR_7BIT << 1)

#define OLED_WIDTH  128U
#define OLED_HEIGHT  64U
#define OLED_PAGES   (OLED_HEIGHT / 8U)

/* SSD1306 commands */
#define OLED_CMD_SETCONTRAST      0x81U
#define OLED_CMD_DISPLAYALLON_RESUME 0xA4U
#define OLED_CMD_DISPLAYALLON     0xA5U
#define OLED_CMD_NORMALDISPLAY    0xA6U
#define OLED_CMD_INVERTDISPLAY    0xA7U
#define OLED_CMD_DISPLAYOFF       0xAEU
#define OLED_CMD_DISPLAYON        0xAFU
#define OLED_CMD_SETDISPLAYOFFSET 0xD3U
#define OLED_CMD_SETCOMPINS       0xDAU
#define OLED_CMD_SETVCOMDETECT    0xDBU
#define OLED_CMD_SETDISPLAYCLOCKDIV 0xD5U
#define OLED_CMD_SETPRECHARGE     0xD9U
#define OLED_CMD_SETMULTIPLEX     0xA8U
#define OLED_CMD_SETLOWCOLUMN     0x00U
#define OLED_CMD_SETHIGHCOLUMN    0x10U
#define OLED_CMD_SETSTARTLINE     0x40U
#define OLED_CMD_MEMORYMODE       0x20U
#define OLED_CMD_COLUMNADDR       0x21U
#define OLED_CMD_PAGEADDR         0x22U
#define OLED_CMD_COMSCANINC       0xC0U
#define OLED_CMD_COMSCANDEC       0xC8U
#define OLED_CMD_SEGREMAP         0xA0U
#define OLED_CMD_CHARGEPUMP       0x8DU

/* 5x7 font: each character is 5 bytes wide, 8 pixels tall */
static const uint8_t font_5x7[][5] = {
    {0x00, 0x00, 0x00, 0x00, 0x00}, /* space */
    {0x00, 0x00, 0x5F, 0x00, 0x00}, /* ! */
    {0x00, 0x07, 0x00, 0x07, 0x00}, /* " */
    {0x14, 0x7F, 0x14, 0x7F, 0x14}, /* # */
    {0x24, 0x2A, 0x7F, 0x2A, 0x12}, /* $ */
    {0x23, 0x13, 0x08, 0x64, 0x62}, /* % */
    {0x36, 0x49, 0x55, 0x22, 0x50}, /* & */
    {0x00, 0x05, 0x03, 0x00, 0x00}, /* ' */
    {0x00, 0x1C, 0x22, 0x41, 0x00}, /* ( */
    {0x00, 0x41, 0x22, 0x1C, 0x00}, /* ) */
    {0x08, 0x2A, 0x1C, 0x2A, 0x08}, /* * */
    {0x08, 0x08, 0x3E, 0x08, 0x08}, /* + */
    {0x00, 0x50, 0x30, 0x00, 0x00}, /* , */
    {0x08, 0x08, 0x08, 0x08, 0x08}, /* - */
    {0x00, 0x60, 0x60, 0x00, 0x00}, /* . */
    {0x20, 0x10, 0x08, 0x04, 0x02}, /* / */
    {0x3E, 0x51, 0x49, 0x45, 0x3E}, /* 0 */
    {0x00, 0x42, 0x7F, 0x40, 0x00}, /* 1 */
    {0x42, 0x61, 0x51, 0x49, 0x46}, /* 2 */
    {0x21, 0x41, 0x45, 0x4B, 0x31}, /* 3 */
    {0x18, 0x14, 0x12, 0x7F, 0x10}, /* 4 */
    {0x27, 0x45, 0x45, 0x45, 0x39}, /* 5 */
    {0x3C, 0x4A, 0x49, 0x49, 0x30}, /* 6 */
    {0x01, 0x71, 0x09, 0x05, 0x03}, /* 7 */
    {0x36, 0x49, 0x49, 0x49, 0x36}, /* 8 */
    {0x06, 0x49, 0x49, 0x29, 0x1E}, /* 9 */
    {0x00, 0x36, 0x36, 0x00, 0x00}, /* : */
    {0x00, 0x56, 0x36, 0x00, 0x00}, /* ; */
    {0x00, 0x08, 0x14, 0x22, 0x41}, /* < */
    {0x14, 0x14, 0x14, 0x14, 0x14}, /* = */
    {0x41, 0x22, 0x14, 0x08, 0x00}, /* > */
    {0x02, 0x01, 0x51, 0x09, 0x06}, /* ? */
    {0x32, 0x49, 0x79, 0x41, 0x3E}, /* @ */
    {0x7E, 0x11, 0x11, 0x11, 0x7E}, /* A */
    {0x7F, 0x49, 0x49, 0x49, 0x36}, /* B */
    {0x3E, 0x41, 0x41, 0x41, 0x22}, /* C */
    {0x7F, 0x41, 0x41, 0x22, 0x1C}, /* D */
    {0x7F, 0x49, 0x49, 0x49, 0x41}, /* E */
    {0x7F, 0x09, 0x09, 0x01, 0x01}, /* F */
    {0x3E, 0x41, 0x41, 0x51, 0x32}, /* G */
    {0x7F, 0x08, 0x08, 0x08, 0x7F}, /* H */
    {0x00, 0x41, 0x7F, 0x41, 0x00}, /* I */
    {0x20, 0x40, 0x41, 0x3F, 0x01}, /* J */
    {0x7F, 0x08, 0x14, 0x22, 0x41}, /* K */
    {0x7F, 0x40, 0x40, 0x40, 0x40}, /* L */
    {0x7F, 0x02, 0x04, 0x02, 0x7F}, /* M */
    {0x7F, 0x04, 0x08, 0x10, 0x7F}, /* N */
    {0x3E, 0x41, 0x41, 0x41, 0x3E}, /* O */
    {0x7F, 0x09, 0x09, 0x09, 0x06}, /* P */
    {0x3E, 0x41, 0x51, 0x21, 0x5E}, /* Q */
    {0x7F, 0x09, 0x19, 0x29, 0x46}, /* R */
    {0x46, 0x49, 0x49, 0x49, 0x31}, /* S */
    {0x01, 0x01, 0x7F, 0x01, 0x01}, /* T */
    {0x3F, 0x40, 0x40, 0x40, 0x3F}, /* U */
    {0x1F, 0x20, 0x40, 0x20, 0x1F}, /* V */
    {0x7F, 0x20, 0x18, 0x20, 0x7F}, /* W */
    {0x63, 0x14, 0x08, 0x14, 0x63}, /* X */
    {0x03, 0x04, 0x78, 0x04, 0x03}, /* Y */
    {0x61, 0x51, 0x49, 0x45, 0x43}, /* Z */
};

static mcu_port_i2c_t oled_i2c = {
    .i2c        = BOARD_I2C1_INSTANCE,
    .scl_port   = BOARD_I2C1_SCL_GPIO_PORT,
    .scl_pin    = BOARD_I2C1_SCL_GPIO_PIN,
    .sda_port   = BOARD_I2C1_SDA_GPIO_PORT,
    .sda_pin    = BOARD_I2C1_SDA_GPIO_PIN,
    .gpio_clock = BOARD_I2C1_GPIO_CLK,
    .i2c_clock  = BOARD_I2C1_CLK,
    .speed_hz   = 100000U,
    .initialized = 0U
};

static void oled_write_cmd(uint8_t cmd)
{
    uint8_t buf[2] = {0x00U, cmd}; /* Co=0, D/C#=0 */
    (void)mcu_port_i2c_write(&oled_i2c, OLED_ADDR_WRITE, buf, 2U);
}

static void oled_write_data(uint8_t data)
{
    uint8_t buf[2] = {0x40U, data}; /* Co=0, D/C#=1 */
    (void)mcu_port_i2c_write(&oled_i2c, OLED_ADDR_WRITE, buf, 2U);
}

static void oled_write_data_buf(const uint8_t *data, uint16_t len)
{
    uint16_t i;
    for (i = 0U; i < len; i++) {
        oled_write_data(data[i]);
    }
}

static void oled_init(void)
{
    mcu_port_delay_ms(100U);

    oled_write_cmd(OLED_CMD_DISPLAYOFF);
    oled_write_cmd(OLED_CMD_SETDISPLAYCLOCKDIV);
    oled_write_cmd(0x80U);
    oled_write_cmd(OLED_CMD_SETMULTIPLEX);
    oled_write_cmd(OLED_HEIGHT - 1U);
    oled_write_cmd(OLED_CMD_SETDISPLAYOFFSET);
    oled_write_cmd(0x00U);
    oled_write_cmd(OLED_CMD_SETSTARTLINE | 0x00U);
    oled_write_cmd(OLED_CMD_CHARGEPUMP);
    oled_write_cmd(0x14U);
    oled_write_cmd(OLED_CMD_MEMORYMODE);
    oled_write_cmd(0x00U);
    oled_write_cmd(OLED_CMD_SEGREMAP | 0x01U);
    oled_write_cmd(OLED_CMD_COMSCANDEC);
    oled_write_cmd(OLED_CMD_SETCOMPINS);
    oled_write_cmd(0x12U);
    oled_write_cmd(OLED_CMD_SETCONTRAST);
    oled_write_cmd(0x7FU);
    oled_write_cmd(OLED_CMD_SETPRECHARGE);
    oled_write_cmd(0xF1U);
    oled_write_cmd(OLED_CMD_SETVCOMDETECT);
    oled_write_cmd(0x40U);
    oled_write_cmd(OLED_CMD_DISPLAYALLON_RESUME);
    oled_write_cmd(OLED_CMD_NORMALDISPLAY);
    oled_write_cmd(OLED_CMD_DISPLAYON);
}

static void oled_clear(void)
{
    uint8_t page;
    for (page = 0U; page < OLED_PAGES; page++) {
        oled_write_cmd(0xB0U | page);
        oled_write_cmd(0x00U);
        oled_write_cmd(0x10U);
        uint16_t col;
        for (col = 0U; col < OLED_WIDTH; col++) {
            oled_write_data(0x00U);
        }
    }
}

static void oled_set_cursor(uint8_t page, uint8_t col)
{
    oled_write_cmd(0xB0U | page);
    oled_write_cmd(0x00U | (col & 0x0FU));
    oled_write_cmd(0x10U | ((col >> 4) & 0x0FU));
}

static void oled_write_char(char ch)
{
    uint8_t idx;
    if (ch >= ' ' && ch <= 'Z') {
        idx = (uint8_t)(ch - ' ');
    } else {
        idx = 0U;
    }
    oled_write_data_buf(font_5x7[idx], 5U);
    oled_write_data(0x00U); /* column spacing */
}

static void oled_write_string(const char *str)
{
    while (*str != '\0') {
        oled_write_char(*str);
        str++;
    }
}

static void app_task(void)
{
    vibe_led_toggle();
}

void app_setup(void)
{
    vibe_serial_begin(115200U);
    mcu_port_delay_ms(300U);
    vibe_println("MOCE SDK CH32 generated app");
    vibe_println("requirement: 帮我在屏幕里显示carry yyds!");

    /* Initialize I2C for OLED */
    if (mcu_port_i2c_init(&oled_i2c) != 0U) {
        vibe_println("I2C init OK");
    } else {
        vibe_println("I2C init FAIL");
        return;
    }

    /* Check OLED presence */
    if (mcu_port_i2c_is_ready(&oled_i2c, OLED_ADDR_7BIT) == 0U) {
        vibe_println("OLED not found at 0x3C");
        return;
    }
    vibe_println("OLED detected");

    /* Initialize OLED */
    oled_init();
    oled_clear();

    /* Display text on page 2 (row ~16) */
    oled_set_cursor(2U, 20U);
    oled_write_string("carry yyds!");

    vibe_println("OLED display done");

    (void)vibe_task_every_ms(1000U, app_task);
}