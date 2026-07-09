#include "common_can_node.h"
#include "ch32v20x.h"
#include "ch32v20x_gpio.h"
#include "ch32v20x_rcc.h"
#include "mcu_port_time.h"
#include "vibe_api.h"
#include "vibe_runtime.h"

#include <stdio.h>

#define NODE_ID            2U
#define DEVICE_TYPE        DEVICE_TYPE_MOTOR
#define FW_VERSION         1U
#define CAPABILITY_FLAGS   0x03U

#define HEARTBEAT_MS       1000U
#define CAN_POLL_MS        20U

#define MOTOR_A_PWM_PORT   GPIOA
#define MOTOR_A_PWM_PIN    GPIO_Pin_7
#define MOTOR_A_DIR_PORT   GPIOA
#define MOTOR_A_DIR_PIN    GPIO_Pin_5
#define MOTOR_B_PWM_PORT   GPIOA
#define MOTOR_B_PWM_PIN    GPIO_Pin_6
#define MOTOR_B_DIR_PORT   GPIOA
#define MOTOR_B_DIR_PIN    GPIO_Pin_4
#define MOTOR_GPIO_CLK     RCC_APB2Periph_GPIOA

#define PWM_DEFAULT_FREQ_HZ 1000U
#define PWM_TIMER_CLOCK_HZ  96000000UL
#define PWM_PERIOD_TICKS    1000U

static uint8_t can_ready;
static uint16_t motor_duty[2];
static uint8_t motor_dir[2];

static void heartbeat_task(void)
{
    if (can_ready) {
        (void)node_send_hello(NODE_ID, DEVICE_TYPE, FW_VERSION, CAPABILITY_FLAGS);
    }
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
    GPIO_ResetBits(MOTOR_A_DIR_PORT, MOTOR_A_DIR_PIN);
    GPIO_ResetBits(MOTOR_B_DIR_PORT, MOTOR_B_DIR_PIN);
}

static void motor_pwm_init(void)
{
    GPIO_InitTypeDef gpio = {0};
    uint32_t prescaler;

    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM3, ENABLE);

    gpio.GPIO_Pin = MOTOR_A_PWM_PIN | MOTOR_B_PWM_PIN;
    gpio.GPIO_Mode = GPIO_Mode_AF_PP;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOA, &gpio);

    prescaler = PWM_TIMER_CLOCK_HZ / (PWM_DEFAULT_FREQ_HZ * PWM_PERIOD_TICKS);
    if (prescaler == 0U) {
        prescaler = 1U;
    }

    TIM3->CTLR1 = 0U;
    TIM3->PSC = (uint16_t)(prescaler - 1U);
    TIM3->ATRLR = (uint16_t)(PWM_PERIOD_TICKS - 1U);
    TIM3->CH1CVR = 0U;
    TIM3->CH2CVR = 0U;
    TIM3->CHCTLR1 = 0x6868U;
    TIM3->CCER = (uint16_t)((TIM3->CCER & ~(uint16_t)0x0033U) | 0x0011U);
    TIM3->SWEVGR |= 0x0001U;
    TIM3->CTLR1 |= 0x0081U;
}

static void motor_set(uint8_t channel, uint16_t duty_permille, uint8_t direction)
{
    uint16_t pulse;

    if (duty_permille > 1000U) {
        duty_permille = 1000U;
    }
    direction = (direction != 0U) ? 1U : 0U;
    pulse = (uint16_t)((PWM_PERIOD_TICKS * duty_permille) / 1000U);

    if (channel == 0U) {
        motor_duty[0] = duty_permille;
        motor_dir[0] = direction;
        if (direction == 0U) GPIO_ResetBits(MOTOR_A_DIR_PORT, MOTOR_A_DIR_PIN);
        else GPIO_SetBits(MOTOR_A_DIR_PORT, MOTOR_A_DIR_PIN);
        TIM3->CH2CVR = pulse;
    } else if (channel == 1U) {
        motor_duty[1] = duty_permille;
        motor_dir[1] = direction;
        if (direction == 0U) GPIO_ResetBits(MOTOR_B_DIR_PORT, MOTOR_B_DIR_PIN);
        else GPIO_SetBits(MOTOR_B_DIR_PORT, MOTOR_B_DIR_PIN);
        TIM3->CH1CVR = pulse;
    }
}

static void handle_motor_cmd(const bsp_can_frame_t *frame)
{
    uint8_t channel;
    uint16_t duty;
    uint8_t direction;
    char line[96];

    if (frame->dlc < 4U) {
        (void)node_send_ack(NODE_ID, DEVICE_TYPE, 0U, 0U);
        return;
    }

    channel = frame->data[0];
    duty = node_get_u16_le(frame->data, 1U);
    direction = frame->data[3];
    if (channel > 1U) {
        (void)node_send_ack(NODE_ID, DEVICE_TYPE, 0U, 0U);
        return;
    }

    motor_set(channel, duty, direction);
    (void)snprintf(line, sizeof(line), "MOTOR ch=%u duty=%u dir=%u", channel, duty, direction);
    vibe_println(line);
    (void)node_send_ack(NODE_ID, DEVICE_TYPE, channel, 1U);
}

static void can_command_task(void)
{
    bsp_can_frame_t frame;

    if (!can_ready) return;

    while (bsp_can_receive(&frame)) {
        if (frame.id == CAN_ID_MOTOR_CMD(NODE_ID)) {
            handle_motor_cmd(&frame);
        }
    }
}

void app_setup(void)
{
    char line[96];

    vibe_serial_begin(115200U);
    mcu_port_delay_ms(300U);
    vibe_println("CH32 motor gateway");
    (void)snprintf(line, sizeof(line), "NODE_ID = %u", NODE_ID); vibe_println(line);
    (void)snprintf(line, sizeof(line), "DEVICE_TYPE = %u", DEVICE_TYPE); vibe_println(line);
    (void)snprintf(line, sizeof(line), "CAN hello id = 0x%03X", CAN_ID_HELLO(NODE_ID)); vibe_println(line);
    (void)snprintf(line, sizeof(line), "ACK id = 0x%03X", CAN_ID_ACK(NODE_ID)); vibe_println(line);
    vibe_println("channel0: PA7 PWM + PA5 DIR, channel1: PA6 PWM + PA4 DIR");

    motor_gpio_init();
    motor_pwm_init();
    can_ready = bsp_can_init_50k();
    vibe_println(can_ready ? "CAN init ok" : "CAN init failed");
    heartbeat_task();

    (void)vibe_task_every_ms(HEARTBEAT_MS, heartbeat_task);
    (void)vibe_task_every_ms(CAN_POLL_MS, can_command_task);
}
