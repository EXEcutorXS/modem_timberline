/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __CAN_H
#define __CAN_H

/* Includes ------------------------------------------------------------------*/
#include "n32wb452.h"
#include "n32wb452_can.h"

/* Classes ------------------------------------------------------------------*/
class Can
{
    public:
        Can(void);
        void initialize(void);
        void handler(void);
        void SendMessage(uint32_t AID, uint8_t AD0, uint8_t AD1, uint8_t AD2, uint8_t AD3,
                         uint8_t AD4, uint8_t AD5, uint8_t AD6, uint8_t AD7);
        void sendRaw(bool ext, uint32_t id, uint8_t dlc, const uint8_t* data);
        void processCanRxMessage(CanRxMessage *msg);

        uint16_t linkCnt;
        CanRxMessage RxMessage;
		uint8_t idType;
		uint8_t idAddress;

    private:
        CanTxMessage TxMessage;
};
extern Can can;

#endif /* __CAN_H */
