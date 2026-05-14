#ifndef __GPIO_CPP
#define __GPIO_CPP

#include "n32wb452.h"

class Gpio_C {
    public:
        void Initialize(GPIO_Module* APort,uint16_t APin, GPIO_ModeType AMode)
        {
            port = APort;
            pin = APin;
            mode = AMode;
            if (port == GPIOA) RCC_EnableAPB2PeriphClk(RCC_APB2_PERIPH_GPIOA, ENABLE);
            if (port == GPIOB) RCC_EnableAPB2PeriphClk(RCC_APB2_PERIPH_GPIOB, ENABLE);
            if (port == GPIOC) RCC_EnableAPB2PeriphClk(RCC_APB2_PERIPH_GPIOC, ENABLE);
            if (mode==GPIO_Mode_AF_OD || mode==GPIO_Mode_AF_OD) RCC_EnableAPB2PeriphClk(RCC_APB2_PERIPH_AFIO, ENABLE);
                    
            GPIO_InitType GPIO_InitStructure;
            GPIO_InitStructure.Pin = pin;
            GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
            GPIO_InitStructure.GPIO_Mode = mode;
            GPIO_InitPeripheral(port, &GPIO_InitStructure);
        }
        void ChangeMode(GPIO_ModeType AMode)
        {
            mode = AMode;
            if (mode==GPIO_Mode_AF_OD || mode==GPIO_Mode_AF_OD) RCC_EnableAPB2PeriphClk(RCC_APB2_PERIPH_AFIO, ENABLE);
                    
            GPIO_InitType GPIO_InitStructure;
            GPIO_InitStructure.Pin = pin;
            GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
            GPIO_InitStructure.GPIO_Mode = mode;
            GPIO_InitPeripheral(port, &GPIO_InitStructure);
        }
        void Set(void)
        {
            if (mode==GPIO_Mode_Out_OD || mode==GPIO_Mode_Out_PP) {
                GPIO_SetBits(port, pin);
            }
        }
        void Reset(void)
        {
            if (mode==GPIO_Mode_Out_OD || mode==GPIO_Mode_Out_PP) {
                GPIO_ResetBits(port, pin);
            }
        }
        bool Get(void)
        {
            return GPIO_ReadInputDataBit(port, pin);
        }
    private:
        GPIO_Module* port;
        uint16_t pin;
        GPIO_ModeType mode;
};

#endif /*__GPIO_CPP*/
