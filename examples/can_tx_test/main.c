#include "bsp_can.h"
#include "vibe_api.h"
#include "vibe_runtime.h"

#include <stdio.h>

#define CAN_TEST_ID        0x123U
#define CAN_TX_PERIOD_MS   1000U

static uint32_t tx_count;
static uint8_t can_ready;

static void print_status(const char *prefix)
{
    bsp_can_status_t status;
    char line[128];

    bsp_can_get_status(&status);
    (void)snprintf(line, sizeof(line),
                   "%s tx_err=%u rx_err=%u lec=0x%02X ew=%u ep=%u bo=%u",
                   prefix,
                   status.tx_error_count,
                   status.rx_error_count,
                   status.last_error_code,
                   status.error_warning,
                   status.error_passive,
                   status.bus_off);
    vibe_println(line);
    (void)snprintf(line, sizeof(line), "CAN diag init_stage=%u rx_sample=%u",
                   status.init_stage,
                   status.rx_sample);
    vibe_println(line);
}

static void can_tx_task(void)
{
    uint8_t data[8] = {
        0x01U, 0x02U, 0x03U, 0x04U,
        0x05U, 0x06U, 0x07U, 0x08U,
    };
    char line[160];

    if (bsp_can_send_std(CAN_TEST_ID, data, sizeof(data))) {
        (void)snprintf(line, sizeof(line),
                       "can_tx #%lu id=0x%03X OK data=[%02X %02X %02X %02X %02X %02X %02X %02X]",
                       (unsigned long)tx_count,
                       CAN_TEST_ID,
                       data[0], data[1], data[2], data[3],
                       data[4], data[5], data[6], data[7]);
        vibe_println(line);
        ++tx_count;
    } else {
        (void)snprintf(line, sizeof(line),
                       "can_tx #%lu id=0x%03X FAILED data=[%02X %02X %02X %02X %02X %02X %02X %02X]",
                       (unsigned long)tx_count,
                       CAN_TEST_ID,
                       data[0], data[1], data[2], data[3],
                       data[4], data[5], data[6], data[7]);
        vibe_println(line);
        print_status("CAN status");
    }

    vibe_led_toggle();
}

void app_setup(void)
{
    vibe_serial_begin(115200U);
    vibe_wait_ms(300U);
    vibe_println("MOCE SDK CH32V203G6U6 CAN TX test for ESP32");
    vibe_println("CAN1 TX=PA12 RX=PA11, bitrate=50 kbit/s, id=0x123, period=1000ms");
    vibe_println("CAN payload: 01 02 03 04 05 06 07 08");

    can_ready = bsp_can_init_50k();
    if (!can_ready) {
        vibe_println("CAN init failed");
        print_status("CAN init status");
        return;
    }

    vibe_println("CAN init ok, sending ESP32-compatible standard frames...");
    (void)vibe_task_every_ms(CAN_TX_PERIOD_MS, can_tx_task);
}
