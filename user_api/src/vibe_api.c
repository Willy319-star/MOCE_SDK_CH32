#include "vibe_api.h"
#include "vibe_runtime.h"
#include "board.h"
#include "bsp_led.h"
#include "bsp_serial.h"
#include "bsp_time.h"

static uint32_t blink_interval_ms;

static void blink_task(void)
{
    bsp_led_toggle();
}

void vibe_init(void)
{
    board_init();
    bsp_led_init();
}

void vibe_wait_ms(uint32_t ms)
{
    bsp_delay_ms(ms);
}

uint32_t vibe_millis(void)
{
    return bsp_millis();
}

void vibe_led_on(void)
{
    bsp_led_on();
}

void vibe_led_off(void)
{
    bsp_led_off();
}

void vibe_led_toggle(void)
{
    bsp_led_toggle();
}

void vibe_led_blink(uint32_t interval_ms)
{
    blink_interval_ms = interval_ms;
    (void)vibe_task_every_ms(blink_interval_ms, blink_task);
}

void vibe_serial_begin(uint32_t baudrate)
{
    bsp_serial_begin(baudrate);
}

void vibe_print(const char *text)
{
    bsp_serial_write(text);
}

void vibe_println(const char *text)
{
    bsp_serial_writeln(text);
}
