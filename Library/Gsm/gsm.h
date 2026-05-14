/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __GSM_H
#define __GSM_H

/* Includes ------------------------------------------------------------------*/
#include "n32wb452.h"
#include "main.h"
#include <cstdint>
#include "usart.h"
#include "gpio.h"

/* Defines ------------------------------------------------------------------ */

extern uint8_t versionHardware;
/* Classes ------------------------------------------------------------------*/

typedef enum MobileOperatorTypeDef {
	OPERATOR_BEELINE = 0,
	OPERATOR_MEGAFON = 1,
	OPERATOR_MTS = 2
} MobileOperator;

class Gsm
{
    public:
        Usart_C usart;
        Gpio_C WakeupPin;
        Gpio_C PowergoodPin;
        Gpio_C RingPin;
        Gpio_C DTRPin;
        Gpio_C PowerkeyPin;
        Gsm(void);
    
        typedef enum {
            PROCESS_POWER_ON,    
            PROCESS_POWER_OFF,
            PROCESS_SLEEP_ON,
            PROCESS_SLEEP_OFF,
            PROCESS_WAIT_READY,
            PROCESS_INIT_GSM,
            PROCESS_REQUEST_BALANCE,
            PROCESS_INIT_GPRS,
            PROCESS_INIT_CERTIFICAT,
            PROCESS_TRANSMIT_GPRS,
            PROCESS_CONNECT_WEB,
            PROCESS_TRANSMIT_WEB,
            PROCESS_CONNECT_FTP,
            PROCESS_FTP_GET,
            PROCESS_REQUEST_CSQ,
            PROCESS_ANSWER_RING,
            PROCESS_EMPTY,
            PROCESS_SEND_SMS_ENGLISH,
            PROCESS_SEND_SMS_RUSSIAN,
            PROCESS_READ_SMS
        } ProcessTypeDef;

        typedef enum {
            ANSWER_TIMEOUT =        1<<0,
            ANSWER_WAIT =           1<<1,
            
            ANSWER_ERROR =          1<<2,
            ANSWER_OK =             1<<3,
            ANSWER_ALREADY =          1<<4,
            ANSWER_CALL_READY =     1<<5,
            ANSWER_CGMR =           1<<6,
            ANSWER_RING =           1<<7,
            ANSWER_SEND_OK =           1<<8,
            
            ANSWER_CONNECT_FAIL =           1<<9,
            ANSWER_COPS =           1<<10,
            ANSWER_CONNECT_OK =           1<<11,
            ANSWER_CSQ =            1<<12,
            ANSWER_CLOSED =           1<<13,
            ANSWER_CLIP =           1<<14,
            ANSWER_CMTI =           1<<15,
            ANSWER_CMGR =           1<<16,
            
            ANSWER_CMGS =           1<<17,
            ANSWER_CUSD =           1<<18,
            ANSWER_ICCID =          1<<19,
            ANSWER_CNUM =           1<<20,
            ANSWER_SOCKET =          1<<21,
            ANSWER_FTP =            1<<22,
            ANSWER_0X3E =    1<<23,
            ANSWER_CFTPSLOGIN =     1<<24,
            
            ANSWER_DOWNLOAD =       1<<25,
            ANSWER_HTTPACTION =     1<<26,
            ANSWER_HTTPHEAD =       1<<27,
            ANSWER_HTTPREAD =       1<<28,
            ANSWER_DTMF =       1<<29,
            ANSWER_NO_CARRIER =       1<<30
        } AnswerTypeDef;
        
        typedef enum {
            ERROR_EMPTY,
            ERROR_TIMEOUT,
            ERROR_ANSWER,
            ERROR_CONNECT
        } ErrorTypeDef;
        
        void initialize(void);
//        void changeBaudrate(uint32_t baudrate);
        void handler(void);
//        void usartIrqHandler(void);
        void changeProcess(ProcessTypeDef number);
        void processPowerOn(void);
        void processPowerOff(void);
        void processSleepOn(void);
        void processSleepOff(void);
        void processWaitReady(void);
        void processInitGsm(void);
        void processRequestBalance(void);
        void processRequestCsq(void);
        
        void processAnswerRing(void);

        bool sendMessage(const char *transMessage, uint32_t duration);
//        void setDtr(bool state);
        void startTransmission(const char *array);
//        void transmitNextByte(void);
        void receiptNextByte(uint8_t byte);
        
        void bufRevers(char* str);
        void intToChar(char* str, int n);
//        void setPwrkey(bool state);
        
        char phoneRing[16];
        bool inputRing, inputWakeup, inputPowerGood;
        
//        bool isBusy;
        bool isAnswer;
        bool isReadJson, isReadJson1, isReadJson2;
        int8_t mode; // фаза текущего режима работы с GSM-модулем
        ProcessTypeDef process; // текущий режим работы с GSM-модулем
        uint32_t answer;
        ErrorTypeDef error;
        
        bool isOnlySmsMode;

		#define MAX_URL_LEN 99
        char strokeUrl[MAX_URL_LEN];
        uint16_t strokeUrlLength;
        char strokePost[1024];
        uint16_t strokePostLength;
        char postDataLog[1024];
        
        bool isReadAnswerData;
        bool isReadClip;
        uint16_t answerDataPoint;
        static const uint16_t ANSWER_ARRAY_MAX = 1024;
        uint8_t answerData[ANSWER_ARRAY_MAX];
        
        bool isNeedAnswerSmsOnStart,
            isNeedAnswerSmsOnStop,
            isNeedAnswerSmsOnFault;
        char posRingPhone;
        char phones[5][16];
        char phoneBalance[16];
        bool isRing;
        int8_t dtmfMode;
        MobileOperator mobileOperator;
        uint8_t counterSendSms;
        uint32_t numberSms;
        bool isNeedToRequestBalance;
        bool isNeedToSendBalanceOnSms;
        
        bool isReadImei;
        char iccid[21];
        char phoneSim[16];
        char imei[16];
        char cgmr[32];
        
        typedef enum {
            NUMSIM_SEARCH,
            NUMSIM_EXTERNAL,
            NUMSIM_INTERNAL,
            NUMSIM_NO
        } NumSimTypeDef;
        NumSimTypeDef numSim;
        
    
    private:
        uint16_t parsing(void);
        void processReceivedData(void);
        uint16_t receivePoint;
//        uint16_t transPoint;
        uint16_t bufferCursorR;
        static const uint16_t RECEIVE_ARRAY_MAX = 1024;  // 
        static const uint16_t TRANS_ARRAY_MAX = 512;  // 
        char transArray[TRANS_ARRAY_MAX], receiveArray[RECEIVE_ARRAY_MAX];
//        uint16_t point;
//        static const uint16_t BUFFER_ARRAY_MAX = 2048;
//        uint8_t buffer[BUFFER_ARRAY_MAX];   // 

};
extern Gsm gsm;

#endif /* __GSM_H */
