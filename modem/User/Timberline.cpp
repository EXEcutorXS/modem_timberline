#include "Timberline.h"
#include "Modem.h"
#include "StringTransfer.h"
#include "timberline_sms.h"
#include "button.h"
#include "led.h"
#include "flash.h"
#include "log.h"
#include "core.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/* Convert °C to display unit — setpoints are stored internally in °C
   (the SMS parser converts on the way in; see toCelsius() there). */
static int16_t dispTemp(int8_t c) {
    if (modem.tempUnit == 1 /* TL_UNIT_F */)
        return (int16_t)c * 9 / 5 + 32;
    return c;
}

static const char* UNIT_STR(void) { return modem.tempUnit == 1 ? "\xb0""F" : "\xb0""C"; }

#include "Heaters.h"
#include "unix_time.h"

Timberline timberline;

/* Minimal base64 encoder — no library in this codebase already provides
   one. Only used for the packed telemetry blob (see mqttTelemetryHandler);
   everything else this firmware publishes is plain decimal text. `out`
   must be at least 4*ceil(len/3)+1 bytes; returns the encoded length
   (excluding the null terminator). */
static const char BASE64_ALPHABET[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static int base64Encode(const uint8_t* data, int len, char* out) {
    int n = 0, i = 0;
    for (; i + 2 < len; i += 3) {
        uint32_t v = ((uint32_t)data[i] << 16) | ((uint32_t)data[i + 1] << 8) | data[i + 2];
        out[n++] = BASE64_ALPHABET[(v >> 18) & 0x3F];
        out[n++] = BASE64_ALPHABET[(v >> 12) & 0x3F];
        out[n++] = BASE64_ALPHABET[(v >> 6) & 0x3F];
        out[n++] = BASE64_ALPHABET[v & 0x3F];
    }
    int rem = len - i;
    if (rem == 1) {
        uint32_t v = (uint32_t)data[i] << 16;
        out[n++] = BASE64_ALPHABET[(v >> 18) & 0x3F];
        out[n++] = BASE64_ALPHABET[(v >> 12) & 0x3F];
        out[n++] = '=';
        out[n++] = '=';
    } else if (rem == 2) {
        uint32_t v = ((uint32_t)data[i] << 16) | ((uint32_t)data[i + 1] << 8);
        out[n++] = BASE64_ALPHABET[(v >> 18) & 0x3F];
        out[n++] = BASE64_ALPHABET[(v >> 12) & 0x3F];
        out[n++] = BASE64_ALPHABET[(v >> 6) & 0x3F];
        out[n++] = '=';
    }
    out[n] = '\0';
    return n;
}

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
            /* Wire bit keeps the old "onlySmsMode" polarity (1=SMS-only) —
               invert on the way into useInternet, which uses the opposite sense. */
            if (((D[1]>>0)&3)<2) modem.useInternet = !((D[1]>>0)&1);
            if (((D[1]>>2)&3)<2) modem.faultReport   = (D[1]>>2)&1;
            if (((D[1]>>4)&3)<2) modem.cmdAck         = (D[1]>>4)&1;
            if (((D[1]>>6)&3)<2) modem.tempUnit       = (D[1]>>6)&1;
            //D[2] = force2gOnly, D[3] = allowRoaming — plain bytes (0/1, 0xFF=без изменений)
            if (D[2] <= 1) modem.force2gOnly  = D[2];
            if (D[3] <= 1) modem.allowRoaming = D[3];
            /* No flash.writeSetup() here — dataActualizator.handler() already
               runs every tick, compares these same 6 fields old-vs-new, and
               writes flash exactly once if (and only if) something actually
               changed. Writing here too would mean every single reception of
               this message rewrites flash regardless of whether any value
               actually differs — fine while this only fires on user action,
               but a flash-wear risk if something starts sending it on a
               regular cadence. */
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
            if (((D[5]>>0)&3)<2) floorPumpState  = (D[5]>>0)&1;
            if (((D[5]>>2)&3)<2) enginePumpState = (D[5]>>2)&1;
        }
        if (D[0]==6)
        {
            if (D[1]!=0xFF) floorTemperature  = D[1]-75;
            if (D[2]!=0xFF) engineTemperature = D[2]-75;
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

/* Physical button: short press cycles burner -> element -> both -> burner...
   and turns all zones on to heat; long press turns everything off. */
static uint8_t buttonCycleState = 0;   /* 0=burner, 1=element, 2=both */

static void onButtonShortPress(void)
{
    buttonCycleState = (uint8_t)((buttonCycleState + 1) % 3);
    bool burnerOn  = (buttonCycleState == 0) || (buttonCycleState == 2);
    bool elementOn = (buttonCycleState == 1) || (buttonCycleState == 2);

    uint8_t D[8] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};
    D[0] = (uint8_t)(TL_ZONE_HEAT | (TL_ZONE_HEAT<<2) | (TL_ZONE_HEAT<<4) | (TL_ZONE_HEAT<<6));
    D[1] = (uint8_t)(0xFC | TL_ZONE_HEAT);
    D[7] = (uint8_t)(0xF0 | (burnerOn ? 1 : 0) | ((elementOn ? 1 : 0) << 2));
    sendToHcu(22, D);
}

static void onButtonLongPress(void)
{
    uint8_t Doff[8] = {0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00};
    sendToHcu(22, Doff);
    buttonCycleState = 0;   /* next short press starts fresh at "burner" */
}

/* Factory reset: hold the button 10s (onLongPress above already fired at
   1.5s on the way there, turning the heater off — harmless, arguably
   correct, ahead of wiping settings). Green stays off for the whole hold
   (see led.h) and only starts blinking once the button no longer needs to
   be read — by this point the reset is already committed. */
static void onButtonFactoryReset(void)
{
    flash.factoryReset();
    led.blinkGreenFactoryReset();
    NVIC_SystemReset();
}


/* Send reply only if device-command confirmations are enabled */
static void ack(const char* phone, const char* msg) {
    if (modem.cmdAck) modem.sendSms(phone, msg);
}

/* zoneConnected: 0=not connected, 1=dependent heater (has a fan), 2=defrost
   (always on, not user-controllable), 3=radiator (no fan). Both 1 and 3 are
   real user-controllable zones — only 0/2 are excluded here. Fan commands
   are still technically accepted for a type-3 zone (no separate gate for
   that) — the web UI just doesn't offer a fan control there, since there's
   no fan. */
static bool zoneControllable(uint8_t zoneNum) {
    if (zoneNum < 1 || zoneNum > ZONE_COUNT) return true;
    int8_t c = timberline.zoneConnected[zoneNum - 1];
    return c != 0 && c != 2;
}

/* True if name == "zn<1-5>/<prop>" — splits off the zone number and leaves
   prop pointing at the text after the slash (e.g. "state", "daySp"). Grouped
   this way so a client can subscribe to a whole zone at once ("zn1/#"). */
static bool zonePrefix(const char* name, uint8_t& zoneNum, const char*& prop) {
    if (name[0] != 'z' || name[1] != 'n') return false;
    if (name[2] < '1' || name[2] > '5' || name[3] != '/') return false;
    zoneNum = (uint8_t)(name[2] - '0');
    prop = name + 4;
    return true;
}

/* MQTT "cmd/desired/<name>" dispatch — reuses the exact sendToHcu()/bit-packing
   already used by the SMS command handler below, just triggered per-message
   instead of per comma-separated SMS segment (so D[] is reset fresh every call,
   unlike the SMS loop which shares one D[] across all commands in one SMS). */
static void onMqttCommandReceived(const char* name, const char* payload) {
    uint8_t D[8] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};
    int     ival;
    bool    bval = (payload[0] == '1');

    if (!strcmp(name, "btnHtr")) {
        D[7] = 0xFC | bval;
        sendToHcu(22, D);
    }
    else if (!strcmp(name, "btnElement")) {
        D[7] = (uint8_t)(0xF3 | (bval << 2));
        sendToHcu(22, D);
    }
    else if (!strcmp(name, "btnFloor")) {
        D[7] = (uint8_t)(0xCF | (bval << 4));
        sendToHcu(22, D);
    }
    else if (!strcmp(name, "btnEngine")) {
        D[7] = (uint8_t)(0x3F | (bval << 6));
        sendToHcu(22, D);
    }
    else if (!strcmp(name, "floorSp")) {
        ival = atoi(payload);
        if (ival < 2 || ival > 32) return;
        D[0] = 3; D[1] = (uint8_t)(ival + 75);
        sendToHcu(19, D);
    }
    else if (!strcmp(name, "engineSp")) {
        ival = atoi(payload);
        if (ival < 0 || ival > 80) return;
        D[0] = 3; D[3] = (uint8_t)(ival + 75);
        sendToHcu(19, D);
    }
    else if (!strcmp(name, "engineDur")) {
        /* Same PGN19/D[0]=3 sub-packet as floorSp/engineSp/etc, but this one
           spans 2 bytes (D[4]*256+D[5]) — matches how it's read back in
           ProcessCanMessage (PGN19 D[0]==3) and needs engineDurationMinutes
           to be a uint16_t, not uint8_t, to hold the full range. >1440 is
           treated as "unlimited" by convention (same idiom as
           SystemTimeLimitHours — see Timberline.h). */
        ival = atoi(payload);
        if (ival < 10 || ival > 1450) return;
        D[0] = 3; D[4] = (uint8_t)(ival >> 8); D[5] = (uint8_t)(ival & 0xFF);
        sendToHcu(19, D);
    }
    else if (!strcmp(name, "sysTimeLimit")) {
        ival = atoi(payload);
        if (ival < 1 || ival > 100) return;
        D[0] = 3; D[6] = (uint8_t)ival;
        sendToHcu(19, D);
    }
    else if (!strcmp(name, "floorHyst")) {
        /* Same PGN19/D[0]=3 sub-packet as floorSp/engineSp/sysTimeLimit —
           matches how it's read back in ProcessCanMessage (PGN1 D[0]==3, D[2]). */
        ival = atoi(payload);
        if (ival < 2 || ival > 10) return;
        D[0] = 3; D[2] = (uint8_t)ival;
        sendToHcu(19, D);
    }
    else if (!strcmp(name, "pumpForceDur")) {
        ival = atoi(payload);
        if (ival < 2 || ival > 60) return;
        D[0] = 3; D[7] = (uint8_t)ival;
        sendToHcu(19, D);
    }
    else if (!strcmp(name, "btnEco")) {
        /* PGN19, sub-packet 1 (D[0]=1), D[4] bits 4-5 — matches how it's read
           back in ProcessCanMessage (PGN1 D[0]==2, D[4]>>4). */
        D[0] = 1; D[4] = (uint8_t)(0xCF | (bval << 4));
        sendToHcu(19, D);
    }
    else if (!strcmp(name, "dayStartHr")) {
        /* PGN41, same byte layout as the read side (ProcessCanMessage case 41) —
           no sub-packet selector, same PGN number for read and write, like
           PGN22/25/26/27 already are. */
        ival = atoi(payload);
        if (ival < 0 || ival > 23) return;
        D[0] = (uint8_t)ival;
        sendToHcu(41, D);
    }
    else if (!strcmp(name, "dayStartMin")) {
        ival = atoi(payload);
        if (ival < 0 || ival > 59) return;
        D[1] = (uint8_t)ival;
        sendToHcu(41, D);
    }
    else if (!strcmp(name, "nightStartHr")) {
        ival = atoi(payload);
        if (ival < 0 || ival > 23) return;
        D[2] = (uint8_t)ival;
        sendToHcu(41, D);
    }
    else if (!strcmp(name, "nightStartMin")) {
        ival = atoi(payload);
        if (ival < 0 || ival > 59) return;
        D[3] = (uint8_t)ival;
        sendToHcu(41, D);
    }
    else if (!strcmp(name, "telemetryInt")) {
        /* Modem-local only — no HCU/CAN involved, just how often
           mqttTelemetryHandler() below publishes. Lives on modem, not here
           (see Modem::telemetryIntervalSec). Name is intentionally
           "telemetryInt", not the more obvious "telemetryInterval" — that's
           17 chars and silently truncates in Modem::mqttRxName[16] (only 15
           usable chars), which made this compare always fail. */
        ival = atoi(payload);
        if (ival < 5 || ival > 60) return;
        modem.telemetryIntervalSec = (uint8_t)ival;
    }
    else {
        uint8_t     zoneNum;
        const char* prop;
        if (!zonePrefix(name, zoneNum, prop)) return;
        if (!zoneControllable(zoneNum)) return;

        if (!strcmp(prop, "state")) {
            int st = !strcmp(payload, "off") ? TL_ZONE_OFF
                   : !strcmp(payload, "heat") ? TL_ZONE_HEAT
                   : !strcmp(payload, "vent") ? TL_ZONE_VENT : -1;
            if (st < 0) return;
            switch (zoneNum) {
            case 1: D[0] = (uint8_t)(0xFC | st); break;
            case 2: D[0] = (uint8_t)(0xF3 | (st << 2)); break;
            case 3: D[0] = (uint8_t)(0xCF | (st << 4)); break;
            case 4: D[0] = (uint8_t)(0x3F | (st << 6)); break;
            case 5: D[1] = (uint8_t)(0xFC | st); break;
            }
            sendToHcu(22, D);
        }
        else if (!strcmp(prop, "daySp")) {
            ival = atoi(payload);
            if (ival < 10 || ival > 32) return;
            D[zoneNum - 1] = (uint8_t)(ival + 75);
            sendToHcu(25, D);
        }
        else if (!strcmp(prop, "nightSp")) {
            ival = atoi(payload);
            if (ival < 10 || ival > 32) return;
            D[zoneNum - 1] = (uint8_t)(ival + 75);
            sendToHcu(26, D);
        }
        else if (!strcmp(prop, "fanPct")) {
            ival = atoi(payload);
            if (ival < 10 || ival > 100) return;
            D[zoneNum - 1] = (uint8_t)ival;
            sendToHcu(27, D);
        }
        else if (!strcmp(prop, "fanManual")) {
            switch (zoneNum) {
            case 1: D[5] = (uint8_t)(0xFC | bval); break;
            case 2: D[5] = (uint8_t)(0xF3 | (bval << 2)); break;
            case 3: D[5] = (uint8_t)(0xCF | (bval << 4)); break;
            case 4: D[5] = (uint8_t)(0x3F | (bval << 6)); break;
            case 5: D[6] = (uint8_t)(0xFC | bval); break;
            }
            sendToHcu(27, D);
        }
    }
}

static const char HELP_SMS[] =
    "Bad cmd\n"
    "burner/element/floor/engine on/off\n"
    "z1 off\n"
		"z1 heat\n"
		"z1 vent\n"
		"z1day 25\n"
		"z1 manual\n"
		"z1 auto\n"
    "warmup\n"
		"off\n"
		"status\n"
		"?\n";

static const char HELP_SMS_DE[] =
    "Falscher Befehl\n"
    "brenner/heizstab/fussboden/motor ein/aus\n"
    "z1 aus\n"
		"z1 heizen\n"
		"z1 lueften\n"
		"z1tag 25\n"
		"z1 manuell\n"
		"z1 auto\n"
    "aufwaermen\n"
		"aus\n"
		"zustand\n"
		"?\n";


/* Lightweight, non-cryptographic token for "getlink" — this MCU has no
   hardware RNG. Mixes the boot tick, a call counter and the device's own
   IMEI through two rounds of a simple string hash so repeated calls produce
   different values. Not resistant to a determined attacker who can guess
   timing — accepted as a known limitation: guessing it also requires
   already knowing the username, and a fresh "getlink" overwrites the
   retained token, invalidating any previous one. */
static void generateLinkToken(char* out, int outLen) {
    static uint32_t counter = 0;
    counter++;

    char mix[32];
    int  n = 0;
    uint32_t tick = core.getTick();
    for (int i = 0; i < 4; i++) mix[n++] = (char)(tick >> (i * 8));
    for (int i = 0; i < 4; i++) mix[n++] = (char)(counter >> (i * 8));
    for (int i = 0; modem.imei[i] && n < 30; i++) mix[n++] = modem.imei[i];

    uint32_t h1 = 5381, h2 = 52711;
    for (int i = 0; i < n; i++) {
        h1 = h1 * 33 ^ (uint8_t)mix[i];
        h2 = h2 * 33 ^ (uint8_t)mix[n - 1 - i];
    }

    static const char hex[] = "0123456789abcdef";
    int p = 0;
    for (int i = 7; i >= 0 && p < outLen - 1; i--) out[p++] = hex[(h1 >> (i * 4)) & 0xF];
    for (int i = 7; i >= 0 && p < outLen - 1; i--) out[p++] = hex[(h2 >> (i * 4)) & 0xF];
    out[p] = '\0';
}

static void onSmsReceived(const char* phone, const char* text) {
    /* Push the last-received SMS to the bus as soon as it arrives, regardless
       of auth outcome — useful for diagnosing rejected/garbled commands too. */
    stringTransfer.sendString(text,  STRID_LAST_REC_SMS_TEXT, can.idType, can.idAddress);
    stringTransfer.sendString(phone, STRID_LAST_REC_SMS_NUM,  can.idType, can.idAddress);

    uint8_t D[8]= {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};
    TlSmsParseResult result;
    tl_sms_parse(phone, text, modem.pin, modem.phones[0], &modem.phones[1], (TlTempUnit)modem.tempUnit, result);

    if (!result.authenticated) {
        log_info("SMS: auth failed\r\n");
        return;
    }

    /* Reply in German if the SMS used any German keyword; otherwise fall back
       to the persisted default language (see the "lang"/"sprache" command) —
       needed for replies that carry no language cue of their own, such as a
       parse-error help text or a bare "?" status request. */
    bool de = (result.lang == TL_LANG_DE) || (modem.language == TL_LANG_DE);

    bool hasUnknown = false;
    for (uint8_t e = 0; e < result.errCount; e++) {
        log_info("SMS parse error: ");
        log_info(result.errors[e]);
        log_info("\r\n");
        if (strncmp(result.errors[e], "unknown:", 8) == 0)
            hasUnknown = true;
    }
    if (hasUnknown)
        modem.sendSms(phone, de ? HELP_SMS_DE : HELP_SMS);

    for (uint8_t i = 0; i < result.cmdCount; i++) {
        const TlSmsCmd& cmd = result.cmds[i];
        switch (cmd.type) {

        case TL_CMD_PING:
            modem.sendSms(phone, "pong");
            break;

        case TL_CMD_RESET:
            modem.sendSms(phone, de ? "Neustart..." : "Resetting...");
            NVIC_SystemReset();
            break;

        case TL_CMD_ADMIN: {
            const char* p = cmd.phone[0] ? cmd.phone : phone;
            strncpy(modem.phones[0], p, 15);
            modem.phones[0][15] = '\0';
            /* flash write handled centrally by dataActualizator.handler(). */
            modem.sendSms(phone, de ? "Admin gesetzt" : "Admin set");
            break;
        }

        case TL_CMD_PHONE:
            if (cmd.phoneNum >= 1 && cmd.phoneNum <= 4) {
                const char* p = cmd.phone[0] ? cmd.phone : phone;
                strncpy(modem.phones[cmd.phoneNum], p, 15);
                modem.phones[cmd.phoneNum][15] = '\0';
                /* flash write handled centrally by dataActualizator.handler(). */
                modem.sendSms(phone, de ? "Telefon aktualisiert." : "Phone updated.");
            }
            break;

        case TL_CMD_SETPIN:
            memcpy(modem.pin, cmd.pin, 5);
            /* flash write handled centrally by dataActualizator.handler(). */
            modem.sendSms(phone, de ? "PIN aktualisiert." : "PIN updated.");
            break;

        case TL_CMD_OFF: {
            uint8_t Doff[8] = {0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00};
            sendToHcu(22, Doff);
            ack(phone, de ? "Alles aus" : "All off");
            break;
        }

        case TL_CMD_BURNER:
            D[7] = 0xFC | cmd.boolVal;
            sendToHcu(22, D);
            ack(phone, de ? (cmd.boolVal ? "Brenner: EIN" : "Brenner: AUS")
                        : (cmd.boolVal ? "Burner: ON"   : "Burner: OFF"));
            break;

        case TL_CMD_ELEMENT:
            D[7] = 0xF3 | (cmd.boolVal << 2);
            sendToHcu(22, D);
            ack(phone, de ? (cmd.boolVal ? "Heizstab: EIN" : "Heizstab: AUS")
                        : (cmd.boolVal ? "Element: ON"    : "Element: OFF"));
            break;

        case TL_CMD_FLOOR_TOGGLE:
            D[7] = 0xCF | (cmd.boolVal << 4);
            sendToHcu(22, D);
            ack(phone, de ? (cmd.boolVal ? "Boden: EIN" : "Boden: AUS")
                        : (cmd.boolVal ? "Floor: ON"   : "Floor: OFF"));
            break;

        case TL_CMD_FLOOR_SETPOINT: {
            D[0] = 3;
            D[1] = (uint8_t)(cmd.intVal + 75);
            sendToHcu(19, D);
            static char rsp[24];
            sprintf(rsp, de ? "Boden: %d%s" : "Floor: %d%s", dispTemp(cmd.intVal), UNIT_STR());
            ack(phone, rsp);
            break;
        }

        case TL_CMD_ENGINE_TOGGLE:
            D[7] = 0x3F | (cmd.boolVal << 6);
            sendToHcu(22, D);
            ack(phone, de ? (cmd.boolVal ? "Motor: EIN" : "Motor: AUS")
                        : (cmd.boolVal ? "Engine: ON"  : "Engine: OFF"));
            break;

        case TL_CMD_ENGINE_SETPOINT: {
            D[0] = 3;
            D[3] = (uint8_t)(cmd.intVal + 75);
            sendToHcu(19, D);
            static char rsp[24];
            sprintf(rsp, de ? "Motor: %d%s" : "Engine: %d%s", dispTemp(cmd.intVal), UNIT_STR());
            ack(phone, rsp);
            break;
        }

        case TL_CMD_ZONE_STATE: {
            if (!zoneControllable(cmd.zone.num)) { ack(phone, de ? "Nicht verfuegbar" : "Not available"); break; }
            static const char* zstate[]   = {"off","heat","vent"};
            static const char* zstateDe[] = {"aus","heizen","lueften"};
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
                    cmd.zone.state < 3 ? (de ? zstateDe[cmd.zone.state] : zstate[cmd.zone.state]) : "?");
            ack(phone, rsp);
            break;
        }

        case TL_CMD_ZONE_FAN_MODE: {
            if (!zoneControllable(cmd.zone.num)) { ack(phone, de ? "Nicht verfuegbar" : "Not available"); break; }
            switch (cmd.zone.num) {          /* 1-based */
            case 1: D[5] = 0xFC | cmd.zone.fanMode; break;
            case 2: D[5] = 0xF3 | (cmd.zone.fanMode << 2); break;
            case 3: D[5] = 0xCF | (cmd.zone.fanMode << 4); break;
            case 4: D[5] = 0x3F | (cmd.zone.fanMode << 6); break;
            case 5: D[6] = 0xFC | cmd.zone.fanMode; break;
            }
            sendToHcu(27, D);
            static char rsp[24];
            sprintf(rsp, de ? "Z%d Luefter: %s" : "Z%d fan: %s", cmd.zone.num,
                    cmd.zone.fanMode == TL_FAN_MANUAL ? (de ? "manuell" : "manual") : "auto");
            ack(phone, rsp);
            break;
        }

        case TL_CMD_ZONE_FAN_PERCENT: {
            if (!zoneControllable(cmd.zone.num)) { ack(phone, de ? "Nicht verfuegbar" : "Not available"); break; }
            D[cmd.zone.num - 1] = cmd.zone.percent;   /* 0-based array index */
            sendToHcu(27, D);
            static char rsp[20];
            sprintf(rsp, de ? "Z%d Luefter: %d%%" : "Z%d fan: %d%%", cmd.zone.num, cmd.zone.percent);
            ack(phone, rsp);
            break;
        }

        case TL_CMD_ZONE_DAY_SP: {
            if (!zoneControllable(cmd.zone.num)) { ack(phone, de ? "Nicht verfuegbar" : "Not available"); break; }
            D[cmd.zone.num - 1] = (uint8_t)(cmd.zone.setpoint + 75);  /* 0-based array index */
            sendToHcu(25, D);
            static char rsp[24];
            sprintf(rsp, de ? "Z%d Tag: %d%s" : "Z%d day: %d%s", cmd.zone.num, dispTemp(cmd.zone.setpoint), UNIT_STR());
            ack(phone, rsp);
            break;
        }

        case TL_CMD_ZONE_NIGHT_SP: {
            if (!zoneControllable(cmd.zone.num)) { ack(phone, de ? "Nicht verfuegbar" : "Not available"); break; }
            D[cmd.zone.num - 1] = (uint8_t)(cmd.zone.setpoint + 75);  /* 0-based array index */
            sendToHcu(25, D);
            static char rsp[24];
            sprintf(rsp, de ? "Z%d Nacht: %d%s" : "Z%d night: %d%s", cmd.zone.num, dispTemp(cmd.zone.setpoint), UNIT_STR());
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
            ack(phone, de ? "Aufwaermen gestartet" : "Warmup started");
            break;

        case TL_CMD_UNIT:
            modem.tempUnit = cmd.unit;
            /* flash write handled centrally by dataActualizator.handler() —
               see the comment on PGN60 case 60 above. */
            modem.sendSms(phone, de ? (cmd.unit == TL_UNIT_F ? "Einheit: F" : "Einheit: C")
                                   : (cmd.unit == TL_UNIT_F ? "Units: F"   : "Units: C"));
            break;

        case TL_CMD_FAULTREPORT:
            modem.faultReport = cmd.boolVal;
            /* flash write handled centrally by dataActualizator.handler(). */
            modem.sendSms(phone, de ? (cmd.boolVal ? "Fehlermeldung: EIN" : "Fehlermeldung: AUS")
                                   : (cmd.boolVal ? "Fault report: ON"   : "Fault report: OFF"));
            break;

        case TL_CMD_SYSTIMER: {
            timberline.SystemTimeLimitHours = cmd.intVal;
            static char rsp[28];
            uint8_t t = cmd.intVal;
            if (t > 100 || t == 0) t = 100;
            if (t > 96) sprintf(rsp, de ? "Systemzeit: unbegrenzt" : "System timer: unlimited");
            else        sprintf(rsp, de ? "Systemzeit: %d h" : "System timer: %d h", t);
            D[0] = 3; D[6] = t;
            sendToHcu(19, D);
            ack(phone, rsp);
            break;
        }

        case TL_CMD_ACK:
            modem.cmdAck = cmd.boolVal;
            /* flash write handled centrally by dataActualizator.handler(). */
            modem.sendSms(phone, de ? (cmd.boolVal ? "Bestaetigung: EIN" : "Bestaetigung: AUS")
                                   : (cmd.boolVal ? "Ack: ON"       : "Ack: OFF"));
            break;

        case TL_CMD_STATUS:
            timberline.sendStatus(phone, de);
            break;

        case TL_CMD_LANG: {
            modem.language = cmd.langArg;
            /* flash write handled centrally by dataActualizator.handler(). */
            bool langDe = (cmd.langArg == TL_LANG_DE);
            modem.sendSms(phone, langDe ? "Sprache: Deutsch" : "Language: English");
            break;
        }

        case TL_CMD_SERVER: {
            strncpy(modem.mqttBroker, cmd.strArg, sizeof(modem.mqttBroker) - 1);
            modem.mqttBroker[sizeof(modem.mqttBroker) - 1] = '\0';
            /* flash write handled centrally by dataActualizator.handler(). */
            modem.mqttForceReconnect();
            modem.sendSms(phone, de ? "MQTT-Server aktualisiert" : "MQTT server updated");
            break;
        }

        case TL_CMD_LOGIN: {
            strncpy(modem.mqttUsername, cmd.strArg, sizeof(modem.mqttUsername) - 1);
            modem.mqttUsername[sizeof(modem.mqttUsername) - 1] = '\0';
            /* flash write handled centrally by dataActualizator.handler(). */
            modem.mqttForceReconnect();
            modem.sendSms(phone, de ? "MQTT-Login aktualisiert" : "MQTT login updated");
            break;
        }

        case TL_CMD_PASSWORD: {
            strncpy(modem.mqttPassword, cmd.strArg, sizeof(modem.mqttPassword) - 1);
            modem.mqttPassword[sizeof(modem.mqttPassword) - 1] = '\0';
            /* flash write handled centrally by dataActualizator.handler(). */
            modem.mqttForceReconnect();
            modem.sendSms(phone, de ? "MQTT-Passwort aktualisiert" : "MQTT password updated");
            break;
        }

        case TL_CMD_INTERNET: {
            modem.useInternet = cmd.boolVal;
            /* flash write handled centrally by dataActualizator.handler(). */
            modem.sendSms(phone, de ? (cmd.boolVal ? "Internet: EIN" : "Internet: AUS")
                                   : (cmd.boolVal ? "Internet: ON"  : "Internet: OFF"));
            break;
        }

        case TL_CMD_ROAMING: {
            modem.allowRoaming = cmd.boolVal;
            /* flash write handled centrally by dataActualizator.handler(). */
            modem.sendSms(phone, de ? (cmd.boolVal ? "Roaming-Internet: EIN" : "Roaming-Internet: AUS")
                                   : (cmd.boolVal ? "Roaming internet: ON"  : "Roaming internet: OFF"));
            break;
        }

        case TL_CMD_FORCE_2G: {
            modem.force2gOnly = cmd.boolVal;
            /* flash write handled centrally by dataActualizator.handler().
               This field takes effect live now too — see doIdle()'s
               force2gOnly-change reactive block — not just on next
               reconnect/boot. */
            modem.sendSms(phone, de ? (cmd.boolVal ? "Nur 2G: EIN" : "Nur 2G: AUS")
                                   : (cmd.boolVal ? "2G only: ON"  : "2G only: OFF"));
            break;
        }

        case TL_CMD_APN: {
            strncpy(modem.apn, cmd.strArg, sizeof(modem.apn) - 1);
            modem.apn[sizeof(modem.apn) - 1] = '\0';
            /* flash write handled centrally by dataActualizator.handler().
               No forced teardown/reinit here — takes effect on the next
               connect attempt, which happens within 60s if internet is
               currently down (see doIdle()'s timerNet-gated retry). */
            modem.sendSms(phone, de ? (modem.apn[0] ? "APN aktualisiert" : "APN zurückgesetzt (automatisch)")
                                   : (modem.apn[0] ? "APN updated" : "APN reset (auto)"));
            break;
        }

        case TL_CMD_APN_USER: {
            strncpy(modem.apnUsername, cmd.strArg, sizeof(modem.apnUsername) - 1);
            modem.apnUsername[sizeof(modem.apnUsername) - 1] = '\0';
            /* flash write handled centrally by dataActualizator.handler(). */
            modem.sendSms(phone, de ? (modem.apnUsername[0] ? "APN-Benutzer aktualisiert" : "APN-Benutzer zurückgesetzt")
                                   : (modem.apnUsername[0] ? "APN username updated" : "APN username reset"));
            break;
        }

        case TL_CMD_APN_PASS: {
            strncpy(modem.apnPassword, cmd.strArg, sizeof(modem.apnPassword) - 1);
            modem.apnPassword[sizeof(modem.apnPassword) - 1] = '\0';
            /* flash write handled centrally by dataActualizator.handler(). */
            modem.sendSms(phone, de ? (modem.apnPassword[0] ? "APN-Passwort aktualisiert" : "APN-Passwort zurückgesetzt")
                                   : (modem.apnPassword[0] ? "APN password updated" : "APN password reset"));
            break;
        }

        case TL_CMD_GETLINK: {
            if (!modem.useInternet) {
                modem.sendSms(phone, de ? "Erst Internet aktivieren: internet 1"
                                       : "Enable internet first: internet 1");
                break;
            }
            char token[17];
            generateLinkToken(token, sizeof(token));
            modem.mqttPublish("linkToken", token);

            char* url = modem.connectionLink;
            int   n = 0;
            const int urlMax = (int)sizeof(modem.connectionLink) - 1;
            /* https on the default port (443, terminated by nginx in front of
               the web app) — not ":3000", which is the app's own plain-HTTP
               port and has no TLS cert bound to it. Also requires mqttBroker
               to hold a hostname (matching the cert's CN), not a bare IP. */
            const char* pre = "https://";
            while (*pre) url[n++] = *pre++;
            for (const char* p = modem.mqttBroker; *p && n < urlMax; ) url[n++] = *p++;
            const char* mid = "/go/";
            while (*mid && n < urlMax) url[n++] = *mid++;
            for (const char* p = modem.mqttUsername; *p && n < urlMax; ) url[n++] = *p++;
            if (n < urlMax) url[n++] = '/';
            for (const char* p = token; *p && n < urlMax; ) url[n++] = *p++;
            url[n] = '\0';

            modem.sendSms(phone, url);
            /* Persisting + pushing to the panel (STRID_CONNECTION_LINK) is
               handled centrally by dataActualizator.handler(), same as
               every other field it tracks — no direct sendString()/
               flash.writeSetup() call needed here. */
            break;
        }

        default:
            break;
        }
    }
}

void Timberline::init(void) {
    modem.onSmsReceived  = onSmsReceived;
    modem.onMqttCommand  = onMqttCommandReceived;
    button.onShortPress    = onButtonShortPress;
    button.onLongPress     = onButtonLongPress;
    button.onVeryLongPress = onButtonFactoryReset;
    stringTransfer.registerString(STRID_IMEI,           modem.imei,          sizeof(modem.imei));
    stringTransfer.registerString(STRID_PIN,             modem.pin,           sizeof(modem.pin));
    stringTransfer.registerString(STRID_ADMIN_PHONE,     modem.phones[0],     sizeof(modem.phones[0]));
    stringTransfer.registerString(STRID_TRUSTED_PHONE1,  modem.phones[1],     sizeof(modem.phones[1]));
    stringTransfer.registerString(STRID_TRUSTED_PHONE2,  modem.phones[2],     sizeof(modem.phones[2]));
    stringTransfer.registerString(STRID_TRUSTED_PHONE3,  modem.phones[3],     sizeof(modem.phones[3]));
    stringTransfer.registerString(STRID_TRUSTED_PHONE4,  modem.phones[4],     sizeof(modem.phones[4]));
    stringTransfer.registerString(STRID_LAST_REC_SMS_TEXT,  modem.cmgrBody,   sizeof(modem.cmgrBody));
    stringTransfer.registerString(STRID_LAST_REC_SMS_NUM,   modem.cmgrPhone,  sizeof(modem.cmgrPhone));
    stringTransfer.registerString(STRID_LAST_SENT_SMS_TEXT, modem.smsText,    sizeof(modem.smsText));
    stringTransfer.registerString(STRID_LAST_SENT_SMS_NUM,  modem.smsPhone,   sizeof(modem.smsPhone));
    stringTransfer.registerString(STRID_OPERATOR_NAME,   modem.operatorName,  sizeof(modem.operatorName));
    stringTransfer.registerString(STRID_OPERATOR_CODE,   modem.operatorCode,  sizeof(modem.operatorCode));
    stringTransfer.registerString(STRID_INTERNET_CHECK_URL, modem.internetCheckUrl, sizeof(modem.internetCheckUrl));
    stringTransfer.registerString(STRID_MQTT_BROKER,     modem.mqttBroker,    sizeof(modem.mqttBroker));
    stringTransfer.registerString(STRID_MODEM_LOGIN,     modem.mqttUsername,  sizeof(modem.mqttUsername));
    stringTransfer.registerString(STRID_MODEM_PASSWORD,  modem.mqttPassword,  sizeof(modem.mqttPassword));
    stringTransfer.registerString(STRID_IP_V4,           modem.ipAddress,     sizeof(modem.ipAddress));
    stringTransfer.registerString(STRID_CONNECTION_LINK, modem.connectionLink, sizeof(modem.connectionLink));
}

/* ── mqttActualizerHandler ─────────────────────────────────────────────
   "cmd/actual/<name>" mirrors the live value of each Control-section field,
   whatever last set it (MQTT command, SMS, physical button, panel dial) —
   these fields are themselves populated from the HCU's own CAN telemetry
   (see ProcessCanMessage case 22/25/26/27), so this is real confirmed state,
   not just an echo of the last command. Same diff/publish idiom as
   DataActualizator, called every tick from Work_C::handler().

   Every field is normally only republished on change (retain=1 on the
   broker side is what lets a *late* subscriber pick up the last value) —
   but a subscriber that's new to this particular retained-topic namespace
   (first boot, or the device re-pointed at a different mqttUsername via
   the "login" SMS command) has no retained value to inherit at all until
   something actually changes again. justConnected forces one full dump of
   every field right after (re)connecting, so a fresh client sees complete
   state immediately instead of just whatever happens to change next. */
void Timberline::mqttActualizerHandler(void) {
    static bool        wasConnected = false;
    static bool        prevHtr, prevElement, prevFloor, prevEngine;
    static uint8_t     prevFloorSp, prevEngineSp, prevSysTimeLimit;
    static uint16_t    prevEngineDur;
    static zoneState_t prevZoneState[ZONE_COUNT];
    static uint8_t     prevZoneDaySp[ZONE_COUNT], prevZoneNightSp[ZONE_COUNT], prevZoneFanPct[ZONE_COUNT];
    static bool        prevZoneFanManual[ZONE_COUNT];
    /* Hardware-presence mirror per zone — lets the web UI show only zones
       connected as a regular (1) or radiator (3) user-controllable zone,
       not unconnected (0) or defrost (2, always-on, not user-controllable).
       Same idiom as floorConnected/engineConnected above. */
    static int8_t      prevZoneConnected[ZONE_COUNT];
    /* Read-only mirrors — no known HCU write protocol yet, see mqtt-topic-scheme memory.
       StorageButton/mainHeaterNum deliberately excluded — not needed. */
    static bool        prevEco;
    static uint8_t     prevFloorHyst, prevPumpForceDur;
    static uint8_t     prevDayStartHr, prevDayStartMin, prevNightStartHr, prevNightStartMin;
    static uint8_t     prevTelemetryInterval;
    /* Hardware-presence mirrors, not user controls — lets the web UI hide
       btnFloor/btnEngine on systems that don't have that hardware. */
    static bool        prevFloorConnected, prevEngineConnected;
    /* Active HCU error codes (PGN 46, memcpy'd wholesale into errors[8] —
       see ProcessCanMessage) — rare event, so this is diff-published as a
       small readable CSV of the currently-nonzero codes, not folded into
       the 30s binary telemetry blob (see mqttTelemetryHandler below). */
    static uint8_t     prevErrorsSnapshot[8];

    char buf[8];

    bool justConnected = modem.mqttConnected && !wasConnected;
    wasConnected = modem.mqttConnected;

    if (HeaterButton  != prevHtr     || justConnected) { prevHtr     = HeaterButton;  modem.mqttPublish("btnHtr",     HeaterButton  ? "1" : "0"); }
    if (ElementButton != prevElement || justConnected) { prevElement = ElementButton; modem.mqttPublish("btnElement", ElementButton ? "1" : "0"); }
    if (FloorButton   != prevFloor   || justConnected) { prevFloor   = FloorButton;   modem.mqttPublish("btnFloor",   FloorButton   ? "1" : "0"); }
    if (EngineButton  != prevEngine  || justConnected) { prevEngine  = EngineButton;  modem.mqttPublish("btnEngine",  EngineButton  ? "1" : "0"); }

    /* floorConnected/engineConnected themselves always publish, including
       false — that's the "hardware not present" signal a subscriber needs.
       But the setpoint/hysteresis *values* behind a feature that isn't
       present are meaningless (leftover/default RAM content, not real
       configured state) — only publish those while the feature is
       actually there, and force one publish the moment it *becomes*
       present so a subscriber gets the real value right away instead of
       waiting for it to next change. */
    bool floorJustConnected  = floorConnected  && !prevFloorConnected;
    bool engineJustConnected = engineConnected && !prevEngineConnected;
    if (floorConnected  != prevFloorConnected  || justConnected) { prevFloorConnected  = floorConnected;  modem.mqttPublish("floorConnected",  floorConnected  ? "1" : "0"); }
    if (engineConnected != prevEngineConnected || justConnected) { prevEngineConnected = engineConnected; modem.mqttPublish("engineConnected", engineConnected ? "1" : "0"); }

    if (floorConnected && (floorSetpoint != prevFloorSp || justConnected || floorJustConnected)) {
        prevFloorSp = floorSetpoint;
        sprintf(buf, "%d", floorSetpoint);
        modem.mqttPublish("floorSp", buf);
    }
    if (engineConnected && (engineSetpoint != prevEngineSp || justConnected || engineJustConnected)) {
        prevEngineSp = engineSetpoint;
        sprintf(buf, "%d", engineSetpoint);
        modem.mqttPublish("engineSp", buf);
    }
    if (engineConnected && (engineDurationMinutes != prevEngineDur || justConnected || engineJustConnected)) {
        prevEngineDur = engineDurationMinutes;
        sprintf(buf, "%d", engineDurationMinutes);
        modem.mqttPublish("engineDur", buf);
    }
    if (SystemTimeLimitHours != prevSysTimeLimit || justConnected) {
        prevSysTimeLimit = SystemTimeLimitHours;
        sprintf(buf, "%d", SystemTimeLimitHours);
        modem.mqttPublish("sysTimeLimit", buf);
    }

    static const char* zoneStateStr[]        = {"off", "heat", "vent"};
    static const char* zoneTopicState[]       = {"zn1/state","zn2/state","zn3/state","zn4/state","zn5/state"};
    static const char* zoneTopicDaySp[]       = {"zn1/daySp","zn2/daySp","zn3/daySp","zn4/daySp","zn5/daySp"};
    static const char* zoneTopicNightSp[]     = {"zn1/nightSp","zn2/nightSp","zn3/nightSp","zn4/nightSp","zn5/nightSp"};
    static const char* zoneTopicFanPct[]      = {"zn1/fanPct","zn2/fanPct","zn3/fanPct","zn4/fanPct","zn5/fanPct"};
    static const char* zoneTopicFanManual[]   = {"zn1/fanManual","zn2/fanManual","zn3/fanManual","zn4/fanManual","zn5/fanManual"};
    static const char* zoneTopicConnected[]   = {"zn1/connected","zn2/connected","zn3/connected","zn4/connected","zn5/connected"};

    for (int i = 0; i < ZONE_COUNT; i++) {
        /* zoneConnected: 0=not present, 1=regular user zone, 2=defrost
           (always-on, not user-controllable), 3=radiator zone (also a real
           user-controllable zone, just a different heat-emitter type).
           Only 1 and 3 have meaningful state/setpoints/fan data worth
           publishing — 0 and 2 would otherwise leak stale/not-applicable
           values for a slot that isn't a real user zone. */
        bool isRegularZone = (zoneConnected[i] == 1 || zoneConnected[i] == 3);
        bool wasRegularZone = (prevZoneConnected[i] == 1 || prevZoneConnected[i] == 3);
        bool zoneJustConnected = isRegularZone && !wasRegularZone;

        if (zoneConnected[i] != prevZoneConnected[i] || justConnected) {
            prevZoneConnected[i] = zoneConnected[i];
            sprintf(buf, "%d", zoneConnected[i]);
            modem.mqttPublish(zoneTopicConnected[i], buf);
        }
        if (isRegularZone && (zoneStates[i] != prevZoneState[i] || justConnected || zoneJustConnected)) {
            prevZoneState[i] = zoneStates[i];
            modem.mqttPublish(zoneTopicState[i], zoneStates[i] <= vent ? zoneStateStr[zoneStates[i]] : "off");
        }
        if (isRegularZone && (zoneDaySetpoint[i] != prevZoneDaySp[i] || justConnected || zoneJustConnected)) {
            prevZoneDaySp[i] = zoneDaySetpoint[i];
            sprintf(buf, "%d", zoneDaySetpoint[i]);
            modem.mqttPublish(zoneTopicDaySp[i], buf);
        }
        if (isRegularZone && (zoneNightSetpoint[i] != prevZoneNightSp[i] || justConnected || zoneJustConnected)) {
            prevZoneNightSp[i] = zoneNightSetpoint[i];
            sprintf(buf, "%d", zoneNightSetpoint[i]);
            modem.mqttPublish(zoneTopicNightSp[i], buf);
        }
        if (isRegularZone && (zoneManualFanPercent[i] != prevZoneFanPct[i] || justConnected || zoneJustConnected)) {
            prevZoneFanPct[i] = zoneManualFanPercent[i];
            sprintf(buf, "%d", zoneManualFanPercent[i]);
            modem.mqttPublish(zoneTopicFanPct[i], buf);
        }
        if (isRegularZone && (zoneFanManualMode[i] != prevZoneFanManual[i] || justConnected || zoneJustConnected)) {
            prevZoneFanManual[i] = zoneFanManualMode[i];
            modem.mqttPublish(zoneTopicFanManual[i], zoneFanManualMode[i] ? "1" : "0");
        }
    }

    if (EcoButton != prevEco || justConnected) { prevEco = EcoButton; modem.mqttPublish("btnEco", EcoButton ? "1" : "0"); }
    if (floorConnected && (floorHysteresis != prevFloorHyst || justConnected || floorJustConnected)) {
        prevFloorHyst = floorHysteresis;
        sprintf(buf, "%d", floorHysteresis);
        modem.mqttPublish("floorHyst", buf);
    }
    if (pumpForceDurationMinutes != prevPumpForceDur || justConnected) {
        prevPumpForceDur = pumpForceDurationMinutes;
        sprintf(buf, "%d", pumpForceDurationMinutes);
        modem.mqttPublish("pumpForceDur", buf);
    }
    if (dayStartHour != prevDayStartHr || justConnected) {
        prevDayStartHr = dayStartHour;
        sprintf(buf, "%d", dayStartHour);
        modem.mqttPublish("dayStartHr", buf);
    }
    if (dayStartMinute != prevDayStartMin || justConnected) {
        prevDayStartMin = dayStartMinute;
        sprintf(buf, "%d", dayStartMinute);
        modem.mqttPublish("dayStartMin", buf);
    }
    if (nightStartHour != prevNightStartHr || justConnected) {
        prevNightStartHr = nightStartHour;
        sprintf(buf, "%d", nightStartHour);
        modem.mqttPublish("nightStartHr", buf);
    }
    if (nightStartMinute != prevNightStartMin || justConnected) {
        prevNightStartMin = nightStartMinute;
        sprintf(buf, "%d", nightStartMinute);
        modem.mqttPublish("nightStartMin", buf);
    }
    if (modem.telemetryIntervalSec != prevTelemetryInterval || justConnected) {
        prevTelemetryInterval = modem.telemetryIntervalSec;
        sprintf(buf, "%d", modem.telemetryIntervalSec);
        modem.mqttPublish("telemetryInt", buf);
    }

    bool errorsChanged = justConnected;
    for (int i = 0; i < 8; i++) {
        if (errors[i] != prevErrorsSnapshot[i]) { errorsChanged = true; break; }
    }
    if (errorsChanged) {
        for (int i = 0; i < 8; i++) prevErrorsSnapshot[i] = errors[i];
        char csv[32];
        int  cn = 0;
        bool first = true;
        for (int i = 0; i < 8; i++) {
            if (errors[i] == 0) continue;
            if (!first) csv[cn++] = ',';
            first = false;
            uint8_t v = errors[i];
            if (v >= 100) csv[cn++] = '0' + v / 100;
            if (v >= 10)  csv[cn++] = '0' + (v / 10) % 10;
            csv[cn++] = '0' + v % 10;
        }
        if (first) csv[cn++] = '0';  /* no active errors */
        csv[cn] = '\0';
        modem.mqttPublish("errors", csv);
    }
}

/* ── mqttTelemetryHandler ────────────────────────────────────────────────
   Fast-changing status fields (temperatures, fan speeds, pump states, ...)
   packed into one 20-byte struct and base64-encoded into a single
   "telemetry" topic, published unconditionally every
   modem.telemetryIntervalSec seconds (5-60, default 15, settable live via
   cmd/desired/telemetryInt — see onMqttCommandReceived() and
   Modem::telemetryIntervalSec) — unlike
   mqttActualizerHandler's fields above, these change too often for
   diff-publishing to save anything, and the same interval × ~40 separate
   flat topics would mean ~40× the AT+CMQTT command round-trips every cycle
   on top of ~5x the raw bytes (see mqtt-topic-scheme memory for the
   numbers this was sized against — traffic stays trivial either way at
   this interval, the real cost is AT round-trips sharing the modem's one
   state machine with SMS/CSQ/CREG/command-echo handling). Byte layout:
     0    tankTemperature (int8)
     1    heater Tliquid (int8)
     2-3  heater Voltage, tenths of a volt, big-endian (uint16 — same scale
          as the CAN wire encoding, D[0]*256+D[1])
     4    outdoorTemperature (int8)
     5    flags: bits 0-2 heaterStateIcon(0-3: idle/blowing/ignition/workOnPower), bit 3 elementState,
          bit 4 DomesticWaterFlow, bits 5-7 liquidLevel(0-6)
     6    pumpState[0..7] as a bitmask, bit i = pump i (PUMP1..AUX_PUMP3)
     7-11 zoneFanCurrentPwm[0..4]
     12-16 zoneCurrentTemp[0..4] (int8 each)
     17   floorTemperature (int8)
     18   engineTemperature (int8)
     19   flags2: bit 0 floorPumpState, bit 1 enginePumpState, bits 2-7 spare
   errors[] is deliberately NOT included here — see the CSV "errors" topic
   above; it's a rare event, not worth the binary/base64 treatment. */
void Timberline::mqttTelemetryHandler(void) {
    static uint32_t timerTelemetry = 0;
    uint32_t now = core.getTick();
    if (!modem.mqttConnected || (now - timerTelemetry) < (uint32_t)modem.telemetryIntervalSec * 1000) return;
    timerTelemetry = now;

    uint8_t raw[20];
    raw[0] = (uint8_t)tankTemperature;
    raw[1] = (uint8_t)(int8_t)heaters.Instances[mainHeaterNum].Tliquid;

    uint16_t voltage10 = (uint16_t)(heaters.Instances[mainHeaterNum].Voltage * 10.0f + 0.5f);
    raw[2] = (uint8_t)(voltage10 >> 8);
    raw[3] = (uint8_t)(voltage10 & 0xFF);

    raw[4] = (uint8_t)outdoorTemperature;

    raw[5] = (uint8_t)((heaterStateIcon & 0x07)
                      | ((elementState        ? 1 : 0) << 3)
                      | ((DomesticWaterFlow   ? 1 : 0) << 4)
                      | ((liquidLevel & 0x07) << 5));

    uint8_t pumps = 0;
    for (int i = 0; i < PUMP_COUNT; i++) if (pumpState[i]) pumps |= (uint8_t)(1 << i);
    raw[6] = pumps;

    for (int i = 0; i < ZONE_COUNT; i++) raw[7 + i]  = zoneFanCurrentPwm[i];
    for (int i = 0; i < ZONE_COUNT; i++) raw[12 + i] = (uint8_t)zoneCurrentTemp[i];

    raw[17] = (uint8_t)floorTemperature;
    raw[18] = (uint8_t)engineTemperature;
    raw[19] = (uint8_t)((floorPumpState  ? 1 : 0)
                       | ((enginePumpState ? 1 : 0) << 1));

    char b64[36];
    base64Encode(raw, sizeof(raw), b64);
    modem.mqttPublish("telemetry", b64);
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

static const char* stageStr(heaterStateIcon_t s, bool de) {
    switch (s) {
        case ignition:    return de ? "zuend" : "IGN";  /* ignition/warming combined */
        case workOnPower: return de ? "betr"  : "work";
        case blowing:     return de ? "blas"  : "blow";
        default:          return "idle";
    }
}

void Timberline::sendStatus(const char* phone, bool german) {
    static char msg[141];
    uint8_t n = 0;
    const char* u = UNIT_STR();
    bool de = german;

    /* ── Burner ── */
    n = apStr(msg, n, de ? "Brenner:" : "Burn:");
    if (HeaterButton) {
        n = apStr(msg, n, de ? "ein-" : "on-");
        n = apStr(msg, n, stageStr(heaterStateIcon, de));
        n = apStr(msg, n, "-");
        n = apInt(msg, n, dispTemp((int8_t)heaters.Instances[mainHeaterNum].Tliquid));
        n = apStr(msg, n, u);
    } else {
        n = apStr(msg, n, de ? "aus" : "off");
    }
    msg[n++] = '\n';

    /* ── Element (skip if disabled) ── */
    if (!elementDisabled) {
        n = apStr(msg, n, de ? "Heizst:" : "Elm:");
        n = apStr(msg, n, elementState ? (de ? "ein" : "on") : (de ? "aus" : "off"));
        msg[n++] = '\n';
    }

    /* ── Zones (only real user-controllable zones — dependent heater (1) or
       radiator (3) — not unconnected (0) and not defrost (2), which is
       always-on and not user-controllable) ── */
    for (uint8_t i = 0; i < ZONE_COUNT; i++) {
        if (zoneConnected[i] != 1 && zoneConnected[i] != 3) continue;
        msg[n++] = 'Z';
        msg[n++] = '1' + i;
        msg[n++] = ' ';
        switch (zoneStates[i]) {
            case heat: n = apStr(msg, n, de ? "heiz"  : "heat"); break;
            case vent: n = apStr(msg, n, de ? "lueft" : "vent"); break;
            default:   n = apStr(msg, n, de ? "aus"   : "off");  break;
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

    n = apStr(msg, n, de ? "Tauss:" : "Tout:");
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
