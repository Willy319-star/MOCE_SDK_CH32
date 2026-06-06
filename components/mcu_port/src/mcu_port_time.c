#include "mcu_port_time.h"
#include "debug.h"

static volatile uint32_t port_tick_ms;

void mcu_port_delay_ms(uint32_t ms)
{
    Delay_Ms(ms);
    port_tick_ms += ms;
}

uint32_t mcu_port_millis(void)
{
    return port_tick_ms;
}
