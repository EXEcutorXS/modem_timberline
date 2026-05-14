#include "can.h"

Can can;

Can::Can(void) : linkCnt(0)
{
}

void Can::initialize(void)
{
    GPIO_InitType GPIO_InitStructure;
    GPIO_InitStruct(&GPIO_InitStructure);

    RCC_EnableAPB2PeriphClk(RCC_APB2_PERIPH_AFIO | RCC_APB2_PERIPH_GPIOD, ENABLE);

    GPIO_InitStructure.Pin       = GPIO_PIN_8;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPU;
    GPIO_InitPeripheral(GPIOD, &GPIO_InitStructure);

    GPIO_InitStructure.Pin        = GPIO_PIN_9;
    GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_AF_PP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_InitPeripheral(GPIOD, &GPIO_InitStructure);

    GPIO_ConfigPinRemap(GPIO_RMP1_CAN1, ENABLE);

    CAN_FilterInitType CAN_FilterInitStructure;
    CAN_FilterInitStructure.Filter_Num            = CAN_FILTERNUM0;
    CAN_FilterInitStructure.Filter_Mode           = CAN_Filter_IdMaskMode;
    CAN_FilterInitStructure.Filter_Scale          = CAN_Filter_32bitScale;
    CAN_FilterInitStructure.Filter_HighId         = CAN_FILTER_STDID(0x400);
    CAN_FilterInitStructure.Filter_LowId          = CAN_FILTER_STDID(0x400);
    CAN_FilterInitStructure.FilterMask_HighId     = CAN_STD_ID_H_MASK_DONT_CARE;
    CAN_FilterInitStructure.FilterMask_LowId      = CAN_STD_ID_L_MASK_DONT_CARE;
    CAN_FilterInitStructure.Filter_FIFOAssignment = CAN_FIFO0;
    CAN_FilterInitStructure.Filter_Act            = ENABLE;
    CAN1_InitFilter(&CAN_FilterInitStructure);
    CAN_INTConfig(CAN1, CAN_INT_FMP0, ENABLE);

    NVIC_InitType NVIC_InitStructure;
    NVIC_PriorityGroupConfig(NVIC_PriorityGroup_0);
    NVIC_InitStructure.NVIC_IRQChannel                   = USB_LP_CAN1_RX0_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 0x0;
    NVIC_InitStructure.NVIC_IRQChannelSubPriority        = 0x0;
    NVIC_InitStructure.NVIC_IRQChannelCmd                = ENABLE;
    NVIC_Init(&NVIC_InitStructure);

    CAN_InitType CAN_InitStructure;
    RCC_EnableAPB1PeriphClk(RCC_APB1_PERIPH_CAN1, ENABLE);
    CAN_DeInit(CAN1);
    CAN_InitStruct(&CAN_InitStructure);
    CAN_InitStructure.TTCM              = DISABLE;
    CAN_InitStructure.ABOM              = DISABLE;
    CAN_InitStructure.AWKUM             = DISABLE;
    CAN_InitStructure.NART              = DISABLE;
    CAN_InitStructure.RFLM              = DISABLE;
    CAN_InitStructure.TXFP              = ENABLE;
    CAN_InitStructure.OperatingMode     = OPERATINGMODE;
    CAN_InitStructure.RSJW              = CAN_BIT_RSJW;
    CAN_InitStructure.TBS1              = CAN_BIT_BS1;
    CAN_InitStructure.TBS2              = CAN_BIT_BS2;
    CAN_InitStructure.BaudRatePrescaler = CAN_BAUDRATEPRESCALER;
    CAN_Init(CAN1, &CAN_InitStructure);
    CAN_Filter_Init();
    CAN_NVIC_Config();
}

void Can::handler(void)
{
}

void Can::SendMessage(uint32_t AID, uint8_t AD0, uint8_t AD1, uint8_t AD2, uint8_t AD3,
                      uint8_t AD4, uint8_t AD5, uint8_t AD6, uint8_t AD7)
{
    TxMessage.StdId  = 0;
    TxMessage.ExtId  = AID;
    TxMessage.RTR    = CAN_RTR_DATA;
    TxMessage.IDE    = CAN_ID_EXT;
    TxMessage.DLC    = 8;
    TxMessage.Data[0] = AD0; TxMessage.Data[1] = AD1;
    TxMessage.Data[2] = AD2; TxMessage.Data[3] = AD3;
    TxMessage.Data[4] = AD4; TxMessage.Data[5] = AD5;
    TxMessage.Data[6] = AD6; TxMessage.Data[7] = AD7;

    uint8_t mailbox = CAN_Transmit(CAN1, &TxMessage);
    uint32_t i = 0;
    uint8_t status = 0;
    while (status != CANTXOK) {
        status = CAN_TransmitStatus(CAN1, mailbox);
        if (++i == 0xFFFF) { initialize(); break; }
    }
}

bool Can::checkReceiveMsgAddr(CanRxMsg *msg)
{
    uint8_t type = (msg->ExtId >> 13) & 0x7F;
    return (type == MY_CAN_TYPE || type == CAN_BROADCAST_TYPE);
}

void Can::processCanRxMessage(CanRxMsg *msg)
{
    // TODO: handle incoming CAN message from device
    (void)msg;
}

extern "C" void USB_LP_CAN1_RX0_IRQHandler(void)
{
    can.linkCnt = 0;

    if (CAN_GetITStatus(CAN1, CAN_INT_FMP0) == SET) {
        CAN_Receive(CAN1, CAN_FIFO0, &can.RxMessage);
        if (can.checkReceiveMsgAddr(&can.RxMessage)) {
            can.processCanRxMessage(&can.RxMessage);
        }
        CAN_ClearITPendingBit(CAN1, CAN_INT_FMP0);
    }
}
