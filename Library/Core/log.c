/*****************************************************************************
 * Copyright (c) 2019, Nations Technologies Inc.
 *
 * All rights reserved.
 * ****************************************************************************
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * - Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the disclaimer below.
 *
 * Nations' name may not be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * DISCLAIMER: THIS SOFTWARE IS PROVIDED BY NATIONS "AS IS" AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT ARE
 * DISCLAIMED. IN NO EVENT SHALL NATIONS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA,
 * OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
 * EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 * ****************************************************************************/

/**
 * @file log.c
 * @author Nations
 * @version v1.0.1
 *
 * @copyright Copyright (c) 2019, Nations Technologies Inc. All rights reserved.
 */
#include "log.h"
#include "hw_config.h"
#include "unix_time.h"

extern char serialNumberModem[16];

#if LOG_ENABLE

int logTypeMess = LOG_TYPE_AT;

//-----------------------------------------------------
void log_info(const char* mess)
{
    if (true){
        while(true){
            if (*mess != 0){
                USART_To_USB_Send_Data(*mess++);
            }
            else{
                break;
            }
        }
    }
}
//-----------------------------------------------------
void log_warning(const char* mess)
{
    if (true){
        while(true){
            if (*mess != 0){
                USART_To_USB_Send_Data(*mess++);
            }
            else{
                break;
            }
        }
    }
}
//-----------------------------------------------------
void log_error(const char* mess)
{
    if (true){
        while(true){
            if (*mess != 0){
                USART_To_USB_Send_Data(*mess++);
            }
            else{
                break;
            }
        }
    }
}
//-----------------------------------------------------
void log_gsm(const char* mess)
{
    if (logTypeMess == LOG_TYPE_GSM || logTypeMess == LOG_TYPE_ALL){
        while(true){
            if (*mess != 0){
                USART_To_USB_Send_Data(*mess++);
            }
            else{
                break;
            }
        }
    }
}
//-----------------------------------------------------
void log_heater(const char* mess)
{
    if (logTypeMess == LOG_TYPE_HEATER || logTypeMess == LOG_TYPE_ALL || logTypeMess == LOG_TYPE_USART){
        while(true){
            if (*mess != 0){
                USART_To_USB_Send_Data(*mess++);
            }
            else{
                break;
            }
        }
    }
}
//-----------------------------------------------------
void log_panel(const char* mess)
{
    if (logTypeMess == LOG_TYPE_PANEL || logTypeMess == LOG_TYPE_ALL || logTypeMess == LOG_TYPE_USART){
        while(true){
            if (*mess != 0){
                USART_To_USB_Send_Data(*mess++);
            }
            else{
                break;
            }
        }
    }
}
//-----------------------------------------------------
void log_ble(const char* mess)
{
    if (logTypeMess == LOG_TYPE_BLE || logTypeMess == LOG_TYPE_ALL){
        while(true){
            if (*mess != 0){
                USART_To_USB_Send_Data(*mess++);
            }
            else{
                break;
            }
        }
    }
}
//-----------------------------------------------------
void log_at(const char* mess)
{
    if (logTypeMess == LOG_TYPE_AT){
        while (*mess){
            if (*mess != '\r')          /* skip CR — terminal handles LF alone */
                USART_To_USB_Send_Data(*mess);
            mess++;
        }
    }
}
//-----------------------------------------------------
/*
extern "C" int fputc(int ch, FILE* f)
{
    USART_To_USB_Send_Data(ch);
    
    return ch;
}*/

#ifdef USE_FULL_ASSERT

__WEAK void assert_failed(const uint8_t* expr, const uint8_t* file, uint32_t line)
{
    log_error("assertion failed: `%s` at %s:%d", expr, file, line);
    while (1)
    {
    }
}
#endif // USE_FULL_ASSERT


#endif // LOG_ENABLE
