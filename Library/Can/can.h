/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __CAN_H
#define __CAN_H

/* Includes ------------------------------------------------------------------*/
#include "n32wb452.h"
#include "n32wb452_can.h"

/* Classes ------------------------------------------------------------------*/
class Can
{
    public:
        Can(void);
        void initialize(void);
        void handler(void);
        void SendMessage(uint32_t AID, uint8_t AD0, uint8_t AD1, uint8_t AD2, uint8_t AD3,
                         uint8_t AD4, uint8_t AD5, uint8_t AD6, uint8_t AD7);
        /* Returns true if the frame was actually queued into a hardware TX
           mailbox, false if all 3 were busy (CAN_TxSTS_NoMailBox) and the
           frame was silently dropped — SlcanBridge uses this to only ack
           ('Z'/'z') a 'T'/'t' command once the frame is genuinely on its
           way, instead of always acking regardless (see SlcanBridge.cpp's
           own comment on this, added after 2026-09-02 reports of frequent
           "errors" from CAN Tool specifically while flashing over the
           SLCAN bridge — a burst of back-to-back frames is exactly when
           all 3 mailboxes are most likely to still be busy). */
        bool sendRaw(bool ext, uint32_t id, uint8_t dlc, const uint8_t* data);
        void processCanRxMessage(CanRxMessage *msg);

        /* Toggles CAN_MCTRL_NART (No Automatic Retransmission). Left ENABLE
           by default (see initialize()) for the modem's own traffic — but
           that means a single arbitration loss or bit error on a busy bus
           permanently drops that frame instead of the hardware silently
           retrying it, which is exactly wrong for SLCAN bridge mode: a
           real CAN adapter (see the org's own dedicated one, CAN-adapter's
           can.c, prsf_enable = FALSE = auto-retransmit ON) just lets the
           hardware keep trying. SlcanBridge flips this on entering/leaving
           bridge mode ('O'/'C') rather than changing the default, so the
           modem's own normal operation (whatever "prevents TEC storm" was
           protecting against — see initialize()'s comment) is unaffected
           outside an active bridge session. Safe to call live — NART is a
           mode-control bit, not gated by CAN_MCR's INRQ like the bit-timing
           fields are. */
        void setAutoRetransmit(bool enable);

        /* True if at least one of the 3 hardware TX mailboxes is free right
           now. SendMessage()/sendRaw() don't check this themselves — if all
           3 are still busy transmitting when called, CAN_TransmitMessage()
           silently drops the frame (returns CAN_TxSTS_NoMailBox, which
           neither of them look at) rather than queuing or blocking. Normal
           single/occasional sends never hit this in practice, but anything
           that fires several frames back-to-back (see Timberline::
           doCanRelay()'s PGN=106 fragment streaming) needs to check this
           before each send instead of assuming it always succeeds. */
        bool txReady(void) const;

        uint16_t linkCnt;
        CanRxMessage RxMessage;
		uint8_t idType;
		uint8_t idAddress;

    private:
        CanTxMessage TxMessage;
};
extern Can can;

#endif /* __CAN_H */
