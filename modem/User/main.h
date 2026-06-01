#ifndef __MAIN_H
#define __MAIN_H

#include "n32wb452.h"
#include "core.h"
#include "Modem.h"
#include "can.h"
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
