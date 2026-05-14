/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __UNIX_TIME_H
#define __UNIX_TIME_H

/* Includes ------------------------------------------------------------------*/
#include "n32wb452.h"

/* Classes ------------------------------------------------------------------*/
class UnixTimeClass
{
    public:
        void handler(void);
        void getTimestamp(char* str);
        char* getTimestamp(void);
        void timeRegulate(void);
        bool isTimeOk(void);
        void timeUpdate(void);
        void initialize(void);    
    
        void timerToCal (unsigned long timer);
        unsigned long calToTimer (void);
    
        int year;
        char mon;
        char mday;
        char hour;
        char min;
        char sec;
        char wday;
        char str[64];
    
    private:
        void config(void);   
    
//        RTC_DateTypeDef RTC_DateStructure;
//        RTC_TimeTypeDef RTC_TimeStructure;
//        RTC_InitTypeDef RTC_InitStructure;
//        RTC_AlarmTypeDef  RTC_AlarmStructure;
        __IO uint32_t AsynchPrediv, SynchPrediv; 
        
};
extern UnixTimeClass unixTime;

#endif /* __UNIX_TIME_H */
