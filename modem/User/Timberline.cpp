#include "Timberline.h"
#include "Modem.h"
#include "StringTransfer.h"
#include "timberline_sms.h"
#include "flash.h"
#include "log.h"
#include <string.h>
#include <stdio.h>

/* Convert temperature to °C if user works in °F */
static int8_t toC(int8_t val) {
    if (modem.tempUnit == TL_UNIT_F)
        return (int8_t)(((int16_t)val - 32) * 5 / 9);
    return val;
}
#include "Heaters.h"
#include "unix_time.h"

Timberline timberline;

void Timberline::ProcessCanMessage(CanRxMessage* msg)
{
    uint16_t pgn = (msg->ExtId>>20)&0x1FF;
    uint8_t TransType = (msg->ExtId>>3)&127;
    uint8_t TransAddr = (msg->ExtId)&7;
    uint8_t* D = msg->Data;


    if ((TransType==126 && TransAddr==1) || TransType==125) timberline.connected = true;
    uint32_t ID, V32;

    uint8_t temp=0;

    ID=(pgn<<20)+(TransType<<13)+(TransAddr<<10)+(can.idType<<3)+can.idAddress;

    stringTransfer.onCanMessage(msg->ExtId, D);

    uint32_t Addr;

    if ((TransType==34) ||    //Binar 10D
            (TransType==35) ||    //Binar 10B
            (TransType==23) ||    //Binar 5D
            (TransType==27) ||    //Binar 5B
            (TransType==42) ||    //Binar Split D
            (TransType==43))      //Binar Split B
        heaters.Instances[TransAddr].type=(heaterType_t)TransType;
    switch(pgn)
    {
    case 1: //
        ID=(2<<20)+(TransType<<13)+(TransAddr<<10)+(can.idType<<3)+can.idAddress;
        switch((D[0]<<8)+D[1])
        {
        case  0://
            can.SendMessage(ID,D[0],D[1],VERSION_1,VERSION_2,VERSION_2,VERSION_4,0xFF,0xFF);
            break;
        case 1://
            break;
        case 3: //
            break;
        case 4: //
            break;
        case 22://Reset CPU
            //ToDo     AA 55
            //*(__IO uint32_t*) (NVIC_VectTab_RAM+1020) = 0x0016AA55;
            NVIC_SystemReset();
            break;
        default:
            can.SendMessage((2<<20)+(TransType<<13)+(TransAddr<<10)+(can.idType<<3)+can.idAddress,D[0],D[1],0xFF,0xFF,0xFF,0xFF,0xFF,0xFF);
        }
        break;
    case 2: //
        switch((D[0]<<8)+D[1])
        {
        case  0:// ?
            break;
        }
        break;
    case 60: //GSM settings write request, same sub-packet layout as the status broadcast
        if (D[0]==1) {
            //2 бита на bool (00=off,01=on,11=без изменений), как в canBroadcast()
            if (((D[1]>>0)&3)<2) modem.isOnlySmsMode = (D[1]>>0)&1;
            if (((D[1]>>2)&3)<2) modem.faultReport   = (D[1]>>2)&1;
            if (((D[1]>>4)&3)<2) modem.cmdAck         = (D[1]>>4)&1;
            if (((D[1]>>6)&3)<2) modem.tempUnit       = (D[1]>>6)&1;
            flash.writeSetup();
        }
        break;
    case 7:

    /*
    {
            if (D[0]==4)
            {
    					if (D[1]==3)
    					{
               if (D[4]*0x1000000+D[5]*0x10000+D[6]*0x100+D[7]==0xFFFFFFFF)
                {
                    parameters.readProcess.readProcessNum++;
                    parameters.readProcess.succReadFlag=true;
                    break;
                }
                parameters.newState.parametersFound++;
                parameters.foundParams[parameters.readProcess.arrayIndex].num=D[2]*256+D[3];
                parameters.foundParams[parameters.readProcess.arrayIndex].rawValue=D[4]*0x1000000+D[5]*0x10000+D[6]*0x100+D[7];
                parameters.readProcess.arrayIndex++;
    						parameters.readProcess.readProcessNum++;
                parameters.readProcess.succReadFlag=true;
    					}
    					if (D[1]==0)
    					{
    					parameters.erasingDone=true;
    					}
    					if (D[1]==2)
    					{
    					parameters.writeProcess.flashWriteSuccessfull=true;
    					}
            }
            if (D[0]==5)
            {
                parameters.readProcess.readProcessNum++;
                parameters.readProcess.succReadFlag=true;
            }
            break;
        }
    */
    case 10:
        if (D[0]!=255) heaters.Instances[TransAddr].stage=D[0];
        if (D[1]!=255) heaters.Instances[TransAddr].mode=D[1];
        if (D[2]!=255) heaters.Instances[TransAddr].errorCode=D[2];
        if ((D[3]&3)!=3) heaters.Instances[TransAddr].pumpFault=D[3]&3;
        if (D[4]!=255) heaters.Instances[TransAddr].WarningCode = D[4];
        if (D[5]!=255) heaters.Instances[TransAddr].BlinkCount = D[5];
        break;
    case 11:
        if (D[0]!=255||D[1]!=255) heaters.Instances[TransAddr].Voltage = (D[0]*256+D[1])/10.0f;
        break;
    case 12:
        if (D[0]!=0xff) heaters.Instances[TransAddr].BlowerSet = D[0];
        if (D[1]!=0xff) heaters.Instances[TransAddr].BlowerReal = D[1];
        if (D[2]!=255||D[3]!=255) heaters.Instances[TransAddr].FPSet = (D[2]*256+D[3])/100.0f;
        if (D[6]!=0xff) heaters.Instances[TransAddr].GlowPlug = D[6];
        if ((D[7]&3)<2) heaters.Instances[TransAddr].PumpState = D[7]&3;

        break;
    case 13:
        if (D[0]!=255 || D[1]!=255) heaters.Instances[TransAddr].Tflame = (D[0]<<8)+D[1];
        if (D[2]!=255) heaters.Instances[TransAddr].Tcpu = D[2]-75;
        if (D[3]!=255) heaters.Instances[TransAddr].Tliquid = D[3]-75;
        if (D[4]!=255) heaters.Instances[TransAddr].Toverheat = D[4]-75;
        break;
    case 18:
        switch(TransType)
        {
        case 23:
        case 27:
        case 34:
        case 35:
        case 43:
        case 44:
            memcpy(heaters.Instances[TransAddr].version,D,4);
            break;
        case 125: //  HCU
            memcpy(timberline.MbcVersion,D,4);
            break;
        case 126: //  HCU
            if (TransAddr==1)
                memcpy(timberline.MbcVersion,D,4);
            break;
        }
        break;
    case 19:
        hcuType = TransType;
        hcuAddress = TransAddr;
        if (D[0]==1)
        {
            if (((D[4]>>0)&3)<2) DomesticWaterFlow = D[4]&1;
            if (((D[4]>>2)&3)<2) DomesticWaterButton = (D[4]>>2)&1;
            if (((D[4]>>4)&3)<2) EcoButton = (D[4]>>4)&1;
            if (((D[5]>>0)&3)<2) floorConnected = D[5]&1;
            if (((D[5]>>2)&3)<2) engineConnected = (D[5]>>2)&1;
            if (((D[6]>>0)&3)<2) pumpState[AUX_PUMP1] = (D[6]>>0)&1;
            if (((D[6]>>2)&3)<2) pumpState[AUX_PUMP2] = (D[6]>>2)&1;
            if (((D[6]>>4)&3)<2) pumpState[AUX_PUMP3] = (D[6]>>4)&1;
            if (((D[6]>>6)&3)<2) pumpState[PUMP4] = (D[6]>>6)&1;
            if (((D[7]>>0)&3)<2) pumpState[HEATER_PUMP] = (D[7]>>0)&3;
            if (((D[7]>>2)&3)<2) pumpState[PUMP1] = (D[7]>>2)&3;
            if (((D[7]>>4)&3)<2) pumpState[PUMP2] = (D[7]>>4)&3;
            if (((D[7]>>6)&3)<2) pumpState[PUMP3] = (D[7]>>6)&3;
        }

        if (D[0]==3)
        {

            if (D[1]<=125) floorSetpoint=D[1]-75;
            if (D[2]<=10) floorHysteresis=D[2];
            if (D[3]<=155) engineSetpoint=D[3]-75;
            if ((D[4]*256+D[5])<=1450) engineDurationMinutes=D[4]*256+D[5];
            if (D[6]<=100 && D[6]>0) SystemTimeLimitHours=D[6];
            if (D[7]>=2 && D[7]<=60) pumpForceDurationMinutes=D[7];
        }

        if (D[0]==4)
        {
            if ((D[1])<4) zoneConnected[0] = D[1];
            if ((D[2])<4) zoneConnected[1] = D[2];
            if ((D[3])<4) zoneConnected[2] = D[3];
            if ((D[4])<4) zoneConnected[3] = D[4];
            if ((D[5])<4) zoneConnected[4] = D[5];
        }
        if (D[0]==5)
        {
            if (D[1]!=0xFF || D[2]!=0xFF || D[3]!=0xFF || D[4]!=0xFF)
                elementSeconds = D[1]<<24|D[2]<<16|D[3]<<8|D[4];
        }

        break;


    case 21:
        if (D[2]!=0xFF) tankTemperature = D[2]-75;
        if (D[4]!=0xFF) outdoorTemperature = D[4]-75;
        if (D[5]!=0xFF) heaterStateIcon = (heaterStateIcon_t)D[5];
        if (D[6]!=0xFF) liquidLevel = D[6];
        if (((D[7]>>2)&3)!=3) elementState = (D[7]>>2)&3;
        if (((D[7]>>4)&3)<3) elementDisabled = (D[7]>>4)&3;
        break;

    case 22:

        temp=D[0]&3;
        if (temp!=3)
            zoneStates[0] = (zoneState_t)temp;
        temp=(D[0]>>2)&3;
        if (temp!=3)
            zoneStates[1] = (zoneState_t)temp;
        temp=(D[0]>>4)&3;
        if (temp!=3)
            zoneStates[2] = (zoneState_t)temp;
        temp=(D[0]>>6)&3;
        if (temp!=3)
            zoneStates[3] = (zoneState_t)temp;
        temp=D[1]&3;
        if (temp!=3)
            zoneStates[4] = (zoneState_t)temp;

        if (D[2]!=255) zoneCurrentTemp[0]=D[2]-75;
        if (D[3]!=255) zoneCurrentTemp[1]=D[3]-75;
        if (D[4]!=255) zoneCurrentTemp[2]=D[4]-75;
        if (D[5]!=255) zoneCurrentTemp[3]=D[5]-75;
        if (D[6]!=255) zoneCurrentTemp[4]=D[6]-75;

        temp=D[7]&3;
        if (temp!=3)
            HeaterButton = temp;
        temp=(D[7]>>2)&3;

        if (temp<2)
            ElementButton = temp;
        temp=(D[7]>>4)&3;

        if (temp<2)
            FloorButton = temp;

        temp=(D[7]>>6)&3;
        if (temp<2)
            EngineButton = temp;
        break;

    case 24:
        if (D[3]!=255) zoneFanCurrentPwm[0]=D[3];
        if (D[4]!=255) zoneFanCurrentPwm[1]=D[4];
        if (D[5]!=255) zoneFanCurrentPwm[2]=D[5];
        if (D[6]!=255) zoneFanCurrentPwm[3]=D[6];
        if (D[7]!=255) zoneFanCurrentPwm[4]=D[7];
        break;

    case 25:
        if (D[0]!=255) zoneDaySetpoint[0]=D[0]-75;
        if (D[1]!=255) zoneDaySetpoint[1]=D[1]-75;
        if (D[2]!=255) zoneDaySetpoint[2]=D[2]-75;
        if (D[3]!=255) zoneDaySetpoint[3]=D[3]-75;
        if (D[4]!=255) zoneDaySetpoint[4]=D[4]-75;
        break;

    case 26:
        if (D[0]!=255) zoneNightSetpoint [0]=D[0]-75;
        if (D[1]!=255) zoneNightSetpoint[1]=D[1]-75;
        if (D[2]!=255) zoneNightSetpoint[2]=D[2]-75;
        if (D[3]!=255) zoneNightSetpoint[3]=D[3]-75;
        if (D[4]!=255) zoneNightSetpoint[4]=D[4]-75;
        break;

    case 27:
        if (D[0]!=255) zoneManualFanPercent[0] = D[0];
        if (D[1]!=255) zoneManualFanPercent[1] = D[1];
        if (D[2]!=255) zoneManualFanPercent[2] = D[2];
        if (D[3]!=255) zoneManualFanPercent[3] = D[3];
        if (D[4]!=255) zoneManualFanPercent[4] = D[4];

        temp=D[5]&3;
        if (temp!=3)
            zoneFanManualMode[0] = temp!=0;
        temp=(D[5]>>2)&3;
        if (temp!=3)
            zoneFanManualMode[1] = temp!=0;
        temp=(D[5]>>4)&3;
        if (temp!=3)
            zoneFanManualMode[2] = temp!=0;
        temp=(D[5]>>6)&3;
        if (temp!=3)
            zoneFanManualMode[3] = temp!=0;
        temp=D[6]&3;
        if (temp!=3)
            zoneFanManualMode[4] = temp!=0;
        break;
    case 28:
        heaters.Instances[TransAddr].totalTime=D[0]<<24|D[1]<<16|D[2]<<8|D[3];
        heaters.Instances[TransAddr].workTime=D[4]<<24|D[5]<<16|D[6]<<8|D[7];
        break;
    case 29:
        if (D[0]==2)
            heaters.Instances[TransAddr].pressure = (D[4]*0x10000+D[5]*0x100+D[6])/1000.0;
        break;

    case 34: //   flash
        ID=(35<<20)+(TransType<<13)+(TransAddr<<10)+(can.idType<<3)+can.idAddress;
        Addr=(D[0]<<24)+(D[1]<<16)+(D[2]<<8)+D[3];
        for (uint16_t i=0; i<(D[4]<<8)+D[5]; i++)
        {
            V32=*(uint32_t*)Addr ;
            can.SendMessage(ID,Addr>>24,Addr>>16,Addr>>8,Addr,V32>>24,V32>>16,V32>>8,V32);
            Addr=Addr+4;
        }
        break;
    case 40:
    {
        if (D[0]!=255) unixTime.year = 2000+D[0];
        if (D[1]<13 && D[1]>0)   unixTime.mon = D[1];
        if (D[2]<32)   unixTime.mday = D[2];
        if (D[3]<24)   unixTime.hour = D[3];
        if (D[4]<60)   unixTime.min =  D[4];
        if (D[5]<60)   unixTime.sec =  D[5];
        unixTime.UnixTime = unixTime.calToTimer();
    }
    break;
    case 41:

        if (D[0]<24)  dayStartHour=D[0];
        if (D[1]<60)  dayStartMinute=D[1];
        if (D[2]<24)  nightStartHour=D[2];
        if (D[3]<60)  nightStartMinute = D[3];
        break;

    case 42:

        if (D[0]!=255) pumpForceFlag[HEATER_PUMP] = D[0];
        if (D[1]!=255) pumpForceFlag[PUMP1] = D[1];
        if (D[2]!=255) pumpForceFlag[PUMP2] = D[2];
        if (D[3]!=255) pumpForceFlag[PUMP3] = D[3];
        if (D[4]!=255) pumpForceFlag[AUX_PUMP1] = D[4];
        if (D[5]!=255) pumpForceFlag[AUX_PUMP2] = D[5];
        if (D[6]!=255) pumpForceFlag[AUX_PUMP3] = D[6];
        if (D[7]!=255) pumpForceFlag[PUMP4] = D[7];
        break;
    case 46:
        memcpy(errors,D,8);
        break;
    }//switch(PGN)

}

void sendToHcu(uint16_t pgn,uint8_t* D)
{
    can.SendMessage(pgn<<20 | timberline.hcuType<<13 | timberline.hcuAddress<<10 | can.idType | can.idAddress,D[0],D[1],D[2],D[3],D[4],D[5],D[6],D[7]);
}


/* Send reply only if device-command confirmations are enabled */
static void ack(const char* phone, const char* msg) {
    if (modem.cmdAck) modem.sendSms(phone, msg);
}

static void onSmsReceived(const char* phone, const char* text) {
    uint8_t D[8]= {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};
    TlSmsParseResult result;
    tl_sms_parse(phone, text, modem.pin, modem.phones[0], &modem.phones[1], result);

    if (!result.authenticated) {
        log_info("SMS: auth failed\r\n");
        return;
    }

    for (uint8_t e = 0; e < result.errCount; e++) {
        log_info("SMS parse error: ");
        log_info(result.errors[e]);
        log_info("\r\n");
    }

    for (uint8_t i = 0; i < result.cmdCount; i++) {
        const TlSmsCmd& cmd = result.cmds[i];
        switch (cmd.type) {

        case TL_CMD_PING:
            modem.sendSms(phone, "pong");
            break;

        case TL_CMD_RESET:
            modem.sendSms(phone, "Resetting...");
            NVIC_SystemReset();
            break;

        case TL_CMD_ADMIN:
            strncpy(modem.phones[0], cmd.phone, 15);
            modem.phones[0][15] = '\0';
            modem.sendSms(phone, "Admin set");
            break;

        case TL_CMD_PHONE:
            if (cmd.phoneNum >= 1 && cmd.phoneNum <= 4) {
                strncpy(modem.phones[cmd.phoneNum], cmd.phone, 15);
                modem.phones[cmd.phoneNum][15] = '\0';
                modem.sendSms(phone, "Phone updated.");
            }
            break;

        case TL_CMD_SETPIN:
            memcpy(modem.pin, cmd.pin, 5);
            modem.sendSms(phone, "PIN updated.");
            break;

        case TL_CMD_OFF: {
            uint8_t Doff[8] = {0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00};
            sendToHcu(22, Doff);
            ack(phone, "All off");
            break;
        }

        case TL_CMD_BURNER:
            D[7] = 0xFC | cmd.boolVal;
            sendToHcu(22, D);
            ack(phone, cmd.boolVal ? "Burner: ON" : "Burner: OFF");
            break;

        case TL_CMD_ELEMENT:
            D[7] = 0xF3 | (cmd.boolVal << 2);
            sendToHcu(22, D);
            ack(phone, cmd.boolVal ? "Element: ON" : "Element: OFF");
            break;

        case TL_CMD_FLOOR_TOGGLE:
            D[7] = 0xCF | (cmd.boolVal << 4);
            sendToHcu(22, D);
            ack(phone, cmd.boolVal ? "Floor: ON" : "Floor: OFF");
            break;

        case TL_CMD_FLOOR_SETPOINT: {
            int8_t sp = toC(cmd.intVal);
            D[0] = 3;
            D[1] = (uint8_t)(sp + 75);
            sendToHcu(19, D);
            static char rsp[24];
            sprintf(rsp, "Floor: %d\xb0%c", sp, modem.tempUnit ? 'F' : 'C');
            ack(phone, rsp);
            break;
        }

        case TL_CMD_ENGINE_TOGGLE:
            D[7] = 0x3F | (cmd.boolVal << 6);
            sendToHcu(22, D);
            ack(phone, cmd.boolVal ? "Engine: ON" : "Engine: OFF");
            break;

        case TL_CMD_ENGINE_SETPOINT: {
            int8_t sp = toC(cmd.intVal);
            D[0] = 3;
            D[3] = (uint8_t)(sp + 75);
            sendToHcu(19, D);
            static char rsp[24];
            sprintf(rsp, "Engine: %d\xb0%c", sp, modem.tempUnit ? 'F' : 'C');
            ack(phone, rsp);
            break;
        }

        case TL_CMD_ZONE_STATE: {
            static const char* zstate[] = {"off","heat","vent"};
            switch (cmd.zone.num) {          /* 1-based: z1..z5 */
            case 1: D[0] = 0xFC | cmd.zone.state; break;
            case 2: D[0] = 0xF3 | (cmd.zone.state << 2); break;
            case 3: D[0] = 0xCF | (cmd.zone.state << 4); break;
            case 4: D[0] = 0x3F | (cmd.zone.state << 6); break;
            case 5: D[1] = 0xFC | cmd.zone.state; break;
            }
            sendToHcu(22, D);
            static char rsp[16];
            sprintf(rsp, "Z%d: %s", cmd.zone.num,
                    cmd.zone.state < 3 ? zstate[cmd.zone.state] : "?");
            ack(phone, rsp);
            break;
        }

        case TL_CMD_ZONE_FAN_MODE: {
            switch (cmd.zone.num) {          /* 1-based */
            case 1: D[4] = 0xFC | cmd.zone.fanMode; break;
            case 2: D[4] = 0xF3 | (cmd.zone.fanMode << 2); break;
            case 3: D[4] = 0xCF | (cmd.zone.fanMode << 4); break;
            case 4: D[4] = 0x3F | (cmd.zone.fanMode << 6); break;
            case 5: D[5] = 0xFC | cmd.zone.fanMode; break;
            }
            sendToHcu(27, D);
            static char rsp[24];
            sprintf(rsp, "Z%d fan: %s", cmd.zone.num,
                    cmd.zone.fanMode == TL_FAN_MANUAL ? "manual" : "auto");
            ack(phone, rsp);
            break;
        }

        case TL_CMD_ZONE_FAN_PERCENT: {
            D[cmd.zone.num - 1] = cmd.zone.percent;   /* 0-based array index */
            sendToHcu(27, D);
            static char rsp[20];
            sprintf(rsp, "Z%d fan: %d%%", cmd.zone.num, cmd.zone.percent);
            ack(phone, rsp);
            break;
        }

        case TL_CMD_ZONE_DAY_SP: {
            int8_t sp = toC(cmd.zone.setpoint);
            D[cmd.zone.num - 1] = (uint8_t)(sp + 75);  /* 0-based array index */
            sendToHcu(25, D);
            static char rsp[24];
            sprintf(rsp, "Z%d day: %d\xb0%c", cmd.zone.num, sp, modem.tempUnit ? 'F' : 'C');
            ack(phone, rsp);
            break;
        }

        case TL_CMD_ZONE_NIGHT_SP: {
            int8_t sp = toC(cmd.zone.setpoint);
            D[cmd.zone.num - 1] = (uint8_t)(sp + 75);  /* 0-based array index */
            sendToHcu(25, D);
            static char rsp[24];
            sprintf(rsp, "Z%d night: %d\xb0%c", cmd.zone.num, sp, modem.tempUnit ? 'F' : 'C');
            ack(phone, rsp);
            break;
        }

        case TL_CMD_WARMUP:
            D[0] = 0x55; D[1] = 0xFD;
            sendToHcu(22, D);
            switch (cmd.warmupMode) {
            case TL_WARMUP_BURNER:  D[7] = 0xF0 | 1; sendToHcu(22, D); break;
            case TL_WARMUP_ELEMENT: D[7] = 0xF0 | 4; sendToHcu(22, D); break;
            case TL_WARMUP_BOTH:    D[7] = 0xF0 | 5; sendToHcu(22, D); break;
            }
            ack(phone, "Warmup started");
            break;

        case TL_CMD_UNIT:
            modem.tempUnit = cmd.unit;
            flash.writeSetup();
            modem.sendSms(phone, cmd.unit == TL_UNIT_F ? "Units: F" : "Units: C");
            break;

        case TL_CMD_FAULTREPORT:
            modem.faultReport = cmd.boolVal;
            flash.writeSetup();
            modem.sendSms(phone, cmd.boolVal ? "Fault report: ON" : "Fault report: OFF");
            break;

        case TL_CMD_SYSTIMER: {
            timberline.SystemTimeLimitHours = cmd.intVal;
            static char rsp[28];
            uint8_t t = cmd.intVal;
            if (t > 100 || t == 0) t = 100;
            if (t > 96) sprintf(rsp, "System timer: unlimited");
            else        sprintf(rsp, "System timer: %d h", t);
            D[0] = 3; D[6] = t;
            sendToHcu(19, D);
            ack(phone, rsp);
            break;
        }

        case TL_CMD_ACK:
            modem.cmdAck = cmd.boolVal;
            flash.writeSetup();
            modem.sendSms(phone, cmd.boolVal ? "Ack: ON" : "Ack: OFF");
            break;

        case TL_CMD_STATUS:
            timberline.sendStatus(phone);
            break;

        default:
            break;
        }
    }
}

void Timberline::init(void) {
    modem.onSmsReceived = onSmsReceived;
    stringTransfer.registerString(STRID_IMEI, modem.imei, sizeof(modem.imei));
}

/* ── sendStatus ──────────────────────────────────────────────────────── */

/* Append a null-terminated string, return new position */
static uint8_t apStr(char* buf, uint8_t n, const char* s) {
    while (*s && n < 139) buf[n++] = *s++;
    return n;
}

/* Append integer (signed), return new position */
static uint8_t apInt(char* buf, uint8_t n, int16_t v) {
    if (v < 0) { if (n < 139) buf[n++] = '-'; v = -v; }
    if (v >= 100) { if (n < 139) buf[n++] = '0' + v/100; }
    if (v >= 10)  { if (n < 139) buf[n++] = '0' + (v/10)%10; }
    if (n < 139) buf[n++] = '0' + v%10;
    return n;
}

/* Convert °C to display unit */
static int16_t dispTemp(int8_t c) {
    if (modem.tempUnit == 1 /* TL_UNIT_F */)
        return (int16_t)c * 9 / 5 + 32;
    return c;
}

static const char* UNIT_STR(void) { return modem.tempUnit == 1 ? "\xb0""F" : "\xb0""C"; }

static const char* stageStr(heaterStateIcon_t s) {
    switch (s) {
        case ignition:    return "IGN";
        case heating:     return "heat";
        case workOnPower: return "work";
        case blowing:     return "blow";
        default:          return "idle";
    }
}

void Timberline::sendStatus(const char* phone) {
    static char msg[141];
    uint8_t n = 0;
    const char* u = UNIT_STR();

    /* ── Burner ── */
    n = apStr(msg, n, "Burn:");
    if (HeaterButton) {
        n = apStr(msg, n, "on-");
        n = apStr(msg, n, stageStr(heaterStateIcon));
        n = apStr(msg, n, "-");
        n = apInt(msg, n, dispTemp((int8_t)heaters.Instances[mainHeaterNum].Tliquid));
        n = apStr(msg, n, u);
    } else {
        n = apStr(msg, n, "off");
    }
    msg[n++] = '\n';

    /* ── Element (skip if disabled) ── */
    if (!elementDisabled) {
        n = apStr(msg, n, "Elm:");
        n = apStr(msg, n, elementState ? "on" : "off");
        msg[n++] = '\n';
    }

    /* ── Zones (only connected) ── */
    for (uint8_t i = 0; i < ZONE_COUNT; i++) {
        if (zoneConnected[i] <= 0) continue;
        msg[n++] = 'Z';
        msg[n++] = '1' + i;
        msg[n++] = ' ';
        switch (zoneStates[i]) {
            case heat: n = apStr(msg, n, "heat"); break;
            case vent: n = apStr(msg, n, "vent"); break;
            default:   n = apStr(msg, n, "off");  break;
        }
        msg[n++] = ' ';
        n = apInt(msg, n, dispTemp(zoneCurrentTemp[i]));
        n = apStr(msg, n, u);
        n = apStr(msg, n, "->");
        n = apInt(msg, n, dispTemp((int8_t)zoneDaySetpoint[i]));
        n = apStr(msg, n, u);
        msg[n++] = '\n';
    }

    /* ── Tank, outdoor ── */
    n = apStr(msg, n, "Ttank:");
    n = apInt(msg, n, dispTemp(tankTemperature));
    n = apStr(msg, n, u); msg[n++] = '\n';

    n = apStr(msg, n, "Tout:");
    n = apInt(msg, n, dispTemp(outdoorTemperature));
    n = apStr(msg, n, u); msg[n++] = '\n';

    /* ── Voltage ── */
    int16_t v10 = (int16_t)(heaters.Instances[mainHeaterNum].Voltage * 10.0f + 0.5f);
    if (v10 >= 100) { if (n < 139) msg[n++] = '0' + v10/100; }
    if (n < 139) msg[n++] = '0' + (v10/10)%10;
    if (n < 139) msg[n++] = '.';
    if (n < 139) msg[n++] = '0' + v10%10;
    n = apStr(msg, n, "V");

    msg[n] = 0;
    modem.sendSms(phone, msg);
}
