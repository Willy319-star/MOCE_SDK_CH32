#include "bsp_bme280.h"
#include "mcu_port_time.h"
#include "vibe_api.h"
#include "vibe_runtime.h"

#include <stdio.h>

static uint8_t bme_ready;

static void print_bme_task(void)
{
    bsp_bme280_sample_t sample;
    char line[160];

    if (!bme_ready) {
        uint8_t chip_id = 0U;
        bsp_bme280_debug_t debug;
        if (bsp_bme280_chip_id(&chip_id)) {
            (void)snprintf(line, sizeof(line),
                           "BME280 not ready CHIP_ID=0x%02X init_stage=%u",
                           chip_id,
                           bsp_bme280_init_stage());
        } else {
            (void)snprintf(line, sizeof(line),
                           "BME280 not ready CHIP_ID read failed init_stage=%u",
                           bsp_bme280_init_stage());
        }
        vibe_println(line);
        if (bsp_bme280_debug_probe(&debug)) {
            (void)snprintf(line, sizeof(line),
                           "BME280 probe normal m0=0x%02X m3=0x%02X swapped m0=0x%02X m3=0x%02X",
                           debug.normal_mode0_id,
                           debug.normal_mode3_id,
                           debug.swapped_mode0_id,
                           debug.swapped_mode3_id);
            vibe_println(line);
        }
        return;
    }

    if (!bsp_bme280_read_sample(&sample)) {
        vibe_println("BME280 read failed");
        return;
    }

    (void)snprintf(line, sizeof(line),
                   "BME280 temp=%ld.%02ldC hum=%lu.%02lu%%RH press=%luPa",
                   (long)(sample.temperature_centi_c / 100L),
                   (long)(sample.temperature_centi_c < 0 ? -(sample.temperature_centi_c % 100L) :
                                                          (sample.temperature_centi_c % 100L)),
                   (unsigned long)(sample.humidity_centi_rh / 100UL),
                   (unsigned long)(sample.humidity_centi_rh % 100UL),
                   (unsigned long)sample.pressure_pa);
    vibe_println(line);
    vibe_led_toggle();
}

void app_setup(void)
{
    uint8_t chip_id = 0U;
    char line[80];

    vibe_serial_begin(115200U);
    mcu_port_delay_ms(300U);
    vibe_println("MOCE SDK CH32V203G6U6 BME280 readout");
    vibe_println("SPI1: CS=PA4 SCK=PA5 MISO=PA6 MOSI=PA7, 3V3/GND");

    bme_ready = bsp_bme280_init();
    if (bsp_bme280_chip_id(&chip_id)) {
        (void)snprintf(line, sizeof(line), "BME280 CHIP_ID=0x%02X init_stage=%u",
                       chip_id,
                       bsp_bme280_init_stage());
        vibe_println(line);
    } else {
        (void)snprintf(line, sizeof(line), "BME280 CHIP_ID read failed init_stage=%u",
                       bsp_bme280_init_stage());
        vibe_println(line);
    }

    vibe_println(bme_ready ? "BME280 init ok" : "BME280 init failed");
    (void)vibe_task_every_ms(1000U, print_bme_task);
}
