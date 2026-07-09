#include "ch32v20x_gpio.h"
#include "ch32v20x_rcc.h"
#include "debug.h"
#include "vibe_api.h"

#include <stdio.h>

#define SERVO_PWM_PORT_A        GPIOA
#define SERVO_PWM_PINS_A        (GPIO_Pin_6 | GPIO_Pin_7)
#define SERVO_PWM_PORT_B        GPIOB
#define SERVO_PWM_PINS_B        (GPIO_Pin_6 | GPIO_Pin_7)
#define SERVO_GPIO_CLK          (RCC_APB2Periph_GPIOA | RCC_APB2Periph_GPIOB)
#define SERVO_PERIOD_US         20000U
#define SERVO_MIN_PULSE_US      1000U
#define SERVO_MID_PULSE_US      1500U
#define SERVO_MAX_PULSE_US      2000U
#define SERVO_STEP_DEG          10U
#define SERVO_FRAMES_PER_STEP   25U

static uint16_t servo_angle_to_pulse_us(uint8_t angle)
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

static void servo_set_all_high(void)
{
    GPIO_SetBits(SERVO_PWM_PORT_A, SERVO_PWM_PINS_A);
    GPIO_SetBits(SERVO_PWM_PORT_B, SERVO_PWM_PINS_B);
}

static void servo_set_all_low(void)
{
    GPIO_ResetBits(SERVO_PWM_PORT_A, SERVO_PWM_PINS_A);
    GPIO_ResetBits(SERVO_PWM_PORT_B, SERVO_PWM_PINS_B);
}

static void servo_output_frame(uint16_t pulse_us)
{
    if (pulse_us < SERVO_MIN_PULSE_US) {
        pulse_us = SERVO_MIN_PULSE_US;
    } else if (pulse_us > SERVO_MAX_PULSE_US) {
        pulse_us = SERVO_MAX_PULSE_US;
    }

    servo_set_all_high();
    Delay_Us(pulse_us);
    servo_set_all_low();
    Delay_Us((uint32_t)(SERVO_PERIOD_US - pulse_us));
}

static void servo_hold_angle(uint8_t angle, uint8_t frames)
{
    uint8_t i;
    uint16_t pulse_us = servo_angle_to_pulse_us(angle);

    for (i = 0U; i < frames; ++i) {
        servo_output_frame(pulse_us);
    }
}

static void servo_sweep_forever(void)
{
    uint8_t angle;
    uint16_t loop_count = 0U;
    char line[96];

    while (1) {
        for (angle = 0U; angle <= 180U; angle = (uint8_t)(angle + SERVO_STEP_DEG)) {
            servo_hold_angle(angle, SERVO_FRAMES_PER_STEP);
        }

        for (angle = 180U; angle >= SERVO_STEP_DEG; angle = (uint8_t)(angle - SERVO_STEP_DEG)) {
            servo_hold_angle((uint8_t)(angle - SERVO_STEP_DEG), SERVO_FRAMES_PER_STEP);
        }

        loop_count++;
        (void)snprintf(line, sizeof(line), "servo sweep loop=%u pins=PB6 PB7 PA6 PA7", loop_count);
        vibe_println(line);
    }
}

void app_setup(void)
{
    vibe_serial_begin(115200U);
    vibe_println("MOCE SDK CH32V203G6U6 4-channel servo PWM mirror");
    vibe_println("PB6 PB7 PA6 PA7 output the same software PWM signal");
    vibe_println("PWM period=20ms, pulse=1000..2000us, sweep 0-180 deg");
    vibe_println("Use external servo 5V supply and common GND with CH32.");

    servo_gpio_init();
    servo_hold_angle(90U, 50U);
    servo_sweep_forever();
}
