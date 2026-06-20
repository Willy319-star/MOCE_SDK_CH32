#include "bsp_can.h"
#include "vibe_api.h"
#include "vibe_runtime.h"

#include <stdio.h>

static uint32_t rx_count;
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

static void print_frame(const bsp_can_frame_t *frame)
{
    char line[176];

    (void)snprintf(line, sizeof(line),
                   "CAN RX id=0x%03lX dlc=%u count=%lu data=%02X %02X %02X %02X %02X %02X %02X %02X",
                   (unsigned long)frame->id,
                   frame->dlc,
                   (unsigned long)rx_count,
                   frame->data[0], frame->data[1], frame->data[2], frame->data[3],
                   frame->data[4], frame->data[5], frame->data[6], frame->data[7]);
    vibe_println(line);
}

static void can_rx_task(void)
{
    bsp_can_frame_t frame;
    uint8_t got_frame = 0U;

    if (!can_ready) {
        vibe_println("CAN RX alive, CAN init not ready");
        print_status("CAN status");
        return;
    }

    while (bsp_can_receive(&frame)) {
        got_frame = 1U;
        ++rx_count;
        print_frame(&frame);
        vibe_led_toggle();
    }

    if (!got_frame) {
        char line[64];
        (void)snprintf(line, sizeof(line), "CAN RX waiting count=%lu", (unsigned long)rx_count);
        vibe_println(line);
        print_status("CAN status");
    }
}

void app_setup(void)
{
    vibe_serial_begin(115200U);
    vibe_wait_ms(300U);
    vibe_println("MOCE SDK CH32V203G6U6 CAN RX test");
    vibe_println("CAN1 TX=PA12 RX=PA11, bus=CANH/CANL/GND, bitrate=50k");
    (void)vibe_task_every_ms(500U, can_rx_task);

    can_ready = bsp_can_init_50k();
    if (!can_ready) {
        vibe_println("CAN init failed");
        return;
    }

    vibe_println("CAN init ok, waiting for standard frames...");
}
