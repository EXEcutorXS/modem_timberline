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

	/* ── CAN firmware relay (OTA part 4) ────────────────────────────────
	   Replays whatever's already staged+verified in the modem's own flash
	   (see Modem::ota.stagedValid/stagedVersion/stagedBytes, written by
	   Modem::doOta()) onto MBC-2 over CAN, using the OmniProtocol
	   bootloader sub-protocol (PGN=1 to switch modes, PGN=105 for the
	   set-address/erase/flash/verify commands, PGN=106 for raw fragment
	   bytes) — see doCanRelay() in Timberline.cpp for the full sequence
	   and byte-exact command reference (OmniProtocol.pdf, confirmed
	   against a real working PC flashing tool's source). Deliberately a
	   separate action from the HTTP download (Modem::startOta()) — the
	   user reviews what's staged first, then explicitly triggers this. */
	enum CanRelayStatus { RELAY_IDLE, RELAY_STAGING, RELAY_DONE, RELAY_ERROR };
	struct CanRelayState {
		bool           startRequested; /* same reentrancy rationale as Modem::OtaScratch::startRequested —
		                                   set from onMqttCommandReceived(), which can run mid-parseLine() */
		CanRelayStatus status;
		uint16_t       fragment;      /* current 512-byte fragment index, for progress reporting */
		uint16_t       fragmentTotal;
		uint8_t        retries;
		bool           failed;
		bool           bootloaderSeen;  /* set by ProcessCanMessage() on a PGN=18 reply from device type 123 */
		uint32_t       setAddrEcho;     /* PGN=105 sub1 response fields, set by ProcessCanMessage() */
		bool           setAddrGotResp;
		bool           eraseGotResp;
		uint8_t        eraseResult;
		bool           flashGotResp;
		uint8_t        flashResult;
		bool           checkGotResp;    /* PGN=105 sub3 response — length+CRC of what the bootloader currently has in RAM */
		uint32_t       checkLen;
		uint32_t       checkCrc;
	} canRelay;
	void startCanRelay(void);
	void doCanRelay(void);

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


