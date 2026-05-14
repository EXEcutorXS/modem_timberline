/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __CAN_H
#define __CAN_H

/* Includes ------------------------------------------------------------------*/
#include "n32wb452.h"

#define MY_CAN_ADDR         6
#define CAN_BROADCAST_ADDR  7
#define MY_CAN_TYPE         126
#define CAN_BROADCAST_TYPE  127

/* Classes ------------------------------------------------------------------*/
class Can
{
    public:
        Can(void);
        void initialize(void);
        void handler(void);
        void SendMessage(uint32_t AID, uint8_t AD0, uint8_t AD1, uint8_t AD2, uint8_t AD3,
                         uint8_t AD4, uint8_t AD5, uint8_t AD6, uint8_t AD7);
        bool checkReceiveMsgAddr(CanRxMsg *msg);
        void processCanRxMessage(CanRxMsg *msg);

        uint16_t linkCnt;
        CanRxMsg RxMessage;

    private:
        CanTxMsg TxMessage;
};
extern Can can;

#endif /* __CAN_H */
