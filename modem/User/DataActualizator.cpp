#include "DataActualizator.h"
#include "Modem.h"
#include "can.h"
#include "flash.h"
#include "StringTransfer.h"
#include <string.h>

static uint32_t Pgn60Id(void) {
    return (60u<<20) | ((uint32_t)can.idType<<13) | ((uint32_t)can.idAddress<<10)
         | ((uint32_t)can.idType<<3) | can.idAddress;
}

void DataActualizator::ActualizeInternalData(void) {
    /* Wire/struct field keeps the old "onlySmsMode" polarity (1=SMS-only) —
       modem.useInternet uses the opposite sense, invert here at the boundary. */
    newState.onlySmsMode = !modem.useInternet;
    newState.faultReport = modem.faultReport;
    newState.cmdAck       = modem.cmdAck;
    newState.tempUnit     = modem.tempUnit;
    newState.force2gOnly  = modem.force2gOnly;
    newState.allowRoaming = modem.allowRoaming;

    /* Not on CAN yet — see the State comment — but SMS can already change
       these, and a future panel/CAN write path will too, so they're
       change-detected (and, for the string ones, echoed back — see
       handler()) here already rather than each write-site handling it
       itself.

       These specific fields (phones/pin/mqttBroker/mqttUsername/
       mqttPassword) are also exactly the ones StringTransfer can write into
       from the CAN RX interrupt (once a panel write path exists — see
       StringTransfer::onPgn62). Reading them here happens in the plain
       main-loop thread, which that interrupt CAN preempt mid-copy — without
       a guard, this read could see a mix of pre- and post-update bytes if
       the CAN write lands in the middle of it. __disable_irq() for the
       duration of the copy (a few dozen bytes, microseconds) rules that
       out; nothing here is slow enough for the brief interrupt latency hit
       to matter. */
    __disable_irq();
    memcpy(newState.phones, modem.phones, sizeof(newState.phones));
    strncpy(newState.pin, modem.pin, sizeof(newState.pin) - 1);
    newState.pin[sizeof(newState.pin) - 1] = 0;
    strncpy(newState.mqttBroker, modem.mqttBroker, sizeof(newState.mqttBroker) - 1);
    newState.mqttBroker[sizeof(newState.mqttBroker) - 1] = 0;
    strncpy(newState.mqttUsername, modem.mqttUsername, sizeof(newState.mqttUsername) - 1);
    newState.mqttUsername[sizeof(newState.mqttUsername) - 1] = 0;
    strncpy(newState.mqttPassword, modem.mqttPassword, sizeof(newState.mqttPassword) - 1);
    newState.mqttPassword[sizeof(newState.mqttPassword) - 1] = 0;
    strncpy(newState.internetCheckUrl, modem.internetCheckUrl, sizeof(newState.internetCheckUrl) - 1);
    newState.internetCheckUrl[sizeof(newState.internetCheckUrl) - 1] = 0;
    strncpy(newState.connectionLink, modem.connectionLink, sizeof(newState.connectionLink) - 1);
    newState.connectionLink[sizeof(newState.connectionLink) - 1] = 0;
    strncpy(newState.apn, modem.apn, sizeof(newState.apn) - 1);
    newState.apn[sizeof(newState.apn) - 1] = 0;
    strncpy(newState.apnUsername, modem.apnUsername, sizeof(newState.apnUsername) - 1);
    newState.apnUsername[sizeof(newState.apnUsername) - 1] = 0;
    strncpy(newState.apnPassword, modem.apnPassword, sizeof(newState.apnPassword) - 1);
    newState.apnPassword[sizeof(newState.apnPassword) - 1] = 0;
    __enable_irq();

    newState.language = modem.language; /* single byte — already atomic, no guard needed */
}

/* Sub-packet 1: D[1] = 4 флага x 2 бита/bool (00=off,01=on,11=нет данных):
 *   bits0-1 onlySms, bits2-3 faultReport, bits4-5 cmdAck, bits6-7 tempUnit.
 *   D[2] = force2gOnly, D[3] = allowRoaming — plain bytes (0/1, 0xFF=no
 *   data), D[1]'s 4 slots were already full, plenty of spare bytes here. */
void DataActualizator::sendSettings(void) {
    uint8_t d1 = (uint8_t)(  (newState.onlySmsMode ? 1u : 0u)
                            | ((newState.faultReport ? 1u : 0u) << 2)
                            | ((newState.cmdAck       ? 1u : 0u) << 4)
                            | ((uint32_t)(newState.tempUnit & 1)  << 6));
    can.SendMessage(Pgn60Id(), 1, d1, newState.force2gOnly ? 1 : 0,
                     newState.allowRoaming ? 1 : 0, 0xFF, 0xFF, 0xFF, 0xFF);
}

void DataActualizator::handler(void) {
    ActualizeInternalData();

    /* This is the single place flash persistence happens for every field
       this class tracks (the 6 CAN ones plus the 7 SMS-only ones below),
       on purpose — whatever changed them (CAN write from the panel, an SMS
       command, anything else in the future — including a planned panel/CAN
       write path for phones/pin/language/mqtt*), a real value change is
       detected here exactly once and written exactly once. The individual
       write-sites used to each call flash.writeSetup() themselves; the CAN
       path in particular gets re-sent unconditionally (see canBroadcast()'s
       resendSettings() safety net) and would otherwise rewrite flash on
       every single reception regardless of whether anything actually
       changed — fine today, but a real flash-wear risk if something starts
       sending PGN60 sub1 on a regular cadence rather than only on user
       action.

       Each string field is checked *individually*, not folded into one
       combined flag — the planned flow is: panel changes a string -> CAN to
       modem -> modem applies it -> this handler notices *that specific
       field* changed -> persists it -> immediately echoes that same field
       back over StringTransfer, so the panel sees its own change confirmed
       right away instead of waiting for the next unrelated poll/request.
       Collapsing everything into a single bool would still get the flash
       write right, but there'd be no way to know *which* field to echo. */
    bool anyChanged = false;

    bool canFieldsChanged = (oldState.onlySmsMode  != newState.onlySmsMode ||
                              oldState.faultReport  != newState.faultReport ||
                              oldState.cmdAck       != newState.cmdAck      ||
                              oldState.tempUnit     != newState.tempUnit    ||
                              oldState.force2gOnly  != newState.force2gOnly ||
                              oldState.allowRoaming != newState.allowRoaming);
    if (canFieldsChanged) {
        sendSettings();
        anyChanged = true;
    }

    /* phones[0] = admin, phones[1..4] = trusted 1-4 — each has its own
       STRID (registerString() already serves these on-demand; this is the
       "push it back the moment it changes" half of the same field). */
    static const uint16_t phoneStrId[5] = {
        STRID_ADMIN_PHONE, STRID_TRUSTED_PHONE1, STRID_TRUSTED_PHONE2,
        STRID_TRUSTED_PHONE3, STRID_TRUSTED_PHONE4
    };
    for (uint8_t i = 0; i < 5; i++) {
        if (strcmp(oldState.phones[i], newState.phones[i]) != 0) {
            stringTransfer.sendString(newState.phones[i], phoneStrId[i], can.idType, can.idAddress);
            anyChanged = true;
        }
    }

    if (strcmp(oldState.pin, newState.pin) != 0) {
        stringTransfer.sendString(newState.pin, STRID_PIN, can.idType, can.idAddress);
        anyChanged = true;
    }

    if (oldState.language != newState.language) {
        /* No STRID for this — it's a single byte, not a string, and there's
           no live "language changed" push mechanism yet. Still needs
           persisting like everything else here. */
        anyChanged = true;
    }

    if (strcmp(oldState.mqttBroker, newState.mqttBroker) != 0) {
        stringTransfer.sendString(newState.mqttBroker, STRID_MQTT_BROKER, can.idType, can.idAddress);
        anyChanged = true;
    }
    if (strcmp(oldState.mqttUsername, newState.mqttUsername) != 0) {
        stringTransfer.sendString(newState.mqttUsername, STRID_MODEM_LOGIN, can.idType, can.idAddress);
        anyChanged = true;
    }
    if (strcmp(oldState.mqttPassword, newState.mqttPassword) != 0) {
        stringTransfer.sendString(newState.mqttPassword, STRID_MODEM_PASSWORD, can.idType, can.idAddress);
        anyChanged = true;
    }
    if (strcmp(oldState.internetCheckUrl, newState.internetCheckUrl) != 0) {
        stringTransfer.sendString(newState.internetCheckUrl, STRID_INTERNET_CHECK_URL, can.idType, can.idAddress);
        anyChanged = true;
    }
    if (strcmp(oldState.connectionLink, newState.connectionLink) != 0) {
        stringTransfer.sendString(newState.connectionLink, STRID_CONNECTION_LINK, can.idType, can.idAddress);
        anyChanged = true;
    }

    if (strcmp(oldState.apn, newState.apn) != 0) {
        /* No STRID/echo — same as language, see the State comment. */
        anyChanged = true;
    }
    if (strcmp(oldState.apnUsername, newState.apnUsername) != 0) anyChanged = true;
    if (strcmp(oldState.apnPassword, newState.apnPassword) != 0) anyChanged = true;

    if (anyChanged) {
        oldState = newState;
        flash.writeSetup();
    }
}

void DataActualizator::init(void) {
    /* Called once at boot, right after flash.readSetup() has loaded the
       real settings into modem/core. Without this, oldState starts
       zero-initialized (global, no constructor) while newState immediately
       reflects the real loaded values on the first handler() call — every
       phone/pin/mqtt* field and CAN flag would look "changed" from zero,
       triggering a full sendSettings() + a StringTransfer echo for every
       string field + a flash write, on every single power-up. Seed
       oldState from the same loaded values with no sends/writes so the
       first real handler() call only reacts to actual changes. */
    ActualizeInternalData();
    oldState = newState;
}

void DataActualizator::resendSettings(void) {
    ActualizeInternalData();
    sendSettings();
    oldState = newState;
}

DataActualizator dataActualizator;
