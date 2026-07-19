#ifndef DATA_ACTUALIZATOR_H
#define DATA_ACTUALIZATOR_H

#include <stdint.h>

/* Отправляет PGN60 sub-packet 1 (настройки) сразу при изменении — сравнивает
 * newState/oldState и шлёт только при отличии. Периодический повтор (на
 * случай, если пульт пропустил рассылку) — забота canBroadcast(), см.
 * resendSettings(). */
class DataActualizator {
public:
    void handler(void);
    void resendSettings(void);   /* force a re-send with the current values; called by canBroadcast() on a timer */

private:
    struct State {
        bool    onlySmsMode;
        bool    faultReport;
        bool    cmdAck;
        uint8_t tempUnit;
    };

    State oldState;
    State newState;

    void ActualizeInternalData(void);
    void sendSettings(void);
};

extern DataActualizator dataActualizator;

#endif /* DATA_ACTUALIZATOR_H */
