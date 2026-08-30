#include "main.h"

/* ── CAN firmware relay (OTA part 4) ────────────────────────────────
   Replays whatever's already staged+verified in the modem's own flash
   (see Modem::ota.stagedValid/stagedVersion/stagedBytes, written by
   Modem::doOta()) onto a target device over CAN, using the OmniProtocol
   bootloader sub-protocol (PGN=1 to switch modes, PGN=105 for the
   set-address/erase/flash/verify commands, PGN=106 for raw fragment
   bytes) — see handler() in CanRelay.cpp for the full sequence and
   byte-exact command reference (OmniProtocol.pdf, confirmed against a
   real working PC flashing tool's source). Deliberately a separate action
   from the HTTP download (Modem::startOta()) — the user reviews what's
   staged first, then explicitly triggers this.

   Split out of Timberline into its own class (2026-08-29, user's call:
   "Ota находится в классе Timberline. Это не правильно. Надо вынести в
   отдельный. У него должен быть свой парсер CAN сообщений.") — it owns
   its own CAN message parser (ProcessCanMessage()), called independently
   from Can::processCanRxMessage() alongside Timberline::ProcessCanMessage()
   on every incoming frame, rather than Timberline forwarding
   relay-relevant frames to it. Each class's ProcessCanMessage() looks at
   the same raw CanRxMessage and picks out only what it cares about. */
class CanRelay
{
	public:
	enum Status { RELAY_IDLE, RELAY_STAGING, RELAY_DONE, RELAY_ERROR };
	/* Which named step of handler()'s sequence we're currently in, while
	   status==RELAY_STAGING — published as "canRelayStep" (see
	   Timberline::mqttActualizerHandler) purely for the web UI to show the
	   user something more specific than "Flashing…" the whole time (see
	   handler()'s own comment for exactly where each phase gets set).
	   Doesn't affect control flow at all — step (the function-local
	   sub-state machine) already does that; this is a read-only mirror of
	   "which named stretch of it we're in" for display. */
	enum Phase { RELAY_PHASE_SWITCHING, RELAY_PHASE_DETECTED, RELAY_PHASE_ERASING,
	             RELAY_PHASE_ERASED, RELAY_PHASE_TRANSFERRING, RELAY_PHASE_RETURNING };

	bool     startRequested; /* same reentrancy rationale as Modem::OtaScratch::startRequested —
	                             set from Timberline::onMqttCommandReceived(), which can run mid-parseLine() */
	uint8_t  targetType;    /* device type + CAN address to relay onto, from the "<type>:<address>"
	                            canRelayStart payload */
	uint8_t  targetAddress;
	Status   status;
	Phase    phase;
	uint16_t fragment;      /* current 512-byte fragment index, for progress reporting */
	uint16_t fragmentTotal;
	uint8_t  retries;       /* per-fragment retry count — reset to 0 at the start of each
	                            new fragment (case 10), unlike totalFails below */
	uint16_t totalFails;    /* running count of failed attempts across the WHOLE relay
	                            (erase/setaddr/verify/flash — any retry point), never reset
	                            mid-relay — published as "canRelayFails" so the web UI can
	                            show retries happening even though they don't show up in
	                            canRelayProgress (that only counts completed fragments) */
	bool     failed;
	bool     bootloaderSeen;  /* set by ProcessCanMessage() on a PGN=18 reply from device type 123 */
	uint8_t  bootloaderVersion[4]; /* that reply's own version quad — looked up in
	                                   Modem::BUILTIN_BOOTLOADERS to pick a safe algorithm,
	                                   see handler() step 1 */
	uint8_t  algorithm;       /* which bootloader-flashing protocol generation to speak, set
	                              once from Modem::lookupBootloaderAlgorithm() at detection
	                              (handleDetect()) — 2: gen2, handled by handleGen2()
	                              (PGN=105/106, the custom running-sum checksum). 3: gen3,
	                              handled by handleGen3() (PGN=110/111, same sub-command
	                              numbering as 105/106 but a real CRC32 instead of the custom
	                              checksum, and its own erase-memory variant — D[1]==1,
	                              "erase program only", the bootloader itself knows which
	                              sectors that means, no explicit sector list needed unlike
	                              gen2's D[1]==255 "erase sectors 2-7"). Deliberately two
	                              separate state machines, not one branching on this field
	                              per-case — see handler()'s own comment for why. */
	uint32_t setAddrEcho;     /* PGN=105/110 sub1 response fields, set by ProcessCanMessage() */
	bool     setAddrGotResp;
	bool     eraseGotResp;
	uint8_t  eraseResult;
	bool     flashGotResp;
	uint8_t  flashResult;
	bool     checkGotResp;    /* PGN=105 sub3 response — length+CRC of what the bootloader currently has in RAM */
	uint32_t checkLen;
	uint32_t checkCrc;
	int8_t   detectStep;      /* handleDetect()'s own step — a class member rather than a
	                              local static (unlike each generation handler's own step,
	                              see their own comment) because it needs an explicit reset
	                              from handler()'s "start a fresh relay" block: phase stays
	                              RELAY_PHASE_SWITCHING for detectStep's *entire* duration
	                              (not just its first tick), so handleDetect() itself has no
	                              reliable way to tell "just starting" apart from "still
	                              mid-poll" the way handleGen2()/handleGen3() can (their
	                              fresh-entry signal, phase==RELAY_PHASE_DETECTED, is only
	                              ever true for one tick). Reset to 0 explicitly instead. */

	void start(uint8_t targetType, uint8_t targetAddress);
	void handler(void);
	void ProcessCanMessage(CanRxMessage* msg);

	private:
	/* Two independent, self-contained state machines (2026-08-30, see
	   handler()'s own comment for why they're not one machine branching
	   on `algorithm`) plus the shared detection phase and finish helpers
	   both call into. Not meant to be called from outside handler(). */
	void handleDetect(void);
	void handleGen2(void);
	void handleGen3(void);
	void finishSuccess(void);
	void finishFailed(void);
};

extern CanRelay canRelay;
