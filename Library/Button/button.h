/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __BUTTON_H
#define __BUTTON_H

/* Includes ------------------------------------------------------------------*/
#include "n32wb452.h"
#include "main.h"

/* Classes ------------------------------------------------------------------*/
class Button
{
    public:
        void initialize(void);
        void handler(void);
        bool getState(void);
    
        uint8_t buttonStatus;
    
        enum state_t
        {
            _BUTTON_STATE_RELEASED,
            _BUTTON_STATE_PRESSED
        };
        
        enum status_t
        {
            _BUTTON_STATUS_IDLE,
            _BUTTON_STATUS_PRESSED,
            _BUTTON_STATUS_LONG_HOLD,
            _BUTTON_STATUS_RESET_HOLD
        };
        
        state_t state;
        status_t status;
        
        Button &resetStatus(void);
        
    private:
        uint32_t tickWhenPress;
        uint32_t tickLastTouch;
        uint32_t timerPress;
        uint32_t timerTout;
    
        uint8_t isPressed : 1;
        uint8_t isReleased : 1;
    
        static const uint16_t TIME_DEBOUNCE = 100;
        static const uint16_t TIME_LONG_HOLD = 1500;
        static const uint16_t TIME_RESET = 15000;
        static const uint16_t TIME_RELOAD = 1000;
    
};
extern Button button;

#endif /* __BUTTON_H */
