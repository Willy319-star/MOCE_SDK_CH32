#include "vibe_api.h"
#include "vibe_runtime.h"

void app_setup(void)
{
    vibe_serial_begin(115200U);
    vibe_println("MOCE SDK CH32 started.");
    vibe_led_blink(500U);
}
