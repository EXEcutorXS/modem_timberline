#include "modem_handler.h"

ModeTypeDef mode = MODE_SLEEP;
int levelGsm = 0;
char serialNumberModem[16] = {0};

uint8_t step = 0;
uint8_t stepOld = 0;
bool isReset = false;
bool isChangePhones = false;
char updateToVersion[16] = {0};
uint16_t keyToNeedReset = 0;
uint8_t counterTrouble = 0;
char dtmfChar = 0;
bool isUnknownRing = false;
bool isConnectedSocket = false;

void setLowPower(bool enable)
{
    (void)enable;
}

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
