#include "common_can_node.h"
#include "board_pins.h"
#include "mcu_port_spi.h"
#include "mcu_port_time.h"
#include "vibe_runtime.h"

#include <stdint.h>
#include <string.h>

#define DEVICE_TYPE              DEVICE_TYPE_SPI
#define FW_VERSION               1U
#define PROTOCOL_VERSION         1U

#define CAP_RAW_TRANSFER         0x01U
#define CAP_MODE_CONFIG          0x02U
#define CAP_RX_WINDOW            0x04U
#define CAP_CRC16                0x08U
#define CAP_DYNAMIC_NODE         0x80U
#define CAPABILITY_FLAGS         (CAP_RAW_TRANSFER | CAP_MODE_CONFIG | \
                                  CAP_RX_WINDOW | CAP_CRC16 | \
                                  CAP_DYNAMIC_NODE)

#define SPI_NODE_ID_MIN          0x21U
#define SPI_NODE_ID_MAX          0x30U

#define HEARTBEAT_MS             1000U
#define CAN_POLL_MS              1U
#define DISCOVERY_SERVICE_MS     1U
#define DISCOVERY_SLOT_MS        2U
#define DISCOVERY_SLOT_COUNT     32U
#define ID_ACK_REPEAT_GAP_MS     5U
#define TRANSFER_TIMEOUT_MS      1000U

#define DYN_RELEASE_BROADCAST_MARKER 0xA5U

#define SPI_CMD_CONFIG           0x01U
#define SPI_CMD_START            0x02U
#define SPI_CMD_EXECUTE          0x03U

#define SPI_STATUS_CONFIG        0x01U
#define SPI_STATUS_RX_CHUNK      0x02U
#define SPI_STATUS_DONE          0x03U

#define SPI_ACK_CONFIG           0x40U
#define SPI_ACK_START            0x41U
#define SPI_ACK_DATA             0x42U
#define SPI_ACK_EXECUTE          0x43U

#define SPI_RESULT_FAILED        0U
#define SPI_RESULT_OK            1U

#define SPI_ERROR_NONE           0U
#define SPI_ERROR_INVALID        1U
#define SPI_ERROR_BUSY           2U
#define SPI_ERROR_SEQUENCE       3U
#define SPI_ERROR_LENGTH         4U
#define SPI_ERROR_CRC            5U
#define SPI_ERROR_TIMEOUT        6U
#define SPI_ERROR_HARDWARE       7U
#define SPI_ERROR_DUPLICATE_ID   8U
#define SPI_ERROR_NOT_READY      9U

#define SPI_TRANSFER_MAX         512U
#define SPI_DATA_PAYLOAD_MAX     5U
#define SPI_RX_CHUNK_MAX         4U

typedef enum {
    DISCOVERY_RESPONSE_NONE = 0,
    DISCOVERY_RESPONSE_F0,
    DISCOVERY_RESPONSE_F2,
} discovery_response_t;

typedef struct {
    uint8_t active;
    uint8_t transfer_id;
    uint16_t tx_length;
    uint16_t total_length;
    uint16_t rx_skip;
    uint16_t received_length;
    uint16_t expected_sequence;
    uint32_t last_activity_ms;
} spi_transfer_session_t;

typedef struct {
    uint8_t valid;
    uint8_t transfer_id;
    uint8_t result;
    uint8_t error;
    uint16_t rx_skip;
    uint16_t rx_length;
    uint16_t rx_crc;
} spi_completed_transfer_t;

static uint8_t can_ready;
static uint8_t spi_ready;
static uint8_t my_node_id = NODE_ID_UNASSIGNED;
static uint8_t id_assigned;
static uint16_t node_token;
static uint8_t request_sequence;
static uint8_t spi_dummy_byte = 0xFFU;

static discovery_response_t pending_discovery_response;
static uint8_t pending_discovery_repeats;
static uint8_t pending_discovery_sequence;
static uint32_t pending_discovery_due_ms;

static spi_transfer_session_t transfer_session;
static spi_completed_transfer_t completed_transfer;
static uint8_t transfer_tx[SPI_TRANSFER_MAX];
static uint8_t transfer_rx[SPI_TRANSFER_MAX];

static mcu_port_spi_t spi_bus = {
    .spi = BOARD_SPI1_INSTANCE,
    .cs_port = BOARD_SPI1_CS_GPIO_PORT,
    .cs_pin = BOARD_SPI1_CS_GPIO_PIN,
    .sck_port = BOARD_SPI1_SCK_GPIO_PORT,
    .sck_pin = BOARD_SPI1_SCK_GPIO_PIN,
    .miso_port = BOARD_SPI1_MISO_GPIO_PORT,
    .miso_pin = BOARD_SPI1_MISO_GPIO_PIN,
    .mosi_port = BOARD_SPI1_MOSI_GPIO_PORT,
    .mosi_pin = BOARD_SPI1_MOSI_GPIO_PIN,
    .gpio_clock = BOARD_SPI1_GPIO_CLK,
    .spi_clock = BOARD_SPI1_CLK,
    .clock_bus = MCU_PORT_SPI_CLOCK_APB2,
    .mode = MCU_PORT_SPI_MODE_0,
    .divider = MCU_PORT_SPI_DIV_64,
    .bit_order = MCU_PORT_SPI_MSB_FIRST,
    .cs_active_low = 1U,
    .initialized = 0U,
};

static uint8_t divider_to_code(mcu_port_spi_divider_t divider);

static uint8_t spi_node_id_valid(uint8_t node_id)
{
    return node_id >= SPI_NODE_ID_MIN && node_id <= SPI_NODE_ID_MAX;
}

static uint8_t time_reached(uint32_t now, uint32_t deadline)
{
    return (int32_t)(now - deadline) >= 0 ? 1U : 0U;
}

static void reset_transfer_session(void)
{
    memset(&transfer_session, 0, sizeof(transfer_session));
}

static void reset_all_transfer_state(void)
{
    reset_transfer_session();
    memset(&completed_transfer, 0, sizeof(completed_transfer));
}

static uint32_t discovery_slot_delay_ms(uint8_t sequence)
{
    uint16_t mixed;

    mixed = (uint16_t)(node_token ^ ((uint16_t)sequence * 0x45D9U));
    mixed ^= (uint16_t)(mixed >> 7U);
    return (uint32_t)(mixed % DISCOVERY_SLOT_COUNT) * DISCOVERY_SLOT_MS;
}

static void send_f0(uint8_t sequence)
{
    node_send_discovery(CAN_ID_DISCOVERY, DYN_CMD_REQUEST_ID,
                        DEVICE_TYPE, FW_VERSION, CAPABILITY_FLAGS,
                        sequence, node_token, NODE_ID_UNASSIGNED);
}

static void send_f2(void)
{
    uint8_t data[8] = {0};

    data[0] = DYN_CMD_ID_ACK;
    data[1] = my_node_id;
    data[2] = DYN_MAGIC0;
    data[3] = DYN_MAGIC1;
    node_put_u16_le(data, 4U, node_token);
    data[6] = DEVICE_TYPE;
    data[7] = FW_VERSION;
    (void)bsp_can_send_std(CAN_ID_DISCOVERY, data, sizeof(data));
}

static void schedule_discovery_response(discovery_response_t response,
                                        uint8_t sequence,
                                        uint8_t repeat_count,
                                        uint8_t use_slot)
{
    uint32_t delay_ms = 0U;

    if (response == DISCOVERY_RESPONSE_NONE || repeat_count == 0U) {
        pending_discovery_response = DISCOVERY_RESPONSE_NONE;
        pending_discovery_repeats = 0U;
        return;
    }

    if (use_slot) {
        delay_ms = discovery_slot_delay_ms(sequence);
    }
    pending_discovery_response = response;
    pending_discovery_repeats = repeat_count;
    pending_discovery_sequence = sequence;
    pending_discovery_due_ms = mcu_port_millis() + delay_ms;
}

static void discovery_response_task(void)
{
    uint32_t now;

    if (!can_ready || pending_discovery_response == DISCOVERY_RESPONSE_NONE) {
        return;
    }

    now = mcu_port_millis();
    if (!time_reached(now, pending_discovery_due_ms)) {
        return;
    }

    if (pending_discovery_response == DISCOVERY_RESPONSE_F2 && id_assigned) {
        send_f2();
    } else if (pending_discovery_response == DISCOVERY_RESPONSE_F0 &&
               !id_assigned) {
        send_f0(pending_discovery_sequence);
    }

    if (pending_discovery_repeats > 0U) {
        --pending_discovery_repeats;
    }
    if (pending_discovery_repeats == 0U) {
        pending_discovery_response = DISCOVERY_RESPONSE_NONE;
    } else {
        pending_discovery_due_ms = now + ID_ACK_REPEAT_GAP_MS;
    }
}

static void send_spi_ack(uint8_t phase,
                         uint8_t result,
                         uint8_t transfer_id,
                         uint16_t processed_length,
                         uint8_t error)
{
    uint8_t data[8] = {0};

    data[0] = phase;
    data[1] = result;
    data[2] = my_node_id;
    data[3] = DEVICE_TYPE;
    data[4] = transfer_id;
    node_put_u16_le(data, 5U, processed_length);
    data[7] = error;
    (void)bsp_can_send_std(CAN_ID_ACK(my_node_id), data, sizeof(data));
}

static void send_config_status(uint8_t result, uint8_t error)
{
    uint8_t data[8] = {0};

    data[0] = SPI_STATUS_CONFIG;
    data[1] = result;
    data[2] = (uint8_t)spi_bus.mode;
    data[3] = divider_to_code(spi_bus.divider);
    data[4] = (uint8_t)spi_bus.bit_order;
    data[5] = spi_bus.cs_active_low;
    data[6] = spi_dummy_byte;
    data[7] = error;
    (void)bsp_can_send_std(CAN_ID_STATUS(my_node_id), data, sizeof(data));
}

static void send_completed_result(void)
{
    uint8_t data[8] = {0};
    uint16_t offset = 0U;
    uint16_t sequence = 0U;
    uint8_t chunk_length;

    if (!completed_transfer.valid) {
        return;
    }

    if (completed_transfer.result == SPI_RESULT_OK) {
        while (offset < completed_transfer.rx_length) {
            chunk_length = (uint8_t)(completed_transfer.rx_length - offset);
            if (chunk_length > SPI_RX_CHUNK_MAX) {
                chunk_length = SPI_RX_CHUNK_MAX;
            }

            memset(data, 0, sizeof(data));
            data[0] = SPI_STATUS_RX_CHUNK;
            data[1] = completed_transfer.transfer_id;
            node_put_u16_le(data, 2U, sequence);
            memcpy(&data[4],
                   &transfer_rx[completed_transfer.rx_skip + offset],
                   chunk_length);
            (void)bsp_can_send_std(CAN_ID_STATUS(my_node_id), data,
                                   (uint8_t)(4U + chunk_length));
            offset = (uint16_t)(offset + chunk_length);
            ++sequence;
        }
    }

    memset(data, 0, sizeof(data));
    data[0] = SPI_STATUS_DONE;
    data[1] = completed_transfer.transfer_id;
    data[2] = completed_transfer.result;
    node_put_u16_le(data, 3U, completed_transfer.rx_length);
    node_put_u16_le(data, 5U, completed_transfer.rx_crc);
    data[7] = completed_transfer.error;
    (void)bsp_can_send_std(CAN_ID_STATUS(my_node_id), data, sizeof(data));

    send_spi_ack(SPI_ACK_EXECUTE,
                 completed_transfer.result,
                 completed_transfer.transfer_id,
                 completed_transfer.rx_length,
                 completed_transfer.error);
}

static uint8_t divider_from_code(uint8_t code,
                                 mcu_port_spi_divider_t *divider)
{
    static const mcu_port_spi_divider_t dividers[] = {
        MCU_PORT_SPI_DIV_2,
        MCU_PORT_SPI_DIV_4,
        MCU_PORT_SPI_DIV_8,
        MCU_PORT_SPI_DIV_16,
        MCU_PORT_SPI_DIV_32,
        MCU_PORT_SPI_DIV_64,
        MCU_PORT_SPI_DIV_128,
        MCU_PORT_SPI_DIV_256,
    };

    if (divider == 0 || code >= (uint8_t)(sizeof(dividers) /
                                          sizeof(dividers[0]))) {
        return 0U;
    }
    *divider = dividers[code];
    return 1U;
}

static uint8_t divider_to_code(mcu_port_spi_divider_t divider)
{
    switch (divider) {
    case MCU_PORT_SPI_DIV_2:
        return 0U;
    case MCU_PORT_SPI_DIV_4:
        return 1U;
    case MCU_PORT_SPI_DIV_8:
        return 2U;
    case MCU_PORT_SPI_DIV_16:
        return 3U;
    case MCU_PORT_SPI_DIV_32:
        return 4U;
    case MCU_PORT_SPI_DIV_64:
        return 5U;
    case MCU_PORT_SPI_DIV_128:
        return 6U;
    case MCU_PORT_SPI_DIV_256:
        return 7U;
    default:
        return 0xFFU;
    }
}

static void handle_config(const bsp_can_frame_t *frame)
{
    mcu_port_spi_mode_t old_mode;
    mcu_port_spi_divider_t old_divider;
    mcu_port_spi_bit_order_t old_bit_order;
    uint8_t old_cs_active_low;
    mcu_port_spi_divider_t divider;
    uint8_t error = SPI_ERROR_NONE;

    if (transfer_session.active) {
        error = SPI_ERROR_BUSY;
    } else if (frame->dlc < 8U || frame->data[1] > MCU_PORT_SPI_MODE_3 ||
               !divider_from_code(frame->data[2], &divider) ||
               frame->data[3] > MCU_PORT_SPI_LSB_FIRST ||
               frame->data[4] > 1U ||
               frame->data[7] != PROTOCOL_VERSION) {
        error = SPI_ERROR_INVALID;
    }

    if (error != SPI_ERROR_NONE) {
        send_config_status(SPI_RESULT_FAILED, error);
        send_spi_ack(SPI_ACK_CONFIG, SPI_RESULT_FAILED, 0U, 0U, error);
        return;
    }

    old_mode = spi_bus.mode;
    old_divider = spi_bus.divider;
    old_bit_order = spi_bus.bit_order;
    old_cs_active_low = spi_bus.cs_active_low;

    spi_bus.cs_active_low = frame->data[4];
    spi_ready = mcu_port_spi_configure(
        &spi_bus,
        (mcu_port_spi_mode_t)frame->data[1],
        divider,
        (mcu_port_spi_bit_order_t)frame->data[3]);
    if (!spi_ready) {
        spi_bus.cs_active_low = old_cs_active_low;
        spi_ready = mcu_port_spi_configure(&spi_bus, old_mode, old_divider,
                                           old_bit_order);
        send_config_status(SPI_RESULT_FAILED, SPI_ERROR_HARDWARE);
        send_spi_ack(SPI_ACK_CONFIG, SPI_RESULT_FAILED, 0U, 0U,
                     SPI_ERROR_HARDWARE);
        return;
    }

    spi_dummy_byte = frame->data[5];
    send_config_status(SPI_RESULT_OK, SPI_ERROR_NONE);
    send_spi_ack(SPI_ACK_CONFIG, SPI_RESULT_OK, 0U, 0U, SPI_ERROR_NONE);
}

static uint8_t start_matches_active(const bsp_can_frame_t *frame)
{
    return frame->data[1] == transfer_session.transfer_id &&
           node_get_u16_le(frame->data, 2U) == transfer_session.tx_length &&
           node_get_u16_le(frame->data, 4U) ==
               transfer_session.total_length &&
           node_get_u16_le(frame->data, 6U) == transfer_session.rx_skip;
}

static void handle_start(const bsp_can_frame_t *frame)
{
    uint8_t transfer_id;
    uint16_t tx_length;
    uint16_t total_length;
    uint16_t rx_skip;

    if (frame->dlc < 8U) {
        send_spi_ack(SPI_ACK_START, SPI_RESULT_FAILED, 0U, 0U,
                     SPI_ERROR_INVALID);
        return;
    }

    transfer_id = frame->data[1];
    tx_length = node_get_u16_le(frame->data, 2U);
    total_length = node_get_u16_le(frame->data, 4U);
    rx_skip = node_get_u16_le(frame->data, 6U);

    if (!spi_ready) {
        send_spi_ack(SPI_ACK_START, SPI_RESULT_FAILED, transfer_id, 0U,
                     SPI_ERROR_NOT_READY);
        return;
    }
    if (transfer_session.active) {
        if (start_matches_active(frame)) {
            transfer_session.last_activity_ms = mcu_port_millis();
            send_spi_ack(SPI_ACK_START, SPI_RESULT_OK, transfer_id,
                         transfer_session.received_length, SPI_ERROR_NONE);
        } else {
            send_spi_ack(SPI_ACK_START, SPI_RESULT_FAILED, transfer_id,
                         transfer_session.received_length, SPI_ERROR_BUSY);
        }
        return;
    }
    if (completed_transfer.valid &&
        completed_transfer.transfer_id == transfer_id) {
        send_spi_ack(SPI_ACK_START, SPI_RESULT_FAILED, transfer_id, 0U,
                     SPI_ERROR_DUPLICATE_ID);
        return;
    }
    if (total_length == 0U || total_length > SPI_TRANSFER_MAX ||
        tx_length > total_length || rx_skip > total_length) {
        send_spi_ack(SPI_ACK_START, SPI_RESULT_FAILED, transfer_id, 0U,
                     SPI_ERROR_LENGTH);
        return;
    }

    memset(&completed_transfer, 0, sizeof(completed_transfer));
    reset_transfer_session();
    transfer_session.active = 1U;
    transfer_session.transfer_id = transfer_id;
    transfer_session.tx_length = tx_length;
    transfer_session.total_length = total_length;
    transfer_session.rx_skip = rx_skip;
    transfer_session.last_activity_ms = mcu_port_millis();
    send_spi_ack(SPI_ACK_START, SPI_RESULT_OK, transfer_id, 0U,
                 SPI_ERROR_NONE);
}

static void handle_data(const bsp_can_frame_t *frame)
{
    uint8_t transfer_id;
    uint16_t sequence;
    uint8_t payload_length;
    uint16_t remaining;

    if (frame->dlc < 3U) {
        return;
    }

    transfer_id = frame->data[0];
    sequence = node_get_u16_le(frame->data, 1U);
    if (!transfer_session.active ||
        transfer_id != transfer_session.transfer_id) {
        send_spi_ack(SPI_ACK_DATA, SPI_RESULT_FAILED, transfer_id, 0U,
                     SPI_ERROR_NOT_READY);
        return;
    }
    if (sequence != transfer_session.expected_sequence) {
        send_spi_ack(SPI_ACK_DATA, SPI_RESULT_FAILED, transfer_id,
                     transfer_session.received_length,
                     SPI_ERROR_SEQUENCE);
        reset_transfer_session();
        return;
    }

    payload_length = (uint8_t)(frame->dlc - 3U);
    remaining = (uint16_t)(transfer_session.tx_length -
                           transfer_session.received_length);
    if (payload_length == 0U || payload_length > SPI_DATA_PAYLOAD_MAX ||
        payload_length > remaining) {
        send_spi_ack(SPI_ACK_DATA, SPI_RESULT_FAILED, transfer_id,
                     transfer_session.received_length, SPI_ERROR_LENGTH);
        reset_transfer_session();
        return;
    }

    memcpy(&transfer_tx[transfer_session.received_length],
           &frame->data[3], payload_length);
    transfer_session.received_length = (uint16_t)(
        transfer_session.received_length + payload_length);
    ++transfer_session.expected_sequence;
    transfer_session.last_activity_ms = mcu_port_millis();

    if (transfer_session.received_length == transfer_session.tx_length) {
        send_spi_ack(SPI_ACK_DATA, SPI_RESULT_OK, transfer_id,
                     transfer_session.received_length, SPI_ERROR_NONE);
    }
}

static void save_failed_completion(uint8_t transfer_id, uint8_t error)
{
    completed_transfer.valid = 1U;
    completed_transfer.transfer_id = transfer_id;
    completed_transfer.result = SPI_RESULT_FAILED;
    completed_transfer.error = error;
    completed_transfer.rx_skip = 0U;
    completed_transfer.rx_length = 0U;
    completed_transfer.rx_crc = 0xFFFFU;
    reset_transfer_session();
}

static void handle_execute(const bsp_can_frame_t *frame)
{
    uint8_t transfer_id;
    uint16_t expected_crc;
    uint16_t actual_crc;
    uint16_t index;

    if (frame->dlc < 8U) {
        send_spi_ack(SPI_ACK_EXECUTE, SPI_RESULT_FAILED, 0U, 0U,
                     SPI_ERROR_INVALID);
        return;
    }

    transfer_id = frame->data[1];
    if (completed_transfer.valid &&
        completed_transfer.transfer_id == transfer_id) {
        send_completed_result();
        return;
    }
    if (!transfer_session.active ||
        transfer_id != transfer_session.transfer_id) {
        send_spi_ack(SPI_ACK_EXECUTE, SPI_RESULT_FAILED, transfer_id, 0U,
                     SPI_ERROR_NOT_READY);
        return;
    }
    if (frame->data[7] != PROTOCOL_VERSION) {
        save_failed_completion(transfer_id, SPI_ERROR_INVALID);
        send_completed_result();
        return;
    }
    if (transfer_session.received_length != transfer_session.tx_length) {
        send_spi_ack(SPI_ACK_EXECUTE, SPI_RESULT_FAILED, transfer_id,
                     transfer_session.received_length, SPI_ERROR_LENGTH);
        return;
    }

    expected_crc = node_get_u16_le(frame->data, 2U);
    actual_crc = node_crc16_ccitt(transfer_tx,
                                  transfer_session.tx_length);
    if (expected_crc != actual_crc) {
        save_failed_completion(transfer_id, SPI_ERROR_CRC);
        send_completed_result();
        return;
    }

    for (index = transfer_session.tx_length;
         index < transfer_session.total_length; ++index) {
        transfer_tx[index] = spi_dummy_byte;
    }

    completed_transfer.transfer_id = transfer_id;
    completed_transfer.rx_skip = transfer_session.rx_skip;
    completed_transfer.rx_length = (uint16_t)(
        transfer_session.total_length - transfer_session.rx_skip);

    if (!mcu_port_spi_transaction(&spi_bus, transfer_tx, transfer_rx,
                                  transfer_session.total_length,
                                  spi_dummy_byte)) {
        save_failed_completion(transfer_id, SPI_ERROR_HARDWARE);
        send_completed_result();
        return;
    }

    completed_transfer.valid = 1U;
    completed_transfer.result = SPI_RESULT_OK;
    completed_transfer.error = SPI_ERROR_NONE;
    completed_transfer.rx_crc = node_crc16_ccitt(
        &transfer_rx[completed_transfer.rx_skip],
        completed_transfer.rx_length);
    reset_transfer_session();
    send_completed_result();
}

static void handle_command(const bsp_can_frame_t *frame)
{
    if (frame == 0 || frame->dlc < 1U) {
        return;
    }

    switch (frame->data[0]) {
    case SPI_CMD_CONFIG:
        handle_config(frame);
        break;
    case SPI_CMD_START:
        handle_start(frame);
        break;
    case SPI_CMD_EXECUTE:
        handle_execute(frame);
        break;
    default:
        send_spi_ack(frame->data[0], SPI_RESULT_FAILED, 0U, 0U,
                     SPI_ERROR_INVALID);
        break;
    }
}

static void handle_assign_id(const bsp_can_frame_t *frame)
{
    uint8_t new_node_id;

    if (frame->dlc < 8U || frame->data[2] != DYN_MAGIC0 ||
        frame->data[3] != DYN_MAGIC1 ||
        frame->data[6] != DEVICE_TYPE ||
        frame->data[7] != PROTOCOL_VERSION ||
        node_get_u16_le(frame->data, 4U) != node_token) {
        return;
    }

    new_node_id = frame->data[1];
    if (!spi_node_id_valid(new_node_id)) {
        return;
    }

    my_node_id = new_node_id;
    id_assigned = 1U;
    reset_all_transfer_state();
    schedule_discovery_response(DISCOVERY_RESPONSE_F2, request_sequence,
                                ID_ACK_REPEAT_COUNT, 0U);
    (void)node_send_hello(my_node_id, DEVICE_TYPE, FW_VERSION,
                          CAPABILITY_FLAGS);
}

static void handle_discovery(const bsp_can_frame_t *frame)
{
    uint8_t release_is_broadcast = 0U;
    uint8_t release_is_targeted = 0U;

    if (frame == 0 || frame->dlc < 1U) {
        return;
    }

    if (frame->data[0] == DYN_CMD_ASSIGN_ID) {
        handle_assign_id(frame);
        return;
    }

    if (frame->data[0] == DYN_CMD_RELEASE_ID && frame->dlc >= 8U &&
        frame->data[1] == DYN_MAGIC0 &&
        frame->data[2] == DYN_MAGIC1 &&
        frame->data[7] == PROTOCOL_VERSION) {
        release_is_broadcast =
            frame->data[3] == DYN_RELEASE_BROADCAST_MARKER ? 1U : 0U;
        release_is_targeted =
            frame->data[3] == 0U && frame->data[6] == DEVICE_TYPE &&
            node_get_u16_le(frame->data, 4U) == node_token ? 1U : 0U;
        if (release_is_broadcast || release_is_targeted) {
            my_node_id = NODE_ID_UNASSIGNED;
            id_assigned = 0U;
            reset_all_transfer_state();
            ++request_sequence;
            schedule_discovery_response(DISCOVERY_RESPONSE_F0,
                                        request_sequence, 1U, 1U);
        }
        return;
    }

    if (frame->data[0] == DYN_CMD_REQUEST_ID && frame->dlc >= 8U &&
        frame->data[1] == DEVICE_TYPE &&
        node_get_u16_le(frame->data, 5U) == 0U) {
        schedule_discovery_response(
            id_assigned ? DISCOVERY_RESPONSE_F2 : DISCOVERY_RESPONSE_F0,
            frame->data[4],
            id_assigned ? ID_ACK_REPEAT_COUNT : 1U,
            1U);
    }
}

static void check_transfer_timeout(void)
{
    uint32_t now;
    uint8_t transfer_id;
    uint16_t received_length;

    if (!transfer_session.active) {
        return;
    }

    now = mcu_port_millis();
    if ((uint32_t)(now - transfer_session.last_activity_ms) <
        TRANSFER_TIMEOUT_MS) {
        return;
    }

    transfer_id = transfer_session.transfer_id;
    received_length = transfer_session.received_length;
    reset_transfer_session();
    send_spi_ack(SPI_ACK_EXECUTE, SPI_RESULT_FAILED, transfer_id,
                 received_length, SPI_ERROR_TIMEOUT);
}

static void heartbeat_task(void)
{
    if (!can_ready) {
        return;
    }

    if (id_assigned) {
        (void)node_send_hello(my_node_id, DEVICE_TYPE, FW_VERSION,
                              CAPABILITY_FLAGS);
    } else {
        ++request_sequence;
        schedule_discovery_response(DISCOVERY_RESPONSE_F0,
                                    request_sequence, 1U, 1U);
    }
}

static void can_poll_task(void)
{
    bsp_can_frame_t frame;

    if (!can_ready) {
        return;
    }

    check_transfer_timeout();
    while (bsp_can_receive(&frame)) {
        if (frame.id == CAN_ID_DISCOVERY) {
            handle_discovery(&frame);
        } else if (id_assigned && frame.id == CAN_ID_CMD(my_node_id)) {
            handle_command(&frame);
        } else if (id_assigned && frame.id == CAN_ID_DATA(my_node_id)) {
            handle_data(&frame);
        }
    }
}

void app_setup(void)
{
    node_token = node_read_token(DEVICE_TYPE);
    reset_all_transfer_state();
    spi_ready = mcu_port_spi_init(&spi_bus);
    can_ready = bsp_can_init(BSP_CAN_BITRATE_500K);

    if (can_ready) {
        heartbeat_task();
        (void)vibe_task_every_ms(DISCOVERY_SERVICE_MS,
                                 discovery_response_task);
        (void)vibe_task_every_ms(HEARTBEAT_MS, heartbeat_task);
        (void)vibe_task_every_ms(CAN_POLL_MS, can_poll_task);
    }
}
