#ifndef FAULT_MANAGER_H
#define FAULT_MANAGER_H

#include <stdint.h>

class FaultManager {
public:
    void handler(void);

private:
    uint8_t prevErrors[8];
    bool    initialized;

    /* Global SMS cooldown — at most one fault SMS per COOLDOWN_MS, regardless
       of which code(s) changed. Simpler than per-code tracking; the user
       already knows to check on the RV after the first text. */
    static const uint32_t COOLDOWN_MS = 60UL * 60UL * 1000UL;   /* 1 hour */
    uint32_t lastSentTick;
    bool     everSent;

    static const char* getText(uint8_t code);
};

extern FaultManager faultManager;

#endif /* FAULT_MANAGER_H */
