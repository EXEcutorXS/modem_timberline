#ifndef __MODEM_HANDLER_H__
#define __MODEM_HANDLER_H__

#include <stdint.h>
#include "gsm.h"
#include "sms.h"
#include "can.h"
#include "led.h"

typedef enum {
    MODE_SLEEP = 0,
    MODE_WAIT  = 1,
    MODE_INIT  = 2,
    MODE_WORK  = 3,
} ModeTypeDef;

extern ModeTypeDef mode;

extern int levelGsm;
extern char serialNumberModem[16];

/* Legacy compatibility globals used in gsm.cpp / sms.cpp / core.cpp */
extern uint8_t step;
extern uint8_t stepOld;
#define MODE_WORK_STEP_READ_SMS 1

extern bool isReset;
extern bool isChangePhones;
extern char updateToVersion[16];
extern uint16_t keyToNeedReset;
extern uint8_t counterTrouble;
extern char dtmfChar;
extern bool isUnknownRing;
extern bool isConnectedSocket;

void setLowPower(bool enable);

void modemHandler(void);
void changeMode(ModeTypeDef newMode);
void modeInit(void);
void modeWork(void);

#endif /* __MODEM_HANDLER_H__ */
