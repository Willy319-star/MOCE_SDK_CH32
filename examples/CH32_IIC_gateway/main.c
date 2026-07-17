#include "bsp_can.h"
#include "bsp_mpu6050.h"
#include "board_pins.h"
#include "ch32v20x_gpio.h"
#include "ch32v20x_rcc.h"
#include "mcu_port_i2c.h"
#include "mcu_port_time.h"
#include "vibe_api.h"
#include "vibe_runtime.h"

#include <stdio.h>
#include <string.h>

#define CAN_ID_MPU_ACCEL      0x350U
#define CAN_ID_MPU_GYRO       0x351U
#define CAN_ID_MPU_TEMP       0x352U
#define CAN_ID_OLED_CLEAR     0x410U
#define CAN_ID_OLED_TEXT      0x411U
#define CAN_ID_PWM_SET        0x420U
#define CAN_ID_GATEWAY_ACK    0x500U

#define OLED_ADDR8            0x78U
#define OLED_ADDR7            (OLED_ADDR8 >> 1)
#define MPU_REPORT_PERIOD_MS  500U
#define CAN_POLL_PERIOD_MS    20U

#define PWM_DEFAULT_FREQ_HZ   1000U
#define PWM_TIMER_CLOCK_HZ    96000000UL
#define PWM_PERIOD_TICKS      1000U

#define MOTOR_A_PWM_PORT      GPIOA
#define MOTOR_A_PWM_PIN       GPIO_Pin_7
#define MOTOR_A_DIR_PORT      GPIOA
#define MOTOR_A_DIR_PIN       GPIO_Pin_5
#define MOTOR_B_PWM_PORT      GPIOA
#define MOTOR_B_PWM_PIN       GPIO_Pin_6
#define MOTOR_B_DIR_PORT      GPIOA
#define MOTOR_B_DIR_PIN       GPIO_Pin_4
#define MOTOR_GPIO_CLK        RCC_APB2Periph_GPIOA

static uint8_t can_ready;
static uint8_t mpu_ready;
static uint8_t oled_ready;
static uint16_t sample_count;
static uint16_t motor_a_duty_permille;
static uint16_t motor_b_duty_permille;

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

static void put_u16_le(uint8_t *data, uint8_t off, uint16_t value)
{
    data[off] = (uint8_t)(value & 0xFFU);
    data[(uint8_t)(off + 1U)] = (uint8_t)((value >> 8U) & 0xFFU);
}

static uint16_t get_u16_le(const uint8_t *data, uint8_t off)
{
    return (uint16_t)((uint16_t)data[off] | ((uint16_t)data[(uint8_t)(off + 1U)] << 8U));
}

static void put_i16_le(uint8_t *data, uint8_t off, int16_t value)
{
    put_u16_le(data, off, (uint16_t)value);
}

static void send_ack(uint8_t code, uint16_t source_id, uint8_t result)
{
    uint8_t data[8] = {0};

    data[0] = code;
    put_u16_le(data, 1U, source_id);
    data[3] = result;
    (void)bsp_can_send_std(CAN_ID_GATEWAY_ACK, data, sizeof(data));
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
        if (!mcu_port_i2c_write(&oled_i2c, OLED_ADDR7, buffer, (uint16_t)(chunk + 1U))) {
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

static const uint8_t font_space[5] = {0,0,0,0,0};
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

    if (!mcu_port_i2c_init(&oled_i2c)) return 0U;
    mcu_port_delay_ms(80U);
    if (!mcu_port_i2c_is_ready(&oled_i2c, OLED_ADDR7)) return 0U;
    for (i = 0U; i < sizeof(init_cmds); ++i) {
        if (!oled_cmd(init_cmds[i])) return 0U;
    }
    oled_ready = 1U;
    oled_clear();
    return 1U;
}

static void motor_gpio_init(void)
{
    GPIO_InitTypeDef gpio = {0};

    RCC_APB2PeriphClockCmd(MOTOR_GPIO_CLK | RCC_APB2Periph_AFIO, ENABLE);
    AFIO->PCFR1 &= ~(uint32_t)AFIO_PCFR1_TIM3_REMAP;

    gpio.GPIO_Pin = MOTOR_A_DIR_PIN | MOTOR_B_DIR_PIN;
    gpio.GPIO_Mode = GPIO_Mode_Out_PP;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOA, &gpio);

    /* DIR low means forward for both motors. */
    GPIO_ResetBits(MOTOR_A_DIR_PORT, MOTOR_A_DIR_PIN);
    GPIO_ResetBits(MOTOR_B_DIR_PORT, MOTOR_B_DIR_PIN);
}

static void motor_pwm_init(void)
{
    GPIO_InitTypeDef gpio = {0};
    uint32_t prescaler;

    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM3, ENABLE);

    /* TIM3_CH1 = PA6 -> motor B PWM2, TIM3_CH2 = PA7 -> motor A PWM1. */
    gpio.GPIO_Pin = MOTOR_B_PWM_PIN | MOTOR_A_PWM_PIN;
    gpio.GPIO_Mode = GPIO_Mode_AF_PP;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOA, &gpio);

    prescaler = (PWM_TIMER_CLOCK_HZ / (PWM_DEFAULT_FREQ_HZ * PWM_PERIOD_TICKS));
    if (prescaler == 0U) {
        prescaler = 1U;
    }

    TIM3->CTLR1 = 0U;
    TIM3->PSC = (uint16_t)(prescaler - 1U);
    TIM3->ATRLR = (uint16_t)(PWM_PERIOD_TICKS - 1U);
    TIM3->CH1CVR = 0U;
    TIM3->CH2CVR = 0U;
    TIM3->CHCTLR1 = 0x6868U; /* PWM mode 1 + preload on CH1 and CH2. */
    TIM3->CCER = (uint16_t)((TIM3->CCER & ~(uint16_t)0x0033U) | 0x0011U);
    TIM3->SWEVGR |= 0x0001U;
    TIM3->CTLR1 |= 0x0081U;
}

static void motors_init(void)
{
    motor_gpio_init();
    motor_pwm_init();
    motor_a_duty_permille = 0U;
    motor_b_duty_permille = 0U;
}

static void motor_set_duty(uint8_t channel, uint16_t duty_permille)
{
    uint16_t pulse;

    if (duty_permille > 1000U) {
        duty_permille = 1000U;
    }

    pulse = (uint16_t)((PWM_PERIOD_TICKS * duty_permille) / 1000U);

    if (channel == 0U) {
        motor_a_duty_permille = duty_permille;
        GPIO_ResetBits(MOTOR_A_DIR_PORT, MOTOR_A_DIR_PIN);
        TIM3->CH2CVR = pulse;
    } else if (channel == 1U) {
        motor_b_duty_permille = duty_permille;
        GPIO_ResetBits(MOTOR_B_DIR_PORT, MOTOR_B_DIR_PIN);
        TIM3->CH1CVR = pulse;
    }
}

static void mpu_report_task(void)
{
    bsp_mpu6050_raw_t raw;
    uint8_t frame[8];

    if (!mpu_ready || !can_ready) {
        return;
    }

    if (!bsp_mpu6050_read_raw(&raw)) {
        return;
    }

    put_u16_le(frame, 0U, sample_count);
    put_i16_le(frame, 2U, raw.accel_x);
    put_i16_le(frame, 4U, raw.accel_y);
    put_i16_le(frame, 6U, raw.accel_z);
    (void)bsp_can_send_std(CAN_ID_MPU_ACCEL, frame, sizeof(frame));

    put_u16_le(frame, 0U, sample_count);
    put_i16_le(frame, 2U, raw.gyro_x);
    put_i16_le(frame, 4U, raw.gyro_y);
    put_i16_le(frame, 6U, raw.gyro_z);
    (void)bsp_can_send_std(CAN_ID_MPU_GYRO, frame, sizeof(frame));

    put_u16_le(frame, 0U, sample_count);
    put_i16_le(frame, 2U, raw.temperature);
    frame[4] = 'M'; frame[5] = 'P'; frame[6] = 'U'; frame[7] = 'T';
    (void)bsp_can_send_std(CAN_ID_MPU_TEMP, frame, sizeof(frame));

    ++sample_count;
}

static void handle_oled_text(const bsp_can_frame_t *frame)
{
    char text[7];
    uint8_t i;

    if (frame->dlc < 3U) {
        send_ack(0x11U, CAN_ID_OLED_TEXT, 0U);
        return;
    }

    for (i = 0U; i < 6U; ++i) {
        text[i] = (char)frame->data[(uint8_t)(i + 2U)];
        if (text[i] == '\0') {
            break;
        }
    }
    text[6] = '\0';
    oled_string(frame->data[0], frame->data[1], text);
    send_ack(0x11U, CAN_ID_OLED_TEXT, oled_ready);
}

static void handle_pwm_set(const bsp_can_frame_t *frame)
{
    uint16_t duty;
    uint8_t channel;

    if (frame->dlc < 3U) {
        send_ack(0x20U, CAN_ID_PWM_SET, 0U);
        return;
    }

    channel = frame->data[0];
    duty = get_u16_le(frame->data, 1U);
    if (channel > 1U) {
        send_ack(0x20U, CAN_ID_PWM_SET, 0U);
        return;
    }

    motor_set_duty(channel, duty);
    send_ack(0x20U, CAN_ID_PWM_SET, 1U);
}

static void can_command_task(void)
{
    bsp_can_frame_t frame;

    if (!can_ready) {
        return;
    }

    while (bsp_can_receive(&frame)) {
        switch (frame.id) {
        case CAN_ID_OLED_CLEAR:
            oled_clear();
            send_ack(0x10U, CAN_ID_OLED_CLEAR, oled_ready);
            break;
        case CAN_ID_OLED_TEXT:
            handle_oled_text(&frame);
            break;
        case CAN_ID_PWM_SET:
            handle_pwm_set(&frame);
            break;
        default:
            break;
        }
    }
}

void app_setup(void)
{
    uint8_t whoami = 0U;
    char line[128];

    vibe_serial_begin(115200U);
    mcu_port_delay_ms(300U);
    vibe_println("MOCE SDK CH32V203G6U6 gateway");
    vibe_println("CAN 50k: MPU report 0x350-0x352, OLED cmd 0x410/0x411, PWM cmd 0x420");
    vibe_println("Motor A: PA7 TIM3_CH2 PWM + PA5 DIR low, Motor B: PA6 TIM3_CH1 PWM + PA4 DIR low");
    vibe_println("CAN 0x420: channel 0->A, channel 1->B, duty 0..1000 permille");

    motors_init();

    can_ready = bsp_can_init_50k();
    vibe_println(can_ready ? "CAN init ok" : "CAN init failed");

    mpu_ready = bsp_mpu6050_init();
    if (bsp_mpu6050_whoami(&whoami)) {
        (void)snprintf(line, sizeof(line), "MPU6050 ready=%u WHO_AM_I=0x%02X", mpu_ready, whoami);
        vibe_println(line);
    } else {
        vibe_println("MPU6050 not detected");
    }

    oled_ready = oled_init();
    vibe_println(oled_ready ? "OLED init ok" : "OLED not detected");

    (void)vibe_task_every_ms(MPU_REPORT_PERIOD_MS, mpu_report_task);
    (void)vibe_task_every_ms(CAN_POLL_PERIOD_MS, can_command_task);
}


