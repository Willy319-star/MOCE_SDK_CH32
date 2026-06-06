#include "board.h"
#include "board_pins.h"
#include "ch32v20x_gpio.h"
#include "ch32v20x_rcc.h"
#include "ch32v20x_usart.h"

#include <sys/stat.h>

static uint8_t uart_ready;

void board_uart_init(uint32_t baudrate)
{
    GPIO_InitTypeDef gpio = {0};
    USART_InitTypeDef uart = {0};

    RCC_APB2PeriphClockCmd(BOARD_UART_GPIO_CLK | BOARD_UART_CLK, ENABLE);

    gpio.GPIO_Pin = BOARD_UART_TX_GPIO_PIN;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    gpio.GPIO_Mode = GPIO_Mode_AF_PP;
    GPIO_Init(BOARD_UART_TX_GPIO_PORT, &gpio);

    gpio.GPIO_Pin = BOARD_UART_RX_GPIO_PIN;
    gpio.GPIO_Mode = GPIO_Mode_IN_FLOATING;
    GPIO_Init(BOARD_UART_RX_GPIO_PORT, &gpio);

    uart.USART_BaudRate = baudrate;
    uart.USART_WordLength = USART_WordLength_8b;
    uart.USART_StopBits = USART_StopBits_1;
    uart.USART_Parity = USART_Parity_No;
    uart.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    uart.USART_Mode = USART_Mode_Tx | USART_Mode_Rx;
    USART_Init(BOARD_UART_INSTANCE, &uart);
    USART_Cmd(BOARD_UART_INSTANCE, ENABLE);

    uart_ready = 1U;
}

void board_uart_write(const uint8_t *data, uint16_t size)
{
    if (uart_ready == 0U || data == 0) {
        return;
    }

    for (uint16_t i = 0; i < size; ++i) {
        while (USART_GetFlagStatus(BOARD_UART_INSTANCE, USART_FLAG_TXE) == RESET) {
        }
        USART_SendData(BOARD_UART_INSTANCE, data[i]);
    }
}

uint8_t board_uart_read_byte(uint8_t *data)
{
    if (uart_ready == 0U || data == 0) {
        return 0U;
    }

    if (USART_GetFlagStatus(BOARD_UART_INSTANCE, USART_FLAG_RXNE) == RESET) {
        return 0U;
    }

    *data = (uint8_t)USART_ReceiveData(BOARD_UART_INSTANCE);
    return 1U;
}

int _close(int file)
{
    (void)file;
    return -1;
}

int _fstat(int file, struct stat *st)
{
    (void)file;
    st->st_mode = S_IFCHR;
    return 0;
}

int _isatty(int file)
{
    (void)file;
    return 1;
}

int _lseek(int file, int ptr, int dir)
{
    (void)file;
    (void)ptr;
    (void)dir;
    return 0;
}

int _read(int file, char *ptr, int len)
{
    (void)file;
    (void)ptr;
    (void)len;
    return 0;
}

