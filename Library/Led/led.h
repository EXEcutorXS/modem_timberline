/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __LED_H
#define __LED_H

/* Includes ------------------------------------------------------------------*/
#include "n32wb452.h"
#include "main.h"
#include "gpio.h"

/* Defines ------------------------------------------------------------------*/
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

        uint8_t modeGsm;

    private:
        void handlerGsm(void);
        void handlerButtonLed(void);

        /* Yellow channel (PA6 / TIM3_CH1) — PWM output, not a plain GPIO,
           so it can breathe for the burner-only pattern. */
        void     initYellowPwm(void);
        void     setYellowDuty(uint16_t duty);   /* 0..PWM_PERIOD */
        void     stepErrorBlink(uint8_t code);
        static const uint16_t PWM_PERIOD = 999;

        Gpio_C LedGreen;
        Gpio_C LedMode;

};
extern Led led;

#endif /* __LED_H */
