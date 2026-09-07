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
  heaterVersion: null, panelVersion: null,
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
  logPacket('RX', ptype, b);

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
      state.heaterVersion = [b[1], b[2], b[3], b[4]].join('.');
      state.panelVersion = [b[9], b[10], b[11], b[12]].join('.');
      break;
    }
    case PACKET_TYPE.CONN_STATUS: {
      // One-off response to the panel confirmation: carries the shared device key.
      const key = b.slice(12, 20);
      onPaired(key);
      break;
    }
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

/* ── BLE connection ────────────────────────────────────────────────────── */
let ble = { device: null, server: null, txChar: null, rxChar: null };
let pairing = false;
let pairingTimer = null;
let writeChain = Promise.resolve();

function queueWrite(bytes) {
  writeChain = writeChain.then(() => doWrite(bytes)).catch((e) => console.error('BLE write error', e));
  return writeChain;
}
async function doWrite(bytes) {
  if (!ble.rxChar) return;
  logPacket('TX', bytes[0], bytes);
  try {
    if (ble.rxChar.writeValueWithoutResponse) await ble.rxChar.writeValueWithoutResponse(bytes);
    else await ble.rxChar.writeValue(bytes);
  } catch (e) {
    console.error('BLE write failed', e);
  }
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

/* ── Packet log (the collapsed "Packet log" panel) ────────────────────── */
const rawLogEntries = [];
function logPacket(dir, ptype, bytes) {
  const hex = Array.from(bytes, (x) => x.toString(16).padStart(2, '0')).join(' ');
  const time = new Date().toLocaleTimeString('en-GB', { hour12: false });
  rawLogEntries.push(`${time} ${dir === 'RX' ? '←' : '→'} [${ptype}] ${hex}`);
  if (rawLogEntries.length > 200) rawLogEntries.shift();
  const el = document.getElementById('rawLog');
  if (el) { el.textContent = rawLogEntries.slice().reverse().join('\n'); }
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
}

function render() {
  renderIconRow();
  renderZoneRow();
  renderDrumRow();
  renderSettings();
  renderSchedule();
  renderErrors();
  renderMisc();
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
  renderKnownDevices();
  render();
  tryAutoReconnect();
})();
