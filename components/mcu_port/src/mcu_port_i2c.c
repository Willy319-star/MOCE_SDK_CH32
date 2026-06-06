#include "mcu_port_i2c.h"
#include "ch32v20x_gpio.h"
#include "ch32v20x_i2c.h"
#include "ch32v20x_rcc.h"

#define I2C_TIMEOUT 100000U

static uint8_t wait_event(I2C_TypeDef *i2c, uint32_t event)
{
    uint32_t guard = I2C_TIMEOUT;

    while (I2C_CheckEvent(i2c, event) != READY) {
        if (--guard == 0U) {
            return 0U;
        }
    }

    return 1U;
}

static uint8_t wait_flag_clear(I2C_TypeDef *i2c, uint32_t flag)
{
    uint32_t guard = I2C_TIMEOUT;

    while (I2C_GetFlagStatus(i2c, flag) != RESET) {
        if (--guard == 0U) {
            return 0U;
        }
    }

    return 1U;
}

static uint8_t wait_flag_set(I2C_TypeDef *i2c, uint32_t flag)
{
    uint32_t guard = I2C_TIMEOUT;

    while (I2C_GetFlagStatus(i2c, flag) == RESET) {
        if (--guard == 0U) {
            return 0U;
        }
    }

    return 1U;
}

static void clear_addr_flag(I2C_TypeDef *i2c)
{
    volatile uint16_t tmp;

    tmp = i2c->STAR1;
    tmp = i2c->STAR2;
    (void)tmp;
}

uint8_t mcu_port_i2c_init(mcu_port_i2c_t *i2c)
{
    GPIO_InitTypeDef gpio = {0};
    I2C_InitTypeDef init = {0};

    if (i2c == 0) {
        return 0U;
    }

    RCC_APB2PeriphClockCmd(i2c->gpio_clock | RCC_APB2Periph_AFIO, ENABLE);
    RCC_APB1PeriphClockCmd(i2c->i2c_clock, ENABLE);

    gpio.GPIO_Pin = i2c->scl_pin;
    gpio.GPIO_Mode = GPIO_Mode_AF_OD;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(i2c->scl_port, &gpio);

    gpio.GPIO_Pin = i2c->sda_pin;
    gpio.GPIO_Mode = GPIO_Mode_AF_OD;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(i2c->sda_port, &gpio);

    I2C_DeInit(i2c->i2c);
    init.I2C_ClockSpeed = i2c->speed_hz;
    init.I2C_Mode = I2C_Mode_I2C;
    init.I2C_DutyCycle = I2C_DutyCycle_2;
    init.I2C_OwnAddress1 = 0x00U;
    init.I2C_Ack = I2C_Ack_Enable;
    init.I2C_AcknowledgedAddress = I2C_AcknowledgedAddress_7bit;
    I2C_Init(i2c->i2c, &init);
    I2C_Cmd(i2c->i2c, ENABLE);

    i2c->initialized = 1U;
    return 1U;
}

uint8_t mcu_port_i2c_is_ready(mcu_port_i2c_t *i2c, uint8_t address)
{
    uint8_t ok;

    if (i2c == 0 || !i2c->initialized) {
        return 0U;
    }

    if (!wait_flag_clear(i2c->i2c, I2C_FLAG_BUSY)) {
        return 0U;
    }

    I2C_GenerateSTART(i2c->i2c, ENABLE);
    ok = wait_event(i2c->i2c, I2C_EVENT_MASTER_MODE_SELECT);
    if (ok) {
        I2C_Send7bitAddress(i2c->i2c, (uint8_t)(address << 1), I2C_Direction_Transmitter);
        ok = wait_event(i2c->i2c, I2C_EVENT_MASTER_TRANSMITTER_MODE_SELECTED);
    }
    I2C_GenerateSTOP(i2c->i2c, ENABLE);

    return ok;
}

uint8_t mcu_port_i2c_write(mcu_port_i2c_t *i2c, uint8_t address, const uint8_t *data, uint16_t length)
{
    uint16_t i;

    if (i2c == 0 || data == 0 || length == 0U || !i2c->initialized) {
        return 0U;
    }

    if (!wait_flag_clear(i2c->i2c, I2C_FLAG_BUSY)) {
        return 0U;
    }

    I2C_GenerateSTART(i2c->i2c, ENABLE);
    if (!wait_event(i2c->i2c, I2C_EVENT_MASTER_MODE_SELECT)) {
        I2C_GenerateSTOP(i2c->i2c, ENABLE);
        return 0U;
    }

    I2C_Send7bitAddress(i2c->i2c, (uint8_t)(address << 1), I2C_Direction_Transmitter);
    if (!wait_event(i2c->i2c, I2C_EVENT_MASTER_TRANSMITTER_MODE_SELECTED)) {
        I2C_GenerateSTOP(i2c->i2c, ENABLE);
        return 0U;
    }

    for (i = 0U; i < length; ++i) {
        I2C_SendData(i2c->i2c, data[i]);
        if (!wait_event(i2c->i2c, I2C_EVENT_MASTER_BYTE_TRANSMITTED)) {
            I2C_GenerateSTOP(i2c->i2c, ENABLE);
            return 0U;
        }
    }

    I2C_GenerateSTOP(i2c->i2c, ENABLE);
    return 1U;
}

uint8_t mcu_port_i2c_write_reg(mcu_port_i2c_t *i2c, uint8_t address, uint8_t reg, uint8_t value)
{
    if (i2c == 0 || !i2c->initialized) {
        return 0U;
    }

    if (!wait_flag_clear(i2c->i2c, I2C_FLAG_BUSY)) {
        return 0U;
    }

    I2C_GenerateSTART(i2c->i2c, ENABLE);
    if (!wait_event(i2c->i2c, I2C_EVENT_MASTER_MODE_SELECT)) {
        I2C_GenerateSTOP(i2c->i2c, ENABLE);
        return 0U;
    }

    I2C_Send7bitAddress(i2c->i2c, (uint8_t)(address << 1), I2C_Direction_Transmitter);
    if (!wait_event(i2c->i2c, I2C_EVENT_MASTER_TRANSMITTER_MODE_SELECTED)) {
        I2C_GenerateSTOP(i2c->i2c, ENABLE);
        return 0U;
    }

    I2C_SendData(i2c->i2c, reg);
    if (!wait_event(i2c->i2c, I2C_EVENT_MASTER_BYTE_TRANSMITTED)) {
        I2C_GenerateSTOP(i2c->i2c, ENABLE);
        return 0U;
    }

    I2C_SendData(i2c->i2c, value);
    if (!wait_event(i2c->i2c, I2C_EVENT_MASTER_BYTE_TRANSMITTED)) {
        I2C_GenerateSTOP(i2c->i2c, ENABLE);
        return 0U;
    }

    I2C_GenerateSTOP(i2c->i2c, ENABLE);
    return 1U;
}

uint8_t mcu_port_i2c_read_regs(mcu_port_i2c_t *i2c, uint8_t address, uint8_t reg, uint8_t *data, uint16_t length)
{
    uint16_t i;

    if (i2c == 0 || data == 0 || length == 0U || !i2c->initialized) {
        return 0U;
    }

    if (!wait_flag_clear(i2c->i2c, I2C_FLAG_BUSY)) {
        return 0U;
    }

    I2C_AcknowledgeConfig(i2c->i2c, ENABLE);
    I2C_GenerateSTART(i2c->i2c, ENABLE);
    if (!wait_event(i2c->i2c, I2C_EVENT_MASTER_MODE_SELECT)) {
        I2C_GenerateSTOP(i2c->i2c, ENABLE);
        return 0U;
    }

    I2C_Send7bitAddress(i2c->i2c, (uint8_t)(address << 1), I2C_Direction_Transmitter);
    if (!wait_event(i2c->i2c, I2C_EVENT_MASTER_TRANSMITTER_MODE_SELECTED)) {
        I2C_GenerateSTOP(i2c->i2c, ENABLE);
        return 0U;
    }

    I2C_SendData(i2c->i2c, reg);
    if (!wait_event(i2c->i2c, I2C_EVENT_MASTER_BYTE_TRANSMITTED)) {
        I2C_GenerateSTOP(i2c->i2c, ENABLE);
        return 0U;
    }

    I2C_GenerateSTART(i2c->i2c, ENABLE);
    if (!wait_event(i2c->i2c, I2C_EVENT_MASTER_MODE_SELECT)) {
        I2C_GenerateSTOP(i2c->i2c, ENABLE);
        return 0U;
    }

    I2C_Send7bitAddress(i2c->i2c, (uint8_t)(address << 1), I2C_Direction_Receiver);
    if (length == 1U) {
        if (!wait_flag_set(i2c->i2c, I2C_FLAG_ADDR)) {
            I2C_GenerateSTOP(i2c->i2c, ENABLE);
            return 0U;
        }

        I2C_AcknowledgeConfig(i2c->i2c, DISABLE);
        clear_addr_flag(i2c->i2c);
        I2C_GenerateSTOP(i2c->i2c, ENABLE);

        if (!wait_flag_set(i2c->i2c, I2C_FLAG_RXNE)) {
            I2C_AcknowledgeConfig(i2c->i2c, ENABLE);
            return 0U;
        }

        data[0] = I2C_ReceiveData(i2c->i2c);
        I2C_AcknowledgeConfig(i2c->i2c, ENABLE);
        return 1U;
    }

    if (!wait_event(i2c->i2c, I2C_EVENT_MASTER_RECEIVER_MODE_SELECTED)) {
        I2C_GenerateSTOP(i2c->i2c, ENABLE);
        return 0U;
    }

    for (i = 0U; i < length; ++i) {
        if (i == (uint16_t)(length - 1U)) {
            I2C_AcknowledgeConfig(i2c->i2c, DISABLE);
            I2C_GenerateSTOP(i2c->i2c, ENABLE);
        }

        if (!wait_flag_set(i2c->i2c, I2C_FLAG_RXNE)) {
            I2C_GenerateSTOP(i2c->i2c, ENABLE);
            I2C_AcknowledgeConfig(i2c->i2c, ENABLE);
            return 0U;
        }

        data[i] = I2C_ReceiveData(i2c->i2c);
    }

    I2C_AcknowledgeConfig(i2c->i2c, ENABLE);
    return 1U;
}

uint8_t mcu_port_i2c_read_reg(mcu_port_i2c_t *i2c, uint8_t address, uint8_t reg, uint8_t *value)
{
    if (value == 0) {
        return 0U;
    }

    return mcu_port_i2c_read_regs(i2c, address, reg, value, 1U);
}

