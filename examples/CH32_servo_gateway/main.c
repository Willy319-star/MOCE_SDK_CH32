#include "common_can_node.h"
#include "ch32v20x_gpio.h"
#include "ch32v20x_rcc.h"
#include "debug.h"
#include "mcu_port_time.h"
#include "vibe_api.h"
#include "vibe_runtime.h"

#include <stdio.h>

#define NODE_ID            3U
#define DEVICE_TYPE        DEVICE_TYPE_SERVO
#define FW_VERSION         1U
#define CAPABILITY_FLAGS   0x0FU

#define HEARTBEAT_MS       1000U
#define CAN_POLL_MS        20U
#define SERVO_TASK_MS      20U

#define SERVO_PWM_PORT_A   GPIOA
#define SERVO_PWM_PINS_A   (GPIO_Pin_6 | GPIO_Pin_7)
#define SERVO_PWM_PORT_B   GPIOB
#define SERVO_PWM_PINS_B   (GPIO_Pin_6 | GPIO_Pin_7)
#define SERVO_GPIO_CLK     (RCC_APB2Periph_GPIOA | RCC_APB2Periph_GPIOB)

#define SERVO_PERIOD_US    20000U
#define SERVO_MIN_US       1000U
#define SERVO_MID_US       1500U
#define SERVO_MAX_US       2000U
#define SERVO_MAX_CHANNELS 4U

static uint8_t can_ready;
static uint16_t servo_angle[SERVO_MAX_CHANNELS] = {90U, 90U, 90U, 90U};
static uint16_t servo_pulse_us[SERVO_MAX_CHANNELS] = {SERVO_MID_US, SERVO_MID_US, SERVO_MID_US, SERVO_MID_US};

static void heartbeat_task(void)
{
    if (can_ready) {
        (void)node_send_hello(NODE_ID, DEVICE_TYPE, FW_VERSION, CAPABILITY_FLAGS);
    }
}

static uint16_t servo_angle_to_pulse(uint16_t angle)
{
    uint32_t pulse;

    if (angle > 180U) angle = 180U;
    pulse = SERVO_MIN_US + ((uint32_t)(SERVO_MAX_US - SERVO_MIN_US) * angle) / 180UL;
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
    case 0U: if (high) GPIO_SetBits(GPIOA, GPIO_Pin_6); else GPIO_ResetBits(GPIOA, GPIO_Pin_6); break;
    case 1U: if (high) GPIO_SetBits(GPIOA, GPIO_Pin_7); else GPIO_ResetBits(GPIOA, GPIO_Pin_7); break;
    case 2U: if (high) GPIO_SetBits(GPIOB, GPIO_Pin_6); else GPIO_ResetBits(GPIOB, GPIO_Pin_6); break;
    case 3U: if (high) GPIO_SetBits(GPIOB, GPIO_Pin_7); else GPIO_ResetBits(GPIOB, GPIO_Pin_7); break;
    default: break;
    }
}

static void servo_output_frame(void)
{
    uint8_t ch;
    uint32_t used = 0U;

    for (ch = 0U; ch < SERVO_MAX_CHANNELS; ++ch) {
        servo_set_pin(ch, 1U);
        Delay_Us(servo_pulse_us[ch]);
        servo_set_pin(ch, 0U);
        used += servo_pulse_us[ch];
    }

    if (used < SERVO_PERIOD_US) {
        Delay_Us(SERVO_PERIOD_US - used);
    }
}

static void servo_set_angle(uint8_t channel, uint16_t angle)
{
    if (channel >= SERVO_MAX_CHANNELS) return;
    if (angle > 180U) angle = 180U;
    servo_angle[channel] = angle;
    servo_pulse_us[channel] = servo_angle_to_pulse(angle);
}

static void handle_servo_cmd(const bsp_can_frame_t *frame)
{
    uint8_t channel;
    uint16_t angle;
    char line[96];

    if (frame->dlc < 3U) {
        (void)node_send_ack(NODE_ID, DEVICE_TYPE, 0U, 0U);
        return;
    }

    channel = frame->data[0];
    angle = node_get_u16_le(frame->data, 1U);
    if (channel >= SERVO_MAX_CHANNELS) {
        (void)node_send_ack(NODE_ID, DEVICE_TYPE, channel, 0U);
        return;
    }

    servo_set_angle(channel, angle);
    (void)snprintf(line, sizeof(line), "SERVO ch=%u angle=%u", channel, servo_angle[channel]);
    vibe_println(line);
    (void)node_send_ack(NODE_ID, DEVICE_TYPE, channel, 1U);
}

static void can_command_task(void)
{
    bsp_can_frame_t frame;

    if (!can_ready) return;
    while (bsp_can_receive(&frame)) {
        if (frame.id == CAN_ID_SERVO_CMD(NODE_ID)) {
            handle_servo_cmd(&frame);
        }
    }
}

static void servo_task(void)
{
    servo_output_frame();
}

void app_setup(void)
{
    char line[96];

    vibe_serial_begin(115200U);
    mcu_port_delay_ms(300U);
    vibe_println("CH32 servo gateway");
    (void)snprintf(line, sizeof(line), "NODE_ID = %u", NODE_ID); vibe_println(line);
    (void)snprintf(line, sizeof(line), "DEVICE_TYPE = %u", DEVICE_TYPE); vibe_println(line);
    (void)snprintf(line, sizeof(line), "CAN hello id = 0x%03X", CAN_ID_HELLO(NODE_ID)); vibe_println(line);
    (void)snprintf(line, sizeof(line), "ACK id = 0x%03X", CAN_ID_ACK(NODE_ID)); vibe_println(line);
    vibe_println("ch0=PA6 ch1=PA7 ch2=PB6 ch3=PB7");

    servo_gpio_init();
    servo_set_angle(0U, 90U);
    servo_set_angle(1U, 90U);
    servo_set_angle(2U, 90U);
    servo_set_angle(3U, 90U);
    can_ready = bsp_can_init_50k();
    vibe_println(can_ready ? "CAN init ok" : "CAN init failed");
    heartbeat_task();

    (void)vibe_task_every_ms(HEARTBEAT_MS, heartbeat_task);
    (void)vibe_task_every_ms(CAN_POLL_MS, can_command_task);
    (void)vibe_task_every_ms(SERVO_TASK_MS, servo_task);
}
