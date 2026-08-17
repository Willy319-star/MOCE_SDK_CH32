#include "mcu_port_i2c.h"
#include "ch32v20x_gpio.h"
#include "ch32v20x_i2c.h"
#include "ch32v20x_rcc.h"

#define I2C_TIMEOUT 100000U
#define I2C_RECOVERY_PULSES 9U

static void i2c_recovery_delay(void)
{
    volatile uint32_t i;

    for (i = 0U; i < 600U; ++i) {
        __asm volatile ("nop");
    }
}

static uint8_t wait_event(I2C_TypeDef *i2c, uint32_t event)
{
    uint32_t guard = I2C_TIMEOUT;

    while (I2C_CheckEvent(i2c, event) != READY) {
        if (I2C_GetFlagStatus(i2c, I2C_FLAG_AF) != RESET) {
            return 0U;
        }
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
        if (I2C_GetFlagStatus(i2c, I2C_FLAG_AF) != RESET) {
            return 0U;
        }
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

static void i2c_stop_and_clear_error(I2C_TypeDef *i2c)
{
    I2C_GenerateSTOP(i2c, ENABLE);
    I2C_ClearFlag(i2c, I2C_FLAG_AF | I2C_FLAG_BERR | I2C_FLAG_ARLO | I2C_FLAG_OVR);
}

static uint8_t wait_addr_ack_or_nack(I2C_TypeDef *i2c)
{
    uint32_t guard = I2C_TIMEOUT;

    while (guard-- > 0U) {
        if (I2C_GetFlagStatus(i2c, I2C_FLAG_ADDR) != RESET) {
            clear_addr_flag(i2c);
            return 1U;
        }

        if (I2C_GetFlagStatus(i2c, I2C_FLAG_AF) != RESET) {
            I2C_ClearFlag(i2c, I2C_FLAG_AF);
            return 0U;
        }
    }

    return 0U;
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
    I2C_ClearFlag(i2c->i2c, I2C_FLAG_AF | I2C_FLAG_BERR | I2C_FLAG_ARLO | I2C_FLAG_OVR);

    i2c->initialized = 1U;
    return 1U;
}

uint8_t mcu_port_i2c_recover_bus(mcu_port_i2c_t *i2c)
{
    GPIO_InitTypeDef gpio = {0};
    uint8_t i;

    if (i2c == 0) {
        return 0U;
    }

    RCC_APB2PeriphClockCmd(i2c->gpio_clock | RCC_APB2Periph_AFIO, ENABLE);
    RCC_APB1PeriphClockCmd(i2c->i2c_clock, ENABLE);

    I2C_Cmd(i2c->i2c, DISABLE);
    I2C_DeInit(i2c->i2c);

    gpio.GPIO_Mode = GPIO_Mode_Out_OD;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;

    gpio.GPIO_Pin = i2c->scl_pin;
    GPIO_Init(i2c->scl_port, &gpio);
    gpio.GPIO_Pin = i2c->sda_pin;
    GPIO_Init(i2c->sda_port, &gpio);

    GPIO_SetBits(i2c->scl_port, i2c->scl_pin);
    GPIO_SetBits(i2c->sda_port, i2c->sda_pin);
    i2c_recovery_delay();

    for (i = 0U; i < I2C_RECOVERY_PULSES; ++i) {
        GPIO_ResetBits(i2c->scl_port, i2c->scl_pin);
        i2c_recovery_delay();
        GPIO_SetBits(i2c->scl_port, i2c->scl_pin);
        i2c_recovery_delay();
    }

    GPIO_ResetBits(i2c->sda_port, i2c->sda_pin);
    i2c_recovery_delay();
    GPIO_SetBits(i2c->scl_port, i2c->scl_pin);
    i2c_recovery_delay();
    GPIO_SetBits(i2c->sda_port, i2c->sda_pin);
    i2c_recovery_delay();

    i2c->initialized = 0U;
    return mcu_port_i2c_init(i2c);
}

static uint8_t i2c_prepare_bus(mcu_port_i2c_t *i2c)
{
    if (i2c == 0) {
        return 0U;
    }

    if (!i2c->initialized && !mcu_port_i2c_init(i2c)) {
        return 0U;
    }

    I2C_ClearFlag(i2c->i2c, I2C_FLAG_AF | I2C_FLAG_BERR | I2C_FLAG_ARLO | I2C_FLAG_OVR);
    if (wait_flag_clear(i2c->i2c, I2C_FLAG_BUSY)) {
        return 1U;
    }

    if (!mcu_port_i2c_recover_bus(i2c)) {
        return 0U;
    }

    I2C_ClearFlag(i2c->i2c, I2C_FLAG_AF | I2C_FLAG_BERR | I2C_FLAG_ARLO | I2C_FLAG_OVR);
    return wait_flag_clear(i2c->i2c, I2C_FLAG_BUSY);
}

uint8_t mcu_port_i2c_is_ready(mcu_port_i2c_t *i2c, uint8_t address)
{
    if (!i2c_prepare_bus(i2c)) {
        return 0U;
    }

    I2C_GenerateSTART(i2c->i2c, ENABLE);
    if (!wait_event(i2c->i2c, I2C_EVENT_MASTER_MODE_SELECT)) {
        i2c_stop_and_clear_error(i2c->i2c);
        return 0U;
    }

    I2C_Send7bitAddress(i2c->i2c, (uint8_t)(address << 1), I2C_Direction_Transmitter);
    if (wait_addr_ack_or_nack(i2c->i2c)) {
        I2C_GenerateSTOP(i2c->i2c, ENABLE);
        return 1U;
    }

    i2c_stop_and_clear_error(i2c->i2c);
    return 0U;
}

uint8_t mcu_port_i2c_write(mcu_port_i2c_t *i2c, uint8_t address, const uint8_t *data, uint16_t length)
{
    uint16_t i;

    if (i2c == 0 || data == 0 || length == 0U) {
        return 0U;
    }

    if (!i2c_prepare_bus(i2c)) {
        return 0U;
    }

    I2C_GenerateSTART(i2c->i2c, ENABLE);
    if (!wait_event(i2c->i2c, I2C_EVENT_MASTER_MODE_SELECT)) {
        i2c_stop_and_clear_error(i2c->i2c);
        return 0U;
    }

    I2C_Send7bitAddress(i2c->i2c, (uint8_t)(address << 1), I2C_Direction_Transmitter);
    if (!wait_addr_ack_or_nack(i2c->i2c)) {
        i2c_stop_and_clear_error(i2c->i2c);
        return 0U;
    }

    for (i = 0U; i < length; ++i) {
        I2C_SendData(i2c->i2c, data[i]);
        if (!wait_event(i2c->i2c, I2C_EVENT_MASTER_BYTE_TRANSMITTED)) {
            i2c_stop_and_clear_error(i2c->i2c);
            return 0U;
        }
    }

    I2C_GenerateSTOP(i2c->i2c, ENABLE);
    return 1U;
}

uint8_t mcu_port_i2c_write_reg(mcu_port_i2c_t *i2c, uint8_t address, uint8_t reg, uint8_t value)
{
    if (i2c == 0) {
        return 0U;
    }

    if (!i2c_prepare_bus(i2c)) {
        return 0U;
    }

    I2C_GenerateSTART(i2c->i2c, ENABLE);
    if (!wait_event(i2c->i2c, I2C_EVENT_MASTER_MODE_SELECT)) {
        i2c_stop_and_clear_error(i2c->i2c);
        return 0U;
    }

    I2C_Send7bitAddress(i2c->i2c, (uint8_t)(address << 1), I2C_Direction_Transmitter);
    if (!wait_addr_ack_or_nack(i2c->i2c)) {
        i2c_stop_and_clear_error(i2c->i2c);
        return 0U;
    }

    I2C_SendData(i2c->i2c, reg);
    if (!wait_event(i2c->i2c, I2C_EVENT_MASTER_BYTE_TRANSMITTED)) {
        i2c_stop_and_clear_error(i2c->i2c);
        return 0U;
    }

    I2C_SendData(i2c->i2c, value);
    if (!wait_event(i2c->i2c, I2C_EVENT_MASTER_BYTE_TRANSMITTED)) {
        i2c_stop_and_clear_error(i2c->i2c);
        return 0U;
    }

    I2C_GenerateSTOP(i2c->i2c, ENABLE);
    return 1U;
}

uint8_t mcu_port_i2c_read_regs(mcu_port_i2c_t *i2c, uint8_t address, uint8_t reg, uint8_t *data, uint16_t length)
{
    uint16_t i;

    if (i2c == 0 || data == 0 || length == 0U) {
        return 0U;
    }

    if (!i2c_prepare_bus(i2c)) {
        return 0U;
    }

    I2C_AcknowledgeConfig(i2c->i2c, ENABLE);
    I2C_GenerateSTART(i2c->i2c, ENABLE);
    if (!wait_event(i2c->i2c, I2C_EVENT_MASTER_MODE_SELECT)) {
        i2c_stop_and_clear_error(i2c->i2c);
        return 0U;
    }

    I2C_Send7bitAddress(i2c->i2c, (uint8_t)(address << 1), I2C_Direction_Transmitter);
    if (!wait_addr_ack_or_nack(i2c->i2c)) {
        i2c_stop_and_clear_error(i2c->i2c);
        return 0U;
    }

    I2C_SendData(i2c->i2c, reg);
    if (!wait_event(i2c->i2c, I2C_EVENT_MASTER_BYTE_TRANSMITTED)) {
        i2c_stop_and_clear_error(i2c->i2c);
        return 0U;
    }

    I2C_GenerateSTART(i2c->i2c, ENABLE);
    if (!wait_event(i2c->i2c, I2C_EVENT_MASTER_MODE_SELECT)) {
        i2c_stop_and_clear_error(i2c->i2c);
        return 0U;
    }

    I2C_Send7bitAddress(i2c->i2c, (uint8_t)(address << 1), I2C_Direction_Receiver);
    if (!wait_addr_ack_or_nack(i2c->i2c)) {
        i2c_stop_and_clear_error(i2c->i2c);
        I2C_AcknowledgeConfig(i2c->i2c, ENABLE);
        return 0U;
    }

    if (length == 1U) {
        I2C_AcknowledgeConfig(i2c->i2c, DISABLE);
        I2C_GenerateSTOP(i2c->i2c, ENABLE);

        if (!wait_flag_set(i2c->i2c, I2C_FLAG_RXNE)) {
            I2C_AcknowledgeConfig(i2c->i2c, ENABLE);
            i2c_stop_and_clear_error(i2c->i2c);
            return 0U;
        }

        data[0] = I2C_ReceiveData(i2c->i2c);
        I2C_AcknowledgeConfig(i2c->i2c, ENABLE);
        return 1U;
    }

    for (i = 0U; i < length; ++i) {
        if (i == (uint16_t)(length - 1U)) {
            I2C_AcknowledgeConfig(i2c->i2c, DISABLE);
            I2C_GenerateSTOP(i2c->i2c, ENABLE);
        }

        if (!wait_flag_set(i2c->i2c, I2C_FLAG_RXNE)) {
            I2C_AcknowledgeConfig(i2c->i2c, ENABLE);
            i2c_stop_and_clear_error(i2c->i2c);
            return 0U;
        }

        data[i] = I2C_ReceiveData(i2c->i2c);
    }

    I2C_AcknowledgeConfig(i2c->i2c, ENABLE);
    return 1U;
}

uint8_t mcu_port_i2c_write_read(mcu_port_i2c_t *i2c, uint8_t address,
                                const uint8_t *write_data, uint16_t write_len,
                                uint8_t *read_data, uint16_t read_len)
{
    uint16_t i;

    if (i2c == 0 || write_data == 0 || write_len == 0U ||
        read_data == 0 || read_len == 0U || !i2c_prepare_bus(i2c)) {
        return 0U;
    }

    I2C_AcknowledgeConfig(i2c->i2c, ENABLE);
    I2C_GenerateSTART(i2c->i2c, ENABLE);
    if (!wait_event(i2c->i2c, I2C_EVENT_MASTER_MODE_SELECT)) goto fail;
    I2C_Send7bitAddress(i2c->i2c, (uint8_t)(address << 1), I2C_Direction_Transmitter);
    if (!wait_addr_ack_or_nack(i2c->i2c)) goto fail;
    for (i = 0U; i < write_len; ++i) {
        I2C_SendData(i2c->i2c, write_data[i]);
        if (!wait_event(i2c->i2c, I2C_EVENT_MASTER_BYTE_TRANSMITTED)) goto fail;
    }
    I2C_GenerateSTART(i2c->i2c, ENABLE);
    if (!wait_event(i2c->i2c, I2C_EVENT_MASTER_MODE_SELECT)) goto fail;
    I2C_Send7bitAddress(i2c->i2c, (uint8_t)(address << 1), I2C_Direction_Receiver);
    if (!wait_addr_ack_or_nack(i2c->i2c)) goto fail;

    if (read_len == 1U) {
        I2C_AcknowledgeConfig(i2c->i2c, DISABLE);
        I2C_GenerateSTOP(i2c->i2c, ENABLE);
        if (!wait_flag_set(i2c->i2c, I2C_FLAG_RXNE)) goto fail;
        read_data[0] = I2C_ReceiveData(i2c->i2c);
    } else {
        for (i = 0U; i < read_len; ++i) {
            if (i == (uint16_t)(read_len - 1U)) {
                I2C_AcknowledgeConfig(i2c->i2c, DISABLE);
                I2C_GenerateSTOP(i2c->i2c, ENABLE);
            }
            if (!wait_flag_set(i2c->i2c, I2C_FLAG_RXNE)) goto fail;
            read_data[i] = I2C_ReceiveData(i2c->i2c);
        }
    }
    I2C_AcknowledgeConfig(i2c->i2c, ENABLE);
    return 1U;

fail:
    I2C_AcknowledgeConfig(i2c->i2c, ENABLE);
    i2c_stop_and_clear_error(i2c->i2c);
    return 0U;
}

static void gpio_i2c_delay(void)
{
    volatile uint32_t index;
    for (index = 0U; index < 300U; ++index) {
        __asm volatile ("nop");
    }
}

uint8_t mcu_port_i2c_gpio_probe(mcu_port_i2c_t *i2c, uint8_t address,
                                uint8_t swap_scl_sda)
{
    GPIO_InitTypeDef gpio = {0};
    uint16_t scl_pin;
    uint16_t sda_pin;
    uint8_t value;
    uint8_t ack;

    if (i2c == 0 || address > 0x7FU) return 0U;
    scl_pin = swap_scl_sda ? i2c->sda_pin : i2c->scl_pin;
    sda_pin = swap_scl_sda ? i2c->scl_pin : i2c->sda_pin;
    I2C_Cmd(i2c->i2c, DISABLE);
    I2C_DeInit(i2c->i2c);
    gpio.GPIO_Mode = GPIO_Mode_Out_OD;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    gpio.GPIO_Pin = scl_pin; GPIO_Init(i2c->scl_port, &gpio);
    gpio.GPIO_Pin = sda_pin; GPIO_Init(i2c->sda_port, &gpio);
    GPIO_SetBits(i2c->scl_port, scl_pin);
    GPIO_SetBits(i2c->sda_port, sda_pin);
    gpio_i2c_delay();
    GPIO_ResetBits(i2c->sda_port, sda_pin);
    gpio_i2c_delay();
    GPIO_ResetBits(i2c->scl_port, scl_pin);
    value = (uint8_t)(address << 1U);
    for (uint8_t bit = 0U; bit < 8U; ++bit) {
        if ((value & 0x80U) != 0U) GPIO_SetBits(i2c->sda_port, sda_pin);
        else GPIO_ResetBits(i2c->sda_port, sda_pin);
        gpio_i2c_delay();
        GPIO_SetBits(i2c->scl_port, scl_pin); gpio_i2c_delay();
        GPIO_ResetBits(i2c->scl_port, scl_pin); value <<= 1U;
    }
    GPIO_SetBits(i2c->sda_port, sda_pin); gpio_i2c_delay();
    GPIO_SetBits(i2c->scl_port, scl_pin); gpio_i2c_delay();
    ack = GPIO_ReadInputDataBit(i2c->sda_port, sda_pin) == Bit_RESET;
    GPIO_ResetBits(i2c->scl_port, scl_pin);
    GPIO_ResetBits(i2c->sda_port, sda_pin); gpio_i2c_delay();
    GPIO_SetBits(i2c->scl_port, scl_pin); gpio_i2c_delay();
    GPIO_SetBits(i2c->sda_port, sda_pin);
    i2c->initialized = 0U;
    (void)mcu_port_i2c_init(i2c);
    return ack;
}

uint8_t mcu_port_i2c_read_reg(mcu_port_i2c_t *i2c, uint8_t address, uint8_t reg, uint8_t *value)
{
    if (value == 0) {
        return 0U;
    }

    return mcu_port_i2c_read_regs(i2c, address, reg, value, 1U);
}
