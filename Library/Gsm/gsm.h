#ifndef __GSM_H
#define __GSM_H

#include "n32wb452.h"
#include "main.h"
#include <cstdint>
#include "usart.h"
#include "gpio.h"

extern uint8_t versionHardware;

typedef enum {
    OPERATOR_BEELINE = 0,
    OPERATOR_MEGAFON = 1,
    OPERATOR_MTS     = 2
} MobileOperator;

class Gsm
{
public:
    Usart_C usart;
    Gpio_C  WakeupPin;
    Gpio_C  PowergoodPin;
    Gpio_C  RingPin;
    Gpio_C  DTRPin;
    Gpio_C  PowerkeyPin;

    Gsm(void);

    /* ── Process (state-machine top level) ──────────────────────────────── */
    typedef enum {
        PROCESS_POWER_ON,
        PROCESS_POWER_OFF,
        PROCESS_SLEEP_ON,
        PROCESS_SLEEP_OFF,
        PROCESS_WAIT_READY,
        PROCESS_INIT_GSM,
        PROCESS_REQUEST_BALANCE,
        PROCESS_REQUEST_CSQ,
        PROCESS_REQUEST_CREG,
        PROCESS_ANSWER_RING,
        PROCESS_EMPTY,
        PROCESS_SEND_SMS_ENGLISH,
        PROCESS_SEND_SMS_RUSSIAN,
        PROCESS_READ_SMS
    } ProcessTypeDef;

    /* ── Answer flags (bit N = ANSWER_STRINGS index N-2, set by parsing()) ─
     * Bit positions are fixed — must match ANSWER_STRINGS[] in gsm.cpp.    */
    typedef enum {
        ANSWER_TIMEOUT      = 1<<0,
        ANSWER_WAIT         = 1<<1,
        ANSWER_ERROR        = 1<<2,   /* "ERROR"            idx  0 */
        ANSWER_OK           = 1<<3,   /* "OK\r"             idx  1 */
        ANSWER_ALREADY      = 1<<4,   /* "+CIPOPEN: "       idx  2 */
        ANSWER_CALL_READY   = 1<<5,   /* "ATREADY"          idx  3 */
        ANSWER_CGMR         = 1<<6,   /* "+CGMR: "          idx  4 */
        ANSWER_RING         = 1<<7,   /* "RING"             idx  5 */
        ANSWER_SEND_OK      = 1<<8,   /* "+CIPSEND:"        idx  6 */
        ANSWER_COPS         = 1<<10,  /* "+COPS: "          idx  8 */
        ANSWER_CSQ          = 1<<12,  /* "+CSQ: "           idx 10 */
        ANSWER_CLOSED       = 1<<13,  /* "CLOSE"            idx 11 */
        ANSWER_CLIP         = 1<<14,  /* "+CLIP:"           idx 12 */
        ANSWER_CMTI         = 1<<15,  /* "+CMTI: \"SM\","   idx 13 */
        ANSWER_CMGR         = 1<<16,  /* "+CMGR:"           idx 14 */
        ANSWER_CMGS         = 1<<17,  /* "+CMGS: "          idx 15 */
        ANSWER_CUSD         = 1<<18,  /* "+CUSD:"           idx 16 */
        ANSWER_ICCID        = 1<<19,  /* "+ICCID: "         idx 17 */
        ANSWER_CNUM         = 1<<20,  /* "+CNUM:"           idx 18 */
        ANSWER_SOCKET       = 1<<21,  /* "RECV FROM:"       idx 19 */
        ANSWER_FTP          = 1<<22,  /* "+CFTPSGETFILE: "  idx 20 */
        ANSWER_0X3E         = 1<<23,  /* ">"                idx 21 */
        ANSWER_DTMF         = 1<<29,  /* "+RXDTMF: "        idx 27 */
        ANSWER_NO_CARRIER   = 1<<30   /* "NO CARRIER"       idx 28 */
    } AnswerTypeDef;
    static const uint32_t ANSWER_CREG = (uint32_t)1 << 31; /* "+CREG: " idx 29 */

    typedef enum {
        ERROR_EMPTY,
        ERROR_TIMEOUT,
        ERROR_ANSWER,
        ERROR_CONNECT
    } ErrorTypeDef;

    /* ── Public methods ─────────────────────────────────────────────────── */
    void initialize(void);
    void handler(void);
    void changeProcess(ProcessTypeDef number);

    void processPowerOn(void);
    void processPowerOff(void);
    void processSleepOn(void);
    void processSleepOff(void);
    void processWaitReady(void);
    void processInitGsm(void);
    void processRequestBalance(void);
    void processRequestCsq(void);
    void processRequestCreg(void);
    void processAnswerRing(void);

    bool sendMessage(const char *transMessage, uint32_t duration);
    void startTransmission(const char *array);
    void receiptNextByte(uint8_t byte);

    /* ── State ──────────────────────────────────────────────────────────── */
    char  phoneRing[16];
    bool  inputRing, inputWakeup, inputPowerGood;

    bool           isAnswer;
    int8_t         mode;
    ProcessTypeDef process;
    uint32_t       answer;
    ErrorTypeDef   error;

    bool isOnlySmsMode;

    /* parsing() quote / brace tracking — named per their actual use        */
    bool isInQuote;   /* true while between " " in CLIP/CUSD/COPS responses */
    bool isInBrace;   /* true after first '{' seen (SOCKET JSON)            */
    bool isInBrace2;  /* true after first '}' seen — waiting for second '}' */

    /* Payload captured after the last matched prefix                       */
    bool     isReadAnswerData;
    bool     isReadClip;
    uint16_t answerDataPoint;
    static const uint16_t ANSWER_ARRAY_MAX = 1024;
    uint8_t  answerData[ANSWER_ARRAY_MAX];

    /* Phone / PIN ──────────────────────────────────────────────────────── */
    char  posRingPhone;
    char  phones[5][16]; /* [0] = admin phone; [1..4] legacy — TODO: remove */
    char  pin[5];        /* 4-digit PIN, null-terminated, default "1234"     */

    /* Misc modem state ─────────────────────────────────────────────────── */
    char    phoneBalance[16];
    bool    isRing;
    int8_t  dtmfMode;
    MobileOperator mobileOperator;
    uint8_t  counterSendSms;
    uint32_t numberSms;
    bool     isNeedToRequestBalance;
    bool     isNeedToSendBalanceOnSms;

    bool    isReadImei;
    char    iccid[21];
    char    phoneSim[16];
    char    imei[16];
    char    cgmr[32];
    uint8_t cregStat;   /* last +CREG stat: 0=no, 1=home, 5=roaming */

    typedef enum {
        NUMSIM_SEARCH,
        NUMSIM_EXTERNAL,
        NUMSIM_INTERNAL,
        NUMSIM_NO
    } NumSimTypeDef;
    NumSimTypeDef numSim;

private:
    uint16_t parsing(void);
    void     processReceivedData(void);

    uint16_t bufferCursorR;
    uint16_t receivePoint;
    static const uint16_t RECEIVE_ARRAY_MAX = 1024;
    static const uint16_t TRANS_ARRAY_MAX   = 512;
    char transArray[TRANS_ARRAY_MAX];
    char receiveArray[RECEIVE_ARRAY_MAX];
};

extern Gsm gsm;

#endif /* __GSM_H */
