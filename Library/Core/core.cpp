/******************************************************************************
*  
* 
* 
* :  ..
* 
* 08.08.2018
* :
*******************************************************************************/
/* Includes ------------------------------------------------------------------*/
#include "core.h"
#include "main.h"
#include "gsm.h"
#include "unix_time.h"
#include "flash.h"
#include "modem_handler.h"

enum
{
    SYSCLK_PLLSRC_HSI,
    SYSCLK_PLLSRC_HSE,
};

Core core;
//-----------------------------------------------------
Core::Core(void)
{
    
}
//-----------------------------------------------------
void Core::initialize(void)
{
    SetSysClockToPLL((uint32_t)48000000, SYSCLK_PLLSRC_HSE); // 144000000 //48000000
    //SystemInit();
    remapTable();
    __enable_irq();
    
    SystemCoreClockUpdate();
	SysTick_Config(SystemCoreClock/1000);
    NVIC_SetPriority(SysTick_IRQn, 0);
    
//    RCC_EnableAPB1PeriphClk(RCC_APB1_PERIPH_TIM6, ENABLE);
//	TIM_Enable(TIM6, ENABLE);
//	TIM6->PSC = SystemCoreClock/1000000-1;
// ---
//	RCC_EnableAPB1PeriphClk(RCC_APB1_PERIPH_TIM7, ENABLE);
//	TIM_Enable(TIM7, ENABLE);
//	TIM7->PSC = SystemCoreClock/1000-1;
    
    RCC_EnableAPB2PeriphClk(RCC_APB2_PERIPH_AFIO, ENABLE);
    // Disable the Serial Wire and JTAG Debug Port SWJ-DP
    GPIO_ConfigPinRemap(GPIO_RMP_SW_JTAG_SW_ENABLE, ENABLE); // GPIO_RMP_SW_JTAG_DISABLE
    
    counterMain = 0;
    counter = 0;
}
//-----------------------------------------------------
void Core::handler(void)
{
    static uint32_t timerMain;
    
    if ((core.getTick()-timerMain) >= 1000)
    {
        timerMain = core.getTick();
        counterMain = counter;
        counter = 0;
    }
    counter++;
}
//-------------------------------------------------------------------
/**
*\*\name    SetSysClockToPLL.
*\*\fun     Selects PLL clock as System clock source and configure HCLK, PCLK2
*\*\         and PCLK1 prescalers.
*\*\param   none
*\*\note    PLL frequency must be greater than or equal to 32MHz.
*\*\return  none 
**/
void Core::SetSysClockToPLL(uint32_t freq, uint8_t src) 
{
///    RCC_ClocksType RCC_ClockFreq;
    ErrorStatus HSEStartUpStatus;
    
    uint32_t pllsrc = (src == SYSCLK_PLLSRC_HSI ? RCC_PLL_SRC_HSI_DIV2 : RCC_PLL_SRC_HSE_DIV2);
    uint32_t pllmul;
    uint32_t latency;
    uint32_t pclk1div, pclk2div;

    if (HSE_VALUE != 16000000)
    {
        /* HSE_VALUE == 8000000 is needed in this project! */
        while (1)
            ;
    }

    /* SYSCLK, HCLK, PCLK2 and PCLK1 configuration
     * -----------------------------*/
    /* RCC system reset(for debug purpose) */
    RCC_DeInit();

    if (src == SYSCLK_PLLSRC_HSE)
    {
        /* Enable HSE */
        RCC_ConfigHse(RCC_HSE_ENABLE);

        /* Wait till HSE is ready */
        HSEStartUpStatus = RCC_WaitHseStable();

        if (HSEStartUpStatus != SUCCESS)
        {
            /* If HSE fails to start-up, the application will have wrong clock
               configuration. User can add here some code to deal with this
               error */

            /* Go to infinite loop */
            while (1)
                ;
        }
    }

    switch (freq)
    {
    case 24000000:
        latency  = FLASH_LATENCY_0;
        pllmul   = RCC_PLL_MUL_3;   // RCC_PLL_MUL_6
        pclk1div = RCC_HCLK_DIV1;
        pclk2div = RCC_HCLK_DIV1;
        break;
    case 36000000:
        latency  = FLASH_LATENCY_1;
        pllmul   = RCC_PLL_MUL_9;   // RCC_PLL_MUL_9
        pclk1div = RCC_HCLK_DIV2;   // RCC_HCLK_DIV1
        pclk2div = RCC_HCLK_DIV1;
        break;
    case 48000000:
        latency  = FLASH_LATENCY_1;
        pllmul   = RCC_PLL_MUL_6;  // RCC_PLL_MUL_12
        pclk1div = RCC_HCLK_DIV2;
        pclk2div = RCC_HCLK_DIV1;
        break;
    case 56000000:
        latency  = FLASH_LATENCY_1;
        pllmul   = RCC_PLL_MUL_7;  // RCC_PLL_MUL_14
        pclk1div = RCC_HCLK_DIV2;
        pclk2div = RCC_HCLK_DIV1;
        break;
    case 72000000:
        latency  = FLASH_LATENCY_2;
        pllmul   = RCC_PLL_MUL_9;  // RCC_PLL_MUL_18
        pclk1div = RCC_HCLK_DIV2;
        pclk2div = RCC_HCLK_DIV1;
        break;
    case 96000000:
        latency  = FLASH_LATENCY_2;
        pllmul   = RCC_PLL_MUL_12;  // RCC_PLL_MUL_24
        pclk1div = RCC_HCLK_DIV4;
        pclk2div = RCC_HCLK_DIV2;
        break;
    case 128000000:
        latency  = FLASH_LATENCY_3;
        pllmul   = RCC_PLL_MUL_16;  // RCC_PLL_MUL_32
        pclk1div = RCC_HCLK_DIV4;
        pclk2div = RCC_HCLK_DIV2;
        break;
    case 144000000:
        /* must use HSE as PLL source */
        latency  = FLASH_LATENCY_4;
        pllsrc   = RCC_PLL_SRC_HSE_DIV1;
        pllmul   = RCC_PLL_MUL_9;  // RCC_PLL_MUL_18
        pclk1div = RCC_HCLK_DIV4;
        pclk2div = RCC_HCLK_DIV2;
        break;
    default:
        while (1)
            ;
    }

    FLASH_SetLatency(latency);

    /* HCLK = SYSCLK */
    RCC_ConfigHclk(RCC_SYSCLK_DIV1);

    /* PCLK2 = HCLK */
    RCC_ConfigPclk2(pclk2div);

    /* PCLK1 = HCLK */
    RCC_ConfigPclk1(pclk1div);

    RCC_ConfigPll(pllsrc, pllmul);

    /* Enable PLL */
    RCC_EnablePll(ENABLE);

    /* Wait till PLL is ready */
    while (RCC_GetFlagStatus(RCC_FLAG_PLLRD) == RESET)
        ;

    /* Select PLL as system clock source */
    RCC_ConfigSysclk(RCC_SYSCLK_SRC_PLLCLK);

    /* Wait till PLL is used as system clock source */
    while (RCC_GetSysclkSrc() != 0x08)
        ;
}
//-------------------------------------------------------------------
void Core::SetSysClockToHSI(void)
{
    RCC_DeInit();

    RCC_EnableHsi(ENABLE);

    /* Enable Prefetch Buffer */
    FLASH_PrefetchBufSet(FLASH_PrefetchBuf_EN);

    /* Flash 0 wait state */
    FLASH_SetLatency(FLASH_LATENCY_0);

    /* HCLK = SYSCLK */
    RCC_ConfigHclk(RCC_SYSCLK_DIV1);

    /* PCLK2 = HCLK */
    RCC_ConfigPclk2(RCC_HCLK_DIV1);

    /* PCLK1 = HCLK */
    RCC_ConfigPclk1(RCC_HCLK_DIV1);

    /* Select HSE as system clock source */
    RCC_ConfigSysclk(RCC_SYSCLK_SRC_HSI);

    /* Wait till PLL is used as system clock source */
    while (RCC_GetSysclkSrc() != 0x00)
    {
    }
}

/**
 * @brief  Selects HSE as System clock source and configure HCLK, PCLK2
 *         and PCLK1 prescalers.
 */
void Core::SetSysClockToHSE(void)
{
///    RCC_ClocksType RCC_ClockFreq;
    ErrorStatus HSEStartUpStatus;
    
    /* SYSCLK, HCLK, PCLK2 and PCLK1 configuration
     * -----------------------------*/
    /* RCC system reset(for debug purpose) */
    RCC_DeInit();

    /* Enable HSE */
    RCC_ConfigHse(RCC_HSE_ENABLE);

    /* Wait till HSE is ready */
    HSEStartUpStatus = RCC_WaitHseStable();

    if (HSEStartUpStatus == SUCCESS)
    {
        /* Enable Prefetch Buffer */
        FLASH_PrefetchBufSet(FLASH_PrefetchBuf_EN);

        if (HSE_Value <= 32000000)
        {
            /* Flash 0 wait state */
            FLASH_SetLatency(FLASH_LATENCY_0);
        }
        else
        {
            /* Flash 1 wait state */
            FLASH_SetLatency(FLASH_LATENCY_1);
        }

        /* HCLK = SYSCLK */
        RCC_ConfigHclk(RCC_SYSCLK_DIV1);

        /* PCLK2 = HCLK */
        RCC_ConfigPclk2(RCC_HCLK_DIV1);

        /* PCLK1 = HCLK */
        RCC_ConfigPclk1(RCC_HCLK_DIV1);

        /* Select HSE as system clock source */
        RCC_ConfigSysclk(RCC_SYSCLK_SRC_HSE);

        /* Wait till HSE is used as system clock source */
        while (RCC_GetSysclkSrc() != 0x04)
        {
        }
    }
    else
    {
        /* If HSE fails to start-up, the application will have wrong clock
           configuration. User can add here some code to deal with this error */

        /* Go to infinite loop */
        while (1)
        {
        }
    }
}
//-------------------------------------------------------------------
//void TIM6_init(void)
//{
//    TIM_TimeBaseInitType TIM_TimeBaseStructure;
//    NVIC_InitType NVIC_InitStructure;

//    RCC_EnableAPB1PeriphClk(RCC_APB1_PERIPH_TIM6, ENABLE); //????

//    //???TIM3???
//    TIM_TimeBaseStructure.Period    = 19;                //???????????????????????????
//    TIM_TimeBaseStructure.Prescaler = 3599;              //??????36MHZ?????;TIMx???????????
//    TIM_TimeBaseStructure.ClkDiv    = TIM_CLK_DIV1;      //??????:TDTS = Tck_tim
//    TIM_TimeBaseStructure.CntMode   = TIM_CNT_MODE_DOWN; // TIM??????
//    TIM_InitTimeBase(TIM6, &TIM_TimeBaseStructure);      //??????????TIMx???????

//    /*?????????*/
//    TIM6->STS &= 0xFFFE;                         //??update?????,???????????????????????
//    TIM_ConfigInt(TIM6, TIM_INT_UPDATE, ENABLE); //?????TIM6??,??????

//    //?????NVIC??
//    NVIC_InitStructure.NVIC_IRQChannel                   = TIM6_IRQn; // TIM6??
//    NVIC_InitStructure.NVIC_IRQChannelCmd                = ENABLE;    // IRQ?????
//    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 2;         //?????2
//    NVIC_InitStructure.NVIC_IRQChannelSubPriority        = 0;         //????0
//    NVIC_Init(&NVIC_InitStructure);                                   //???NVIC???

//    TIM_Enable(TIM6, ENABLE);
//}
////-------------------------------------------------------------------
//void TIM6_deinit(void)
//{
//    TIM_Enable(TIM6, DISABLE);
//    TIM_DeInit(TIM6);
//}
//void TIM6_IRQHandler(void)
//{
//    if (TIM_GetIntStatus(TIM6, TIM_FLAG_UPDATE) != RESET)
//    {
//        TIM6->STS &= ~(0x01 << 0); //???????

//        //bluetooth.system_10s_cnt++;
//        //if (bluetooth.system_10s_cnt >= (2000 * 5)) // 5ms*2000 = 10s
//        //{
//        //    bluetooth.system_10s_cnt  = 0;
//        //    bluetooth.system_10s_flag = 1;
//        //}
//    }
//}
//-------------------------------------------------------------------
void Core::enableWatchdog(void)
{
#ifdef LSI_TIM_MEASURE
    /* Enable the LSI OSC */
    RCC_EnableLsi(ENABLE);
    /* Wait till LSI is ready */
    while (RCC_GetFlagStatus(RCC_FLAG_LSIRD) == RESET)
    {
    }
    /* TIM Configuration -------------------------------------------------------*/
    TIM5_ConfigForLSI();
    /* Wait until the TIM5 get 3 LSI edges */
    while (CaptureNumber != 3)
    {
    }
    /* Disable TIM5 CC4 Interrupt Request */
    TIM_ConfigInt(TIM5, TIM_INT_CC4, DISABLE);
#endif
    /* IWDG timeout equal to 250 ms (the timeout may varies due to LSI frequency
       dispersion) */
    /* Enable write access to IWDG_PR and IWDG_RLR registers */
    IWDG_WriteConfig(IWDG_WRITE_ENABLE);
    /* IWDG counter clock: LSI/32 */
    IWDG_SetPrescalerDiv(IWDG_PRESCALER_DIV128);
    /* Set counter reload value to obtain 250ms IWDG TimeOut.
       Counter Reload Value = 250ms/IWDG counter clock period
                            = 250ms / (LSI/32)
                            = 0.25s / (LsiFreq/32)
                            = LsiFreq/(32 * 4)
                            = LsiFreq/128 
    */
    IWDG_CntReload(40000 / 128);
    /* Reload IWDG counter */
    IWDG_ReloadKey();
    /* Enable IWDG (the LSI oscillator will be enabled by hardware) */
    IWDG_Enable();
}

//-----------------------------------------------------
void Core::delayUs(uint32_t delay)
{
	while(delay--)
        for(uint16_t m = 0; m < 28; m++);
}
//-----------------------------------------------------
void Core::delayMs(uint32_t delay)
{
	uint32_t timer = core.getTick();
    while (1){
        if ((core.getTick()-timer) > delay) break;
    }
}

//-----------------------------------------------------
void Core::protectedFlash(void)
{
    /*
    if (FLASH_GetReadOutProtectionStatus() == RESET){
        FLASH_Unlock();
        FLASH_ReadOutProtection(ENABLE);
        FLASH_Lock();
    }
    */
}
//-----------------------------------------------------
void Core::remapTable(void)
{
    NVIC_SetVectorTable(NVIC_VectTab_FLASH, 0x30000);
}
//-----------------------------------------------------
void Core::incTick(void)
{
    this->msTick++;
}
//-----------------------------------------------------
uint32_t Core::getTick(void)
{
    return this->msTick;
}
//-----------------------------------------------------
void Core::setTimer(uint32_t value)
{
    this->tickTimer = this->msTick + value;
}
//-----------------------------------------------------
uint32_t Core::getTimer(void)
{
    int32_t i;
    
    i = this->tickTimer - this->msTick;
    if (i < 0) i = 0;
    return i;
}
//-----------------------------------------------------
void Core::resetTimer(void)
{
    this->tickTimer = this->msTick;
}
//-----------------------------------------------------
extern "C" void SysTick_Handler(void)
{
    static uint16_t count = 0;
    
    core.incTick();         // ---
    if (count < 1000) count++;
    else{
        count = 0;
        core.unixTime++;
    }
}
//-----------------------------------------------------

//-----------------------------------------------------
extern "C" void HardFault_Handler(void)
{
  /* Go to infinite loop when Hard Fault exception occurs */
  
  //GPIO_SetBits(GPIOC, GPIO_PIN_13);
  //while (1)
  //{
  
  //}
  isReset = true;
  flash.writeSetup();
  NVIC_SystemReset();
}
//-----------------------------------------------------

//-----------------------------------------------------
extern "C" void TSC_IRQHandler(void)
{
 while (1)
    {
    }   
}


//-----------------------------------------------------
uint16_t Core::getDelta(int16_t valA, int16_t valB)
{
    int16_t val;
    
    val = valA-valB;
    if (val < 0) val = -val;
    return val;
}
//-----------------------------------------------------
bool Core::isTimeOk(void)
{
    if (unixTime > OUR_DAYS_UNIX_TIME)
        return true;
    else 
        return false;        
}
//-----------------------------------------------------
