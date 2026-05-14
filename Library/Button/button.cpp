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
    
    if (heater.stateTestStand != -1) return;
    
    if (isNeedToManualAccept){
        if ((core.getTick()-timerManualAccept) > (5*60*1000)){
            isNeedToManualAccept = false;
        }
    }
    
    if (status != statusOld){
        statusOld = status;
        if (status == _BUTTON_STATUS_PRESSED){
            if (heater.verProtocol == 3) {
                led.setFrozenTime(5000);
            } 
            else {
                led.setFrozenTime(8000);
            }
            if (isNeedToManualAccept){
                isNeedToManualAccept = false;
                isManualAccept = true;
                led.setHeaterLed(LED_MODE_OFF);
            }
            else{
                if (heater.device[heater.selectedDevice].stage == heater.MODE_WAIT){
                    log_info("\r\nBUTTON: START_HEATER\r\n");
                    story.add(STORY_START_COMMAND);//story.add(STORY_START_COMMAND_BUTTON);
                    heater.run();
                    led.setHeaterLed(LED_MODE_GREEN);
                    isCommandStart = true;
                }
                else{
                    log_info("\r\nBUTTON: FINISH\r\n");
                    story.add(STORY_STOP_COMMAND);//story.add(STORY_STOP_COMMAND_BUTTON);
                    heater.stop();
                    led.setHeaterLed(LED_MODE_OFF);
                    isCommandFinish = true;
                }
            }
        }
        if (status == _BUTTON_STATUS_LONG_HOLD){
            if (heater.verProtocol == 3) {
                led.setFrozenTime(5000);
            } 
            else {
                led.setFrozenTime(8000);
            }
            if (isNeedToManualAccept){
                isNeedToManualAccept = false;
                isManualCancel = true;
                led.setHeaterLed(LED_MODE_OFF);
            }
            else {
                if (heater.device[heater.selectedDevice].stage == heater.MODE_WAIT){
                    if (heater.device[heater.selectedDevice].isPreheater){
                        log_info("\r\nBUTTON: START_PUMP\r\n");
                        story.add(STORY_PUMP_COMMAND);//story.add(STORY_PUMP_COMMAND_BUTTON);
                        heater.pump();
                        led.setHeaterLed(LED_MODE_GREEN);
                        isCommandPump = true;
                    }
                    else{
                        story.add(STORY_VENTILATION_COMMAND);//story.add(STORY_VENTILATION_COMMAND_BUTTON);
                        heater.startVentilation(true);
                        led.setHeaterLed(LED_MODE_GREEN);
                    }
                }
                else{
                    log_info("\r\nBUTTON: FINISH\r\n");
                    story.add(STORY_STOP_COMMAND);//story.add(STORY_STOP_COMMAND_BUTTON);
                    heater.stop();
                    led.setHeaterLed(LED_MODE_OFF);
                    isCommandFinish = true;
                }
            }
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
