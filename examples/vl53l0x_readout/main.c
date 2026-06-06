#include "bsp_vl53l0x.h"
#include "mcu_port_time.h"
#include "vibe_api.h"
#include "vibe_runtime.h"

#include <stdio.h>

#define VL53L0X_POLL_INTERVAL_MS    20U
#define VL53L0X_TIMEOUT_MS          800U
#define VL53L0X_INVALID_REPORT_MS   1000U

static uint8_t sensor_ready;
static uint8_t measurement_pending;
static uint8_t timeout_count;
static uint8_t last_invalid_status = 0xffU;
static uint32_t measurement_started_ms;
static uint32_t last_invalid_report_ms;
static uint32_t last_diag_report_ms;

static void print_result_bytes(void)
{
    char item[8];

    vibe_print(" result=");
    for (uint8_t i = 0U; i < 12U; ++i) {
        if (i != 0U) {
            vibe_print(",");
        }
        (void)snprintf(item, sizeof(item), "%02X", (unsigned)bsp_vl53l0x_last_result_byte(i));
        vibe_print(item);
    }
}

static void print_status_line(const char *prefix)
{
    char line[128];

    (void)snprintf(line, sizeof(line),
                   "%s status=%u model=0x%02X range_status=%u raw=%umm errors=%lu",
                   prefix,
                   (unsigned)bsp_vl53l0x_status(),
                   (unsigned)bsp_vl53l0x_model_id(),
                   (unsigned)bsp_vl53l0x_range_status(),
                   (unsigned)bsp_vl53l0x_raw_distance_mm(),
                   (unsigned long)bsp_vl53l0x_error_count());
    vibe_print(line);
    if ((uint32_t)(mcu_port_millis() - last_diag_report_ms) >= 3000U) {
        last_diag_report_ms = mcu_port_millis();
        print_result_bytes();
    }
    vibe_println("");
}

static uint8_t start_measurement(void)
{
    if (!sensor_ready || measurement_pending) {
        return 0U;
    }

    if (!bsp_vl53l0x_start_measurement()) {
        print_status_line("VL53L0X start failed");
        return 0U;
    }

    measurement_pending = 1U;
    measurement_started_ms = mcu_port_millis();
    return 1U;
}

static void vl53l0x_reinit(void)
{
    vibe_println("VL53L0X init begin");
    vibe_println("I2C: SCL=PB6 SDA=PB7, address=0x29, VCC=3V3");

    sensor_ready = bsp_vl53l0x_begin();
    measurement_pending = 0U;
    timeout_count = 0U;

    if (sensor_ready) {
        vibe_println("VL53L0X init ok");
        print_status_line("VL53L0X ready");
        (void)start_measurement();
    } else {
        vibe_println("VL53L0X init failed");
        print_status_line("VL53L0X failed");
    }
}

static void report_invalid_frame(void)
{
    uint8_t range_status = bsp_vl53l0x_range_status();
    uint32_t now = mcu_port_millis();

    if (range_status != last_invalid_status ||
        (uint32_t)(now - last_invalid_report_ms) >= VL53L0X_INVALID_REPORT_MS) {
        last_invalid_status = range_status;
        last_invalid_report_ms = now;
        print_status_line("VL53L0X invalid");
    }
}

static void vl53l0x_task(void)
{
    uint16_t distance_mm = 0U;
    char line[128];
    uint32_t now;

    if (!sensor_ready) {
        return;
    }

    if (!measurement_pending) {
        (void)start_measurement();
        return;
    }

    if (bsp_vl53l0x_is_measurement_ready()) {
        if (bsp_vl53l0x_read_distance_mm(&distance_mm)) {
            timeout_count = 0U;
            (void)snprintf(line, sizeof(line),
                           "VL53L0X distance=%umm status=%u errors=%lu",
                           (unsigned)distance_mm,
                           (unsigned)bsp_vl53l0x_range_status(),
                           (unsigned long)bsp_vl53l0x_error_count());
            vibe_println(line);
        } else {
            report_invalid_frame();
        }

        measurement_pending = 0U;
        (void)start_measurement();
        return;
    }

    now = mcu_port_millis();
    if ((uint32_t)(now - measurement_started_ms) > VL53L0X_TIMEOUT_MS) {
        measurement_pending = 0U;
        bsp_vl53l0x_reset_state();
        timeout_count++;
        if (timeout_count >= 3U) {
            sensor_ready = 0U;
            vibe_println("VL53L0X timeout, reinit");
            vl53l0x_reinit();
        } else {
            vibe_println("VL53L0X timeout, restart");
            (void)start_measurement();
        }
    }
}

void app_setup(void)
{
    vibe_serial_begin(115200U);
    mcu_port_delay_ms(300U);
    vibe_println("MOCE SDK CH32V203G6U6 VL53L0X readout");

    vl53l0x_reinit();

    (void)vibe_task_every_ms(VL53L0X_POLL_INTERVAL_MS, vl53l0x_task);
}
