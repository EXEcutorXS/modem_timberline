#include "FaultManager.h"
#include "Timberline.h"
#include "Modem.h"
#include "core.h"
#include "log.h"
#include <string.h>

FaultManager faultManager;

/* ── Error code → text ──────────────────────────────────────────────── */
struct FaultEntry { uint8_t code; const char* text; };

static const FaultEntry FAULTS[] = {
    {  1, "Overheat" },
    {  2, "Overheat" },
    {  3, "Error of the overheat temp sensor" },
    {  4, "Error of the liquid temp sensor" },
    {  5, "Open circuit of the flame temp sensor" },
    {  9, "Glow plug error" },
    { 10, "Fan speed does not correspond to the defined" },
    { 12, "High supply voltage" },
    { 13, "No ignition" },
    { 14, "Water pump error" },
    { 15, "Low supply voltage" },
    { 16, "Body temp sensor does not cool down" },
    { 17, "Short circuit of the fuel pump" },
    { 22, "Open circuit of the fuel pump" },
    { 27, "Fan does not rotate" },
    { 28, "Fan self-rotation" },
    { 29, "Exceeding the limit of flame blowout" },
    { 36, "Overheating of the flame indicator" },
    { 40, "No connection with the heater" },
    { 45, "Open circuit of the tank temp sensor" },
    { 46, "Short circuit of the tank temp sensor" },
    { 53, "Open circuit of the flow sensor" },
    { 54, "Short circuit of the flow sensor" },
    { 55, "Open circuit of the air temp sensor" },
    { 56, "Short circuit of the air temp sensor" },
    { 57, "Short circuit of the zone 1 temp sensor" },
    { 58, "Open circuit of the zone 1 temp sensor" },
    { 59, "Short circuit of the zone 2 temp sensor" },
    { 60, "Open circuit of the zone 2 temp sensor" },
    { 61, "Short circuit of the zone 3 temp sensor" },
    { 62, "Open circuit of the zone 3 temp sensor" },
    { 63, "Short circuit of the zone 4 temp sensor" },
    { 64, "Open circuit of the zone 4 temp sensor" },
    { 65, "Short circuit of the zone 5 temp sensor" },
    { 66, "Open circuit of the zone 5 temp sensor" },
    { 69, "Short circuit of the pump 1" },
    { 70, "Open circuit of the pump 1" },
    { 71, "Short circuit of the pump 2" },
    { 72, "Open circuit of the pump 2" },
    { 73, "Short circuit of the pump 3" },
    { 74, "Open circuit of the pump 3" },
    { 75, "Short circuit of the pump 4" },
    { 76, "Open circuit of the pump 4" },
    { 77, "Short circuit of the pump 5" },
    { 78, "Open circuit of the pump 5" },
    { 79, "Short circuit of the fan 1" },
    { 80, "Open circuit of the fan 1" },
    { 81, "Short circuit of the fan 2" },
    { 82, "Open circuit of the fan 2" },
    { 83, "Short circuit of the fan 3" },
    { 84, "Open circuit of the fan 3" },
    { 85, "Short circuit of the fan 4" },
    { 86, "Open circuit of the fan 4" },
    { 87, "Short circuit of the fan 5" },
    { 88, "Open circuit of the fan 5" },
    { 91, "Liquid level too low" },
    { 92, "Liquid level too high" },
    { 93, "Level sensor short circuit" },
    { 94, "Level sensor open circuit" },
    { 95, "Open circ aux temp sensor 1" },
    { 96, "Short circ aux temp sensor 1" },
    { 97, "Open circ aux temp sensor 2" },
    { 98, "Short circ aux temp sensor 2" },
    { 99, "Open circ aux temp sensor 3" },
    {100, "Short circ aux temp sensor 3" },
    {101, "Open circ aux temp sensor 4" },
    {102, "Short circ aux temp sensor 4" },
    {103, "Extension board link fault" },
    {104, "Zone-pump conflict" },
    {200, "No connection with inverter" },
    {201, "Evaporator fan short circuit" },
    {202, "Evaporator fan open circuit" },
    {203, "Evaporator fan overcurrent" },
    {204, "Evaporator sensor SC" },
    {205, "Evaporator sensor OC" },
    {206, "Cabin temp sensor SC" },
    {207, "Cabin temp sensor OC" },
    {210, "Compressor short circuit" },
    {211, "Single phase OC" },
    {212, "Compressor overcurrent" },
    {213, "Condensor fan SC" },
    {214, "Condensor fan OC" },
    {215, "Condensor fan overcurrent" },
    {216, "Low voltage" },
    {217, "Inverter PCB overheat" },
};

static const uint8_t FAULTS_COUNT = sizeof(FAULTS) / sizeof(FAULTS[0]);

const char* FaultManager::getText(uint8_t code) {
    for (uint8_t i = 0; i < FAULTS_COUNT; i++)
        if (FAULTS[i].code == code) return FAULTS[i].text;
    return 0;
}

/* ── handler ─────────────────────────────────────────────────────────── */
void FaultManager::handler(void) {
    /* On first call — just snapshot current state, don't send SMS */
    if (!initialized) {
        initialized = true;
        for (uint8_t i = 0; i < 8; i++) prevErrors[i] = timberline.errors[i];
        return;
    }

    if (!modem.faultReport) {
        for (uint8_t i = 0; i < 8; i++) prevErrors[i] = timberline.errors[i];
        return;
    }

    if (!modem.phones[0][0]) return;   /* no admin phone set */

    uint32_t now = core.getTick();
    if (everSent && (now - lastSentTick) < COOLDOWN_MS) {
        /* Already texted within the last hour — don't spam while the user
           clears/re-checks a fault; they already know to look at the RV. */
        for (uint8_t i = 0; i < 8; i++) prevErrors[i] = timberline.errors[i];
        return;
    }

    /* Collect new errors (present now but not before) */
    static char msg[141];
    uint8_t len = 0;

    for (uint8_t i = 0; i < 8; i++) {
        uint8_t code = timberline.errors[i];
        if (code == 0 || code == prevErrors[i]) continue;

        const char* text = getText(code);

        /* Build "E<code>: <text>\n", truncate at 140 chars */
        char line[48];
        uint8_t n = 0;
        line[n++] = 'E';
        if (code >= 100) { line[n++] = '0' + code/100; }
        if (code >= 10)  { line[n++] = '0' + (code/10)%10; }
        line[n++] = '0' + code%10;
        line[n++] = ':'; line[n++] = ' ';
        if (text) {
            for (uint8_t k = 0; text[k] && n < 46; k++) line[n++] = text[k];
        } else {
            line[n++] = '?';
        }
        line[n++] = '\n'; line[n] = 0;

        if (len + n > 139) break;   /* no room — stop here */
        for (uint8_t k = 0; k < n; k++) msg[len++] = line[k];
    }
    msg[len] = 0;

    if (len > 0) {
        log_info("FAULT SMS: "); log_info(msg); log_info("\r\n");
        modem.sendSms(modem.phones[0], msg);
        lastSentTick = now;
        everSent     = true;
    }

    for (uint8_t i = 0; i < 8; i++) prevErrors[i] = timberline.errors[i];
}
