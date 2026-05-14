/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __LED_H
#define __LED_H

/* Includes ------------------------------------------------------------------*/
#include "n32wb452.h"
#include "main.h"
#include "gpio.h"

/* Defines ------------------------------------------------------------------*/
#define LED_MODE_OFF            0
#define LED_MODE_GREEN          1
#define LED_MODE_YELLOW         2

#define LED_MODE_GSM_OFF            0
#define LED_MODE_GSM_ON             1
#define LED_MODE_GSM_BLINK_SLOW     2
#define LED_MODE_GSM_BLINK_FAST     3
#define LED_MODE_GSM_BLINK_MAX      4
#define LED_MODE_GSM_BLINK_TWO      5
#define LED_MODE_GSM_BLINK_THREE    6

/* Classes ------------------------------------------------------------------*/
class Led
{
    public:
        
        void initialize(void);
        void handler(void);
        
        uint8_t codeToCount(uint8_t error);
        void setPercent(uint8_t percent);
        void setHeaterLed(uint8_t mode);
        void setFrozenTime(uint32_t duration);
        
        uint8_t modeGsm, modeHeater;
        uint8_t codeHeater;
        uint32_t timerFrozen;

    private:
        void handlerGsm(void);
        void handlerHeater(void);
        Gpio_C LedYellow;
        Gpio_C LedGreen;
        Gpio_C LedMode;

};
extern Led led;

#endif /* __LED_H */
