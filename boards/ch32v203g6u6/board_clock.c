#include "board.h"
#include "ch32v20x.h"
#include "debug.h"

void board_clock_init(void)
{
    /* The WCH startup calls SystemInit(). Keep clock setup conservative here.
     * If you want 144MHz explicitly, configure HSE/PLL here using the WCH
     * standard peripheral library for your board crystal.
     */
    SystemCoreClockUpdate();
    Delay_Init();
}
