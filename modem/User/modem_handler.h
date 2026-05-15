#ifndef __MODEM_HANDLER_H__
#define __MODEM_HANDLER_H__

#include <stdint.h>
#include "gsm.h"
#include "sms.h"
#include "can.h"
#include "led.h"

/* ── Mode ────────────────────────────────────────────────────────────────── */
typedef enum {
    MODE_SLEEP = 0,
    MODE_WAIT  = 1,
    MODE_INIT  = 2,
    MODE_WORK  = 3,
} ModeTypeDef;

extern ModeTypeDef mode;

/* ── Modem runtime state ─────────────────────────────────────────────────
 * Populated during modeInit(), refreshed periodically in modeWork().    */
typedef struct {
    int8_t  csq;            /* signal quality: 0-31; -1 = unknown         */
    bool    isRegistered;   /* registered on home network  (+CREG: 1)      */
    bool    isRoaming;      /* registered while roaming   (+CREG: 5)       */
    char    imei[16];       /* IMEI, null-terminated                       */
    char    iccid[21];      /* ICCID, null-terminated                      */
    char    ownNumber[16];  /* own SIM number, null-terminated             */
    char    firmware[32];   /* modem firmware version string               */
} ModemState;

extern ModemState modem;

/* ── Globals still referenced by gsm.cpp / sms.cpp / core.cpp ───────── */
extern int      levelGsm;           /* raw CSQ value written by gsm.cpp   */
extern uint8_t  counterTrouble;     /* consecutive init failures           */
extern uint8_t  step;               /* SMS read sub-step                   */
extern uint8_t  stepOld;
#define MODE_WORK_STEP_READ_SMS 1
extern bool     isConnectedSocket;
extern bool     bridgeMode;         /* USB VCP <-> modem passthrough     */

/* Bridge TX buffer: filled by USB ISR, drained by gsm.handler() main loop */
#define BRIDGE_TX_MAX  64
extern volatile uint8_t  bridgeTxBuf[BRIDGE_TX_MAX];
extern volatile uint8_t  bridgeTxLen;
extern bool     isReset;
extern bool     isChangePhones;
extern char     updateToVersion[16];
extern uint16_t keyToNeedReset;
extern char     dtmfChar;
extern bool     isUnknownRing;
extern char     serialNumberModem[16];

/* ── API ─────────────────────────────────────────────────────────────────── */
void setLowPower(bool enable);
void modemHandler(void);
void changeMode(ModeTypeDef newMode);
void modeInit(void);
void modeWork(void);

#endif /* __MODEM_HANDLER_H__ */
