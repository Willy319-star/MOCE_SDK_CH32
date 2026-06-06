#include "mcu_port_gpio.h"
#include "ch32v20x_gpio.h"
#include "ch32v20x_rcc.h"

void mcu_port_gpio_configure_output(const mcu_port_gpio_t *gpio)
{
    GPIO_InitTypeDef init = {0};

    if (gpio == 0) {
        return;
    }

    RCC_APB2PeriphClockCmd(gpio->clock, ENABLE);
    init.GPIO_Pin = gpio->pin;
    init.GPIO_Mode = GPIO_Mode_Out_PP;
    init.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(gpio->port, &init);
    mcu_port_gpio_write(gpio, 0U);
}

void mcu_port_gpio_configure_input(const mcu_port_gpio_t *gpio, uint8_t pull_up)
{
    GPIO_InitTypeDef init = {0};

    if (gpio == 0) {
        return;
    }

    RCC_APB2PeriphClockCmd(gpio->clock, ENABLE);
    init.GPIO_Pin = gpio->pin;
    init.GPIO_Mode = pull_up ? GPIO_Mode_IPU : GPIO_Mode_IPD;
    init.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(gpio->port, &init);
}

void mcu_port_gpio_write(const mcu_port_gpio_t *gpio, uint8_t active)
{
    uint8_t high;

    if (gpio == 0) {
        return;
    }

    high = active ? 1U : 0U;
    if (gpio->active_low) {
        high = high ? 0U : 1U;
    }

    if (high) {
        GPIO_SetBits(gpio->port, gpio->pin);
    } else {
        GPIO_ResetBits(gpio->port, gpio->pin);
    }
}

uint8_t mcu_port_gpio_read(const mcu_port_gpio_t *gpio)
{
    uint8_t active;

    if (gpio == 0) {
        return 0U;
    }

    active = GPIO_ReadInputDataBit(gpio->port, gpio->pin) ? 1U : 0U;
    if (gpio->active_low) {
        active = active ? 0U : 1U;
    }

    return active;
}

void mcu_port_gpio_toggle(const mcu_port_gpio_t *gpio)
{
    if (gpio == 0) {
        return;
    }

    if (GPIO_ReadOutputDataBit(gpio->port, gpio->pin)) {
        GPIO_ResetBits(gpio->port, gpio->pin);
    } else {
        GPIO_SetBits(gpio->port, gpio->pin);
    }
}
