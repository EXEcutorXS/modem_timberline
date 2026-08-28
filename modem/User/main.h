#ifndef __MAIN_H
#define __MAIN_H

#include "n32wb452.h"
#include "core.h"
#include "Modem.h"
#include "can.h"
#include "version.h"
#include <string.h>

/* USB pins:
 *   A11 - USB DM
 *   A12 - USB DP
 */

#if defined (__CC_ARM)
  #pragma anon_unions
#endif

#define HIGH(_arg_) ((uint8_t)((_arg_ >> 8) & 0xFF))
#define LOW(_arg_)  (_arg_ & 0xFF)

#ifndef MIN
#define MIN(a, b) (((a) < (b)) ? (a) : (b))
#endif

/* nations-bootloader contract — see that project's plan doc for the full
   memory map. This app's own IROM1 (Options for Target -> Target, mirrored
   into modemDragonfly.sct's LR_IROM1) starts at MAIN_PROGRAM_START_ADDRESS-
   0x400... no: IROM1 actually starts at ADDRESS_CRC itself (0x0802A000) and
   covers the footer page too — see the IROM1 size note next to ADDRESS_CRC
   below. MAIN_PROGRAM_START_ADDRESS is where the app's own vector table
   lives (one page after ADDRESS_CRC), which is what the bootloader's
   JumpToApp() actually jumps to. */
#define ADDRESS_CRC                 0x0802A000u   //страница футера (_CRCR) — читается загрузчиком
#define MAIN_PROGRAM_START_ADDRESS  0x0802A800u   //таблица векторов приложения — на страницу дальше футера
#define BOOT_MAGIC_ADDR              0x20023FFCu   //последнее слово физического ОЗУ — см. nations-bootloader

/* Pin mapping (NW452RE)
 *
 * CAN:
 *   B8 - CAN RX
 *   B9 - CAN TX
 *
 * GSM (USART to modem):
 *   B10 - GSM TX (USART3 TX)
 *   B11 - GSM RX (USART3 RX)
 *
 * LED:
 *   B14 - Status LED
 */

extern "C" void TIM6_IRQHandler(void);

#endif /* __MAIN_H */
