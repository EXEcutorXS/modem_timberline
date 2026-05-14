#ifndef __RANDOMIZE_H
#define __RANDOMIZE_H

#include "n32wb452.h"
#include "main.h"

class Randomize
{
    public:
        Randomize(void);
        void initialize(void);
        uint32_t getValue(void);
    
        static const int ARRAY_SIZE = 32;
        
        uint16_t array[ARRAY_SIZE];

    private:
        
};

extern Randomize randomize;


#endif /* __RANDOMIZE_H */
