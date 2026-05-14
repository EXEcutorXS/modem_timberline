/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __CONVERTER_H
#define __CONVERTER_H

/* Includes ------------------------------------------------------------------*/
#include "n32wb452.h"

/* Classes ------------------------------------------------------------------*/
class Converter_C
{
    public:
        Converter_C(void);
       
        uint8_t timerModeToStr(uint8_t mode, char* str);
        uint8_t timerTimeToStr(uint16_t time, char* str);
        uint8_t timerWeekToStr(uint8_t week, char* str);
//        bool strToTime(char* str);
    
        void cutStr(char *str, char* cut, uint16_t start, uint16_t len);
        uint32_t hexStrToLong(char* str);
        int32_t strToInt(char* str);
        uint32_t strToLong(char* str);
        double strToFloat(char* str);
        uint8_t intToStr(int32_t D, char* str);
        uint8_t longToStr(uint32_t D, char* str);
        uint8_t intToStrFix(int32_t D, uint8_t i, char* str);
        uint8_t floatToStr(float X, uint8_t N, char* str);
        uint16_t strToStr(char* from, char* to);
        int charToHex(uint8_t val, char* str);
        int byteToHex(uint8_t val, char* str);
        uint8_t longToHex(uint32_t val, char* str);
        uint8_t intToHex(int32_t val, char* str);
        uint8_t floatToHex(float val, char* str);
        uint8_t keyValToStr(char* key, float val, char* str);
        uint8_t keyValToStr(char* key, int32_t val, char* str);
        uint8_t keyValToStr(char* key, uint32_t val, char* str);
        uint8_t keyBoolToStr(char* key, bool val, char* str);
        uint8_t keyBoolToStrFull(char* key, bool val, char* str);
        uint8_t keyValToStr(char* key, char* val, char* str);
        uint8_t keyStrIntToStr(char* key, char* val, char* str);
        bool compareStr(char* one, char* two);
        bool containsStr(char* val, char* str);
//        uint16_t getDelta(int16_t valA, int16_t valB);
//        bool isTimeOk(void);
        
//        uint32_t unixTime;
//        
//        uint32_t timeZoneUnixTime;
//        int8_t timeZone;
    
    private:
//        uint32_t msTick;
//        uint32_t tickTimer;
        
};
extern Converter_C Convert;

/* Info ------------------------------------------------------------------*/


#endif /* __CORE_H */
