#include "main.h"

enum heaterType_t {BinarCompactD=27,BinarCOmactB=23,Binar10D=34,Binar10B=45,BinarSpiltD=43,BinarSplitB=44};

struct HeaterState_t {
	uint8_t version[4];
    heaterType_t type;
    uint8_t stage;
    uint8_t mode;
    int16_t Tflame;
    int16_t Tcpu;
    int16_t Tliquid;
    int16_t Toverheat;
    int16_t BlowerSet;
    int16_t BlowerReal;
    float FPSet;
    uint8_t GlowPlug;
    float Voltage;
    bool PumpState;
    uint8_t errorCode;
    bool pumpFault;
    uint8_t WarningCode;
    uint8_t BlinkCount;
    uint32_t totalTime;
    uint32_t workTime;
    float pressure;
    bool ecoMode;
};

class Heaters
{
public:
    HeaterState_t Instances[7];
};

extern Heaters heaters;

