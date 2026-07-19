/******************************************************************************
*  DD Inform
*
*
* :  ..,  ..
*
* 26.03.2026
* :
*******************************************************************************/
/* Includes ------------------------------------------------------------------*/
#include "led.h"
#include "Timberline.h"
//#include "gsm.h"

Led led;
//-----------------------------------------------------
void Led::initialize(void)
{
    LedMode.Initialize(GPIOB, GPIO_PIN_2, GPIO_Mode_Out_PP);
    // Green channel — retired (kept driven low; see handlerButtonLed).
    LedGreen.Initialize(GPIOC, GPIO_PIN_13, GPIO_Mode_Out_PP);
    // Yellow channel — PWM (TIM3_CH1 on PA6), not a plain GPIO.
    initYellowPwm();
}
//-----------------------------------------------------
/* PA6 = TIM3_CH1 in the default (no-remap) AFIO config. TIM3 is otherwise
   unused at runtime (bsp_timer.c's TIM3 helpers exist but nothing calls
   them), so this doesn't collide with anything. */
void Led::initYellowPwm(void)
{
    RCC_EnableAPB2PeriphClk(RCC_APB2_PERIPH_GPIOA | RCC_APB2_PERIPH_AFIO, ENABLE);
    RCC_EnableAPB1PeriphClk(RCC_APB1_PERIPH_TIM3, ENABLE);

    GPIO_InitType gpioInit;
    gpioInit.Pin        = GPIO_PIN_6;
    gpioInit.GPIO_Mode  = GPIO_Mode_AF_PP;
    gpioInit.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_InitPeripheral(GPIOA, &gpioInit);

    TIM_TimeBaseInitType timeBase;
    timeBase.Prescaler = 47;           /* 48MHz / 48 = 1MHz timer tick */
    timeBase.CntMode   = TIM_CNT_MODE_UP;
    timeBase.Period    = PWM_PERIOD;   /* 1MHz / 1000 = 1kHz PWM, no visible flicker */
    timeBase.ClkDiv    = 0;
    timeBase.RepetCnt  = 0;
    /* TIM_InitTimeBase() reads these for TIM3 too (routes capture inputs
       from the comparator vs. the I/O pin) — keep them off explicitly
       rather than leaving stack garbage in an uninitialized struct. */
    timeBase.CapCh1FromCompEn    = false;
    timeBase.CapCh2FromCompEn    = false;
    timeBase.CapCh3FromCompEn    = false;
    timeBase.CapCh4FromCompEn    = false;
    timeBase.CapEtrClrFromCompEn = false;
    timeBase.CapEtrSelFromTscEn  = false;
    TIM_InitTimeBase(TIM3, &timeBase);

    OCInitType ocInit;
    ocInit.OcMode       = TIM_OCMODE_PWM1;
    ocInit.OutputState  = TIM_OUTPUT_STATE_ENABLE;
    ocInit.OutputNState = TIM_OUTPUT_NSTATE_DISABLE;
    ocInit.Pulse        = 0;
    ocInit.OcPolarity   = TIM_OC_POLARITY_HIGH;
    ocInit.OcNPolarity  = TIM_OC_POLARITY_HIGH;
    ocInit.OcIdleState  = TIM_OC_IDLE_STATE_RESET;
    ocInit.OcNIdleState = TIM_OCN_IDLE_STATE_RESET;
    TIM_InitOc1(TIM3, &ocInit);
    TIM_ConfigOc1Preload(TIM3, TIM_OC_PRE_LOAD_ENABLE);

    TIM_Enable(TIM3, ENABLE);
}
//-----------------------------------------------------
void Led::setYellowDuty(uint16_t duty)
{
    if (duty > PWM_PERIOD) duty = PWM_PERIOD;
    TIM_SetCmp1(TIM3, duty);
}
//-----------------------------------------------------
void Led::handler(void)
{
    handlerGsm();
    handlerButtonLed();
}
//-----------------------------------------------------
void Led::handlerGsm(void)
{
    static uint32_t timer = 0;
    static bool state = false;
    static uint16_t pause = 500;
    static uint8_t counter = 0;

    if ((core.getTick()-timer) >= pause){
        timer = core.getTick();

        switch(modeGsm){
            case LED_MODE_GSM_OFF:
                LedMode.Reset();
                break;
            case LED_MODE_GSM_ON:
                LedMode.Set();
                break;
            case LED_MODE_GSM_BLINK_SLOW:
                state = !state;
                pause = 300;
                if (!state){
                    pause = 700;
                }
                if (state) LedMode.Set();
                else LedMode.Reset();
                break;
            case LED_MODE_GSM_BLINK_FAST:
                pause = 150;
                state = !state;
                if (state) LedMode.Set();
                else LedMode.Reset();
                break;
            case LED_MODE_GSM_BLINK_MAX:
                pause = 50;
                state = !state;
                if (state) LedMode.Set();
                else LedMode.Reset();
                break;
            case LED_MODE_GSM_BLINK_TWO:
                state = !state;
                pause = 300;
                if (!state){
                    counter++;
                    if (counter >= 2){
                        counter = 0;
                        pause = 1000;
                    }
                    else{
                        pause = 300;
                    }
                }
                if (state) LedMode.Set();
                else LedMode.Reset();
                break;
            case LED_MODE_GSM_BLINK_THREE:
                state = !state;
                pause = 300;
                if (!state){
                    counter++;
                    if (counter >= 3){
                        counter = 0;
                        pause = 1000;
                    }
                    else{
                        pause = 300;
                    }
                }
                if (state) LedMode.Set();
                else LedMode.Reset();
                break;
        }
    }
}
//-----------------------------------------------------
/* Yellow (PWM) channel on the button:
 *   - any active fault (priority)  -> blink out the first fault's code,
 *                                      digit by digit (hundreds/tens/units),
 *                                      fast flashes with short gaps between
 *                                      digits and a long pause before repeat
 *   - burner + element together    -> steady on
 *   - element only                 -> 2 quick blinks every 2 s
 *   - burner only                  -> smooth breathing pulse
 *   - neither                      -> off
 * Green is retired — a lit LED was found to interfere with reading the
 * button's own GPIO pin; kept off unconditionally. */
void Led::handlerButtonLed(void)
{
    LedGreen.Reset();

    uint8_t errorCode = 0;
    for (uint8_t i = 0; i < 8; i++) {
        if (timberline.errors[i]) { errorCode = timberline.errors[i]; break; }
    }

    if (errorCode) {
        stepErrorBlink(errorCode);
        return;
    }

    bool burnerOn  = timberline.HeaterButton;
    bool elementOn = timberline.ElementButton;

    static uint32_t timer = 0;
    static bool     state = false;
    static int16_t  breathLevel = 0;
    static int8_t   breathDir   = 1;

    if (burnerOn && elementOn) {
        setYellowDuty(PWM_PERIOD);
        state = false; breathLevel = 0; breathDir = 1;
    } else if (elementOn) {
        /* Slow square-wave blink: 2 s on, 2 s off */
        if ((core.getTick() - timer) >= 2000) {
            timer = core.getTick();
            state = !state;
            setYellowDuty(state ? PWM_PERIOD : 0);
        }
        breathLevel = 0; breathDir = 1;
    } else if (burnerOn) {
        /* Smooth breathing: ~10ms per step, ~100 steps up + 100 down ≈ 2s cycle */
        if ((core.getTick() - timer) >= 10) {
            timer = core.getTick();
            breathLevel += breathDir * (PWM_PERIOD / 100);
            if (breathLevel >= PWM_PERIOD) { breathLevel = PWM_PERIOD; breathDir = -1; }
            if (breathLevel <= 0)          { breathLevel = 0;          breathDir = 1;  }
            setYellowDuty((uint16_t)breathLevel);
        }
        state = false;
    } else {
        setYellowDuty(0);
        state = false; breathLevel = 0; breathDir = 1;
    }
}
//-----------------------------------------------------
/* Blink out a fault code digit by digit (hundreds, tens, units — leading
   zero digits contribute only the inter-digit gap, no flashes). Fast
   flashes within a digit, a short gap between digits, a long pause before
   the whole 3-digit sequence repeats. */
void Led::stepErrorBlink(uint8_t code)
{
    static uint32_t timer    = 0;
    static uint16_t pause    = 10;
    static uint8_t  digitIdx = 0;
    static uint8_t  step     = 0;   /* flash sub-step within the current digit */
    static bool     longPause = false;

    if ((core.getTick() - timer) < pause) return;
    timer = core.getTick();

    if (longPause) {
        longPause = false;
        digitIdx  = 0;
        step      = 0;
    }

    uint8_t digits[3] = { (uint8_t)(code/100), (uint8_t)((code/10)%10), (uint8_t)(code%10) };
    uint8_t n = digits[digitIdx];

    if (step < (uint8_t)(2*n)) {
        bool on = (step % 2) == 0;
        setYellowDuty(on ? PWM_PERIOD : 0);
        step++;
        pause = 150;
    } else {
        setYellowDuty(0);
        digitIdx++;
        step = 0;
        if (digitIdx >= 3) {
            longPause = true;
            pause = 2000;
        } else {
            pause = 500;
        }
    }
}
//-----------------------------------------------------
