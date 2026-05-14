/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __PARAMS_NAME_H
#define __PARAMS_NAME_H

#include "main.h"

ALIGN const char START_PARAMS_NAME[] =          "SPN";
/* Parameters measure -----------------------------------------------------------*/
ALIGN const char STAGE[] =                      "Stg"; // Стадия работы подогревателя/отопителя
ALIGN const char PHASE[] =                      "Phs"; // Режим работы подогревателя/отопителя
ALIGN const char STAGE_TIME[] =                 "StT"; // Время работы на стадии
ALIGN const char PHASE_TIME[] =                 "PhT"; // Время работы на режиме
ALIGN const char FAULT_CODE[] =                 "CdF"; // Код неисправности подогревателя/отопителя
ALIGN const char WARNING_CODE[] =               "CdW";
ALIGN const char FLAME_BREAK_BORDER[] =         "FBB";
ALIGN const char CALIBRATION_VALUE_TC1[] =      "CV1";
ALIGN const char CALIBRATION_VALUE_TC2[] =      "CV2";
ALIGN const char CALIBRATION_VALUE_TC3[] =      "CV3";
ALIGN const char TC1[] =                        "TC1";
ALIGN const char TC2[] =                        "TC2";
ALIGN const char TC3[] =                        "TC3";
ALIGN const char TS1[] =                        "TS1";
ALIGN const char TS2[] =                        "TS2";
ALIGN const char TS3[] =                        "TS3";
ALIGN const char VOLTAGE[] =                    "VHt"; // Напряжение питания подогревателя/отопителя
ALIGN const char FUEL_PUMP_AMP[] =              "FPA"; // ток топливного насоса
ALIGN const char TEMPERATURE_CPU[] =            "TCp";
ALIGN const char VOLTAGE_CPU[] =                "VCp";
ALIGN const char VOLTAGE_REFERENCE[] =          "VRf";
ALIGN const char PRESS_SENS_TEMPERATURE[] =     "TPS";
ALIGN const char PRESS_SENS_PRESSURE[] =        "PPS";
ALIGN const char FAN_REVOLUTIONS[] =            "FRv";
ALIGN const char FREQUENCE_FUEL_PUMP[] =        "FFP";
ALIGN const char PERCENT_GLOW_PLUG[] =          "PGP";
ALIGN const char PERCENT_WATER_PUMP[] =         "PWP";
ALIGN const char TEMPERATURE_PANEL[] =          "TPn";
ALIGN const char FLAME_STATE[] =                "FlS";
ALIGN const char FUEL_PUMP_STATE[] =            "FPS";
ALIGN const char GLOW_PLUG_STATE[] =            "GPS";
ALIGN const char ENGINE_AUTOMOBILE_STATE[] =    "EAS";

ALIGN const char STATUS_WARMING_UP[] =          "SWU"; // BLE
ALIGN const char UNIX_TIME[] =                  "UnT"; // BLE
ALIGN const char BLE_ACCEPT[] =                 "BlA"; // BLE
ALIGN const char BLE_CANCEL[] =                 "BlC"; // BLE
ALIGN const char BLE_ID[] =                     "BId"; // BLE
ALIGN const char TIME_WORK[] =                  "TWr"; // BLE
ALIGN const char TEMPERATURE_BY_WORK[] =        "TBW"; // BLE

/* Parameters in flash memory -----------------------------------------------------------*/
// серийный номер отопителя=
ALIGN const char ID_HEATER[] =                  "IDH";
// назначение датчиков температуры
// (0-не используется, 1-вход воздуха, 2-выход воздуха, 3-корпуса, 4-перегрева, 5-жидкости, 6-индикатор пламени, 7-внешний датчик)
ALIGN const char PIN_TC1[] =                    "PC1";
ALIGN const char PIN_TC2[] =                    "PC2";
ALIGN const char PIN_TC3[] =                    "PC3";
ALIGN const char PIN_TS1[] =                    "PS1";
ALIGN const char PIN_TS2[] =                    "PS2";
ALIGN const char PIN_TS3[] =                    "PS3";
// назначение входов (0-не используется, 1-есть)
ALIGN const char PIN_IN_SIGNALING[] =           "PIS";
ALIGN const char PIN_IN_ENGINE[] =              "PIE";
// назначение выходов (0-не используется, 1-есть)
ALIGN const char PIN_OUT_RELAY[] =              "POR";
ALIGN const char PIN_OUT_PUMP[] =               "POP";
ALIGN const char PIN_OUT_HIGH[] =               "POH";
ALIGN const char PIN_OUT_OFF[] =                "POO";
ALIGN const char PIN_OUT_IGNITION[] =           "POI";
ALIGN const char PIN_OUT_LITTLE[] =             "POL";
ALIGN const char PIN_OUT_STRONG[] =             "POS";
ALIGN const char PIN_SENS_FLAME[] =             "PSF"; // источник определения наличия горения (0-не используется, 1-термопара, 2-давление, 3-давление и термопара)
ALIGN const char PIN_SENS_FUEL_PUMP[] =         "PSP"; // определять заполнение топливного насоса (0-не используется, 1-есть)
ALIGN const char PIN_PULSE_FUEL_PUMP[] =        "PPP"; // длительность импульса топливного насоса в мс
ALIGN const char PIN_TYPE_FAN[] =               "PTF"; // тип двигателя нагнетателя (0-бесколлекторный, 1-коллекторный)
ALIGN const char PIN_FREQ_FAN[] =               "PFF"; // частота ШИМ нагнетателя
ALIGN const char PIN_MODE_FAN[] =               "PMF"; // режим управления нагнетателем(0-ШИМ, 1-отрезанный ШИМ, 2-одновибратор)
ALIGN const char PIN_VOLTAGE_GLOW_PLUG[] =      "VGP"; // напряжение свечи

ALIGN const char FLAME_DETECTION_H[] =          "FDH"; // порог дельта Т определения горения на Н
ALIGN const char FLAME_FAILURE_H[] =            "FFH"; // порог дельта Т определения срыва на Н
ALIGN const char FLAME_FAILURE_W[] =            "FFW"; // порог дельта Т определения срыва на W
ALIGN const char OVERHEAT_LOW_IN[] =            "OLI"; // снижение мощности при Т входа по перегреву
ALIGN const char OVERHEAT_WAIT_IN[] =           "OWI"; // ожидание при Т входа по перегреву
ALIGN const char OVERHEAT_WAIT_CASE[] =         "OWC"; // ожидание при Т корпуса по перегреву
ALIGN const char OVERHEAT_WAIT_OUT[] =          "OWO"; // ожидание при Т выхода по перегреву
ALIGN const char OVERHEAT_START_IN[] =          "OSI"; // выход из ожидания при Т входа по перегреву
ALIGN const char OVERHEAT_START_CASE[] =        "OSC"; // выход из ожидания при Т корпуса по перегреву
ALIGN const char OVERHEAT_START_OUT[] =         "OSO"; // выход из ожидания при Т выхода по перегреву
ALIGN const char OVERHEAT_WAIT_TIME[] =         "OWT"; // минимальное время на ждущем по перегреву
ALIGN const char OVERHEAT_WAIT_REVOLUTION[] =   "OWR"; // обороты на ждущем по перегреву

//------ Блок управления отоплением ELWELL --------------------------------------------------------------------
ALIGN const char HCU_HEATER_STATE[] =           "FHS"; //+ состояние подогревателя (включен/выключен)
ALIGN const char HCU_ACH_STATE[] =              "ACS"; //+ состояние ТЭНа (включен/выключен)
ALIGN const char HCU_DWATER_STATE[] =           "DWS"; //  состояние нагрева воды для бытовых нужд (включен/выключен)
ALIGN const char HCU_ZONE0_STATE[] =            "Z0S"; //+ состояние отопления помещения (Zone 0) (включен/выключен)
ALIGN const char HCU_ZONE0_SETPOINT[] =         "ASP"; //+ уставка температуры для отопления помещения
ALIGN const char HCU_FAN_MANUAL[] =             "FMn"; //  ручное управление отоплением помещения
ALIGN const char HCU_FAN_MANUAL_PWR[] =         "FMP"; //  уставка мощности при ручном управлении отоплением помещения
ALIGN const char HCU_PUMP_MANUAL[] =            "PMn"; //  ручное управление помпой

ALIGN const char HCU_TANK_T[] =                 "TnT"; //+ температура бака
ALIGN const char HCU_EXCHANGER_T[] =            "ExT"; //+ температура теплообменника
ALIGN const char HCU_AIR_T[] =                  "ArT"; //+ температура воздуха в помещении


/* Settings -----------------------------------------------------------*/
ALIGN const char COUNTER_LOCK_STARTUP[] =       "CLS"; // счетчик кол-ва неудачных попыток розжига
ALIGN const char COUNTER_RETRY_STARTUP[] =      "CRS"; // счетчик кол-ва повторных попыток розжига
ALIGN const char SETUP_WORK_TIME[] =            "SWT"; // BLE // длительность работы в минутах
ALIGN const char SETUP_POWER[] =                "SPw"; // BLE // мощность работы (от 1 до ...)
ALIGN const char AIR_SETPOINT_TEMPERATURE[] =   "AST"; // BLE !!!! изменено // уставка температуры
ALIGN const char SETUP_MODE[] =                 "SMd"; // BLE // режим работы (по мощности, датчикам...)

ALIGN const char RUNNING_TIME_MIN[] =           "RMn"; // длительность работы минимум
ALIGN const char RUNNING_TIME_MAX[] =           "RMx"; // длительность работы максимум
ALIGN const char POWER_STEPS[] =                "PwS"; // кол-во ступеней мощности
ALIGN const char TSETPOINT_MIN[] =              "SpL"; // минимальная уставка температуры
ALIGN const char TSETPOINT_MAX[] =              "SpH"; // максимальная уставка температуры

ALIGN const char WORK_UNLIMITED[] =             "WUn"; // BLE
ALIGN const char WARM_UP_AUTO[] =               "WUA"; // BLE
ALIGN const char WARM_UP_MANUAL[] =             "WUM"; // BLE
ALIGN const char WARM_UP_SETPOINT_T[] =         "WST"; // BLE
ALIGN const char PREHEATER_SETPOINT_T[] =       "PST"; // BLE
ALIGN const char PUMP_IN_STANDBY[] =            "PIS"; // BLE
ALIGN const char PUMP_ON_ENGINE[] =             "POE"; // BLE
ALIGN const char TURN_ON_FURNACE[] =            "TOF"; // BLE
ALIGN const char FURNACE_SETPONT_T[] =          "FST"; // BLE
ALIGN const char EXTERNAL_OPERATING[] =         "EOp"; // BLE


ALIGN const char TIMER_IS_UNLIMITED[] =         "TUn"; // BLE
ALIGN const char TIMER1_DAY[] =                 "T1D"; // BLE
ALIGN const char TIMER2_DAY[] =                 "T2D"; // BLE
ALIGN const char TIMER3_DAY[] =                 "T3D"; // BLE
ALIGN const char TIMER1_HOUR[] =                "T1H"; // BLE
ALIGN const char TIMER2_HOUR[] =                "T2H"; // BLE
ALIGN const char TIMER3_HOUR[] =                "T3H"; // BLE
ALIGN const char TIMER1_MINUTE[] =              "T1M"; // BLE
ALIGN const char TIMER2_MINUTE[] =              "T2M"; // BLE
ALIGN const char TIMER3_MINUTE[] =              "T3M"; // BLE
ALIGN const char TIMER1_ON[] =                  "T1O"; // BLE
ALIGN const char TIMER2_ON[] =                  "T2O"; // BLE
ALIGN const char TIMER3_ON[] =                  "T3O"; // BLE
ALIGN const char TIMERS_IS_ON[] =               "TsO"; // BLE

ALIGN const char TEMPERATURE_IS_CELSIUS[] =     "TIC"; // BLE

/* Commands -----------------------------------------------------------*/
ALIGN const char CLEAR_PARAMETER_MEMORY[] =     "ClP";
ALIGN const char CLEAR_FACTORY_MEMORY[] =       "ClF";
ALIGN const char CLEAR_SETTING_MEMORY[] =       "ClS";
ALIGN const char CLEAR_MEASURE_MEMORY[] =       "ClM";

ALIGN const char RESET_SEND_MEASURE[] =         "RSM";

ALIGN const char START_HEATER[] =               "SHt"; // BLE
ALIGN const char START_VENTILATION[] =          "SVn"; // BLE
ALIGN const char START_WATER_PUMP[] =           "SWP"; // BLE
ALIGN const char START_FUEL_PUMP[] =            "SFP";
ALIGN const char FINISH_HEATER[] =              "FHt"; // BLE
ALIGN const char FINISH_HEATER_FAULT[] =        "FHF";

ALIGN const char RESET_CPU[] =                  "Rst";
ALIGN const char PAUSE_CORE[] =                 "PCr";
ALIGN const char START_CORE[] =                 "SCr";
ALIGN const char RESET_CORE[] =                 "RCr";

ALIGN const char CALIBRATION_TC[] =             "CTC";

ALIGN const char SET_BAUDRATE[] =               "SBr";

ALIGN const char RESET_PANEL[] =                "RPn"; // BLE
ALIGN const char RESET_HEATER[] =               "RHt"; // BLE
ALIGN const char BOOT_PANEL[] =                 "BPn"; // BLE

ALIGN const char END_PARAMS_NAME[] =            "EPN";


#endif /* __PARAMS_NAME_H */
