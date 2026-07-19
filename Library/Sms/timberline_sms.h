#ifndef TIMBERLINE_SMS_H
#define TIMBERLINE_SMS_H

#include "main.h"
#include <cstdint>

/* ── Limits ─────────────────────────────────────────────────────────────────*/
#define TL_SMS_MAX_COMMANDS  10
#define TL_SMS_MAX_ERRORS     5
#define TL_SMS_ERR_LEN       32

/* ── Enums ──────────────────────────────────────────────────────────────────*/
/* Note: ARMCC v5 does not support enum base types — plain enums used here.  */
enum TlZoneState { TL_ZONE_OFF = 0, TL_ZONE_HEAT = 1, TL_ZONE_VENT = 2 };
enum TlFanMode   { TL_FAN_AUTO = 0, TL_FAN_MANUAL = 1 };
enum TlTempUnit    { TL_UNIT_C = 0,       TL_UNIT_F = 1 };
enum TlWarmupMode  { TL_WARMUP_BURNER = 0, TL_WARMUP_ELEMENT = 1, TL_WARMUP_BOTH = 2 };

enum TlCmdType {
    TL_CMD_NONE = 0,
    TL_CMD_ADMIN,           /* phone[16]              — set admin phone         */
    TL_CMD_PHONE,           /* phoneNum (1-4), phone[16] — set trusted phone   */
    TL_CMD_WARMUP,          /* warmupMode             — warmup burner/element/both */
    TL_CMD_FAULTREPORT,     /* boolVal                — enable fault SMS        */
    TL_CMD_SETPIN,          /* pin[5]                 — change PIN code         */
    TL_CMD_UNIT,            /* unit (TlTempUnit)      — C or F                  */
    TL_CMD_BURNER,          /* boolVal                — diesel burner on/off    */
    TL_CMD_ELEMENT,         /* boolVal                — electric element on/off */
    TL_CMD_FLOOR_TOGGLE,    /* boolVal                — floor heat on/off       */
    TL_CMD_FLOOR_SETPOINT,  /* intVal  2..32 °C (already converted from °F)    — floor target temp */
    TL_CMD_ENGINE_TOGGLE,   /* boolVal                — engine preheat on/off   */
    TL_CMD_ENGINE_SETPOINT, /* intVal  1..80 °C (already converted from °F)    — engine target temp */
    TL_CMD_SYSTIMER,        /* intVal  1..100 h       — system time limit       */
    TL_CMD_ZONE_STATE,      /* zone.num, zone.state   — off/heat/vent           */
    TL_CMD_ZONE_FAN_MODE,   /* zone.num, zone.fanMode — auto/manual             */
    TL_CMD_ZONE_FAN_PERCENT,/* zone.num, zone.percent — 10..100 %               */
    TL_CMD_ZONE_DAY_SP,     /* zone.num, zone.setpoint 10..32 °C (already converted from °F) */
    TL_CMD_ZONE_NIGHT_SP,   /* zone.num, zone.setpoint 10..32 °C (already converted from °F) */
    TL_CMD_STATUS,          /* no params              — request status reply    */
    TL_CMD_PING,            /* no params              — modem info reply        */
    TL_CMD_RESET,           /* no params              — reboot modem            */
    TL_CMD_FACTORY,         /* no params              — reset modem settings    */
    TL_CMD_OFF,             /* no params              — turn everything off      */
    TL_CMD_ACK,             /* boolVal                — enable/disable device command confirmations */
};

/* ── Zone sub-payload ───────────────────────────────────────────────────────*/
struct TlZonePayload {
    uint8_t     num;       /* Zone number: 1..5 */
    TlZoneState state;     /* ZONE_STATE                                  */
    TlFanMode   fanMode;   /* ZONE_FAN_MODE                               */
    uint8_t     percent;   /* ZONE_FAN_PERCENT (10..100)                  */
    int8_t      setpoint;  /* ZONE_DAY_SP / ZONE_NIGHT_SP (10..32)        */
};

/* ── Command payload ────────────────────────────────────────────────────────*/
struct TlSmsCmd {
    TlCmdType    type;
    bool         boolVal;  /* BURNER, ELEMENT, FLOOR_TOGGLE, ENGINE_TOGGLE, FAULTREPORT */
    int8_t       intVal;   /* FLOOR_SETPOINT, ENGINE_SETPOINT, SYSTIMER                 */
    TlTempUnit   unit;       /* UNIT                                                     */
    TlWarmupMode warmupMode; /* WARMUP                                                   */
    uint8_t      phoneNum;   /* PHONE — index 1..4                                       */
    char         phone[16];  /* ADMIN, PHONE                                             */
    char         pin[5];     /* SETPIN                                                   */
    TlZonePayload zone;    /* ZONE_* commands                                            */
};

/* ── Parse result ───────────────────────────────────────────────────────────*/
struct TlSmsParseResult {
    bool     authenticated;                          /* false → wrong PIN or unknown sender  */
    bool     isAdmin;                                /* true  → sender is admin phone        */
    uint8_t  cmdCount;
    TlSmsCmd cmds[TL_SMS_MAX_COMMANDS];
    uint8_t  errCount;
    char     errors[TL_SMS_MAX_ERRORS][TL_SMS_ERR_LEN]; /* human-readable parse errors      */
};

/**
 * Parse an incoming Timberline control SMS.
 *
 * Authentication rules:
 *   - Admin phone  (adminPhone): commands without PIN prefix.
 *   - Any other number: message must start with 4-digit PIN and a space,
 *     e.g. "1234 burner on,z1 heat".
 *   - If adminPhone is NULL or empty the device has no admin set yet;
 *     in that case any sender may use PIN-based auth.
 *
 * Multiple commands are separated by commas.
 * Command text is case-insensitive.
 *
 * @param senderPhone  Sender phone number, e.g. "+79001234567"
 * @param message      Raw SMS body text
 * @param pin          4-digit PIN stored on device, e.g. "1234"
 * @param adminPhone   Admin phone stored on device, NULL / "" if not configured
 * @param tempUnit     Device's current temperature unit (see "unit" command) — used to
 *                     validate/interpret setpoint arguments (floor/engine/z#day/z#night).
 *                     Setpoints are converted to °C here; callers always get °C back.
 * @param result       Output filled by this function
 */
/* trustedPhones: array of 4 phone strings (phones[1..4]), may be NULL or empty strings */
void tl_sms_parse(const char*       senderPhone,
                  const char*       message,
                  const char*       pin,
                  const char*       adminPhone,
                  const char        trustedPhones[][16],
                  TlTempUnit        tempUnit,
                  TlSmsParseResult& result);

#endif /* TIMBERLINE_SMS_H */
