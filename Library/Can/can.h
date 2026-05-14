/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __CAN_H
#define __CAN_H

/* Includes ------------------------------------------------------------------*/
#include "n32wb452.h"

typedef enum canMessageType_t{
    UNKNOWN_MSG,
    HEATER_MSG,
    PANEL_MSG
} canMessageType_t;

#define MY_CAN_ADDR         6 // 0 - 6 - обычные адреса, 7 - широковещательный
#define CAN_BROADCAST_ADDR  7

#define MY_CAN_TYPE         126 // 126 -  устройство управлени€
#define CAN_BROADCAST_TYPE  127 // 127 -  вс€кое устройство

/* Classes ------------------------------------------------------------------*/
class Can
{
    public:
        int ID;                                        //»дентификатор ѕакета

        Can(void);
        void handler(void);                            //обработка полученного параметра
        void initialize(void);
        void SendMessage(uint32_t AID, uint8_t AD0, uint8_t AD1, uint8_t AD2, uint8_t AD3, uint8_t AD4, uint8_t AD5, uint8_t AD6, uint8_t AD7);
        canMessageType_t CheckReceiveMsgAddr(uint32_t *CanIdentifier);
        void sendCanRxMsgToHeaterParser(uint8_t *RxMessage);
        void sendCanRxMsgToPanelParser(uint8_t *RxMessage);
    
        uint16_t linkCnt;
    
    private:
    
};
extern Can can;

#endif /* __CAN_H */
