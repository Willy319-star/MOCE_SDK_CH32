#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void vibe_init(void);
void vibe_wait_ms(uint32_t ms);
uint32_t vibe_millis(void);

void vibe_led_on(void);
void vibe_led_off(void);
void vibe_led_toggle(void);
void vibe_led_blink(uint32_t interval_ms);

void vibe_serial_begin(uint32_t baudrate);
void vibe_print(const char *text);
void vibe_println(const char *text);

#ifdef __cplusplus
}
#endif
