#include "button.h"
#include "math.h"
#include "led.h"

#include "log.h"

Button button;
//-----------------------------------------------------
Button &Button::resetStatus(void)
{
    status = _BUTTON_STATUS_IDLE;
    return *this;
}
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
    static status_t statusOld = _BUTTON_STATUS_IDLE;
    
    if (status != statusOld){
        statusOld = status;
        if (status == _BUTTON_STATUS_PRESSED){
            led.setFrozenTime(5000);
            log_info("\r\nBUTTON: PRESSED\r\n");
            // TODO: handle short press — send CAN command
        }
        if (status == _BUTTON_STATUS_LONG_HOLD){
            led.setFrozenTime(8000);
            log_info("\r\nBUTTON: LONG HOLD\r\n");
            // TODO: handle long press — send CAN command
        }
    }
    
    buttonStatus = getState();
    
    if(buttonStatus)
    {
        if(state != _BUTTON_STATE_PRESSED)
        {
            tickWhenPress = core.getTick();
        }
        state = _BUTTON_STATE_PRESSED;
        tickLastTouch = core.getTick();
    }
    else
    {
        timerPress = core.getTick() - tickWhenPress;
        state = _BUTTON_STATE_RELEASED;
    }
    
    
    if(state == _BUTTON_STATE_PRESSED)
    {
        isPressed = true;
        if ((core.getTick() - tickWhenPress) >= TIME_LONG_HOLD)
        {
            isPressed = false;
            if (isReleased)
            {
                isReleased = false;
                status = _BUTTON_STATUS_LONG_HOLD;
                timerTout = core.getTick() + TIME_RELOAD;
            }
        }
    }
    else
    {
        isReleased = true;
        if (isPressed)
        {
            isPressed = false;
            if (timerPress > TIME_DEBOUNCE)
            {
                status = _BUTTON_STATUS_PRESSED;
                timerTout = core.getTick() + TIME_RELOAD;
            }
        }
    }
    
    
    if ((status not_eq _BUTTON_STATUS_IDLE) and (core.getTick() > timerTout))
        resetStatus();
}
//-----------------------------------------------------
bool Button::getState(void)
{
    bool state = !GPIO_ReadInputDataBit(GPIOA, GPIO_PIN_8);
    return state;
}
