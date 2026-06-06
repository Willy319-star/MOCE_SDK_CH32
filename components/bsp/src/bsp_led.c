#include "bsp_led.h"
#include "board_pins.h"
#include "mcu_port_gpio.h"

static const mcu_port_gpio_t led = {
    .port = BOARD_LED_GPIO_PORT,
    .pin = BOARD_LED_GPIO_PIN,
    .clock = BOARD_LED_GPIO_CLK,
    .active_low = BOARD_LED_ACTIVE_LOW,
};

void bsp_led_init(void)
{
    mcu_port_gpio_configure_output(&led);
}

void bsp_led_on(void)
{
    mcu_port_gpio_write(&led, 1U);
}

void bsp_led_off(void)
{
    mcu_port_gpio_write(&led, 0U);
}

void bsp_led_toggle(void)
{
    mcu_port_gpio_toggle(&led);
}
