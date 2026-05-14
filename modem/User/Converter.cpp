/******************************************************************************
* ООО Теплостар
* Самара
* 
* Программисты: Клюев А.А.
* 
* 08.08.2018
* Описание:
*******************************************************************************/
/* Includes ------------------------------------------------------------------*/
#include "core.h"
#include "Converter.h"
//#include "main.h"
//#include "gsm.h"
//#include "usart.h"
//#include "panel.h"
//#include "Bridge.h"
//#include "unix_time.h"
Converter_C Convert;
//-----------------------------------------------------
Converter_C::Converter_C(void)
{
    
}
//-----------------------------------------------------

void Converter_C::cutStr(char *str, char* cut, uint16_t start, uint16_t len)
{
    uint16_t a = 0;
    
    for (int i=start; i<(start+len); i++){
        cut[a++] = str[i];
        if (str[i] == 0) break;
    }
    cut[a] = 0;
}
//-----------------------------------------------------
uint32_t Converter_C::hexStrToLong(char* str)
{
    uint32_t num = 0;
    uint32_t i = 0;
    
    while (str[i] && ((str[i] >= '0' && str[i] <= '9') ||
        (str[i] >= 'a' && str[i] <= 'f') ||
        (str[i] >= 'A' && str[i] <= 'F'))){
            
        if (str[i] >= '0' && str[i] <= '9'){
            num = num * 16 + (str[i] - '0');
        }
        else if (str[i] >= 'a' && str[i] <= 'f'){
            num = num * 16 + (str[i] - 'a'+10);
        }
        else if (str[i] >= 'A' && str[i] <= 'F'){
            num = num * 16 + (str[i] - 'A'+10);
        }
        i++;
    }
    return num;
}
//-----------------------------------------------------
int32_t Converter_C::strToInt(char* str)
{
    int32_t num = 0;
    int32_t i = 0;
    bool isNegative = false;
    
    if (str[i] == '-'){
        isNegative = true;
        i++;
    }
    while (str[i] && (str[i] >= '0' && str[i] <= '9')){
        num = num * 10 + (str[i] - '0');
        i++;
    }
    if (isNegative) num = -1 * num;
    return num;
}
//-----------------------------------------------------
uint32_t Converter_C::strToLong(char* str)
{
    uint32_t num = 0;
    uint32_t i = 0;
    
    while (str[i] && (str[i] >= '0' && str[i] <= '9')){
        num = num * 10 + (str[i] - '0');
        i++;
    }
    return num;
}
//-----------------------------------------------------
double Converter_C::strToFloat(char* str)
{
    int32_t num = 0;
    double f=0, d=10;
    int32_t i = 0;
    bool isNegative = false;
    
    if (str[i] == '-'){
        isNegative = true;
        i++;
    }
    while (str[i] && (str[i] >= '0' && str[i] <= '9')){
        num = num * 10 + (str[i] - '0');
        i++;
    }
    if (isNegative) num = -1 * num;
    
    if (str[i] == '.'){
        i++;
        while (str[i] && (str[i] >= '0' && str[i] <= '9')){
            f += (str[i] - '0') / d;
            d = d * 10;
            i++;
        }
    }
    
    return num + f;
}
//-----------------------------------------------------
uint8_t Converter_C::intToStr(int32_t D, char* str)
{
    uint32_t a1, a2, a3, a4, a5, a6, a7, a8, a9;
	uint32_t c1, c2, c3, c4, c5, c6, c7, c8;
    uint8_t n;
	
    n=0;
    if (D < 0){
        *str++='-';
        n++;
        D = -D;
    }
	a1 = (uint8_t)(D/100000000);
	c1 = a1*100000000;
	a2 = (uint8_t)((D-c1)/10000000);
	c2 = a2*10000000;
	a3 = (uint8_t)((D-(c1+c2))/1000000);
	c3 = a3*1000000;
	a4 = (uint8_t)((D-(c1+c2+c3))/100000);
	c4 = a4*100000;
	a5 = (uint8_t)((D-(c1+c2+c3+c4))/10000);
	c5 = a5*10000;
	a6 = (uint8_t)((D-(c1+c2+c3+c4+c5))/1000);
	c6 = a6*1000;
	a7 = (uint8_t)((D-(c1+c2+c3+c4+c5+c6))/100);
	c7 = a7*100;
	a8 = (uint8_t)((D-(c1+c2+c3+c4+c5+c6+c7))/10);
	c8 = a8*10;
	a9 = D-(c1+c2+c3+c4+c5+c6+c7+c8);
	if (a1>0)
	{
		*str++=(a1+0x30);
        n++;
	}
	if (a2>0 || a1>0)
	{
		*str++=(a2+0x30);
        n++;
	}
	if (a3>0 || a2>0 || a1>0)
	{
		*str++=(a3+0x30);
        n++;
	}
	if (a4>0 || a3>0 || a2>0 || a1>0)
	{
		*str++=(a4+0x30);
        n++;
	}
	if (a5>0 || a4>0 || a3>0 || a2>0 || a1>0)
	{
		*str++=(a5+0x30);
        n++;
	}
	if (a6>0 || a5>0 || a4>0 || a3>0 || a2>0 || a1>0)
	{
		*str++=(a6+0x30);
        n++;
	}
	if (a7>0 || a6>0 || a5>0 || a4>0 || a3>0 || a2>0 || a1>0)
	{
		*str++=(a7+0x30);
        n++;
	}
	if (a8>0 || a7>0 || a6>0 || a5>0 || a4>0 || a3>0 || a2>0 || a1>0)
	{
		*str++=(a8+0x30);
        n++;
	}
	//if (a5>0 || a4>0 || a3>0 || a2>0 || a1>0)
	*str++=(a9+0x30);
    n++;
    
    *str = 0;
    return n;
}
//-----------------------------------------------------
uint8_t Converter_C::longToStr(uint32_t D, char* str)
{
    uint32_t a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11;
	uint32_t c1, c2, c3, c4, c5, c6, c7, c8, c9, c10;
    uint8_t n;
	
    n=0;
	a1 = (uint8_t)(D/10000000000);
	c1 = a1*10000000000;
	a2 = (uint8_t)((D-c1)/1000000000);
	c2 = a2*1000000000;
	a3 = (uint8_t)((D-(c1+c2))/100000000);
	c3 = a3*100000000;
	a4 = (uint8_t)((D-(c1+c2+c3))/10000000);
	c4 = a4*10000000;
	a5 = (uint8_t)((D-(c1+c2+c3+c4))/1000000);
	c5 = a5*1000000;
	a6 = (uint8_t)((D-(c1+c2+c3+c4+c5))/100000);
	c6 = a6*100000;
	a7 = (uint8_t)((D-(c1+c2+c3+c4+c5+c6))/10000);
	c7 = a7*10000;
	a8 = (uint8_t)((D-(c1+c2+c3+c4+c5+c6+c7))/1000);
	c8 = a8*1000;
    a9 = (uint8_t)((D-(c1+c2+c3+c4+c5+c6+c7+c8))/100);
	c9 = a9*100;
    a10 = (uint8_t)((D-(c1+c2+c3+c4+c5+c6+c7+c8+c9))/10);
	c10 = a10*10;
	a11 = D-(c1+c2+c3+c4+c5+c6+c7+c8+c9+c10);
	if (a1>0)
	{
		*str++=(a1+0x30);
        n++;
	}
	if (a2>0 || a1>0)
	{
		*str++=(a2+0x30);
        n++;
	}
	if (a3>0 || a2>0 || a1>0)
	{
		*str++=(a3+0x30);
        n++;
	}
	if (a4>0 || a3>0 || a2>0 || a1>0)
	{
		*str++=(a4+0x30);
        n++;
	}
	if (a5>0 || a4>0 || a3>0 || a2>0 || a1>0)
	{
		*str++=(a5+0x30);
        n++;
	}
	if (a6>0 || a5>0 || a4>0 || a3>0 || a2>0 || a1>0)
	{
		*str++=(a6+0x30);
        n++;
	}
	if (a7>0 || a6>0 || a5>0 || a4>0 || a3>0 || a2>0 || a1>0)
	{
		*str++=(a7+0x30);
        n++;
	}
	if (a8>0 || a7>0 || a6>0 || a5>0 || a4>0 || a3>0 || a2>0 || a1>0)
	{
		*str++=(a8+0x30);
        n++;
	}
    if (a9>0 || a8>0 || a7>0 || a6>0 || a5>0 || a4>0 || a3>0 || a2>0 || a1>0)
	{
		*str++=(a9+0x30);
        n++;
	}
    if (a10>0 || a9>0 || a8>0 || a7>0 || a6>0 || a5>0 || a4>0 || a3>0 || a2>0 || a1>0)
	{
		*str++=(a10+0x30);
        n++;
	}
	//if (a5>0 || a4>0 || a3>0 || a2>0 || a1>0)
	*str++=(a11+0x30);
    n++;
    
    *str = 0;
    return n;
}
//-----------------------------------------------------
uint8_t Converter_C::intToStrFix(int32_t D, uint8_t i, char* str)
{
    uint32_t a1, a2, a3, a4, a5, a6, a7, a8, a9;
	uint32_t c1, c2, c3, c4, c5, c6, c7, c8;
    uint8_t n;
	
    n=0;
    if (D < 0){
        *str++='-';
        n++;
        D = -D;
    }
	a1 = (uint8_t)(D/100000000);
	c1 = a1*100000000;
	a2 = (uint8_t)((D-c1)/10000000);
	c2 = a2*10000000;
	a3 = (uint8_t)((D-(c1+c2))/1000000);
	c3 = a3*1000000;
	a4 = (uint8_t)((D-(c1+c2+c3))/100000);
	c4 = a4*100000;
	a5 = (uint8_t)((D-(c1+c2+c3+c4))/10000);
	c5 = a5*10000;
	a6 = (uint8_t)((D-(c1+c2+c3+c4+c5))/1000);
	c6 = a6*1000;
	a7 = (uint8_t)((D-(c1+c2+c3+c4+c5+c6))/100);
	c7 = a7*100;
	a8 = (uint8_t)((D-(c1+c2+c3+c4+c5+c6+c7))/10);
	c8 = a8*10;
	a9 = D-(c1+c2+c3+c4+c5+c6+c7+c8);
	if (a1>0 || i>8)
	{
		*str++=(a1+0x30);
        n++;
	}
	if (a2>0 || a1>0 || i>7)
	{
		*str++=(a2+0x30);
        n++;
	}
	if (a3>0 || a2>0 || a1>0 || i>6)
	{
		*str++=(a3+0x30);
        n++;
	}
	if (a4>0 || a3>0 || a2>0 || a1>0 || i>5)
	{
		*str++=(a4+0x30);
        n++;
	}
	if (a5>0 || a4>0 || a3>0 || a2>0 || a1>0 || i>4)
	{
		*str++=(a5+0x30);
        n++;
	}
	if (a6>0 || a5>0 || a4>0 || a3>0 || a2>0 || a1>0 || i>3)
	{
		*str++=(a6+0x30);
        n++;
	}
	if (a7>0 || a6>0 || a5>0 || a4>0 || a3>0 || a2>0 || a1>0 || i>2)
	{
		*str++=(a7+0x30);
        n++;
	}
	if (a8>0 || a7>0 || a6>0 || a5>0 || a4>0 || a3>0 || a2>0 || a1>0 || i>1)
	{
		*str++=(a8+0x30);
        n++;
	}
	//if (a5>0 || a4>0 || a3>0 || a2>0 || a1>0)
	*str++=(a9+0x30);
    n++;
    
    //*str = 0;
    return n;
}
//-----------------------------------------------------
uint8_t Converter_C::floatToStr(float X, uint8_t N, char* str)
{
	uint32_t A;
    uint8_t n, m;
	
    m = 1;
	if (X < 0)
	{
		*str++=('-');
        m++;
		X = -X;
	}
	A = X;
	n = this->intToStr(A, str);
	str += n;
	*str++=('.');
	
	X = X-A;
	if (N == 1) A = (X+0.05f)*10.0f;
	if (N == 2) A = (X+0.005f)*100.0f;
	if (N == 3) A = (X+0.0005f)*1000.0f;
	if (N == 4) A = (X+0.00005f)*10000.0f;
	if (N == 5) A = (X+0.000005f)*100000.0f;
	if (N == 6) A = (X+0.0000005f)*1000000.0f;
	if (N == 7) A = (X+0.00000005f)*10000000.0f;
	if (N == 8) A = (X+0.000000005f)*100000000.0f;
	this->intToStrFix(A, N, str);
    
    return n + m + N;
}
//-----------------------------------------------------
uint16_t Converter_C::strToStr(char* from, char* to)
{
	uint16_t n = 0;
	
    if (*from == 0) return 0;
    
    for (uint8_t i=0; i<250; i++){
        *to = *from;
        from++;
        to++;
        n++;
        if (*from == 0){
            *to = 0;
            break;
        }
    }
    
    return n;
}
//-----------------------------------------------------
int Converter_C::charToHex(uint8_t val, char* str)
{
    *str = val;
    
    return 1;
}
//-----------------------------------------------------
int Converter_C::byteToHex(uint8_t val, char* str)
{
    char x;
    
    x = val >> 4;
    if (x <= 9) *str = '0' + x;
    else *str = 'A' + (x-10);
    
    str++;
    x = val & 0x0F;
    if (x <= 9) *str = '0' + x;
    else *str = 'A' + (x-10);
    
    return 2;
}
//-----------------------------------------------------
uint8_t Converter_C::longToHex(uint32_t v, char* str)
{
    uint8_t i;
    uint32_t val = v;
    
    for (i=0; i<4; i++){
        byteToHex(((uint8_t*)&val)[i], str);
        str += 2;
    }
    return 8;
}
//-----------------------------------------------------
uint8_t Converter_C::intToHex(int32_t v, char* str)
{
    uint8_t i;
    uint32_t val = v;
    
    for (i=0; i<4; i++){
        byteToHex(((uint8_t*)&val)[i], str);
        str += 2;
    }
    return 8;
}
//-----------------------------------------------------
uint8_t Converter_C::floatToHex(float val, char* str)
{
    uint8_t i;
    
    for (i=0; i<4; i++){
        byteToHex(((uint8_t*)&val)[i], str);
        str += 2;
    }
    return 8;
}
//-----------------------------------------------------
uint8_t Converter_C::keyValToStr(char* key, float val, char* str)
{
    uint8_t n = 4;
    char valStr[16];
    
    *str++ = '\"';
    while (*key != 0){
        *str++ = *key++;
        n++;
    }
    *str++ = '\"';
    *str++ = ':';
    
    uint8_t m = floatToStr(val, 1, valStr);
    for (uint8_t i=0; i<m; i++){
        *str++ = valStr[i];
        n++;
    }
    *str++ = ',';
    
    return n;
}
//-----------------------------------------------------
uint8_t Converter_C::keyValToStr(char* key, int32_t val, char* str)
{
    uint8_t n = 4;
    char valStr[16];
    
    *str++ = '\"';
    while (*key != 0){
        *str++ = *key++;
        n++;
    }
    *str++ = '\"';
    *str++ = ':';
    
    if (val == -127){
        uint8_t m = strToStr("null", valStr);
        for (uint8_t i=0; i<m; i++){
            *str++ = valStr[i];
            n++;
        }
    }
    else{
        uint8_t m = intToStr(val, valStr);
        for (uint8_t i=0; i<m; i++){
            *str++ = valStr[i];
            n++;
        }
    }
    *str++ = ',';
    
    return n;
}
//-----------------------------------------------------
uint8_t Converter_C::keyValToStr(char* key, uint32_t val, char* str)
{
    uint8_t n = 4;
    char valStr[16];
    
    *str++ = '\"';
    while (*key != 0){
        *str++ = *key++;
        n++;
    }
    *str++ = '\"';
    *str++ = ':';
    
    uint8_t m = longToStr(val, valStr);
    for (uint8_t i=0; i<m; i++){
        *str++ = valStr[i];
        n++;
    }
    *str++ = ',';
    
    return n;
}
//-----------------------------------------------------
uint8_t Converter_C::keyBoolToStr(char* key, bool val, char* str)
{
    uint8_t n = 4;
    
    *str++ = '\"';
    while (*key != 0){
        *str++ = *key++;
        n++;
    }
    *str++ = '\"';
    *str++ = ':';
    
    if (val) *str++ = '1';
    else *str++ = '0';
    n++;
    *str++ = ',';
    
    return n;
}
//-----------------------------------------------------
uint8_t Converter_C::keyBoolToStrFull(char* key, bool val, char* str)
{
    uint8_t n = 4;
    
    *str++ = '\"';
    while (*key != 0){
        *str++ = *key++;
        n++;
    }
    *str++ = '\"';
    *str++ = ':';
    
    if (val){
        *str++ = 't';
        n++;
        *str++ = 'r';
        n++;
        *str++ = 'u';
        n++;
        *str++ = 'e';
        n++;
    }
    else{
        *str++ = 'f';
        n++;
        *str++ = 'a';
        n++;
        *str++ = 'l';
        n++;
        *str++ = 's';
        n++;
        *str++ = 'e';
        n++;
    }
    *str++ = ',';
    
    return n;
}
//-----------------------------------------------------
uint8_t Converter_C::keyValToStr(char* key, char* val, char* str)
{
    uint8_t n = 4;  // без кавычек 2
    
    *str++ = '\"';
    while (*key != 0){
        *str++ = *key++;
        n++;
    }
    *str++ = '\"';
    *str++ = ':';
    
    if (*val != 0 && *val != 'n'){
        *str++ = '\"'; n++;
        while (*val != 0){
            *str++ = *val++;
            n++;
        }
        *str++ = '\"'; n++;
    }
    else{
        *str++ = 'n'; n++;
        *str++ = 'u'; n++;
        *str++ = 'l'; n++;
        *str++ = 'l'; n++;
        //n -= 2;
        
    }
    *str++ = ',';
    
    //*str++ = '\'';
    
    return n;
}
//-----------------------------------------------------
uint8_t Converter_C::keyStrIntToStr(char* key, char* val, char* str)
{
    uint8_t n = 4;  // без кавычек 2
    
    *str++ = '\"';
    while (*key != 0){
        *str++ = *key++;
        n++;
    }
    *str++ = '\"';
    *str++ = ':';
    
    ///*str++ = '\"';
    if (*val != 0 && *val != 'n'){
        while (*val != 0){
            *str++ = *val++;
            n++;
        }
    }
    else{
        *str++ = 'n'; n++;
        *str++ = 'u'; n++;
        *str++ = 'l'; n++;
        *str++ = 'l'; n++;
    }
    ///*str++ = '\"';
    *str++ = ',';
    
    
    return n;
}
//-----------------------------------------------------
bool Converter_C::compareStr(char* one, char* two)
{
	for (uint8_t i=0; i<254; i++){
        if (*one != *two) return false;
        one++;
        two++;
        if (*one == 0 || *two == 0){
            break;
        }
    }
    
    return true;//*one == *two;
}
//-----------------------------------------------------
bool Converter_C::containsStr(char* val, char* str)
{
    uint8_t a = 0, b = 0, n = 0;
    bool res = false;
    
	for (uint8_t i=0; i<254; i++){
        if (str[a] == 0) break;
        if (str[a] == val[b]){
            b++;
            a++;
            if (n == 0) n = a;
            if (val[b] == 0){
                res = true;
                break;
            }
        }
        else{
            b = 0;
            if (n == 0){
                a++;
            }
            else{
                a = n;
            }
            n = 0;
        }
    }
    return res;
}

//-----------------------------------------------------
uint8_t Converter_C::timerModeToStr(uint8_t mode, char* str)
{
    uint8_t n=0;
    
    if (mode){
        n += strToStr("On", &str[n]);
    }
    else{
        n += strToStr("Off", &str[n]);
    }
    return n;
}
//-----------------------------------------------------
uint8_t Converter_C::timerTimeToStr(uint16_t time, char* str)
{
    uint8_t n=0;
    uint8_t h, m;
    
    h = time/60;
    m = time%60;
    n += intToStrFix(h, 2, &str[n]);
    str[n++] = ':';
    n += intToStrFix(m, 2, &str[n]);
    str[n] = 0;
    return n;
}
//-----------------------------------------------------
uint8_t Converter_C::timerWeekToStr(uint8_t week, char* str)
{
    uint8_t n=0;
    
    if (week&0x01){
        n += strToStr("MO,", &str[n]);
    }
    if (week&0x02){
        n += strToStr("TU,", &str[n]);
    }
    if (week&0x04){
        n += strToStr("WE,", &str[n]);
    }
    if (week&0x08){
        n += strToStr("TH,", &str[n]);
    }
    if (week&0x10){
        n += strToStr("FR,", &str[n]);
    }
    if (week&0x20){
        n += strToStr("SA,", &str[n]);
    }
    if (week&0x40){
        n += strToStr("SU,", &str[n]);
    }
    if (n == 0){
        n += strToStr("NO,", &str[n]);
    }
    if (n > 0) n--;
    str[n] = 0;
    return n;
}
//-----------------------------------------------------
//bool Converter_C::strToTime(char* str)
//{
    // "2025-07-03 08:42:31.582000"
    
//    unixTime.year = strToInt(&str[0]);
//    unixTime.mon = strToInt(&str[5]);
//    unixTime.mday = strToInt(&str[8]);
//    unixTime.hour = strToInt(&str[11]);
//    unixTime.min = strToInt(&str[14]);
//    unixTime.sec = strToInt(&str[17]);

//    return unixTime.year >= 2025 && unixTime.year < 2125;
//}
