#ifndef STRING_TRANSFER_H
#define STRING_TRANSFER_H

#include <stdint.h>

/* Generic long-string transfer engine, PGN 61 (control) + PGN 62 (data).
 * Portable: depends only on a global `can` object exposing
 * SendMessage(id,8 bytes), idType, idAddress, and core.getTick().
 *
 * Wire format
 * -----------
 * PGN61 D[0]=1 "announce" (sent by whoever is about to push a string):
 *   D[1] reserved, D[2-3]=string id (LE), D[4-5]=length in bytes (LE),
 *   D[6]=encoding (0=ASCII,1=UTF8,2=UTF16,3=Win1251), D[7] reserved.
 *
 * PGN61 D[0]=2 "request" (sent by whoever wants a string):
 *   D[1] reserved, D[2-3]=string id (LE), D[4-7] reserved.
 *
 * PGN62 "data": D[0-1]=string id (LE), D[2]=packet number,
 *   D[3-7]=5 data bytes (absolute byte index = packet number * 5 + n).
 *
 * Packets are paced >=5 ms apart per the bus-load requirement.
 *
 * API
 * ---
 * registerString(id, buffer, size) — associates a string id with a memory
 *   buffer. Serves two roles at once:
 *     - if this node OWNS the string, an incoming PGN61 "request" for that
 *       id is answered automatically by pushing the current buffer content;
 *     - if this node WANTS the string, the buffer is overwritten in place
 *       whenever a transfer for that id completes.
 * sendString(string, id, toType, toAddress) — unconditionally push a string
 *   to a given node (no request needed).
 * requestString(id, fromType, fromAddress) — ask a node to send us a string
 *   we don't have yet; retried automatically until it arrives.
 * onCanMessage(canId, D) — feed every received CAN frame here (from the RX
 *   interrupt or the project's message dispatcher); frames that aren't
 *   PGN 61/62 are ignored.
 * handler() — call periodically; paces outgoing packets and retries timed
 *   out requests.
 */

/* IDs are fixed by the shared modem<->panel parameter table — keep the
   numbering identical in the panel's copy of this enum. */
enum StringId
{
    STRID_IMEI               = 1,   /* MODEM_IMEI */
    STRID_PIN                = 2,   /* MODEM_PIN */
    STRID_ADMIN_PHONE        = 3,
    STRID_TRUSTED_PHONE1     = 4,
    STRID_TRUSTED_PHONE2     = 5,
    STRID_TRUSTED_PHONE3     = 6,
    STRID_TRUSTED_PHONE4     = 7,
    STRID_INTERNET_CHECK_URL = 8,   /* not implemented on the modem yet */
    STRID_MQTT_BROKER        = 9,   /* not implemented on the modem yet */
    STRID_MODEM_LOGIN        = 10,  /* not implemented on the modem yet */
    STRID_MODEM_PASSWORD     = 11,  /* not implemented on the modem yet */
    STRID_LAST_REC_SMS_TEXT  = 12,
    STRID_LAST_REC_SMS_NUM   = 13,
    STRID_LAST_SENT_SMS_TEXT = 14,
    STRID_LAST_SENT_SMS_NUM  = 15,
    STRID_OPERATOR_NAME      = 16,
    STRID_OPERATOR_CODE      = 17,
};

class StringTransfer
{
public:
    void registerString(uint16_t stringId, char* buffer, uint16_t bufferSize);
    void sendString(const char* string, uint16_t stringId, uint8_t toType, uint8_t toAddress);
    void requestString(uint16_t stringId, uint8_t fromType, uint8_t fromAddress);
    void onCanMessage(uint32_t canId, const uint8_t* D);
    void handler(void);

private:
    /* MAX_LEN=161 is the hard ceiling: onPgn62's 32-packet receivedMask bitmask
       allows at most 32*5=160 data bytes (+1 for the terminator) — enough for
       a full SMS body (STRID_LAST_*_SMS_TEXT). MAX_REGS covers every id the
       modem/panel currently register plus headroom for the table's unused ids. */
    enum { MAX_REGS = 16, MAX_LEN = 161 };

    struct RegEntry
    {
        uint16_t id;
        char*    buffer;
        uint16_t size;
    };
    RegEntry regs[MAX_REGS];
    uint8_t  regCount;

    struct
    {
        bool     active;
        uint16_t id;
        char     data[MAX_LEN];
        uint16_t length;
        uint16_t packetsTotal;
        uint16_t packetNum;
        uint8_t  toType;
        uint8_t  toAddress;
        uint32_t lastSendTick;
    } tx;

    /* Only one outgoing transfer can be "active" at a time (see tx above).
       sendString() queues here instead of clobbering an in-flight transfer;
       handler() starts the next queued one once tx finishes. */
    enum { MAX_PENDING = 3 };
    struct PendingSend
    {
        uint16_t id;
        char     data[MAX_LEN];
        uint8_t  toType;
        uint8_t  toAddress;
    };
    PendingSend pending[MAX_PENDING];
    uint8_t     pendingCount;

    struct
    {
        bool     active;
        uint16_t id;
        uint16_t length;
        uint16_t packetsTotal;
        uint32_t receivedMask;
        uint8_t  fromType;
        uint8_t  fromAddress;
        uint32_t requestTick;
        uint8_t  retries;
    } rx;

    /* Only one incoming request can be "active" at a time (see rx above).
       requestString() queues here instead of clobbering an in-flight wait;
       advanceRxQueue() starts the next queued one once rx finishes or gives up. */
    enum { MAX_PENDING_RX = 4 };
    struct PendingRequest
    {
        uint16_t id;
        uint8_t  fromType;
        uint8_t  fromAddress;
    };
    PendingRequest pendingRx[MAX_PENDING_RX];
    uint8_t        pendingRxCount;

    RegEntry* findEntry(uint16_t stringId);
    uint32_t  buildId(uint8_t PGN, uint8_t toType, uint8_t toAddress) const;
    void      beginSend(const char* string, uint16_t stringId, uint8_t toType, uint8_t toAddress);
    void      sendRequestFrame(uint16_t stringId, uint8_t fromType, uint8_t fromAddress);
    void      beginRequest(uint16_t stringId, uint8_t fromType, uint8_t fromAddress);
    void      advanceRxQueue(void);
    void      onPgn61(uint8_t fromType, uint8_t fromAddress, const uint8_t* D);
    void      onPgn62(const uint8_t* D);
};

extern StringTransfer stringTransfer;

#endif /* STRING_TRANSFER_H */
