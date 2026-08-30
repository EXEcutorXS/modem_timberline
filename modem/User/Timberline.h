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
/* Wire order per real CAN documentation (D[5] of PGN 21, cast directly —
   see ProcessCanMessage) — NOT alphabetical/logical order, matches the
   4 actual states: 0 idle, 1 blowing, 2 ignition/warming (one combined
   state, not two), 3 work on power. Originally declared in a different,
   wrong order ({idle,ignition,heating,workOnPower,blowing}, 5 values) —
   fixed 2026-07-27 after the user checked it against documentation; that
   bug affected both this and stageStr()'s SMS "status" burner-stage text. */
enum heaterStateIcon_t {idle,blowing,ignition,workOnPower};

class Timberline
{
	public:
	void init();
	void ProcessCanMessage(CanRxMessage* msg);
	void sendStatus(const char* phone, bool german = false);
	void mqttActualizerHandler(void);
	void mqttTelemetryHandler(void);

	/* Generic device discovery — every PGN=18 announcement (any device
	   type, not just the MBC-2/heater-family special cases handled
	   elsewhere in ProcessCanMessage()) updates this table via
	   recordSeenDevice(), which also publishes "dev<type>_<addr>" =
	   "<v1>.<v2>.<v3>.<v4>" over MQTT whenever a slot is new or its
	   version changes. This is the source list the web UI's device picker
	   is built from — the generalized OTA relay (see CanRelay::handler())
	   can target any device on the bus this way, not only MBC-2. */
	enum { SEEN_DEVICE_MAX = 8 };
	struct SeenDevice {
		bool    active;
		uint8_t type;
		uint8_t address;
		uint8_t version[4];
	};
	SeenDevice seenDevices[SEEN_DEVICE_MAX];
	void recordSeenDevice(uint8_t type, uint8_t address, const uint8_t* version);

	/* Passive discovery: called for every incoming CAN frame (any PGN,
	   see ProcessCanMessage) with its sender's type+address. The first
	   time a given type+address is seen, claims its seenDevices[] slot
	   (so it isn't re-queried on every subsequent frame from the same
	   device) and sends it a targeted PGN=6 [0,18] version request —
	   answered via PGN=18, same as an unprompted self-announcement,
	   caught by the existing case 18 handler. Devices that don't
	   implement PGN=6 (e.g. PU-28's control-panel firmware) still get
	   picked up via the periodic PGN=1 [0,0] broadcast query in
	   work.cpp's canBroadcast() instead — this is a faster, less noisy
	   complement to that, not a replacement. */
	void maybeQueryNewDevice(uint8_t type, uint8_t address);

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
	uint16_t engineDurationMinutes; /* up to 1450 (PGN19 D[0]=3: D[4]*256+D[5]) — wider than uint8_t on purpose */
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
	/* Dedicated floor-heating-pump / engine-preheat-pump status — separate
	   from pumpState[PUMP_COUNT] above (PGN 19 sub-packet 5, D[5] bits 0-1
	   and 2-3) because which of the generic PUMP1-4/AUX pumps physically
	   drives floor/engine wiring differs per installation, so the HCU
	   reports these two as their own fixed fields instead. */
	bool floorPumpState;
	bool enginePumpState;
	uint32_t elementSeconds;
	uint8_t liquidLevel;
	uint8_t errors[8];
};

extern Timberline timberline;


