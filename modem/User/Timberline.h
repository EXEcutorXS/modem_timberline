#include "main.h"

#define ZONE_COUNT 5
#define PUMP_COUNT 8

#define PUMP1 0
#define PUMP2 1
#define PUMP3 2
#define PUMP4 3
#define HEATER_PUMP 4
#define AUX_PUMP1 5
#define AUX_PUMP2 6
#define AUX_PUMP3 7

enum zoneState_t {off,heat,vent};
enum heaterStateIcon_t {idle,ignition,heating,workOnPower,blowing};

class Timberline
{
	public:
	void init();
	void ProcessCanMessage(CanRxMessage* msg);
	void sendStatus(const char* phone, bool german = false);
	
	//Control
	
	bool HeaterButton;
	bool ElementButton;
	bool FloorButton;
	bool EngineButton;
	bool EcoButton;
	bool StorageButton;
	zoneState_t zoneStates[ZONE_COUNT];
	uint8_t zoneDaySetpoint[ZONE_COUNT];
	uint8_t zoneNightSetpoint[ZONE_COUNT];
	uint8_t zoneManualFanPercent[ZONE_COUNT];
	bool zoneFanManualMode[ZONE_COUNT];
	uint8_t floorSetpoint;
	uint8_t floorHysteresis;
	uint8_t engineSetpoint;
	uint8_t engineDurationMinutes;
	uint8_t SystemTimeLimitHours; //96+ - unlimited
	uint8_t pumpForceDurationMinutes;
	uint8_t mainHeaterNum;
	uint8_t dayStartMinute;
	uint8_t dayStartHour;
	uint8_t nightStartMinute;
	uint8_t nightStartHour;
	//Status
	uint8_t MbcVersion[4];
	uint8_t hcuType;
	uint8_t hcuAddress;
	bool connected;
	bool elementState;
	bool elementDisabled;
	bool DomesticWaterFlow;
	bool DomesticWaterButton;
	bool floorConnected;
	bool engineConnected;
	int8_t zoneCurrentTemp[ZONE_COUNT];
	int8_t zoneConnected[ZONE_COUNT];
	int8_t engineTemperature;
	uint32_t engineSecondsLeft;
	int8_t floorTemperature;
	int8_t outdoorTemperature;
	uint32_t systemSecondsLeft;
	int8_t tankTemperature;
	heaterStateIcon_t heaterStateIcon;
	uint8_t zoneFanCurrentPwm[ZONE_COUNT];
	bool pumpState[PUMP_COUNT];
	bool pumpForceFlag[PUMP_COUNT];
	uint32_t elementSeconds;
	uint8_t liquidLevel;
	uint8_t errors[8];
};

extern Timberline timberline;


