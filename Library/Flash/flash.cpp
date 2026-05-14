/******************************************************************************
* ООО DD Inform
* Самара
* 
* Программисты: Клюев А.А.
* 
* 24.12.2024
* Описание:
*******************************************************************************/
/* Includes ------------------------------------------------------------------*/
#include "flash.h"
#include "heater.h"
#include "Converter.h"
#include "modem_handler.h"
#include "randomize.h"
#include "gprs.h"
//-----------------------------------------------------

Flash_C flash;
//-----------------------------------------------------
Flash_C::Flash_C (void)
{

}
//-----------------------------------------------------
void Flash_C::initialize(void)
{

}
//-----------------------------------------------------
void Flash_C::handler(void)
{

}
//-----------------------------------------------------
void Flash_C::writeSetup(void)
{
    uint32_t a, N;
    uint8_t x, array[512], i;
    
    FLASH_Unlock();
    for (a=0; a<512; a++){
        array[a] = *(__IO uint8_t*)(FLASH_SETUP_ADDR+a);
    }
    FLASH_EraseOnePage(FLASH_SETUP_ADDR);
    
    a = 0;
    for (i=0; i<16; i++){
        array[a++] = heater.device[heater.selectedDevice].serialNumber[i];
    }
    for (i=0; i<16; i++){
        array[a++] = updateToVersion[i];
    }
    for (i=0; i<5; i++){
        for (x=0; x<16; x++){
            array[a++] = gsm.phones[i][x];
        }
    }
    a++;//array[a++] = heater.device[heater.selectedDevice].setup.workDay;
    array[a++] = heater.device[heater.selectedDevice].setup.workTime/60;
    array[a++] = heater.device[heater.selectedDevice].setup.workTime%60;
    array[a++] = heater.device[heater.selectedDevice].setup.workUnlimited;
    array[a++] = core.timeZone;
    for (i=0; i<64; i++){
        array[a++] = addrServer[i];
    }
    
    for (i=0; i<8; i++){
        array[a++] = 0;//bluetooth.dev_name[i];
    }
    
    for (i=0; i<8; i++){
        array[a++] = 0;//bluetooth.keyAdapter[i];
    }
    
    for (i=0; i<16; i++){
        array[a++] = 0;//serialNumberModem[i];
    }
    
    if (gsm.isOnlySmsMode) array[a] = 0;
    else if (gprs.isInternetAuto) array[a] = 1;
    else if (gprs.is2GMode) array[a] = 2;
    else array[a] = 4;
    a++;
    
    array[a++] = service_id>>24;
    array[a++] = service_id>>16;
    array[a++] = service_id>>8;
    array[a++] = service_id;
    array[a++] = is_service_id;
    
    for (i=0; i<16; i++){
        array[a++] = heater.device[heater.selectedDevice].previousSerialNumber[i];
    }
    
    array[a++] = isReset;
    
    array[a++] = core.unixTime>>24;
    array[a++] = core.unixTime>>16;
    array[a++] = core.unixTime>>8;
    array[a++] = core.unixTime;
    
    for (i=0; i<3; i++){
        array[a++] = heater.device[heater.selectedDevice].timer[i].id>>24;
        array[a++] = heater.device[heater.selectedDevice].timer[i].id>>16;
        array[a++] = heater.device[heater.selectedDevice].timer[i].id>>8;
        array[a++] = heater.device[heater.selectedDevice].timer[i].id;
        array[a++] = heater.device[heater.selectedDevice].timer[i].mode;
        array[a++] = heater.device[heater.selectedDevice].timer[i].time>>8;
        array[a++] = heater.device[heater.selectedDevice].timer[i].time;
        array[a++] = heater.device[heater.selectedDevice].timer[i].week;
        array[a++] = heater.device[heater.selectedDevice].timer[i].isUnlimited;
        array[a++] = heater.device[heater.selectedDevice].timer[i].isChanged;
    }
    
    if (a > 512){
        while(1){
            
        }            
    }
    
    x = 0;
    for (a=0; a<511; a++){
        x += array[a];
    }
    array[511] = x;       // контрольная сумма
    
    for (a=0; a<512; a+=4){
        N = array[0+a];
        N += array[1+a]<<8;
        N += array[2+a]<<16;
        N += array[3+a]<<24;
        FLASH_ProgramWord(FLASH_SETUP_ADDR+a, N);
    }
    FLASH_Lock();
}
//-----------------------------------------------------
void Flash_C::readSetup(void)
{
    uint8_t x, array[512], a;
    uint16_t i;
    bool result = false;
    bool isNeedToSave = false;
    
    a = 0;
    for (i=0; i<511; i++){
        array[i] = *(__IO uint8_t*)(FLASH_SETUP_ADDR+i);
        a += array[i];
    }
    if (a == *(__IO uint8_t*)(FLASH_SETUP_ADDR+511)){
        result = true;
    }
    if (result == true){
        a = 0;
        for (i=0; i<16; i++){
            a++;//heater.device[heater.selectedDevice].serialNumber[i] = array[a++];
        }
        for (i=0; i<16; i++){
            updateToVersion[i] = array[a++];
        }
        for (i=0; i<5; i++){
            for (x=0; x<16; x++){
                gsm.phones[i][x] = array[a++];
            }
        }
        a++;//heater.device[heater.selectedDevice].setup.workDay = array[a++];
        heater.device[heater.selectedDevice].setup.workTime = array[a++]*60;
        heater.device[heater.selectedDevice].setup.workTime += array[a++];
        heater.device[heater.selectedDevice].setup.workUnlimited = array[a++];
        core.timeZone = array[a++];
        bool isBad = false;
        int count = 0;
        int addr = a;
        for (i=0; i<64; i++){
            addrServer[i] = array[a++];
            if (addrServer[i] == 0 || addrServer[i] == 255) break;
            if (addrServer[i] >= '0' && addrServer[i] <= '9'){
                
            }
            else if (addrServer[i] >= 'a' && addrServer[i] <= 'z'){
                count++;
            }
            else if (addrServer[i] >= 'A' && addrServer[i] <= 'Z'){
                count++;
            }
            else if (addrServer[i] == ':'){
                
            }
            else if (addrServer[i] == '/'){
                
            }
            else if (addrServer[i] == '\\'){
                
            }
            else if (addrServer[i] == '.'){
                
            }
            else if (addrServer[i] == '_'){
                
            }
            else if (addrServer[i] == '-'){
                
            }
            else{
                isBad = true;
            }
        }
        a = addr+64;
        if (addrServer[0] != 'h') isBad = true;
        if (addrServer[1] != 't') isBad = true;
        if (addrServer[2] != 't') isBad = true;
        if (addrServer[3] != 'p') isBad = true;
        if (addrServer[3] != 's') isBad = true;
        if (addrServer[4] != ':') isBad = true;
        if (addrServer[5] != '/') isBad = true;
        if (addrServer[6] != '/') isBad = true;
        if (addrServer[7] != 'r') isBad = true;
        if (addrServer[8] != 'd') isBad = true;
        if (addrServer[9] != 's') isBad = true;
        if (*(__IO uint8_t*) (0x803C007) == 255){
            if (addrServer[10] != 'b') isBad = true;
        }
        else{
            if (addrServer[10] != '2') isBad = true;
        }
        if (isBad || count < 5){
            // URL: http://rdsbeta.advers.ru/api/parameters/current_state/52310004141/
            //core.strToStr("http://rdsbeta.advers.ru/api/parameters/", addrServer);     // ok
            if (*(__IO uint8_t*) (0x803C007) == 255){
                Convert.strToStr("https://rdsbeta.advers.ru/api/", addrServer);     // error 715()
            }
            else{
                Convert.strToStr("https://rds2.advers.ru/api/", addrServer);     // error 715()
            }
            //core.strToStr("http://rds.advers.ru/parameters/", addrServer);
            //core.strToStr("https://time.com", addrServer);
        }
        
        char devName[9];
        for (i=0; i<8; i++){
            devName[i] = array[a++];
        }
        if (devName[0] != 'D'){
            Convert.strToStr("DC      ", devName);
        }
        devName[8] = 0;
        /*if (devName[2] == ' '){
            ///randomize.initialize();
            uint8_t j=2;
            for (int i=0; i<2550; i++){
                core.delayUs(500);
                uint32_t rnd = randomize.getValue();
                uint8_t val = 0;
                for (int k=0; k<4; k++){
                    val ^= (rnd & 0xff); 
                    rnd = rnd>>8;
                }
                if ((val >= '0' && val <= '9') || 
                    (val >= 'a' && val <= 'z') || 
                    (val >= 'A' && val <= 'Z')){
                    devName[j] = val;
                    j++;
                    if (j >= 8) break;
                }
            }
            bluetooth.nameToMac(devName);
            isNeedToSave = true;//flash.writeSetup();
        }
        else{
            bluetooth.nameToMac(devName);
        }
        
        for (i=0; i<8; i++){
            bluetooth.keyAdapter[i] = array[a++];
        }*/
        /*if (bluetooth.keyAdapter[0] == 0 || bluetooth.keyAdapter[0] == 0xff){
            ///randomize.initialize();
            for (int i=0; i<8; i++){
                core.delayUs(500);
                uint32_t rnd = randomize.getValue();
                uint8_t val = 0;
                for (int k=0; k<4; k++){
                    val ^= (rnd & 0xff); 
                    rnd = rnd>>8;
                }
                bluetooth.keyAdapter[i] = val;
            }
            isNeedToSave = true;//flash.writeSetup();
        }*/
        /*bluetooth.keyPhone[0] = bluetooth.keyAdapter[0]<<24;
        bluetooth.keyPhone[0] |= bluetooth.keyAdapter[1]<<16;
        bluetooth.keyPhone[0] |= bluetooth.keyAdapter[2]<<8;
        bluetooth.keyPhone[0] |= bluetooth.keyAdapter[3];
        
        bluetooth.keyPhone[1] = bluetooth.keyAdapter[4]<<24;
        bluetooth.keyPhone[1] |= bluetooth.keyAdapter[5]<<16;
        bluetooth.keyPhone[1] |= bluetooth.keyAdapter[6]<<8;
        bluetooth.keyPhone[1] |= bluetooth.keyAdapter[7];*/
        
        a += 8;
        
        for (i=0; i<16; i++){
            a++;//serialNumberModem[i] = array[a++];
        }
        
        if (array[a] == 0) gsm.isOnlySmsMode = true;
        else if (array[a] == 1){
            gsm.isOnlySmsMode = false;
            gprs.isInternetAuto = true;
        }
        else if (array[a] == 2){
            gsm.isOnlySmsMode = false;
            gprs.isInternetAuto = false;
            gprs.is2GMode = true;
        }
        else if (array[a] == 4){
            gsm.isOnlySmsMode = false;
            gprs.isInternetAuto = false;
            gprs.is2GMode = true;
        }
        else{
            gsm.isOnlySmsMode = false;
            gprs.isInternetAuto = true;
        }
        a++;

        service_id = array[a++]<<24;
        service_id |= array[a++]<<16;
        service_id |= array[a++]<<8;
        service_id |= array[a++];
        a++;
        is_service_id = (service_id != 0 && service_id !=  0xFFFFFFFF);
        //service_id = 0;
        //is_service_id = false;
        
        for (i=0; i<16; i++){
            heater.device[heater.selectedDevice].previousSerialNumber[i] = array[a++];
        }
        
        isReset = array[a++]==1;
        if (isReset){
            core.unixTime = array[a++]<<24;
            core.unixTime |= array[a++]<<16;
            core.unixTime |= array[a++]<<8;
            core.unixTime |= array[a++];
            
            for (i=0; i<3; i++){
                heater.device[heater.selectedDevice].timer[i].id = array[a++]<<24;
                heater.device[heater.selectedDevice].timer[i].id |= array[a++]<<16;
                heater.device[heater.selectedDevice].timer[i].id |= array[a++]<<8;
                heater.device[heater.selectedDevice].timer[i].id |= array[a++];
                
                heater.device[heater.selectedDevice].timer[i].mode = array[a++];
                heater.device[heater.selectedDevice].timer[i].time = array[a++]<<8;
                heater.device[heater.selectedDevice].timer[i].time |= array[a++];
                heater.device[heater.selectedDevice].timer[i].week = array[a++];
                heater.device[heater.selectedDevice].timer[i].isUnlimited = array[a++]==1;
                heater.device[heater.selectedDevice].timer[i].isChanged = array[a++]==1;
            }
        }

    }
    if (core.timeZone > 24) core.timeZone = 4;
    if (result == false) {
        if (*(__IO uint8_t*) (0x803C007) == 255){
            Convert.strToStr("https://rdsbeta.advers.ru/api/", addrServer);
        }
        else{
            Convert.strToStr("https://rds2.advers.ru/api/", addrServer);
        }
        i = 0;
        updateToVersion[i++] = '4';
        updateToVersion[i++] = '2';
        updateToVersion[i++] = '.';
        updateToVersion[i++] = '0';
        updateToVersion[i++] = '.';
        updateToVersion[i++] = '3';
        updateToVersion[i++] = '.';
        updateToVersion[i++] = '3';
        updateToVersion[i++] = '5';
        updateToVersion[i++] = 0;
        //flash.writeSetup();
        char devName[9];
        Convert.strToStr("DC      ", devName);
        devName[8] = 0;
        
        if (heater.device[heater.selectedDevice].serialNumber[0] < '0' || heater.device[heater.selectedDevice].serialNumber[0] > '9'){
            ///randomize.initialize();
            uint8_t j=0;
            for (int i=0; i<2550; i++){
                core.delayUs(500);
                uint32_t rnd = randomize.getValue();
                uint8_t val = 0;
                for (int k=0; k<4; k++){
                    val ^= (rnd & 0xff); 
                    rnd = rnd>>8;
                }
                if (val >= '0' && val <= '9'){
                    heater.device[heater.selectedDevice].serialNumber[j] = val;
                    j++;
                    if (j >= 11) break;
                }
            }
            heater.device[heater.selectedDevice].serialNumber[j++] = 0;
            //flash.writeSetup();
        }
        gsm.isOnlySmsMode = false;
        isNeedToSave = true;//flash.writeSetup();
    }
    
    if (isNeedToSave == true){
        flash.writeSetup();
    }
}
//-----------------------------------------------------
void Flash_C::writeSerial(void)
{
    uint32_t a, N;
    uint8_t array[16], i;
    
    FLASH_Unlock();
    FLASH_EraseOnePage(FLASH_SERIAL_ADDR);
    
    a = 0;
    for (i=0; i<16; i++){
        array[a++] = serialNumberModem[i];
    }
    
    for (a=0; a<16; a+=4){
        N = array[0+a];
        N += array[1+a]<<8;
        N += array[2+a]<<16;
        N += array[3+a]<<24;
        FLASH_ProgramWord(FLASH_SERIAL_ADDR+a, N);
    }
    FLASH_Lock();
}
//-----------------------------------------------------
void Flash_C::readSerial(void)
{
    uint16_t i;
    
    for (i=0; i<16; i++){
        serialNumberModem[i] = *(__IO uint8_t*)(FLASH_SERIAL_ADDR+i);
    }
}
//-----------------------------------------------------
uint8_t Flash_C::getHardwareVersion(void)
{
    uint8_t ver = *(__IO uint8_t*) (0x801C00A);
    return ver;
}
//-------------------------------------------------------------------
