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
   memory map. This app's own code+footer span [0x0802A000, 0x0804A800) is
   now declared as TWO separate regions in Options for Target -> Target
   (mirrored automatically into modemDragonfly.sct's LR_IROM1/LR_IROM2, no
   hand-written scatter file needed): IROM1 = 0x0802A000, size 0x20000
   (128 KB code budget) and IROM2 = 0x0804A000, size 0x800 (one footer
   page). Changed 2026-09-04 from a single 0x20800 region spanning both —
   with one region, the linker must materialize every byte between the end
   of real code and _CRCR's fixed address as literal padding (a real
   L6220E "exceeds limit" build failure once that padding left no room for
   RW_IRAM1's own compressed init-data load image, which the linker always
   places immediately after wherever the code region's *declared* size
   ends, not after wherever the code actually stops). Two separate regions
   avoid that entirely: each is sized to only what it actually needs, so
   there's no gap for the linker to fill and no shared region for
   RW_IRAM1's load image to be squeezed out of. MAIN_PROGRAM_START_ADDRESS
   is IROM1's own start address — the app's vector table is the very first
   thing there, matching the convention every other device type in this
   org's lineup already uses (no reserved page ahead of it) — and
   ADDRESS_CRC (_CRCR's footer page, read by nations-bootloader as
   APP_FOOTER_ADDR) is IROM2's entire span instead. */
#define MAIN_PROGRAM_START_ADDRESS  0x0802A000u   //таблица векторов приложения — самое начало региона
#define ADDRESS_CRC                 0x0804A000u   //страница футера (_CRCR) — теперь последняя страница региона, читается загрузчиком
#define BOOT_MAGIC_ADDR              0x20023FFCu   //последнее слово физического ОЗУ — см. nations-bootloader

/* BOOT_MAGIC_ADDR values — must match nations-bootloader's own main.h
   (BOOT_MAGIC_ENTER_BOOT/ENTER_APP/UPDATE) byte-for-byte, that project is
   what actually reads this word. Set from Timberline.cpp: case 22 (CAN
   "Reset CPU", panel-triggered) for the first two; BOOT_MAGIC_UPDATE from
   the "selfOtaApply" MQTT command — see onMqttCommandReceived(). */
#define BOOT_MAGIC_ENTER_BOOT  0x0016AA55u   //войти в загрузчик и остаться в нём
#define BOOT_MAGIC_ENTER_APP   0x001655AAu   //вернуться в приложение
#define BOOT_MAGIC_UPDATE      0x00166699u   //обновление ПО — загрузчик прошьёт self-OTA образ поверх приложения и запустит его

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
