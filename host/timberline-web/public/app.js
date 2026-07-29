/* Each icon carries its own viewBox + fill mode, since flame/engine are
   filled artwork sourced from svgrepo.com (user-supplied files) with their
   own native coordinate systems, while coil/floor are small hand-drawn
   line icons sharing a 0-24 viewBox and stroke rendering. renderIconRow()
   builds the <svg> wrapper per-icon accordingly (see below). */
const ICONS = {
  /* fire-svgrepo-com.svg — path's own hardcoded fill="#000000" stripped so
     it inherits the wrapper's fill="currentColor" (on/off color state). */
  flame: {
    viewBox: '0 0 24 24', fill: true,
    path: '<path d="M5.926 20.574a7.26 7.26 0 0 0 3.039 1.511c.107.035.179-.105.107-.175-2.395-2.285-1.079-4.758-.107-5.873.693-.796 1.68-2.107 1.608-3.865 0-.176.18-.317.322-.211 1.359.703 2.288 2.25 2.538 3.515.394-.386.537-.984.537-1.511 0-.176.214-.317.393-.176 1.287 1.16 3.503 5.097-.072 8.19-.071.071 0 .212.072.177a8.761 8.761 0 0 0 3.003-1.442c5.827-4.5 2.037-12.48-.43-15.116-.321-.317-.893-.106-.893.351-.036.95-.322 2.004-1.072 2.707-.572-2.39-2.478-5.105-5.195-6.441-.357-.176-.786.105-.75.492.07 3.27-2.063 5.352-3.922 8.059-1.645 2.425-2.717 6.89.822 9.808z"/>',
  },
  /* Lightning bolt — symbolizes electricity for the element/ТЭН button. */
  bolt: {
    viewBox: '0 0 24 24', fill: true,
    path: '<path d="M13 2L4 14H10L9 22L20 9H13L13 2Z"/>',
  },
  floor: {
    viewBox: '0 0 24 24', fill: false,
    path: '<path d="M3 19h18"/><path d="M7 16c1-1 1-2 0-3s-1-2 0-3"/><path d="M12 16c1-1 1-2 0-3s-1-2 0-3"/><path d="M17 16c1-1 1-2 0-3s-1-2 0-3"/>',
  },
  /* engine-motor-svgrepo-com.svg — native viewBox kept as-is (SVG scales
     the coordinate system to whatever size the wrapper renders at). */
  engine: {
    viewBox: '0 0 511.999 511.999', fill: true,
    path: '<path d="M494.32,196.801l-4.858-8.131h-85.516v39.564h-8.557v-57.371h-44.32l-28.138-24.95h-46.53v-22.695h14.966V89.827H172.742v33.391h14.966v22.695h-48.443l-28.138,24.95H55.791v16.696v66.236h-22.4v-42.616H0v118.625h33.391v-42.617h22.4v66.236v16.696h83.474l58.709,52.054h197.414v-58.444h8.557v39.565h85.516l4.858-8.132c1.81-3.027,17.68-31.537,17.68-99.181S496.13,199.829,494.32,196.801z M221.101,123.22h21.909v22.695h-21.909V123.22z M468.927,369.902h-31.59v-39.565h-75.34v58.444H210.646l-58.709-52.054H89.183V204.255h34.617l28.138-24.95h158.32l28.138,24.95h23.601v57.371h75.34v-39.564h31.59c3.873,11.386,9.681,34.956,9.681,73.921C478.609,334.947,472.801,358.516,468.927,369.902z"/>',
  },
};

/* First row: icon toggle buttons. Floor/Engine carry a connectedKey — not
   every physical system has that hardware, so they stay hidden until the
   modem confirms presence via the matching "*Connected" status topic (see
   mqttActualizerHandler in Timberline.cpp). Heater/Element are always
   present, no gating. */
const ICON_BUTTONS = [
  { name: 'btnHtr', label: 'Heater', icon: ICONS.flame },
  { name: 'btnElement', label: 'Element', icon: ICONS.bolt },
  { name: 'btnFloor', label: 'Floor', icon: ICONS.floor, connectedKey: 'floorConnected' },
  { name: 'btnEngine', label: 'Engine', icon: ICONS.engine, connectedKey: 'engineConnected' },
];

/* Everything else, rendered the old label/ON-off way below the icon row. */
const OTHER_BUTTONS = [
  { name: 'btnEco', label: 'Eco' },
];

const ZONE_COUNT = 5;

/* The 3 "drums" under the zone row — all operate on whichever zone is
   currently selected (see selectedZone). Order matches what was asked for:
   day setpoint, fan speed, night setpoint. min/max/step are UI-side
   sanity bounds, not enforced by the firmware — it accepts whatever's
   published to cmd/desired/zn<N>/<key>. */
const DRUMS = [
  { key: 'daySp', label: 'Day', min: 10, max: 32, step: 1, unit: '°' },
  { key: 'fanPct', label: 'Fan', min: 10, max: 100, step: 1, unit: '%', isFan: true },
  { key: 'nightSp', label: 'Night', min: 10, max: 32, step: 1, unit: '°' },
];

/* Floor/engine settings, tucked behind the two <details> "spoilers" in
   index.html — global (not per-zone) values, gated on the same
   floorConnected/engineConnected hardware-presence flags as the icon
   buttons (see ICON_BUTTONS). Ranges mirror the firmware's own validation
   in Timberline.cpp's onMqttCommandReceived() — the slider can't offer
   anything the modem would reject anyway. */
const FLOOR_SETTINGS = [
  { key: 'floorSp', label: 'Setpoint', min: 2, max: 32, step: 1, unit: '°' },
  { key: 'floorHyst', label: 'Hysteresis', min: 2, max: 10, step: 1, unit: '°' },
];
/* engineDur: >1440 (24h) reads as "Unlimited" — same convention the
   firmware itself already uses for SystemTimeLimitHours (see Timberline.h).
   1450 is the one slider stop above that line (10..1450 step 10 lands on
   1440 exactly, then one more step to 1450). */
const ENGINE_SETTINGS = [
  { key: 'engineSp', label: 'Setpoint', min: 0, max: 80, step: 1, unit: '°' },
  { key: 'engineDur', label: 'Run time', min: 10, max: 1450, step: 10, unit: ' min',
    format: (v) => (Number(v) > 1440 ? 'Unlimited' : `${v} min`) },
];

let mqttClient = null;
let mqttUsername = null;
let selectedZone = null;  /* unknown until zn<N>/connected data arrives */
let telemetry = null;     /* decoded "telemetry" blob, see decodeTelemetry() */
const buttonState = {};
const connectedState = {};
const rawStatus = {};

/* Mirrors the 20-byte packed struct built in Timberline::mqttTelemetryHandler
   (modem/User/Timberline.cpp) — byte-for-byte, keep the two in sync. */
function decodeTelemetry(b64) {
  try {
    const bin = atob(b64);
    if (bin.length < 20) return null;
    const raw = new Uint8Array(bin.length);
    for (let i = 0; i < bin.length; i++) raw[i] = bin.charCodeAt(i);
    const i8 = (v) => (v > 127 ? v - 256 : v);

    const flags = raw[5];
    const flags2 = raw[19];
    return {
      tankTemp: i8(raw[0]),
      heaterTemp: i8(raw[1]),
      voltage: ((raw[2] << 8) | raw[3]) / 10,
      outdoorTemp: i8(raw[4]),
      heaterIcon: flags & 0x07,
      elementState: !!((flags >> 3) & 1),
      domesticWaterFlow: !!((flags >> 4) & 1),
      liquidLevel: (flags >> 5) & 0x07,
      pumps: Array.from({ length: 8 }, (_, i) => !!((raw[6] >> i) & 1)),
      zoneFanPwm: Array.from({ length: ZONE_COUNT }, (_, i) => raw[7 + i]),
      zoneCurrentTemp: Array.from({ length: ZONE_COUNT }, (_, i) => i8(raw[12 + i])),
      floorTemp: i8(raw[17]),
      engineTemp: i8(raw[18]),
      floorPumpState: !!(flags2 & 1),
      enginePumpState: !!((flags2 >> 1) & 1),
    };
  } catch (e) {
    console.error('bad telemetry payload', e);
    return null;
  }
}

function $(id) { return document.getElementById(id); }

function showBox(id) {
  ['authBox', 'registerBox', 'smsBox', 'controlBox'].forEach(b => $(b).classList.toggle('hidden', b !== id));
}

function publishToggle(name, isOn) {
  const next = isOn ? '0' : '1';
  mqttClient.publish(`${mqttUsername}/cmd/desired/${name}`, next);
}

function renderIconRow() {
  const container = $('iconRow');
  container.innerHTML = '';
  ICON_BUTTONS.forEach(b => {
    if (b.connectedKey && connectedState[b.connectedKey] !== '1') return;

    const isOn = buttonState[b.name] === '1';
    const btn = document.createElement('button');
    btn.className = 'icon-btn' + (isOn ? ' on' : '');
    const svgAttrs = b.icon.fill
      ? 'fill="currentColor" stroke="none"'
      : 'fill="none" stroke="currentColor" stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round"';
    btn.innerHTML = `<svg viewBox="${b.icon.viewBox}" ${svgAttrs}>${b.icon.path}</svg><span class="icon-label">${b.label}</span>`;
    btn.onclick = () => publishToggle(b.name, isOn);
    container.appendChild(btn);
  });
}

function renderOtherButtons() {
  const container = $('buttons');
  container.innerHTML = '';
  OTHER_BUTTONS.forEach(b => {
    const row = document.createElement('div');
    row.className = 'btn-row';

    const label = document.createElement('span');
    label.textContent = b.label;

    const isOn = buttonState[b.name] === '1';
    const btn = document.createElement('button');
    btn.className = 'switch' + (isOn ? ' on' : '');
    btn.innerHTML = '<span class="switch-thumb"></span>';
    btn.onclick = () => publishToggle(b.name, isOn);

    row.appendChild(label);
    row.appendChild(btn);
    container.appendChild(row);
  });
}

function renderButtons() {
  renderIconRow();
  renderZoneRow();
  renderDrums();
  renderSettings();
  renderOtherButtons();
}

function publishValue(name, value) {
  mqttClient.publish(`${mqttUsername}/cmd/desired/${name}`, String(value));
}

/* zn<N>/connected mirrors Timberline::zoneConnected: 0 = not connected,
   1 = dependent heater (has a fan), 2 = defrost (always-on, not
   user-controllable), 3 = radiator (no fan). 1 and 3 are shown — matches
   the same filter the firmware's own "status" SMS reply uses. Default (no
   data yet) is "not shown", same pattern as floorConnected/engineConnected. */
function connectedZones() {
  const zones = [];
  for (let z = 1; z <= ZONE_COUNT; z++) {
    const c = rawStatus[`zn${z}/connected`];
    if (c === '1' || c === '3') zones.push(z);
  }
  return zones;
}

const ZONE_STATE_LABELS = { off: 'Off', heat: 'Heat', vent: 'Vent' };

/* Zone state (off/heat/vent) is a separate concept from "selected" (which
   zone the drums below operate on) — state now owns the card's background
   color, so "selected" moved to an outline (see CSS) instead of a solid
   color, and state itself is changed via a small "⋮" menu rather than the
   card's main click target (which still just selects the zone for the
   drums, unchanged). Only one menu open at a time; a single top-level
   document click listener (see bottom of file) closes it on any outside
   click, since renderZoneRow() rebuilds these elements from scratch on
   every render and can't rely on a per-element blur/focusout. */
let openZoneMenu = null;  /* zone number whose "⋮" menu is open, or null */

function renderZoneRow() {
  const container = $('zoneRow');
  container.innerHTML = '';
  const zones = connectedZones();
  if (!zones.includes(selectedZone)) selectedZone = zones[0] || null;

  zones.forEach((z) => {
    const temp = telemetry ? telemetry.zoneCurrentTemp[z - 1] : undefined;
    const zoneType = rawStatus[`zn${z}/connected`];
    const state = rawStatus[`zn${z}/state`];
    const stateOptions = zoneType === '3' ? ['off', 'heat'] : ['off', 'heat', 'vent'];

    const card = document.createElement('div');
    card.className = 'zone-btn' + (state ? ` state-${state}` : '') + (z === selectedZone ? ' selected' : '');
    card.onclick = () => { selectedZone = z; openZoneMenu = null; renderZoneRow(); renderDrums(); };

    const info = document.createElement('div');
    info.innerHTML = `<span class="zone-temp">${temp !== undefined ? temp + '°' : '–'}</span>`;
    card.appendChild(info);

    const menuBtn = document.createElement('button');
    menuBtn.className = 'zone-menu-btn';
    menuBtn.textContent = '⋮';
    menuBtn.onclick = (e) => {
      e.stopPropagation();
      openZoneMenu = (openZoneMenu === z) ? null : z;
      renderZoneRow();
    };
    card.appendChild(menuBtn);

    if (openZoneMenu === z) {
      const menu = document.createElement('div');
      menu.className = 'zone-menu';
      stateOptions.forEach((opt) => {
        const item = document.createElement('button');
        item.className = 'zone-menu-item' + (state === opt ? ' active' : '');
        item.textContent = ZONE_STATE_LABELS[opt];
        item.onclick = (e) => {
          e.stopPropagation();
          mqttClient.publish(`${mqttUsername}/cmd/desired/zn${z}/state`, opt);
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

/* Count of drums currently mid-gesture (dragged, coasting on inertia, or
   easing into their snap) — a counter, not a bool, so two concurrent
   interactions (e.g. two-finger touch on two different drums) can't have
   one finish() clear protection the other still needs. renderButtons()
   gets called on *every* incoming MQTT message (a telemetry tick alone
   fires every 15s), and this whole row is normally torn down and rebuilt
   from scratch each time — without this guard, an unrelated message would
   yank a reel out from under a spin in progress. Decremented once a
   gesture fully settles (see finish() below), at which point the next
   natural render (or the explicit one finish() triggers) picks up reality
   again. */
let drumBusyCount = 0;

const REEL_ROW_H = 34;   /* px per value step */
const REEL_VISIBLE = 3;  /* odd — shows 1 row of context above/below the center */

/* 3 spinning "drums" for the selected zone: drag/flick up-down for inertia
   (real momentum + friction, then eases into the nearest valid value —
   see runInertia()/snap() below), or scroll the wheel to step by one. The
   fan drum additionally responds to a long-press (~600ms, anywhere on the
   card — see wireLongPress()) by toggling zn<N>/fanManual; while in auto
   mode it shows the word "auto" instead of a reel and ignores drag/scroll
   (there's no percentage to spin to). A zone with connected==3 (radiator)
   has no fan hardware at all, so the fan drum is left out entirely for it —
   not just disabled. */
function renderDrums() {
  if (drumBusyCount > 0) return;
  const container = $('drumRow');
  container.innerHTML = '';

  const zoneType = selectedZone ? rawStatus[`zn${selectedZone}/connected`] : null;
  const hasFan = zoneType !== '3';
  const drums = DRUMS.filter((d) => !d.isFan || hasFan);

  drums.forEach((d) => container.appendChild(buildDrum(d)));
}

/* Floor/engine settings rows — built once (not torn down and rebuilt on
   every message like the icon/zone/drum rows above), because these are
   plain native <input type=range>: recreating the element mid-drag would
   yank it out from under the user's finger the way it would for the
   custom drum widget. renderSettings() below only ever toggles visibility
   and nudges .value from live data, skipping whichever slider the user is
   actively touching right now. */
let settingsRowsBuilt = false;
function buildSettingsRow(groupId, s) {
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
  input.min = String(s.min);
  input.max = String(s.max);
  input.step = String(s.step);
  input.addEventListener('input', () => { value.textContent = s.format ? s.format(input.value) : input.value + s.unit; });
  input.addEventListener('change', () => publishValue(s.key, input.value));

  row.appendChild(label);
  row.appendChild(input);
  return row;
}

function buildSettingsPanels() {
  if (settingsRowsBuilt) return;
  settingsRowsBuilt = true;
  FLOOR_SETTINGS.forEach((s) => $('floorSettings').appendChild(buildSettingsRow('floorSettings', s)));
  ENGINE_SETTINGS.forEach((s) => $('engineSettings').appendChild(buildSettingsRow('engineSettings', s)));
}

function updateSettingsGroup(groupId, connectedKey, settings) {
  const details = $(groupId);
  const visible = connectedState[connectedKey] === '1';
  details.classList.toggle('hidden', !visible);
  if (!visible) return;

  settings.forEach((s) => {
    const raw = rawStatus[s.key];
    if (raw === undefined) return;
    const input = $(`${groupId}-${s.key}`);
    if (document.activeElement === input) return; /* mid-drag — don't fight the user */
    if (input.value !== raw) {
      input.value = raw;
      $(`${groupId}-${s.key}-value`).textContent = s.format ? s.format(raw) : raw + s.unit;
    }
  });
}

function renderSettings() {
  buildSettingsPanels();
  updateSettingsGroup('floorSettings', 'floorConnected', FLOOR_SETTINGS);
  updateSettingsGroup('engineSettings', 'engineConnected', ENGINE_SETTINGS);
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
  const topicName = selectedZone ? `zn${selectedZone}/${d.key}` : null;
  const manualTopic = selectedZone ? `zn${selectedZone}/fanManual` : null;
  const raw = topicName ? rawStatus[topicName] : undefined;
  const isAuto = !!d.isFan && !!selectedZone && rawStatus[manualTopic] !== '1';

  const drum = document.createElement('div');
  drum.className = 'drum' + (d.isFan && selectedZone ? (isAuto ? ' auto' : ' manual') : '');

  const label = document.createElement('div');
  label.className = 'drum-label';
  label.textContent = d.label;

  const toggleFanManual = () => {
    if (!(d.isFan && selectedZone)) return;
    mqttClient.publish(`${mqttUsername}/cmd/desired/${manualTopic}`, isAuto ? '1' : '0');
  };

  /* Long-press to flip auto/manual is wired on the whole card, not just the
     small "auto" label or the reel itself — a much bigger, easier target,
     and it covers both the static (auto) and reel (manual) presentations
     with one listener instead of two separate ones. */
  if (d.isFan && selectedZone) wireLongPress(drum, toggleFanManual);

  if (isAuto || raw === undefined) {
    /* Nothing to spin — either genuinely in auto mode, or we don't know
       the current value yet (e.g. right after connecting). */
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
  mask.className = 'drum-reel-mask';
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

  /* offset(index) = px translateY putting row `index` exactly in the
     center slot; total on-screen offset is always offset(baseIndex) plus
     whatever the live drag has added on top. */
  const indexOffset = (idx) => ((REEL_VISIBLE - 1) / 2) * REEL_ROW_H - idx * REEL_ROW_H;
  let dragOffset = 0;

  /* Soft stop past the first/last value — dragging or flinging beyond the
     range compresses instead of scrolling into blank space, like a real
     drum hitting a physical end-stop. Applied to dragOffset itself (not
     just the paint) so there's only one source of truth: snap()'s own
     total/idx math sees the same compressed position everything else does. */
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

  /* This drum's own contribution to the shared drumBusyCount, tracked
     separately so re-grabbing the reel mid-inertia/mid-snap (before
     finish() ever ran for the previous release) can't double-increment —
     markBusy()/markIdle() are idempotent from this drum's point of view. */
  let iAmBusy = false;
  const markBusy = () => { if (!iAmBusy) { iAmBusy = true; drumBusyCount++; } };
  const markIdle = () => { if (iAmBusy) { iAmBusy = false; drumBusyCount = Math.max(0, drumBusyCount - 1); } };

  function runInertia() {
    const FRICTION = 0.95; /* multiplier per ~16.7ms frame */
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
      const t = Math.min(1, (now - startTime) / DURATION);
      const eased = 1 - Math.pow(1 - t, 3);
      reel.style.transform = `translateY(${startOffset + (target - startOffset) * eased}px)`;
      if (t < 1) { rafId = requestAnimationFrame(ease); }
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
      publishValue(topicName, newVal);
      /* Mirror the value locally before reconciling — rawStatus[topicName]
         still holds the pre-drag value at this instant (the device's own
         "actual" echo hasn't arrived yet), so without this the very next
         line rebuilds the drum from stale data and it visibly snaps back
         to the old value for a moment before the echo corrects it again. */
      rawStatus[topicName] = String(newVal);
    }
    /* Reconcile with reality now, in case nothing else happens to arrive
       and re-render soon (e.g. the command silently fails). */
    renderDrums();
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

/* Matches Timberline.h's heaterStateIcon_t: 0 idle, 1 blowing,
   2 ignition/warming (one combined state), 3 work on power. */
const HEATER_ICON_LABELS = ['Idle', 'Blowing', 'Ignition/warming', 'Work on power'];
const PUMP_LABELS = ['Pump 1', 'Pump 2', 'Pump 3', 'Pump 4', 'Heater pump', 'Aux pump 1', 'Aux pump 2', 'Aux pump 3'];

function statusRow(label, value) {
  return `<tr><td>${label}</td><td>${value}</td></tr>`;
}

/* Human-readable summary built from the decoded "telemetry" blob (see
   decodeTelemetry()) plus the "errors" CSV topic — everything else here
   (buttons, zones, drums) already has its own UI; this is just the fields
   that don't fit anywhere else. */
function renderStatusTable() {
  const table = $('statusTable');
  if (!telemetry) { table.innerHTML = ''; return; }

  const t = telemetry;
  const onPumps = PUMP_LABELS.filter((_, i) => t.pumps[i]);
  const errors = rawStatus.errors;

  let rows = '';
  rows += statusRow('Tank temp', `${t.tankTemp}°`);
  rows += statusRow('Heater temp', `${t.heaterTemp}°`);
  rows += statusRow('Voltage', `${t.voltage.toFixed(1)}V`);
  rows += statusRow('Outdoor temp', `${t.outdoorTemp}°`);
  rows += statusRow('Heater state', HEATER_ICON_LABELS[t.heaterIcon] || '?');
  rows += statusRow('Element', t.elementState ? 'ON' : 'off');
  rows += statusRow('Domestic water', t.domesticWaterFlow ? 'flowing' : 'off');
  rows += statusRow('Liquid level', `${t.liquidLevel}/6`);
  rows += statusRow('Pumps running', onPumps.length ? onPumps.join(', ') : 'none');
  /* Floor/engine are optional hardware — only show once we know it's
     actually installed (same connectedState gate the icon row uses). */
  if (connectedState.floorConnected === '1') {
    rows += statusRow('Floor temp', `${t.floorTemp}°`);
    rows += statusRow('Floor pump', t.floorPumpState ? 'ON' : 'off');
  }
  if (connectedState.engineConnected === '1') {
    rows += statusRow('Engine temp', `${t.engineTemp}°`);
    rows += statusRow('Engine pump', t.enginePumpState ? 'ON' : 'off');
  }
  if (errors && errors !== '0') {
    rows += `<tr class="errors-row"><td colspan="2">Errors: ${errors}</td></tr>`;
  }
  table.innerHTML = rows;
}

function renderStatus() {
  $('statusPre').textContent = JSON.stringify(rawStatus, null, 2);
  renderStatusTable();
}

/* Visible, at-a-glance proof that *this browser* actually has a live MQTT
   session — separate from whether the modem itself is connected. The user
   hit a real case where the modem was up but their phone's tab silently
   wasn't (page backgrounded, wifi hiccup, whatever) with nothing on
   screen to reveal that. green = connected, amber/pulsing = connecting or
   reconnecting, red = disconnected. */
function setConnStatus(state, label) {
  const dot = $('connDot');
  if (!dot) return;
  dot.className = 'conn-dot' + (state ? ' ' + state : '');
  $('connLabel').textContent = label;
}

/* The modem's own presence, via the "online" topic — Last Will (published
   by the broker itself if the modem's session ever drops without a clean
   disconnect) plus an explicit "0" the modem sends on a graceful shutdown
   and "1" right after it connects (Modem.cpp doMqttStart/doMqttTeardown).
   A *different* signal than setConnStatus() above: that's this browser's
   own link to the broker, this is whether the device itself is there at
   all. Unknown (gray, "Device: —") until the first "online" message —
   which arrives immediately on subscribe if it was ever retained. */
function setDeviceStatus(online) {
  const dot = $('deviceDot');
  if (!dot) return;
  dot.className = 'conn-dot' + (online === null ? '' : online ? ' connected' : ' disconnected');
  $('deviceLabel').textContent = online === null ? 'Device: —' : online ? 'Device online' : 'Device offline';
}

function connectMqtt(creds) {
  mqttUsername = creds.mqttUsername;
  /* A page loaded over https can't open a plain ws:// connection — the
     browser blocks it as mixed content. Match the page's own scheme. */
  const wsScheme = location.protocol === 'https:' ? 'wss' : 'ws';
  const url = `${wsScheme}://${creds.mqttHost}:${creds.mqttWsPort}`;
  setConnStatus('connecting', 'Broker connecting…');
  setDeviceStatus(null);
  mqttClient = mqtt.connect(url, {
    username: creds.mqttUsername,
    password: creds.mqttPassword,
    clientId: 'web-' + Math.random().toString(16).slice(2),
  });

  mqttClient.on('connect', () => {
    setConnStatus('connected', 'Broker connected');
    mqttClient.subscribe(`${mqttUsername}/cmd/actual/#`);
  });

  mqttClient.on('reconnect', () => setConnStatus('connecting', 'Broker reconnecting…'));
  mqttClient.on('close',     () => setConnStatus('disconnected', 'Broker disconnected'));
  mqttClient.on('offline',   () => setConnStatus('disconnected', 'Broker disconnected'));

  mqttClient.on('message', (topic, payload) => {
    const marker = '/cmd/actual/';
    const idx = topic.indexOf(marker);
    if (idx === -1) return;
    const name = topic.slice(idx + marker.length);
    const value = payload.toString();
    rawStatus[name] = value;
    if (ICON_BUTTONS.some(b => b.name === name) || OTHER_BUTTONS.some(b => b.name === name)) buttonState[name] = value;
    if (name === 'floorConnected' || name === 'engineConnected') connectedState[name] = value;
    if (name === 'telemetry') { telemetry = decodeTelemetry(value); rawStatus.telemetry_decoded = telemetry; }
    if (name === 'online') setDeviceStatus(value === '1');
    renderStatus();
    renderButtons();
  });

  mqttClient.on('error', (e) => { console.error('mqtt error', e); setConnStatus('disconnected', 'Broker disconnected'); });

  showBox('controlBox');
  renderButtons();
  renderStatus();
}

async function api(path, body) {
  const r = await fetch(path, {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify(body),
  });
  const data = await r.json();
  if (!r.ok) throw new Error(data.error || 'request failed');
  return data;
}

async function apiGet(path) {
  const r = await fetch(path);
  const data = await r.json();
  if (!r.ok) throw new Error(data.error || 'request failed');
  return data;
}

const sleep = (ms) => new Promise((resolve) => setTimeout(resolve, ms));

/* Magic link — "/go/<username>/<token>" (see the "getlink" SMS command on
   the modem). The page itself is served for this path too (so the URL is
   bookmarkable and reopening it keeps working), app.js just recognizes the
   pattern and redeems it instead of showing the login form. Never touches
   the real account password — see /api/go/:username/:token on the backend.

   Retries for a while before giving up: right after a "server X,login
   Y,password Z,getlink" combo SMS, the modem has to tear down its old MQTT
   session and reconnect under the new login before it can even publish the
   token the link depends on — that takes a few seconds, during which the
   very first visit to a freshly-received link legitimately 404s. */
async function tryMagicLink() {
  const m = location.pathname.match(/^\/go\/([^/]+)\/([^/]+)$/);
  if (!m) return false;
  const [, username, token] = m;

  const maxAttempts = 10;
  const retryDelayMs = 2000;
  for (let attempt = 1; attempt <= maxAttempts; attempt++) {
    try {
      const creds = await apiGet(`/api/go/${encodeURIComponent(username)}/${encodeURIComponent(token)}`);
      $('err').textContent = '';
      connectMqtt(creds);
      return true;
    } catch (e) {
      if (attempt === maxAttempts) {
        $('err').textContent = 'This link is invalid or expired — request a new one (send "getlink" via SMS).';
        return false;
      }
      $('err').textContent = 'Waiting for the device to finish connecting…';
      await sleep(retryDelayMs);
    }
  }
}

$('loginBtn').onclick = async () => {
  $('err').textContent = '';
  try {
    const creds = await api('/api/login', { login: $('loginLogin').value, password: $('loginPassword').value });
    connectMqtt(creds);
  } catch (e) {
    $('err').textContent = e.message;
  }
};

/* location.hostname (not a hardcoded IP) matches how the backend derives
   mqttHost when MQTT_HOST isn't set — broker and app are assumed co-located.
   See the "Portability" note in host/README.md. */
$('registerBtn').onclick = async () => {
  const login = $('regLogin').value;
  const password = $('regPassword').value;
  try {
    await api('/api/register', { login, password, email: $('regEmail').value || undefined });
    $('smsCommand').textContent = `server ${location.hostname},login ${login},password ${password},getlink`;
    $('loginLogin').value = login;
    showBox('smsBox');
  } catch (e) {
    alert(e.message);
  }
};

/* navigator.clipboard is only available in a "secure context" (HTTPS, or
   localhost as the one exemption) — this site is currently plain http:// on
   a bare IP (no TLS, deliberate MVP limitation, see host/README.md), so the
   Clipboard API is simply absent there and the old one-liner failed
   silently. Fall back to the classic textarea+execCommand trick, which
   still works without HTTPS. */
function copyText(text) {
  if (navigator.clipboard && window.isSecureContext) return navigator.clipboard.writeText(text);
  const ta = document.createElement('textarea');
  ta.value = text;
  ta.style.position = 'fixed';
  ta.style.left = '-9999px';
  document.body.appendChild(ta);
  ta.focus();
  ta.select();
  try {
    if (!document.execCommand('copy')) throw new Error('execCommand copy failed');
  } finally {
    document.body.removeChild(ta);
  }
  return Promise.resolve();
}

$('copySmsBtn').onclick = async () => {
  const btn = $('copySmsBtn');
  try {
    await copyText($('smsCommand').textContent);
    const original = btn.textContent;
    btn.textContent = 'Copied!';
    setTimeout(() => { btn.textContent = original; }, 1500);
  } catch (e) {
    alert('Copy failed — please select and copy the text manually.');
  }
};
$('smsContinueBtn').onclick = () => showBox('authBox');

$('showRegisterBtn').onclick = () => showBox('registerBox');
$('showLoginBtn').onclick = () => showBox('authBox');

$('logoutBtn').onclick = () => {
  if (mqttClient) mqttClient.end();
  mqttClient = null;
  showBox('authBox');
};

showBox('authBox');
tryMagicLink();
