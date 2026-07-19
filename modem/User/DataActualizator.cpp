#include "DataActualizator.h"
#include "Modem.h"
#include "can.h"

static uint32_t Pgn60Id(void) {
    return (60u<<20) | ((uint32_t)can.idType<<13) | ((uint32_t)can.idAddress<<10)
         | ((uint32_t)can.idType<<3) | can.idAddress;
}

void DataActualizator::ActualizeInternalData(void) {
    newState.onlySmsMode = modem.isOnlySmsMode;
    newState.faultReport = modem.faultReport;
    newState.cmdAck       = modem.cmdAck;
    newState.tempUnit     = modem.tempUnit;
}

/* Sub-packet 1: D[1] = 4 флага x 2 бита/bool (00=off,01=on,11=нет данных):
 *   bits0-1 onlySms, bits2-3 faultReport, bits4-5 cmdAck, bits6-7 tempUnit */
void DataActualizator::sendSettings(void) {
    uint8_t d1 = (uint8_t)(  (newState.onlySmsMode ? 1u : 0u)
                            | ((newState.faultReport ? 1u : 0u) << 2)
                            | ((newState.cmdAck       ? 1u : 0u) << 4)
                            | ((uint32_t)(newState.tempUnit & 1)  << 6));
    can.SendMessage(Pgn60Id(), 1, d1, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF);
}

void DataActualizator::handler(void) {
    ActualizeInternalData();

    if (oldState.onlySmsMode != newState.onlySmsMode ||
        oldState.faultReport != newState.faultReport ||
        oldState.cmdAck       != newState.cmdAck      ||
        oldState.tempUnit     != newState.tempUnit) {
        sendSettings();
        oldState = newState;
    }
}

void DataActualizator::resendSettings(void) {
    ActualizeInternalData();
    sendSettings();
    oldState = newState;
}

DataActualizator dataActualizator;
