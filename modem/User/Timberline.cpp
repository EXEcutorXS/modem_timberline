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

/* Defined further down (near doCanRelay()) — forward-declared here so
   ProcessCanMessage()/maybeQueryNewDevice() near the top of this file can
   use it too. */
static uint32_t canId(uint16_t pgn, uint8_t toType, uint8_t toAddress);

/* Convert °C to display unit — setpoints are stored internally in °C
   (the SMS parser converts on the way in; see toCelsius() there). */
static int16_t dispTemp(int8_t c) {
    if (modem.config.tempUnit == 1 /* TL_UNIT_F */)
        return (int16_t)c * 9 / 5 + 32;
    return c;
}

static const char* UNIT_STR(void) { return modem.config.tempUnit == 1 ? "\xb0""F" : "\xb0""C"; }

#include "Heaters.h"
#include "unix_time.h"

Timberline timberline;

/* Совместимость с nations-bootloader: он читает метаданные из ADDRESS_CRC.
   Вся страница 0x0802A000..0x0802A800 зарезервирована под футер — код и
   таблица векторов начинаются со следующей страницы
   (MAIN_PROGRAM_START_ADDRESS=0x0802A800), линкеру не нужно ничего
   "обтекать" внутри кода (см. modemDragonfly.uvprojx IROM1 и main.h).
   lenMain=0x55555555 → "debug mode" → загрузчик запускает приложение без
   проверки CRC (тот же приём, что и в PU28-Timberline/User/Main/main.cpp). */
const uint8_t _CRCR[FLASH_PAGE_SIZE] __attribute__((at(ADDRESS_CRC))) =
{
    0x55, 0x55, 0x55, 0x55,
    0x55, 0x55,
    VERSION_1, VERSION_2, VERSION_3, VERSION_4,
    0x00
};

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
	uint8_t RecType = (msg->ExtId>>13)&127;
    uint8_t RecAddr = (msg->ExtId>>10)&7;
    uint8_t TransType = (msg->ExtId>>3)&127;
    uint8_t TransAddr = (msg->ExtId)&7;
    uint8_t* D = msg->Data;



    if ((TransType==126 && TransAddr==1) || TransType==125) timberline.connected = true;
    /* pgn==18 already delivers the sender's version directly (see case 18
       below) — no need to also query it. */
    if (pgn != 18) timberline.maybeQueryNewDevice(TransType, TransAddr);
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
				if (RecType!=can.idType && RecType!=127) return;
				if (RecAddr!=can.idAddress && RecAddr!=7) return;
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
            if (D[2]==0) *(__IO uint32_t*)BOOT_MAGIC_ADDR = BOOT_MAGIC_ENTER_BOOT; //войти в nations-bootloader
            if (D[2]==1) *(__IO uint32_t*)BOOT_MAGIC_ADDR = BOOT_MAGIC_ENTER_APP;  //остаться/вернуться в приложение
            NVIC_SystemReset();
            break;
        case 30: //Auto-register (panel "Авторегистрация" button)
            /* Only meaningful on a never-configured device — ignore silently
               if mqtt.username is already set, so an accidental/repeated
               press can't clobber a real account. The panel button itself
               only shows while modemData.newState.ConnectionLink is empty
               (see PU28's ModemInternetInfo), which normally keeps this from
               even being reachable once configured, but the guard belongs
               here too since CAN messages aren't trustworthy on their own. */
            if (modem.mqtt.username[0] == 0) modem.startAutoRegister();
            break;
        default:
            can.SendMessage((2<<20)+(TransType<<13)+(TransAddr<<10)+(can.idType<<3)+can.idAddress,D[0],D[1],0xFF,0xFF,0xFF,0xFF,0xFF,0xFF);
        }
        break;
    case 2: // reply to a PGN=1 [0,0] "who are you" broadcast (see work.cpp's
             // canBroadcast()) — used by devices that don't self-announce via
             // PGN=18 on their own (e.g. PU-28: control-panel firmware only
             // answers this query, see PU28-Timberline's messages.cpp case 0).
             // Version quad sits at D[2..5], not D[0..3] — D[0]/D[1] are just
             // an echo of the query's own D[0]/D[1].
        switch((D[0]<<8)+D[1])
        {
        case  0:
            timberline.recordSeenDevice(TransType, TransAddr, &D[2]);
            break;
        }
        break;
    case 60: //GSM settings write request, same sub-packet layout as the status broadcast
        if (D[0]==1) {
            //2 бита на bool (00=off,01=on,11=без изменений), как в canBroadcast()
            /* Wire bit keeps the old "onlySmsMode" polarity (1=SMS-only) —
               invert on the way into useInternet, which uses the opposite sense. */
            if (((D[1]>>0)&3)<2) modem.config.useInternet = !((D[1]>>0)&1);
            if (((D[1]>>2)&3)<2) modem.config.faultReport   = (D[1]>>2)&1;
            if (((D[1]>>4)&3)<2) modem.config.cmdAck         = (D[1]>>4)&1;
            if (((D[1]>>6)&3)<2) modem.config.tempUnit       = (D[1]>>6)&1;
            //D[2] = force2gOnly, D[3] = allowRoaming — plain bytes (0/1, 0xFF=без изменений)
            if (D[2] <= 1) modem.config.force2gOnly  = D[2];
            if (D[3] <= 1) modem.config.allowRoaming = D[3];
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
        timberline.recordSeenDevice(TransType, TransAddr, D);
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
        case 123: //  Bootloader — see doCanRelay()'s bootloader-detection poll
            canRelay.bootloaderSeen = true;
            memcpy(canRelay.bootloaderVersion, D, 4);
            break;
        }
        break;
    case 19:

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
			  hcuType = TransType;
        hcuAddress = TransAddr;
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
    case 105: //  Bootloader flash sub-protocol responses — see doCanRelay()
        if (TransType==123) {
            switch (D[0]) {
            case 1: //  echo of the set-address request (sub0)
                canRelay.setAddrEcho = ((uint32_t)D[1]<<24)|((uint32_t)D[2]<<16)|((uint32_t)D[3]<<8)|D[4];
                canRelay.setAddrGotResp = true;
                break;
            case 3: //  length+CRC of what's currently in the bootloader's RAM buffer (sub2 query)
                canRelay.checkLen = ((uint32_t)D[1]<<16)|((uint32_t)D[2]<<8)|D[3];
                canRelay.checkCrc = ((uint32_t)D[4]<<24)|((uint32_t)D[5]<<16)|((uint32_t)D[6]<<8)|D[7];
                canRelay.checkGotResp = true;
                break;
            case 5: //  result of the RAM->flash commit (sub4)
                canRelay.flashResult = D[1];
                canRelay.flashGotResp = true;
                break;
            case 7: //  result of the erase-memory request (sub6)
                canRelay.eraseResult = D[1];
                canRelay.eraseGotResp = true;
                break;
            }
        }
        break;
    }//switch(PGN)

}

void sendToHcu(uint16_t pgn,uint8_t* D)
{
    can.SendMessage(pgn<<20 | timberline.hcuType<<13 | timberline.hcuAddress<<10 | can.idType | can.idAddress,D[0],D[1],D[2],D[3],D[4],D[5],D[6],D[7]);
}

/* ── CAN firmware relay (OTA part 4) ─────────────────────────────────────
   MBC-2's own flash image starts at 0x08020000 (confirmed against its
   scatter file, HCU-Timberline2/Objects/hcu.sct: LR_IROM1 0x08020000
   0x0001FFC0 + LR_IROM2 0x0803FFC0 0x40 — together exactly the 128 KB
   region Modem::ota's staging buffer mirrors byte-for-byte from offset 0),
   and the bootloader always identifies itself as device type 123
   regardless of the app's own type (125 for MBC-2) — see OmniProtocol.pdf's
   device-type table and PGN=1/6/105/106 sections. Flash base address and
   erase-sector list are no longer hardcoded here — they come from
   modem.ota.flashBase/eraseSectors[], fetched from the target firmware's
   own profile on the server (see Modem::doFetchProfile()), so this same
   relay logic works for any device type, not just MBC-2. */
#define CAN_RELAY_FRAGMENT_SIZE 512

/* Always-visible USB debug output for the relay, same idiom as Modem.cpp's
   logOtaFail()/logOtaInfo() for the HTTP download side — log_error()/
   log_info() are unconditional (unlike log_at(), not gated by the current
   log-level debug command), and there's no printf here, so lines are
   hand-built via appendUint()/appendHex(). */
static int appendUint(char* buf, int n, uint32_t v) {
    char tmp[10]; int t = 0;
    if (v == 0) { buf[n++] = '0'; return n; }
    while (v > 0 && t < 10) { tmp[t++] = (char)('0' + v % 10); v /= 10; }
    while (t > 0) buf[n++] = tmp[--t];
    return n;
}
static int appendHex(char* buf, int n, uint32_t v) {
    buf[n++] = '0'; buf[n++] = 'x';
    for (int8_t shift = 28; shift >= 0; shift -= 4) {
        uint8_t nib = (uint8_t)((v >> shift) & 0xF);
        buf[n++] = (char)(nib < 10 ? ('0' + nib) : ('A' + nib - 10));
    }
    return n;
}
static void logRelayInfo(const char* msg) {
    log_info("[CANRELAY] ");
    log_info(msg);
    log_info("\r\n");
}
static void logRelayInfoNum(const char* label, uint32_t v) {
    static char buf[64];
    int n = 0;
    const char* pre = "[CANRELAY] ";
    while (*pre) buf[n++] = *pre++;
    for (const char* p = label; *p; p++) buf[n++] = *p;
    buf[n++] = '=';
    n = appendUint(buf, n, v);
    buf[n++] = '\r'; buf[n++] = '\n'; buf[n] = 0;
    log_info(buf);
}
/* extraIsHex: addresses/CRCs read far better in hex than the decimal
   appendUint() everywhere else in this codebase uses — worth the one-off
   inconsistency here since address/CRC mismatches are exactly what this
   is for diagnosing. */
static void logRelayFail(uint8_t stepNum, const char* reason, uint32_t extra, bool extraIsHex) {
    static char buf[112];
    int n = 0;
    const char* pre = "[CANRELAY] FAIL step ";
    while (*pre) buf[n++] = *pre++;
    n = appendUint(buf, n, stepNum);
    buf[n++] = ' '; buf[n++] = '(';
    for (const char* p = reason; *p && n < 90; p++) buf[n++] = *p;
    buf[n++] = '=';
    n = extraIsHex ? appendHex(buf, n, extra) : appendUint(buf, n, extra);
    buf[n++] = ')'; buf[n++] = '\r'; buf[n++] = '\n'; buf[n] = 0;
    log_error(buf);
}

static uint32_t canId(uint16_t pgn, uint8_t toType, uint8_t toAddress) {
    return ((uint32_t)pgn<<20) | ((uint32_t)toType<<13) | ((uint32_t)toAddress<<10)
         | ((uint32_t)can.idType<<3) | can.idAddress;
}

void Timberline::startCanRelay(uint8_t targetType, uint8_t targetAddress) {
    canRelay.targetType = targetType;
    canRelay.targetAddress = targetAddress;
    canRelay.startRequested = true;
}

/* Finds (or claims) this type+address's slot in seenDevices[] and, only if
   the version actually changed (or the slot is brand new), publishes
   "dev<type>_<addr>" = "<v1>.<v2>.<v3>.<v4>" over MQTT — deliberately NOT
   unconditional, since PGN=18 announcements repeat periodically for every
   device on the bus and modem.mqttPublish() has no diff-check of its own
   (see its comment in Modem.cpp); calling it on every single announcement
   would mark the same topic dirty forever, doing nothing but wasted AT
   traffic. Table full (more than SEEN_DEVICE_MAX distinct type+address
   pairs seen) — silently drop, same precedent as mqttPublish()'s own
   full-queue behavior. */
void Timberline::recordSeenDevice(uint8_t type, uint8_t address, const uint8_t* version) {
    SeenDevice* slot = 0;
    for (int i = 0; i < SEEN_DEVICE_MAX; i++) {
        if (seenDevices[i].active && seenDevices[i].type == type && seenDevices[i].address == address) {
            slot = &seenDevices[i];
            break;
        }
    }
    if (!slot) {
        for (int i = 0; i < SEEN_DEVICE_MAX; i++) {
            if (!seenDevices[i].active) { slot = &seenDevices[i]; break; }
        }
        if (!slot) return;
        slot->active  = true;
        slot->type    = type;
        slot->address = address;
        memset(slot->version, 0xFF, 4);  /* guarantees the memcmp below sees a change on first sight */
    }
    if (memcmp(slot->version, version, 4) == 0) return;
    memcpy(slot->version, version, 4);

    char topic[16];
    int  n = 0;
    const char* pre = "dev";
    while (*pre) topic[n++] = *pre++;
    n = appendUint(topic, n, type);
    topic[n++] = '_';
    n = appendUint(topic, n, address);
    topic[n] = 0;

    char value[20];
    int  vn = 0;
    vn = appendUint(value, vn, version[0]); value[vn++] = '.';
    vn = appendUint(value, vn, version[1]); value[vn++] = '.';
    vn = appendUint(value, vn, version[2]); value[vn++] = '.';
    vn = appendUint(value, vn, version[3]); value[vn] = 0;

    modem.mqttPublish(topic, value);
}

void Timberline::maybeQueryNewDevice(uint8_t type, uint8_t address) {
    if (type == can.idType && address == can.idAddress) return;   /* not our own frames */
    if (type == 127 && address == 7) return;                      /* broadcast sentinel, not a real sender */

    for (int i = 0; i < SEEN_DEVICE_MAX; i++) {
        if (seenDevices[i].active && seenDevices[i].type == type && seenDevices[i].address == address) return;
    }
    SeenDevice* slot = 0;
    for (int i = 0; i < SEEN_DEVICE_MAX; i++) {
        if (!seenDevices[i].active) { slot = &seenDevices[i]; break; }
    }
    if (!slot) return;
    slot->active  = true;
    slot->type    = type;
    slot->address = address;
    memset(slot->version, 0xFF, 4);

    can.SendMessage(canId(6, type, address), 0,18, 0xFF,0xFF,0xFF,0xFF,0xFF,0xFF);
}

/* Replays Modem::ota's staged flash image onto canRelay.targetType/
   targetAddress over CAN. Steps:
   0-1   switch the target device's app into the bootloader (PGN=1,
         [0,22,0], addressed to canRelay.targetType/targetAddress) and poll
         for it reappearing as device type 123 (PGN=6 [0,18] version
         request, answered by any device — see ProcessCanMessage's PGN=18
         case).
   2-3   erase each sector listed in modem.ota.eraseSectors[] in turn
         (PGN=105 sub6) — or, if the published firmware carried no sector
         list at all, one D[1]=255 "erase all" instead — fetched from the
         target firmware's own profile (see Modem::doFetchProfile()).
   10-18 per-fragment loop, one CAN_RELAY_FRAGMENT_SIZE (512 byte) fragment
         at a time: set the bootloader's write address (sub0/1), stream the
         fragment as raw 8-byte PGN=106 frames (one per doCanRelay() tick —
         never burst multiple sends in one call; see the SendMessage()
         call sites elsewhere in this file and StringTransfer.cpp for why:
         the CAN peripheral has a handful of TX mailboxes and SendMessage()
         doesn't block or retry, so blasting frames in a tight loop would
         silently drop the tail once mailboxes fill), verify what the
         bootloader actually received via a length+CRC query (sub2/3 — the
         CRC algorithm, crc += byte*170771; crc ^= (crc>>16)&0xFFFF, is
         confirmed from the real PC tool's C# source, not reconstructed),
         and only then commit it from the bootloader's RAM into its own
         flash (sub4/5). A verify mismatch or missing response at any point
         retries the whole fragment (re-send address included) rather than
         just the failed piece, up to CAN_RELAY_MAX_RETRIES times.
   20    switch MBC-2 back into its application (PGN=1, [0,22,1]). */
#define CAN_RELAY_MAX_RETRIES 5
void Timberline::doCanRelay(void) {
    static int8_t   step = 0;
    static uint32_t t = 0;
    static uint32_t phaseStart = 0;
    static uint16_t byteOffset = 0;     /* 0..CAN_RELAY_FRAGMENT_SIZE, within the current fragment */
    static uint32_t fragCrc = 0;
    static uint32_t lastFrameTick = 0;  /* paces the PGN=106 burst below — see its comment */
    static uint8_t  eraseIndex = 0;     /* index into modem.ota.eraseSectors[0..eraseSectorCount-1], see case 2's comment */

    if (canRelay.status != RELAY_STAGING) {
        if (canRelay.startRequested) {
            canRelay.startRequested = false;
            if (!modem.ota.stagedValid || modem.ota.stagedBytes == 0
                || (modem.ota.stagedBytes % CAN_RELAY_FRAGMENT_SIZE) != 0) {
                canRelay.status = RELAY_ERROR;
                return;
            }
            canRelay.status = RELAY_STAGING;
            canRelay.failed = false;
            canRelay.retries = 0;
            canRelay.fragment = 0;
            canRelay.fragmentTotal = (uint16_t)(modem.ota.stagedBytes / CAN_RELAY_FRAGMENT_SIZE);
            eraseIndex = 0;
            step = 0;
            logRelayInfo("start");
            logRelayInfoNum("fragmentTotal", canRelay.fragmentTotal);
            logRelayInfoNum("targetType", canRelay.targetType);
            logRelayInfoNum("targetAddress", canRelay.targetAddress);
        }
        return;
    }

    switch (step) {
    case 0:
        can.SendMessage(canId(1, canRelay.targetType, canRelay.targetAddress), 0,22,0, 0xFF,0xFF,0xFF,0xFF,0xFF);
        canRelay.bootloaderSeen = false;
        t = core.getTick();
        phaseStart = t;
        step = 1;
        logRelayInfo("switch-to-bootloader sent, waiting for type 123...");
        break;
    case 1:
        if (canRelay.bootloaderSeen) {
            /* Which PGN105/106 sequence this specific bootloader actually
               supports safely — not every one out there behaves the same,
               some are known unstable and must never be used for OTA (see
               Modem::lookupBootloaderAlgorithm()/bootloaderTable, fetched
               from "/bootloaders.txt" alongside the profile). Algorithm 2
               is the sequence implemented below (case 2 onward); anything
               else (0 = version not in the table, 1 = known unstable) is
               refused outright rather than attempting it and hoping. */
            uint8_t algo = modem.lookupBootloaderAlgorithm(canRelay.bootloaderVersion);
            if (algo != 2) {
                logRelayFail(1, algo == 0 ? "bootloader-algorithm-unknown" : "bootloader-algorithm-unsafe", algo, false);
                canRelay.failed = true; step = 30; break;
            }
            logRelayInfo("bootloader detected, algorithm 2");
            step = 2; break;
        }
        if ((core.getTick() - phaseStart) >= 15000) { logRelayFail(1, "bootloader-timeout", 0, false); canRelay.failed = true; step = 30; break; }
        if ((core.getTick() - t) >= 800) {
            can.SendMessage(canId(6, 123, 0), 0,18, 0xFF,0xFF,0xFF,0xFF,0xFF,0xFF);
            t = core.getTick();
        }
        break;
    case 2: {
        /* Erases every sector listed in modem.ota.eraseSectors[], one at a
           time — *unless* the profile published no list at all
           (eraseSectorCount==0, see Modem::doFetchProfile()), in which case
           this deliberately sends D[1]=255 ("erase all") instead, as a
           single command. 255 used to be avoided here entirely — confirmed
           in the real bootloader source (messages.cpp) it wipes sectors
           2-7, not just wherever the app happens to live — so it's only
           safe when whoever published this specific firmware file actually
           confirmed the broader erase is fine for that device/bootloader
           (that's what an *absent* sector list in the filename now means,
           see host/README.md). An explicit list stays the precise,
           narrowly-scoped way to erase — device-type/bootloader-version
           specific, not something this generic relay logic can assume on
           its own. */
        uint8_t sectorToErase = (modem.ota.eraseSectorCount == 0) ? 255 : modem.ota.eraseSectors[eraseIndex];
        can.SendMessage(canId(105, 123, 0), 6, sectorToErase, 0xFF,0xFF,0xFF,0xFF,0xFF,0xFF);
        canRelay.eraseGotResp = false;
        t = core.getTick();
        canRelay.retries = 0;
        step = 3;
        logRelayInfoNum("erase sector sent", sectorToErase);
        break;
    }
    case 3:
        if (canRelay.eraseGotResp) {
            if (canRelay.eraseResult != 0) {
                logRelayFail(3, "erase-result", canRelay.eraseResult, false);
                if (++canRelay.retries >= 3) { canRelay.failed = true; step = 30; }
                else step = 2;
                break;
            }
            logRelayInfoNum("erase ok, sector", (modem.ota.eraseSectorCount == 0) ? 255 : modem.ota.eraseSectors[eraseIndex]);
            eraseIndex++;
            if (modem.ota.eraseSectorCount != 0 && eraseIndex < modem.ota.eraseSectorCount) { step = 2; }
            else step = 10;
        } else if ((core.getTick() - t) >= 8000) {
            logRelayFail(3, "erase-timeout-retries", canRelay.retries, false);
            if (++canRelay.retries >= 3) { canRelay.failed = true; step = 30; }
            else step = 2;
        }
        break;

    /* ── per-fragment loop ────────────────────────────────────────────── */
    case 10:
        if (canRelay.fragment >= canRelay.fragmentTotal) { logRelayInfo("all fragments done"); step = 20; break; }
        canRelay.retries = 0;
        step = 11;
        break;
    case 11: {
        uint32_t addr = modem.ota.flashBase + (uint32_t)canRelay.fragment * CAN_RELAY_FRAGMENT_SIZE;
        can.SendMessage(canId(105, 123, 0), 0,
            (uint8_t)(addr>>24), (uint8_t)(addr>>16), (uint8_t)(addr>>8), (uint8_t)addr,
            0xFF,0xFF,0xFF);
        canRelay.setAddrGotResp = false;
        t = core.getTick();
        step = 12;
        break;
    }
    case 12: {
        uint32_t addr = modem.ota.flashBase + (uint32_t)canRelay.fragment * CAN_RELAY_FRAGMENT_SIZE;
        if (canRelay.setAddrGotResp) {
            if (canRelay.setAddrEcho != addr) {
                logRelayFail(12, "setaddr-echo-mismatch(want)", addr, true);
                logRelayFail(12, "setaddr-echo-mismatch(got)", canRelay.setAddrEcho, true);
                if (++canRelay.retries >= CAN_RELAY_MAX_RETRIES) { canRelay.failed = true; step = 30; }
                else step = 11;
                break;
            }
            byteOffset = 0;
            fragCrc = 0;
            step = 13;
        } else if ((core.getTick() - t) >= 500) {
            logRelayFail(12, "setaddr-timeout-frag", canRelay.fragment, false);
            if (++canRelay.retries >= CAN_RELAY_MAX_RETRIES) { canRelay.failed = true; step = 30; }
            else step = 11;
        }
        break;
    }
    case 13: {
        /* Paced ~3ms apart, not fired every tick — confirmed on real
           hardware that back-to-back sends with no gap easily outrun what
           a 250 kbit/s bus can actually drain, the hardware only has 3 TX
           mailboxes, and SendMessage() silently drops a frame instead of
           queuing/blocking once all 3 are still busy (see Can::txReady(),
           kept as a belt-and-suspenders check alongside the delay, not
           instead of it). 2ms plus the mailbox check alone still lost
           roughly 1 frame per fragment fairly often — bumped to 3ms and,
           more importantly, Work_C::handler() now holds off the modem's
           own other periodic CAN traffic (canBroadcast()/stringTransfer)
           for the whole relay so it isn't competing for the same 3
           mailboxes. */
        if ((core.getTick() - lastFrameTick) < 3) break;
        if (!can.txReady()) break;
        const uint8_t* src = (const uint8_t*)(FLASH_OTA_BUF_ADDR
            + (uint32_t)canRelay.fragment * CAN_RELAY_FRAGMENT_SIZE + byteOffset);
        for (uint8_t i = 0; i < 8; i++) {
            fragCrc += (uint32_t)src[i] * 170771U;
            fragCrc ^= (fragCrc >> 16) & 0xFFFFU;
        }
        can.SendMessage(canId(106, 123, 0), src[0],src[1],src[2],src[3],src[4],src[5],src[6],src[7]);
        lastFrameTick = core.getTick();
        byteOffset += 8;
        if (byteOffset >= CAN_RELAY_FRAGMENT_SIZE) step = 14;
        break;
    }
    case 14:
        /* Same ~3ms gap after the last PGN=106 frame before sub2 — mailboxes
           could still be draining right after the burst above. */
        if ((core.getTick() - lastFrameTick) < 3) break;
        if (!can.txReady()) break;
        can.SendMessage(canId(105, 123, 0), 2, 0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF);
        canRelay.checkGotResp = false;
        t = core.getTick();
        step = 15;
        break;
    case 15:
        if (canRelay.checkGotResp) {
            if (canRelay.checkLen != CAN_RELAY_FRAGMENT_SIZE || canRelay.checkCrc != fragCrc) {
                logRelayFail(15, "verify-len(got)", canRelay.checkLen, false);
                logRelayFail(15, "verify-crc(want)", fragCrc, true);
                logRelayFail(15, "verify-crc(got)", canRelay.checkCrc, true);
                if (++canRelay.retries >= CAN_RELAY_MAX_RETRIES) { canRelay.failed = true; step = 30; }
                else step = 11;
                break;
            }
            step = 16;
        } else if ((core.getTick() - t) >= 800) {
            logRelayFail(15, "verify-timeout-frag", canRelay.fragment, false);
            if (++canRelay.retries >= CAN_RELAY_MAX_RETRIES) { canRelay.failed = true; step = 30; }
            else step = 11;
        }
        break;
    case 16:
        can.SendMessage(canId(105, 123, 0), 4, 0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF);
        canRelay.flashGotResp = false;
        t = core.getTick();
        step = 17;
        break;
    case 17:
        if (canRelay.flashGotResp) {
            if (canRelay.flashResult != 0) {
                logRelayFail(17, "flash-result-frag", canRelay.fragment, false);
                if (++canRelay.retries >= CAN_RELAY_MAX_RETRIES) { canRelay.failed = true; step = 30; }
                else step = 11;
                break;
            }
            canRelay.fragment++;
            if ((canRelay.fragment % 16) == 0 || canRelay.fragment == canRelay.fragmentTotal)
                logRelayInfoNum("fragment ok, done", canRelay.fragment);
            step = 10;
        } else if ((core.getTick() - t) >= 2000) {
            logRelayFail(17, "flash-timeout-frag", canRelay.fragment, false);
            if (++canRelay.retries >= CAN_RELAY_MAX_RETRIES) { canRelay.failed = true; step = 30; }
            else step = 11;
        }
        break;

    case 20:
        can.SendMessage(canId(1, 123, 0), 0,22,1, 0xFF,0xFF,0xFF,0xFF,0xFF);
        logRelayInfo("switch-to-app sent");
        step = 30;
        break;

    case 30:
        canRelay.status = canRelay.failed ? RELAY_ERROR : RELAY_DONE;
        logRelayInfo(canRelay.failed ? "result=ERROR" : "result=DONE");
        step = 0;
        break;
    }
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
    if (modem.config.cmdAck) modem.sendSms(phone, msg);
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
           back in ProcessCanMessage (PGN19, case 19, D[0]==1, D[4]>>4). */
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
        modem.mqtt.telemetryIntervalSec = (uint8_t)ival;
    }
    else if (!strcmp(name, "otaStart")) {
        /* payload = "<type>:<version>", e.g. "125:125.0.0.16" — type is the
           target device's OmniProtocol type (first byte of its version
           quad, see recordSeenDevice()/seenDevices[]), version becomes the
           firmware/<type>/<version>/ path segment the modem downloads from
           (see Modem::startOta()/doOta()). Ignored while a download is
           already in progress — re-publish the same otaStart value again
           once the current run finishes (idle/done/error) to actually
           start a new one. */
        if (modem.ota.status == Modem::OTA_STAGING) return;
        const char* colon = strchr(payload, ':');
        if (!colon || colon == payload) return;
        uint8_t deviceType = (uint8_t)atoi(payload);
        modem.startOta(deviceType, colon + 1);
    }
    else if (!strcmp(name, "canRelayStart")) {
        /* payload = "<type>:<address>", e.g. "125:1" — the device to relay
           whatever's already staged+verified in the modem's own flash onto,
           over CAN (see Timberline::doCanRelay()). A deliberately separate
           trigger from otaStart: the user reviews the staged version
           (otaStaged, see Modem::ota.stagedVersion) before committing to
           actually flashing the device. Refuses while a relay or a
           download is already running, or nothing valid is staged. */
        if (timberline.canRelay.status == Timberline::RELAY_STAGING) return;
        if (modem.ota.status == Modem::OTA_STAGING) return;
        if (!modem.ota.stagedValid) return;
        const char* colon = strchr(payload, ':');
        if (!colon || colon == payload) return;
        uint8_t targetType = (uint8_t)atoi(payload);
        uint8_t targetAddress = (uint8_t)atoi(colon + 1);
        timberline.startCanRelay(targetType, targetAddress);
    }
    else if (!strcmp(name, "selfOtaApply")) {
        /* No payload needed — always applies whatever's in modem.selfOta
           right now, same "review what's staged, then commit separately"
           split as canRelayStart above. Refuses while a download is still
           running, or nothing valid is staged. Setting BOOT_MAGIC_UPDATE
           and resetting hands off to nations-bootloader's
           ApplySelfOtaImage() (see that project's main.cpp) — it erases
           and reflashes the app region from the self-OTA buffer, deriving
           a real footer from modem.selfOta's own meta record, then boots
           the result. A bad/incomplete image there is caught before
           anything is touched (meta checksum + whole-image CRC16 mismatch
           just cancels the update and boots the current app after the
           bootloader's usual grace period) or leaves the device parked in
           the bootloader's CAN-recovery loop if a flash write partway
           through actually failed — never a silent brick either way. */
        if (modem.ota.status == Modem::OTA_STAGING) return;
        if (!modem.selfOta.stagedValid) return;
        *(__IO uint32_t*)BOOT_MAGIC_ADDR = BOOT_MAGIC_UPDATE;
        NVIC_SystemReset();
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


/* generateLinkToken() and the "getlink" URL-building logic now live in
   Modem.cpp as Modem::publishLinkToken() — shared with auto-registration
   (see doAutoRegister()), which needs the exact same finishing step but has
   no phone number to reply to. */

static void onSmsReceived(const char* phone, const char* text) {
    /* Push the last-received SMS to the bus as soon as it arrives, regardless
       of auth outcome — useful for diagnosing rejected/garbled commands too. */
    stringTransfer.sendString(text,  STRID_LAST_REC_SMS_TEXT, can.idType, can.idAddress);
    stringTransfer.sendString(phone, STRID_LAST_REC_SMS_NUM,  can.idType, can.idAddress);

    uint8_t D[8]= {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};
    TlSmsParseResult result;
    tl_sms_parse(phone, text, modem.config.pin, modem.config.phones[0], &modem.config.phones[1], (TlTempUnit)modem.config.tempUnit, result);

    if (!result.authenticated) {
        log_info("SMS: auth failed\r\n");
        return;
    }

    /* Reply in German if the SMS used any German keyword; otherwise fall back
       to the persisted default language (see the "lang"/"sprache" command) —
       needed for replies that carry no language cue of their own, such as a
       parse-error help text or a bare "?" status request. */
    bool de = (result.lang == TL_LANG_DE) || (modem.config.language == TL_LANG_DE);

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
            strncpy(modem.config.phones[0], p, 15);
            modem.config.phones[0][15] = '\0';
            /* flash write handled centrally by dataActualizator.handler(). */
            modem.sendSms(phone, de ? "Admin gesetzt" : "Admin set");
            break;
        }

        case TL_CMD_PHONE:
            if (cmd.phoneNum >= 1 && cmd.phoneNum <= 4) {
                const char* p = cmd.phone[0] ? cmd.phone : phone;
                strncpy(modem.config.phones[cmd.phoneNum], p, 15);
                modem.config.phones[cmd.phoneNum][15] = '\0';
                /* flash write handled centrally by dataActualizator.handler(). */
                modem.sendSms(phone, de ? "Telefon aktualisiert." : "Phone updated.");
            }
            break;

        case TL_CMD_SETPIN:
            memcpy(modem.config.pin, cmd.pin, 5);
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
            modem.config.tempUnit = cmd.unit;
            /* flash write handled centrally by dataActualizator.handler() —
               see the comment on PGN60 case 60 above. */
            modem.sendSms(phone, de ? (cmd.unit == TL_UNIT_F ? "Einheit: F" : "Einheit: C")
                                   : (cmd.unit == TL_UNIT_F ? "Units: F"   : "Units: C"));
            break;

        case TL_CMD_FAULTREPORT:
            modem.config.faultReport = cmd.boolVal;
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
            modem.config.cmdAck = cmd.boolVal;
            /* flash write handled centrally by dataActualizator.handler(). */
            modem.sendSms(phone, de ? (cmd.boolVal ? "Bestaetigung: EIN" : "Bestaetigung: AUS")
                                   : (cmd.boolVal ? "Ack: ON"       : "Ack: OFF"));
            break;

        case TL_CMD_STATUS:
            timberline.sendStatus(phone, de);
            break;

        case TL_CMD_LANG: {
            modem.config.language = cmd.langArg;
            /* flash write handled centrally by dataActualizator.handler(). */
            bool langDe = (cmd.langArg == TL_LANG_DE);
            modem.sendSms(phone, langDe ? "Sprache: Deutsch" : "Language: English");
            break;
        }

        case TL_CMD_SERVER: {
            strncpy(modem.mqtt.broker, cmd.strArg, sizeof(modem.mqtt.broker) - 1);
            modem.mqtt.broker[sizeof(modem.mqtt.broker) - 1] = '\0';
            /* flash write handled centrally by dataActualizator.handler(). */
            modem.mqttForceReconnect();
            modem.sendSms(phone, de ? "MQTT-Server aktualisiert" : "MQTT server updated");
            break;
        }

        case TL_CMD_LOGIN: {
            strncpy(modem.mqtt.username, cmd.strArg, sizeof(modem.mqtt.username) - 1);
            modem.mqtt.username[sizeof(modem.mqtt.username) - 1] = '\0';
            /* flash write handled centrally by dataActualizator.handler(). */
            modem.mqttForceReconnect();
            modem.sendSms(phone, de ? "MQTT-Login aktualisiert" : "MQTT login updated");
            break;
        }

        case TL_CMD_PASSWORD: {
            strncpy(modem.mqtt.password, cmd.strArg, sizeof(modem.mqtt.password) - 1);
            modem.mqtt.password[sizeof(modem.mqtt.password) - 1] = '\0';
            /* flash write handled centrally by dataActualizator.handler(). */
            modem.mqttForceReconnect();
            modem.sendSms(phone, de ? "MQTT-Passwort aktualisiert" : "MQTT password updated");
            break;
        }

        case TL_CMD_INTERNET: {
            modem.config.useInternet = cmd.boolVal;
            /* flash write handled centrally by dataActualizator.handler(). */
            modem.sendSms(phone, de ? (cmd.boolVal ? "Internet: EIN" : "Internet: AUS")
                                   : (cmd.boolVal ? "Internet: ON"  : "Internet: OFF"));
            break;
        }

        case TL_CMD_ROAMING: {
            modem.config.allowRoaming = cmd.boolVal;
            /* flash write handled centrally by dataActualizator.handler(). */
            modem.sendSms(phone, de ? (cmd.boolVal ? "Roaming-Internet: EIN" : "Roaming-Internet: AUS")
                                   : (cmd.boolVal ? "Roaming internet: ON"  : "Roaming internet: OFF"));
            break;
        }

        case TL_CMD_FORCE_2G: {
            modem.config.force2gOnly = cmd.boolVal;
            /* flash write handled centrally by dataActualizator.handler().
               This field takes effect live now too — see doIdle()'s
               force2gOnly-change reactive block — not just on next
               reconnect/boot. */
            modem.sendSms(phone, de ? (cmd.boolVal ? "Nur 2G: EIN" : "Nur 2G: AUS")
                                   : (cmd.boolVal ? "2G only: ON"  : "2G only: OFF"));
            break;
        }

        case TL_CMD_APN: {
            strncpy(modem.internet.apn, cmd.strArg, sizeof(modem.internet.apn) - 1);
            modem.internet.apn[sizeof(modem.internet.apn) - 1] = '\0';
            /* flash write handled centrally by dataActualizator.handler().
               No forced teardown/reinit here — takes effect on the next
               connect attempt, which happens within 60s if internet is
               currently down (see doIdle()'s timerNet-gated retry). */
            modem.sendSms(phone, de ? (modem.internet.apn[0] ? "APN aktualisiert" : "APN zurückgesetzt (automatisch)")
                                   : (modem.internet.apn[0] ? "APN updated" : "APN reset (auto)"));
            break;
        }

        case TL_CMD_APN_USER: {
            strncpy(modem.internet.apnUsername, cmd.strArg, sizeof(modem.internet.apnUsername) - 1);
            modem.internet.apnUsername[sizeof(modem.internet.apnUsername) - 1] = '\0';
            /* flash write handled centrally by dataActualizator.handler(). */
            modem.sendSms(phone, de ? (modem.internet.apnUsername[0] ? "APN-Benutzer aktualisiert" : "APN-Benutzer zurückgesetzt")
                                   : (modem.internet.apnUsername[0] ? "APN username updated" : "APN username reset"));
            break;
        }

        case TL_CMD_APN_PASS: {
            strncpy(modem.internet.apnPassword, cmd.strArg, sizeof(modem.internet.apnPassword) - 1);
            modem.internet.apnPassword[sizeof(modem.internet.apnPassword) - 1] = '\0';
            /* flash write handled centrally by dataActualizator.handler(). */
            modem.sendSms(phone, de ? (modem.internet.apnPassword[0] ? "APN-Passwort aktualisiert" : "APN-Passwort zurückgesetzt")
                                   : (modem.internet.apnPassword[0] ? "APN password updated" : "APN password reset"));
            break;
        }

        case TL_CMD_GETLINK: {
            if (!modem.config.useInternet) {
                modem.sendSms(phone, de ? "Erst Internet aktivieren: internet 1"
                                       : "Enable internet first: internet 1");
                break;
            }
            modem.publishLinkToken();
            modem.sendSms(phone, modem.internet.connectionLink);
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
    stringTransfer.registerString(STRID_IMEI,           modem.network.imei,          sizeof(modem.network.imei));
    stringTransfer.registerString(STRID_PIN,             modem.config.pin,           sizeof(modem.config.pin));
    stringTransfer.registerString(STRID_ADMIN_PHONE,     modem.config.phones[0],     sizeof(modem.config.phones[0]));
    stringTransfer.registerString(STRID_TRUSTED_PHONE1,  modem.config.phones[1],     sizeof(modem.config.phones[1]));
    stringTransfer.registerString(STRID_TRUSTED_PHONE2,  modem.config.phones[2],     sizeof(modem.config.phones[2]));
    stringTransfer.registerString(STRID_TRUSTED_PHONE3,  modem.config.phones[3],     sizeof(modem.config.phones[3]));
    stringTransfer.registerString(STRID_TRUSTED_PHONE4,  modem.config.phones[4],     sizeof(modem.config.phones[4]));
    stringTransfer.registerString(STRID_LAST_REC_SMS_TEXT,  modem.network.cmgrBody,   sizeof(modem.network.cmgrBody));
    stringTransfer.registerString(STRID_LAST_REC_SMS_NUM,   modem.network.cmgrPhone,  sizeof(modem.network.cmgrPhone));
    stringTransfer.registerString(STRID_LAST_SENT_SMS_TEXT, modem.network.smsText,    sizeof(modem.network.smsText));
    stringTransfer.registerString(STRID_LAST_SENT_SMS_NUM,  modem.network.smsPhone,   sizeof(modem.network.smsPhone));
    stringTransfer.registerString(STRID_OPERATOR_NAME,   modem.network.operatorName,  sizeof(modem.network.operatorName));
    stringTransfer.registerString(STRID_OPERATOR_CODE,   modem.network.operatorCode,  sizeof(modem.network.operatorCode));
    stringTransfer.registerString(STRID_INTERNET_CHECK_URL, modem.internet.internetCheckUrl, sizeof(modem.internet.internetCheckUrl));
    stringTransfer.registerString(STRID_MQTT_BROKER,     modem.mqtt.broker,    sizeof(modem.mqtt.broker));
    stringTransfer.registerString(STRID_MODEM_LOGIN,     modem.mqtt.username,  sizeof(modem.mqtt.username));
    stringTransfer.registerString(STRID_MODEM_PASSWORD,  modem.mqtt.password,  sizeof(modem.mqtt.password));
    stringTransfer.registerString(STRID_IP_V4,           modem.internet.ipAddress,     sizeof(modem.internet.ipAddress));
    stringTransfer.registerString(STRID_CONNECTION_LINK, modem.internet.connectionLink, sizeof(modem.internet.connectionLink));
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
    /* One combined "cmd/actual/zn<N>" topic per zone now (see the zone loop
       below), not six — connected/state/daySp/nightSp/fanPct/fanManual
       packed into one underscore-delimited string, so this only needs to
       remember the last string actually published per zone to diff
       against, not one array per field. currentTemp/fanCurrent stay out
       of this — see the zone loop's own comment. */
    static char        prevZoneCombined[ZONE_COUNT][32];
    /* Read-only mirrors — no known HCU write protocol yet, see mqtt-topic-scheme memory.
       StorageButton/mainHeaterNum deliberately excluded — not needed. */
    static bool        prevEco;
    static uint8_t     prevFloorHyst, prevPumpForceDur;
    static uint8_t     prevDayStartHr, prevDayStartMin, prevNightStartHr, prevNightStartMin;
    static uint8_t     prevTelemetryInterval;
    /* MBC-2 OTA progress — see Modem::otaStatus/otaPage/otaPageTotal and
       doOta(). Read-only (no HCU write protocol — set purely by the modem
       itself as it downloads/stages), diff-published the same way as
       everything else here. */
    static Modem::OtaStatus prevOtaStatus;
    static uint16_t    prevOtaPage;
    /* What's actually sitting in the modem's flash OTA buffer right now —
       see Modem::ota.stagedValid/stagedVersion. Separate from otaStatus/
       otaProgress above (those describe a download in progress); this is
       what the web panel shows so the user can decide whether to relay it
       onto MBC-2 over CAN, independent of whatever the modem's last
       download run happened to be. */
    static char        prevStagedVersion[24];
    static bool        prevStagedValid;
    static uint8_t     prevStagedType;
    /* Same idea, for the modem's own separate self-OTA buffer — see
       Modem::selfOta and the "selfOtaStaged" publish below. */
    static char        prevSelfStagedVersion[24];
    static bool        prevSelfStagedValid;
    /* Hardware-presence mirrors, not user controls — lets the web UI hide
       btnFloor/btnEngine on systems that don't have that hardware. */
    static bool        prevFloorConnected, prevEngineConnected;
    /* Active HCU error codes (PGN 46, memcpy'd wholesale into errors[8] —
       see ProcessCanMessage) — rare event, so this is diff-published as a
       small readable CSV of the currently-nonzero codes, not folded into
       the 30s binary telemetry blob (see mqttTelemetryHandler below). */
    static uint8_t     prevErrorsSnapshot[8];

    char buf[8];

    bool justConnected = modem.mqtt.connected && !wasConnected;
    wasConnected = modem.mqtt.connected;

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

    static const char* zoneTopic[] = {"zn1","zn2","zn3","zn4","zn5"};

    /* One "cmd/actual/zn<N>" per zone: "<connected>_<state>_<daySp>_
       <nightSp>_<fanPct>_<fanManual>" — was six separate topics (30 across
       5 zones, the single biggest chunk of the MQTT_PUB_MAX table, see its
       comment in Modem.h), now one per zone (5 total). Deliberately does
       NOT include currentTemp/fanCurrent — those stay in the separate
       "telemetry" blob (see mqttTelemetryHandler()), which exists
       specifically so *fast-changing* per-zone values go out as 1 packet
       instead of many; folding them in here would mean every temperature
       tick republishes all 5 zone topics too, undoing exactly what
       telemetry buys (tried this first, reverted — see the memory/commit
       history if curious). state is the raw zoneState_t ordinal
       (0=off/1=heat/2=vent), not the old "off"/"heat"/"vent" string —
       app.js maps it back for display.

       connected always reflects reality even for a zone that isn't a real
       user zone (0=not present, 2=defrost) — state/setpoints/fan fields
       just hold whatever's already in RAM for those (typically stale/zero,
       never written by the HCU for a slot that doesn't exist) rather than
       a dedicated "N/A" sentinel; the web UI already gates all of that on
       `connected` before displaying it, same as before. */
    for (int i = 0; i < ZONE_COUNT; i++) {
        char combined[32];
        sprintf(combined, "%d_%d_%d_%d_%d_%d",
            zoneConnected[i],
            (int)zoneStates[i],
            zoneDaySetpoint[i],
            zoneNightSetpoint[i],
            zoneManualFanPercent[i],
            zoneFanManualMode[i] ? 1 : 0);
        if (strcmp(combined, prevZoneCombined[i]) != 0 || justConnected) {
            strncpy(prevZoneCombined[i], combined, sizeof(prevZoneCombined[i]) - 1);
            prevZoneCombined[i][sizeof(prevZoneCombined[i]) - 1] = 0;
            modem.mqttPublish(zoneTopic[i], combined);
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
    if (modem.mqtt.telemetryIntervalSec != prevTelemetryInterval || justConnected) {
        prevTelemetryInterval = modem.mqtt.telemetryIntervalSec;
        sprintf(buf, "%d", modem.mqtt.telemetryIntervalSec);
        modem.mqttPublish("telemetryInt", buf);
    }
    if (modem.ota.status != prevOtaStatus || justConnected) {
        prevOtaStatus = modem.ota.status;
        static const char* otaStatusStr[] = { "idle", "staging", "done", "error" };
        modem.mqttPublish("otaStatus", otaStatusStr[modem.ota.status]);
    }
    /* Only worth publishing progress while actually staging — otaPage is
       meaningless (and noisy to diff-publish) once idle/done/error. */
    if (modem.ota.status == Modem::OTA_STAGING
        && (modem.ota.page != prevOtaPage || justConnected)) {
        prevOtaPage = modem.ota.page;
        sprintf(buf, "%u/%u", modem.ota.page, modem.ota.pageTotal);
        modem.mqttPublish("otaProgress", buf);
    }
    if (modem.ota.stagedValid != prevStagedValid
        || strcmp(modem.ota.stagedVersion, prevStagedVersion) != 0
        || justConnected) {
        prevStagedValid = modem.ota.stagedValid;
        strncpy(prevStagedVersion, modem.ota.stagedVersion, sizeof(prevStagedVersion) - 1);
        prevStagedVersion[sizeof(prevStagedVersion) - 1] = 0;
        modem.mqttPublish("otaStaged", modem.ota.stagedValid ? modem.ota.stagedVersion : "");
    }
    /* Which device type the staged image (above) is for — needed by the web
       UI's per-device cards to know which device's "Flash" button to
       enable, now that OTA/relay target any device on the bus, not only
       MBC-2. */
    if (modem.ota.deviceType != prevStagedType || justConnected) {
        prevStagedType = modem.ota.deviceType;
        char tbuf[4]; int tn = appendUint(tbuf, 0, modem.ota.deviceType); tbuf[tn] = 0;
        modem.mqttPublish("otaStagedType", tbuf);
    }
    /* What's staged in the modem's own separate self-OTA buffer (see
       Modem::selfOta in Modem.h) — independent of otaStaged/otaStagedType
       above, which only ever describe the shared target-device buffer.
       No "type" companion needed here (unlike otaStagedType): this is
       always the modem's own firmware, never CAN-relayed. */
    if (modem.selfOta.stagedValid != prevSelfStagedValid
        || strcmp(modem.selfOta.stagedVersion, prevSelfStagedVersion) != 0
        || justConnected) {
        prevSelfStagedValid = modem.selfOta.stagedValid;
        strncpy(prevSelfStagedVersion, modem.selfOta.stagedVersion, sizeof(prevSelfStagedVersion) - 1);
        prevSelfStagedVersion[sizeof(prevSelfStagedVersion) - 1] = 0;
        modem.mqttPublish("selfOtaStaged", modem.selfOta.stagedValid ? modem.selfOta.stagedVersion : "");
    }
    /* The modem's own firmware version (VERSION_1..4, a compile-time
       constant — see Version.h, same values already broadcast over CAN's
       own PGN=18 in canBroadcast()) — never changes at runtime, so just
       publish it once per connection rather than diff-checking it. */
    if (justConnected) {
        char mv[20];
        int mn = 0;
        mn = appendUint(mv, mn, VERSION_1); mv[mn++] = '.';
        mn = appendUint(mv, mn, VERSION_2); mv[mn++] = '.';
        mn = appendUint(mv, mn, VERSION_3); mv[mn++] = '.';
        mn = appendUint(mv, mn, VERSION_4); mv[mn] = 0;
        modem.mqttPublish("modemVersion", mv);
    }
    /* MBC-2's own currently-running app version, as it actually reports it
       (PGN=18, see ProcessCanMessage's case 18 — MbcVersion[4] =
       VER_PRODUCT_TYPE.VER_VOLTAGE.VER_PRODUCT_SUBTYPE.VER_ASSEMBLAGE_NUMBER),
       not to be confused with otaStaged above (what's downloaded and
       verified in the modem's own flash, waiting to be relayed — could be
       a different version, or nothing at all). All-zero is treated as
       "never seen a broadcast yet" rather than a genuine "0.0.0.0" —
       VER_PRODUCT_TYPE is never actually 0 for a real device. */
    static uint8_t prevMbcVersion[4];
    if (memcmp(MbcVersion, prevMbcVersion, 4) != 0 || justConnected) {
        memcpy(prevMbcVersion, MbcVersion, 4);
        bool seen = MbcVersion[0] || MbcVersion[1] || MbcVersion[2] || MbcVersion[3];
        if (seen) {
            char verBuf[20];
            int vn = 0;
            vn = appendUint(verBuf, vn, MbcVersion[0]); verBuf[vn++] = '.';
            vn = appendUint(verBuf, vn, MbcVersion[1]); verBuf[vn++] = '.';
            vn = appendUint(verBuf, vn, MbcVersion[2]); verBuf[vn++] = '.';
            vn = appendUint(verBuf, vn, MbcVersion[3]); verBuf[vn] = 0;
            modem.mqttPublish("mbcVersion", verBuf);
        } else {
            modem.mqttPublish("mbcVersion", "");
        }
    }
    static Timberline::CanRelayStatus prevCanRelayStatus;
    static uint16_t prevCanRelayFragment;
    if (canRelay.status != prevCanRelayStatus || justConnected) {
        prevCanRelayStatus = canRelay.status;
        static const char* canRelayStatusStr[] = { "idle", "staging", "done", "error" };
        modem.mqttPublish("canRelayStatus", canRelayStatusStr[canRelay.status]);
    }
    if (canRelay.status == Timberline::RELAY_STAGING
        && (canRelay.fragment != prevCanRelayFragment || justConnected)) {
        prevCanRelayFragment = canRelay.fragment;
        sprintf(buf, "%u/%u", canRelay.fragment, canRelay.fragmentTotal);
        modem.mqttPublish("canRelayProgress", buf);
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
   modem.mqtt.telemetryIntervalSec seconds (5-60, default 15, settable live via
   cmd/desired/telemetryInt — see onMqttCommandReceived() and
   Modem::telemetryIntervalSec) — unlike
   mqttActualizerHandler's fields above, these change too often for
   diff-publishing to save anything, and the same interval × ~13 separate
   flat topics would mean ~13× the AT+CMQTT command round-trips every cycle
   on top of ~5x the raw bytes (see mqtt-topic-scheme memory for the
   numbers this was sized against — traffic stays trivial either way at
   this interval, the real cost is AT round-trips sharing the modem's one
   state machine with SMS/CSQ/CREG/command-echo handling). Per-zone current
   temp/fan speed live here (raw[7..16]), not in the "zn<N>" actual topics
   (mqttActualizerHandler's zone loop) — deliberately: those two are the
   fastest-changing fields of the bunch, and this blob exists specifically
   so fast-changing values go out as 1 packet on a timer instead of many;
   folding them into the 5 per-zone topics was tried and reverted — it
   meant every temperature tick republishing all 5 zone topics too,
   undoing exactly what this buys. Byte layout:
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
    if (!modem.mqtt.connected || (now - timerTelemetry) < (uint32_t)modem.mqtt.telemetryIntervalSec * 1000) return;
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
