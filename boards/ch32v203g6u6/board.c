#include "board.h"
#include "ch32v20x.h"

void board_init(void)
{
    board_clock_init();
    board_gpio_init();
}

void board_error_handler(void)
{
    __disable_irq();
    while (1) {
    }
}
