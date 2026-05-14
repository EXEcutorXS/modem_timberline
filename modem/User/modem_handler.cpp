#include "modem_handler.h"

ModeTypeDef mode = MODE_SLEEP;
int levelGsm = 0;
char serialNumberModem[16] = {0};

void changeMode(ModeTypeDef newMode)
{
    mode = newMode;
}

void modeInit(void)
{
    // TODO: GSM initialization sequence
}

void modeWork(void)
{
    // TODO: main work loop — receive SMS, send CAN, reply via SMS
}

void modemHandler(void)
{
    switch (mode) {
        case MODE_SLEEP: break;
        case MODE_WAIT:  break;
        case MODE_INIT:  modeInit(); break;
        case MODE_WORK:  modeWork(); break;
    }
}
