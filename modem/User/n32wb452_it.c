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
 * @file n32wb452_it.c
 * @author Nations
 * @version v1.0.0
 *
 * @copyright Copyright (c) 2019, Nations Technologies Inc. All rights reserved.
 */
#include <stdio.h>
#include "n32wb452_it.h"
#include "usb_istr.h"
/** @addtogroup N32WB452X_StdPeriph_Template
 * @{
 */

extern __IO uint32_t CurrDataCounterEnd;

/******************************************************************************/
/*            Cortex-M4 Processor Exceptions Handlers                         */
/******************************************************************************/

/**
 * @brief  This function handles Hard Fault exception.
 */
void NMI_Handler(void)
{
    while (1)
    {
    }
}
/**
 * @brief  This function handles Memory Manage exception.
 */
void MemManage_Handler(void)
{
    /* Go to infinite loop when Memory Manage exception occurs */
    while (1)
    {
    }
}

/**
 * @brief  This function handles Bus Fault exception.
 */
void BusFault_Handler(void)
{
    /* Go to infinite loop when Bus Fault exception occurs */
    while (1)
    {
    }
}

/**
 * @brief  This function handles Usage Fault exception.
 */
void UsageFault_Handler(void)
{
    /* Go to infinite loop when Usage Fault exception occurs */
    while (1)
    {
    }
}

/**
 * @brief  This function handles SVCall exception.
 */
void SVC_Handler(void)
{
    while (1)
    {
    }
}

/**
 * @brief  This function handles Debug Monitor exception.
 */
void DebugMon_Handler(void)
{
    while (1)
    {
    }
}

void EXTI0_IRQHandler(void)
{
while (1)
    {
    }
}

void CAN1_RX1_IRQHandler(void)
{
    CAN_ClearINTPendingBit(CAN1, CAN_INT_FF1);
}
void CAN2_RX0_IRQHandler(void)
{
    CAN_ClearINTPendingBit(CAN2, CAN_INT_FF0);
}
void CAN2_RX1_IRQHandler(void)
{
    CAN_ClearINTPendingBit(CAN2, CAN_INT_FF1);
}

void TIM1_CC_IRQHandler(void)
{
    while (1)
    {
    }
}

void TIM3_IRQHandler(void)
{
 while (1)
    {
    } 
}
void TIM14_IRQHandler(void)
{
 while (1)
    {
    }   
}
void TIM15_IRQHandler(void)
{
 while (1)
    {
    }   
}
void TIM16_IRQHandler(void)
{
  while (1)
    {
    }  
}
void TIM17_IRQHandler(void)
{
    while (1)
    {
    }
}
/**
 * @brief  This function handles USB_LP_CAN1_RX0_IRQ Handler.
 */
void USB_LP_CAN1_RX0_IRQHandler(void)
{
    USB_Istr();
}
/**
 * @brief  This function handles USB WakeUp interrupt request.
 */
void USBWakeUp_IRQHandler(void)
{
    EXTI_ClrITPendBit(EXTI_LINE18);
}
/******************************************************************************/
/*                 N32WB452X Peripherals Interrupt Handlers                     */
/*  Add here the Interrupt Handler for the used peripheral(s) (PPP), for the  */
/*  available peripheral interrupt handler's name please refer to the startup */
/*  file (startup_n32wb452x.s).                                                 */
/******************************************************************************/

/**
 * @brief  This function handles PPP interrupt request.
 */
/*void PPP_IRQHandler(void)
{
}*/
void WWDG_IRQHandler(void)
{
    while (1)
    {
    }
}
void PVD_IRQHandler(void)
{
    while (1)
    {
    }
}
void TAMPER_IRQHandler(void)
{
    while (1)
    {
    }
}
void RTC_WKUP_IRQHandler(void)
    {
    while (1)
    {
    }
}
void FLASH_IRQHandler(void)
    {
    while (1)
    {
    }
}
void RCC_IRQHandler(void)
    {
    while (1)
    {
    }
}

void EXTI1_IRQHandler(void)
    {
    while (1)
    {
    }
}
void EXTI2_IRQHandler(void)
    {
    while (1)
    {
    }
}
void EXTI3_IRQHandler(void)
    {
    while (1)
    {
    }
}
void EXTI4_IRQHandler(void)
    {
    while (1)
    {
    }
}
void DMA1_Channel1_IRQHandler(void)
    {
    while (1)
    {
    }
}
void DMA1_Channel2_IRQHandler(void)
    {
    while (1)
    {
    }
}
void DMA1_Channel3_IRQHandler(void)
    {
    while (1)
    {
    }
}
void DMA1_Channel4_IRQHandler(void)
    {
    while (1)
    {
    }
}
void DMA1_Channel5_IRQHandler(void)
    {
    while (1)
    {
    }
}
void DMA1_Channel6_IRQHandler(void)
    {
    while (1)
    {
    }
}
void DMA1_Channel7_IRQHandler(void)
    {
    while (1)
    {
    }
}
void ADC1_2_IRQHandler(void)
    {
    while (1)
    {
    }
}
void USB_HP_CAN1_TX_IRQHandler(void)
    {
    while (1)
    {
    }
}

void CAN1_SCE_IRQHandler(void)
    {
    while (1)
    {
    }
}
void EXTI9_5_IRQHandler(void)
    {
    while (1)
    {
    }
}
void TIM1_BRK_IRQHandler(void)
    {
    while (1)
    {
    }
}
void TIM1_UP_IRQHandler(void)
    {
    while (1)
    {
    }
}
void TIM1_TRG_COM_IRQHandler(void)
    {
    while (1)
    {
    }
}

void TIM2_IRQHandler(void)
    {
    while (1)
    {
    }
}

void TIM4_IRQHandler(void)
    {
    while (1)
    {
    }
}
void I2C1_EV_IRQHandler(void)
    {
    while (1)
    {
    }
}
void I2C1_ER_IRQHandler(void)
    {
    while (1)
    {
    }
}
void I2C2_EV_IRQHandler(void)
    {
    while (1)
    {
    }
}
void I2C2_ER_IRQHandler(void)
    {
    while (1)
    {
    }
}
void SPI1_IRQHandler(void)
    {
    while (1)
    {
    }
}
void SPI2_IRQHandler(void)
    {
    while (1)
    {
    }
}
void USART1_IRQHandler(void)
    {
    while (1)
    {
    }
}
void USART2_IRQHandler(void)
    {
    while (1)
    {
    }
}
void USART3_IRQHandler(void)
    {
    while (1)
    {
    }
}
void EXTI15_10_IRQHandler(void)
    {
    while (1)
    {
    }
}
void RTCAlarm_IRQHandler(void)
    {
    while (1)
    {
    }
}

void TIM8_BRK_IRQHandler(void)
    {
    while (1)
    {
    }
}
void TIM8_UP_IRQHandler(void)
    {
    while (1)
    {
    }
}
void TIM8_TRG_COM_IRQHandler(void)
    {
    while (1)
    {
    }
}
void TIM8_CC_IRQHandler(void)
    {
    while (1)
    {
    }
}
void RESERVE47_IRQHandler(void)
    {
    while (1)
    {
    }
}
void RESERVE48_IRQHandler(void)
    {
    while (1)
    {
    }
}
void SDIO_IRQHandler(void)
    {
    while (1)
    {
    }
}
void TIM5_IRQHandler(void)
    {
    while (1)
    {
    }
}
void SPI3_IRQHandler(void)
    {
    while (1)
    {
    }
}
void UART4_IRQHandler(void)
    {
    while (1)
    {
    }
}
void UART5_IRQHandler(void)
    {
    while (1)
    {
    }
}
void TIM6_IRQHandler(void)
    {
    while (1)
    {
    }
}
void TIM7_IRQHandler(void)
    {
    while (1)
    {
    }
}
void DMA2_Channel1_IRQHandler(void)
    {
    while (1)
    {
    }
}
void DMA2_Channel2_IRQHandler(void)
    {
    while (1)
    {
    }
}
void DMA2_Channel3_IRQHandler(void)
    {
    while (1)
    {
    }
}
void DMA2_Channel4_IRQHandler(void)
    {
    while (1)
    {
    }
}
void DMA2_Channel5_IRQHandler(void)
    {
    while (1)
    {
    }
}
void RESERVE61_IRQHandler(void)
    {
    while (1)
    {
    }
}
void RESERVE62_IRQHandler(void)
    {
    while (1)
    {
    }
}
void CAN2_TX_IRQHandler(void)
    {
    while (1)
    {
    }
}

void CAN2_SCE_IRQHandler(void)
    {
    while (1)
    {
    }
}
void RESERVE67_IRQHandler(void)
    {
    while (1)
    {
    }
}
void DMA2_Channel6_IRQHandler(void)
    {
    while (1)
    {
    }
}
void DMA2_Channel7_IRQHandler(void)
    {
    while (1)
    {
    }
}
void I2C3_EV_IRQHandler(void)
    {
    while (1)
    {
    }
}
void I2C3_ER_IRQHandler(void)
    {
    while (1)
    {
    }
}
void I2C4_EV_IRQHandler(void)
    {
    while (1)
    {
    }
}
void I2C4_ER_IRQHandler(void)
    {
    while (1)
    {
    }
}
void UART6_IRQHandler(void)
    {
    while (1)
    {
    }
}
void UART7_IRQHandler(void)
    {
    while (1)
    {
    }
}
void DMA1_Channel8_IRQHandler(void)
    {
    while (1)
    {
    }
}
void DMA2_Channel8_IRQHandler(void)
    {
    while (1)
    {
    }
}
void DVP_IRQHandler(void)
    {
    while (1)
    {
    }
}
void SAC_IRQHandler(void)
    {
    while (1)
    {
    }
}
void MMU_IRQHandler(void)
    {
    while (1)
    {
    }
}
void TSC_IRQHandler(void)
    {
    while (1)
    {
    }
}

void RSRAM_IRQHandler(void)
    {
    while (1)
    {
    }
}
/**
 * @}
 */
