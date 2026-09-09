'use strict';

/* ── PU28 Timberline BLE protocol ─────────────────────────────────────────
   Reverse-engineered from the firmware: User/Bluetoorh/bluetooth.h,
   User/Bluetoorh/BluetoothHandler.cpp, User/Main/sample_service.c
   (PU28-Timberline repository). See also tools/ble_debug/*.py in the same
   repo — this page implements the same protocol, but as a full control
   panel rather than a debug console.

   sample_service.c is not part of PU-28.uvprojx (not compiled into the
   GD32 firmware) — the GATT server actually lives in the firmware of a
   separate BLE module (BlueNRG-132, see bluetooth.cpp — the GD32 is just
   an SPI bridge to it), this file in the repo only documents what's
   flashed onto it. The service/characteristic UUIDs from it are confirmed
   against real hardware; the advertised name is not: on real hardware the
   panel shows up as "Autoterm PU" (the retail brand), not "Timberline" —
   see the filter in doConnectPicker().

   GATT: one service, two characteristics, 20-byte packets, byte[0] = type.
   TX (device→browser, NOTIFY) and RX (browser→device, WRITE NO RESPONSE)
   reuse the same type numbers but with different field meanings — kept
   clearly separated in the comments below.

   The look (icons, zone cards, setpoint drum pickers) is deliberately
   copied from the main MQTT control panel (../app.js) — the same ICONS SVG
   set, the same drum-row inertia physics, the same "never show a change as
   applied until the device itself confirms it in telemetry" principle
   (desiredValues/pending-spinner below).
*/
const SERVICE_UUID = 'd973f2e0-b19e-11e2-9e96-0800200c9a66';
const CHAR_TX_UUID = 'd973f2e1-b19e-11e2-9e96-0800200c9a66'; // device -> browser, NOTIFY
const CHAR_RX_UUID = 'd973f2e2-b19e-11e2-9e96-0800200c9a66'; // browser -> device, WRITE

const PACKET_TYPE = {
  SETUP: 1, STATUS: 2, WORK: 3, FIRMWARE: 8, ID: 5, TIME: 7, CONN_STATUS: 9,
  REBOOT: 11, MEMORY: 12, MEMORY_DATA: 13,
  FRAG_INIT: 14, FRAG_DATA: 15, FRAG_MAP: 16, FRAG_CRC: 17, FRAG_PROGRAM: 18, FRAG_DATA_ACK: 19,
};

const ZONE_COUNT = 5;

/* Error table — ported from User/Activity/ErrorsPage.h + the English
   STR_ERR_* strings from User/Text/strings.cpp. num>=0 means "append the
   zone/pump number to the text", same as getErrorText() on the device. */
const ERROR_LIST = [
  [57, 'SC zone sensor', 1], [59, 'SC zone sensor', 2], [61, 'SC zone sensor', 3],
  [63, 'SC zone sensor', 4], [65, 'SC zone sensor', 5],
  [58, 'OC zone sensor', 1], [60, 'OC zone sensor', 2], [62, 'OC zone sensor', 3],
  [64, 'OC zone sensor', 4], [66, 'OC zone sensor', 5],
  [79, 'SC zone fan', 1], [81, 'SC zone fan', 2], [83, 'SC zone fan', 3],
  [85, 'SC zone fan', 4], [87, 'SC zone fan', 5],
  [80, 'OC zone fan', 1], [82, 'OC zone fan', 2], [84, 'OC zone fan', 3],
  [86, 'OC zone fan', 4], [88, 'OC zone fan', 5],
  [69, 'SC pump', 1], [71, 'SC pump', 2], [73, 'SC pump', 3], [75, 'SC pump', 4], [77, 'SC pump', 5],
  [70, 'OC pump', 1], [72, 'OC pump', 2], [74, 'OC pump', 3], [76, 'OC pump', 4], [78, 'OC pump', 5],
  [91, 'Liquid level low', -1], [93, 'Level sensor SC', -1], [94, 'Level sensor OC', -1],
  [53, 'Flow sensor OC', -1], [54, 'Flow sensor SC', -1],
  [45, 'Tank sensor OC', -1], [46, 'Tank sensor SC', -1],
  [55, 'Outdoor sensor OC', -1], [56, 'Outdoor sensor SC', -1],
  [40, 'No heater connect.', -1],
  [1, 'Overheat', -1], [2, 'Overheat', -1],
  [13, 'No ignition', -1], [29, 'Flame break limit', -1], [16, 'Body sensor no cool', -1],
  [36, 'Flame sensor overh.', -1], [27, 'Fan not rotating', -1], [28, 'Fan self-rotation', -1],
  [10, 'Fan speed no corr.', -1], [17, 'Fuel pump SC', -1], [22, 'Fuel pump OC', -1],
  [9, 'Glow plug error', -1], [4, 'Liquid sensor error', -1], [3, 'Overheat sensor err', -1],
  [5, 'Flame sensor OC', -1], [15, 'Low voltage', -1], [12, 'High voltage', -1],
  [14, 'Water pump error', -1],
];
function errorText(code) {
  if (!code) return null;
  const e = ERROR_LIST.find((row) => row[0] === code);
  if (!e) return `Error ${code} (no description)`;
  return e[2] >= 0 ? `${e[1]} ${e[2]}` : e[1];
}

/* ── Device state, assembled from telemetry (types 1/2/3/8) ──────────────
   Commands are built as "read this state, apply one change, send the whole
   packet back" — almost every field within one packet type is applied
   unconditionally on the device (see BluetoothHandler.cpp case 1/2/3), so a
   partial write without knowing the rest of the fields would silently wipe
   them. */
const state = {
  daySetpoint: [null, null, null, null, null],
  nightSetpoint: [null, null, null, null, null],
  zoneTemp: [null, null, null, null, null],
  // 0 = no zone, 1 = fan zone, 2 = defrost (not user-controllable, hidden
  // just like 0 — see connectedZones()), 3 = radiator (no fan).
  zoneConnected: [0, 0, 0, 0, 0],
  zoneState: [0, 0, 0, 0, 0],       // 0=off, 1=heat, 2=vent
  fanManual: [false, false, false, false, false],
  fanPercent: [0, 0, 0, 0, 0],
  errors: [0, 0, 0, 0, 0],
  outdoorTemp: null,
  heaterOn: false, elementOn: false, domesticWaterOn: false, floorOn: false, engineOn: false,
  dayStartHour: 7, dayStartMinute: 0, nightStartHour: 22, nightStartMinute: 0,
  systemTimeLimit: 24,
  floorSetpoint: 25, floorHysteresis: 2,
  engineSetpoint: 20, engineTimeLimit: 60,
  underfloorConnected: false, engineConnected: false,
  heaterVersion: null, panelVersion: null, hcuVersion: null,
  haveSetup: false, haveStatus: false, haveWork: false,
};

/* ── localStorage: this app's own id plus the pairing key handed back by
   the device, so the panel doesn't have to be confirmed again on every
   reconnect (see the flow below). The key is stored per device.id —
   BluetoothDevice's id is stable for one physical device + this origin. */
function randomHex(nBytes) {
  const arr = new Uint8Array(nBytes);
  crypto.getRandomValues(arr);
  return Array.from(arr, (b) => b.toString(16).padStart(2, '0')).join('');
}
function hexToBytes(hex) {
  const out = new Uint8Array(hex.length / 2);
  for (let i = 0; i < out.length; i++) out[i] = parseInt(hex.substr(i * 2, 2), 16);
  return out;
}
function getOwnId() {
  let id = localStorage.getItem('pu28ble_own_id');
  if (!id || id.length !== 16) {
    id = randomHex(8);
    localStorage.setItem('pu28ble_own_id', id);
  }
  return hexToBytes(id);
}
function getStoredKey(deviceId) {
  const hex = localStorage.getItem('pu28ble_key_' + deviceId);
  return hex ? hexToBytes(hex) : new Uint8Array(8); // all-zero key if never paired yet
}
function setStoredKey(deviceId, bytes) {
  localStorage.setItem('pu28ble_key_' + deviceId, Array.from(bytes, (b) => b.toString(16).padStart(2, '0')).join(''));
}
function forgetStoredKey(deviceId) {
  localStorage.removeItem('pu28ble_key_' + deviceId);
}

/* ── Parsing incoming packets (device -> browser) ─────────────────────── */
function parsePacket(b) {
  const ptype = b[0];

  if (ptype === 0xFF) { setConnLabel('BLE bridge reports: not connected', 'disconnected'); return ptype; }

  switch (ptype) {
    case PACKET_TYPE.SETUP: {
      for (let i = 0; i < ZONE_COUNT; i++) {
        state.daySetpoint[i] = b[1 + i];
        state.nightSetpoint[i] = b[6 + i];
        state.zoneTemp[i] = b[11 + i] - 40;
      }
      state.errors[0] = b[16];
      const flags = b[17];
      state.heaterOn = !!(flags & 0x01);
      state.elementOn = !!(flags & 0x02);
      state.domesticWaterOn = !!(flags & 0x04);
      state.floorOn = !!(flags & 0x20);
      state.engineOn = !!(flags & 0x40);
      state.outdoorTemp = b[18] - 40;
      state.haveSetup = true;
      break;
    }
    case PACKET_TYPE.STATUS: {
      for (let i = 0; i < ZONE_COUNT; i++) {
        state.fanPercent[i] = b[1 + i] & 0x7F;
        state.fanManual[i] = !!(b[1 + i] & 0x80);
      }
      for (let i = 0; i < ZONE_COUNT; i++) state.errors[i] = b[7 + i];
      state.zoneConnected[0] = b[12] & 0x0F;
      state.zoneConnected[1] = (b[12] >> 4) & 0x0F;
      state.zoneConnected[2] = b[13] & 0x0F;
      state.zoneConnected[3] = (b[13] >> 4) & 0x0F;
      state.zoneConnected[4] = b[14] & 0x0F;
      state.zoneState[0] = b[15] & 3;
      state.zoneState[1] = (b[15] >> 2) & 3;
      state.zoneState[2] = (b[15] >> 4) & 3;
      state.zoneState[3] = (b[15] >> 6) & 3;
      state.zoneState[4] = b[16] & 3;
      state.haveStatus = true;
      break;
    }
    case PACKET_TYPE.WORK: {
      state.dayStartHour = b[2]; state.dayStartMinute = b[3];
      state.nightStartHour = b[4]; state.nightStartMinute = b[5];
      state.systemTimeLimit = b[9];
      state.underfloorConnected = !!(b[13] & 1);
      state.engineConnected = !!(b[13] & 2);
      state.floorSetpoint = b[14];
      state.engineSetpoint = b[15];
      state.engineTimeLimit = b[16] * 256 + b[17];
      state.floorHysteresis = b[18];
      state.haveWork = true;
      break;
    }
    case PACKET_TYPE.FIRMWARE: {
      // Wire order (BluetoothHandler.cpp case 8): heater, then the panel's OWN
      // version (_CRCR[6..9] at ADDRESS_CRC), then the MBC-2/HCU's version last -
      // b[9..12] is HCU, NOT the panel, despite what this used to assume (that bug
      // showed up as the restore-update filter matching against "125.x.x.x", the
      // MBC-2's device type, instead of the panel's own "126.x.x.x").
      state.heaterVersion = [b[1], b[2], b[3], b[4]].join('.');
      state.panelVersion = [b[5], b[6], b[7], b[8]].join('.');
      state.hcuVersion = [b[9], b[10], b[11], b[12]].join('.');
      break;
    }
    case PACKET_TYPE.CONN_STATUS: {
      // One-off response to the panel confirmation: carries the shared device key.
      const key = b.slice(12, 20);
      onPaired(key);
      break;
    }
    case PACKET_TYPE.MEMORY: {
      // Response to the external-flash memory server (see writeMemoryRegion() below) —
      // mirrors the device's own reply shape for each sub-command, keyed by b[1].
      const sub = b[1];
      const resp = { sub };
      if (sub === 2) { // query: staged length (16-bit) + running checksum (signed 32-bit)
        resp.count = (b[2] << 8) | b[3];
        resp.crc = (b[4] << 24) | (b[5] << 16) | (b[6] << 8) | b[7];
      } else if (sub === 8) { // read4 result
        resp.status = b[2];
        resp.bytes = [b[3], b[4], b[5], b[6]];
      } else if (sub === 10) { // region checksum result
        resp.status = b[2];
        resp.crc = (b[3] << 24) | (b[4] << 16) | (b[5] << 8) | b[6];
      } else { // 0 (set-address), 4 (commit), 6 (erase) - plain ack
        resp.status = b[2];
      }
      resolveMemWaiters(resp);
      break;
    }
    // Burst-transfer protocol responses (see writeMemoryRegionBurst() below) - share
    // resolveMemWaiters()/waitForMemResponse() with TYPE_MEMORY above: both are used
    // strictly request-then-await, never concurrently, so one waiter queue is enough.
    case PACKET_TYPE.FRAG_INIT:
      resolveMemWaiters({ sub: 14, status: b[1] });
      break;
    case PACKET_TYPE.FRAG_MAP:
      resolveMemWaiters({ sub: 16, bitmap: b.slice(1, 17) });
      break;
    case PACKET_TYPE.FRAG_CRC:
      resolveMemWaiters({ sub: 17, crc: ((b[1] << 24) | (b[2] << 16) | (b[3] << 8) | b[4]) >>> 0 });
      break;
    case PACKET_TYPE.FRAG_PROGRAM:
      resolveMemWaiters({
        sub: 18, status: b[1],
        crc: ((b[2] << 24) | (b[3] << 16) | (b[4] << 8) | b[5]) >>> 0,
      });
      break;
    case PACKET_TYPE.FRAG_DATA_ACK:
      resolveMemWaiters({ sub: 19, status: b[1], index: b[2] });
      break;
    default:
      break;
  }
  render();
  return ptype;
}

/* ── Building outgoing packets (browser -> device) ────────────────────── */
function pkt() { return new Uint8Array(20); }

function buildSetupPacket(overrideFlags) {
  const b = pkt();
  b[0] = PACKET_TYPE.SETUP;
  for (let i = 0; i < ZONE_COUNT; i++) {
    b[1 + i] = state.daySetpoint[i] ?? 20;
    b[6 + i] = state.nightSetpoint[i] ?? 17;
  }
  const flags = {
    heater: state.heaterOn, element: state.elementOn, clearError: false,
    underfloor: state.floorOn, engine: state.engineOn,
    ...overrideFlags,
  };
  b[17] = (flags.heater ? 1 : 0) | (flags.element ? 2 : 0) | (flags.clearError ? 16 : 0)
        | (flags.underfloor ? 32 : 0) | (flags.engine ? 64 : 0);
  return b;
}

function buildStatusPacket(overrideZones) {
  const b = pkt();
  b[0] = PACKET_TYPE.STATUS;
  const fanManual = overrideZones?.fanManual ?? state.fanManual;
  const fanPercent = overrideZones?.fanPercent ?? state.fanPercent;
  const zoneState = overrideZones?.zoneState ?? state.zoneState;
  for (let i = 0; i < ZONE_COUNT; i++) {
    b[1 + i] = (fanPercent[i] & 0x7F) | (fanManual[i] ? 0x80 : 0);
  }
  b[15] = (zoneState[0] & 3) | ((zoneState[1] & 3) << 2) | ((zoneState[2] & 3) << 4) | ((zoneState[3] & 3) << 6);
  b[16] = zoneState[4] & 3;
  return b;
}

function buildWorkPacket(overrides) {
  const b = pkt();
  b[0] = PACKET_TYPE.WORK;
  const s = { ...state, ...overrides };
  b[2] = s.dayStartHour; b[3] = s.dayStartMinute;
  b[4] = s.nightStartHour; b[5] = s.nightStartMinute;
  b[9] = s.systemTimeLimit;
  b[14] = s.floorSetpoint;
  b[15] = s.engineSetpoint;
  b[16] = Math.floor(s.engineTimeLimit / 256);
  b[17] = s.engineTimeLimit % 256;
  b[18] = s.floorHysteresis;
  return b;
}

function buildTimePacket() {
  const b = pkt();
  const now = new Date();
  b[0] = PACKET_TYPE.TIME;
  b[1] = now.getDate();
  b[2] = now.getMonth() + 1;
  b[3] = now.getFullYear() - 2000;
  b[4] = now.getDay() === 0 ? 7 : now.getDay(); // 1=Monday
  b[5] = now.getHours();
  b[6] = now.getMinutes();
  b[7] = now.getSeconds();
  return b;
}

function buildIdPacket(ownId, key) {
  const b = pkt();
  b[0] = PACKET_TYPE.ID;
  b.set(ownId, 1);
  b.set(key, 9);
  return b;
}

/* ── External-flash memory server (stage 1: raw address+length access) ───
   Mirrors the device's CAN PGN107/108 "memory server" (User/Can/messages.cpp
   in PU28-Timberline) byte-for-byte over BLE instead: same sub-command
   values, same non-cryptographic "×170771 rolling" checksum — chosen so the
   firmware and this page agree on what a fragment's checksum even means.
   Reliability follows the same pattern as the firmware's own CAN-relay tool
   (SlotsScreen.cpp): write in MEM_FRAGMENT_SIZE-byte fragments, ask the
   device what it actually staged (length+checksum), and only commit a
   fragment to flash once that matches what we meant to send — otherwise
   resend the whole fragment. writeValueWithoutResponse() can silently drop
   a packet; without this round-trip a dropped byte would corrupt the image
   with no way to notice. */
// Real-hardware testing found a hard, consistent ceiling around 17 data
// packets before a burst starts silently dropping packets (awaiting each
// writeValueWithoutResponse() isn't enough pacing on its own - see
// MEM_PACKET_GAP_MS below), so 512B (~29 packets) meant every fragment sat
// right at that edge. 128B (~8 packets) stays comfortably under it even if
// the gap isn't perfectly tuned, and a retry only costs resending 8 packets
// instead of 29.
const MEM_FRAGMENT_SIZE = 128;
const MEM_DATA_CHUNK = 18;     // usable bytes per 20-byte BLE packet (byte0=type, byte1=count)
// A full image is hundreds of fragments; retries are cheap (resending one
// small fragment, not the whole transfer), but too few retries means the
// odds of some single fragment somewhere hitting a transient failure that
// many times over a multi-minute run add up fast enough to abort an
// otherwise-good transfer.
const MEM_MAX_RETRIES = 10;
// The OS/GATT layer accepting a write into ITS OWN queue (what awaiting
// writeValueWithoutResponse() actually waits for) isn't the same as that
// packet having gone out over the air yet - some stacks (this was on iOS/
// Bluefy, whose Web Bluetooth support is CoreBluetooth under the hood, and
// CoreBluetooth's own write-without-response has no real queueing beyond a
// small internal buffer) silently drop anything past that once it fills.
// 20ms fixed the drops; 10ms reproduced them (worse than before, since more
// packets means more chances to hit the wall); 15ms still wasn't enough
// (aborted at 13% on real hardware, despite the smaller MEM_FRAGMENT_SIZE
// above too) - back to the one value confirmed to actually work.
const MEM_PACKET_GAP_MS = 20;
function sleep(ms) { return new Promise((resolve) => setTimeout(resolve, ms)); }

// Exactly replicates the firmware's `int32_t crc; crc += byte*170771; crc ^=
// (crc>>16)&0xFFFF;` - `|0` truncates to a 32-bit signed int (matching
// int32_t wraparound) and JS's `>>` is arithmetic (sign-extending) just like
// C's on a signed operand, so this produces the identical value bit-for-bit.
function memCrcStep(crc, byte) {
  crc = (crc + byte * 170771) | 0;
  crc = (crc ^ ((crc >> 16) & 0xFFFF)) | 0;
  return crc;
}

function buildMemSetAddress(addr) {
  const b = pkt();
  b[0] = PACKET_TYPE.MEMORY; b[1] = 0;
  b[2] = (addr >>> 24) & 0xFF; b[3] = (addr >>> 16) & 0xFF;
  b[4] = (addr >>> 8) & 0xFF; b[5] = addr & 0xFF;
  return b;
}
function buildMemQuery() { const b = pkt(); b[0] = PACKET_TYPE.MEMORY; b[1] = 2; return b; }
function buildMemCommit() { const b = pkt(); b[0] = PACKET_TYPE.MEMORY; b[1] = 4; return b; }
function buildMemErase(block) { // 0-127 = one 64KB block, 255 = whole chip
  const b = pkt(); b[0] = PACKET_TYPE.MEMORY; b[1] = 6; b[2] = block; return b;
}
function buildMemRead4(addr) {
  const b = pkt(); b[0] = PACKET_TYPE.MEMORY; b[1] = 8;
  b[2] = (addr >>> 24) & 0xFF; b[3] = (addr >>> 16) & 0xFF;
  b[4] = (addr >>> 8) & 0xFF; b[5] = addr & 0xFF;
  return b;
}
function buildMemCrcRegion(addr, len) {
  const b = pkt(); b[0] = PACKET_TYPE.MEMORY; b[1] = 10;
  b[2] = (addr >>> 24) & 0xFF; b[3] = (addr >>> 16) & 0xFF;
  b[4] = (addr >>> 8) & 0xFF; b[5] = addr & 0xFF;
  b[6] = (len >>> 16) & 0xFF; b[7] = (len >>> 8) & 0xFF; b[8] = len & 0xFF;
  return b;
}
function buildMemDataChunk(bytes, offset, count) {
  const b = pkt(); b[0] = PACKET_TYPE.MEMORY_DATA; b[1] = count;
  for (let i = 0; i < count; i++) b[2 + i] = bytes[offset + i];
  return b;
}

// One waiter per in-flight request - the memory sub-protocol is always used
// strictly request-then-await-response (see writeMemoryFragment()/the
// mem*() helpers below), so there's never more than one pending at a time.
let memWaiters = [];
function resolveMemWaiters(resp) {
  const waiters = memWaiters; memWaiters = [];
  for (const w of waiters) { clearTimeout(w.timer); w.resolve(resp); }
}
function waitForMemResponse(timeoutMs = 4000) {
  return new Promise((resolve, reject) => {
    const timer = setTimeout(() => {
      memWaiters = memWaiters.filter((w) => w.timer !== timer);
      reject(new Error('timed out waiting for a response from the device'));
    }, timeoutMs);
    memWaiters.push({ resolve, timer });
  });
}

async function memSetAddress(addr) { await queueWriteStrict(buildMemSetAddress(addr)); return waitForMemResponse(); }
async function memQuery() { await queueWriteStrict(buildMemQuery()); return waitForMemResponse(); }
async function memCommit() { await queueWriteStrict(buildMemCommit()); return waitForMemResponse(); }
async function memErase(block) { await queueWriteStrict(buildMemErase(block)); return waitForMemResponse(15000); } // a chip erase can take a while
async function memRead4(addr) { await queueWriteStrict(buildMemRead4(addr)); return waitForMemResponse(); }
async function memCrcRegion(addr, len) { await queueWriteStrict(buildMemCrcRegion(addr, len)); return waitForMemResponse(8000); }

// Writes one fragment (<= MEM_FRAGMENT_SIZE bytes), retrying the whole
// fragment (not just the missing byte - there's no way to tell which byte
// was dropped) up to MEM_MAX_RETRIES times if the device's reported
// length/checksum don't match what was sent. Each data packet is awaited
// individually (queueWriteStrict, not fire-and-forget) AND followed by
// MEM_PACKET_GAP_MS of real wall-clock delay - surfaces a failed write
// immediately instead of leaving memQuery() to time out for no visible
// reason, and the explicit sleep (not just the await) is what actually
// avoids the GATT-stack packet drops described above.
// onPacket(sent, total), if given, fires after every packet in THIS fragment
// (reset each retry attempt - a fragment that's being retried reports its
// own progress from 0 again, not a running total across attempts).
async function writeMemoryFragment(addr, bytes, onPacket) {
  let lastErr = null;
  for (let attempt = 0; attempt < MEM_MAX_RETRIES; attempt++) {
    try {
      await memSetAddress(addr);
      let localCrc = 0;
      const totalPackets = Math.max(1, Math.ceil(bytes.length / MEM_DATA_CHUNK));
      let sent = 0;
      for (let off = 0; off < bytes.length; off += MEM_DATA_CHUNK) {
        const count = Math.min(MEM_DATA_CHUNK, bytes.length - off);
        await queueWriteStrict(buildMemDataChunk(bytes, off, count));
        await sleep(MEM_PACKET_GAP_MS);
        for (let i = 0; i < count; i++) localCrc = memCrcStep(localCrc, bytes[off + i]);
        sent++;
        if (onPacket) onPacket(sent, totalPackets);
      }
      const resp = await memQuery();
      if (resp.count === bytes.length && resp.crc === localCrc) {
        await memCommit();
        return;
      }
      lastErr = new Error(`checksum mismatch (device: ${resp.count}B/0x${(resp.crc >>> 0).toString(16)}, `
        + `expected: ${bytes.length}B/0x${(localCrc >>> 0).toString(16)})`);
    } catch (e) {
      lastErr = e;
    }
    console.warn(`writeMemoryFragment @0x${addr.toString(16)} attempt ${attempt + 1} failed: ${lastErr.message}`);
  }
  throw new Error(`fragment @0x${addr.toString(16)} failed after ${MEM_MAX_RETRIES} attempts: ${lastErr.message}`);
}

// Writes an arbitrary Uint8Array to the external flash starting at
// startAddr, fragment by fragment. Does NOT erase first - the caller is
// responsible for erasing the target region (memErase()) beforehand, same
// division of responsibility as the CAN protocol this mirrors.
// onProgress(bytesDone, bytesTotal, packetsDone, packetsTotal) fires after
// every single data packet (not just once per fragment) so real transfer
// progress - or a stall - is visible as it happens, not just at fragment
// boundaries every MEM_FRAGMENT_SIZE bytes.
async function writeMemoryRegion(startAddr, data, onProgress) {
  const packetsTotal = Math.max(1, Math.ceil(data.length / MEM_DATA_CHUNK));
  let packetsDoneBefore = 0;
  for (let off = 0; off < data.length; off += MEM_FRAGMENT_SIZE) {
    const chunk = data.subarray(off, Math.min(off + MEM_FRAGMENT_SIZE, data.length));
    await writeMemoryFragment(startAddr + off, chunk, (sentInFragment) => {
      if (onProgress) {
        const bytesDone = off + Math.min(sentInFragment * MEM_DATA_CHUNK, chunk.length);
        onProgress(bytesDone, data.length, packetsDoneBefore + sentInFragment, packetsTotal);
      }
    });
    packetsDoneBefore += Math.ceil(chunk.length / MEM_DATA_CHUNK);
  }
}

/* ── Burst-transfer protocol (stage 2 write path) ─────────────────────────
   Replaces writeMemoryRegion()/writeMemoryFragment() above as the actual
   write path runMemUpload() uses (that pair stays in place, still backing
   the low-level "Upload local file" tool's write and every memErase/
   memRead4/memCrcRegion diagnostic call - nothing there changes).

   Where the old protocol paced every single packet at MEM_PACKET_GAP_MS
   (20ms) and resent an entire fragment on ANY mismatch, this one fires a
   whole fragment's mini-fragments as a fire-and-forget burst - "whatever
   arrives, arrives" - then asks the device for a bitmap of exactly which
   of the (up to 128) 16-byte mini-fragments actually landed, and resends
   only the missing ones, repeating until the map comes back clean. A
   CRC32 of the whole fragment and then a flash-program+readback-verify
   close out each fragment. See bluetooth.h's TYPE_FRAG_* /
   BluetoothHandler.cpp for the device side.

   Real-hardware testing (2026-09-08) found "fire-and-forget" still needs
   two delays, just far smaller than the old protocol's per-packet ack
   round-trip: a gap between individual mini-fragment sends (the same
   GATT-stack queue-depth ceiling that made the old protocol need
   MEM_PACKET_GAP_MS applies here too, just less severely since nothing is
   awaiting a per-packet reply), and a settle delay after a whole burst
   before asking for the map - the device needs some time to finish
   processing what already arrived before a map request reflects it.
   Both are user-tunable (fragGapMs/fragSettleMs in index.html, read by
   getFragTiming() below) rather than fixed constants - what real hardware
   actually needs turned out to need live experimentation, not a single
   value baked into the code. */
const FRAG_SIZE = 2048;   // matches PU28-Timberline's fragBuf[2048] staging buffer
const FRAG_MINI_SIZE = 16; // matches TYPE_FRAG_DATA's 20-byte packet: type(1)+index(1)+data(16)+CRC16(2)
const FRAG_GAP_MS_DEFAULT = 2;
const FRAG_SETTLE_MS_DEFAULT = 50;
const FRAG_MAP_ROUNDS = 25;   // burst + up to this many "resend only what's missing" rounds
const FRAG_OUTER_RETRIES = 10; // if a fragment doesn't converge (or fails CRC32/program) even after
                               // FRAG_MAP_ROUNDS, restart that fragment from TYPE_FRAG_INIT this many times.
                               // Real-hardware logs (2026-09-08) showed occasional multi-second dropouts
                               // that outlast FRAG_CTRL_RETRIES - bumped from 5 to give those more room
                               // before failing the whole transfer.

// Real-hardware testing (2026-09-08) found the burst+map round trip pays a
// fixed cost (settleMs + a map request/response) no matter how few packets
// are actually missing - and in practice a round almost always converges to
// "only a handful left" within 2-3 rounds regardless of gap/settle tuning.
// Once this few remain, it's cheaper to just ack each one individually
// (TYPE_FRAG_DATA_ACK) than to pay for another whole burst+settle+map round.
const FRAG_TAIL_THRESHOLD = 8;
const FRAG_TAIL_RETRIES = 4; // per straggler, before giving up on it (falls back to another map round)

// Reads the two timing inputs (index.html's "Burst transfer timing" panel),
// falling back to the defaults above if the field is missing/empty/invalid.
// Read once per whole-region transfer (writeMemoryRegionBurst), not re-read
// per packet/round - a mid-transfer edit takes effect on the next transfer.
function getFragTiming() {
  const gapEl = document.getElementById('fragGapMs');
  const settleEl = document.getElementById('fragSettleMs');
  const gap = gapEl ? parseInt(gapEl.value, 10) : NaN;
  const settle = settleEl ? parseInt(settleEl.value, 10) : NaN;
  return {
    gapMs: Number.isFinite(gap) && gap >= 0 ? gap : FRAG_GAP_MS_DEFAULT,
    settleMs: Number.isFinite(settle) && settle >= 0 ? settle : FRAG_SETTLE_MS_DEFAULT,
  };
}

// Persists the two timing inputs across reloads and pre-fills them on page
// load - called once from wireMemoryControls().
function initFragTimingInputs() {
  const gapEl = document.getElementById('fragGapMs');
  const settleEl = document.getElementById('fragSettleMs');
  if (!gapEl || !settleEl) return;
  const savedGap = parseInt(localStorage.getItem('pu28ble_frag_gap_ms'), 10);
  const savedSettle = parseInt(localStorage.getItem('pu28ble_frag_settle_ms'), 10);
  gapEl.value = Number.isFinite(savedGap) ? savedGap : FRAG_GAP_MS_DEFAULT;
  settleEl.value = Number.isFinite(savedSettle) ? savedSettle : FRAG_SETTLE_MS_DEFAULT;
  gapEl.onchange = () => localStorage.setItem('pu28ble_frag_gap_ms', gapEl.value);
  settleEl.onchange = () => localStorage.setItem('pu28ble_frag_settle_ms', settleEl.value);
}

// Thrown by checkCancelled() when the user hits a Cancel button mid-transfer
// - a distinct type so callers can tell "user cancelled" apart from a real
// transport/verification failure (no retry, no scary error text).
class TransferCancelled extends Error {
  constructor() { super('Cancelled by user'); this.name = 'TransferCancelled'; }
}
function checkCancelled(token) {
  if (token && token.cancelled) throw new TransferCancelled();
}

// Exactly replicates the firmware's crc32Update()/crc32Of() (CRC-32/ISO-HDLC,
// poly 0xEDB88320, init/final 0xFFFFFFFF - the same algorithm SlotsScreen.cpp's
// CAN-relay and the bootloader already use, just applied here to a whole
// BLE-staged fragment instead).
function crc32Update(crc, byte) {
  crc ^= byte;
  for (let bit = 0; bit < 8; bit++) crc = (crc & 1) ? ((crc >>> 1) ^ 0xEDB88320) : (crc >>> 1);
  return crc >>> 0;
}
function crc32Of(bytes) {
  let crc = 0xFFFFFFFF;
  for (let i = 0; i < bytes.length; i++) crc = crc32Update(crc, bytes[i]);
  return (crc ^ 0xFFFFFFFF) >>> 0;
}

function buildFragInit(addr, len, crc32) {
  const b = pkt();
  b[0] = PACKET_TYPE.FRAG_INIT;
  b[1] = (addr >>> 24) & 0xFF; b[2] = (addr >>> 16) & 0xFF;
  b[3] = (addr >>> 8) & 0xFF; b[4] = addr & 0xFF;
  b[5] = (len >>> 8) & 0xFF; b[6] = len & 0xFF;
  b[7] = (crc32 >>> 24) & 0xFF; b[8] = (crc32 >>> 16) & 0xFF;
  b[9] = (crc32 >>> 8) & 0xFF; b[10] = crc32 & 0xFF;
  return b;
}
// type is PACKET_TYPE.FRAG_DATA (fire-and-forget) or FRAG_DATA_ACK (device
// always replies) - identical 20-byte payload either way, see bluetooth.h.
//
// 2026-09-08 hardening: the CRC16 used to cover only the data bytes, not the
// index byte (b[1]) that decides where the firmware writes them - and this
// packet type is exempt from the protocol-wide CRC-8 (CRC8_EXEMPT_TYPES,
// below) precisely because byte 19 already holds this CRC16. On the bit-
// banged SPI transport's own admitted lack of framing/resync, a single-byte
// corruption landing on an index the firmware had ALREADY received would
// silently overwrite good data with garbage while leaving that index's
// bitmap bit set - invisible to the TYPE_FRAG_MAP round trip, only caught
// (if at all) by the whole-fragment CRC32 at TYPE_FRAG_PROGRAM, forcing a
// full 2048-byte fragment resend. Longer firmware images mean more mini-
// fragments and proportionally higher odds of hitting exactly this case -
// matches the observed correlation between long files and aborted
// transfers. Fixed by folding the index into the same CRC16 - see
// BluetoothHandler.cpp's storeFragMini() for the matching firmware change.
function buildFragData(type, index, bytes, off, validCount) {
  const b = pkt();
  b[0] = type;
  b[1] = index;
  for (let i = 0; i < validCount; i++) b[2 + i] = bytes[off + i];
  const crc = crc16Modbus(b.subarray(1, 2 + validCount)); // index (b[1]) + data - same CRC16/ARC as the firmware's crc16Of()
  b[18] = (crc >>> 8) & 0xFF;
  b[19] = crc & 0xFF;
  return b;
}
function buildFragMapRequest() { const b = pkt(); b[0] = PACKET_TYPE.FRAG_MAP; return b; }
function buildFragCrcRequest() { const b = pkt(); b[0] = PACKET_TYPE.FRAG_CRC; return b; }
function buildFragProgramRequest() { const b = pkt(); b[0] = PACKET_TYPE.FRAG_PROGRAM; return b; }

// fragInit/fragMapRequest/fragCrcRequest/fragProgramRequest are each a single
// request-packet + single response-packet round trip, unlike the mini-fragment
// burst which is designed from the ground up to tolerate loss. Losing just
// ONE of these (the request write itself, or the one reply notification) used
// to blow away everything a fragment's burst+tail-fill had already recovered,
// forcing a full outer retry - re-sending all 128 mini-fragments from
// scratch (real-hardware logs 2026-09-08 showed exactly this: a lone lost map
// request mid-fragment, and the whole fragment restarted from TYPE_FRAG_INIT).
// withCtrlRetry() retries just the one request/response instead.
const FRAG_CTRL_RETRIES = 5;
async function withCtrlRetry(fn, label, token) {
  let lastErr;
  for (let i = 0; i < FRAG_CTRL_RETRIES; i++) {
    checkCancelled(token);
    try {
      return await fn();
    } catch (e) {
      if (e instanceof TransferCancelled) throw e;
      lastErr = e;
      console.warn(`${label} attempt ${i + 1}/${FRAG_CTRL_RETRIES} failed: ${e.message}`);
    }
  }
  throw lastErr;
}

async function fragInit(addr, len, crc32, token) {
  return withCtrlRetry(async () => {
    await queueWriteStrict(buildFragInit(addr, len, crc32));
    return waitForMemResponse();
  }, 'fragInit', token);
}
async function fragMapRequest(token) {
  return withCtrlRetry(async () => {
    await queueWriteStrict(buildFragMapRequest());
    return waitForMemResponse();
  }, 'fragMapRequest', token);
}
async function fragCrcRequest(token) {
  return withCtrlRetry(async () => {
    await queueWriteStrict(buildFragCrcRequest());
    return waitForMemResponse(8000);
  }, 'fragCrcRequest', token);
}
async function fragProgramRequest(token) {
  return withCtrlRetry(async () => {
    await queueWriteStrict(buildFragProgramRequest());
    return waitForMemResponse(8000);
  }, 'fragProgramRequest', token);
}

// Sends one mini-fragment and waits for its individual ack (TYPE_FRAG_DATA_ACK) -
// used by fragTailFill() to mop up the last few stragglers one at a time
// instead of paying for a whole burst+settle+map round to recover 1-3 packets.
async function fragDataAckSend(idx, bytes) {
  const off = idx * FRAG_MINI_SIZE;
  const validCount = Math.min(FRAG_MINI_SIZE, bytes.length - off);
  await queueWriteStrict(buildFragData(PACKET_TYPE.FRAG_DATA_ACK, idx, bytes, off, validCount));
  return waitForMemResponse();
}

// Which mini-fragment indexes (0..miniCount-1) the device's bitmap says it
// does NOT have a CRC16-valid copy of yet.
function fragBitmapMissing(bitmap, miniCount) {
  const missing = [];
  for (let i = 0; i < miniCount; i++) {
    if (!(bitmap[i >> 3] & (1 << (i & 7)))) missing.push(i);
  }
  return missing;
}

// Fire-and-forget burst - not awaited/acked per packet (that's the whole
// point of this protocol: let some drop, find out via the map, resend only
// those), but paced gapMs apart so the phone's GATT stack doesn't just drop
// everything past its outbound queue depth (see the file-header comment
// above). queueWrite() (not queueWriteStrict()) is still correct here: an
// individual write failing is just another way a mini-fragment can end up
// "missing", already handled by the map round that follows. Checks the
// cancel token between packets so hitting Cancel mid-burst stops promptly
// instead of finishing the whole burst first.
async function fragBurstSend(bytes, indexes, gapMs, token) {
  for (const idx of indexes) {
    checkCancelled(token);
    const off = idx * FRAG_MINI_SIZE;
    const validCount = Math.min(FRAG_MINI_SIZE, bytes.length - off);
    queueWrite(buildFragData(PACKET_TYPE.FRAG_DATA, idx, bytes, off, validCount));
    await sleep(gapMs);
  }
}

// Resolves a small number of stragglers one at a time via TYPE_FRAG_DATA_ACK
// instead of another burst+settle+map round - each packet gets its own
// immediate round-trip confirmation, no waiting for a settle delay or a
// separate map request to find out whether it landed. Retries a given
// straggler up to FRAG_TAIL_RETRIES times (a single ack write/response can
// still fail like any other BLE round-trip); returns whatever indexes are
// still unresolved after that (normally empty - the caller falls back to
// another ordinary map round for anything left, rather than treating that
// as fatal).
async function fragTailFill(bytes, indexes, logElId, token) {
  const stillMissing = [];
  for (const idx of indexes) {
    checkCancelled(token);
    let ok = false;
    for (let attempt = 0; attempt < FRAG_TAIL_RETRIES && !ok; attempt++) {
      try {
        const resp = await fragDataAckSend(idx, bytes);
        if (resp.status === 0) ok = true;
      } catch (e) {
        console.warn(`fragTailFill: ack for mini-fragment ${idx} failed (attempt ${attempt + 1}): ${e.message}`);
      }
    }
    if (!ok) stillMissing.push(idx);
  }
  return stillMissing;
}

// Round-level summary for a burst transfer - a dedicated small panel per
// upload flow (restoreFragLog / fwFragLog / memFragLog in index.html),
// deliberately NOT the shared "Packet log" panel: that one interleaves
// every packet type from all BLE activity, which made the round-by-round
// numbers impossible to pick out during an actual transfer. Prints
// "Frag_N" once per fragment/retry, then one "done/total" line per map
// round, e.g.:
//   Frag_1/3
//   178/256
//   223/256
//   256/256
//   Frag_2/3
//   ...
function logFrag(line, logElId) {
  const el = document.getElementById(logElId || 'memFragLog');
  if (!el) return;
  el.textContent += (el.textContent ? '\n' : '') + line;
  el.scrollTop = el.scrollHeight;
}

// Copies one frag-log panel's text to the clipboard, prefixed with the
// timing settings that produced it (gap/settle only make sense alongside
// the rounds they shaped) - lets the user hand a failed transfer's log
// over for diagnosis without having to retype or screenshot it.
function copyFragLog(logElId, btn) {
  const el = document.getElementById(logElId);
  if (!el) return;
  const timing = getFragTiming();
  const header = `[gap=${timing.gapMs}ms settle=${timing.settleMs}ms ${new Date().toLocaleString()}]`;
  const text = `${header}\n${el.textContent || '(empty)'}`;
  const showCopied = () => {
    const orig = btn.textContent;
    btn.textContent = 'Copied!';
    setTimeout(() => { btn.textContent = orig; }, 1500);
  };
  const fallbackCopy = () => {
    const ta = document.createElement('textarea');
    ta.value = text;
    ta.style.position = 'fixed';
    ta.style.opacity = '0';
    document.body.appendChild(ta);
    ta.select();
    try { document.execCommand('copy'); showCopied(); } catch (e) { alert('Copy failed: ' + e.message); }
    document.body.removeChild(ta);
  };
  if (navigator.clipboard && navigator.clipboard.writeText) {
    navigator.clipboard.writeText(text).then(showCopied).catch(fallbackCopy);
  } else {
    fallbackCopy();
  }
}

function wireFragLogCopyButtons() {
  [['restoreFragLogCopyBtn', 'restoreFragLog'], ['fwFragLogCopyBtn', 'fwFragLog'], ['memFragLogCopyBtn', 'memFragLog']]
    .forEach(([btnId, logElId]) => {
      const btn = document.getElementById(btnId);
      if (btn) btn.onclick = () => copyFragLog(logElId, btn);
    });
}

// onRound(miniDone, miniTotal) fires after each map-check round (including
// the initial one right after the burst), so progress is visible converging
// even though - unlike the old protocol - individual packets aren't tracked.
// `token` (a {cancelled: bool} object, see checkCancelled()) is checked at
// every await boundary - a TransferCancelled thrown here is NOT retried,
// unlike every other failure, and propagates straight out to the caller.
async function writeFragmentBurst(addr, bytes, fragLabel, onRound, logElId, timing, token) {
  const crc32 = crc32Of(bytes);
  const miniCount = Math.max(1, Math.ceil(bytes.length / FRAG_MINI_SIZE));
  let lastErr = null;

  for (let attempt = 0; attempt < FRAG_OUTER_RETRIES; attempt++) {
    checkCancelled(token);
    try {
      logFrag(attempt === 0 ? fragLabel : `${fragLabel} (retry ${attempt + 1})`, logElId);
      await fragInit(addr, bytes.length, crc32, token);

      const allIndexes = Array.from({ length: miniCount }, (_, i) => i);
      await fragBurstSend(bytes, allIndexes, timing.gapMs, token);
      await sleep(timing.settleMs);
      checkCancelled(token);

      let missing = allIndexes;
      for (let round = 0; round < FRAG_MAP_ROUNDS; round++) {
        checkCancelled(token);
        const mapResp = await fragMapRequest(token);
        missing = fragBitmapMissing(mapResp.bitmap, miniCount);
        const doneCount = miniCount - missing.length;
        logFrag(`${doneCount}/${miniCount}`, logElId);
        if (onRound) onRound(doneCount, miniCount);
        if (missing.length === 0) break;

        if (missing.length <= FRAG_TAIL_THRESHOLD) {
          // Cheaper to ack these few individually than pay for another
          // whole burst+settle+map round trip - see fragTailFill().
          missing = await fragTailFill(bytes, missing, logElId, token);
          const tailDone = miniCount - missing.length;
          logFrag(`${tailDone}/${miniCount} (tail-fill)`, logElId);
          if (onRound) onRound(tailDone, miniCount);
          if (missing.length === 0) break;
          continue; // whatever tail-fill couldn't resolve gets a normal map round next
        }

        await fragBurstSend(bytes, missing, timing.gapMs, token);
        await sleep(timing.settleMs);
      }
      if (missing.length > 0) {
        throw new Error(`${missing.length}/${miniCount} mini-fragments still missing after ${FRAG_MAP_ROUNDS} rounds`);
      }

      const crcResp = await fragCrcRequest(token);
      if (crcResp.crc !== crc32) {
        throw new Error(`fragment CRC32 mismatch (device 0x${crcResp.crc.toString(16)}, expected 0x${crc32.toString(16)})`);
      }

      const progResp = await fragProgramRequest(token);
      if (progResp.status !== 0) {
        throw new Error(`flash programming/verify failed (readback CRC32 0x${progResp.crc.toString(16)})`);
      }
      return; // success
    } catch (e) {
      if (e instanceof TransferCancelled) throw e;
      lastErr = e;
      console.warn(`writeFragmentBurst @0x${addr.toString(16)} attempt ${attempt + 1} failed: ${e.message}`);
    }
  }
  throw new Error(`fragment @0x${addr.toString(16)} failed after ${FRAG_OUTER_RETRIES} attempts: ${lastErr.message}`);
}

// Drop-in replacement for writeMemoryRegion() with the same onProgress(bytesDone,
// bytesTotal, packetsDone, packetsTotal) shape (here "packets" = mini-fragments
// confirmed received, so existing progress-bar rendering needs no changes).
async function writeMemoryRegionBurst(startAddr, data, onProgress, logElId, token) {
  const timing = getFragTiming();
  const miniTotal = Math.max(1, Math.ceil(data.length / FRAG_MINI_SIZE));
  const fragTotal = Math.max(1, Math.ceil(data.length / FRAG_SIZE));
  let miniDoneBefore = 0;
  let fragIndex = 0;
  for (let off = 0; off < data.length; off += FRAG_SIZE) {
    fragIndex++;
    const chunk = data.subarray(off, Math.min(off + FRAG_SIZE, data.length));
    await writeFragmentBurst(startAddr + off, chunk, `Frag_${fragIndex}/${fragTotal}`, (doneInFrag, totalInFrag) => {
      if (onProgress) {
        const bytesDone = off + Math.min(doneInFrag * FRAG_MINI_SIZE, chunk.length);
        onProgress(bytesDone, data.length, miniDoneBefore + doneInFrag, miniTotal);
      }
    }, logElId, timing, token);
    miniDoneBefore += Math.ceil(chunk.length / FRAG_MINI_SIZE);
  }
}

/* ── Screen wake lock ──────────────────────────────────────────────────────
   A multi-minute BLE transfer stops dead the moment the phone's screen locks
   - iOS/Android both suspend background JS enough to stall mid-transfer BLE
   writes. It resumes on its own once the screen wakes again (per the user's
   own report), so nothing is actually broken - there's just no reason to
   make someone babysit the screen for several minutes when a standard API
   exists for exactly this. Held only for the duration of an actual transfer
   (acquireWakeLock()/releaseWakeLock() bracket each upload flow below), not
   the whole BLE session, so normal screen-timeout behavior is untouched the
   rest of the time. Supported in current Chrome/Edge/Android and Safari
   16.4+ (so also Bluefy, a WKWebView wrapper, on any reasonably current
   iOS) - unsupported browsers just silently proceed without it, same as
   before this existed. */
let wakeLock = null;
let wakeLockWanted = false; // re-requested on visibilitychange if a transfer is still running

async function acquireWakeLock() {
  wakeLockWanted = true;
  if (!('wakeLock' in navigator) || wakeLock) return;
  try {
    wakeLock = await navigator.wakeLock.request('screen');
    wakeLock.addEventListener('release', () => { wakeLock = null; });
  } catch (e) {
    console.warn('[wakeLock] request failed (transfer continues without it):', e.message);
  }
}

function releaseWakeLock() {
  wakeLockWanted = false;
  if (wakeLock) { wakeLock.release(); wakeLock = null; }
}

// The wake lock is auto-released by the browser whenever the page goes into
// the background (tab-switch, app-switch) - re-request it the moment it's
// foregrounded again, but only if a transfer is still actually in progress.
document.addEventListener('visibilitychange', () => {
  if (document.visibilityState === 'visible' && wakeLockWanted) acquireWakeLock();
});

/* ── BLE connection ────────────────────────────────────────────────────── */
let ble = { device: null, server: null, txChar: null, rxChar: null };
let pairing = false;
let pairingTimer = null;
let writeChain = Promise.resolve();

// Protocol-wide packet integrity (2026-09-08, replaces the earlier
// "just pace it more gently" band-aid - see BluetoothHandler.cpp's matching
// comment for the full incident writeup). The panel's BLE transport
// (Bluetooth::handler() in bluetooth.cpp - bit-banged SPI to the module,
// no framing/resync) had NO integrity check on any inbound command packet
// except this app's own burst sub-protocol - a corrupted byte landing in
// buf[0] as a low value (1/2/3...) got dispatched as TYPE_SETUP/TYPE_WORK/etc
// with zero validation, causing real hardware to report spurious zone/
// element/heater activation under heavy BLE traffic. Device isn't in
// production yet, so this is a real protocol fix, not a workaround: every
// outbound packet except the three that already use byte 19 for their own
// data (MEMORY_DATA's rolling checksum, FRAG_DATA/FRAG_DATA_ACK's CRC16)
// now carries a CRC-8 (poly 0x07, init 0x00) of bytes[0..18] in byte 19.
// The firmware drops anything that fails this check before it ever reaches
// the command dispatcher - see the matching check in BluetoothHandler.cpp.
function crc8Of(bytes, len) {
  let crc = 0;
  for (let i = 0; i < len; i++) {
    crc ^= bytes[i];
    for (let b = 0; b < 8; b++) crc = (crc & 0x80) ? ((crc << 1) ^ 0x07) & 0xFF : (crc << 1) & 0xFF;
  }
  return crc;
}
const CRC8_EXEMPT_TYPES = new Set([PACKET_TYPE.MEMORY_DATA, PACKET_TYPE.FRAG_DATA, PACKET_TYPE.FRAG_DATA_ACK]);

async function doWrite(bytes) {
  if (!ble.rxChar) throw new Error('not connected');
  if (!CRC8_EXEMPT_TYPES.has(bytes[0])) bytes[19] = crc8Of(bytes, 19);
  if (ble.rxChar.writeValueWithoutResponse) await ble.rxChar.writeValueWithoutResponse(bytes);
  else await ble.rxChar.writeValue(bytes);
}

// Fire-and-forget - swallows write failures (logged, not thrown). Every existing
// caller in this file (telemetry/setpoint/time-sync packets etc.) assumes
// queueWrite() never rejects, so this keeps that contract.
function queueWrite(bytes) {
  writeChain = writeChain.then(() => doWrite(bytes)).catch((e) => console.error('BLE write error', e));
  return writeChain;
}

// Same ordering/queue as queueWrite() (both thread through the one shared
// writeChain, so relative order is preserved regardless of which of the two
// a given packet uses), but the returned promise actually rejects on failure.
// Used by the memory-protocol code below (writeMemoryFragment et al.): a
// silently swallowed write there used to leave callers waiting out a full
// response timeout for a device reply that a failed write could never
// produce - this makes that failure visible immediately instead.
function queueWriteStrict(bytes) {
  const result = writeChain.then(() => doWrite(bytes));
  writeChain = result.catch(() => {});
  return result;
}

function onNotify(event) {
  const b = new Uint8Array(event.target.value.buffer);
  const ptype = parsePacket(b);
  if (pairing && ptype !== undefined && ptype !== 0xFF) {
    // Any meaningful packet while waiting for confirmation means the device
    // has accepted us (either instantly via a known key, or telemetry is
    // already flowing) — CONN_STATUS(9) is handled separately in onPaired()
    // and also clears pairing through the same flag.
    if (ptype !== PACKET_TYPE.CONN_STATUS) stopPairingIfRunning('Connected');
  }
}

function onPaired(keyBytes) {
  if (ble.device) setStoredKey(ble.device.id, keyBytes);
  stopPairingIfRunning('Connected (approved on panel)');
}

function startPairing() {
  pairing = true;
  const ownId = getOwnId();
  const key = ble.device ? getStoredKey(ble.device.id) : new Uint8Array(8);
  setConnLabel('Waiting for confirmation on the device panel…', 'connecting');
  const send = () => queueWrite(buildIdPacket(ownId, key));
  send();
  pairingTimer = setInterval(send, 3000);
}
function stopPairingIfRunning(label) {
  if (!pairing) { if (label) setConnLabel(label, 'connected'); return; }
  pairing = false;
  if (pairingTimer) { clearInterval(pairingTimer); pairingTimer = null; }
  setConnLabel(label || 'Connected', 'connected');
  document.getElementById('controlBox').classList.remove('hidden');
  queueWrite(buildTimePacket()); // one-off time sync right after the link is established
}

// Remembers the physical device (by its origin-stable BluetoothDevice.id,
// not its advertised name — see tryAutoReconnect() below) that a connection
// last actually succeeded against, so future page loads can reconnect to
// exactly that unit without the user picking from a list again. This is
// what makes the confusing device-name situation (the panel shows up
// alongside other Autoterm/Timberline hardware, e.g. an MBC-2 unit
// advertising as "Timberline 2", and Bluefy's unfiltered list sometimes
// shows raw identifiers instead of names) only ever matter once.
const LAST_DEVICE_KEY = 'pu28ble_last_device_id';

async function connectToDevice(device) {
  try {
    setConnLabel('Connecting…', 'connecting');
    ble.device = device;
    device.addEventListener('gattserverdisconnected', onDisconnected);
    const server = await device.gatt.connect();

    let service;
    try {
      service = await server.getPrimaryService(SERVICE_UUID);
    } catch (e) {
      // Most likely picked the wrong entry from Bluefy's unfiltered "Show
      // all devices" list (its native picker shows opaque identifiers
      // instead of names there, so this is easy to get wrong) — disconnect
      // cleanly and say so, instead of leaving a half-connected device that
      // will never answer our packets and reporting a generic error.
      device.gatt.disconnect();
      document.getElementById('err').textContent =
        "That device doesn't have the Autoterm PU service — probably the wrong entry. Try \"Show all devices\" again and pick a different one.";
      setConnLabel('Not connected', 'disconnected');
      return;
    }

    const txChar = await service.getCharacteristic(CHAR_TX_UUID);
    const rxChar = await service.getCharacteristic(CHAR_RX_UUID);
    await txChar.startNotifications();
    txChar.addEventListener('characteristicvaluechanged', onNotify);
    ble.server = server; ble.txChar = txChar; ble.rxChar = rxChar;

    localStorage.setItem(LAST_DEVICE_KEY, device.id);

    document.getElementById('connectBtn').classList.add('hidden');
    document.getElementById('disconnectBtn').classList.remove('hidden');
    startPairing();
  } catch (e) {
    console.error(e);
    document.getElementById('err').textContent = 'Connection error: ' + describeErrorWithHint(e);
    setConnLabel('Not connected', 'disconnected');
  }
}

/* Auto-connect on page load: if this browser has previously been granted
   permission for a device (Chrome and, per Bluefy's own changelog, Bluefy
   itself both implement getDevices()) and we recognize its id from a past
   successful connection, reconnect straight away — no "Connect via
   Bluetooth" tap, no device list, no re-encountering whatever name Bluefy
   happened to show it under this time. Deliberately NOT silent on failure
   (an earlier version was, to avoid greeting the user with an unrequested
   error banner) — on a page whose whole point is reconnecting to one
   specific device, "nothing happened and there's no way to tell why" was
   worse than a plain status line: was there no stored device, did
   getDevices() come back empty, did the connect itself fail (e.g. panel
   currently out of range)? Each is now visibly distinct instead of all
   looking identical from the outside. */
async function tryAutoReconnect() {
  if (!navigator.bluetooth?.getDevices) {
    console.log('[BLE] auto-reconnect: getDevices() not supported by this browser');
    return;
  }
  const lastId = localStorage.getItem(LAST_DEVICE_KEY);
  if (!lastId) {
    console.log('[BLE] auto-reconnect: no remembered device yet');
    return;
  }
  try {
    const devices = await navigator.bluetooth.getDevices();
    console.log(`[BLE] auto-reconnect: getDevices() returned ${devices.length} device(s), looking for ${lastId}`);
    const match = devices.find((d) => d.id === lastId);
    if (!match) {
      document.getElementById('err').textContent =
        "Auto-reconnect: the remembered device isn't in this browser's permitted-devices list anymore (permissions may have been reset) — use \"Show all devices\" to pick it again.";
      return;
    }
    await connectToDevice(match);
  } catch (e) {
    console.error('[BLE] auto-reconnect failed', e);
    document.getElementById('err').textContent = 'Auto-reconnect error: ' + describeErrorWithHint(e);
  }
}

function onDisconnected() {
  if (pairingTimer) { clearInterval(pairingTimer); pairingTimer = null; }
  pairing = false;
  setConnLabel('Connection lost', 'disconnected');
  document.getElementById('connectBtn').classList.remove('hidden');
  document.getElementById('disconnectBtn').classList.add('hidden');
  document.getElementById('controlBox').classList.add('hidden');
  renderKnownDevices();
}

async function doConnectPicker() {
  document.getElementById('err').textContent = '';
  try {
    // "Timberline" — the assumed name from User/Main/sample_service.c (that
    // file isn't part of the PU-28.uvprojx build, it's just documentation
    // for a separate BLE module's GATT service). On real hardware the
    // device advertises as "Autoterm PU" — the same product under its
    // retail brand. Filters are OR'd, so we keep both name variants plus
    // the GATT service itself as the most reliable signal, name-independent.
    const device = await navigator.bluetooth.requestDevice({
      filters: [
        { services: [SERVICE_UUID] },
        { namePrefix: 'Autoterm' },
        { namePrefix: 'Timberline' },
      ],
      optionalServices: [SERVICE_UUID],
    });
    await connectToDevice(device);
  } catch (e) {
    handleRequestDeviceError(e);
  }
}

/* Bluefy (the main third-party Web Bluetooth browser on iOS — stock Safari
   has no Web Bluetooth support at all) has documented bugs matching
   requestDevice() filters (W3C web-bluetooth issue #624: the same filter
   that works in Chrome finds nothing in Bluefy). Rather than guess which
   clause it mishandles, "Show all devices" below sidesteps filtering
   entirely with acceptAllDevices — every advertising BLE device shows up
   in the native picker, the user just has to recognize "Autoterm PU" (or
   "Timberline...") in the list themselves. */
async function doConnectPickerUnfiltered() {
  document.getElementById('err').textContent = '';
  try {
    const device = await navigator.bluetooth.requestDevice({
      acceptAllDevices: true,
      optionalServices: [SERVICE_UUID],
    });
    await connectToDevice(device);
  } catch (e) {
    handleRequestDeviceError(e);
  }
}

function handleRequestDeviceError(e) {
  // Some non-Chrome Web Bluetooth implementations (third-party iOS
  // browsers — stock Safari has no Web Bluetooth support at all) reject
  // with something that isn't a standard DOMException: a plain object, a
  // bare string, even undefined. `e?.name`/describeError() below handle
  // all of those instead of assuming `.name`/`.message` exist, which
  // previously showed the literal text "Error: undefined" whenever they
  // didn't.
  if (e?.name !== 'NotFoundError') {
    document.getElementById('err').textContent = 'Error: ' + describeErrorWithHint(e);
  }
}

function describeError(e) {
  if (e === undefined || e === null) return 'unknown error';
  if (typeof e === 'string') return e;
  if (e.message) return e.message;
  if (e.name) return e.name;
  try { return JSON.stringify(e); } catch (_) { return String(e); }
}

/* Bluefy (and presumably other CoreBluetooth-backed iOS Web Bluetooth
   bridges) sometimes reject with a bare CBError numeric code instead of a
   proper DOMException — describeError() above then shows just that digit,
   which is useless on its own. Known codes get a plain-language hint
   appended; unrecognized text passes through unchanged. */
const KNOWN_ERROR_HINTS = {
  // CBError.Code.invalidHandle (Apple's CoreBluetooth) — a stale GATT
  // handle cache on this phone, typically after the peripheral (or the
  // browser app) restarted since the last successful connection.
  '2': 'this looks like a stale Bluetooth cache on this phone — try iOS Settings → Bluetooth → tap the ⓘ next to the device → "Forget This Device", then reconnect. Toggling Bluetooth off/on or force-quitting the browser app can also clear it.',
};
function describeErrorWithHint(e) {
  const text = describeError(e);
  const hint = KNOWN_ERROR_HINTS[text];
  return hint ? `${text} (${hint})` : text;
}

function doDisconnect() {
  if (ble.device?.gatt?.connected) ble.device.gatt.disconnect();
}

/* Previously permitted devices (Chrome: navigator.bluetooth.getDevices) —
   lets the user reconnect in one tap without the system chooser dialog,
   and together with the stored pairing key, without confirming on the
   device panel at all. */
async function renderKnownDevices() {
  const box = document.getElementById('knownDevices');
  box.innerHTML = '';
  if (!navigator.bluetooth?.getDevices) return;
  try {
    const devices = await navigator.bluetooth.getDevices();
    // Web Bluetooth grants (persistent) permission for whatever gets
    // selected in the native picker the instant it's selected — before our
    // own code ever runs, so it applies even to a device that turns out to
    // have the wrong GATT service and gets disconnected immediately (see
    // the "wrong entry" branch in connectToDevice()). Every mis-click while
    // hunting for the right entry in an unfiltered "Show all devices" list
    // (Bluefy's picker often shows raw identifiers, not names — the "many
    // 'Connect: <number>' chips" this is answering) leaves its own
    // permanent entry here. Once we have a remembered last-good device
    // (tryAutoReconnect() already handles reconnecting to it on load),
    // there's no reason to keep dumping that whole accumulated list on
    // screen — just offer to clear it out in one tap.
    const lastId = localStorage.getItem(LAST_DEVICE_KEY);
    if (lastId && devices.some((d) => d.id === lastId)) {
      const others = devices.filter((d) => d.id !== lastId);
      if (others.length > 0) {
        const cleanup = document.createElement('button');
        cleanup.className = 'small';
        cleanup.textContent = `Forget ${others.length} other paired device(s)`;
        const note = document.createElement('p');
        note.className = 'hint';
        cleanup.onclick = async () => {
          cleanup.disabled = true;
          cleanup.textContent = 'Forgetting…';
          note.textContent = '';
          // BluetoothDevice.forget() is a newer, less commonly implemented
          // part of the persistent-permissions spec than getDevices() itself
          // — some browsers can list previously-granted devices but not
          // actually revoke them. Verify by re-reading the list afterwards
          // instead of assuming forget() succeeded just because it didn't
          // throw (some stub implementations resolve without doing anything).
          let noForgetSupport = false;
          for (const d of others) {
            if (!d.forget) { noForgetSupport = true; continue; }
            try { await d.forget(); } catch (e) { /* checked below via re-read */ }
          }
          let stillThere = others.length;
          try {
            const after = await navigator.bluetooth.getDevices();
            stillThere = after.filter((d) => d.id !== lastId).length;
          } catch (e) { /* keep the pre-cleanup count as a conservative fallback */ }

          if (stillThere === 0) {
            renderKnownDevices();
            return;
          }
          cleanup.disabled = false;
          cleanup.textContent = `Forget ${stillThere} other paired device(s)`;
          note.textContent = noForgetSupport
            ? "This browser can list paired devices but can't remove them from a page — clear them from the browser app's own settings instead."
            : "Didn't take effect — try again, or clear permissions from the browser app's own settings.";
        };
        box.appendChild(cleanup);
        box.appendChild(note);
      }
      return;
    }

    for (const d of devices) {
      const btn = document.createElement('button');
      btn.textContent = 'Connect: ' + (d.name || d.id.slice(0, 8));
      btn.onclick = () => connectToDevice(d);
      box.appendChild(btn);
    }
  } catch (e) { /* getDevices unavailable in this context — not fatal */ }
}

function forgetDevice() {
  if (!ble.device) return;
  forgetStoredKey(ble.device.id);
  if (localStorage.getItem(LAST_DEVICE_KEY) === ble.device.id) localStorage.removeItem(LAST_DEVICE_KEY);
  if (ble.device.forget) ble.device.forget();
  doDisconnect();
}

function setConnLabel(text, cls) {
  document.getElementById('connLabel').textContent = text;
  const dot = document.getElementById('connDot');
  dot.className = 'conn-dot' + (cls ? ' ' + cls : '');
}

/* ═══ UI — visually copied from the main /app.js ═════════════════════════ */

/* The same SVGs (the same <path>s) as ICONS in the main app — see the
   detailed comment there about sources (svgrepo.com) and why the "filled"
   and "outline" icons carry different wrapper attributes. */
const ICONS = {
  flame: {
    viewBox: '0 0 24 24', fill: true,
    path: '<path d="M5.926 20.574a7.26 7.26 0 0 0 3.039 1.511c.107.035.179-.105.107-.175-2.395-2.285-1.079-4.758-.107-5.873.693-.796 1.68-2.107 1.608-3.865 0-.176.18-.317.322-.211 1.359.703 2.288 2.25 2.538 3.515.394-.386.537-.984.537-1.511 0-.176.214-.317.393-.176 1.287 1.16 3.503 5.097-.072 8.19-.071.071 0 .212.072.177a8.761 8.761 0 0 0 3.003-1.442c5.827-4.5 2.037-12.48-.43-15.116-.321-.317-.893-.106-.893.351-.036.95-.322 2.004-1.072 2.707-.572-2.39-2.478-5.105-5.195-6.441-.357-.176-.786.105-.75.492.07 3.27-2.063 5.352-3.922 8.059-1.645 2.425-2.717 6.89.822 9.808z"/>',
  },
  bolt: {
    viewBox: '0 0 24 24', fill: true,
    path: '<path d="M13 2L4 14H10L9 22L20 9H13L13 2Z"/>',
  },
  floor: {
    viewBox: '0 0 24 24', fill: false,
    path: '<path d="M3 19h18"/><path d="M7 16c1-1 1-2 0-3s-1-2 0-3"/><path d="M12 16c1-1 1-2 0-3s-1-2 0-3"/><path d="M17 16c1-1 1-2 0-3s-1-2 0-3"/>',
  },
  engine: {
    viewBox: '0 0 511.999 511.999', fill: true,
    path: '<path d="M494.32,196.801l-4.858-8.131h-85.516v39.564h-8.557v-57.371h-44.32l-28.138-24.95h-46.53v-22.695h14.966V89.827H172.742v33.391h14.966v22.695h-48.443l-28.138,24.95H55.791v16.696v66.236h-22.4v-42.616H0v118.625h33.391v-42.617h22.4v66.236v16.696h83.474l58.709,52.054h197.414v-58.444h8.557v39.565h85.516l4.858-8.132c1.81-3.027,17.68-31.537,17.68-99.181S496.13,199.829,494.32,196.801z M221.101,123.22h21.909v22.695h-21.909V123.22z M468.927,369.902h-31.59v-39.565h-75.34v58.444H210.646l-58.709-52.054H89.183V204.255h34.617l28.138-24.95h158.32l28.138,24.95h23.601v57.371h75.34v-39.564h31.59c3.873,11.386,9.681,34.956,9.681,73.921C478.609,334.947,472.801,358.516,468.927,369.902z"/>',
  },
  fan: {
    viewBox: '0 0 24 24', fill: true,
    path: '<path d="M12,11a1,1,0,1,0,1,1,1,1,0,0,0-1-1m.5-9C17,2,17.1,5.57,14.73,6.75a3.36,3.36,0,0,0-1.62,2.47,3.17,3.17,0,0,1,1.23.91C18,8.13,22,8.92,22,12.5c0,4.5-3.58,4.6-4.75,2.23a3.44,3.44,0,0,0-2.5-1.62,3.24,3.24,0,0,1-.91,1.23c2,3.69,1.2,7.66-2.38,7.66C7,22,6.89,18.42,9.26,17.24a3.46,3.46,0,0,0,1.62-2.45,3,3,0,0,1-1.25-.92C5.94,15.85,2,15.07,2,11.5,2,7,5.54,6.89,6.72,9.26A3.39,3.39,0,0,0,9.2,10.87a2.91,2.91,0,0,1,.92-1.22C8.13,6,8.92,2,12.48,2Z"/>',
  },
  radiator: {
    viewBox: '0 0 491.6 491.6', fill: true,
    path: '<path d="M153.6,0H92.2C80.9,0,71.7,9.2,71.7,20.5V41H30.8c-11.3,0-20.5,9.2-20.5,20.5v61.4c0,11.3,9.2,20.5,20.5,20.5h41v204.8h-41c-11.3,0-20.5,9.2-20.5,20.5v61.4c0,11.3,9.2,20.5,20.5,20.5h41v20.5c0,11.3,9.2,20.5,20.5,20.5h61.4c11.3,0,20.5-9.2,20.5-20.5V20.5C174.1,9.1,165,0,153.6,0z M71.7,102.4H51.2V81.9h20.5V102.4z M71.7,409.6H51.2v-20.5h20.5V409.6z"/><path d="M276.5,0h-61.4c-11.3,0-20.5,9.2-20.5,20.5v450.6c0,11.3,9.2,20.5,20.5,20.5h61.4c11.3,0,20.5-9.2,20.5-20.5V20.5C297,9.1,287.8,0,276.5,0z"/><path d="M460.8,143.3c11.3,0,20.5-9.2,20.5-20.5V61.4c0-11.3-9.2-20.5-20.5-20.5h-41V20.5c0-11.3-9.2-20.5-20.5-20.5h-61.4c-11.3,0-20.5,9.2-20.5,20.5v450.6c0,11.3,9.2,20.5,20.5,20.5h61.4c11.3,0,20.5-9.2,20.5-20.5v-20.5h41c11.3,0,20.5-9.2,20.5-20.5v-61.4c0-11.3-9.2-20.5-20.5-20.5h-41V143.3H460.8z M419.9,81.9h20.5v20.5h-20.5V81.9z M419.9,389.1h20.5v20.5h-20.5V389.1z"/>',
  },
};

/* Same "pending vs confirmed" principle as desiredValues in the main app:
   a card/slider keeps showing the value the device itself confirmed, the
   spinning icon is the only sign a command is "in flight". One flat map
   for every section (the key spaces don't overlap). */
const desiredValues = {};

function makeSpinner() {
  const s = document.createElement('div');
  s.className = 'pending-spinner';
  return s;
}

const ICON_BUTTONS = [
  { key: 'heaterOn', flagKey: 'heater', label: 'Heater', icon: ICONS.flame },
  { key: 'elementOn', flagKey: 'element', label: 'Element', icon: ICONS.bolt },
  { key: 'floorOn', flagKey: 'underfloor', label: 'Floor', icon: ICONS.floor, connKey: 'underfloorConnected' },
  { key: 'engineOn', flagKey: 'engine', label: 'Engine', icon: ICONS.engine, connKey: 'engineConnected' },
];

function renderIconRow() {
  const row = document.getElementById('iconRow');
  row.innerHTML = '';
  for (const b of ICON_BUTTONS) {
    if (b.connKey && !state[b.connKey]) continue;

    const confirmed = state[b.key];
    let pending = desiredValues[b.flagKey];
    if (pending !== undefined && pending === confirmed) { delete desiredValues[b.flagKey]; pending = undefined; }
    const effectiveOn = pending !== undefined ? pending : confirmed;
    const displayOn = confirmed; // color follows the confirmed state, not the optimistic one

    const btn = document.createElement('button');
    btn.className = 'icon-btn' + (displayOn ? ' on' : '');
    const svgAttrs = b.icon.fill
      ? 'fill="currentColor" stroke="none"'
      : 'fill="none" stroke="currentColor" stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round"';
    btn.innerHTML = `<svg viewBox="${b.icon.viewBox}" ${svgAttrs}>${b.icon.path}</svg><span class="icon-label">${b.label}</span>`;
    btn.onclick = () => {
      const next = !effectiveOn;
      desiredValues[b.flagKey] = next;
      // Build every flag from "pending if any, else confirmed" — not just
      // the one just clicked. Reading the other three straight from `state`
      // (confirmed-only) lost an earlier still-in-flight click whenever a
      // second button was pressed before telemetry caught up: this packet
      // would send that flag's old confirmed value and silently revert it.
      const overrides = {};
      for (const ib of ICON_BUTTONS) {
        const p = desiredValues[ib.flagKey];
        overrides[ib.flagKey] = p !== undefined ? p : state[ib.key];
      }
      queueWrite(buildSetupPacket(overrides));
      renderIconRow();
    };
    if (pending !== undefined) btn.appendChild(makeSpinner());
    row.appendChild(btn);
  }
}

/* zoneConnected: 1 = fan zone, 3 = radiator (no fan) — the same two values
   connectedZones() filters for in the main app (0 = no zone, 2 = defrost —
   neither is shown). Indices here are 0-based (like the rest of `state`),
   unlike the main MQTT app, which numbers zones 1..5 after the topic name. */
function connectedZones() {
  const zones = [];
  for (let i = 0; i < ZONE_COUNT; i++) {
    if (state.zoneConnected[i] === 1 || state.zoneConnected[i] === 3) zones.push(i);
  }
  return zones;
}

const ZONE_STATE_NAMES = ['off', 'heat', 'vent'];
const ZONE_STATE_LABELS = { off: 'Off', heat: 'Heat', vent: 'Vent' };

let selectedZone = null;          // zone index (0-based) the drums below operate on
let zoneManuallySelected = false; // false until the user clicks a zone — auto-picks the first connected one
let openZoneMenu = null;          // zone index whose "⋮" menu is open, or null

function renderZoneRow() {
  const container = document.getElementById('zoneRow');
  container.innerHTML = '';
  const zones = connectedZones();
  if (!zoneManuallySelected || !zones.includes(selectedZone)) selectedZone = zones.length ? zones[0] : null;

  zones.forEach((i) => {
    const zoneType = state.zoneConnected[i]; // 1 or 3
    const temp = state.zoneTemp[i];
    const stateKey = `zoneState:${i}`;
    const confirmedStateNum = state.zoneState[i];
    let pendingStateNum = desiredValues[stateKey];
    if (pendingStateNum !== undefined && pendingStateNum === confirmedStateNum) {
      delete desiredValues[stateKey];
      pendingStateNum = undefined;
    }
    const stateName = ZONE_STATE_NAMES[confirmedStateNum] || 'off';
    const stateOptions = zoneType === 3 ? ['off', 'heat'] : ['off', 'heat', 'vent'];

    const card = document.createElement('div');
    card.className = 'zone-btn' + ` state-${stateName}` + (i === selectedZone ? ' selected' : '');
    card.onclick = () => {
      selectedZone = i; zoneManuallySelected = true; openZoneMenu = null;
      renderZoneRow(); renderDrumRow();
    };

    // Unlike the main MQTT app, the BLE protocol never carries the fan's
    // actual live PWM speed (Timberline::ZoneFanPwmCurrent isn't part of
    // any packet BluetoothHandler.cpp builds) — bytes 1..5 of the STATUS
    // packet are the manual-mode *setpoint* (ZoneFanManualPercent), not a
    // live reading. So there's no fan-level gauge here, and the icon never
    // spins — even gating it on "manual mode + nonzero percent" is still
    // only a guess about whether the fan is actually turning right now
    // (ignition delay, thermostat cutoff, a stalled fan...), not a fact.
    // A static icon is just a zone-type badge (fan vs radiator), nothing more.
    const zoneIconDef = zoneType === 1 ? ICONS.fan : zoneType === 3 ? ICONS.radiator : null;
    if (zoneIconDef) {
      const wrap = document.createElement('div');
      wrap.className = 'zone-icon-wrap';
      const iconEl = document.createElement('div');
      iconEl.className = 'zone-icon';
      iconEl.innerHTML = `<svg viewBox="${zoneIconDef.viewBox}" fill="currentColor" stroke="none">${zoneIconDef.path}</svg>`;
      wrap.appendChild(iconEl);
      card.appendChild(wrap);
    }

    const info = document.createElement('div');
    info.className = 'zone-info';
    info.innerHTML = `<span class="zone-temp">${temp !== null ? temp + '°' : '–'}</span>`;
    card.appendChild(info);

    if (pendingStateNum !== undefined) {
      const spinner = makeSpinner();
      spinner.classList.add('zone-spinner');
      card.appendChild(spinner);
    }

    const menuBtn = document.createElement('button');
    menuBtn.className = 'zone-menu-btn';
    menuBtn.textContent = '⋮';
    menuBtn.onclick = (e) => {
      e.stopPropagation();
      openZoneMenu = (openZoneMenu === i) ? null : i;
      renderZoneRow();
    };
    card.appendChild(menuBtn);

    if (openZoneMenu === i) {
      const menu = document.createElement('div');
      menu.className = 'zone-menu';
      stateOptions.forEach((opt) => {
        const item = document.createElement('button');
        item.className = 'zone-menu-item' + (stateName === opt ? ' active' : '');
        item.textContent = ZONE_STATE_LABELS[opt];
        item.onclick = (e) => {
          e.stopPropagation();
          const idx = ZONE_STATE_NAMES.indexOf(opt);
          desiredValues[stateKey] = idx;
          // Mutate state itself (not just an override passed to this one
          // packet) — same convention as the setpoint drums/sliders below,
          // so a fan-manual toggle or fan% change still pending for this
          // zone isn't reverted by buildStatusPacket()'s own state.* fallback.
          const zoneStateArr = state.zoneState.slice();
          zoneStateArr[i] = idx;
          state.zoneState = zoneStateArr;
          queueWrite(buildStatusPacket());
          openZoneMenu = null;
          renderZoneRow();
        };
        menu.appendChild(item);
      });
      card.appendChild(menu);
    }

    container.appendChild(card);
  });
}

document.addEventListener('click', () => {
  if (openZoneMenu !== null) { openZoneMenu = null; renderZoneRow(); }
});

/* ── Setpoint drum pickers for the selected zone — the drag/inertia/snap
   physics is ported from the main app verbatim; only what finish() does
   differs (writes a BLE packet instead of mqtt.publish). */
let drumBusyCount = 0;
const REEL_ROW_H = 34;
const REEL_VISIBLE = 3;

const DRUMS = [
  { key: 'day', label: 'Day', min: 10, max: 32, step: 1, unit: '°' },
  { key: 'fan', label: 'Fan', min: 10, max: 100, step: 1, unit: '%', isFan: true },
  { key: 'night', label: 'Night', min: 10, max: 32, step: 1, unit: '°' },
];

function renderDrumRow() {
  if (drumBusyCount > 0) return;
  const container = document.getElementById('drumRow');
  container.innerHTML = '';
  if (selectedZone === null) return;
  const zoneType = state.zoneConnected[selectedZone];
  const hasFan = zoneType !== 3;
  DRUMS.filter((d) => !d.isFan || hasFan).forEach((d) => container.appendChild(buildDrum(d)));
}

function wireLongPress(el, onLongPress) {
  const MOVE_CANCELS_PRESS_PX = 6;
  const LONG_PRESS_MS = 600;
  let startY = null;
  let pressTimer = null;
  el.addEventListener('pointerdown', (e) => {
    startY = e.clientY;
    pressTimer = setTimeout(() => { pressTimer = null; startY = null; onLongPress(); }, LONG_PRESS_MS);
  });
  el.addEventListener('pointermove', (e) => {
    if (startY === null || !pressTimer) return;
    if (Math.abs(e.clientY - startY) > MOVE_CANCELS_PRESS_PX) { clearTimeout(pressTimer); pressTimer = null; }
  });
  const end = () => { if (pressTimer) { clearTimeout(pressTimer); pressTimer = null; } startY = null; };
  el.addEventListener('pointerup', end);
  el.addEventListener('pointercancel', end);
}

function buildDrum(d) {
  const zone = selectedZone;
  const dataKey = `${d.key}:${zone}`;
  const confirmed = d.key === 'day' ? state.daySetpoint[zone]
    : d.key === 'night' ? state.nightSetpoint[zone]
      : state.fanPercent[zone];
  let pending = desiredValues[dataKey];
  if (pending !== undefined && pending === confirmed) { delete desiredValues[dataKey]; pending = undefined; }
  const isPending = pending !== undefined;
  const raw = isPending ? pending : confirmed;
  const isAuto = !!d.isFan && !state.fanManual[zone];

  const drum = document.createElement('div');
  drum.className = 'drum' + (d.isFan ? (isAuto ? ' auto' : ' manual') : '');

  const label = document.createElement('div');
  label.className = 'drum-label';
  label.textContent = d.label;

  const toggleFanManual = () => {
    if (!d.isFan) return;
    // Mutate state directly (see the zone-menu comment above) — otherwise
    // a still-pending zoneState or fan% change for this same zone would be
    // reverted by this packet's state.* fallback for the other two fields.
    const arr = state.fanManual.slice();
    arr[zone] = !arr[zone];
    state.fanManual = arr;
    queueWrite(buildStatusPacket());
  };
  if (d.isFan) wireLongPress(drum, toggleFanManual);

  if (isAuto || raw === undefined || raw === null) {
    const staticEl = document.createElement('div');
    staticEl.className = 'drum-static';
    staticEl.textContent = isAuto ? 'auto' : '–';
    drum.appendChild(staticEl);
    drum.appendChild(label);
    return drum;
  }

  const values = [];
  for (let v = d.min; v <= d.max; v += d.step) values.push(v);
  const currentVal = Math.min(d.max, Math.max(d.min, parseInt(raw, 10)));
  let baseIndex = values.indexOf(currentVal);
  if (baseIndex === -1) baseIndex = 0;

  const mask = document.createElement('div');
  mask.className = 'drum-reel-mask' + (isPending ? ' pending' : '');
  mask.style.height = `${REEL_ROW_H * REEL_VISIBLE}px`;

  const reel = document.createElement('div');
  reel.className = 'drum-reel';
  reel.style.width = '100%';
  values.forEach((v) => {
    const row = document.createElement('div');
    row.className = 'reel-item';
    row.style.height = `${REEL_ROW_H}px`;
    row.textContent = `${v}${d.unit}`;
    reel.appendChild(row);
  });

  const highlight = document.createElement('div');
  highlight.className = 'drum-highlight';
  highlight.style.height = `${REEL_ROW_H}px`;

  mask.appendChild(reel);
  mask.appendChild(highlight);
  drum.appendChild(mask);
  drum.appendChild(label);

  const indexOffset = (idx) => ((REEL_VISIBLE - 1) / 2) * REEL_ROW_H - idx * REEL_ROW_H;
  let dragOffset = 0;

  const minTotal = indexOffset(values.length - 1);
  const maxTotal = indexOffset(0);
  const OVERSCROLL_GIVE = 0.35;
  const clampDragOffset = () => {
    const total = indexOffset(baseIndex) + dragOffset;
    let clamped = total;
    if (total > maxTotal) clamped = maxTotal + (total - maxTotal) * OVERSCROLL_GIVE;
    else if (total < minTotal) clamped = minTotal + (total - minTotal) * OVERSCROLL_GIVE;
    dragOffset += clamped - total;
  };

  const paint = () => { reel.style.transform = `translateY(${indexOffset(baseIndex) + dragOffset}px)`; };
  paint();

  let startY = null, lastY = null, lastT = null, velocity = 0;
  let rafId = null;
  const stopAnim = () => { if (rafId !== null) { cancelAnimationFrame(rafId); rafId = null; } };

  let iAmBusy = false;
  const markBusy = () => { if (!iAmBusy) { iAmBusy = true; drumBusyCount++; } };
  const markIdle = () => { if (iAmBusy) { iAmBusy = false; drumBusyCount = Math.max(0, drumBusyCount - 1); } };

  function runInertia() {
    const FRICTION = 0.95;
    let last = performance.now();
    function step(now) {
      const dt = now - last; last = now;
      velocity *= Math.pow(FRICTION, dt / 16.67);
      dragOffset += velocity * dt;
      clampDragOffset();
      paint();
      if (Math.abs(velocity) > 0.02) rafId = requestAnimationFrame(step);
      else snap();
    }
    rafId = requestAnimationFrame(step);
  }

  function snap() {
    const total = indexOffset(baseIndex) + dragOffset;
    let idx = Math.round((((REEL_VISIBLE - 1) / 2) * REEL_ROW_H - total) / REEL_ROW_H);
    idx = Math.max(0, Math.min(values.length - 1, idx));
    const target = indexOffset(idx);
    const startOffset = total;
    const startTime = performance.now();
    const DURATION = 180;
    function ease(now) {
      const tt = Math.min(1, (now - startTime) / DURATION);
      const eased = 1 - Math.pow(1 - tt, 3);
      reel.style.transform = `translateY(${startOffset + (target - startOffset) * eased}px)`;
      if (tt < 1) { rafId = requestAnimationFrame(ease); }
      else { finish(idx); }
    }
    rafId = requestAnimationFrame(ease);
  }

  function finish(idx) {
    markIdle();
    baseIndex = idx;
    dragOffset = 0;
    const newVal = values[idx];
    if (newVal !== currentVal) {
      desiredValues[dataKey] = newVal;
      if (d.key === 'day') {
        const arr = state.daySetpoint.slice(); arr[zone] = newVal; state.daySetpoint = arr;
        queueWrite(buildSetupPacket());
      } else if (d.key === 'night') {
        const arr = state.nightSetpoint.slice(); arr[zone] = newVal; state.nightSetpoint = arr;
        queueWrite(buildSetupPacket());
      } else if (d.key === 'fan') {
        const arr = state.fanPercent.slice(); arr[zone] = newVal; state.fanPercent = arr;
        queueWrite(buildStatusPacket());
      }
    }
    renderDrumRow();
  }

  mask.addEventListener('pointerdown', (e) => {
    stopAnim();
    markBusy();
    mask.setPointerCapture(e.pointerId);
    startY = lastY = e.clientY;
    lastT = performance.now();
    velocity = 0;
  });
  mask.addEventListener('pointermove', (e) => {
    if (startY === null) return;
    const now = performance.now();
    const dy = e.clientY - lastY;
    const dt = Math.max(1, now - lastT);
    dragOffset += dy;
    clampDragOffset();
    velocity = 0.7 * velocity + 0.3 * (dy / dt);
    lastY = e.clientY; lastT = now;
    paint();
  });
  const onRelease = () => {
    if (startY === null) return;
    startY = null;
    runInertia();
  };
  mask.addEventListener('pointerup', onRelease);
  mask.addEventListener('pointercancel', onRelease);
  mask.addEventListener('wheel', (e) => {
    e.preventDefault();
    stopAnim();
    markBusy();
    dragOffset += e.deltaY < 0 ? REEL_ROW_H : -REEL_ROW_H;
    clampDragOffset();
    paint();
    velocity = 0;
    snap();
  });

  return drum;
}

/* ── Floor / engine / schedule — the same range sliders and the same
   pending logic (blue accent until confirmed) as in the main app
   (buildSettingsRow/updateSettingsGroup there). */
const FLOOR_SETTINGS = [
  { key: 'floorSetpoint', label: 'Setpoint', min: 3, max: 32, step: 1, unit: '°' },
  { key: 'floorHysteresis', label: 'Hysteresis', min: 2, max: 10, step: 1, unit: '°' },
];
const ENGINE_SETTINGS = [
  { key: 'engineSetpoint', label: 'Setpoint', min: 0, max: 80, step: 1, unit: '°' },
  {
    key: 'engineTimeLimit', label: 'Run time', min: 10, max: 1450, step: 10, unit: ' min',
    format: (v) => (Number(v) > 1440 ? 'Unlimited' : `${v} min`),
  },
];
const SCHEDULE_SETTINGS = [
  { key: 'systemTimeLimit', label: 'System run-time limit', min: 1, max: 100, step: 1, unit: ' h' },
];

function buildSettingsRow(groupId, connKey, settings, s) {
  const row = document.createElement('div');
  row.className = 'setting-row';

  const label = document.createElement('label');
  const text = document.createElement('span');
  text.textContent = s.label;
  const value = document.createElement('span');
  value.className = 'setting-value';
  value.id = `${groupId}-${s.key}-value`;
  label.appendChild(text);
  label.appendChild(value);

  const input = document.createElement('input');
  input.type = 'range';
  input.id = `${groupId}-${s.key}`;
  input.min = String(s.min); input.max = String(s.max); input.step = String(s.step);
  input.addEventListener('input', () => { value.textContent = s.format ? s.format(input.value) : input.value + s.unit; });
  input.addEventListener('change', () => {
    const v = Number(input.value);
    desiredValues[s.key] = v;
    state[s.key] = v; // apply locally right away so the other fields in the WORK packet don't roll back
    queueWrite(buildWorkPacket());
    updateSettingsGroup(groupId, connKey, settings);
  });

  row.appendChild(label);
  row.appendChild(input);
  return row;
}

let settingsRowsBuilt = false;
function buildSettingsPanels() {
  if (settingsRowsBuilt) return;
  settingsRowsBuilt = true;
  FLOOR_SETTINGS.forEach((s) => document.getElementById('floorGroup').appendChild(buildSettingsRow('floorGroup', 'underfloorConnected', FLOOR_SETTINGS, s)));
  ENGINE_SETTINGS.forEach((s) => document.getElementById('engineGroup').appendChild(buildSettingsRow('engineGroup', 'engineConnected', ENGINE_SETTINGS, s)));
  SCHEDULE_SETTINGS.forEach((s) => document.getElementById('scheduleGroup').appendChild(buildSettingsRow('scheduleGroup', null, SCHEDULE_SETTINGS, s)));
}

function updateSettingsGroup(groupId, connKey, settings) {
  const details = document.getElementById(groupId);
  const visible = connKey ? !!state[connKey] : true;
  details.classList.toggle('hidden', !visible);
  if (!visible) return;

  settings.forEach((s) => {
    const raw = state[s.key];
    const input = document.getElementById(`${groupId}-${s.key}`);
    const valueEl = document.getElementById(`${groupId}-${s.key}-value`);
    let pending = desiredValues[s.key];
    if (pending !== undefined && raw !== undefined && pending === raw) { delete desiredValues[s.key]; pending = undefined; }
    const display = pending !== undefined ? pending : raw;
    if (display === undefined || display === null) return;

    if (document.activeElement !== input) {
      if (Number(input.value) !== Number(display)) input.value = display;
      valueEl.textContent = s.format ? s.format(display) : display + s.unit;
    }
    input.classList.toggle('pending', pending !== undefined);
  });
}

function renderSettings() {
  buildSettingsPanels();
  updateSettingsGroup('floorGroup', 'underfloorConnected', FLOOR_SETTINGS);
  updateSettingsGroup('engineGroup', 'engineConnected', ENGINE_SETTINGS);
  updateSettingsGroup('scheduleGroup', null, SCHEDULE_SETTINGS);
}

function renderErrors() {
  const card = document.getElementById('errorsCard');
  const list = document.getElementById('errorsList');
  const active = state.errors.filter((c) => c);
  card.classList.toggle('no-errors', active.length === 0);
  list.innerHTML = '';
  if (active.length === 0) {
    list.innerHTML = '<div class="no-errors-text">No errors</div>';
    return;
  }
  for (const code of active) {
    const row = document.createElement('div');
    row.className = 'error-row';
    row.innerHTML = `<span>${errorText(code)}</span><span>#${code}</span>`;
    list.appendChild(row);
  }
}

function renderMisc() {
  document.getElementById('outdoorTemp').textContent = state.outdoorTemp !== null ? state.outdoorTemp + '°C' : '—';
  const dhw = document.getElementById('domesticWaterBadge');
  dhw.textContent = state.domesticWaterOn ? 'on' : 'off';
  dhw.classList.toggle('on', state.domesticWaterOn);
  document.getElementById('heaterFw').textContent = state.heaterVersion || '—';
  document.getElementById('panelFw').textContent = state.panelVersion || '—';
  document.getElementById('hcuFw').textContent = state.hcuVersion || '—';
}

function render() {
  renderIconRow();
  renderZoneRow();
  renderDrumRow();
  renderSettings();
  renderSchedule();
  renderErrors();
  renderMisc();
  renderRestorePanel();
}

/* ── Static control wiring ────────────────────────────────────────────── */
function wireStaticControls() {
  document.getElementById('connectBtn').onclick = doConnectPicker;
  document.getElementById('connectAllBtn').onclick = doConnectPickerUnfiltered;
  document.getElementById('disconnectBtn').onclick = doDisconnect;
  document.getElementById('forgetBtn').onclick = forgetDevice;
  document.getElementById('syncTimeBtn').onclick = () => queueWrite(buildTimePacket());
  document.getElementById('clearErrorsBtn').onclick = () => {
    queueWrite(buildSetupPacket({ clearError: true }));
  };

  document.getElementById('dayStart').onchange = (e) => {
    const [h, m] = e.target.value.split(':').map(Number);
    state.dayStartHour = h; state.dayStartMinute = m;
    queueWrite(buildWorkPacket());
  };
  document.getElementById('nightStart').onchange = (e) => {
    const [h, m] = e.target.value.split(':').map(Number);
    state.nightStartHour = h; state.nightStartMinute = m;
    queueWrite(buildWorkPacket());
  };
}

/* ── External memory (BLE) panel — diagnostic front-end for the writeMemoryRegion()
   protocol above. Stage 1 of firmware distribution over BLE: raw address+length
   access only, so it can be exercised (and trusted) before stage 2 wires it up to
   server-hosted firmware images and stage 3 automates loading a whole slot. */
function parseHexAddr(str) {
  const v = parseInt(str, 16);
  if (!Number.isFinite(v) || v < 0) throw new Error('invalid address');
  return v >>> 0;
}

function showMemTab(name) {
  const isSlots = name === 'slots';
  document.getElementById('memTabSlots').classList.toggle('hidden', !isSlots);
  document.getElementById('memTabRaw').classList.toggle('hidden', isSlots);
  document.getElementById('memTabSlotsBtn').classList.toggle('primary', isSlots);
  document.getElementById('memTabRawBtn').classList.toggle('primary', !isSlots);
}

function wireMemoryControls() {
  initFragTimingInputs();
  wireFragLogCopyButtons();
  document.getElementById('memTabSlotsBtn').onclick = () => showMemTab('slots');
  document.getElementById('memTabRawBtn').onclick = () => showMemTab('raw');

  document.getElementById('memReadBtn').onclick = async () => {
    const out = document.getElementById('memReadResult');
    try {
      const addr = parseHexAddr(document.getElementById('memReadAddr').value);
      out.textContent = 'Reading…';
      const resp = await memRead4(addr);
      out.textContent = resp.status === 0
        ? '0x' + resp.bytes.map((b) => b.toString(16).padStart(2, '0')).join('')
        : 'out of bounds';
    } catch (e) { out.textContent = 'Error: ' + e.message; }
  };

  document.getElementById('memCrcBtn').onclick = async () => {
    const out = document.getElementById('memCrcResult');
    try {
      const addr = parseHexAddr(document.getElementById('memCrcAddr').value);
      const len = parseInt(document.getElementById('memCrcLen').value, 10);
      if (!Number.isFinite(len) || len <= 0) throw new Error('invalid length');
      out.textContent = 'Computing…';
      const resp = await memCrcRegion(addr, len);
      out.textContent = resp.status === 0 ? '0x' + (resp.crc >>> 0).toString(16) : 'out of bounds';
    } catch (e) { out.textContent = 'Error: ' + e.message; }
  };

  document.getElementById('memEraseBtn').onclick = async () => {
    const block = parseInt(document.getElementById('memEraseBlock').value, 10);
    if (!Number.isFinite(block) || block < 0 || block > 127) { alert('Block index must be 0-127'); return; }
    if (!confirm(`Erase 64KB block #${block}? This cannot be undone.`)) return;
    try { await memErase(block); alert('Block erased.'); } catch (e) { alert('Erase failed: ' + e.message); }
  };

  document.getElementById('memEraseChipBtn').onclick = async () => {
    if (!confirm('Erase the ENTIRE external flash chip (8 MB)? This destroys ALL stored firmware slots and cannot be undone.')) return;
    try { await memErase(255); alert('Chip erased.'); } catch (e) { alert('Erase failed: ' + e.message); }
  };

  document.getElementById('memUploadBtn').onclick = async () => {
    const file = document.getElementById('memUploadFile').files[0];
    if (!file) { alert('Choose a file first'); return; }
    let addr;
    try { addr = parseHexAddr(document.getElementById('memUploadAddr').value); }
    catch (e) { alert(e.message); return; }
    const data = new Uint8Array(await file.arrayBuffer());
    await acquireWakeLock();
    try {
      await runMemUpload(document.getElementById('memUploadBtn'), addr, data);
    } finally {
      releaseWakeLock();
    }
  };

  wireServerFirmwareControls();
}

// Shared by every upload source (local file / server-fetched, distribution
// slots / restore area) - takes its progress-bar/text element ids explicitly
// (defaulting to the "External memory" section's own) so progress always
// renders in whichever section the button that triggered it actually lives
// in. It used to be hardcoded to the "External memory" section's elements
// even when called from "Self-update", which sits in its own collapsible
// <details> - if that one happened to be open and "External memory" closed,
// the only visible feedback for a multi-minute transfer was silence followed
// by a sudden "Done", which is exactly what looked like an instant, clearly
// fake completion.
function formatElapsed(ms) {
  const totalSec = ms / 1000;
  if (totalSec < 60) return `${totalSec.toFixed(1)}s`;
  const m = Math.floor(totalSec / 60);
  const s = Math.round(totalSec - m * 60);
  return `${m}m ${s}s`;
}

// Returns true on a completed, verified transfer; false on cancel or failure
// (message already shown in `text`) - callers that do more afterward (write
// a meta record, reboot) MUST check this and bail out rather than treat "the
// call returned" as "the transfer succeeded" (see updateRestoreArea() /
// fwLoadBtn's handler: writing a slot/restore meta record for data that was
// never fully, verifiably written would make the device treat a
// cancelled/failed transfer as a valid image).
async function runMemUpload(triggerBtn, addr, data, els) {
  const wrap = document.getElementById(els?.wrap || 'memProgressWrap');
  const bar = document.getElementById(els?.bar || 'memProgressBar');
  const text = document.getElementById(els?.text || 'memProgressText');
  const logElId = els?.log || 'memFragLog';
  const logEl = document.getElementById(logElId);
  const cancelBtn = document.getElementById(els?.cancelBtn || 'memUploadCancelBtn');
  if (logEl) logEl.textContent = ''; // clear any previous run's rounds before this one starts
  triggerBtn.disabled = true;
  wrap.classList.remove('hidden');
  bar.style.width = '0%';

  const token = { cancelled: false };
  if (cancelBtn) {
    cancelBtn.classList.remove('hidden');
    cancelBtn.disabled = false;
    cancelBtn.onclick = () => {
      token.cancelled = true;
      cancelBtn.disabled = true;
      text.textContent = 'Cancelling…';
    };
  }

  const t0 = performance.now();
  try {
    await writeMemoryRegionBurst(addr, data, (bytesDone, bytesTotal, packetsDone, packetsTotal) => {
      const pct = Math.round((bytesDone / bytesTotal) * 100);
      bar.style.width = pct + '%';
      text.textContent = `Transferred mini-fragment ${packetsDone}/${packetsTotal} `
        + `(${bytesDone}/${bytesTotal} bytes, ${pct}%)`;
    }, logElId, token);
    text.textContent = `Done — ${data.length} bytes written at 0x${addr.toString(16)} `
      + `in ${formatElapsed(performance.now() - t0)}.`;
    return true;
  } catch (e) {
    text.textContent = e instanceof TransferCancelled
      ? `Cancelled after ${formatElapsed(performance.now() - t0)}.`
      : 'Upload failed: ' + e.message;
    return false;
  } finally {
    triggerBtn.disabled = false;
    if (cancelBtn) cancelBtn.classList.add('hidden');
  }
}

/* ── Firmware from server (stage 2) ────────────────────────────────────────
   Same server endpoints the main MQTT app already uses for OTA (see
   host/README.md "Firmware OTA", host/timberline-web/server.js) — no new
   backend code needed, this just calls them from the browser instead of
   having the modem fetch them over cellular AT+HTTP. Deliberately does NOT
   apply the main app's versionsForSubtype() filter (which hides e.g. Multihot
   builds when a Timberline-subtype device is connected) - every published
   126.x version is listed, since any of the panel's 3 generic slots can hold
   any of them regardless of what's currently running. */
const FIRMWARE_TYPE = '126';

// Mirrors PU28-Timberline's User/Memory/memory.h slot layout exactly - keep
// these in sync if that header's constants ever change.
const MEM_CHIP_SIZE = 0x800000;
const MEM_SLOT_COUNT = 3;
const MEM_SLOT_META_SIZE = 0x10000;
const MEM_SLOT_DATA_SIZE = 0x80000;
const MEM_SLOT_SIZE = MEM_SLOT_META_SIZE + MEM_SLOT_DATA_SIZE;
const MEM_SLOTS_START = MEM_CHIP_SIZE - MEM_SLOT_COUNT * MEM_SLOT_SIZE;
function slotMetaAddr(n) { return MEM_SLOTS_START + n * MEM_SLOT_SIZE; }
function slotDataAddr(n) { return slotMetaAddr(n) + MEM_SLOT_META_SIZE; }

// Slot meta record (14 bytes) - matches Memory::writeFirmwareMeta() exactly
// (PU28-BOOT-CAN/User/Main/memory.cpp, read back by PU28-Timberline's own
// Memory::readFirmwareMeta()): target address(4) + len(4) + CRC16(2) +
// version(4), all little-endian. Unlike the restore header, this DOES carry
// a target address - a slot's image is meant for some other device's own
// flash (whatever address SlotsScreen's CAN relay will later copy it to),
// not this panel's, so that address has to come from somewhere: the
// server's per-version /profile endpoint, the same "flashBase" baked into
// the published filename (<version>_0x<flashBase>.bin - see host/README.md).
const MEM_SLOT_META_SIZE_BYTES = 14;

// Type is always the version string's own first segment (this org's version
// scheme is <type>.<subtype/voltage>.<...>.<...> - see host/README.md), so
// there's never a separate type to track alongside a version string.
function firmwareTypeOf(version) { return version.split('.')[0]; }

async function fetchFirmwareProfile(version) {
  const r = await fetch(`/firmware/${firmwareTypeOf(version)}/${version}/profile`);
  if (!r.ok) throw new Error(`profile fetch failed: HTTP ${r.status}`);
  const text = await r.text();
  const m = /flashBase=0x([0-9A-Fa-f]+)/.exec(text);
  if (!m) throw new Error('server profile response missing flashBase');
  return { flashBase: parseInt(m[1], 16) };
}

function buildSlotMeta(flashBase, len, crc, version) {
  const verParts = version.split('.').map(Number);
  const meta = new Uint8Array(MEM_SLOT_META_SIZE_BYTES);
  meta[0] = flashBase & 0xFF; meta[1] = (flashBase >>> 8) & 0xFF;
  meta[2] = (flashBase >>> 16) & 0xFF; meta[3] = (flashBase >>> 24) & 0xFF;
  meta[4] = len & 0xFF; meta[5] = (len >>> 8) & 0xFF;
  meta[6] = (len >>> 16) & 0xFF; meta[7] = (len >>> 24) & 0xFF;
  meta[8] = crc & 0xFF; meta[9] = (crc >>> 8) & 0xFF;
  meta[10] = verParts[0] || 0; meta[11] = verParts[1] || 0;
  meta[12] = verParts[2] || 0; meta[13] = verParts[3] || 0;
  return meta;
}

// Panel backup/restore area, right before the slots above - a single fixed-purpose
// region PU28-BOOT-CAN's bootloader auto-restores from on BOOT_MAGIC_UPDATE, NOT one
// of the generic distribution slots. Mirrors PU28-Timberline's User/Memory/memory.h
// (added there alongside this feature - that app never wrote here before).
const MEM_BACKUP_META_SIZE = 0x10000;
const MEM_BACKUP_DATA_SECTORS = 4;
const MEM_BACKUP_DATA_SIZE = MEM_BACKUP_DATA_SECTORS * 0x10000;
const MEM_BACKUP_SIZE = MEM_BACKUP_META_SIZE + MEM_BACKUP_DATA_SIZE;
const MEM_BACKUP_META_ADDR = MEM_SLOTS_START - MEM_BACKUP_SIZE;
const MEM_ADDRESS_BACKUP = MEM_BACKUP_META_ADDR + MEM_BACKUP_META_SIZE;
const MEM_BACKUP_HEADER_SIZE = 16;
const BACKUP_MAGIC_0 = 0xBB;
const BACKUP_MAGIC_1 = 0xAA;

// CRC-16/Modbus (poly 0xA001, init 0xFFFF, no final XOR) - matches PU28-BOOT-CAN's
// calcCrc()/ADDRESS_CRC and server.js's crc16() bit-for-bit, the one checksum every
// *stored* firmware-integrity check in this org uses. Distinct from memCrcStep()
// above, which only verifies one BLE fragment survived transport intact - this one
// goes into the restore header itself, exactly like Boot::saveBackup() computes it.
function crc16Modbus(bytes) {
  let crc = 0xFFFF;
  for (let i = 0; i < bytes.length; i++) {
    let b = bytes[i];
    for (let bit = 0; bit < 8; bit++) {
      const carry = crc & 1;
      crc >>= 1;
      if ((b & 1) !== carry) crc ^= 0xA001;
      b >>= 1;
    }
  }
  return crc & 0xFFFF;
}

function buildRebootPacket(mode) { // mirrors CAN PGN1 command 22's D[2] - see BluetoothHandler.cpp case 11
  const b = pkt(); b[0] = PACKET_TYPE.REBOOT; b[1] = mode; return b;
}

let deviceTypeNames = {};
async function loadDeviceTypeNames() {
  try {
    const r = await fetch('/device-types.json');
    deviceTypeNames = await r.json();
  } catch (e) { /* purely cosmetic - versions still list fine without names */ }
}
function firmwareDisplayName(version) {
  const subtype = version.split('.').slice(0, 2).join('.'); // e.g. "126.3"
  const name = deviceTypeNames[subtype];
  return name ? `${version} — ${name}` : version;
}

function populateVersionSelect(sel, versions, emptyLabel) {
  sel.innerHTML = '';
  if (versions.length === 0) {
    sel.innerHTML = `<option value="">${emptyLabel}</option>`;
    return;
  }
  for (const v of versions) {
    const opt = document.createElement('option');
    opt.value = v;
    opt.textContent = firmwareDisplayName(v);
    sel.appendChild(opt);
  }
}

// Every published 126.x version (this device's own type only) - feeds
// renderRestorePanel()'s subtype filter. Self-update deliberately stays
// scoped to one type/subtype (see Version.h) - unlike the slots dropdown
// below, this one is NOT meant to show everything on the server.
let allFirmwareVersions = [];

async function fetchFirmwareVersions() {
  try {
    const r = await fetch(`/firmware/${FIRMWARE_TYPE}/versions`);
    const data = await r.json();
    allFirmwareVersions = Array.isArray(data.versions) ? data.versions : [];
    renderRestorePanel();
  } catch (e) {
    console.error('fetchFirmwareVersions failed', e);
  }
}

// Every published version across every type on the server (see server.js's
// GET /firmware/versions) - the distribution-slots dropdown: a slot can hold
// firmware for whatever CAN device it's meant to relay to next, so unlike
// the restore picker above, this one is genuinely unfiltered.
async function fetchAllFirmwareVersions() {
  const sel = document.getElementById('fwVersionSelect');
  try {
    const r = await fetch('/firmware/versions');
    const data = await r.json();
    const versions = Object.values(data.types || {}).flat().sort(compareVersions);
    populateVersionSelect(sel, versions, '(none published)');
  } catch (e) {
    sel.innerHTML = '<option value="">(failed to load — see console)</option>';
    console.error('fetchAllFirmwareVersions failed', e);
  }
}

// "126.3.0.28" -> "126.3" - the axis a self-update must stay within (logo x
// language variant, see Version.h's VERSION_2 in PU28-Timberline). Everything
// past that (the 3rd/4th bytes) is exactly what an update is meant to change.
function panelSubtype() {
  return state.panelVersion ? state.panelVersion.split('.').slice(0, 2).join('.') : null;
}

// Numeric, not lexicographic - "126.3.0.9" must sort below "126.3.0.28".
function compareVersions(a, b) {
  const pa = a.split('.').map(Number);
  const pb = b.split('.').map(Number);
  for (let i = 0; i < Math.max(pa.length, pb.length); i++) {
    const diff = (pa[i] || 0) - (pb[i] || 0);
    if (diff !== 0) return diff;
  }
  return 0;
}

// render() runs on every incoming BLE packet (several a second once connected) -
// renderRestorePanel() used to rebuild the <select>'s <option> list every single
// call regardless of whether anything actually changed, which reset/closed the
// dropdown out from under the user mid-click (visible as constant flicker,
// impossible to pick anything). restoreRenderedKey caches what's currently in
// the DOM (subtype + the exact version list) so the rebuild - and the value
// reset that comes with it - only happens when that content genuinely changes,
// never on a render() tick that has nothing new to show here.
let restoreRenderedKey = null;

function renderRestorePanel() {
  const sel = document.getElementById('restoreVersionSelect');
  const note = document.getElementById('restoreNote');
  const btn = document.getElementById('restoreUpdateBtn');
  const subtype = panelSubtype();
  if (!subtype) {
    if (restoreRenderedKey !== null) {
      sel.innerHTML = '<option value="">(waiting for panel firmware info…)</option>';
      btn.disabled = true;
      restoreRenderedKey = null;
    }
    note.textContent = 'Connect and wait a few seconds for the panel to report its own firmware version.';
    return;
  }
  const matching = allFirmwareVersions
    .filter((v) => v.split('.').slice(0, 2).join('.') === subtype)
    .sort(compareVersions).reverse(); // newest first
  note.textContent = `Panel is running ${state.panelVersion} — showing only ${subtype}.x builds `
    + `(self-update must stay within the same logo/language variant).`;

  const key = subtype + '|' + matching.join(',');
  if (key === restoreRenderedKey) return; // nothing actually changed - leave the DOM (and any open dropdown) alone
  const previousValue = sel.value;
  populateVersionSelect(sel, matching, `(no ${subtype}.x versions published)`);
  if (matching.length > 0) {
    sel.value = (restoreRenderedKey !== null && restoreRenderedKey.startsWith(subtype + '|') && matching.includes(previousValue))
      ? previousValue  // keep the user's own pick across re-renders
      : matching[0];   // subtype (or the published list) just changed - default to newest
  }
  restoreRenderedKey = key;
  btn.disabled = matching.length === 0;
}

async function updateRestoreArea(version, btn, note) {
  note.textContent = `Step 1/4 — downloading ${version} from server…`;
  const r = await fetch(`/firmware/${FIRMWARE_TYPE}/${version}/firmware.bin`);
  if (!r.ok) throw new Error(`server returned HTTP ${r.status}`);
  const data = new Uint8Array(await r.arrayBuffer());
  if (data.length > MEM_BACKUP_DATA_SIZE) {
    throw new Error(`image is ${data.length}B, larger than the restore area's ${MEM_BACKUP_DATA_SIZE}B`);
  }
  note.textContent = `Step 1/4 — downloaded ${data.length} bytes.`;

  const blocksNeeded = Math.ceil(data.length / 0x10000);
  note.textContent = `Step 2/4 — erasing restore area (1 meta block + ${blocksNeeded} data block(s))…`;
  await memErase(MEM_BACKUP_META_ADDR >>> 16);
  for (let i = 0; i < blocksNeeded; i++) await memErase((MEM_ADDRESS_BACKUP >>> 16) + i);
  note.textContent = `Step 2/4 — erased ${1 + blocksNeeded} block(s).`;

  note.textContent = `Step 3/4 — transferring ${data.length} bytes over BLE…`;
  const ok = await runMemUpload(btn, MEM_ADDRESS_BACKUP, data, {
    wrap: 'restoreProgressWrap', bar: 'restoreProgressBar', text: 'restoreProgressText',
    log: 'restoreFragLog', cancelBtn: 'restoreCancelBtn',
  });
  if (!ok) throw new Error('transfer did not complete — restore metadata not written, panel not touched');

  const crc = crc16Modbus(data);
  const verParts = version.split('.').map(Number);
  const hdr = new Uint8Array(MEM_BACKUP_HEADER_SIZE).fill(0xFF);
  hdr[0] = BACKUP_MAGIC_0; hdr[1] = BACKUP_MAGIC_1;
  hdr[2] = data.length & 0xFF; hdr[3] = (data.length >>> 8) & 0xFF;
  hdr[4] = (data.length >>> 16) & 0xFF; hdr[5] = (data.length >>> 24) & 0xFF;
  hdr[6] = crc & 0xFF; hdr[7] = (crc >>> 8) & 0xFF;
  hdr[8] = verParts[0] || 0; hdr[9] = verParts[1] || 0; hdr[10] = verParts[2] || 0; hdr[11] = verParts[3] || 0;
  note.textContent = 'Step 4/4 — writing restore metadata (length/CRC16/version)…';
  await writeMemoryFragment(MEM_BACKUP_META_ADDR, hdr);
  note.textContent = 'Step 4/4 — metadata written.';
  return { data, crc };
}

function wireServerFirmwareControls() {
  loadDeviceTypeNames().then(() => { fetchFirmwareVersions(); fetchAllFirmwareVersions(); });

  document.getElementById('restoreUpdateBtn').onclick = async () => {
    const version = document.getElementById('restoreVersionSelect').value;
    const note = document.getElementById('restoreNote');
    const btn = document.getElementById('restoreUpdateBtn');
    if (!version) return;
    if (!confirm(`Update to ${version} now? This erases the current restore-area backup, writes `
      + `the new image, then reboots the panel to apply it — the BLE connection will drop.`)) return;
    btn.disabled = true;
    await acquireWakeLock();
    try {
      const { data, crc } = await updateRestoreArea(version, btn, note);
      note.textContent = `${version} staged (${data.length}B, CRC16 0x${crc.toString(16)}) — rebooting to apply…`;
      queueWrite(buildRebootPacket(10));
    } catch (e) {
      note.textContent = 'Failed: ' + e.message;
    } finally {
      btn.disabled = false;
      releaseWakeLock();
    }
  };

  document.getElementById('restoreApplyBtn').onclick = () => {
    if (!confirm('Reboot the panel now and apply the staged restore-area update (if valid)? '
      + 'The BLE connection will drop.')) return;
    queueWrite(buildRebootPacket(10));
  };

  document.getElementById('fwLoadBtn').onclick = async () => {
    const version = document.getElementById('fwVersionSelect').value;
    const slot = parseInt(document.getElementById('fwSlotSelect').value, 10);
    const info = document.getElementById('fwInfo');
    const btn = document.getElementById('fwLoadBtn');
    if (!version) { alert('No version selected'); return; }
    btn.disabled = true;
    await acquireWakeLock();
    try {
      info.textContent = `Step 1/4 — downloading ${version} from server…`;
      const [r, profile] = await Promise.all([
        fetch(`/firmware/${firmwareTypeOf(version)}/${version}/firmware.bin`),
        fetchFirmwareProfile(version),
      ]);
      if (!r.ok) throw new Error(`server returned HTTP ${r.status}`);
      const data = new Uint8Array(await r.arrayBuffer());
      if (data.length > MEM_SLOT_DATA_SIZE) {
        throw new Error(`image is ${data.length}B, larger than a slot's ${MEM_SLOT_DATA_SIZE}B data region`);
      }
      info.textContent = `Step 1/4 — downloaded ${data.length} bytes, target 0x${profile.flashBase.toString(16)}.`;

      const blocksNeeded = Math.ceil(data.length / 0x10000);
      info.textContent = `Step 2/4 — erasing slot ${slot} (1 meta block + ${blocksNeeded} data block(s))…`;
      await memErase(slotMetaAddr(slot) >>> 16);
      for (let i = 0; i < blocksNeeded; i++) await memErase((slotDataAddr(slot) >>> 16) + i);
      info.textContent = `Step 2/4 — erased ${1 + blocksNeeded} block(s).`;

      info.textContent = `Step 3/4 — transferring ${data.length} bytes over BLE…`;
      const ok = await runMemUpload(btn, slotDataAddr(slot), data, {
        wrap: 'fwProgressWrap', bar: 'fwProgressBar', text: 'fwProgressText',
        log: 'fwFragLog', cancelBtn: 'fwCancelBtn',
      });
      if (!ok) throw new Error('transfer did not complete — slot metadata not written');

      // CRC16/ARC over the exact bytes just confirmed written (per-fragment
      // ×170771 checks during the transfer above already guarantee those
      // landed correctly) - this is the DIFFERENT checksum the slot meta
      // record itself stores and SlotsScreen's own crcOk()/PGN110 broadcast
      // check against, not a re-verification of the transfer.
      const crc = crc16Modbus(data);
      info.textContent = `Step 4/4 — writing slot metadata (target 0x${profile.flashBase.toString(16)}, `
        + `${data.length}B, CRC16 0x${crc.toString(16)}, v${version})…`;
      await writeMemoryFragment(slotMetaAddr(slot), buildSlotMeta(profile.flashBase, data.length, crc, version));

      info.textContent = `Done — slot ${slot} now holds ${version} (${data.length}B, target `
        + `0x${profile.flashBase.toString(16)}). Ready to relay via SlotsScreen's "Burn" on the panel.`;
    } catch (e) {
      info.textContent = 'Failed: ' + e.message;
    } finally {
      btn.disabled = false;
      releaseWakeLock();
    }
  };

  document.getElementById('fwSlotSelect').onchange = () => {
    // Convenience: keep the manual-address field in sync so switching between
    // "fetch from server" and "upload local file" against the same slot
    // doesn't require recomputing the hex address by hand.
    const slot = parseInt(document.getElementById('fwSlotSelect').value, 10);
    document.getElementById('memUploadAddr').value = '0x' + slotDataAddr(slot).toString(16);
  };
}

/* The day/night start times don't track pending — same simplification as
   the rest of the "flat" WORK fields, refreshed straight from telemetry on
   every render (unless the field is currently focused for editing). */
function renderSchedule() {
  const pad = (n) => String(n).padStart(2, '0');
  const dayEl = document.getElementById('dayStart');
  const nightEl = document.getElementById('nightStart');
  if (document.activeElement !== dayEl) dayEl.value = `${pad(state.dayStartHour)}:${pad(state.dayStartMinute)}`;
  if (document.activeElement !== nightEl) nightEl.value = `${pad(state.nightStartHour)}:${pad(state.nightStartMinute)}`;
}

/* ── Page bootstrap ────────────────────────────────────────────────────── */
(function init() {
  if (!navigator.bluetooth) {
    document.getElementById('unsupportedBox').classList.remove('hidden');
    document.getElementById('app').classList.add('hidden');
    return;
  }
  wireStaticControls();
  wireMemoryControls();
  renderKnownDevices();
  render();
  tryAutoReconnect();
})();
