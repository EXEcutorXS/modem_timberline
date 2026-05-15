#include "modem_handler.h"
#include "core.h"
#include "log.h"
#include "Converter.h"
#include <string.h>

/* ── State variables ──────────────────────────────────────────────────── */
ModeTypeDef mode      = MODE_SLEEP;
ModemState  modem     = { -1, false, false, {0}, {0}, {0}, {0} };

/* Globals still referenced by gsm.cpp / sms.cpp / core.cpp */
int      levelGsm          = 0;
uint8_t  counterTrouble    = 0;
uint8_t  step              = 0;
uint8_t  stepOld           = 0;
bool     isConnectedSocket = false;
bool     bridgeMode        = false;
volatile uint8_t  bridgeTxBuf[BRIDGE_TX_MAX];
volatile uint8_t  bridgeTxLen = 0;
bool     isReset           = false;
bool     isChangePhones    = false;
char     updateToVersion[16]   = {0};
uint16_t keyToNeedReset    = 0;
char     dtmfChar          = 0;
bool     isUnknownRing     = false;
char     serialNumberModem[16] = {0};

/* ── Helpers ──────────────────────────────────────────────────────────── */
void setLowPower(bool enable) { (void)enable; }

void changeMode(ModeTypeDef newMode) { mode = newMode; }

/* ── modeInit ─────────────────────────────────────────────────────────────
 * Drives the GSM power-on and init sequence.
 * Transitions to MODE_WORK once gsm reaches PROCESS_EMPTY (init done).  */
void modeInit(void)
{
    static uint8_t initStep = 0;

    switch (initStep) {
    case 0:
        log_info("\r\n=== Modem initializing ===\r\n");
        gsm.changeProcess(Gsm::PROCESS_POWER_ON);
        initStep = 1;
        break;

    case 1:
        if (gsm.process == Gsm::PROCESS_EMPTY) {
            strncpy(modem.imei,      gsm.imei,     sizeof(modem.imei)      - 1);
            strncpy(modem.iccid,     gsm.iccid,    sizeof(modem.iccid)     - 1);
            strncpy(modem.ownNumber, gsm.phoneSim, sizeof(modem.ownNumber) - 1);
            strncpy(modem.firmware,  gsm.cgmr,     sizeof(modem.firmware)  - 1);
            modem.csq          = (int8_t)levelGsm;
            modem.isRegistered = false;
            modem.isRoaming    = false;

            /* Print summary */
            char buf[32];
            log_info("IMEI:     "); log_info(modem.imei[0]      ? modem.imei      : "?"); log_info("\r\n");
            log_info("ICCID:    "); log_info(modem.iccid[0]     ? modem.iccid     : "?"); log_info("\r\n");
            log_info("Number:   "); log_info(modem.ownNumber[0] ? modem.ownNumber : "?"); log_info("\r\n");
            log_info("Firmware: "); log_info(modem.firmware[0]  ? modem.firmware  : "?"); log_info("\r\n");
            log_info("CSQ:      ");
            Convert.intToStr(modem.csq, buf);
            log_info(buf); log_info("\r\n");
            log_info("=== Ready ===\r\n\r\n");

            initStep = 0;
            changeMode(MODE_WORK);
        }
        break;
    }
}

/* ── modeWork ─────────────────────────────────────────────────────────────
 * Periodic polling: CSQ every 30 s, CREG every 60 s.                    */
void modeWork(void)
{
    static uint32_t timerCsq  = 0;
    static uint32_t timerCreg = 0;
    static bool     waitCsq   = false;
    static bool     waitCreg  = false;
    static int8_t   lastCsq   = -2;   /* track changes for log output    */
    static uint8_t  lastCreg  = 0xFF;

    uint32_t now = core.getTick();

    /* ── CSQ poll ── */
    if (!waitCsq && (now - timerCsq) >= 30000UL) {
        if (gsm.process == Gsm::PROCESS_EMPTY) {
            gsm.changeProcess(Gsm::PROCESS_REQUEST_CSQ);
            waitCsq = true;
        }
    }
    if (waitCsq && gsm.process == Gsm::PROCESS_EMPTY) {
        modem.csq = (levelGsm <= 31) ? (int8_t)levelGsm : -1;
        timerCsq  = core.getTick();
        waitCsq   = false;
        if (modem.csq != lastCsq) {
            char buf[32];
            lastCsq = modem.csq;
            log_info("[CSQ] ");
            Convert.intToStr(modem.csq, buf);
            log_info(buf); log_info("\r\n");
        }
    }

    /* ── CREG poll ── */
    if (!waitCreg && (now - timerCreg) >= 60000UL) {
        if (gsm.process == Gsm::PROCESS_EMPTY) {
            gsm.changeProcess(Gsm::PROCESS_REQUEST_CREG);
            waitCreg = true;
        }
    }
    if (waitCreg && gsm.process == Gsm::PROCESS_EMPTY) {
        modem.isRegistered = (gsm.cregStat == 1);
        modem.isRoaming    = (gsm.cregStat == 5);
        timerCreg = core.getTick();
        waitCreg  = false;
        if (gsm.cregStat != lastCreg) {
            lastCreg = gsm.cregStat;
            const char* s = "unknown";
            if      (gsm.cregStat == 0) s = "not registered";
            else if (gsm.cregStat == 1) s = "home";
            else if (gsm.cregStat == 2) s = "searching";
            else if (gsm.cregStat == 3) s = "denied";
            else if (gsm.cregStat == 5) s = "roaming";
            log_info("[CREG] ");
            log_info((char*)s);
            log_info("\r\n");
        }
    }
}

/* ── modemHandler ────────────────────────────────────────────────────── */
void modemHandler(void)
{
    switch (mode) {
        case MODE_SLEEP: break;
        case MODE_WAIT:  break;
        case MODE_INIT:  modeInit(); break;
        case MODE_WORK:  modeWork(); break;
    }
}
