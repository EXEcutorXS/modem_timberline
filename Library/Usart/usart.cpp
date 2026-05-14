#include "usart.h"
#include <string.h>

Usart_C::Usart_C(void)
    : baudrate(0), isTransmission(false),
      _usartNum(0), _rxHead(0), _rxTail(0),
      _txLen(0), _txPos(0), _usart(nullptr)
{
}

void Usart_C::initialize(uint8_t usartNum, uint32_t baud)
{
    _usartNum = usartNum;
    baudrate = baud;
    _rxHead = _rxTail = 0;
    _txLen = _txPos = 0;
    isTransmission = false;

    USART_InitType cfg;
    cfg.BaudRate            = baud;
    cfg.WordLength          = USART_WL_8B;
    cfg.StopBits            = USART_STPB_1;
    cfg.Parity              = USART_PE_NO;
    cfg.Mode                = USART_MODE_RX | USART_MODE_TX;
    cfg.HardwareFlowControl = USART_HFCTRL_NONE;

    if (usartNum == 1) {
        _usart = USART1;
        RCC_EnableAPB2PeriphClk(RCC_APB2_PERIPH_USART1, ENABLE);
        RCC_EnableAPB2PeriphClk(RCC_APB2_PERIPH_GPIOA, ENABLE);
        GPIO_InitType gpio;
        GPIO_InitStruct(&gpio);
        gpio.Pin = GPIO_PIN_9; gpio.GPIO_Mode = GPIO_Mode_AF_PP; gpio.GPIO_Speed = GPIO_Speed_50MHz;
        GPIO_InitPeripheral(GPIOA, &gpio);
        gpio.Pin = GPIO_PIN_10; gpio.GPIO_Mode = GPIO_Mode_IN_FLOATING;
        GPIO_InitPeripheral(GPIOA, &gpio);
        NVIC_InitType nvic = {USART1_IRQn, 1, 1, ENABLE};
        NVIC_Init(&nvic);
    } else if (usartNum == 3) {
        _usart = USART3;
        RCC_EnableAPB1PeriphClk(RCC_APB1_PERIPH_USART3, ENABLE);
        RCC_EnableAPB2PeriphClk(RCC_APB2_PERIPH_GPIOB, ENABLE);
        GPIO_InitType gpio;
        GPIO_InitStruct(&gpio);
        gpio.Pin = GPIO_PIN_10; gpio.GPIO_Mode = GPIO_Mode_AF_PP; gpio.GPIO_Speed = GPIO_Speed_50MHz;
        GPIO_InitPeripheral(GPIOB, &gpio);
        gpio.Pin = GPIO_PIN_11; gpio.GPIO_Mode = GPIO_Mode_IN_FLOATING;
        GPIO_InitPeripheral(GPIOB, &gpio);
        NVIC_InitType nvic = {USART3_IRQn, 1, 1, ENABLE};
        NVIC_Init(&nvic);
    }

    if (_usart) {
        USART_Init(_usart, &cfg);
        USART_ConfigInt(_usart, USART_INT_RXDNE, ENABLE);
        USART_Enable(_usart, ENABLE);
    }
}

void Usart_C::changeBaudrate(uint32_t baud)
{
    baudrate = baud;
    if (_usart) {
        USART_Enable(_usart, DISABLE);
        USART_InitType cfg;
        cfg.BaudRate            = baud;
        cfg.WordLength          = USART_WL_8B;
        cfg.StopBits            = USART_STPB_1;
        cfg.Parity              = USART_PE_NO;
        cfg.Mode                = USART_MODE_RX | USART_MODE_TX;
        cfg.HardwareFlowControl = USART_HFCTRL_NONE;
        USART_Init(_usart, &cfg);
        USART_Enable(_usart, ENABLE);
    }
}

void Usart_C::send(uint8_t* buf, uint32_t len)
{
    if (!_usart || len == 0 || len > sizeof(_txBuf)) return;
    memcpy(_txBuf, buf, len);
    _txLen = (uint16_t)len;
    _txPos = 0;
    isTransmission = true;
    USART_ConfigInt(_usart, USART_INT_TXDE, ENABLE);
}

uint16_t Usart_C::getBufferPos(void)
{
    return _rxHead;
}

uint8_t Usart_C::getByte(uint16_t pos)
{
    return _rxBuf[pos % BUFFER_SIZE];
}

void Usart_C::receiveIntHandler(uint8_t byte)
{
    _rxBuf[_rxHead] = byte;
    _rxHead = (_rxHead + 1) % BUFFER_SIZE;
}

void Usart_C::transmitNextByte(void)
{
    if (!_usart) return;
    if (_txPos < _txLen) {
        USART_SendData(_usart, _txBuf[_txPos++]);
    } else {
        isTransmission = false;
        USART_ConfigInt(_usart, USART_INT_TXDE, DISABLE);
    }
}
