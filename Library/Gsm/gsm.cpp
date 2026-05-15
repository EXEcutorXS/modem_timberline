/******************************************************************************
*  
* 
* 
* :  ..
* 
* 14.11.2024
* :
*******************************************************************************/
/* Includes ------------------------------------------------------------------*/
#include "Converter.h"
#include "gsm.h"
#include "sms.h"
#include "modem_handler.h"
#include <memory>
#include <string.h>

#include "hw_config.h"
#include "log.h"

static const uint16_t ANSWER_STRINGS_MAX = 30;
static const char ANSWER_STRINGS[ANSWER_STRINGS_MAX][20] =
{
    "ERROR",                    // 0
    "OK\r",                     // 1
    "+CIPOPEN: ",               // 2
    "ATREADY",                  // 3
    "+CGMR: ",                  // 4
    "RING",                     // 5
    "+CIPSEND:",                // 6

    "CONNECT FAIL\r",           // 7
    "+COPS: ",                  // 8
    "CONNECT OK\r",             // 9
    "+CSQ: ",                   // 10
    "CLOSE",                    // 11
    "+CLIP:",                   // 12
    "+CMTI: \"SM\",",           // 13
    "+CMGR:",                   // 14

    "+CMGS: ",                  // 15
    "+CUSD:",                   // 16
    "+ICCID: ",                 // 17
    "+CNUM:",                   // 18
    "RECV FROM:",               // 19
    "+CFTPSGETFILE: ",          // 20
    ">",                        // 21
    "+CFTPSLOGIN: ",            // 22

    "DOWNLOAD\r",               // 23
    "+HTTPACTION:",             // 24
    "+HTTPHEAD:",               // 25
    "+HTTPREAD:",               // 26
    "+RXDTMF: ",                // 27
    "NO CARRIER",               // 28
    "+CREG: "                   // 29 -> ANSWER_CREG (1<<31)
};

uint8_t versionHardware;
extern bool isConnectedSocket;

Gsm gsm;

extern "C" void USART1_IRQHandler(void)
{
    if (USART_GetIntStatus(USART1, USART_INT_RXDNE) != RESET)
        gsm.usart.receiveIntHandler((uint8_t)USART_ReceiveData(USART1));
    if (USART_GetIntStatus(USART1, USART_INT_TXDE) != RESET)
        gsm.usart.transmitNextByte();
}

//-----------------------------------------------------
Gsm::Gsm(void)
{
    mobileOperator = OPERATOR_MTS;//OPERATOR_MTS;
}
//-----------------------------------------------------
void Gsm::initialize(void)
{
    usart.initialize(1, 115200); // USART1 — A7682E default baud
    WakeupPin.Initialize(GPIOA, GPIO_PIN_0, GPIO_Mode_IN_FLOATING);
    PowergoodPin.Initialize(GPIOA, GPIO_PIN_3, GPIO_Mode_IPU);
    RingPin.Initialize(GPIOB, GPIO_PIN_0, GPIO_Mode_IN_FLOATING);
    DTRPin.Initialize(GPIOB, GPIO_PIN_1, GPIO_Mode_Out_PP);
    DTRPin.Reset();
    
    // Configure the pwrkey pin
    if (versionHardware == 1) {
        PowerkeyPin.Initialize(GPIOB, GPIO_PIN_3, GPIO_Mode_Out_PP);
    } else {
        PowerkeyPin.Initialize(GPIOB, GPIO_PIN_7, GPIO_Mode_Out_PP);
    }
    PowerkeyPin.Reset();
    
    dtmfMode = 1;
    bufferCursorR = 0;
}
//-----------------------------------------------------

//-----------------------------------------------------
void Gsm::handler(void)
{
    uint16_t res;

    if (bridgeMode) {
        processReceivedData();      /* modem → USB (drain RX buffer)       */

        /* TX: send buffered bytes (filled by USB ISR) when UART is free   */
        if (bridgeTxLen > 0 && !usart.isTransmission) {
            uint8_t len = (uint8_t)bridgeTxLen;
            bridgeTxLen = 0;        /* clear before read to avoid race     */
            /* local echo → USB VCP (modem has ATE0, echo is off)          */
            for (uint8_t i = 0; i < len; i++)
                USART_To_USB_Send_Data((char)bridgeTxBuf[i]);
            /* forward to modem via interrupt-driven Usart_C               */
            uint8_t tmp[BRIDGE_TX_MAX];
            for (uint8_t i = 0; i < len; i++) tmp[i] = bridgeTxBuf[i];
            usart.send(tmp, len);
        }
        return;
    }

    inputRing = RingPin.Get();
    inputWakeup = WakeupPin.Get();
    inputPowerGood = PowergoodPin.Get();

    if (inputPowerGood == false) return;
    
    processReceivedData();
    res = parsing();

    if (res){
        isAnswer = true;
        answer |= (uint32_t)1<<res;
        
        if (process != PROCESS_ANSWER_RING){

            if (answer & ANSWER_CMTI){
                /*if (answerDataPoint > 0){
                    if (answerData[0] >= '1' && answerData[0] <= '9'){
                        numberSms = answerData[0]-'0';
                    }
                    else{
                        numberSms = 1;
                    }
                }
                else numberSms = 1;*/
            }
            
        }
        
    }
    
    if (gsm.numberSms > 0 &&
        step != MODE_WORK_STEP_READ_SMS &&
        process >= PROCESS_INIT_GSM && process <= PROCESS_EMPTY){
        stepOld = step;
        step = MODE_WORK_STEP_READ_SMS;
        changeProcess(gsm.PROCESS_READ_SMS); // PROCESS_TRANSMIT_GPRS
    }

    /* ── Incoming call: log caller ID (set by CLIP parser in parsing()) ── */
    if (gsm.isRing) {
        gsm.isRing = false;
        log_info("[RING] from: ");
        if (gsm.phoneRing[0]) {
            char tmp[16] = {0};
            for (int i = 0; i < 15 && gsm.phoneRing[i]; i++) tmp[i] = gsm.phoneRing[i];
            log_info(tmp);
        } else {
            log_info("unknown");
        }
        log_info("\r\n");
    }

    switch (process){
        case PROCESS_POWER_ON:
            processPowerOn();
            break;
        case PROCESS_POWER_OFF:
            processPowerOff();
            break;
        case PROCESS_SLEEP_ON:
            processSleepOn();
            break;
        case PROCESS_SLEEP_OFF:
            processSleepOff();
            break;
        case PROCESS_WAIT_READY:
            processWaitReady();
            break;
        case PROCESS_INIT_GSM:
            processInitGsm();
            break;
        case PROCESS_REQUEST_BALANCE:
            processRequestBalance();
            break;
        case PROCESS_REQUEST_CSQ:
            processRequestCsq();
            break;
        case PROCESS_REQUEST_CREG:
            processRequestCreg();
            break;
        case PROCESS_ANSWER_RING:
            processAnswerRing();
            break;
        case PROCESS_SEND_SMS_ENGLISH:
            sms.processSendSmsEnglish();
            break;
        case PROCESS_SEND_SMS_RUSSIAN:
            sms.processSendSmsRussian();
            break;
        case PROCESS_READ_SMS:
            sms.processReadSms();
            break;
        case PROCESS_EMPTY:
            
            break;
    } 
}
//-------------------------------------------------------------------
void Gsm::processReceivedData(void)
{
    uint8_t AByte;

    while(bufferCursorR != usart.getBufferPos()) {
        AByte = usart.getByte(bufferCursorR);
        receiptNextByte(AByte);
        bufferCursorR++;
        if (bufferCursorR >= usart.BUFFER_SIZE) bufferCursorR = 0;
    }
}
//-----------------------------------------------------
void Gsm::changeProcess(ProcessTypeDef number)
{
    process = number;
    mode = 0;
    error = ERROR_EMPTY;
    isAnswer = false;
    answer   = 0;
}
//-----------------------------------------------------
void Gsm::processPowerOff(void)
{
    static uint32_t timer = 0;
    
    switch(mode){
    case 0:
        if (sendMessage("AT\r\n", 1000)){
        
            if (answer & ANSWER_TIMEOUT){
                mode = 3;
                break;
            }
            if (answer & ANSWER_ERROR){
                mode++;
                break;
            }
            if (answer & ANSWER_OK){
                mode++;
                break;
            }
        
        }
        break;
    case 1:
        PowerkeyPin.Set();
        timer = core.getTick();
        mode++;
        break;
    case 2:
        if ((core.getTick() - timer) >= 4500){
            PowerkeyPin.Reset();
            mode++;
        }
        break;
    default:
        changeProcess(PROCESS_EMPTY);
        break;
    }
}
//-----------------------------------------------------
void Gsm::processSleepOn(void)
{
    switch(mode){
    case 0:
        DTRPin.Set();   /* DTR HIGH — разрешаем модему войти в режим сна */
        mode++;
        break;
    case 1:
        if (sendMessage("AT+CSCLK=1\r\n", 1000)){
            if (answer & ANSWER_TIMEOUT){ mode++; break; }
            if (answer & ANSWER_ERROR)  { mode++; break; }
            if (answer & ANSWER_OK)     { mode++; break; }
        }
        break;
    default:
        changeProcess(PROCESS_EMPTY);
        break;
    }
}
//-----------------------------------------------------
void Gsm::processSleepOff(void)
{
    static uint32_t timer = 0;
    switch(mode){
    case 0:
        DTRPin.Reset();         /* DTR LOW — будим модем аппаратно       */
        timer = core.getTick();
        mode++;
        break;
    case 1:
        /* Ждём 100 мс — модем поднимается после фронта DTR             */
        if ((core.getTick() - timer) >= 100UL) mode++;
        break;
    case 2:
        if (sendMessage("AT+CSCLK=0\r\n", 1000)){
            if (answer & ANSWER_TIMEOUT){ mode++; break; }
            if (answer & ANSWER_ERROR)  { mode++; break; }
            if (answer & ANSWER_OK)     { mode++; break; }
        }
        break;
    default:
        changeProcess(PROCESS_EMPTY);
        break;
    }
}
//-----------------------------------------------------
void Gsm::processPowerOn(void)
{
    static uint32_t timer = 0;
    static bool isFirst = true;
    
    if (isFirst == true){
        isFirst = false;
        #ifdef IS_PLATE_MODEM
        GPIO_SetBits(GPIOC, GPIO_Pin_1);
        #endif
        PowerkeyPin.Set();
        timer = core.getTick();
        //led.modeGsm = LED_MODE_GSM_BLINK_MAX;
        log_gsm("\r\nGSM power on\r\n");
    }
    if ((core.getTick() - timer) > 4500){ // 1500
        led.modeGsm = LED_MODE_GSM_ON;
        PowerkeyPin.Reset();
    }
    if ((core.getTick() - timer) > 5000){ // 2000
        isFirst = true;
        changeProcess(PROCESS_WAIT_READY);
    }
}
//-----------------------------------------------------
void Gsm::processWaitReady(void)
{
    static uint32_t timer = 0, timerSec = 0;
    static bool isFirst = true;
    
    if (isFirst == true){
        isFirst = false;
        timer = core.getTick();
        log_gsm("GSM wait ready");
    }
    
    if ((core.getTick() - timerSec) >= 1000){
        timerSec = core.getTick();
        log_gsm(".");
    }
    
    if ((core.getTick() - timer) >= 10000){
        isFirst = true;
        changeProcess(PROCESS_INIT_GSM);
        log_gsm("\r\n");
    }

    if (answer & ANSWER_CALL_READY){
        changeProcess(PROCESS_INIT_GSM);
        log_gsm("OK\r\n");
    }

}
//-----------------------------------------------------
void Gsm::processInitGsm(void)
{
    static uint32_t timer=0;
    uint8_t a;
    bool res;
    
    switch(mode){
    case 0:
        if (sendMessage("AT\r\n", 3000)){
            setLowPower(false);
            if (answer & ANSWER_TIMEOUT){
                mode++;//error = ERROR_TIMEOUT;
                break;
            }
            if (answer & ANSWER_ERROR){
                error = ERROR_ANSWER;
                break;
            }
            if (answer & ANSWER_OK){
                mode = 2;   /* skip baud-rate negotiation — fixed at 115200 */
                timer = core.getTick();
                log_gsm("GSM initialize.");
                break;
            }

        }
        break;

    case 1:
        /* Baud-rate negotiation removed — modem is fixed at 115200.
           Fall straight through to case 2. */
        mode = 2;
        break;
        
    case 2:
        numSim = NUMSIM_SEARCH;
        if ((core.getTick()-timer) > 3000){
            if (sendMessage("AT+BINDSIM=0\r\n", 10000)){
                // external SIM
                if (answer & ANSWER_TIMEOUT){
                    mode++;//error = ERROR_TIMEOUT;
                    log_gsm(".");
                    break;
                }
                if (answer & ANSWER_ERROR){
                    mode++;//error = ERROR_ANSWER;
                    log_gsm(".");
                    break;
                }
                if (answer & ANSWER_OK){
                    mode++;
                    log_gsm(".");
                    break;
                }
            
            }
        }
        break;
        
    case 3:
        if (sendMessage("AT+SWITCHSIM=0\r\n", 10000)){
            // external SIM
            if (answer & ANSWER_TIMEOUT){
                mode++;//error = ERROR_TIMEOUT;
                log_gsm(".");
                break;
            }
            if (answer & ANSWER_ERROR){
                mode++;//error = ERROR_ANSWER;
                log_gsm(".");
                break;
            }
            if (answer & ANSWER_OK){
                mode++;
                log_gsm(".");
                break;
            }
        
        }
        break;
        
    case 4:
        if (sendMessage("AT+COPS?\r\n", 3000)){
            if (answer & ANSWER_TIMEOUT){
                mode = 8;///mode++;
                log_gsm(".");
                break;
            }
            if (answer & ANSWER_ERROR){
                mode = 8;///mode++;
                log_gsm(".");
                break;
            }
            if (answer & ANSWER_OK){
                ///mode++;
                //break;
            }
            if (answer & ANSWER_COPS){
                if (gsm.answerDataPoint > 0){
                    if (Convert.compareStr((char *)answerData, "25002")) {   // MegaFon
                        gsm.mobileOperator = OPERATOR_MEGAFON;
                    }
                    else if (Convert.compareStr((char *)answerData, "25099")) {  // Beeline
                        gsm.mobileOperator = OPERATOR_BEELINE;
                    }
                    else if (Convert.compareStr((char *)answerData, "25001")) {      // MTS
                        gsm.mobileOperator = OPERATOR_MTS;
                    }
                }
                numSim = NUMSIM_EXTERNAL;
                mode = 9;
                log_gsm(".");
                break;
            }
        
        }
        break;
        
    case 5:
        if ((core.getTick()-timer) > 3000){
            if (sendMessage("AT+BINDSIM=1\r\n", 10000)){
                // external SIM
                if (answer & ANSWER_TIMEOUT){
                    mode++;//error = ERROR_TIMEOUT;
                    log_gsm(".");
                    break;
                }
                if (answer & ANSWER_ERROR){
                    mode++;//error = ERROR_ANSWER;
                    log_gsm(".");
                    break;
                }
                if (answer & ANSWER_OK){
                    mode++;
                    log_gsm(".");
                    break;
                }
            
            }
        }
        break;
        
    case 6:
        if (sendMessage("AT+SWITCHSIM=1\r\n", 10000)){
            // external SIM
            if (answer & ANSWER_TIMEOUT){
                mode++;//error = ERROR_TIMEOUT;
                log_gsm(".");
                break;
            }
            if (answer & ANSWER_ERROR){
                mode++;//error = ERROR_ANSWER;
                log_gsm(".");
                break;
            }
            if (answer & ANSWER_OK){
                mode++;
                log_gsm(".");
                break;
            }
        
        }
        break;
        
    case 7:
        if (sendMessage("AT+COPS?\r\n", 3000)){
            if (answer & ANSWER_TIMEOUT){
                mode = 8; // try with select external SIM card
                numSim = NUMSIM_NO;
                log_gsm(".");
                break;
            }
            if (answer & ANSWER_ERROR){
                mode = 8; // try with select external SIM card
                numSim = NUMSIM_NO;
                log_gsm(".");
                break;
            }
            if (answer & ANSWER_OK){
                ///mode++;
                //break;
            }
            if (answer & ANSWER_COPS){
                if (gsm.answerDataPoint > 0){
                    if (Convert.compareStr((char *)answerData, "25002")) {   // MegaFon
                        gsm.mobileOperator = OPERATOR_MEGAFON;
                    }
                    else if (Convert.compareStr((char *)answerData, "25099")) {  // Beeline
                        gsm.mobileOperator = OPERATOR_BEELINE;
                    }
                    else if (Convert.compareStr((char *)answerData, "25001")) {      // MTS
                        gsm.mobileOperator = OPERATOR_MTS;
                    }
                }
                numSim = NUMSIM_INTERNAL;
                mode = 10;
                log_gsm(".");
                break;
            }
        
        }
        break;
        
    case 8:
        if ((core.getTick()-timer) > 3000){
            if (sendMessage("AT+BINDSIM=0\r\n", 10000)){
                // external SIM
                if (answer & ANSWER_TIMEOUT){
                    mode++;//error = ERROR_TIMEOUT;
                    log_gsm(".");
                    break;
                }
                if (answer & ANSWER_ERROR){
                    mode++;//error = ERROR_ANSWER;
                    log_gsm(".");
                    break;
                }
                if (answer & ANSWER_OK){
                    mode++;
                    log_gsm(".");
                    break;
                }
            
            }
        }
        break;
        
    case 9:
        if (sendMessage("AT+SWITCHSIM=0\r\n", 10000)){
            // select external SIM card
            if (answer & ANSWER_TIMEOUT){
                mode = 11;//error = ERROR_TIMEOUT;
                log_gsm(".");
                break;
            }
            if (answer & ANSWER_ERROR){
                mode = 11;//error = ERROR_ANSWER;
                log_gsm(".");
                break;
            }
            if (answer & ANSWER_OK){
                mode = 11;
                log_gsm(".");
                break;
            }
        
        }
        break;
        
    case 10:
        if (sendMessage("AT+SWITCHSIM=1\r\n", 10000)){
            // select internal SIM card (E-SIM)
            if (answer & ANSWER_TIMEOUT){
                mode = 11;//error = ERROR_TIMEOUT;
                log_gsm(".");
                break;
            }
            if (answer & ANSWER_ERROR){
                mode = 11;//error = ERROR_ANSWER;
                log_gsm(".");
                break;
            }
            if (answer & ANSWER_OK){
                mode = 11;
                log_gsm(".");
                break;
            }
        
        }
        break;
        
        
    case 11:
        if (sendMessage("AT+CGMR\r\n", 3000)){
        
            if (answer & ANSWER_CGMR){
                if (gsm.answerDataPoint > 0){
                    for (int i=0; i<31; i++){
                        cgmr[i] = gsm.answerData[i];
                    }
                    cgmr[31] = 0;
                    mode++;
                    log_gsm(".");
                    break;
                }
            }
            if (answer & ANSWER_TIMEOUT){
                mode++;//error = ERROR_TIMEOUT;
                log_gsm(".");
                break;
            }
            if (answer & ANSWER_ERROR){
                mode++;//error = ERROR_ANSWER;
                log_gsm(".");
                break;
            }
            if (answer & ANSWER_OK){
                mode++;
                log_gsm(".");
                break;
            }
            
        
        }
        break;
            
    case 12:
            gsm.mode++;
            /*if (gsm.sendMessage("ATZ\r\n", 3000)){
                if (gsm.answer & gsm.ANSWER_TIMEOUT){
                    gsm.mode++;
                    break;
                }
                if (gsm.answer & gsm.ANSWER_ERROR){
                    gsm.mode++;
                    break;
                }
                if (gsm.answer & gsm.ANSWER_OK){
                    gsm.mode++;
                    break;
                }
            }*/
        break;
        
    case 13:
        if (sendMessage("ATE0\r\n", 300)){  //))// ATE0
        
            if (answer & ANSWER_TIMEOUT){
                error = ERROR_TIMEOUT;
                break;
            }
            if (answer & ANSWER_ERROR){
                error = ERROR_ANSWER;
                break;
            }
            if (answer & ANSWER_OK){
                mode++;
                log_gsm(".");
                break;
            }
        
        }
        break;
    case 14:
        if (sendMessage("AT+CMEE=2\r\n", 300)){
        
            if (answer & ANSWER_TIMEOUT){
                error = ERROR_TIMEOUT;
                break;
            }
            if (answer & ANSWER_ERROR){
                error = ERROR_ANSWER;
                break;
            }
            if (answer & ANSWER_OK){
                mode++;
                log_gsm(".");
                break;
            }
        
        }
        break;
    case 15:
        if (sendMessage("AT+CMGF=1\r\n", 300)){
        
            if (answer & ANSWER_TIMEOUT){
                mode++;
                timer = core.getTick();
                log_gsm(".");
                //!!//error = ERROR_TIMEOUT;
                break;
            }
            if (answer & ANSWER_ERROR){
                error = ERROR_ANSWER;
                break;
            }
            if (answer & ANSWER_OK){
                mode++;
                timer = core.getTick();
                log_gsm(".");
                break;
            }
        
        }
        break;
        
    case 16:
        if (core.getTick() > 160000) mode++;
        else{
            if ((core.getTick()-timer) > 6000){
                if (sendMessage("AT+CMGD=,4\r\n", 5000)){
            
                    if (answer & ANSWER_TIMEOUT){
                        mode++;//error = ERROR_TIMEOUT;
                        log_gsm(".");
                        break;
                    }
                    if (answer & ANSWER_ERROR){
                        mode++;//error = ERROR_ANSWER;
                        log_gsm(".");
                        break;
                    }
                    if (answer & ANSWER_OK){
                        if (numSim == NUMSIM_NO) numSim = NUMSIM_EXTERNAL;
                        mode++;
                        log_gsm(".");
                        break;
                    }
                
                }
            }
        }
        break;
    case 17:
        if (sendMessage("AT+CLIP=1\r\n", 300)){
    
            if (answer & ANSWER_TIMEOUT){
                mode++;
                log_gsm(".");
                break;
            }
            if (answer & ANSWER_ERROR){
                mode++;
                log_gsm(".");
                break;
            }
            if (answer & ANSWER_OK){
                mode++;
                log_gsm(".");
                break;
            }
        
        }
        break;
    case 18:
        if (sendMessage("AT+CSQ\r\n", 300)){
    
            if (answer & ANSWER_TIMEOUT){
                mode++;   /* don't get stuck — advance even without CSQ */
                log_gsm(".");
                break;
            }
            if (answer & ANSWER_ERROR){
                mode++;
                log_gsm(".");
                break;
            }
            if (answer & ANSWER_CSQ){
                timer = core.getTick();
                mode++;
                log_gsm(".");
                break;
            }
        
        }
        break;
        
    case 19:
        if ((core.getTick()-timer) > 1000){
            error = ERROR_TIMEOUT;
            break;
        }
        if (gsm.answerDataPoint > 0){
            answer &= ~ANSWER_CSQ;
            levelGsm = Convert.strToInt((char*) gsm.answerData);
            mode++;
            log_gsm(".");
            break;
        }
        break;
        
    case 20:
        timer = core.getTick();
        if (sendMessage("AT+CICCID\r\n", 9000)){
        
            if (answer & ANSWER_ICCID){
                if (gsm.answerDataPoint > 0){
                    for (int i=0; i<20; i++){
                        iccid[i] = gsm.answerData[i];
                    }
                    iccid[20] = 0;
                    mode++;
                    log_gsm(".");
//                    break;
                }
            }
            if (answer & ANSWER_TIMEOUT){
                mode++;//error = ERROR_TIMEOUT;
                log_gsm(".");
                break;
            }
            if (answer & ANSWER_ERROR){
                mode++;//error = ERROR_ANSWER;
                log_gsm(".");
                break;
            }
            if (answer & ANSWER_OK){
                mode++;
                log_gsm(".");
                break;
            }
        }
        break;
        
    case 21:
        if (sendMessage("AT+CGSN\r\n", 500)){
            if ((core.getTick()-timer) > 1000 && !isReadImei){
                isReadImei = true;
                gsm.answerDataPoint = 0;
                //answer = 0;
            }
            if ((core.getTick()-timer) > 2000){
                if (answer & ANSWER_TIMEOUT){
                    uint8_t j = 0;
                    //if (gsm.answerDataPoint >= 15){
                        for (int i=0; i<20; i++){
                            if (gsm.answerData[i] >= '0' && gsm.answerData[i] <= '9'){
                                imei[j++] = gsm.answerData[i];
                                if (j >= 15) break;
                            }
                        }
                        imei[15] = 0;
                        mode++;
                        log_gsm(".");
                    //}
                    isReadImei = false;
                    break;
                }
            }   
        }
        break;
    case 22:
        if (sendMessage("AT+CNUM\r\n", 300)){
        
            if (answer & ANSWER_CNUM){
                if (gsm.answerDataPoint > 0){
                    a=0;
                    res = false;
                    for (int i=0; i<64; i++){
                        if (gsm.answerData[i] == '+') res = true;
                        if (res && gsm.answerData[i] == '\"') break;
                        if (res) phoneSim[a++] = gsm.answerData[i];
                        if (a >= 15) break;
                    }
                    phoneSim[a++] = 0;
                    mode++;
                    log_gsm(".");
                    break;
                }
            }
            if (answer & ANSWER_TIMEOUT){
                mode++;//error = ERROR_TIMEOUT;
                log_gsm(".");
                break;
            }
            if (answer & ANSWER_ERROR){
                mode++;//error = ERROR_ANSWER;
                log_gsm(".");
                break;
            }
            if (answer & ANSWER_OK){
                mode++;
                log_gsm(".");
                break;
            }
        
        }
        break;
        
    case 23:
        /* Enable new-SMS notifications: modem will send +CMTI: "SM",N */
        if (sendMessage("AT+CNMI=2,1,0,0,0\r\n", 300)){
            if (answer & ANSWER_TIMEOUT){ mode++; log_gsm("."); break; }
            if (answer & ANSWER_ERROR)  { mode++; log_gsm("."); break; }
            if (answer & ANSWER_OK)     { mode++; log_gsm("."); break; }
        }
        break;

    default:
        led.modeGsm = LED_MODE_GSM_BLINK_SLOW;
        changeProcess(PROCESS_EMPTY);
        log_gsm(". OK\r\n");
        log_gsm("SIM number: ");
        if (phoneSim[0] == 0){
            log_gsm("unknown");
        }
        else{
            log_gsm(phoneSim);
        }
        log_gsm("\r\n");
        break;
    }
}
//-----------------------------------------------------
void Gsm::processRequestBalance(void)
{
    #define BALANCE_MSG_LEN 32
    char balance_msg[BALANCE_MSG_LEN] = {0};
    
    switch(mode){
        
    case 0:
        setLowPower(false);
        gsm.mode++;
    
    case 1:
        if (gsm.sendMessage("AT+CSCLK=0\r\n", 750)){
        
            if (gsm.answer & gsm.ANSWER_TIMEOUT){
                //??//error = ERROR_TIMEOUT;
                gsm.mode++;
                break;
            }
            if (gsm.answer & gsm.ANSWER_ERROR){
                gsm.error = gsm.ERROR_ANSWER;
                gsm.mode++;
                break;
            }
            if (gsm.answer & gsm.ANSWER_OK){
                gsm.mode++;
            }
        
        }
        break;
        
    case 2:
        mode++;
        log_gsm("Request balance\r\n");
        /*if (sendMessage("AT+CGATT=0\r\n", 75000)){
            
            if (answer & ANSWER_TIMEOUT){
                error = ERROR_TIMEOUT;
            }
            if (answer & ANSWER_ERROR){
                error = ERROR_ANSWER;
            }
            if (answer & ANSWER_OK){
                mode++;
            }
        
        }*/
        break;
    case 3:
        if (sendMessage("AT+CSCS=\"IRA\"\r\n", 300)){   // GSM, UCS2
        
            if (answer & ANSWER_TIMEOUT){
                error = ERROR_TIMEOUT;
            }
            if (answer & ANSWER_ERROR){
                error = ERROR_ANSWER;
            }
            if (answer & ANSWER_OK){
                mode++;
            }
        
        }
        break;
    case 4:
                
        if (phoneBalance[0] == '*' || phoneBalance[0] == '#'){
            uint8_t n = Convert.strToStr("AT+CUSD=1,\"", balance_msg);
            n += Convert.strToStr(phoneBalance, &balance_msg[n]);
            n += Convert.strToStr("\",15\r\n", &balance_msg[n]);
        }
        else{
            switch (mobileOperator) {
                case OPERATOR_MEGAFON: 
                    strncpy(balance_msg, "AT+CUSD=1,*100#,15\r\n", BALANCE_MSG_LEN - 2);
                    break;
                case OPERATOR_BEELINE: 
                    Convert.strToStr("AT+CUSD=1,\"*102#\",15\r\n", balance_msg);
                    break;
                case OPERATOR_MTS: 
                    strncpy(balance_msg, "AT+CUSD=1,\"*100#\",15\r\n", BALANCE_MSG_LEN);
                    break;
                default:
                    strncpy(balance_msg, "AT+CUSD=1,\"*100#\",15\r\n", BALANCE_MSG_LEN);
            }
        }
        
        if (sendMessage(balance_msg, 10000)){
            
            if (answer & ANSWER_TIMEOUT){
                error = ERROR_TIMEOUT;
            }
            if (answer & ANSWER_ERROR){
                error = ERROR_ANSWER;
            }
            if (answer & ANSWER_OK){
                ///mode++;
            }
            if (answer & ANSWER_CUSD){
                mode++;
            }
        
        }
        break;
    default:
        changeProcess(PROCESS_EMPTY);
        break;
    }
}
//-----------------------------------------------------
void Gsm::processRequestCsq(void)
{
    static uint32_t timer = 0;
    switch(mode){
    case 0:
        setLowPower(false);
        mode++;
        break;
    case 1:
        if (sendMessage("AT+CSQ\r\n", 1000)){
            if (answer & ANSWER_TIMEOUT){
                error = ERROR_TIMEOUT;
                mode = 99;
                break;
            }
            if (answer & ANSWER_ERROR){
                error = ERROR_ANSWER;
                mode = 99;
                break;
            }
            if (answer & ANSWER_CSQ){
                timer = core.getTick();
                mode++;
            }
        }
        break;
    case 2:
        /* Wait for answerData to be populated by the parser */
        if (answerDataPoint > 0){
            answer &= ~ANSWER_CSQ;
            levelGsm = Convert.strToInt((char*)answerData);
            mode = 99;
            break;
        }
        if ((core.getTick() - timer) > 500){
            mode = 99;  /* timeout — keep previous levelGsm */
        }
        break;
    default:
        changeProcess(PROCESS_EMPTY);
        break;
    }
}
//-----------------------------------------------------
void Gsm::processRequestCreg(void)
{
    static uint32_t timer = 0;
    switch(mode){
    case 0:
        mode++;
        break;
    case 1:
        if (sendMessage("AT+CREG?\r\n", 1000)){
            if (answer & ANSWER_TIMEOUT){
                mode = 99;
                break;
            }
            if (answer & ANSWER_ERROR){
                mode = 99;
                break;
            }
            if ((uint32_t)answer & ANSWER_CREG){
                timer = core.getTick();
                mode++;
            }
        }
        break;
    case 2:
        /* answerData: "1\r" (URC) or "0,1\r" (query) — find stat digit */
        if (answerDataPoint > 0){
            answer &= ~ANSWER_CREG;
            /* stat is the last digit 0-5 before \r */
            uint8_t stat = 0;
            for (int i = 0; i < answerDataPoint; i++){
                char c = (char)answerData[i];
                if (c >= '0' && c <= '5') stat = (uint8_t)(c - '0');
            }
            cregStat = stat;
            mode = 99;
            break;
        }
        if ((core.getTick() - timer) > 500){
            mode = 99;
        }
        break;
    default:
        changeProcess(PROCESS_EMPTY);
        break;
    }
}
//-----------------------------------------------------
void Gsm::processAnswerRing(void)
{
    static char str[255] = {0};
    uint8_t a;
    
    switch(mode){
    case 0:
        setLowPower(false);
        if (dtmfMode == -1){
            mode++;
        }
        else{
            mode = 2;
        }
        break;
    case 1:
        if (sendMessage("ATA\r\n", 1000)){
            if (answer & ANSWER_TIMEOUT){
                mode++;
                break;
            }
            if (answer & ANSWER_ERROR){
                mode++;
                break;
            }
            if (answer & ANSWER_OK){
                mode++;
                break;
            }
        }
        break;
    case 2:
        if (dtmfMode == -1){
            mode = 99;
        }
        else if (dtmfMode == -2){
            mode = 4;
            dtmfMode = -1;
        }
        else{
            a = 0;
            a += Convert.strToStr("AT+CCMXPLAY=\"c:/", &str[a]);
            a += Convert.intToStr(dtmfMode, &str[a]);
            a += Convert.strToStr(".amr\",1,0\r\n", &str[a]);
            mode++;
            dtmfMode = -1;
        }
        break;
    case 3:
        if (sendMessage(str, 1000)){
        
            if (answer & ANSWER_TIMEOUT){
                mode = 99;
                break;
            }
            if (answer & ANSWER_ERROR){
                mode = 99;
                break;
            }
            if (answer & ANSWER_OK){
                mode = 99;
                break;
            }
            if (answer & ANSWER_DTMF){
                mode = 99;
                break;
            }
        }
        break;
    case 4:
        if (sendMessage("AT+CVHU=0\r\n", 1000)){
            if (answer & ANSWER_TIMEOUT){
                 mode++;
                break;
            }
            if (answer & ANSWER_ERROR){
                mode++;
                break;
            }
            if (answer & ANSWER_OK){
                mode++;
                break;
            }
        }
        break;
    case 5:
        if (sendMessage("ATH\r\n", 1000)){
            if (answer & ANSWER_TIMEOUT){
                 mode++;
                break;
            }
            if (answer & ANSWER_ERROR){
                mode++;
                break;
            }
            if (answer & ANSWER_OK){
                mode++;
                break;
            }
        }
        break;
    default:
        changeProcess(PROCESS_EMPTY);
        break;
    }
}
//-----------------------------------------------------
bool Gsm::sendMessage(const char *transMessage, uint32_t duration)
{
    static uint32_t timerDuration=0, timer=0;
    //!!//static bool isFirst = true;
    uint32_t a;
    static uint32_t sum = 0;
    
    if ((core.getTick() - timer) >= 10){
        timer = core.getTick();
        a = 0;
        for (int i=0; i<1024; i++){
            a += transMessage[i];
            if (transMessage[i] == 0) break;
        }
        
        if (a != sum){
            sum = a;
            // ---
            timerDuration = core.getTick();
            //!!//isFirst = false;
            isAnswer = false;
            answer = 0; // ---
            answer |= ANSWER_WAIT;
            startTransmission(transMessage);
        }
        else{
            // ---
            if (isAnswer == true){
                // ---
                if ((core.getTick() - timerDuration) > duration){
                    // ---
                    sum = 0;//!!//isFirst = true;
                    answer |= ANSWER_TIMEOUT;
                    return true;
                }
                
                //!!//isFirst = true;
                return true;
            }
            else{
                // ---
                if ((core.getTick() - timerDuration) > duration){
                    // ---
                    sum = 0;//!!//isFirst = true;
                    answer |= ANSWER_TIMEOUT;
                    return true;
                }
            }
        }
    }
    return false;
}
//-----------------------------------------------------
//void Gsm::setPwrkey(bool state)
//{
//    if (state == true){
//        switch(versionHardware){
//            case 1:
//                GPIO_SetBits(PWRKEY_V1_PORT, PWRKEY_V1_PIN);
//                break;
//            case 2:
//                GPIO_SetBits(PWRKEY_V2_PORT, PWRKEY_V2_PIN);
//                break;
//            default:
//                GPIO_SetBits(PWRKEY_V2_PORT, PWRKEY_V2_PIN);
//        }
//    }
//    else{
//        switch(versionHardware){
//            case 1:
//                GPIO_ResetBits(PWRKEY_V1_PORT, PWRKEY_V1_PIN);
//                break;
//            case 2:
//                GPIO_ResetBits(PWRKEY_V2_PORT, PWRKEY_V2_PIN);
//                break;
//            default:
//                GPIO_ResetBits(PWRKEY_V2_PORT, PWRKEY_V2_PIN);
//        }
//    }
//}
//-----------------------------------------------------
//void Gsm::setDtr(bool state)
//{
//    if (state == true){
//        GPIO_SetBits(GPIOB, GPIO_PIN_1);
//    }
//    else{
//        GPIO_ResetBits(GPIOB, GPIO_PIN_1);
//    }
//}
//-----------------------------------------------------
uint16_t Gsm::parsing(void)
{
    static uint16_t posY = 0;   //      GSM 
    static uint8_t posX = 0;    //      GSM 
    static uint16_t pointTemp = 0;  //   GSM   ()
    static uint16_t point = 0;  //   GSM   
    uint16_t res = 0;
    static uint16_t answerLast = 0;
    static bool isFirst = false;
    static char answerCodeStr[8];
    static char answerCodeStrPoint = 0;
    static uint16_t headLength = 0;
    
    while (posY < ANSWER_STRINGS_MAX)
    {
        if (pointTemp == receivePoint){
            return 0;    //   ...
        }
        if (ANSWER_STRINGS[posY][posX] == receiveArray[pointTemp]){
            posX++;
            pointTemp++;
            
            if (pointTemp > (RECEIVE_ARRAY_MAX-1)) pointTemp -= RECEIVE_ARRAY_MAX;
            if (ANSWER_STRINGS[posY] [posX] == 0){
                posX = 0;
                point = pointTemp;
                res = posY + 2;    // ---
                posY = 0;
            }
            if (res > 3){
                answerLast = res;
                isFirst = true;
            }
            if ((1<<res) & ANSWER_CMTI){
                if (receiveArray[pointTemp] >= '1' && receiveArray[pointTemp] <= '9'){
                    numberSms = receiveArray[pointTemp]-'0';
                }
                else{
                    numberSms = 1;
                }
            }
            return res;    //  ,   
        }
        
        pointTemp = point;
        posX = 0;
        posY++;
    }
    // ---
    if (((uint32_t)1<<answerLast) & ANSWER_CSQ
        || ((uint32_t)1<<answerLast) & ANSWER_ICCID
        || ((uint32_t)1<<answerLast) & ANSWER_CGMR
        || ((uint32_t)1<<answerLast) & ANSWER_CNUM
        || ((uint32_t)1<<answerLast) & ANSWER_CMTI
        || ((uint32_t)1<<answerLast) & ANSWER_CMGR
        || ((uint32_t)1<<answerLast) & ANSWER_CREG){
        if (isFirst){
            isFirst = false;
            answerDataPoint = 0;
            for (int i=0; i<ANSWER_ARRAY_MAX; i++) answerData[i] = 0;
        }
        if (answerDataPoint < ANSWER_ARRAY_MAX){
            answerData[answerDataPoint++] = receiveArray[pointTemp];
        }
    }
    else if (((uint32_t)1<<answerLast) & ANSWER_DTMF){          // DTMF
        // ---
        if (isFirst){
            isFirst = false;
            answerDataPoint = 0;
            for (int i=0; i<ANSWER_ARRAY_MAX; i++) answerData[i] = 0;
            isReadJson = true;
        }
        if (isReadJson){
            if (answerDataPoint < ANSWER_ARRAY_MAX){
                answerData[answerDataPoint++] = receiveArray[pointTemp];
                if (answerDataPoint > 1){
                    dtmfChar = answerData[0];
                    isReadJson = false;
                }
            }
        }
    }
    else if (((uint32_t)1<<answerLast) & ANSWER_FTP){
        // ---
        if (isFirst){
            isFirst = false;
            answerDataPoint = 0;
            for (int i=0; i<ANSWER_ARRAY_MAX; i++) answerData[i] = 0;
        }
        if (answerDataPoint < ANSWER_ARRAY_MAX){
            answerData[answerDataPoint++] = receiveArray[pointTemp];
        }
    }
    else if (((uint32_t)1<<answerLast) & ANSWER_CUSD      // ---
        || ((uint32_t)1<<answerLast) & ANSWER_COPS){      // ---
        // ---
        if (isFirst){
            isFirst = false;
            answerDataPoint = 0;
            for (int i=0; i<ANSWER_ARRAY_MAX; i++) answerData[i] = 0;
            isReadJson = false;
        }
        if (isReadJson == false){
            if (receiveArray[pointTemp] == 34) isReadJson = true;
        }
        else{
            if (receiveArray[pointTemp] == 34){
                isReadJson = false;
                answerLast = 0;
            }
            else{
                if (answerDataPoint < ANSWER_ARRAY_MAX){
                    answerData[answerDataPoint++] = receiveArray[pointTemp];
                }
            }
        
        }
    }
    else if (((uint32_t)1<<answerLast) & ANSWER_CLIP){
        
        // ---
        if (isFirst){
            isFirst = false;
            answerDataPoint = 0;
            for (int i=0; i<ANSWER_ARRAY_MAX; i++) answerData[i] = 0;
            isReadJson = false;
        }
        if (isReadJson == false){
            if (receiveArray[pointTemp] == 34) isReadJson = true;
        }
        else{
            if (receiveArray[pointTemp] == 34){
                isReadJson = false;
                answerLast = 0;
                
                if (answerDataPoint >= 12){
                    
                    for (uint8_t i=0; i<12; i++){
                        if (i == answerDataPoint){
                            phoneRing[i] = 0;
                            break;
                        }
                        phoneRing[i] = answerData[i];
                    }
                    
                    bool isOk = false;
                    uint8_t len = 0;
                    for (uint8_t i=0; i<5; i++){
                        isOk = true;
                        len = 0;
                        for (uint8_t x=0; x<16; x++){
                            if (phones[i][x] == 0 || answerData[x] == 0) break;
                            if (phones[i][x] != answerData[x] && x >= 2) isOk = false;
                            len++;
                        }
                        if (isOk && len > 8){
                            posRingPhone = i;
                            break;
                        }
                        else isOk = false;
                    }
                    if (isOk && len > 8){
                        if (gsm.phones[0][0] == '+'){
                            isUnknownRing = false;
                            isRing = true;
                        }
                    }
                    else{
                        if (gsm.phones[0][0] == '+'){
                            isUnknownRing = true;
                            posRingPhone = 0;
                            isRing = true;
                        }
                        else{
                            isUnknownRing = true;
                            posRingPhone = 0;
                            isRing = true;
                        }
                    }
                }
                
            }
            else{
                if (answerDataPoint < ANSWER_ARRAY_MAX){
                    answerData[answerDataPoint++] = receiveArray[pointTemp];
                }
            }
        
        }
    }
    else if (((uint32_t)1<<answerLast) & ANSWER_HTTPACTION){
        //    ','
        if (isFirst){
            isFirst = false;
            answerCodeStrPoint = 0;
            for (int i=0; i<8; i++) answerCodeStr[i] = 0;
            isReadJson = false;
        }
        if (isReadJson == false){
            if (receiveArray[pointTemp] == ',') isReadJson = true;
        }
        else{
            if (receiveArray[pointTemp] == ','){
                isReadJson = false;
                answerLast = 0;
            }
            else{
                if (answerCodeStrPoint < 8){
                    answerCodeStr[answerCodeStrPoint++] = receiveArray[pointTemp];
                }
            }
        
        }
    }
    else if (((uint32_t)1<<answerLast) & ANSWER_HTTPREAD){   // ---
        //    "{}"
        if (isFirst){
            isFirst = false;
            answerDataPoint = 0;
            for (int i=0; i<ANSWER_ARRAY_MAX; i++) answerData[i] = 0;
            isReadJson = false;
        }
        if (isReadJson == false){
            if (receiveArray[pointTemp] == '{') isReadJson = true;
        }
        else{
            if (receiveArray[pointTemp] == 0x0d){//'}'
                isReadJson = false;
                answerLast = 0;
                answerLast = 0;
            }
            else{
                if (answerDataPoint < ANSWER_ARRAY_MAX){
                    answerData[answerDataPoint++] = receiveArray[pointTemp];
                }
            }

        }
    }

    else if (((uint32_t)1<<answerLast) & ANSWER_HTTPHEAD){   // ---
        //    " "
        if (isFirst){
            isFirst = false;
            answerDataPoint = 0;
            for (int i=0; i<ANSWER_ARRAY_MAX; i++) answerData[i] = 0;
            isReadJson = false;
            headLength = 0;
        }
        if (headLength == 0){
            if (isReadJson == false){
                if (receiveArray[pointTemp] == ' ') isReadJson = true;
            }
            else{
                if (receiveArray[pointTemp] == '\r' || 
                    receiveArray[pointTemp] == '\n' || 
                    answerDataPoint >= 3){
                    //isReadJson = false;
                    //answerLast = 0;
                    headLength = Convert.strToInt((char*) answerData);
                    answerDataPoint = 0;
                    for (int i=0; i<ANSWER_ARRAY_MAX; i++) answerData[i] = 0;
                }
                else{
                    if (answerDataPoint < ANSWER_ARRAY_MAX){
                        answerData[answerDataPoint++] = receiveArray[pointTemp];
                    }
                }
            
            }
        }
        else{
            if (answerDataPoint > headLength){
                isReadJson = false;
                answerLast = 0;
            }
            else{
                if (answerDataPoint < ANSWER_ARRAY_MAX){
                    answerData[answerDataPoint++] = receiveArray[pointTemp];
                }
            }
        }
    }
    else if (((uint32_t)1<<answerLast) & ANSWER_SOCKET){
        
        //    "{}"
        if (isFirst){
            isFirst = false;
            answerDataPoint = 0;
            for (int i=0; i<ANSWER_ARRAY_MAX; i++) answerData[i] = 0;
            isReadJson1 = false;
            isReadJson2 = false;
            isReadJson = false;
        }
        if (isReadJson == false){
            if (isReadJson1 == false){
                if (receiveArray[pointTemp] == '{') isReadJson1 = true;
            }
            else {
                if (receiveArray[pointTemp] == '\"') isReadJson = true;
                else isReadJson1 = false;
            }
        }
        else{
            if (isReadJson2 == false && receiveArray[pointTemp] == '}'){
                isReadJson2 = true;
            }
            else if (isReadJson2 == true && receiveArray[pointTemp] == '}'){
                isReadJson = false;
                answerLast = 0;
            }
            else{
                isReadJson2 = false;
                if (answerDataPoint < ANSWER_ARRAY_MAX){
                    answerData[answerDataPoint++] = receiveArray[pointTemp];
                }
            }
        
        }
    }
    else if (((uint32_t)1<<answerLast) & ANSWER_CLOSED){
       isConnectedSocket = false;
    }
    
    posY = 0;
    pointTemp++;
    if (pointTemp > (RECEIVE_ARRAY_MAX-1)) pointTemp -= RECEIVE_ARRAY_MAX;
    point = pointTemp;
    
    return 0;    // ---
}
//-----------------------------------------------------
void Gsm::startTransmission(const char *array)
{
    if (!usart.isTransmission && *array != 0){
        uint16_t dataLen = 0;
        for (uint16_t i=0; i<TRANS_ARRAY_MAX; i++){
            transArray[i] = *array++;
            dataLen++;
            if (transArray[i] == 0) break;
        }
        usart.send((uint8_t*)transArray, dataLen);
        log_at(">> ");
        log_at(transArray);
        
//        transPoint = 0;
//        transPoint++;
//        if (transArray[0] >= 'A' && transArray[0] <= 'Z'){
//            buffer[point++] = transArray[0]-('A'-'a');  //))// ---
//        }
//        else{
//            buffer[point++] = transArray[0];  //))// ---
//        }
//        if (point >= BUFFER_ARRAY_MAX) point = 0;  //))// ---
        #ifdef IS_LOG_GSM
        panel.startTransmission(transArray);
        #endif
    }
}
//-----------------------------------------------------
//void Gsm::transmitNextByte(void)
//{
//    if (transArray[transPoint] != 0){
//        USART_SendData(USART1, transArray[transPoint]);
//        //USART_To_USB_Send_Data(transArray[transPoint]);
// ---
//        char tmp[2] = {0,0};
//        tmp[0] = transArray[transPoint];
//        log_at(tmp);
//        if (transArray[transPoint] >= 'A' && transArray[transPoint] <= 'Z'){
//            buffer[point++] = transArray[transPoint]-('A'-'a');  //))// ---
//        }
//        else{
//            buffer[point++] = transArray[transPoint];  //))// ---
//        }
//        if (point >= BUFFER_ARRAY_MAX) point = 0;  //))// ---
// ---
//        transPoint++;
//    }
//    else{
//        isBusy = false;
//        USART_ConfigInt(USART1, USART_INT_TXC, DISABLE);
//    }
//}
//-----------------------------------------------------
void Gsm::receiptNextByte(uint8_t byte)
{
    if (bridgeMode) {
        /* Bridge mode: send raw byte directly to USB VCP */
        USART_To_USB_Send_Data((char)byte);
        return;
    }

    receiveArray[receivePoint++] = byte;
    if (receivePoint >= RECEIVE_ARRAY_MAX) receivePoint = 0;

    char tmp[2] = {(char)byte, 0};
    log_at(tmp);
}

//-----------------------------------------------------
//void Gsm::usartIrqHandler(void)
//{
//    uint8_t byte;
// ---
//    if (USART_GetIntStatus(USART1, USART_INT_RXDNE) != RESET){
//        ///USART_ClearITPendingBit(USART1, USART_INT_RXDNE);
//        byte = USART_ReceiveData(USART1);
//        gsm.receiptNextByte(byte);
//        //USART_To_USB_Send_Data(byte);
//    }
//    if (USART_GetIntStatus(USART1, USART_INT_TXC) != RESET){
//        USART_ClrIntPendingBit(USART1, USART_INT_TXC);
//        gsm.transmitNextByte();
//    }
// ---
//    volatile uint32_t data = USART1->DAT;
//    volatile uint32_t sr = USART1->STS;
//    volatile uint32_t res = sr+data; //dummy op to supress optimisation
//}
//-----------------------------------------------------
void Gsm::bufRevers(char* str)
{
    int i = 0;
    uint16_t temp = 0;
    int r = 0;

    while (str[i] != '\0') {
        i++;
    }
    i = i - 1;
    for (r = 0; r < i; i--, r++) {
        temp = str[i];
        str[i] = str[r];
        str[r] = temp;
    }
}
//-----------------------------------------------------
void Gsm::intToChar(char* str, int n)
{
    int i = 0;
    while (n > 9) {
        str[i++] = (n % 10) + '0';
        n = n / 10;
    }
    str[i++] = n + '0';
    str[i] = '\0';
    bufRevers(str);
}
//-----------------------------------------------------
