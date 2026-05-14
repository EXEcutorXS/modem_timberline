/* Define to prevent recursive inclusion --------------------------*/
#ifndef __FLASHSETUP_H
#define __FLASHSETUP_H

/* Includes -------------------------------------------------------*/
#include "n32wb452.h"
/* Define -------------------------------------------------------- */
#define FLASH_SERIAL_ADDR               0x807F000       // Length 2 KB
#define FLASH_SETUP_ADDR                0x807F800       // Length 2 KB

/* Classes --------------------------------------------------------*/
class Flash_C
{
    public:
        Flash_C(void);
        void initialize(void);
        void handler(void);
        void writeSetup(void);
        void readSetup(void);
        void writeSerial(void);
        void readSerial(void);
        uint8_t getHardwareVersion(void);

    private:
        

};
extern Flash_C flash;

#endif /* __FLASHSETUP_H */
