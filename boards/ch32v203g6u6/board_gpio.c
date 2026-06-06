#include "board.h"
#include "board_pins.h"
#include "ch32v20x_gpio.h"
#include "ch32v20x_rcc.h"

void board_gpio_init(void)
{
    GPIO_InitTypeDef gpio = {0};

    RCC_APB2PeriphClockCmd(BOARD_LED_GPIO_CLK, ENABLE);

    gpio.GPIO_Pin = BOARD_LED_GPIO_PIN;
    gpio.GPIO_Mode = GPIO_Mode_Out_PP;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(BOARD_LED_GPIO_PORT, &gpio);

#if BOARD_LED_ACTIVE_LOW
    GPIO_SetBits(BOARD_LED_GPIO_PORT, BOARD_LED_GPIO_PIN);
#else
    GPIO_ResetBits(BOARD_LED_GPIO_PORT, BOARD_LED_GPIO_PIN);
#endif
}
