#include "can.h"
#include "log.h"
#include "Timberline.h"
#include "SlcanBridge.h"

Can can;

Can::Can(void) : linkCnt(0)
{
}

void Can::initialize(void)
{
    GPIO_InitType GPIO_InitStructure;
    GPIO_InitStruct(&GPIO_InitStructure);

    RCC_EnableAPB2PeriphClk(RCC_APB2_PERIPH_AFIO | RCC_APB2_PERIPH_GPIOB, ENABLE);

    /* PB5 — CAN2 RX, input pull-up */
    GPIO_InitStructure.Pin       = GPIO_PIN_5;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPU;
    GPIO_InitPeripheral(GPIOB, &GPIO_InitStructure);

    /* PB6 — CAN2 TX, alternate function push-pull */
    GPIO_InitStructure.Pin        = GPIO_PIN_6;
    GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_AF_PP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_InitPeripheral(GPIOB, &GPIO_InitStructure);

    GPIO_ConfigPinRemap(GPIO_RMP1_CAN2, ENABLE);

    /* PA5 — CAN_STB, low = transceiver active */
    RCC_EnableAPB2PeriphClk(RCC_APB2_PERIPH_GPIOA, ENABLE);
    GPIO_InitStructure.Pin        = GPIO_PIN_5;
    GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_Out_PP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_2MHz;
    GPIO_InitPeripheral(GPIOA, &GPIO_InitStructure);
    GPIO_ResetBits(GPIOA, GPIO_PIN_5);

    RCC_EnableAPB1PeriphClk(RCC_APB1_PERIPH_CAN2, ENABLE);
    CAN_DeInit(CAN2);

    CAN_InitType CAN_InitStructure;
    CAN_InitStruct(&CAN_InitStructure);
    CAN_InitStructure.TTCM              = DISABLE;
    CAN_InitStructure.ABOM              = ENABLE;  /* auto Bus-Off recovery */
    CAN_InitStructure.AWKUM             = DISABLE;
    CAN_InitStructure.NART              = ENABLE;  /* no auto-retransmit — prevents TEC storm */
    CAN_InitStructure.RFLM              = DISABLE;
    CAN_InitStructure.TXFP              = ENABLE;
    CAN_InitStructure.OperatingMode     = CAN_Normal_Mode;
    CAN_InitStructure.RSJW              = CAN_RSJW_1tq;
    CAN_InitStructure.TBS1              = CAN_TBS1_11tq;
    CAN_InitStructure.TBS2              = CAN_TBS2_4tq;
    CAN_InitStructure.BaudRatePrescaler = 6; /* 250 kbps @ APB1=24MHz: 24M/(6*16)=250k */
    CAN_Init(CAN2, &CAN_InitStructure);

    CAN_FilterInitType CAN_FilterInitStructure;
    CAN_FilterInitStructure.Filter_Num            = 0;
    CAN_FilterInitStructure.Filter_Mode           = CAN_Filter_IdMaskMode;
    CAN_FilterInitStructure.Filter_Scale          = CAN_Filter_32bitScale;
    CAN_FilterInitStructure.Filter_HighId         = 0x0000;
    CAN_FilterInitStructure.Filter_LowId          = 0x0000;
    CAN_FilterInitStructure.FilterMask_HighId     = 0x0000;
    CAN_FilterInitStructure.FilterMask_LowId      = 0x0000;
    CAN_FilterInitStructure.Filter_FIFOAssignment = CAN_Filter_FIFO0;
    CAN_FilterInitStructure.Filter_Act            = ENABLE;
    CAN2_InitFilter(&CAN_FilterInitStructure);

    CAN_INTConfig(CAN2, CAN_INT_FMP0, ENABLE);

    NVIC_InitType NVIC_InitStructure;
    NVIC_PriorityGroupConfig(NVIC_PriorityGroup_0);
    NVIC_InitStructure.NVIC_IRQChannel                   = CAN2_RX0_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 0x0;
    NVIC_InitStructure.NVIC_IRQChannelSubPriority        = 0x0;
    NVIC_InitStructure.NVIC_IRQChannelCmd                = ENABLE;
    NVIC_Init(&NVIC_InitStructure);

	idType=VERSION_1;
	idAddress=0;
}

void Can::handler(void)
{
}

void Can::SendMessage(uint32_t AID, uint8_t AD0, uint8_t AD1, uint8_t AD2, uint8_t AD3,
                      uint8_t AD4, uint8_t AD5, uint8_t AD6, uint8_t AD7)
{
    /* In SLCAN bridge mode the modem is a transparent adapter — suppress its
       own firmware traffic so it doesn't interfere with the host's CAN session. */
    if (slcanBridge.active) return;

    TxMessage.StdId   = 0;
    TxMessage.ExtId   = AID;
    TxMessage.RTR     = CAN_RTRQ_Data;
    TxMessage.IDE     = CAN_Extended_Id;
    TxMessage.DLC     = 8;
    TxMessage.Data[0] = AD0; TxMessage.Data[1] = AD1;
    TxMessage.Data[2] = AD2; TxMessage.Data[3] = AD3;
    TxMessage.Data[4] = AD4; TxMessage.Data[5] = AD5;
    TxMessage.Data[6] = AD6; TxMessage.Data[7] = AD7;

    CAN_TransmitMessage(CAN2, &TxMessage);
}


void Can::sendRaw(bool ext, uint32_t id, uint8_t dlc, const uint8_t* data)
{
    TxMessage.StdId = ext ? 0 : (uint16_t)id;
    TxMessage.ExtId = ext ? id : 0;
    TxMessage.RTR   = CAN_RTRQ_Data;
    TxMessage.IDE   = ext ? CAN_Extended_Id : CAN_Standard_Id;
    TxMessage.DLC   = dlc;
    for (uint8_t i = 0; i < 8; i++)
        TxMessage.Data[i] = i < dlc ? data[i] : 0;
    CAN_TransmitMessage(CAN2, &TxMessage);

    slcanBridge.onCanTx(ext, id, dlc, data);
}

void Can::processCanRxMessage(CanRxMessage *msg)
{
    slcanBridge.onCanRx(msg);

    /* In SLCAN bridge mode skip timberline processing — the modem acts as a
       transparent adapter and must not respond to frames on behalf of the bus. */
    if (slcanBridge.active) return;

    timberline.ProcessCanMessage(msg);
}

extern "C" void CAN2_RX0_IRQHandler(void)
{
    if (CAN_GetIntStatus(CAN2, CAN_INT_FMP0) == SET) {
        can.linkCnt = 0;
        CAN_ReceiveMessage(CAN2, CAN_FIFO0, &can.RxMessage);
        can.processCanRxMessage(&can.RxMessage);
        CAN_ClearINTPendingBit(CAN2, CAN_INT_FMP0);
    }
}
