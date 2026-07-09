#include "bsp_can.h"
#include "ch32v20x_gpio.h"
#include "ch32v20x_rcc.h"
#include "debug.h"
#include "vibe_api.h"
#include "vibe_runtime.h"

#include <stdio.h>

#define CAN_ID_SERVO_SET      0x430U
#define CAN_ID_GATEWAY_ACK    0x500U
#define CAN_POLL_PERIOD_MS    20U
#define SERVO_REPORT_MS       1000U

#define SERVO_PWM_PORT_A      GPIOA
#define SERVO_PWM_PINS_A      (GPIO_Pin_6 | GPIO_Pin_7)
#define SERVO_PWM_PORT_B      GPIOB
#define SERVO_PWM_PINS_B      (GPIO_Pin_6 | GPIO_Pin_7)
#define SERVO_GPIO_CLK        (RCC_APB2Periph_GPIOA | RCC_APB2Periph_GPIOB)

#define SERVO_PERIOD_US       20000U
#define SERVO_MIN_PULSE_US    1000U
#define SERVO_MID_PULSE_US    1500U
#define SERVO_MAX_PULSE_US    2000U
#define SERVO_MAX_CHANNELS    4U

static uint8_t can_ready;
static uint16_t servo_angle[SERVO_MAX_CHANNELS] = {90U, 90U, 90U, 90U};
static uint16_t servo_pulse_us[SERVO_MAX_CHANNELS] = {
    SERVO_MID_PULSE_US, SERVO_MID_PULSE_US, SERVO_MID_PULSE_US, SERVO_MID_PULSE_US
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

static void send_ack(uint8_t code, uint16_t source_id, uint8_t result)
{
    uint8_t data[8] = {0};

    data[0] = code;
    put_u16_le(data, 1U, source_id);
    data[3] = result;
    (void)bsp_can_send_std(CAN_ID_GATEWAY_ACK, data, sizeof(data));
}

static uint16_t servo_angle_to_pulse_us(uint16_t angle)
{
    uint32_t pulse;

    if (angle > 180U) {
        angle = 180U;
    }

    pulse = SERVO_MIN_PULSE_US;
    pulse += ((uint32_t)(SERVO_MAX_PULSE_US - SERVO_MIN_PULSE_US) * angle) / 180UL;
    return (uint16_t)pulse;
}

static void servo_gpio_init(void)
{
    GPIO_InitTypeDef gpio = {0};

    RCC_APB2PeriphClockCmd(SERVO_GPIO_CLK, ENABLE);

    gpio.GPIO_Mode = GPIO_Mode_Out_PP;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;

    gpio.GPIO_Pin = SERVO_PWM_PINS_A;
    GPIO_Init(SERVO_PWM_PORT_A, &gpio);

    gpio.GPIO_Pin = SERVO_PWM_PINS_B;
    GPIO_Init(SERVO_PWM_PORT_B, &gpio);

    GPIO_ResetBits(SERVO_PWM_PORT_A, SERVO_PWM_PINS_A);
    GPIO_ResetBits(SERVO_PWM_PORT_B, SERVO_PWM_PINS_B);
}

static void servo_set_pin(uint8_t channel, uint8_t high)
{
    switch (channel) {
    case 0U:
        if (high) GPIO_SetBits(GPIOA, GPIO_Pin_6); else GPIO_ResetBits(GPIOA, GPIO_Pin_6);
        break;
    case 1U:
        if (high) GPIO_SetBits(GPIOA, GPIO_Pin_7); else GPIO_ResetBits(GPIOA, GPIO_Pin_7);
        break;
    case 2U:
        if (high) GPIO_SetBits(GPIOB, GPIO_Pin_6); else GPIO_ResetBits(GPIOB, GPIO_Pin_6);
        break;
    case 3U:
        if (high) GPIO_SetBits(GPIOB, GPIO_Pin_7); else GPIO_ResetBits(GPIOB, GPIO_Pin_7);
        break;
    default:
        break;
    }
}

static void servo_output_all_frame(void)
{
    uint8_t ch;

    for (ch = 0U; ch < SERVO_MAX_CHANNELS; ++ch) {
        servo_set_pin(ch, 1U);
        Delay_Us(servo_pulse_us[ch]);
        servo_set_pin(ch, 0U);
    }

    Delay_Us((uint32_t)(SERVO_PERIOD_US -
        servo_pulse_us[0] - servo_pulse_us[1] - servo_pulse_us[2] - servo_pulse_us[3]));
}

static void servo_set_angle(uint8_t channel, uint16_t angle)
{
    if (channel >= SERVO_MAX_CHANNELS) {
        return;
    }

    if (angle > 180U) {
        angle = 180U;
    }

    servo_angle[channel] = angle;
    servo_pulse_us[channel] = servo_angle_to_pulse_us(angle);
}

static void handle_servo_set(const bsp_can_frame_t *frame)
{
    uint8_t channel;
    uint16_t angle;
    char line[96];

    if (frame->dlc < 3U) {
        send_ack(0x30U, CAN_ID_SERVO_SET, 0U);
        return;
    }

    channel = frame->data[0];
    angle = get_u16_le(frame->data, 1U);
    if (channel >= SERVO_MAX_CHANNELS) {
        send_ack(0x30U, CAN_ID_SERVO_SET, 0U);
        return;
    }

    servo_set_angle(channel, angle);
    send_ack(0x30U, CAN_ID_SERVO_SET, 1U);

    (void)snprintf(line, sizeof(line), "SERVO ch=%u angle=%u pulse=%uus",
                   channel, servo_angle[channel], servo_pulse_us[channel]);
    vibe_println(line);
}

static void can_command_task(void)
{
    bsp_can_frame_t frame;

    if (!can_ready) {
        return;
    }

    while (bsp_can_receive(&frame)) {
        if (frame.id == CAN_ID_SERVO_SET) {
            handle_servo_set(&frame);
        }
    }
}

static void servo_pwm_task(void)
{
    servo_output_all_frame();
}

static void servo_report_task(void)
{
    char line[128];

    (void)snprintf(line, sizeof(line), "servo gateway alive A6=%u A7=%u B6=%u B7=%u can=%u",
                   servo_angle[0], servo_angle[1], servo_angle[2], servo_angle[3], can_ready);
    vibe_println(line);
}

void app_setup(void)
{
    vibe_serial_begin(115200U);
    vibe_println("MOCE SDK CH32V203G6U6 servo gateway");
    vibe_println("Servo pins: ch0=PA6 ch1=PA7 ch2=PB6 ch3=PB7, software PWM 50Hz");
    vibe_println("CAN 0x430: data[0]=ch 0..3, data[1..2]=angle 0..180 deg LE");
    vibe_println("ACK 0x500: data[0]=0x30 data[1..2]=0x430 data[3]=result");
    vibe_println("Use external servo 5V supply and common GND with CH32.");

    servo_gpio_init();
    servo_set_angle(0U, 90U);
    servo_set_angle(1U, 90U);
    servo_set_angle(2U, 90U);
    servo_set_angle(3U, 90U);

    can_ready = bsp_can_init_50k();
    vibe_println(can_ready ? "CAN init ok" : "CAN init failed");

    (void)vibe_task_every_ms(CAN_POLL_PERIOD_MS, can_command_task);
    (void)vibe_task_every_ms(20U, servo_pwm_task);
    (void)vibe_task_every_ms(SERVO_REPORT_MS, servo_report_task);
}
