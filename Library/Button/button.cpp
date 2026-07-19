#include "button.h"
#include "log.h"

Button button;
//-----------------------------------------------------
void Button::initialize(void)
{
    GPIO_InitType GPIO_InitStructure;

    RCC_EnableAPB2PeriphClk(RCC_APB2_PERIPH_GPIOA, ENABLE);

    GPIO_InitStructure.Pin        = GPIO_PIN_8;
    GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_IPU;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;

    GPIO_InitPeripheral(GPIOA, &GPIO_InitStructure);
}
//-----------------------------------------------------
void Button::handler(void)
{
    bool raw = getState();
    gpioState = raw;   /* live pin read, for watching in the debugger */

    /* Debounce: accept a new raw level only once it's held stable. */
    if (raw != rawPrev) {
        rawPrev       = raw;
        rawChangeTick = core.getTick();
    }
    bool stable = (core.getTick() - rawChangeTick) >= TIME_DEBOUNCE;

    if (stable && raw != debounced) {
        debounced = raw;

        if (debounced) {
            /* Press edge */
            pressTick = core.getTick();
            longFired = false;
            debugState = BTN_PRESSED;
        } else {
            /* Release edge — short press, unless this hold already fired long */
            debugState = BTN_IDLE;
            if (!longFired) {
                log_info("\r\nBUTTON: PRESSED\r\n");
                if (onShortPress) onShortPress();
            }
        }
    }

    if (debounced && !longFired && (core.getTick() - pressTick) >= TIME_LONG_HOLD) {
        longFired  = true;
        debugState = BTN_LONG_FIRED;
        log_info("\r\nBUTTON: LONG HOLD\r\n");
        if (onLongPress) onLongPress();
    }
}
//-----------------------------------------------------
bool Button::getState(void)
{
    bool state = !GPIO_ReadInputDataBit(GPIOA, GPIO_PIN_8);
    return state;
}
