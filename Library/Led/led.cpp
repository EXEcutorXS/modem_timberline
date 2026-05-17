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
#include "button.h"
//#include "gsm.h"

Led led;
//-----------------------------------------------------
void Led::initialize(void)
{
    LedMode.Initialize(GPIOB, GPIO_PIN_2, GPIO_Mode_Out_PP);
    // Configure the Led pin on button
    if(versionHardware == 1){
        LedGreen.Initialize(GPIOA, GPIO_PIN_6, GPIO_Mode_Out_PP);
        LedYellow.Initialize(GPIOA, GPIO_PIN_2, GPIO_Mode_Out_PP);
    } else {
        LedGreen.Initialize(GPIOA, GPIO_PIN_6, GPIO_Mode_Out_PP);
        LedYellow.Initialize(GPIOC, GPIO_PIN_13, GPIO_Mode_Out_PP);
    }
}
//-----------------------------------------------------
void Led::handler(void)
{
    if (core.getTick() >= timerFrozen){
        handlerGsm();
        handlerHeater();
    }
    else{
        if ((timerFrozen-core.getTick()) >= 10000){
            // protect on reload msTick
            timerFrozen = core.getTick();
        }
    }
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
void Led::handlerHeater(void)
{
    static uint32_t timer = 0;
    static bool state = false;
    static uint16_t pause = 500;
    static uint8_t count = 0;
    
    if ((core.getTick()-timer) > pause){
        timer = core.getTick();
        
        {
            switch(modeHeater){
                case 0:
                    setHeaterLed(LED_MODE_OFF);
                    count = 0;
                    break;
                case 1:
                    setHeaterLed(LED_MODE_GREEN);
                    count = 0;
                    break;
                case 2:
                    pause = 500;
                    state = !state;
                    if (state) setHeaterLed(LED_MODE_GREEN);
                    else setHeaterLed(LED_MODE_OFF);
                    count = 0;
                    break;
                case 3:
                    pause = 100;
                    state = !state;
                    if (state) setHeaterLed(LED_MODE_GREEN);
                    else setHeaterLed(LED_MODE_OFF);
                    count = 0;
                    break;
                case 4:
                    state = !state;
                    pause = 300;
                    if (!state){
                        count++;
                        if (count >= codeHeater){
                            count = 0;
                            pause = 2000;
                        }
                        else{
                            pause = 300;
                        }
                    }
                    
                    if (state) setHeaterLed(LED_MODE_YELLOW);
                    else setHeaterLed(LED_MODE_OFF);
                    break;
                case 5:
                    pause = 1500;
                    state = !state;
                    if (state) setHeaterLed(LED_MODE_GREEN);
                    else setHeaterLed(LED_MODE_OFF);
                    count = 0;
                    break;
            }
        }
    }
}
//-----------------------------------------------------
uint8_t Led::codeToCount(uint8_t error)
{
    uint8_t ledErrorCode;
    
    switch (error)
	{
	case 1: // ---
		ledErrorCode = 1;
		break;

	case 2:				  // ---
		ledErrorCode = 1; // ---
		break;

	case 3: // ---
		ledErrorCode = 6;
		break;

	case 4:				  // ---
		ledErrorCode = 6; // ---
		break;

	case 5:				  // ---
		ledErrorCode = 5; // ---
		break;

	case 6: // ---
		ledErrorCode = 6;
		break;

	case 7: // ---
		ledErrorCode = 3;
		break;

	case 8: // ---
		ledErrorCode = 3;
		break;

	case 9: // ---
		ledErrorCode = 4;
		break;

	case 10: //    (  )
		ledErrorCode = 4;
		break;

	case 11: // .     
		ledErrorCode = 1;
		break;

	case 12: // ,    16 (30)
		ledErrorCode = 9;
		break;

	case 13:			  // ---
		ledErrorCode = 2; // ---
		break;

	case 14: // ---
		ledErrorCode = 7;
		break;

	case 15: // ,    10,5 (20)
		ledErrorCode = 9;
		break;

	case 16:			   // ---
		ledErrorCode = 10; // ---
		break;

	case 17:			  //    - ǻ
		ledErrorCode = 7; // ---
		break;

	case 19:			  // ---
		ledErrorCode = 3; // ---
		break;

	case 20:			   // ---
		ledErrorCode = 13; // ---
		break;

	case 21:			  // ---
		ledErrorCode = 3; // ---
		break;

	case 22:			  //    - »
		ledErrorCode = 7; // ---
		break;

	case 24:			   // ---
		ledErrorCode = 16; // ---
		break;
	case 25:			   //    -
		ledErrorCode = 17; // ---
		break;
	case 27:			  //     (  )
		ledErrorCode = 4; // ---
		break;
	case 28:			  //    (   )
		ledErrorCode = 4; // ---
		break;
	case 29:			   // ---
		ledErrorCode = 3; // 20
		break;
	case 30:			   // ---
		ledErrorCode = 21; // ---
		break;
	case 37:			   // ---
		ledErrorCode = 22; // ---
		break;
	case 50:			   // ---
		ledErrorCode = 23; // ---
		break;
	case 78:			   // ---
		ledErrorCode = 24; // ---
		break;
	default:
		ledErrorCode = 25; // ---
		break;
	}
    
    return ledErrorCode;
}
//-----------------------------------------------------
void Led::setHeaterLed(uint8_t mode)
{
    switch (mode){
        case 0:
            LedGreen.Reset();
            LedYellow.Reset();
            break;
        case 1:
            LedGreen.Set();
            LedYellow.Reset();
            break;
        case 2:
            LedGreen.Reset();
            if (button.getState()) LedYellow.Reset();
            else LedYellow.Set();
            break;
        default:
            LedGreen.Reset();
            LedYellow.Reset();
        break;
    }
}
//-----------------------------------------------------
void Led::setFrozenTime(uint32_t duration)
{
    timerFrozen = core.getTick()+duration;
}
//-----------------------------------------------------
