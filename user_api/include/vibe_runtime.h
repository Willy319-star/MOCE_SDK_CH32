#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*vibe_task_fn_t)(void);

void vibe_runtime_init(void);
int vibe_task_every_ms(uint32_t interval_ms, vibe_task_fn_t task);
void vibe_runtime_run_once(void);

#ifdef __cplusplus
}
#endif
