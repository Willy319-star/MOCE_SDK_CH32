#include "bsp_max31865.h"
#include "mcu_port_time.h"
#include "vibe_api.h"
#include "vibe_runtime.h"

#include <stdio.h>

static uint8_t max_ready;

static void print_max31865_task(void)
{
    bsp_max31865_status_t status;
    bsp_max31865_probe_t probe;
    char line[160];

    if (!max_ready) {
        if (bsp_max31865_probe(&probe)) {
            (void)snprintf(line, sizeof(line),
                           "MAX31865 not ready probe m0 %02X->%02X m1 %02X->%02X m2 %02X->%02X m3 %02X->%02X",
                           probe.mode0_before, probe.mode0_after,
                           probe.mode1_before, probe.mode1_after,
                           probe.mode2_before, probe.mode2_after,
                           probe.mode3_before, probe.mode3_after);
            vibe_println(line);
        }
        return;
    }

    if (!bsp_max31865_read_status(&status)) {
        vibe_println("MAX31865 read failed");
        return;
    }

    (void)snprintf(line, sizeof(line),
                   "MAX31865 SPI no address, CS=PA4 mode=%u cfg_before=0x%02X cfg_after=0x%02X fault=0x%02X rtd_raw=%u",
                   bsp_max31865_active_mode(),
                   status.config_before,
                   status.config_after,
                   status.fault_status,
                   status.rtd_raw);
    vibe_println(line);
    vibe_led_toggle();
}

void app_setup(void)
{
    vibe_serial_begin(115200U);
    mcu_port_delay_ms(300U);

    vibe_println("MOCE SDK CH32V203G6U6 MAX31865 SPI test");
    vibe_println("SPI devices have no address. MAX31865 selected by CS=PA4.");
    vibe_println("SPI1: CS=PA4 SCK=PA5 MISO=PA6 MOSI=PA7, 3V3/GND");

    max_ready = bsp_max31865_init();
    if (max_ready) {
        char line[64];
        (void)snprintf(line, sizeof(line), "MAX31865 init ok mode=%u", bsp_max31865_active_mode());
        vibe_println(line);
    } else {
        vibe_println("MAX31865 init failed");
    }

    (void)vibe_task_every_ms(1000U, print_max31865_task);
}
