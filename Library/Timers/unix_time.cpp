/******************************************************************************
* ООО Теплостар
* Самара
* 
* Программисты: Клюев А.А.
* 
* 25.12.2024
* Описание:
*******************************************************************************/
/* Includes ------------------------------------------------------------------*/
#include "core.h"
#include "unix_time.h"
#include <stdio.h>

/* Defines ------------------------------------------------------------------*/
#define SEC_A_DAY 86400
#define RTC_CLOCK_SOURCE_HSE
#define BKP_VALUE    0x32F0 
#define RTC_CLOCK_SOURCE_LSI

UnixTimeClass unixTime;
//-----------------------------------------------------
void UnixTimeClass::handler(void)
{
    static uint32_t timer;
    
    if ((core.getTick()-timer) >= 1000){
        timer = core.getTick();
        if (this->isTimeOk()) this->timeUpdate();
    }
}
//-----------------------------------------------------
void UnixTimeClass::getTimestamp(char* str)
{
    // 2024-12-25 08:21:03.740
    
    sprintf(str,"%u-%d02-%02d %02d:%02d:%02d",year,mon,mday,hour,min,sec);
}
//-----------------------------------------------------
char* UnixTimeClass::getTimestamp(void)
{
    // 2024-12-25 08:21:03.740
    sprintf(str,"%u-%d02-%02d %02d:%02d:%02d",year,mon,mday,hour,min,sec);
return str;
}
//-----------------------------------------------------
void UnixTimeClass::config(void)
{
  
}
//-----------------------------------------------------
void UnixTimeClass::timeRegulate(void)
{
    
}
//-----------------------------------------------------
bool UnixTimeClass::isTimeOk(void)
{
    return calToTimer() > OUR_DAYS_UNIX_TIME;
}
//-----------------------------------------------------
void UnixTimeClass::timeUpdate(void)
{
    UnixTime++;
	timerToCal(UnixTime);
}
//-----------------------------------------------------

void UnixTimeClass::initialize(void)
{
    
}
//-----------------------------------------------------
void UnixTimeClass::timerToCal (unsigned long timer)
{
    
    unsigned long time, a;
    char b;
    char c;
    char d;     

    time = timer%SEC_A_DAY;
    a = ((timer+43200)/(86400>>1)) + (2440587<<1) + 1;
    a>>=1;
    wday = a%7;
    a+=32044;
    b=(4*a+3)/146097;
    a=a-(146097*b)/4;
    c=(4*a+3)/1461;
    a=a-(1461*c)/4;
    d=(5*a+2)/153;
    mday=a-(153*d+2)/5+1;
    mon=d+3-12*(d/10);
    year=100*b+c-4800+(d/10);
    hour=time/3600;
    min=(time%3600)/60;
    sec=(time%3600)%60;
}
//-----------------------------------------------------
unsigned long UnixTimeClass::calToTimer (void)
{
	char a;
	int y;
	char m;
	unsigned long Uday;
	unsigned long time;
    
	a=((14-this->mon)/12);
	y=this->year+4800-a;
	m=this->mon+(12*a)-3;
	Uday=(this->mday+((153*m+2)/5)+365*y+(y/4)-(y/100)+(y/400)-32045)-2440588;
	time=Uday*86400;
	time+=this->sec+this->min*60+this->hour*3600;
    
    /*time = wday * SEC_A_DAY;
    time += hour*3600;
    time += min*60;
    time += sec;*/
    
	return time;
}
//-----------------------------------------------------
