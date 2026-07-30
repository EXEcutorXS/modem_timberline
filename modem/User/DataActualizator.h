#ifndef DATA_ACTUALIZATOR_H
#define DATA_ACTUALIZATOR_H

#include <stdint.h>

/* Отправляет PGN60 sub-packet 1 (настройки) сразу при изменении — сравнивает
 * newState/oldState и шлёт только при отличии. Периодический повтор (на
 * случай, если пульт пропустил рассылку) — забота canBroadcast(), см.
 * resendSettings(). */
class DataActualizator {
public:
    void init(void);             /* seed oldState from flash-loaded values at boot, no sends/writes */
    void handler(void);
    void resendSettings(void);   /* force a re-send with the current values; called by canBroadcast() on a timer */

private:
    struct State {
        bool    onlySmsMode;
        bool    faultReport;
        bool    cmdAck;
        uint8_t tempUnit;
        bool    force2gOnly;
        bool    allowRoaming;
        /* Not on CAN yet (SMS-only today) — tracked here anyway so flash
           writes for these are already change-detected/centralized before
           a panel/CAN write path for them exists. See sendSettings(): only
           the 6 fields above go out over CAN; these never trigger it. */
        char    phones[5][16];
        char    pin[5];
        uint8_t language;
        char    mqttBroker[32];
        char    mqttUsername[16];
        char    mqttPassword[24];
        char    internetCheckUrl[64];
        /* Generated rarely (once per "getlink" SMS), but worth persisting
           anyway — without this it silently vanishes from the panel on
           every power cycle even though nothing actually changed. */
        char    connectionLink[96];
        /* Explicit PDP context APN override (the "apn" SMS command); empty =
           auto. Like language: no STRID/live push yet, just needs its
           change persisted like everything else here. */
        char    apn[32];
        char    apnUsername[32];  /* "apnuser" SMS command */
        char    apnPassword[32];  /* "apnpass" SMS command */
    };

    State oldState;
    State newState;

    void ActualizeInternalData(void);
    void sendSettings(void);
};

extern DataActualizator dataActualizator;

#endif /* DATA_ACTUALIZATOR_H */
