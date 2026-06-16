#ifndef DATA_ACTUALIZATOR_H
#define DATA_ACTUALIZATOR_H

#include <stdint.h>

/* Отправляет PGN60 sub-packet 1 (настройки) сразу при изменении, а не по
 * таймеру — сравнивает newState/oldState и шлёт только при отличии. */
class DataActualizator {
public:
    void handler(void);

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
