#ifndef FAULT_MANAGER_H
#define FAULT_MANAGER_H

#include <stdint.h>

class FaultManager {
public:
    void handler(void);

private:
    uint8_t prevErrors[8];
    bool    initialized;

    static const char* getText(uint8_t code);
};

extern FaultManager faultManager;

#endif /* FAULT_MANAGER_H */
