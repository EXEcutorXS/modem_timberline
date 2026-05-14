/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __CORE_H
#define __CORE_H

/* Includes ------------------------------------------------------------------*/
#include "n32wb452.h"

/* Defines ------------------------------------------------------------------*/
#define OUR_DAYS_UNIX_TIME 1672603200 // unix Sun Jan 01 2023 20:00:00 GMT+0000

/* Classes ------------------------------------------------------------------*/
class Core
{
    public:
        Core(void);
        void initialize(void);
        void handler(void);
        void enableWatchdog(void);
        void delayUs(uint32_t delay);
        void delayMs(uint32_t delay);
        void protectedFlash(void);
        void remapTable(void);
        void incTick(void);
        uint32_t getTick(void);
        void setTimer(uint32_t value);
        uint32_t getTimer(void);
        void resetTimer(void);

        uint16_t getDelta(int16_t valA, int16_t valB);
        bool isTimeOk(void);
        
        uint32_t unixTime;
        uint32_t counterMain;
        uint32_t timeZoneUnixTime;
        int8_t timeZone;
        
    
    private:
        void SetSysClockToPLL(uint32_t freq, uint8_t src);
        void SetSysClockToHSI(void);
        void SetSysClockToHSE(void);
        uint32_t msTick;
        uint32_t tickTimer;
        uint32_t counter;
};
extern Core core;

/* Info ------------------------------------------------------------------*/


#endif /* __CORE_H */
