#include "main.h"

#define ZONE_COUNT 5

enum zoneState_t {off,heat,vent};
enum heaterState_t {idle,ignition,heating,workOnPower,blowing};

class Timberline
{
	//Control
	uint8_t MbcVersion[4];
	bool HeaterButton;
	bool ElementButton;
	bool FloorButton;
	bool EngineButton;
	zoneState_t zoneStates[ZONE_COUNT];
	uint8_t zoneDaySetpoint[ZONE_COUNT];
	uint8_t zoneNightSetpoint[ZONE_COUNT];
	uint8_t zoneManualFanPercent[ZONE_COUNT];
	bool zoneFanManualMode[ZONE_COUNT];
	uint8_t floorSetpoint;
	uint8_t engineSetpoint;
	uint8_t engineDurationMinutes;
	uint8_t SystemTimeLimitHours; //96+ - unlimited
	//Status
	int8_t zoneCurrentTemp[ZONE_COUNT];
	int8_t engineTemperature;
	uint32_t engineSecondsLeft;
	int8_t floorTemperature;
	float voltage;
	int8_t outdoorTemperature;
	uint32_t systemSecondsLeft;
	int8_t tankTemperature;
	int8_t heaterTemperature;
	heaterState_t heaterState;
	uint8_t zoneFanCurrentPwm[ZONE_COUNT];
};