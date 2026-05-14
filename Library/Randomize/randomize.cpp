#include "randomize.h"
#include <stdio.h>
#include "math.h"


Randomize randomize;
//-----------------------------------------------------
Randomize::Randomize(void)
{
    
}
//-----------------------------------------------------
void Randomize::initialize()
{
    /* Enable DMA1 and DMA2 clocks */
    RCC_EnableAHBPeriphClk(RCC_AHB_PERIPH_DMA1, ENABLE);
    
    /* Enable GPIOC clocks */
    RCC_EnableAPB2PeriphClk(RCC_APB2_PERIPH_GPIOA, ENABLE);
    
    /* Enable ADC1 clocks */
    RCC_EnableAHBPeriphClk(RCC_AHB_PERIPH_ADC1,ENABLE);

    /* RCC_ADCHCLK_DIV16*/
    ADC_ConfigClk(ADC_CTRL3_CKMOD_AHB,RCC_ADCHCLK_DIV16);
    RCC_ConfigAdc1mClk(RCC_ADC1MCLK_SRC_HSE, RCC_ADC1MCLK_DIV8);  //selsect HSE as RCC ADC1M CLK Source
    
    DMA_InitType DMA_InitStructure;
    ADC_InitTypeEx ADC_InitStructureEx;
    ADC_InitType ADC_InitStructure;
    
    /* DMA1 channel1 configuration ----------------------------------------------*/
    DMA_DeInit(DMA1_CH1);
    DMA_InitStructure.PeriphAddr     = (uint32_t)&ADC1->DAT;
    DMA_InitStructure.MemAddr        = (uint32_t)array;
    DMA_InitStructure.Direction      = DMA_DIR_PERIPH_SRC;
    DMA_InitStructure.BufSize        = ARRAY_SIZE;
    DMA_InitStructure.PeriphInc      = DMA_PERIPH_INC_DISABLE;
    DMA_InitStructure.DMA_MemoryInc  = DMA_MEM_INC_ENABLE;
    DMA_InitStructure.PeriphDataSize = DMA_PERIPH_DATA_SIZE_HALFWORD;
    DMA_InitStructure.MemDataSize    = DMA_MemoryDataSize_HalfWord;
    DMA_InitStructure.CircularMode   = DMA_MODE_CIRCULAR;
    DMA_InitStructure.Priority       = DMA_PRIORITY_HIGH;
    DMA_InitStructure.Mem2Mem        = DMA_M2M_DISABLE;
    DMA_Init(DMA1_CH1, &DMA_InitStructure);
    /* Enable DMA1 channel1 */
    DMA_EnableChannel(DMA1_CH1, ENABLE);

    /* ADC1 configuration ------------------------------------------------------*/
    
    ADC_InitStructure.WorkMode       = ADC_WORKMODE_INDEPENDENT;
    ADC_InitStructure.MultiChEn      = ENABLE;
    ADC_InitStructure.ContinueConvEn = ENABLE;
    ADC_InitStructure.ExtTrigSelect  = ADC_EXT_TRIGCONV_NONE;
    ADC_InitStructure.DatAlign       = ADC_DAT_ALIGN_R;
    ADC_InitStructure.ChsNumber      = 1;   // 1
    ADC_Init(ADC1, &ADC_InitStructure);
    /* ADC1 temp sensor enable */
    ADC_EnableTempSensorVrefint(ENABLE);  
        
    ADC_InitStructureEx.VbatMinitEn = ENABLE;
    ADC_InitStructureEx.DeepPowerModEn = DISABLE;
    ADC_InitStructureEx.JendcIntEn = DISABLE;
    ADC_InitStructureEx.EndcIntEn = DISABLE;
    ADC_InitStructureEx.ClkMode = ADC_CTRL3_CKMOD_AHB;
    ADC_InitStructureEx.CalAtuoLoadEn = DISABLE;
    ADC_InitStructureEx.DifModCal = false;
    ADC_InitStructureEx.ResBit = ADC_CTRL3_RES_12BIT;
    ADC_InitStructureEx.Samp303Style = false;
    ADC_InitEx(ADC1, &ADC_InitStructureEx);
    /* ADC1 regular configuration */
    ADC_ConfigRegularChannel(ADC1, ADC_CH_16, 1, ADC_SAMP_TIME_1CYCLES5);   // 11
    /* ADC1 temp sensor enable */
    ADC_EnableTempSensorVrefint(ENABLE);  
    /* Enable ADC1 DMA */
    ADC_EnableDMA(ADC1, ENABLE);

    /* Enable ADC1 */
    ADC_Enable(ADC1, ENABLE);
    /*Check ADC Ready*/
    while(ADC_GetFlagStatusNew(ADC1,ADC_FLAG_RDY) == RESET)
        ;
    /* Start ADC1 calibration */
    ADC_StartCalibration(ADC1);
    /* Check the end of ADC1 calibration */
    while (ADC_GetCalibrationStatus(ADC1))
        ;

    /* Start ADC1 Software Conversion */
    ADC_EnableSoftwareStartConv(ADC1, ENABLE);
}
//-----------------------------------------------------
uint32_t Randomize::getValue(void)
{
    uint32_t val = 0;
    
    for (int i=0; i<32; i++){
        val |= array[i]&0x01;
        val = val<<1;
    }
    
    return val;
}
//-----------------------------------------------------
