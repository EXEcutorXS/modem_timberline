#ifndef __USART_H
#define __USART_H

#include "n32wb452.h"

class Usart_C
{
    public:
        static const uint16_t BUFFER_SIZE = 1024;

        uint32_t baudrate;
        bool isTransmission;

        Usart_C(void);
        void initialize(uint8_t usartNum, uint32_t baud);
        void changeBaudrate(uint32_t baud);
        void send(uint8_t* buf, uint32_t len);
        uint16_t getBufferPos(void);
        uint8_t getByte(uint16_t pos);
        void receiveIntHandler(uint8_t byte);
        void transmitNextByte(void);

    private:
        uint8_t  _usartNum;
        uint8_t  _rxBuf[BUFFER_SIZE];
        uint16_t _rxHead;
        uint16_t _rxTail;
        uint8_t  _txBuf[512];
        uint16_t _txLen;
        uint16_t _txPos;
        USART_Module* _usart;
};

#endif /* __USART_H */
