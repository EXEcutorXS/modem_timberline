#include "Timberline.h"
#include "Modem.h"
#include "timberline_sms.h"
#include "log.h"
#include <string.h>

Timberline timberline;

void Timberline::ProcessCanMessage(CanRxMessage msg);
{

}

static void onSmsReceived(const char* phone, const char* text) {
    TlSmsParseResult result;
    tl_sms_parse(phone, text, modem.pin, modem.phones[0], &modem.phones[1], result);

    if (!result.authenticated) {
        log_info("SMS: auth failed\r\n");
        return;
    }

    for (uint8_t e = 0; e < result.errCount; e++) {
        log_info("SMS parse error: "); log_info(result.errors[e]); log_info("\r\n");
    }

    for (uint8_t i = 0; i < result.cmdCount; i++) {
        const TlSmsCmd& cmd = result.cmds[i];
        switch (cmd.type) {

            case TL_CMD_PING:
                modem.sendSms(phone, "pong");
                break;

            case TL_CMD_RESET:
                modem.sendSms(phone, "Resetting...");
                NVIC_SystemReset();
                break;

            case TL_CMD_ADMIN:
                strncpy(modem.phones[0], cmd.phone, 15);
                modem.phones[0][15] = '\0';
                modem.sendSms(phone, "Admin set.");
                break;

            case TL_CMD_PHONE:
                if (cmd.phoneNum >= 1 && cmd.phoneNum <= 4) {
                    strncpy(modem.phones[cmd.phoneNum], cmd.phone, 15);
                    modem.phones[cmd.phoneNum][15] = '\0';
                    modem.sendSms(phone, "Phone updated.");
                }
                break;

            case TL_CMD_SETPIN:
                memcpy(modem.pin, cmd.pin, 5);
                modem.sendSms(phone, "PIN updated.");
                break;

            /* TODO: device control commands */
            case TL_CMD_BURNER:
            case TL_CMD_ELEMENT:
            case TL_CMD_FLOOR_TOGGLE:
            case TL_CMD_FLOOR_SETPOINT:
            case TL_CMD_ENGINE_TOGGLE:
            case TL_CMD_ENGINE_SETPOINT:
            case TL_CMD_ZONE_STATE:
            case TL_CMD_ZONE_FAN_MODE:
            case TL_CMD_ZONE_FAN_PERCENT:
            case TL_CMD_ZONE_DAY_SP:
            case TL_CMD_ZONE_NIGHT_SP:
            case TL_CMD_STATUS:
            case TL_CMD_WARMUP:
            case TL_CMD_FAULTREPORT:
            case TL_CMD_UNIT:
            case TL_CMD_SYSTIMER:
                modem.sendSms(phone, "TODO");
                break;

            default:
                break;
        }
    }
}
 
void Timberline::init(void) {
    modem.onSmsReceived = onSmsReceived;
}
