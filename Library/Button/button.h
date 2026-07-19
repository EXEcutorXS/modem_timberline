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

        /* Set by Timberline::init() — called on short/long press */
        void (*onShortPress)(void);
        void (*onLongPress)(void);

        /* Debug-only: watch this in the debugger to see what handler() is
           doing without instrumenting it with logging. */
        enum DebugState { BTN_IDLE, BTN_PRESSED, BTN_LONG_FIRED };
        DebugState debugState;
        bool       gpioState;        /* live GPIO read, updated every handler() call */
        bool       rawPrev;         /* previous raw reading, for debounce edge */
        bool       debounced;       /* current debounced pressed state */
        uint32_t   rawChangeTick;
        uint32_t   pressTick;       /* when the debounced press started */
        bool       longFired;       /* long-press already fired for this hold */

    private:
        static const uint16_t TIME_DEBOUNCE  = 30;
        static const uint16_t TIME_LONG_HOLD = 1500;
};
extern Button button;

#endif /* __BUTTON_H */
