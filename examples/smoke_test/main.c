#include "vibe_api.h"
#include "vibe_runtime.h"

static void heartbeat_task(void)
{
    vibe_led_toggle();
    vibe_println("tick");
}

void app_setup(void)
{
    vibe_serial_begin(115200U);
    vibe_println("MOCE SDK CH32V203G6U6 smoke test");
    vibe_println("USART1_TX=PA9, SPI1=PA4/PA5/PA6/PA7, I2C1=PB6/PB7");
    (void)vibe_task_every_ms(500U, heartbeat_task);
}
