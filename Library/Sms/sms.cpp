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
#include "sms.h"
#include "timberline_sms.h"
#include "gsm.h"
#include "modem_handler.h"
#include "flash.h"
#include "unix_time.h"
#include <string.h>

#include "log.h"

Sms sms;

extern "C" void sms_emulate(const char* phone, const char* message)
{
    sms.parseMessage((char*)phone, (char*)message);
}

//-----------------------------------------------------
Sms::Sms(void)
{
    setFactory();
}
//-----------------------------------------------------
void Sms::setFactory(void)
{
    smsCmd.C = 1;  //          .
    smsCmd.E = 1;  //     ,      .
    smsCmd.F = 0;  //    :
                            // 0     ,
                            // 1  .
    smsCmd.J = 0;  //     / .
    smsCmd.H = 0;  // /    .
    smsCmd.I = 2;  // /  :
                            // 1  
                            // 2  
                            //   2.
    smsCmd.L = 1;  //    SMS (1  , 0  )
    smsCmd.N = 92; //           [80..95].
    smsCmd.P = 1;  //       .
    smsCmd.R = 40; //    [30..60].   40.
    smsCmd.S = 15; //     ,                W. 
                            //        1  30?.
                            //   15.
    smsCmd.T = 40; //    .       20  120 .   40 .
    smsCmd.W = 0;  //  :
                            // 0    ,
                            // 2     ,
                            // 3     .
                            //    :
                            // 1       
                            // 2    
                            // 3     () 
                            // 4   ,    .
                            //   4.
    smsCmd.e = 0;  //  :
                            // 0    ,
                            // 1    .
    smsCmd.p = 5;  //   .     0 (  )  9 (  ).
                            //   5.
    smsCmd.r = 0;  //       :
                            // 0    ,  
                            // 1  .
    smsCmd.s = 0;  //   :
                            // 0    , 
                            // 1  .
    smsCmd.t = 88; //           [20..95].   88.
    smsCmd.M = 0;  //     /   5    ,      .
}
//-----------------------------------------------------
void Sms::processSendSmsEnglish(void) {
    static uint32_t timer = 0;
    char str[255];
    
    switch (gsm.mode) {
        
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
            gsm.mode++;
            timerSendSms = core.getTick();
            log_gsm("\r\n");
            unixTime.getTimestamp(str);
            log_gsm(str);
            log_gsm("SEND SMS\r\n");
            log_gsm("Phone: ");
            log_gsm(smsMsg.phone);
            log_gsm("\r\n");
            log_gsm("Message: \"");
            if (smsMsg.message[0] == 'P' && 
                smsMsg.message[1] == 'i' && 
                smsMsg.message[2] == 'n'){
                    log_gsm("Pin: ******");
            }
            else{
                log_gsm(smsMsg.message);
            }
            log_gsm("\"\r\n");
            /*if (gsm.sendMessage("AT+CNMI?\r\n", 3000)){
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
        case 3:
            if (gsm.sendMessage("AT+CSCS=\"IRA\"\r\n", 300)){
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
            }
            break;
        case 4:
            if (gsm.sendMessage("AT+CMGF=1\r\n", 1000)) {
                gsm.mode++;
            }
            break;
        case 5:
            if (gsm.sendMessage("AT+CMGSEX=\"", 1000)) {
                gsm.mode++;
            }
            break;
        case 6:
            if (gsm.sendMessage(smsMsg.phone, 1000)) {
                gsm.mode++;
            }
            break;
        case 7:
            if (gsm.sendMessage("\"\r\n", 1000)) {
                gsm.mode++;
                timer = core.getTick();
            }
            break;
        case 8:
            if ((core.getTick()-timer) > 1000){
                if (gsm.sendMessage(smsMsg.message, 1000)) {
                    gsm.mode++;
                }
            }
            break;
        case 9:
            ///if (gsm.sendMessage("\r\n", 1000)) {
                gsm.mode++;
            ///}
            break;
        case 10:
            if (gsm.sendMessage("\x1A", 10000)) {
                if (gsm.answer & gsm.ANSWER_TIMEOUT){
                    gsm.mode++;
                    timer = core.getTick();
                    break;
                }
                if (gsm.answer & gsm.ANSWER_ERROR){
                    gsm.mode++;
                    timer = core.getTick();
                    break;
                }
                if (gsm.answer & gsm.ANSWER_OK){
                    gsm.mode++;
                    timer = core.getTick();
                    isNeedToSendPinSms = false;
                    isNeedToSendSmsEnglish = false;
                    break;
                }
            }    
            break;
        case 11:
            if ((core.getTick()-timer) > 2000){
                gsm.mode++;
                /*if (gsm.sendMessage("AT+CNMP=2\r\n", 3000)){
                    if (gsm.answer & gsm.ANSWER_TIMEOUT){
                        timer = core.getTick();
                        gsm.mode++;
                        break;
                    }
                    if (gsm.answer & gsm.ANSWER_ERROR){
                        timer = core.getTick();
                        gsm.mode++;
                        break;
                    }
                    if (gsm.answer & gsm.ANSWER_OK){
                        timer = core.getTick();
                        gsm.mode++;
                        break;
                    }
                }*/
            }
            break;
            
        case 12:
            if ((core.getTick()-timer) > 10000){
                isNeedToSendPinSms = false;
                isNeedToSendSmsEnglish = false;
                gsm.mode++;
            }
            break;
            
        default:
            if (keyToNeedReset == 0xAA55){
                keyToNeedReset = 0;
                NVIC_SystemReset();
            }
            gsm.changeProcess(gsm.PROCESS_EMPTY);
            break;
    }
}
//-----------------------------------------------------
void Sms::processSendSmsRussian(void) {
    static uint32_t timer = 0;
    char str[255];
    
    switch (gsm.mode) {
        
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
            gsm.mode++;
            timerSendSms = core.getTick();
            log_gsm("\r\n");
            unixTime.getTimestamp(str);
            log_gsm(str);
            log_gsm("SEND SMS\r\n");
            log_gsm("Phone: ");
            log_gsm(gsm.phones[gsm.posRingPhone]);
            log_gsm("\r\n");
            log_gsm("Message: \"");
            log_gsm(smsMsg.message);
            log_gsm("\"\r\n");
            break;
        case 3:
            if (gsm.sendMessage("AT+CSCS=\"UCS2\"\r\n", 300)){
                gsm.mode++;
            }
            break;
        case 4:
            if (gsm.sendMessage("AT+CMGF=0\r\n", 1000)) {
                gsm.mode++;
            }
            break;
        case 5:
            if (gsm.sendMessage(smsMsg.phone, 1000)) {
                gsm.mode++;
            }
            break;
        case 6:
            if (gsm.sendMessage("\r\n", 1000)) {
                gsm.mode++;
            }
            break;
        case 7:
            if (gsm.sendMessage(smsMsg.message, 1000)) {
                gsm.mode++;
            }
            break;
        case 8:
            if (gsm.sendMessage("\x1A", 15000)) {
                
                if (gsm.answer & gsm.ANSWER_TIMEOUT){
                    //if (gsm.counterSendSms > 0){
                    //    gsm.counterSendSms--;
                    //    gsm.mode = 3;
                    //}
                    //else{
                        gsm.mode++;
                        break;
                    //}
                }
                if (gsm.answer & gsm.ANSWER_ERROR){
                    //if (gsm.counterSendSms > 0){
                    //    gsm.counterSendSms--;
                    //    gsm.mode = 3;
                    //    break;
                    //}
                    //else{
                        gsm.mode++;
                        timer = core.getTick();
                        break;
                    //}
                }
                if (gsm.answer & gsm.ANSWER_OK){
                    gsm.mode++;
                    timer = core.getTick();
                    isNeedToSendPinSms = false;
                    isNeedToSendSmsRussian = false;
                    break;
                }
                else if (gsm.answer & gsm.ANSWER_CMGS){
                    gsm.mode++;
                    timer = core.getTick();
                    break;
                }
                else if (gsm.answer & gsm.ANSWER_CMTI){
                    //mode++;
                }
            }    
            break;
        case 9:
            if ((core.getTick()-timer) > 2000){
                isNeedToSendPinSms = false;
                isNeedToSendSmsRussian = false;
                gsm.mode++;
            }
            break;
            
        default:
            if (keyToNeedReset == 0xAA55){
                keyToNeedReset = 0;
                NVIC_SystemReset();
            }
            gsm.changeProcess(gsm.PROCESS_EMPTY);
            break;
    }
}
//-----------------------------------------------------
void Sms::processReadSms(void) {
    static char cmgr[64] = "AT+CMGR=00\r\n";
    
    switch (gsm.mode) {
        
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
            if (gsm.sendMessage("AT+CSCS=\"GSM\"\r\n", 300)){
                gsm.mode++;
                break;
            }
            break;
        case 3:
            if (gsm.sendMessage("AT+CMGF=1\r\n", 1000)) {
                gsm.mode++;
            }
            break;
        case 4:
            Convert.intToStrFix(gsm.numberSms, 2, &cmgr[8]); // 00   
            gsm.mode++;
            gsm.answerDataPoint = 0;
            for (int i=0; i<gsm.ANSWER_ARRAY_MAX; i++) gsm.answerData[i] = 0;
            break;
        case 5:
            if (gsm.sendMessage(cmgr, 3000)) {

                if (gsm.answer & gsm.ANSWER_TIMEOUT){
                    gsm.numberSms = 0;
                    gsm.changeProcess(gsm.PROCESS_EMPTY);
                    break;
                }
                if (gsm.answer & gsm.ANSWER_ERROR){
                    gsm.numberSms = 0;
                    gsm.changeProcess(gsm.PROCESS_EMPTY);
                    break;
                }
                /* Wait for BOTH +CMGR header AND OK — OK arrives after the
                   full message body, so answerData is complete by then.     */
                if ((gsm.answer & gsm.ANSWER_CMGR) && (gsm.answer & gsm.ANSWER_OK)) {
                    gsm.numberSms = 0;

                    /* ── Null-terminate answerData ───────────────────────── */
                    uint16_t dLen = gsm.answerDataPoint;
                    if (dLen >= gsm.ANSWER_ARRAY_MAX)
                        dLen = gsm.ANSWER_ARRAY_MAX - 1;
                    gsm.answerData[dLen] = 0;

                    /* ── Log raw modem response (always visible) ─────────── */
                    log_info("[SMS] raw: \"");
                    log_info((const char*)gsm.answerData);
                    log_info("\"\r\n");

                    /* ── Extract phone: between 3rd and 4th quote ────────── */
                    /* Format after "+CMGR: ":
                     *   "REC UNREAD","+79XXXXXXXXX",,"date"\r\nBODY\r\n     */
                    char smsPhone[20] = {0};
                    char smsBody[160] = {0};
                    int  qc = 0, pi = 0;
                    for (int i = 0; i < (int)dLen && pi < 19; i++) {
                        char ch = (char)gsm.answerData[i];
                        if (ch == '"') { qc++; continue; }
                        if (qc == 3)  smsPhone[pi++] = ch;
                        else if (qc > 3) break;
                    }

                    /* ── Extract body: text after first '\n' in answerData ── */
                    int nlPos = -1;
                    for (int i = 0; i < (int)dLen; i++) {
                        if (gsm.answerData[i] == '\n') { nlPos = i + 1; break; }
                    }
                    if (nlPos >= 0) {
                        int bi = 0;
                        for (int i = nlPos; i < (int)dLen && bi < 159; i++) {
                            char ch = (char)gsm.answerData[i];
                            if (ch == '\r' || ch == '\n' || ch == 0) break;
                            smsBody[bi++] = ch;
                        }
                    }

                    parseMessage(smsPhone, smsBody);

                    gsm.mode++;
                    break;
                }
            }
            break;
        case 6:
        if (gsm.sendMessage("AT+CMGD=1,2\r\n", 5000)){
    
            if (gsm.answer & gsm.ANSWER_TIMEOUT){
                gsm.mode++;//error = ERROR_TIMEOUT;
                break;
            }
            else if (gsm.answer & gsm.ANSWER_ERROR){
                gsm.mode++;//error = ERROR_ANSWER;
                break;
            }
            else if (gsm.answer & gsm.ANSWER_OK){
                gsm.mode++;
                break;
            }
        
        }
        break;

        default:
            if (keyToNeedReset == 0xAA55){
                keyToNeedReset = 0;
                NVIC_SystemReset();
            }
            step = stepOld;
            gsm.changeProcess(gsm.PROCESS_EMPTY);
            break;

    }
}
//-----------------------------------------------------
void Sms::sendSmsEnglish(const char *number, const char *message) {
    uint16_t length = strlen(message);
    if (!strlen(number) || number[0] != '+') return;
    if (length > 140) return;
    
    for (int i=0; i<16; i++){
        smsMsg.phone[i] = *number++;
    }
    
    for (int i=0; i<140; i++){
        smsMsg.message[i] = *message;
        if (*message == 0) break;
        message++;
    }
    
    //gsm.changeProcess(gsm.PROCESS_SEND_SMS_ENGLISH);
    isNeedToSendSmsEnglish = true;
}
//-----------------------------------------------------
void Sms::sendSmsRussian(const char *number, const char *message) 
{
    uint16_t f = 0;
    uint16_t length = strlen(message);
    
    if (!strlen(number) || number[0] != '+') return;
    if (length > 70) return;
    
    for (int i=0; i<16; i++){
        smsMsg.phone[i] = 0;
    }
    uint8_t posPhone = Convert.strToStr("AT+CMGS=", smsMsg.phone);
    
    f = 0;
    for (int i=0;i<16; i++){
        if (number[i] == 0) break;
        if (number[i] >= '0' && number[i] <= '9')
        {
            f++;
        }
    }
    uint8_t unicode = 1, buf_numb = 1;
    uint16_t buf_point = 0, messNumb = 0;//strlen(message);
    uint16_t a=f/2;
    if (a != (f*2)) f++;    // ---
    f+=2;
    /*
    if (length == 20){
        core.intToStr(95, &smsMsg.phone[posPhone]);
    }
    else{
        core.intToStr(107, &smsMsg.phone[posPhone]);
    }
    */
    ///*
    if (length <= (140/(unicode+1))) Convert.intToStr(f+(length*(unicode+1)), &smsMsg.phone[posPhone]);// 54=95, 60=107
    else
    {
        a = length;
        if (a > (140/(unicode+1)-3)) a=(140/(unicode+1)-3);
        Convert.intToStr(f+6+(a*(unicode+1)), &smsMsg.phone[posPhone]);
    }//*/
    
    
    //smsMsg.message[x++] = 
    
    // SCA  :
    if (length <= (140/(unicode+1))) buf_point += Convert.strToStr("001100", &smsMsg.message[buf_point]);// GSM_SendSim("000100");
    else
    {
        buf_point += Convert.strToStr("00510", &smsMsg.message[buf_point]);
        buf_point += Convert.intToStr(buf_numb-1, &smsMsg.message[buf_point]);
    }
    //    :
    a=0;
    uint8_t buf[16];
    for (int i=0; i<16; i++) buf[i] = 0;
    for (int i=0; i<16; i++)
    {
        if (number[i] == 0) break;
        if (number[i] >= '0' && number[i] <= '9')
        {
            buf[a] = number[i];
            a++;
        }
    }
    f=a/2;
    if (f == (a*2)) f=0;    // ---
    else f=1;   // ---
    if (f == 1) buf[a] = 'F';   // ---
    buf_point += Convert.strToStr("0", &smsMsg.message[buf_point]);
    if (a > 9) a += 55; // 'B'-  11 
    else a += 48;   // ---
    buf_point += Convert.charToHex(a, &smsMsg.message[buf_point]);//GSM_SendData(a,1);
    buf_point += Convert.strToStr("91", &smsMsg.message[buf_point]); //   (+7...)
    for (int i=0; i<20; i+=2)
    {
        a = buf[i+1];
        if (a > 0)
        {
            buf_point += Convert.charToHex(a, &smsMsg.message[buf_point]);//GSM_SendData(a,1);
        }
        a = buf[i];
        if (a > 0)
        {
            buf_point += Convert.charToHex(a, &smsMsg.message[buf_point]);//GSM_SendData(a,1);
        }
        if (a == 0) break;
    }
    // (0018  FLASH ):
    if (false) buf_point += Convert.strToStr("001", &smsMsg.message[buf_point]);  // ---
    else buf_point += Convert.strToStr("000", &smsMsg.message[buf_point]);  //  (Flash-)
    if (unicode) buf_point += Convert.strToStr("8", &smsMsg.message[buf_point]); //  (UCS2)
    else buf_point += Convert.strToStr("4", &smsMsg.message[buf_point]); //  (ASCII)
    buf_point += Convert.strToStr("17", &smsMsg.message[buf_point]); //   - 2 
    //  ( HEX):
    if (length <= (140/(unicode+1))) a = length*2;///*(unicode+1);
    else
    {
        a = length;
        if (a > (140/(unicode+1)-3)) a=(140/(unicode+1)-3);
        a = a*(unicode+1)+6;
    }
    buf_point += Convert.byteToHex(a, &smsMsg.message[buf_point]);//GSM_SendData(a,1);
    
    if (length > (140/(unicode+1)))
    {
        buf_point += Convert.strToStr("050003", &smsMsg.message[buf_point]);
        buf_point += Convert.byteToHex(messNumb, &smsMsg.message[buf_point]);
        buf_point += Convert.byteToHex((buf_point/(140/(unicode+1)-3))+1, &smsMsg.message[buf_point]);
        buf_point += Convert.byteToHex(buf_numb, &smsMsg.message[buf_point]);
    }
    
    ///if (unicode) Send_Unicode();
    for (int i=0; i<length; i++){
        a = message[i];
        if (a > 127) a += 0x350;
        for (int n=0; n<4; n++)
        {
            int b = (a & 0xF000)>>12;
            if (b > 9) b += 55;
            else b += 48;
            
            buf_point += Convert.charToHex(b, &smsMsg.message[buf_point]);
            if (buf_point >= SMS_MAX_LEN) break;
            
            a = a<<4;
        }
        if (i > 67) break;
    }
    smsMsg.message[buf_point] = 0;
    
    //strncpy(smsMsg.message, message, SMS_MAX_LEN);
    
    //gsm.changeProcess(gsm.PROCESS_SEND_SMS_RUSSIAN);
    isNeedToSendSmsRussian = true;
}
//-----------------------------------------------------
bool Sms::parseMessage(char* number, char* message)
{
    /* ── Save sender phone for reply functions ───────────────────────────── */
    strncpy(replyPhone, number, PHONE_MAX_LEN - 1);
    replyPhone[PHONE_MAX_LEN - 1] = '\0';

    char ts[32]; unixTime.getTimestamp(ts);
    log_gsm(ts); log_gsm(" SMS from: "); log_gsm(number[0] ? number : "unknown"); log_gsm("\r\n");

    /* ── Parse ───────────────────────────────────────────────────────────── */
    TlSmsParseResult result;
    tl_sms_parse(number, message, gsm.pin, gsm.phones[0], &gsm.phones[1], result);

    if (!result.authenticated) {
        log_gsm("Auth failed\r\n");
        return false;
    }

    /* ── Log parse errors ────────────────────────────────────────────────── */
    for (uint8_t e = 0; e < result.errCount; e++) {
        log_gsm("Parse error: "); log_gsm(result.errors[e]); log_gsm("\r\n");
    }

    /* ── Dispatch commands ───────────────────────────────────────────────── */
    bool res = (result.cmdCount > 0);
    for (uint8_t i = 0; i < result.cmdCount; i++) {
        const TlSmsCmd& cmd = result.cmds[i];
        switch (cmd.type) {

            case TL_CMD_ADMIN:
                strncpy(gsm.phones[0], cmd.phone, 15);
                gsm.phones[0][15] = '\0';
                flash.writeSetup();
                sendAnswerText("Admin phone updated.");
                break;

            case TL_CMD_PHONE:
                if (cmd.phoneNum >= 1 && cmd.phoneNum <= 5) {
                    strncpy(gsm.phones[cmd.phoneNum], cmd.phone, 15);
                    gsm.phones[cmd.phoneNum][15] = '\0';
                    flash.writeSetup();
                    sendAnswerText("Phone updated.");
                }
                break;

            case TL_CMD_WARMUP:
                /* TODO: set burner/element on, all zones heat, floor on */
                break;

            case TL_CMD_SETPIN:
                memcpy(gsm.pin, cmd.pin, 5);
                flash.writeSetup();
                sendAnswerText("PIN updated.");
                break;

            case TL_CMD_PING:
                sendAnswerSmsPing();
                break;

            case TL_CMD_RESET:
                sendAnswerText("Reset...");
                keyToNeedReset = 0xAA55;
                *(__IO uint32_t *)(0x20023F00) = 0x00000000;
                break;

            case TL_CMD_FACTORY:
                setFactory();
                sendAnswerText("Factory reset done.");
                break;

            case TL_CMD_STATUS:
                /* TODO: build and send status SMS */
                break;

            case TL_CMD_FAULTREPORT:
                /* TODO: cmd.boolVal */
                break;

            case TL_CMD_UNIT:
                /* TODO: cmd.unit — TL_UNIT_C / TL_UNIT_F */
                break;

            case TL_CMD_BURNER:
                /* TODO: cmd.boolVal */
                break;

            case TL_CMD_ELEMENT:
                /* TODO: cmd.boolVal */
                break;

            case TL_CMD_FLOOR_TOGGLE:
                /* TODO: cmd.boolVal */
                break;

            case TL_CMD_FLOOR_SETPOINT:
                /* TODO: cmd.intVal (2..32 °C) */
                break;

            case TL_CMD_ENGINE_TOGGLE:
                /* TODO: cmd.boolVal */
                break;

            case TL_CMD_ENGINE_SETPOINT:
                /* TODO: cmd.intVal (0..80 °C) */
                break;

            case TL_CMD_SYSTIMER:
                /* TODO: cmd.intVal (1..100 h, 96+ = unlimited) */
                break;

            case TL_CMD_ZONE_STATE:
                /* TODO: cmd.zone.num (1..5 or TL_ZONE_ALL), cmd.zone.state */
                break;

            case TL_CMD_ZONE_FAN_MODE:
                /* TODO: cmd.zone.num, cmd.zone.fanMode */
                break;

            case TL_CMD_ZONE_FAN_PERCENT:
                /* TODO: cmd.zone.num, cmd.zone.percent (10..100) */
                break;

            case TL_CMD_ZONE_DAY_SP:
                /* TODO: cmd.zone.num, cmd.zone.setpoint (10..32 °C) */
                break;

            case TL_CMD_ZONE_NIGHT_SP:
                /* TODO: cmd.zone.num, cmd.zone.setpoint (10..32 °C) */
                break;

            default:
                break;
        }
    }

    if (!res) log_gsm("No commands recognised\r\n");
    return res;
}
//-----------------------------------------------------
// REMOVED: getMessageParams() — replaced by tl_sms_parse()
//-----------------------------------------------------
#if 0 /* old parseMessage — kept for reference, delete when no longer needed */
bool Sms::parseMessage_OLD(char* number, char* message)
{
    const int COMMANDS_MAX = 22;
    const char commands[COMMANDS_MAX][13] = 
    {
        "admin",        // 0
        "add",          // 1
        "list",         // 2
        "delall",       // 3
        "del",          // 4
        "update",       // 5
        "ping",         // 6
        "reset",        // 7
        "factory",      // 8
        "*15",          // 9
        "*16",          // 10
        "*1",           // 11
        "*2",           // 12
        "*3",           // 13
        "*4",           // 14
        "*5",           // 15
        "*6",           // 16
        "*7",           // 17
        "*8",           // 18
        "*9",           // 19
        "internet",     // 20
        "start server", // 21
    };
    uint16_t position = 0;
    uint8_t a, b, dot;
    char stroke[64];
    bool res = false;
    bool isCompare = false;
    char str[255];
    
    log_gsm("\r\n");
    unixTime.getTimestamp(str);
    log_gsm(str);
    log_gsm("READ SMS\r\n");
    log_gsm("SMS phone number: ");
    log_gsm(number);
    log_gsm("\r\n");
    
    log_gsm("SMS text: \"");
    log_gsm(message);
    log_gsm("\"\r\n");
    
    bool isTrust = (gsm.phones[0][0] != '+');
    if (strlen(gsm.phones[0]) < 10) isTrust = true;
    if (isTrust){
        log_gsm("The modem does not have a trusted number set\r\n");
    }
    
    if (number[0] != 0){
        for (int k=0; k<6; k++){
            if (k==6){
                if (core.getTick() > (5*60*1000)) break;
                isTrust = true;
            }
            if (gsm.phones[k][0] != 0 || isTrust){
                if ((strlen(gsm.phones[k]) >= 10 && Convert.compareStr(gsm.phones[k], number)) || isTrust){
                    // ---
                    isCompare = true;
                    if (message[0] >= 'A' && message[0] <= 'Z'){
                        //    ,  
                        message[0] += ('a'-'A');
                    }
                    gsm.posRingPhone = k;
                    int8_t code = -1;
                    for (int x=0; x<COMMANDS_MAX; x++){
                        if (Convert.compareStr(message, (char *)commands[x])){
                            code = x;
                            position = strlen(commands[x]);
                            if (message[position] == '.') position++;
                            break;
                        }
                    }
                    if (code >= 11){
                        //  ,  
                        getMessageParams(&message[3]);
                    }
                    
                    if (isTrust && code != 0){
                        log_gsm("Only add admin command can be accepted!\r\n");
                        return false;
                    }
                    
                    switch(code){
                        case 0:
                            // admin
                            a = 0;
                            for (int i=0; i<16; i++){
                                if (message[position] != ' '){
                                    if ((message[position] >= '0' && message[position] <= '9')
                                        || message[position] == '+'){
                                        gsm.phones[0][a++] = message[position];
                                    }
                                    else break;
                                }
                                position++;
                            }
                            if (a > 0){
                                while (true){
                                    if (a < 16){
                                        gsm.phones[0][a++] = 0;
                                    }
                                    else break;
                                }
                            }
                            // The Administrator is assigned.
                            sendAnswerText("The Administrator is assigned.");
                            flash.writeSetup();
                            isChangePhones = true;
                            res = true;
                            break;
                        case 1:
                            // add
                            a = 0;
                            b = 0;
                            for (int i=1; i<5; i++){
                                if ((gsm.phones[i][0] < '0' || gsm.phones[i][0] > '9') && gsm.phones[i][0] != '+'){
                                    for (int j=0; j<16; j++){
                                        if (message[position] != ' '){
                                            if ((message[position] >= '0' && message[position] <= '9')
                                                || message[position] == '+'){
                                                gsm.phones[i][a++] = message[position];
                                                b = i;
                                            }
                                            else break;
                                        }
                                        position++;
                                    }
                                    break;
                                }
                            }
                            if (a > 0){
                                while (true){
                                    if (a < 16){
                                        gsm.phones[b][a++] = 0;
                                    }
                                    else break;
                                }
                            }
                            sendAnswerSmsList();
                            flash.writeSetup();
                            isChangePhones = true;
                            res = true;
                            break;
                        case 2:
                            // list
                            sendAnswerSmsList();
                            res = true;
                            break;
                        case 3:
                            // delall
                            for (int i=0; i<5; i++){
                                for (int j=0; j<16; j++){
                                    gsm.phones[i][j] = 0;
                                }
                                //gsm.phones[i][0] = 'n';
                                //gsm.phones[i][1] = 'u';
                                //gsm.phones[i][2] = 'l';
                                //gsm.phones[i][3] = 'l';
                            }
                            // All the trusted numbers are deleted.
                            gsm.counterSendSms = 3;
                            sendSmsEnglish(number, "All the trusted numbers are deleted.");
                            flash.writeSetup();
                            isChangePhones = true;
                            res = true;
                            break;
                        case 4:
                            // del
                            a = message[position]-'0';
                            if (a >= 2 && a <= 5){
                                for (int i=0; i<16; i++){
                                    gsm.phones[a-1][i] = 0;
                                }
                                //gsm.phones[a-1][0] = 'n';
                                //gsm.phones[a-1][1] = 'u';
                                //gsm.phones[a-1][2] = 'l';
                                //gsm.phones[a-1][3] = 'l';
                            }
                            sendAnswerSmsList();
                            flash.writeSetup();
                            isChangePhones = true;
                            res = true;
                            break;
                        case 5:
                            // update
                            a = 0;
                            dot = 0;
                            for (int i=0; i<16; i++){
                                if (message[position] != ' '){
                                    if ((message[position] >= '0' && message[position] <= '9')
                                        || message[position] == '.'){
                                        stroke[a++] = message[position];
                                        if (message[position] == '.') dot++;
                                    }
                                    else break;
                                }
                                position++;
                            }
                            stroke[a++] = 0;
                            if (dot == 3){
                                Convert.strToStr(stroke, updateToVersion);
                                updateToVersion[15] = gsm.posRingPhone;
                                sendAnswerText("Update started");
                                flash.writeSetup();
                                *(__IO uint32_t *)(0x20023F00) = 0x0016AA55;
                                keyToNeedReset = 0xAA55;
                                //NVIC_SystemReset();
                            }
                            break;
                        case 6:
                            // ping
                            sendAnswerSmsPing();
                            res = true;
                            break;
                        case 7:
                            // reset
                            sendAnswerText("Reset...");
                            keyToNeedReset = 0xAA55;
                            res = true;
                            *(__IO uint32_t *)(0x20023F00) = 0x00000000;
                            //NVIC_SystemReset();
                            break;
                        case 8:
                            setFactory();
                            sendAnswerSmsSettings();
                            res = true;
                            break;
                        case 9:
                            // *15. (A,P,E,C,L)
                            sendAnswerSmsFive();
                            res = true;
                            break;
                        case 10:
                            // *16. (16.100  )
                            a = 0;
                            for (int i=0; i<16; i++){
                                if (message[position] != ' '){
                                    if ((message[position] >= '0' && message[position] <= '9')
                                        || message[position] == '*'
                                        || message[position] == '#'){
                                            
                                        if (a == 0 && message[position] != '*' && message[position] != '#'){
                                            gsm.phoneBalance[a++] = '*';
                                        }
                                        if (a == 4 && message[position] != '*' && message[position] != '#'){
                                            gsm.phoneBalance[a++] = '#';
                                        }
                                        gsm.phoneBalance[a++] = message[position];
                                        if (a > 4) break;
                                    }
                                    else break;
                                }
                                position++;
                            }
                            gsm.isNeedToRequestBalance = true;
                            gsm.isNeedToSendBalanceOnSms = true;
                            res = true;
                            break;
                        case 11:
                            // TODO: SMS command — start CAN device
                            res = true;
                            break;
                        case 12:
                            // TODO: SMS command — update CAN device settings
                            res = true;
                            break;
                        case 13:
                            // TODO: SMS command — stop CAN device
                            res = true;
                            break;
                        case 14:
                            // *4. ( )
                            sendAnswerSmsParameters(ANSWER_PARAM_NAME_INFO);
                            res = true;
                            break;
                        case 15:
                            // *5.  (P,E,T,M)    ( )
                            
                            break;
                        case 16:
                            // *6.    ( )
                            
                            break;
                        case 17:
                            // TODO: SMS command — set CAN device parameters
                            res = true;
                            break;
                        case 18:
                            // TODO: SMS command — set CAN device default settings
                            res = true;
                            break;
                        case 19:
                            // *9.     ( )
                            a = 0;
                            for (int i=0; i<16; i++){
                                if (message[position] != ' '){
                                    if (message[position] >= '0' && message[position] <= '9'){
                                        stroke[a++] = message[position];
                                    }
                                    else break;
                                }
                                position++;
                            }
                            if (a == 11 || a == 12){
                                /*
                                // ---
                                j=0;
                                for (int i=0; i<a; i++){
                                    heater.device[heater.selectedDevice].serialNumber[j++] = stroke[i];
                                }
                                heater.device[heater.selectedDevice].serialNumber[j++] = 0;
                                
                                j=0;
                                for (int i=0; i<(a-7); i++){
                                    strAssembly[j++] = stroke[i];
                                }
                                strAssembly[j++] = 0;
                                assembly = core.strToLong(strAssembly);
                                
                                j=0;
                                for (int i=(a-7); i<a; i++){
                                    strSerial[j++] = stroke[i];
                                }
                                strSerial[j++] = 0;
                                serial = core.strToLong(strSerial);
                                
                                #ifndef USE_CAN
                                heater.setId(assembly, serial);
                                #endif
                                */
                            }
                            sendAnswerSmsSerialNumber();
                            res = true;
                            break;
                            
                        case 20:
                            // SMS-only mode (internet removed)
                            gsm.isOnlySmsMode = true;
                            flash.writeSetup();
                            sendAnswerText("SMS only mode");
                            break;
                        case 21:
                            sendAnswerText("SMS only mode");
                            break;
                    }

                    break;
                }
            }
        }
        if (!res){
            if (isCompare){
                log_gsm("message not recognized!\r\n");
            }
            else{
                log_gsm("No match with trusted number!\r\n");
            }
        }
    }
    return res;
}
#endif /* old parseMessage */
//-----------------------------------------------------
#if 0 /* getMessageParams — no longer used, replaced by tl_sms_parse() */
bool Sms::getMessageParams(char* message)
{
    int i=0;
    smsCmdFlags = 0;
    while(1){
        char name = message[i++];
        if (name == 0) return false;
        if (name == '#') return true;
        uint32_t val = 0;
        for (int x=0; x<5; x++){
            if (message[i] >= '0' && message[i] <= '9'){
                val = val*10;
                val += (message[i]-'0');
                i++;
            }
            else break;
        }
        switch(name){
            case 'A':
                smsCmd.A = val;
                smsCmdFlags |= SMS_CMD_FLAG_A;
                break;
            case 'C':
                smsCmd.C = val;
                smsCmdFlags |= SMS_CMD_FLAG_C;
                break;
            case 'E':
                smsCmd.E = val;
                smsCmdFlags |= SMS_CMD_FLAG_E;
                break;
            case 'F':
                smsCmd.F = val;
                smsCmdFlags |= SMS_CMD_FLAG_F;
                break;
            case 'J':
                smsCmd.J = val;
                smsCmdFlags |= SMS_CMD_FLAG_J;
                break;
            case 'H':
                smsCmd.H = val;
                smsCmdFlags |= SMS_CMD_FLAG_H;
                break;
            case 'I':
                smsCmd.I = val;
                smsCmdFlags |= SMS_CMD_FLAG_I;
                break;
            case 'L':
                smsCmd.L = val;
                smsCmdFlags |= SMS_CMD_FLAG_L;
                break;
            case 'N':
                smsCmd.N = val;
                smsCmdFlags |= SMS_CMD_FLAG_N;
                break;
            case 'P':
                smsCmd.P = val;
                smsCmdFlags |= SMS_CMD_FLAG_P;
                break;
            case 'R':
                smsCmd.R = val;
                smsCmdFlags |= SMS_CMD_FLAG_R;
                break;
            case 'S':
                smsCmd.S = val;
                smsCmdFlags |= SMS_CMD_FLAG_S;
                break;
            case 'T':
                smsCmd.T = val;
                smsCmdFlags |= SMS_CMD_FLAG_T;
                break;
            case 'W':
                smsCmd.W = val;
                smsCmdFlags |= SMS_CMD_FLAG_W;
                break;
            case 'e':
                smsCmd.e = val;
                smsCmdFlags |= SMS_CMD_FLAG_e;
                break;
            case 'p':
                smsCmd.p = val;
                smsCmdFlags |= SMS_CMD_FLAG_p;
                break;
            case 'r':
                smsCmd.r = val;
                smsCmdFlags |= SMS_CMD_FLAG_r;
                break;
            case 's':
                smsCmd.s = val;
                smsCmdFlags |= SMS_CMD_FLAG_s;
                break;
            case 't':
                smsCmd.t = val;
                smsCmdFlags |= SMS_CMD_FLAG_t;
                break;
            case 'M':
                smsCmd.M = val;
                smsCmdFlags |= SMS_CMD_FLAG_M;
                break;
        }
    }
}
#endif /* getMessageParams */
//-----------------------------------------------------
void Sms::sendAnswerSmsParameters(AnswerNameTypeDef name)
{
    // TODO: build CAN device status message and send via SMS
    (void)name;
    sendSmsEnglish(gsm.phones[gsm.posRingPhone], "Status: TODO");
}
//-----------------------------------------------------
void Sms::sendAnswerSmsSerialNumber(void)
{
    // TODO: report modem/device serial number via SMS
    sendSmsEnglish(gsm.phones[gsm.posRingPhone], "SN: TODO");
}
//-----------------------------------------------------
void Sms::sendAnswerSmsSettings(void)
{
    // TODO: send settings SMS
    sendSmsEnglish(gsm.phones[gsm.posRingPhone], "Settings: TODO");
}
//-----------------------------------------------------
void Sms::sendAnswerSmsFive(void)
{
    char message[255];
    uint16_t n;
    
    gsm.counterSendSms = 3;
    if (smsCmd.L == 1){
        n = 0;
        if (smsCmdFlags | SMS_CMD_FLAG_P){
            //       .
            n += Convert.strToStr("   ", &message[n]);
            if (smsCmd.P) n += Convert.strToStr(".", &message[n]);
            else n += Convert.strToStr(".", &message[n]);
            n += Convert.strToStr("\r\n", &message[n]);
        }
        if (smsCmdFlags | SMS_CMD_FLAG_E){
            //     ,      .
            n += Convert.strToStr("    ", &message[n]);
            if (smsCmd.E) n += Convert.strToStr(".", &message[n]);
            else n += Convert.strToStr(".", &message[n]);
            n += Convert.strToStr("\r\n", &message[n]);
        }
        if (smsCmdFlags | SMS_CMD_FLAG_C){
            //          .
            n += Convert.strToStr("    ", &message[n]);
            if (smsCmd.C) n += Convert.strToStr(".", &message[n]);
            else n += Convert.strToStr(".", &message[n]);
            n += Convert.strToStr("\r\n", &message[n]);
        }
        if (smsCmdFlags | SMS_CMD_FLAG_L){
            //    SMS (0  , 1  )
            n += Convert.strToStr("   ", &message[n]);
            n += Convert.strToStr(".", &message[n]);
            n += Convert.strToStr("\r\n", &message[n]);
        }
        n += Convert.strToStr("\0", &message[n]);
        
        sendSmsRussian(gsm.phones[gsm.posRingPhone], message);
    }
    else{
        n = 0;
        if (smsCmdFlags | SMS_CMD_FLAG_P){
            //       .
            n += Convert.strToStr("Sending SMS confirmation ", &message[n]);
            if (smsCmd.P) n += Convert.strToStr("ON.", &message[n]);
            else n += Convert.strToStr("OFF.", &message[n]);
            n += Convert.strToStr("\r\n", &message[n]);
        }
        if (smsCmdFlags | SMS_CMD_FLAG_E){
            //     ,      .
            n += Convert.strToStr("Sending SMS in case of malfunction ", &message[n]);
            if (smsCmd.E) n += Convert.strToStr("ON.", &message[n]);
            else n += Convert.strToStr("OFF.", &message[n]);
            n += Convert.strToStr("\r\n", &message[n]);
        }
        if (smsCmdFlags | SMS_CMD_FLAG_C){
            //          .
            n += Convert.strToStr("Sending SMS by call ", &message[n]);
            if (smsCmd.C) n += Convert.strToStr("ON.", &message[n]);
            else n += Convert.strToStr("OFF.", &message[n]);
            n += Convert.strToStr("\r\n", &message[n]);
        }
        if (smsCmdFlags | SMS_CMD_FLAG_L){
            //    SMS (0  , 1  )
            n += Convert.strToStr("The language of SMS messages is ", &message[n]);
            n += Convert.strToStr("English.", &message[n]);
            n += Convert.strToStr("\r\n", &message[n]);
        }
        n += Convert.strToStr("\0", &message[n]);
        
        sendSmsEnglish(gsm.phones[gsm.posRingPhone], message);
    }
}
//-----------------------------------------------------
void Sms::sendAnswerSmsList(void)
{
    char positions[5][5] = {
        "1.",
        "2.",
        "3.",
        "4.",
        "5."
    };
    char message[255];
    uint16_t n;
    
    gsm.counterSendSms = 3;
    
    n = 0;
    
    for (int i=0; i<5; i++){
        if ((gsm.phones[i][0] >= '0' && gsm.phones[i][0] <= '9')
            || gsm.phones[i][0] == '+'){
            n += Convert.strToStr(positions[i], &message[n]);
            n += Convert.strToStr(gsm.phones[i], &message[n]);
            n += Convert.strToStr("\r\n", &message[n]);
        }
    }
    
    sendSmsEnglish(gsm.phones[gsm.posRingPhone], message);
}
//-----------------------------------------------------
void Sms::sendAnswerSmsPing(void)
{
    char message[255];
    uint16_t n;
    
    gsm.counterSendSms = 3;
    n = 0;
    
    n = Convert.strToStr("Modem SN: ", message);
    n += Convert.strToStr(serialNumberModem, &message[n]);
    n += Convert.strToStr(".\r\n", &message[n]);

    n += Convert.strToStr("IMEI: ", message);
    n += Convert.strToStr(gsm.imei, &message[n]);
    n += Convert.strToStr(".\r\n", &message[n]);

    n += Convert.strToStr("SMS-only mode.\r\n", &message[n]);
    
    n += Convert.strToStr("\0", &message[n]);
    
    sendSmsEnglish(replyPhone, message);
}
//-----------------------------------------------------
void Sms::sendAnswerText(char *message)
{
    gsm.counterSendSms = 3;
    sendSmsEnglish(replyPhone, message);
}
//-----------------------------------------------------
void Sms::SendSetup(void)
{
    // TODO: apply CAN device settings from SMS command
}
//-------------------------------------------------------------------

