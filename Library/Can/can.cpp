/******************************************************************************
* ООО Теплостар
* Самара
* 
* Программисты: Кубышкин К.А.
* 
* 04.10.2018
* Описание:
*******************************************************************************/
/* Includes ------------------------------------------------------------------*/
#include "can.h"
#include "heater.h"

#ifdef USE_CAN

Can can;

Can::Can(void){
//    this->idType=126;    //тип устройства
//    this->idAddress=1;   //адрес устройства   
}


//||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||
void Can::initialize(void)
{
    GPIO_InitType GPIO_InitStructure;
    GPIO_InitStruct(&GPIO_InitStructure);
    /* Configures CAN1 IOs */
    RCC_EnableAPB2PeriphClk(RCC_APB2_PERIPH_AFIO | RCC_APB2_PERIPH_GPIOD, ENABLE);
    /* Configure CAN1 RX pin */
    GPIO_InitStructure.Pin       = GPIO_PIN_8;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPU;
    GPIO_InitPeripheral(GPIOD, &GPIO_InitStructure);
    /* Configure CAN1 TX pin */
    GPIO_InitStructure.Pin        = GPIO_PIN_9;
    GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_AF_PP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_InitPeripheral(GPIOD, &GPIO_InitStructure);
    /* Remap CAN1 GPIOs */
    GPIO_ConfigPinRemap(GPIO_RMP1_CAN1, ENABLE);

    CAN_FilterInitType CAN_FilterInitStructure;
    /* CAN filter init */
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
    /* Configure the NVIC Preemption Priority Bits */
    NVIC_PriorityGroupConfig(NVIC_PriorityGroup_0);
    NVIC_InitStructure.NVIC_IRQChannel                   = USB_LP_CAN1_RX0_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 0x0;
    NVIC_InitStructure.NVIC_IRQChannelSubPriority        = 0x0;
    NVIC_InitStructure.NVIC_IRQChannelCmd                = ENABLE;
    NVIC_Init(&NVIC_InitStructure);
    
    CAN_InitType CAN_InitStructure;
    /* Configure CAN */
    RCC_EnableAPB1PeriphClk(RCC_APB1_PERIPH_CAN1, ENABLE);
    /* CAN register init */
    CAN_DeInit(CAN1);
    /* Struct init*/
    CAN_InitStruct(&CAN_InitStructure);
    /* CAN cell init */
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
    /*Initializes the CAN */
    CAN_Init(CAN1, &CAN_InitStructure);
    CAN_Filter_Init();
    CAN_NVIC_Config();



    GPIO_InitTypeDef  GPIO_InitStructure;
    CAN_TypeDef CAN_OperatingMode;
    CAN_InitTypeDef CAN_InitStructure;
    
    // включаем тактирование порта
    RCC_AHBPeriphClockCmd(RCC_AHBPeriph_GPIOB, ENABLE);
    // включаем тактирование CAN
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_CAN, ENABLE);
    // включаем тактирование порта
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_SYSCFG, ENABLE);
    
    // настройка входа/выхода CAN
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_8 | GPIO_Pin_9;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF;
    GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_NOPULL;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOB, &GPIO_InitStructure);
    GPIO_PinAFConfig(GPIOB, GPIO_PinSource8, GPIO_AF_4);
    GPIO_PinAFConfig(GPIOB, GPIO_PinSource9, GPIO_AF_4);

    //Configure CAN
    CAN->MCR |= CAN_MCR_INRQ;
    while((CAN->MSR & CAN_MSR_INAK)!=CAN_MSR_INAK) {}
    CAN->MCR &=~CAN_MCR_SLEEP ;
    CAN_StructInit(&CAN_InitStructure);
    CAN_InitStructure.CAN_Mode=CAN_Mode_Normal;
    //CAN_InitStructure.CAN_Mode=CAN_Mode_LoopBack; // замыкание на самого себя до выхода из пина (отладка)
    CAN_InitStructure.CAN_Prescaler=8;
    CAN_InitStructure.CAN_SJW = CAN_SJW_1tq;
    CAN_InitStructure.CAN_BS1 = CAN_BS1_15tq;  
    CAN_InitStructure.CAN_BS2 = CAN_BS2_8tq;
    CAN_InitStructure.CAN_TXFP = ENABLE;       //FIFO для передачи сообщений
    CAN_InitStructure.CAN_NART = DISABLE;  
        
    //CAN_InitStructure.CAN_ABOM=ENABLE; // проверить!
        
    CAN_Init(CAN, &CAN_InitStructure);
    CAN->MCR &=~ CAN_MCR_INRQ;
    while((CAN->MSR & CAN_MSR_INAK)==CAN_MSR_INAK){}

    CAN->sFilterRegister[0].FR1 = 0; /* (11) */

    CAN->FMR = CAN_FMR_FINIT;
    CAN->FA1R = CAN_FA1R_FACT0;
    CAN->FMR &=~ CAN_FMR_FINIT;
    CAN->IER |= CAN_IER_FMPIE0;
    NVIC_SetPriority(CEC_CAN_IRQn, 0);
    NVIC_EnableIRQ(CEC_CAN_IRQn);
    //CAN_OperatingModeRequest(&CAN_OperatingMode,CAN_OperatingMode_Normal);
        
//    this->sourceAddress=0x44;        //Адрес источника
//    this->ID1=0x18EE0000;            //Идентификатор Пакета_1
//    this->ID2=0x18EF0000;            //Идентификатор Пакета_2
}

//||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||
void Can::handler(void)//обработка полученного параметра или команды
{   

}

//||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||
void Can::SendMessage(uint32_t AID, uint8_t AD0, uint8_t AD1, uint8_t AD2, uint8_t AD3, uint8_t AD4, uint8_t AD5, uint8_t AD6, uint8_t AD7)
{       
    uint8_t TransmitMailBox=0;
    uint8_t status;
    
    TxMessage.StdId  = 0;
    TxMessage.ExtId = AID;
    TxMessage.RTR = CAN_RTR_DATA;
    TxMessage.IDE = CAN_ID_EXT;
    TxMessage.DLC = 8;   
    TxMessage.Data[0] = AD0;
    TxMessage.Data[1] = AD1;
    TxMessage.Data[2] = AD2;
    TxMessage.Data[3] = AD3;
    TxMessage.Data[4] = AD4;
    TxMessage.Data[5] = AD5;
    TxMessage.Data[6] = AD6;
    TxMessage.Data[7] = AD7;   
    
    TransmitMailBox=CAN_Transmit(CAN,&TxMessage);
    uint32_t i=0;
    status=0;
    while(status!=CANTXOK) {
        status=CAN_TransmitStatus(CAN, TransmitMailBox);
        i++;
        if (i==0xFFFF) {this->initialize(); break;}
    }
}

canMessageType_t Can::CheckReceiveMsgAddr(CanRxMsg *CanIdentifier)
{       
    if (((CanIdentifier->ExtId >> 3)& 0x7F) < 126 && ((CanIdentifier->ExtId >> 3)& 0x7F) != 37){                 // Передатчик не устройство управления
        if (/*(((CanIdentifier->ExtId >> 10)& 0x07) == MY_CAN_ADDR || ((CanIdentifier->ExtId >> 10)& 0x07) == CAN_BROADCAST_ADDR) &&*/  // Приёмник устройство управления с нашим адресом, то есть наверное это мы))
            (((CanIdentifier->ExtId >> 13)& 0x7F) == MY_CAN_TYPE || ((CanIdentifier->ExtId >> 13)& 0x7F) == CAN_BROADCAST_TYPE)){
            return HEATER_MSG;
        }            
    }
    else if ((((CanIdentifier->ExtId >> 3)& 0x7F) == 126) && (((CanIdentifier->ExtId >> 0)& 0x07) == 0 || ((CanIdentifier->ExtId >> 0)& 0x07) == CAN_BROADCAST_ADDR)){ // сообщение от нулевого пульта. //TODO: обмен только с одним пультом!
        if ((((CanIdentifier->ExtId >> 10)& 0x07) == MY_CAN_ADDR) && ((CanIdentifier->ExtId >> 13)& 0x7F) == MY_CAN_TYPE){
            return PANEL_MSG;
        }  
        else{
            // временно
            return PANEL_MSG;
        }            
    }
    return UNKNOWN_MSG;
}

void Can::sendCanRxMsgToHeaterParser(CanRxMsg *RxMessage)
{
    
    uint32_t address;
    uint8_t i;
    bool result;
    
    address = (RxMessage->ExtId & 0x3FF);
    result = false;
    for (i=0; i<3; i++){
        if ((heater.device[i].address) == (address)){
            result = true;
            //if (i == selectedDevice){
                heater.parsingMessage(RxMessage->ExtId >> 20, i);
            //}
            break;
        }
    }
    if (result == false){
        for (i=0; i<3; i++){
            if (heater.device[i].address == (0x3ff<<3)){
                heater.device[i].address = address;
                result = true;
                //if (i == selectedDevice){
                    heater.parsingMessage(RxMessage->ExtId >> 20, i);
                //}
                break;
            }
        }
    }  
}

void Can::sendCanRxMsgToPanelParser(CanRxMsg *RxMessage)
{
     heater.parsingMessagePanel(RxMessage->ExtId >> 20);
}

//Прерывание по приему CAN
extern "C" void CEC_CAN_IRQHandler(void)
{    

    can.linkCnt=0;
    canMessageType_t canMessageType;
    
    if (CAN_GetITStatus(CAN, CAN_IT_FMP0) == SET) {                      // Прерывание получения пакета в буфер FIFO 0
                                                                         // Флаг сбрасывается автоматически после прочтения последнего сообщени
        
        CAN_Receive(CAN, CAN_FIFO0, &can.RxMessage);                     
        
        canMessageType = can.CheckReceiveMsgAddr(&can.RxMessage);
        if (canMessageType==HEATER_MSG){
            can.sendCanRxMsgToHeaterParser(&can.RxMessage);
        }
        else if (canMessageType==PANEL_MSG){
            can.sendCanRxMsgToPanelParser(&can.RxMessage);
        }
        CAN_ClearITPendingBit(CAN,CAN_IT_FMP0);      
    }
    
    if (CAN_GetITStatus(CAN, CAN_IT_FMP1) == SET) {                  // Прерывание получения пакета в буфер FIFO 1
                                                                     // Флаг сбрасывается автоматически после прочтения последнего сообщения

        CAN_Receive(CAN, CAN_FIFO1, &can.RxMessage);                 // Получим сообщение
              
        canMessageType = can.CheckReceiveMsgAddr(&can.RxMessage);
        if (canMessageType==HEATER_MSG){
            can.sendCanRxMsgToHeaterParser(&can.RxMessage);
        }
        else if (canMessageType==PANEL_MSG){
            can.sendCanRxMsgToPanelParser(&can.RxMessage);
        }
                    
        CAN_ClearITPendingBit(CAN,CAN_IT_FMP1);
    }

}  
#endif

//||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||

