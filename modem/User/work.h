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

/* PGN 6 answer. Queues the frame(s) this modem actually transmits for
   that PGN: 13, 18, or a PGN 60 sub-packet. sub 0xFF on PGN 60 is every
   periodic sub-packet (0..6). Sub-packet 8 (PUK) only when asked by number.
   Drained from canBroadcast(), one frame per free TX mailbox. */
void replyToPgnRequest(uint16_t pgn, uint8_t sub, uint8_t toType, uint8_t toAddr);

#endif /* __WORK_H */
