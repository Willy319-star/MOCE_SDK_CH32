#include "vibe_api.h"
#include "vibe_app.h"
#include "vibe_runtime.h"

__attribute__((weak)) void app_setup(void)
{
}

int main(void)
{
    vibe_init();
    vibe_runtime_init();
    app_setup();

    while (1) {
        vibe_runtime_run_once();
        vibe_wait_ms(1U);
    }
}
