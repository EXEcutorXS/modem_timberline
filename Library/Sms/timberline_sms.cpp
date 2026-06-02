#include "timberline_sms.h"
#include <string.h>

/* ═══════════════════════════════════════════════════════════════════════════
   Private helpers
   ═══════════════════════════════════════════════════════════════════════════*/

/* Lowercase ASCII in-place */
static void str_lower(char* s)
{
    for (; *s; ++s)
        if (*s >= 'A' && *s <= 'Z') *s += 32;
}

/* Trim leading and trailing whitespace in-place */
static void str_trim(char* s)
{
    /* leading */
    int start = 0;
    while (s[start] == ' ' || s[start] == '\t') start++;
    if (start) {
        int len = (int)strlen(s) - start;
        memmove(s, s + start, (size_t)(len + 1));
    }
    /* trailing */
    int len = (int)strlen(s);
    while (len > 0 && (s[len-1] == ' '  || s[len-1] == '\t' ||
                       s[len-1] == '\r' || s[len-1] == '\n'))
        s[--len] = '\0';
}

/**
 * Compare the last 10 digits of two phone numbers.
 * Handles +7 / 8 / no-prefix variations.
 * Returns false if either string is shorter than 10 chars.
 */
static bool phones_match(const char* a, const char* b)
{
    int la = (int)strlen(a);
    int lb = (int)strlen(b);
    if (la < 10 || lb < 10) return false;
    return strncmp(a + la - 10, b + lb - 10, 10) == 0;
}

/** Parse "on"/"1" → true, "off"/"0" → false. Returns false if unrecognised. */
static bool parse_bool(const char* s, bool& out)
{
    if (!strcmp(s, "on")  || !strcmp(s, "1")) { out = true;  return true; }
    if (!strcmp(s, "off") || !strcmp(s, "0")) { out = false; return true; }
    return false;
}

/** Parse non-negative decimal integer. Returns false if empty or non-numeric. */
static bool parse_uint(const char* s, int& out)
{
    if (!s || !s[0]) return false;
    int val = 0;
    for (int i = 0; s[i]; i++) {
        if (s[i] < '0' || s[i] > '9') return false;
        val = val * 10 + (s[i] - '0');
    }
    out = val;
    return true;
}

/**
 * Split src by commas into at most maxSegs segments.
 * Replaces ',' with '\0' in src. Returns actual segment count.
 * The last segment (after last comma) is always included.
 */
static int split_comma(char* src, char* segs[], int maxSegs)
{
    if (!src || !src[0] || maxSegs <= 0) return 0;

    int count = 0;
    segs[count++] = src;

    for (char* p = src; *p; p++) {
        if (*p == ',' && count < maxSegs) {
            *p = '\0';
            segs[count++] = p + 1;
        }
    }
    return count;
}

/**
 * Split segment into command token and argument string at the first space.
 * Both cmd and arg point into the segment buffer (no allocation).
 * arg points to "" (empty string) if there is no argument.
 */
static void split_cmd_arg(char* seg, char** cmd, char** arg)
{
    *cmd = seg;
    char* sp = strchr(seg, ' ');
    if (sp) {
        *sp = '\0';
        *arg = sp + 1;
        while (**arg == ' ') (*arg)++;   /* trim extra leading spaces in arg */
    } else {
        *arg = seg + strlen(seg);        /* points to '\0' — empty arg */
    }
}

/** Parse zone character: '1'..'5' → 1..5, 'x' → TL_ZONE_ALL. */
static bool parse_zone_char(char c, uint8_t& znum)
{
    if (c >= '1' && c <= '5') { znum = (uint8_t)(c - '0'); return true; }
    if (c == 'x')             { znum = TL_ZONE_ALL;         return true; }
    return false;
}

/* ── Error / command helpers ───────────────────────────────────────────────*/

static void add_error(TlSmsParseResult& res, const char* msg)
{
    if (res.errCount < TL_SMS_MAX_ERRORS) {
        strncpy(res.errors[res.errCount], msg, TL_SMS_ERR_LEN - 1);
        res.errors[res.errCount][TL_SMS_ERR_LEN - 1] = '\0';
        res.errCount++;
    }
}

static void add_cmd(TlSmsParseResult& res, const TlSmsCmd& cmd)
{
    if (res.cmdCount < TL_SMS_MAX_COMMANDS)
        res.cmds[res.cmdCount++] = cmd;
    else
        add_error(res, "too many commands");
}

/* ═══════════════════════════════════════════════════════════════════════════
   Single command parser
   Receives lowercase, trimmed cmd token and arg string.
   ═══════════════════════════════════════════════════════════════════════════*/
static void parse_one(char* cmd, char* arg, TlSmsParseResult& res)
{
    TlSmsCmd c;
    memset(&c, 0, sizeof(c));
    int ival;
    bool bval;

    /* ── status / ? ─────────────────────────────────────────────────────── */
    if (!strcmp(cmd, "status") || !strcmp(cmd, "?")) {
        c.type = TL_CMD_STATUS; add_cmd(res, c); return;
    }

    /* ── ping / reset / factory ─────────────────────────────────────────── */
    if (!strcmp(cmd, "ping"))    { c.type = TL_CMD_PING;    add_cmd(res, c); return; }
    if (!strcmp(cmd, "reset"))   { c.type = TL_CMD_RESET;   add_cmd(res, c); return; }
    if (!strcmp(cmd, "factory")) { c.type = TL_CMD_FACTORY; add_cmd(res, c); return; }

    /* ── phone# <phone>  (phone1..phone5 — trusted phones) ────────────── */
    if (cmd[0] == 'p' && cmd[1] == 'h' && cmd[2] == 'o' && cmd[3] == 'n' &&
        cmd[4] == 'e' && cmd[5] >= '1' && cmd[5] <= '5' && cmd[6] == '\0') {
        if (!arg[0]) { add_error(res, "phone#: missing number"); return; }
        c.type     = TL_CMD_PHONE;
        c.phoneNum = (uint8_t)(cmd[5] - '0');
        strncpy(c.phone, arg, 15);
        c.phone[15] = '\0';
        add_cmd(res, c);
        return;
    }

    /* ── warmup [burner|element|both] ───────────────────────────────────── */
    if (!strcmp(cmd, "warmup")) {
        c.type = TL_CMD_WARMUP;
        if      (!arg[0] || !strcmp(arg, "burner"))  c.warmupMode = TL_WARMUP_BURNER;
        else if (!strcmp(arg, "element"))             c.warmupMode = TL_WARMUP_ELEMENT;
        else if (!strcmp(arg, "both"))                c.warmupMode = TL_WARMUP_BOTH;
        else { add_error(res, "warmup: burner/element/both"); return; }
        add_cmd(res, c);
        return;
    }

    /* ── admin <phone> ──────────────────────────────────────────────────── */
    if (!strcmp(cmd, "admin")) {
        if (!arg[0]) { add_error(res, "admin: missing phone"); return; }
        c.type = TL_CMD_ADMIN;
        strncpy(c.phone, arg, 15);
        c.phone[15] = '\0';
        add_cmd(res, c);
        return;
    }

    /* ── faultreport on/off ─────────────────────────────────────────────── */
    if (!strcmp(cmd, "faultreport")) {
        if (!parse_bool(arg, bval)) { add_error(res, "faultreport: on/off"); return; }
        c.type = TL_CMD_FAULTREPORT;
        c.boolVal = bval;
        add_cmd(res, c);
        return;
    }

    /* ── setpin <4 digits> ──────────────────────────────────────────────── */
    if (!strcmp(cmd, "setpin")) {
        if (strlen(arg) != 4) { add_error(res, "setpin: need 4 digits"); return; }
        for (int i = 0; i < 4; i++)
            if (arg[i] < '0' || arg[i] > '9') { add_error(res, "setpin: digits only"); return; }
        c.type = TL_CMD_SETPIN;
        memcpy(c.pin, arg, 4);
        c.pin[4] = '\0';
        add_cmd(res, c);
        return;
    }

    /* ── unit C/F ───────────────────────────────────────────────────────── */
    if (!strcmp(cmd, "unit")) {
        if      (!strcmp(arg, "c")) { c.type = TL_CMD_UNIT; c.unit = TL_UNIT_C; add_cmd(res, c); }
        else if (!strcmp(arg, "f")) { c.type = TL_CMD_UNIT; c.unit = TL_UNIT_F; add_cmd(res, c); }
        else add_error(res, "unit: C or F");
        return;
    }

    /* ── off — turn everything off ─────────────────────────────────────── */
    if (!strcmp(cmd, "off")) {
        c.type = TL_CMD_OFF; add_cmd(res, c); return;
    }

    /* ── ack on/off ─────────────────────────────────────────────────────── */
    if (!strcmp(cmd, "ack")) {
        if (!parse_bool(arg, bval)) { add_error(res, "ack: on/off"); return; }
        c.type = TL_CMD_ACK; c.boolVal = bval; add_cmd(res, c); return;
    }

    /* ── burner on/off ──────────────────────────────────────────────────── */
    if (!strcmp(cmd, "burner")) {
        if (!parse_bool(arg, bval)) { add_error(res, "burner: on/off"); return; }
        c.type = TL_CMD_BURNER; c.boolVal = bval; add_cmd(res, c);
        return;
    }

    /* ── element on/off ─────────────────────────────────────────────────── */
    if (!strcmp(cmd, "element")) {
        if (!parse_bool(arg, bval)) { add_error(res, "element: on/off"); return; }
        c.type = TL_CMD_ELEMENT; c.boolVal = bval; add_cmd(res, c);
        return;
    }

    /* ── floor on/off  |  floor 2..32 ──────────────────────────────────── */
    if (!strcmp(cmd, "floor")) {
        if (parse_bool(arg, bval)) {
            c.type = TL_CMD_FLOOR_TOGGLE; c.boolVal = bval; add_cmd(res, c);
        } else if (parse_uint(arg, ival) && ival >= 2 && ival <= 32) {
            c.type = TL_CMD_FLOOR_SETPOINT; c.intVal = (int8_t)ival; add_cmd(res, c);
        } else {
            add_error(res, "floor: on/off or 2-32");
        }
        return;
    }

    /* ── engine on/off  |  engine 0..80 ────────────────────────────────── */
    /* Note: "engine 0" is interpreted as off (bool check first);
             setpoint range starts from 1 to avoid ambiguity.             */
    if (!strcmp(cmd, "engine")) {
        if (parse_bool(arg, bval)) {
            c.type = TL_CMD_ENGINE_TOGGLE; c.boolVal = bval; add_cmd(res, c);
        } else if (parse_uint(arg, ival) && ival >= 1 && ival <= 80) {
            c.type = TL_CMD_ENGINE_SETPOINT; c.intVal = (int8_t)ival; add_cmd(res, c);
        } else {
            add_error(res, "engine: on/off or 1-80");
        }
        return;
    }

    /* ── systimer 1..100 ────────────────────────────────────────────────── */
    if (!strcmp(cmd, "systimer")) {
        if (parse_uint(arg, ival) && ival >= 1 && ival <= 100) {
            c.type = TL_CMD_SYSTIMER; c.intVal = (int8_t)ival; add_cmd(res, c);
        } else {
            add_error(res, "systimer: 1-100");
        }
        return;
    }

    /* ── Zone commands (all start with 'z') ─────────────────────────────── */
    if (cmd[0] == 'z' && cmd[1] != '\0') {
        uint8_t znum;
        int     cmdlen = (int)strlen(cmd);

        /* z#day <10..32>   cmd = "z1day" (len 5) or "zxday" (len 5) */
        if (cmdlen == 5 && !strcmp(cmd + 2, "day") && parse_zone_char(cmd[1], znum)) {
            if (parse_uint(arg, ival) && ival >= 10 && ival <= 32) {
                c.type = TL_CMD_ZONE_DAY_SP;
                c.zone.num = znum;
                c.zone.setpoint = (int8_t)ival;
                add_cmd(res, c);
            } else {
                add_error(res, "z#day: 10-32");
            }
            return;
        }

        /* z#night <10..32>  cmd = "z1night" (len 7) or "zxnight" (len 7) */
        if (cmdlen == 7 && !strcmp(cmd + 2, "night") && parse_zone_char(cmd[1], znum)) {
            if (parse_uint(arg, ival) && ival >= 10 && ival <= 32) {
                c.type = TL_CMD_ZONE_NIGHT_SP;
                c.zone.num = znum;
                c.zone.setpoint = (int8_t)ival;
                add_cmd(res, c);
            } else {
                add_error(res, "z#night: 10-32");
            }
            return;
        }

        /* z# <arg>  cmd = "z1".."z5" or "zx"  (len 2) */
        if (cmdlen == 2 && parse_zone_char(cmd[1], znum)) {
            c.zone.num = znum;

            if      (!strcmp(arg, "off") || !strcmp(arg, "0")) {
                c.type = TL_CMD_ZONE_STATE;    c.zone.state   = TL_ZONE_OFF;
            } else if (!strcmp(arg, "heat") || !strcmp(arg, "on") || !strcmp(arg, "1")) {
                c.type = TL_CMD_ZONE_STATE;    c.zone.state   = TL_ZONE_HEAT;
            } else if (!strcmp(arg, "vent")) {
                c.type = TL_CMD_ZONE_STATE;    c.zone.state   = TL_ZONE_VENT;
            } else if (!strcmp(arg, "auto")) {
                c.type = TL_CMD_ZONE_FAN_MODE; c.zone.fanMode = TL_FAN_AUTO;
            } else if (!strcmp(arg, "manual")) {
                c.type = TL_CMD_ZONE_FAN_MODE; c.zone.fanMode = TL_FAN_MANUAL;
            } else if (parse_uint(arg, ival) && ival >= 10 && ival <= 100) {
                c.type = TL_CMD_ZONE_FAN_PERCENT; c.zone.percent = (uint8_t)ival;
            } else {
                add_error(res, "z#: bad arg");
                return;
            }
            add_cmd(res, c);
            return;
        }
    }

    /* ── Unknown ─────────────────────────────────────────────────────────── */
    char err[TL_SMS_ERR_LEN];
    memset(err, 0, sizeof(err));
    strncpy(err, "unknown: ", TL_SMS_ERR_LEN - 1);
    strncat(err, cmd, TL_SMS_ERR_LEN - 1 - (int)strlen(err));
    add_error(res, err);
}

/* ═══════════════════════════════════════════════════════════════════════════
   Public entry point
   ═══════════════════════════════════════════════════════════════════════════*/
void tl_sms_parse(const char*       senderPhone,
                  const char*       message,
                  const char*       pin,
                  const char*       adminPhone,
                  const char        trustedPhones[][16],
                  TlSmsParseResult& result)
{
    memset(&result, 0, sizeof(result));

    if (!message || !message[0]) return;

    /* ── Authentication ─────────────────────────────────────────────────── */
    bool isAdmin = (adminPhone  && adminPhone[0]  &&
                    senderPhone && senderPhone[0] &&
                    phones_match(senderPhone, adminPhone));

    /* Check trusted phones (phones[1..5]) — authenticated but not admin */
    bool isTrusted = false;
    if (!isAdmin && trustedPhones && senderPhone && senderPhone[0]) {
        for (int i = 0; i < 5; i++) {
            if (trustedPhones[i][0] && phones_match(senderPhone, trustedPhones[i])) {
                isTrusted = true;
                break;
            }
        }
    }

    const char* cmdStart = message;

    if (!isAdmin && !isTrusted) {
        /* Unknown sender must prefix with PIN + space */
        size_t pinLen = pin ? strlen(pin) : 0;
        if (pinLen != 4) return;                          /* no valid PIN stored */
        if (strncmp(message, pin, 4) != 0) return;        /* wrong PIN           */
        if (message[4] != ' ')            return;         /* no space separator  */
        cmdStart = message + 5;
    }

    result.authenticated = true;
    result.isAdmin       = isAdmin;

    /* ── Copy command text into a mutable local buffer ──────────────────── */
    char buf[512];
    strncpy(buf, cmdStart, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    /* ── Split by comma ─────────────────────────────────────────────────── */
    char* segs[TL_SMS_MAX_COMMANDS];
    int   segCount = split_comma(buf, segs, TL_SMS_MAX_COMMANDS);

    /* ── Parse each segment ─────────────────────────────────────────────── */
    for (int i = 0; i < segCount; i++) {
        str_trim(segs[i]);
        if (!segs[i][0]) continue;

        str_lower(segs[i]);

        char* cmd;
        char* arg;
        split_cmd_arg(segs[i], &cmd, &arg);

        parse_one(cmd, arg, result);
    }
}
