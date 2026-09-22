/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __NTC_H
#define __NTC_H

/* Includes ------------------------------------------------------------------*/
#include "n32wb452.h"
#include "n32wb452_adc.h"
#include "gpio.h"

/* Optional external temperature sensor on A1 (PA1) — not present on every
   board, so connected must be checked before trusting temperature at all
   (see handler()'s own comment on how "not connected" is actually
   detected). A 10 kOhm NTC (B=3950) forming a voltage divider with a fixed
   10 kOhm resistor:
       GND -- NTC -- A1 -- 10k -- 3.3V
   (NTC on the low/ground leg, fixed resistor on the 3.3V leg — the ADC
   reading rises with temperature, since a hotter NTC's resistance drops
   and pulls A1 further down... no, up: less NTC resistance means MORE of
   the divider's voltage drop is across the fixed 10k, so A1 itself sits
   closer to 0V. See readRaw()'s own math in the .cpp for the actual
   R_ntc/temperature derivation - this comment is just the wiring.) */
class Ntc
{
    public:
        void initialize(void);
        void handler(void);

        bool    connected;    /* only meaningful once handler() has run at least once */
        int8_t  temperature;  /* Celsius, valid only while connected */

    private:
        uint16_t readRaw(void);

        Gpio_C pin;
};
extern Ntc ntc;

#endif /* __NTC_H */
