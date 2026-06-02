#ifndef __WORK_H
#define __WORK_H

#include "n32wb452.h"

class Work_C
{
    public:
        Work_C(void);
        void initialize(void);
        void handler(void);

    private:
        void resetHandler(void);
        void canBroadcast(void);
};

extern Work_C work;

#endif /* __WORK_H */
