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
 * @file usb_endp.c
 * @author Nations
 * @version v1.0.1
 *
 * @copyright Copyright (c) 2019, Nations Technologies Inc. All rights reserved.
 */

/* Includes ------------------------------------------------------------------*/
#include "usb_lib.h"
#include "usb_desc.h"
#include "usb_mem.h"
#include "hw_config.h"
#include "usb_istr.h"
#include "usb_pwr.h"

/* Private typedef -----------------------------------------------------------*/
/* Private define ------------------------------------------------------------*/

/* Interval between sending IN packets in frame number (1 frame = 1ms).
   SOF_Callback below compares FrameCount *before* incrementing, so the
   actual period is (this value + 1) SOF frames — was 5, i.e. an IN flush
   only every ~6ms, one 64-byte chunk at a time. That throttled the SLCAN
   bridge (SlcanBridge.cpp) badly enough to cause timeouts/errors in CAN
   Tool during flashing: every relayed CAN frame and its 'Z'/'z' ack sits
   in USART_Rx_Buffer for up to ~6ms before going out, and a burst bigger
   than one 64-byte chunk needs multiple such cycles to drain. Dropped to 0
   (flush every single SOF, ~1ms) — same cadence the org's own dedicated
   CAN-adapter (AT32F415, C:\source\CAN-adapter) already uses for its own
   USB IN flush. Plain AT-command traffic is far below this rate either
   way, so this doesn't cost anything there — Handle_USBAsynchXfer() just
   returns immediately when there's nothing queued. */
#define VCOMPORT_IN_FRAME_INTERVAL             0

/* Private macro -------------------------------------------------------------*/
/* Private variables ---------------------------------------------------------*/
uint8_t USB_Rx_Buffer[VIRTUAL_COM_PORT_DATA_SIZE];
extern  uint8_t USART_Rx_Buffer[];
extern uint32_t USART_Rx_ptr_out;
extern uint32_t USART_Rx_length;
extern uint8_t  USB_Tx_State;

/* Private function prototypes -----------------------------------------------*/
/* Private functions ---------------------------------------------------------*/

/*******************************************************************************
* Function Name  : EP1_IN_Callback
* Description    :
* Input          : None.
* Output         : None.
* Return         : None.
*******************************************************************************/
void EP1_IN_Callback (void)
{
    uint16_t USB_Tx_ptr;
    uint16_t USB_Tx_length;

    if (USB_Tx_State == 1)
    {
        if (USART_Rx_length == 0) 
        {
            USB_Tx_State = 0;
        }
        else 
        {
            if (USART_Rx_length > VIRTUAL_COM_PORT_DATA_SIZE)
            {
                USB_Tx_ptr = USART_Rx_ptr_out;
                USB_Tx_length = VIRTUAL_COM_PORT_DATA_SIZE;

                USART_Rx_ptr_out += VIRTUAL_COM_PORT_DATA_SIZE;
                USART_Rx_length -= VIRTUAL_COM_PORT_DATA_SIZE;    
            }
            else 
            {
                USB_Tx_ptr = USART_Rx_ptr_out;
                USB_Tx_length = USART_Rx_length;

                USART_Rx_ptr_out += USART_Rx_length;
                USART_Rx_length = 0;
            }
            USB_CopyUserToPMABuf(&USART_Rx_Buffer[USB_Tx_ptr], ENDP1_TXADDR, USB_Tx_length);
            USB_SetEpTxCnt(ENDP1, USB_Tx_length);
            USB_SetEpTxValid(ENDP1); 
        }
    }
}

/*******************************************************************************
* Function Name  : EP3_OUT_Callback
* Description    :
* Input          : None.
* Output         : None.
* Return         : None.
*******************************************************************************/
void EP3_OUT_Callback(void)
{
    uint16_t USB_Rx_Cnt;

    /* Get the received data buffer and update the counter */
    USB_Rx_Cnt = USB_SilRead(EP3_OUT, USB_Rx_Buffer);

    /* USB data will be immediately processed, this allow next USB traffic being 
    NAKed till the end of the USART Xfer */

    USB_To_USART_Send_Data(USB_Rx_Buffer, USB_Rx_Cnt);

    /* Enable the receive of data on EP3 */
    USB_SetEpRxValid(ENDP3);
}


/*******************************************************************************
* Function Name  : SOF_Callback / INTR_SOFINTR_Callback
* Description    :
* Input          : None.
* Output         : None.
* Return         : None.
*******************************************************************************/
void SOF_Callback(void)
{
    static uint32_t FrameCount = 0;

    if(bDeviceState == CONFIGURED)
    {
        if (FrameCount++ == VCOMPORT_IN_FRAME_INTERVAL)
        {
            /* Reset the frame counter */
            FrameCount = 0;
            /* Check the data to be sent through IN pipe */
            Handle_USBAsynchXfer();
        }
    }  
}

