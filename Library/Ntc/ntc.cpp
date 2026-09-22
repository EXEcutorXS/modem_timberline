#include "ntc.h"
#include "core.h"
#include <math.h>

Ntc ntc;

/* B=3950, R0=10k @ 25C (298.15K), matching divider resistor also 10k — see
   ntc.h for the wiring. Steinhart-Hart, B-parameter form. */
#define NTC_R0         10000.0f
#define NTC_T0         298.15f
#define NTC_BETA       3950.0f
#define NTC_RSERIES    10000.0f
#define ADC_FULL_SCALE 4095

/* An ADC reading this close to full scale means nothing is pulling A1
   toward ground at all - the NTC leg of the divider is open, not just
   cold. A genuinely cold NTC at this R0/Beta (-40C is around 191 kOhm)
   still only divides down to roughly 96% of full scale, well under this
   margin - so this only fires on an actual disconnect, never a real
   (if extreme) reading. */
#define ADC_OPEN_CIRCUIT_THRESHOLD 4080

void Ntc::initialize(void)
{
    pin.Initialize(GPIOA, GPIO_PIN_1, GPIO_Mode_AIN);

    RCC_EnableAHBPeriphClk(RCC_AHB_PERIPH_ADC1, ENABLE);

    ADC_InitType initStruct;
    ADC_InitStruct(&initStruct);
    initStruct.WorkMode       = ADC_WORKMODE_INDEPENDENT;
    initStruct.MultiChEn      = DISABLE;
    initStruct.ContinueConvEn = DISABLE;
    initStruct.ExtTrigSelect  = ADC_EXT_TRIGCONV_NONE;
    initStruct.DatAlign       = ADC_DAT_ALIGN_R;
    initStruct.ChsNumber      = 1;
    ADC_Init(ADC1, &initStruct);

    ADC_ConfigRegularChannel(ADC1, ADC_CH_2, 1, ADC_SAMP_TIME_239CYCLES5); /* CH_2 = PA1, see ADC1_Channel_02_PA1 */
    /* Classic quirk this peripheral shares with STM32F1's ADC1: even a
       purely software-triggered conversion needs ExtTrigConv enabled (with
       ExtTrigSelect already set to "none"/SWSTART above) or
       EnableSoftwareStartConv() below never actually starts anything. */
    ADC_EnableExternalTrigConv(ADC1, ENABLE);
    ADC_Enable(ADC1, ENABLE);

    ADC_StartCalibration(ADC1);
    /* Bounded, not infinite - a one-time boot step, but nothing here
       should be able to wedge the whole MCU if calibration somehow never
       reports done. */
    for (uint32_t i = 0; i < 1000000 && ADC_GetCalibrationStatus(ADC1) == SET; i++) {}
}

uint16_t Ntc::readRaw(void)
{
    ADC_EnableSoftwareStartConv(ADC1, ENABLE);
    for (uint32_t i = 0; i < 100000 && ADC_GetFlagStatus(ADC1, ADC_FLAG_ENDC) == RESET; i++) {}
    uint16_t raw = ADC_GetDat(ADC1);
    ADC_ClearFlag(ADC1, ADC_FLAG_ENDC);
    return raw;
}

/* Sampled once a second - plenty for a slow-moving external temperature,
   and gives the smoothing filter below a real light-noise signal to work
   with instead of chasing single-sample jitter. */
void Ntc::handler(void)
{
    static uint32_t timer = 0;
    uint32_t now = core.getTick();
    if ((now - timer) < 1000) return;
    timer = now;

    uint16_t raw = readRaw();
    if (raw >= ADC_OPEN_CIRCUIT_THRESHOLD) {
        connected = false;
        return;
    }

    /* R_ntc from the divider (NTC on the low/ground leg - see ntc.h):
       V(A1)/3.3 = raw/4095 = R_ntc/(R_ntc+Rseries)
       => R_ntc = Rseries * raw / (4095 - raw) */
    float rNtc = NTC_RSERIES * (float)raw / (float)(ADC_FULL_SCALE - raw);

    float steinhart = logf(rNtc / NTC_R0) / NTC_BETA + 1.0f / NTC_T0;
    float celsius = 1.0f / steinhart - 273.15f;

    /* Light smoothing - an external sensor on a couple of wires picks up
       noticeably more noise than the CAN-sourced temperatures it ends up
       sharing a telemetry blob with (see Timberline::mqttTelemetryHandler). */
    static float filtered;
    static bool  filterSeeded = false;
    if (!filterSeeded) { filtered = celsius; filterSeeded = true; }
    else                filtered += (celsius - filtered) * 0.25f;

    temperature = (int8_t)(filtered + (filtered >= 0.0f ? 0.5f : -0.5f));
    connected = true;
}
