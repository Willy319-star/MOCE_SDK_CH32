#include "vibe_runtime.h"
#include "vibe_api.h"

#define VIBE_MAX_TASKS 8U

typedef struct {
    uint32_t interval_ms;
    uint32_t last_run_ms;
    vibe_task_fn_t task;
} vibe_task_t;

static vibe_task_t tasks[VIBE_MAX_TASKS];

void vibe_runtime_init(void)
{
    for (uint32_t i = 0U; i < VIBE_MAX_TASKS; ++i) {
        tasks[i].interval_ms = 0U;
        tasks[i].last_run_ms = 0U;
        tasks[i].task = 0;
    }
}

int vibe_task_every_ms(uint32_t interval_ms, vibe_task_fn_t task)
{
    if (interval_ms == 0U || task == 0) {
        return -1;
    }

    for (uint32_t i = 0U; i < VIBE_MAX_TASKS; ++i) {
        if (tasks[i].task == 0) {
            tasks[i].interval_ms = interval_ms;
            tasks[i].last_run_ms = vibe_millis();
            tasks[i].task = task;
            return 0;
        }
    }

    return -1;
}

void vibe_runtime_run_once(void)
{
    uint32_t now = vibe_millis();

    for (uint32_t i = 0U; i < VIBE_MAX_TASKS; ++i) {
        if (tasks[i].task != 0 && (now - tasks[i].last_run_ms) >= tasks[i].interval_ms) {
            tasks[i].last_run_ms = now;
            tasks[i].task();
        }
    }
}
