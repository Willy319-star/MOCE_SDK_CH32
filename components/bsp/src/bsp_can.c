#include "bsp_can.h"

#include "board_pins.h"
#include "ch32v20x_can.h"
#include "ch32v20x_gpio.h"
#include "ch32v20x_rcc.h"

#define BSP_CAN_TX_TIMEOUT 20000U
#define BSP_CAN_INIT_TIMEOUT 0x0000FFFFUL

static uint8_t can_init_stage;

static void can_gpio_init(void)
{
    GPIO_InitTypeDef gpio = {0};

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_AFIO | BOARD_CAN_GPIO_CLK, ENABLE);
    RCC_APB1PeriphClockCmd(BOARD_CAN_CLK, ENABLE);

    /* The schematic routes CAN1 to PA11/PA12. Force no-remap so CAN is not moved to PB8/PB9. */
    AFIO->PCFR1 &= ~(uint32_t)AFIO_PCFR1_CAN_REMAP;

    gpio.GPIO_Pin = BOARD_CAN_TX_GPIO_PIN;
    gpio.GPIO_Mode = GPIO_Mode_AF_PP;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(BOARD_CAN_TX_GPIO_PORT, &gpio);

    gpio.GPIO_Pin = BOARD_CAN_RX_GPIO_PIN;
    gpio.GPIO_Mode = GPIO_Mode_IPU;
    GPIO_Init(BOARD_CAN_RX_GPIO_PORT, &gpio);
}

static void can_filter_accept_all(void)
{
    CAN_FilterInitTypeDef filter = {0};

    filter.CAN_FilterNumber = 0U;
    filter.CAN_FilterMode = CAN_FilterMode_IdMask;
    filter.CAN_FilterScale = CAN_FilterScale_32bit;
    filter.CAN_FilterIdHigh = 0U;
    filter.CAN_FilterIdLow = 0U;
    filter.CAN_FilterMaskIdHigh = 0U;
    filter.CAN_FilterMaskIdLow = 0U;
    filter.CAN_FilterFIFOAssignment = CAN_Filter_FIFO0;
    filter.CAN_FilterActivation = ENABLE;
    CAN_FilterInit(&filter);
}

uint8_t bsp_can_init(bsp_can_bitrate_t bitrate)
{
    uint32_t timeout;
    uint32_t bit_timing;

    if (bitrate == BSP_CAN_BITRATE_50K) {
        /* SYSCLK/HCLK=96 MHz and PCLK1=HCLK/2=48 MHz.
         * 20 tq/bit, prescaler=48 -> 50 kbit/s. */
        bit_timing = ((uint32_t)CAN_Mode_Normal << 30) |
                     ((uint32_t)CAN_SJW_1tq << 24) |
                     ((uint32_t)CAN_BS1_15tq << 16) |
                     ((uint32_t)CAN_BS2_4tq << 20) |
                     (48U - 1U);
    } else if (bitrate == BSP_CAN_BITRATE_500K) {
        /* SYSCLK/HCLK=96 MHz and PCLK1=HCLK/2=48 MHz.
         * 16 tq/bit, prescaler=6 -> 500 kbit/s,
         * sample point=(1+13)/16=87.5%. */
        bit_timing = ((uint32_t)CAN_Mode_Normal << 30) |
                     ((uint32_t)CAN_SJW_1tq << 24) |
                     ((uint32_t)CAN_BS1_13tq << 16) |
                     ((uint32_t)CAN_BS2_2tq << 20) |
                     (6U - 1U);
    } else {
        can_init_stage = 4U;
        return 0U;
    }

    can_init_stage = 0U;
    can_gpio_init();
    CAN_DeInit(BOARD_CAN_INSTANCE);

    BOARD_CAN_INSTANCE->CTLR &= ~(uint32_t)CAN_CTLR_SLEEP;
    BOARD_CAN_INSTANCE->CTLR |= CAN_CTLR_INRQ;

    timeout = 0U;
    while (((BOARD_CAN_INSTANCE->STATR & CAN_STATR_INAK) == 0U) &&
           (timeout < BSP_CAN_INIT_TIMEOUT)) {
        ++timeout;
    }

    if ((BOARD_CAN_INSTANCE->STATR & CAN_STATR_INAK) == 0U) {
        can_init_stage = 1U;
        return 0U;
    }

    BOARD_CAN_INSTANCE->CTLR &= ~(uint32_t)(CAN_CTLR_TTCM |
                                            CAN_CTLR_ABOM |
                                            CAN_CTLR_AWUM |
                                            CAN_CTLR_NART |
                                            CAN_CTLR_RFLM |
                                            CAN_CTLR_TXFP);
    BOARD_CAN_INSTANCE->CTLR |= CAN_CTLR_ABOM;

    BOARD_CAN_INSTANCE->BTIMR = bit_timing;

    BOARD_CAN_INSTANCE->CTLR &= ~(uint32_t)CAN_CTLR_INRQ;

    timeout = 0U;
    while (((BOARD_CAN_INSTANCE->STATR & CAN_STATR_INAK) != 0U) &&
           (timeout < BSP_CAN_INIT_TIMEOUT)) {
        ++timeout;
    }

    if ((BOARD_CAN_INSTANCE->STATR & CAN_STATR_INAK) != 0U) {
        can_init_stage = 2U;
        return 0U;
    }

    can_filter_accept_all();
    can_init_stage = 3U;
    return 1U;
}

uint8_t bsp_can_init_50k(void)
{
    return bsp_can_init(BSP_CAN_BITRATE_50K);
}

uint8_t bsp_can_send_std(uint16_t id, const uint8_t *data, uint8_t len)
{
    CanTxMsg tx = {0};
    uint8_t mailbox;
    uint32_t timeout = 0U;
    uint8_t i;

    if ((data == 0) || (len > BSP_CAN_MAX_DATA_LEN) || (id > 0x7FFU)) {
        return 0U;
    }

    tx.StdId = id;
    tx.IDE = CAN_Id_Standard;
    tx.RTR = CAN_RTR_Data;
    tx.DLC = len;

    for (i = 0U; i < len; ++i) {
        tx.Data[i] = data[i];
    }

    mailbox = CAN_Transmit(BOARD_CAN_INSTANCE, &tx);
    if (mailbox == CAN_TxStatus_NoMailBox) {
        return 0U;
    }

    while ((CAN_TransmitStatus(BOARD_CAN_INSTANCE, mailbox) != CAN_TxStatus_Ok) &&
           (timeout < BSP_CAN_TX_TIMEOUT)) {
        ++timeout;
    }

    return (timeout < BSP_CAN_TX_TIMEOUT) ? 1U : 0U;
}

uint8_t bsp_can_receive(bsp_can_frame_t *frame)
{
    CanRxMsg rx = {0};
    uint8_t i;

    if (frame == 0) {
        return 0U;
    }

    if (CAN_MessagePending(BOARD_CAN_INSTANCE, CAN_FIFO0) == 0U) {
        return 0U;
    }

    CAN_Receive(BOARD_CAN_INSTANCE, CAN_FIFO0, &rx);
    frame->id = (rx.IDE == CAN_Id_Standard) ? rx.StdId : rx.ExtId;
    frame->dlc = rx.DLC;

    for (i = 0U; i < BSP_CAN_MAX_DATA_LEN; ++i) {
        frame->data[i] = rx.Data[i];
    }

    return 1U;
}

void bsp_can_get_status(bsp_can_status_t *status)
{
    if (status == 0) {
        return;
    }

    status->tx_error_count = CAN_GetLSBTransmitErrorCounter(BOARD_CAN_INSTANCE);
    status->rx_error_count = CAN_GetReceiveErrorCounter(BOARD_CAN_INSTANCE);
    status->last_error_code = CAN_GetLastErrorCode(BOARD_CAN_INSTANCE);
    status->error_warning = (CAN_GetFlagStatus(BOARD_CAN_INSTANCE, CAN_FLAG_EWG) != RESET) ? 1U : 0U;
    status->error_passive = (CAN_GetFlagStatus(BOARD_CAN_INSTANCE, CAN_FLAG_EPV) != RESET) ? 1U : 0U;
    status->bus_off = (CAN_GetFlagStatus(BOARD_CAN_INSTANCE, CAN_FLAG_BOF) != RESET) ? 1U : 0U;
    status->init_stage = can_init_stage;
    status->rx_sample = ((BOARD_CAN_INSTANCE->STATR & CAN_STATR_RX) != 0U) ? 1U : 0U;
}

