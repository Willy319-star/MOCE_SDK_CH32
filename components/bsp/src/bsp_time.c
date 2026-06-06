#include "bsp_time.h"
#include "mcu_port_time.h"

void bsp_delay_ms(uint32_t ms)
{
    mcu_port_delay_ms(ms);
}

uint32_t bsp_millis(void)
{
    return mcu_port_millis();
}
