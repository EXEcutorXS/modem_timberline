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
  /* fan-svgrepo-com.svg — zn<N> zones with connected==1 (have a fan). Corner
     badge on the zone card, see renderZoneRow(); spins via CSS while the
     zone's live fan speed (parseZoneStatus()'s fanCurrent) is nonzero. */
  fan: {
    viewBox: '0 0 24 24', fill: true,
    path: '<path d="M12,11a1,1,0,1,0,1,1,1,1,0,0,0-1-1m.5-9C17,2,17.1,5.57,14.73,6.75a3.36,3.36,0,0,0-1.62,2.47,3.17,3.17,0,0,1,1.23.91C18,8.13,22,8.92,22,12.5c0,4.5-3.58,4.6-4.75,2.23a3.44,3.44,0,0,0-2.5-1.62,3.24,3.24,0,0,1-.91,1.23c2,3.69,1.2,7.66-2.38,7.66C7,22,6.89,18.42,9.26,17.24a3.46,3.46,0,0,0,1.62-2.45,3,3,0,0,1-1.25-.92C5.94,15.85,2,15.07,2,11.5,2,7,5.54,6.89,6.72,9.26A3.39,3.39,0,0,0,9.2,10.87a2.91,2.91,0,0,1,.92-1.22C8.13,6,8.92,2,12.48,2Z"/>',
  },
  /* radiator-svgrepo-com.svg — zn<N>/connected==3 zones (radiator, no fan).
     Native fill="#000000" stripped so it inherits currentColor like the
     other filled icons. */
  radiator: {
    viewBox: '0 0 491.6 491.6', fill: true,
    path: '<path d="M153.6,0H92.2C80.9,0,71.7,9.2,71.7,20.5V41H30.8c-11.3,0-20.5,9.2-20.5,20.5v61.4c0,11.3,9.2,20.5,20.5,20.5h41v204.8h-41c-11.3,0-20.5,9.2-20.5,20.5v61.4c0,11.3,9.2,20.5,20.5,20.5h41v20.5c0,11.3,9.2,20.5,20.5,20.5h61.4c11.3,0,20.5-9.2,20.5-20.5V20.5C174.1,9.1,165,0,153.6,0z M71.7,102.4H51.2V81.9h20.5V102.4z M71.7,409.6H51.2v-20.5h20.5V409.6z"/><path d="M276.5,0h-61.4c-11.3,0-20.5,9.2-20.5,20.5v450.6c0,11.3,9.2,20.5,20.5,20.5h61.4c11.3,0,20.5-9.2,20.5-20.5V20.5C297,9.1,287.8,0,276.5,0z"/><path d="M460.8,143.3c11.3,0,20.5-9.2,20.5-20.5V61.4c0-11.3-9.2-20.5-20.5-20.5h-41V20.5c0-11.3-9.2-20.5-20.5-20.5h-61.4c-11.3,0-20.5,9.2-20.5,20.5v450.6c0,11.3,9.2,20.5,20.5,20.5h61.4c11.3,0,20.5-9.2,20.5-20.5v-20.5h41c11.3,0,20.5-9.2,20.5-20.5v-61.4c0-11.3-9.2-20.5-20.5-20.5h-41V143.3H460.8z M419.9,81.9h20.5v20.5h-20.5V81.9z M419.9,389.1h20.5v20.5h-20.5V389.1z"/>',
  },
};

/* UI translations. Keys are referenced from the various *_BUTTONS/*_SETTINGS
   arrays below (their `label` field holds an i18n key, not display text)
   plus a handful of static elements translated directly by id in
   applyStaticTranslations(). Units (°, %, min, s) and the brand name
   "Timberline" are deliberately left untranslated — universal/a proper noun.
   Server-generated error messages (e.g. failed login) aren't covered here
   either — that would need i18n on the backend too, out of scope for now. */
const I18N = {
  en: {
    heater: 'Heater', element: 'Element', floor: 'Floor', engine: 'Engine', eco: 'Eco',
    day: 'Day', fan: 'Fan', night: 'Night',
    floorHeating: 'Floor heating', engineHeater: 'Engine heater', misc: 'Misc', raw: 'Raw',
    setpoint: 'Setpoint', hysteresis: 'Hysteresis', runTime: 'Run time', unlimited: 'Unlimited',
    telemetryInterval: 'Telemetry interval', language: 'Language',
    theme: 'Theme', themeSystem: 'System', themeLight: 'Light', themeDark: 'Dark',
    off: 'Off', heat: 'Heat', vent: 'Vent', auto: 'auto',
    logIn: 'Log in', register: 'Register', createAccount: 'Create account', backToLogin: 'Back to login',
    logOut: 'Log out', copy: 'Copy', copied: 'Copied!', continueToLogin: 'Continue to login',
    loginPlaceholder: 'login', passwordPlaceholder: 'password',
    regLoginPlaceholder: 'login (this is also your MQTT username)', emailPlaceholder: 'email (optional)',
    connConnecting: 'Broker connecting…', connConnected: 'Broker connected',
    connReconnecting: 'Broker reconnecting…', connDisconnected: 'Broker disconnected',
    deviceUnknown: 'Device: —', deviceOnline: 'Device online', deviceOffline: 'Device offline',
    linkInvalid: 'This link is invalid or expired — request a new one (send "getlink" via SMS).',
    waitingDevice: 'Waiting for the device to finish connecting…',
    otaDevices: 'Devices', otaVersion: 'Version', otaUpdate: 'Load',
    otaNoVersions: 'No versions published', otaStatusStaging: 'Loading…',
    otaStatusError: 'Load failed', otaEmpty: 'Empty', otaLoadedPrefix: 'Loaded',
    otaNoDevicesFound: 'No devices seen on the bus yet', deviceTypePrefix: 'Type',
    ownFirmware: 'Modem firmware',
    deviceAddressLabel: 'Address', deviceVersionLabel: 'version', modemVersionLabel: 'Modem version',
    canRelayFlash: 'Flash device', canRelayStatusIdle: 'Not flashed yet', canRelayStatusStaging: 'Flashing…',
    canRelayStatusDone: 'Device up to date', canRelayStatusError: 'Flashing failed',
    tankTemp: 'Tank temp', heaterTemp: 'Heater temp', voltage: 'Voltage', outdoorTemp: 'Outdoor temp',
    heaterState: 'Heater state', domesticWater: 'Domestic water', liquidLevel: 'Liquid level',
    pumpsRunning: 'Pumps running', floorTemp: 'Floor temp', floorPump: 'Floor pump',
    engineTemp: 'Engine temp', enginePump: 'Engine pump', errorsLabel: 'Errors',
    onState: 'ON', offState: 'off', flowing: 'flowing', noneLabel: 'none',
    heaterIdle: 'Idle', heaterBlowing: 'Blowing', heaterIgnition: 'Ignition/warming', heaterWorkOnPower: 'Work on power',
    pump1: 'Pump 1', pump2: 'Pump 2', pump3: 'Pump 3', pump4: 'Pump 4',
    heaterPump: 'Heater pump', auxPump1: 'Aux pump 1', auxPump2: 'Aux pump 2', auxPump3: 'Aux pump 3',
  },
  ru: {
    heater: 'Котёл', element: 'ТЭН', floor: 'Пол', engine: 'Двигатель', eco: 'Эко',
    day: 'День', fan: 'Вентилятор', night: 'Ночь',
    floorHeating: 'Тёплый пол', engineHeater: 'Подогрев двигателя', misc: 'Разное', raw: 'Сырые данные',
    setpoint: 'Уставка', hysteresis: 'Гистерезис', runTime: 'Время работы', unlimited: 'Без ограничения',
    telemetryInterval: 'Интервал телеметрии', language: 'Язык',
    theme: 'Тема', themeSystem: 'Системная', themeLight: 'Светлая', themeDark: 'Тёмная',
    off: 'Выкл', heat: 'Нагрев', vent: 'Вентиляция', auto: 'авто',
    logIn: 'Войти', register: 'Регистрация', createAccount: 'Создать аккаунт', backToLogin: 'Назад ко входу',
    logOut: 'Выйти', copy: 'Копировать', copied: 'Скопировано!', continueToLogin: 'Перейти ко входу',
    loginPlaceholder: 'логин', passwordPlaceholder: 'пароль',
    regLoginPlaceholder: 'логин (он же ваш MQTT-логин)', emailPlaceholder: 'email (необязательно)',
    connConnecting: 'Подключение к брокеру…', connConnected: 'Брокер подключён',
    connReconnecting: 'Переподключение к брокеру…', connDisconnected: 'Брокер отключён',
    deviceUnknown: 'Устройство: —', deviceOnline: 'Устройство онлайн', deviceOffline: 'Устройство офлайн',
    linkInvalid: 'Ссылка недействительна или устарела — запросите новую (отправьте SMS "getlink").',
    waitingDevice: 'Ожидание подключения устройства…',
    otaDevices: 'Устройства', otaVersion: 'Версия', otaUpdate: 'Загрузить',
    otaNoVersions: 'Нет опубликованных версий', otaStatusStaging: 'Загрузка…',
    otaStatusError: 'Ошибка загрузки', otaEmpty: 'Пусто', otaLoadedPrefix: 'Загружено',
    otaNoDevicesFound: 'На шине пока не видно устройств', deviceTypePrefix: 'Тип',
    ownFirmware: 'Прошивка модема',
    deviceAddressLabel: 'Адрес', deviceVersionLabel: 'версия', modemVersionLabel: 'Версия модема',
    canRelayFlash: 'Прошить устройство', canRelayStatusIdle: 'Ещё не прошито', canRelayStatusStaging: 'Прошивка…',
    canRelayStatusDone: 'Устройство актуально', canRelayStatusError: 'Ошибка прошивки',
    tankTemp: 'Температура бака', heaterTemp: 'Температура котла', voltage: 'Напряжение', outdoorTemp: 'Уличная температура',
    heaterState: 'Состояние котла', domesticWater: 'Горячая вода', liquidLevel: 'Уровень жидкости',
    pumpsRunning: 'Работают насосы', floorTemp: 'Температура пола', floorPump: 'Насос пола',
    engineTemp: 'Температура двигателя', enginePump: 'Насос двигателя', errorsLabel: 'Ошибки',
    onState: 'ВКЛ', offState: 'выкл', flowing: 'есть проток', noneLabel: 'нет',
    heaterIdle: 'Ожидание', heaterBlowing: 'Обдув', heaterIgnition: 'Розжиг/прогрев', heaterWorkOnPower: 'Работа на мощности',
    pump1: 'Насос 1', pump2: 'Насос 2', pump3: 'Насос 3', pump4: 'Насос 4',
    heaterPump: 'Насос котла', auxPump1: 'Доп. насос 1', auxPump2: 'Доп. насос 2', auxPump3: 'Доп. насос 3',
  },
  de: {
    heater: 'Heizung', element: 'Heizelement', floor: 'Fußboden', engine: 'Motor', eco: 'Eco',
    day: 'Tag', fan: 'Lüfter', night: 'Nacht',
    floorHeating: 'Fußbodenheizung', engineHeater: 'Motorheizung', misc: 'Sonstiges', raw: 'Rohdaten',
    setpoint: 'Sollwert', hysteresis: 'Hysterese', runTime: 'Laufzeit', unlimited: 'Unbegrenzt',
    telemetryInterval: 'Telemetrie-Intervall', language: 'Sprache',
    theme: 'Design', themeSystem: 'System', themeLight: 'Hell', themeDark: 'Dunkel',
    off: 'Aus', heat: 'Heizen', vent: 'Lüften', auto: 'auto',
    logIn: 'Anmelden', register: 'Registrieren', createAccount: 'Konto erstellen', backToLogin: 'Zurück zur Anmeldung',
    logOut: 'Abmelden', copy: 'Kopieren', copied: 'Kopiert!', continueToLogin: 'Weiter zur Anmeldung',
    loginPlaceholder: 'Login', passwordPlaceholder: 'Passwort',
    regLoginPlaceholder: 'Login (zugleich Ihr MQTT-Benutzername)', emailPlaceholder: 'E-Mail (optional)',
    connConnecting: 'Verbinde mit Broker…', connConnected: 'Broker verbunden',
    connReconnecting: 'Broker verbindet erneut…', connDisconnected: 'Broker getrennt',
    deviceUnknown: 'Gerät: —', deviceOnline: 'Gerät online', deviceOffline: 'Gerät offline',
    linkInvalid: 'Der Link ist ungültig oder abgelaufen — fordern Sie einen neuen an (SMS „getlink“ senden).',
    waitingDevice: 'Warte auf Verbindungsaufbau des Geräts…',
    otaDevices: 'Geräte', otaVersion: 'Version', otaUpdate: 'Laden',
    otaNoVersions: 'Keine Version veröffentlicht', otaStatusStaging: 'Wird geladen…',
    otaStatusError: 'Laden fehlgeschlagen', otaEmpty: 'Leer', otaLoadedPrefix: 'Geladen',
    otaNoDevicesFound: 'Noch keine Geräte auf dem Bus gesehen', deviceTypePrefix: 'Typ',
    ownFirmware: 'Modem-Firmware',
    deviceAddressLabel: 'Adresse', deviceVersionLabel: 'Version', modemVersionLabel: 'Modem-Version',
    canRelayFlash: 'Gerät flashen', canRelayStatusIdle: 'Noch nicht geflasht', canRelayStatusStaging: 'Flashen…',
    canRelayStatusDone: 'Gerät aktuell', canRelayStatusError: 'Flashen fehlgeschlagen',
    tankTemp: 'Tanktemperatur', heaterTemp: 'Kesseltemperatur', voltage: 'Spannung', outdoorTemp: 'Außentemperatur',
    heaterState: 'Kesselstatus', domesticWater: 'Warmwasser', liquidLevel: 'Flüssigkeitsstand',
    pumpsRunning: 'Laufende Pumpen', floorTemp: 'Fußbodentemperatur', floorPump: 'Fußbodenpumpe',
    engineTemp: 'Motortemperatur', enginePump: 'Motorpumpe', errorsLabel: 'Fehler',
    onState: 'EIN', offState: 'aus', flowing: 'Durchfluss', noneLabel: 'keine',
    heaterIdle: 'Bereit', heaterBlowing: 'Gebläse', heaterIgnition: 'Zündung/Aufwärmen', heaterWorkOnPower: 'Volllast',
    pump1: 'Pumpe 1', pump2: 'Pumpe 2', pump3: 'Pumpe 3', pump4: 'Pumpe 4',
    heaterPump: 'Kesselpumpe', auxPump1: 'Zusatzpumpe 1', auxPump2: 'Zusatzpumpe 2', auxPump3: 'Zusatzpumpe 3',
  },
};
const LANGUAGES = [
  { value: 'en', label: 'English' },
  { value: 'ru', label: 'Русский' },
  { value: 'de', label: 'Deutsch' },
];
let uiLang = 'en';
function t(key) { return (I18N[uiLang] && I18N[uiLang][key]) || I18N.en[key] || key; }

/* Unlike LANGUAGES above, these option labels ARE i18n keys — "System/
   Light/Dark" reads in whatever language is currently selected, whereas a
   language's own name in the language picker deliberately doesn't (see
   LANGUAGES). See the `translateOptions` branch in buildSettingsRow/
   updateSettingsGroup below for how that distinction is honored. */
const THEMES = [
  { value: 'system', label: 'themeSystem' },
  { value: 'light', label: 'themeLight' },
  { value: 'dark', label: 'themeDark' },
];
const THEME_STORAGE_KEY = 'timberline-theme';

/* Per-browser display preference — deliberately NOT synced via MQTT like
   "lang" (a phone that should follow its own OS dark-mode setting has no
   business inheriting the theme some other device on the same account
   picked). "system" removes the override entirely so the
   prefers-color-scheme media query in index.html decides, same as before
   this setting existed; light/dark force :root[data-theme=...] regardless
   of OS setting. The very first application (avoiding a flash of the wrong
   theme before this script even loads) happens synchronously in an inline
   <script> at the top of index.html's <head> — this just handles later
   changes and keeps that inline copy's logic in sync. */
function applyTheme(value) {
  uiTheme = value;
  rawStatus.theme = value;
  localStorage.setItem(THEME_STORAGE_KEY, value);
  if (value === 'light' || value === 'dark') document.documentElement.dataset.theme = value;
  else delete document.documentElement.dataset.theme;
}
let uiTheme = (() => {
  const saved = localStorage.getItem(THEME_STORAGE_KEY);
  return saved === 'light' || saved === 'dark' ? saved : 'system';
})();

/* Static (non-repeatedly-rendered) elements translated by id — called once
   on load and again whenever uiLang changes (locally or via the "lang"
   actual topic echoing back a device-confirmed value). The dynamically
   rebuilt rows (icon/zone/drum/settings) pick up the new language on their
   next renderButtons() pass instead, since they already call t() live. */
function applyStaticTranslations() {
  $('loginLogin').placeholder = t('loginPlaceholder');
  $('loginPassword').placeholder = t('passwordPlaceholder');
  $('loginBtn').textContent = t('logIn');
  $('showRegisterBtn').textContent = t('register');
  $('regLogin').placeholder = t('regLoginPlaceholder');
  $('regPassword').placeholder = t('passwordPlaceholder');
  $('regEmail').placeholder = t('emailPlaceholder');
  $('registerBtn').textContent = t('createAccount');
  $('showLoginBtn').textContent = t('backToLogin');
  $('copySmsBtn').textContent = t('copy');
  $('smsContinueBtn').textContent = t('continueToLogin');
  $('floorSettings').querySelector('summary').textContent = t('floorHeating');
  $('engineSettings').querySelector('summary').textContent = t('engineHeater');
  $('miscSettings').querySelector('summary').textContent = t('misc');
  $('otaSummary').textContent = t('otaDevices');
  $('modemVersionLabel').textContent = t('modemVersionLabel');
  $('rawDetails').querySelector('summary').textContent = t('raw');
  $('logoutBtn').textContent = t('logOut');
  refreshStatusLabels();
  renderStatus(); /* re-translates the telemetry summary table (HEATER_ICON_LABELS/PUMP_LABELS/etc.) */
  renderOtaPanel(); /* re-translates the status line / "no versions" option */
}

/* First row: icon toggle buttons. Floor/Engine carry a connectedKey — not
   every physical system has that hardware, so they stay hidden until the
   modem confirms presence via the matching "*Connected" status topic (see
   mqttActualizerHandler in Timberline.cpp). Heater/Element are always
   present, no gating. */
const ICON_BUTTONS = [
  { name: 'btnHtr', label: 'heater', icon: ICONS.flame },
  { name: 'btnElement', label: 'element', icon: ICONS.bolt },
  { name: 'btnFloor', label: 'floor', icon: ICONS.floor, connectedKey: 'floorConnected' },
  { name: 'btnEngine', label: 'engine', icon: ICONS.engine, connectedKey: 'engineConnected' },
];

/* Everything else, rendered the old label/ON-off way below the icon row. */
const OTHER_BUTTONS = [
  { name: 'btnEco', label: 'eco' },
];

const ZONE_COUNT = 5;

/* The 3 "drums" under the zone row — all operate on whichever zone is
   currently selected (see selectedZone). Order matches what was asked for:
   day setpoint, fan speed, night setpoint. min/max/step are UI-side
   sanity bounds, not enforced by the firmware — it accepts whatever's
   published to cmd/desired/zn<N>/<key>. */
const DRUMS = [
  { key: 'daySp', label: 'day', min: 10, max: 32, step: 1, unit: '°' },
  { key: 'fanPct', label: 'fan', min: 10, max: 100, step: 1, unit: '%', isFan: true },
  { key: 'nightSp', label: 'night', min: 10, max: 32, step: 1, unit: '°' },
];

/* Floor/engine settings, tucked behind the two <details> "spoilers" in
   index.html — global (not per-zone) values, gated on the same
   floorConnected/engineConnected hardware-presence flags as the icon
   buttons (see ICON_BUTTONS). Ranges mirror the firmware's own validation
   in Timberline.cpp's onMqttCommandReceived() — the slider can't offer
   anything the modem would reject anyway. */
const FLOOR_SETTINGS = [
  { key: 'floorSp', label: 'setpoint', min: 2, max: 32, step: 1, unit: '°' },
  { key: 'floorHyst', label: 'hysteresis', min: 2, max: 10, step: 1, unit: '°' },
];
/* engineDur: >1440 (24h) reads as "Unlimited" — same convention the
   firmware itself already uses for SystemTimeLimitHours (see Timberline.h).
   1450 is the one slider stop above that line (10..1450 step 10 lands on
   1440 exactly, then one more step to 1450). */
const ENGINE_SETTINGS = [
  { key: 'engineSp', label: 'setpoint', min: 0, max: 80, step: 1, unit: '°' },
  { key: 'engineDur', label: 'runTime', min: 10, max: 1450, step: 10, unit: ' min',
    format: (v) => (Number(v) > 1440 ? t('unlimited') : `${v} min`) },
];
/* Modem-level settings, not tied to any zone/hardware presence — always
   shown, unlike FLOOR_SETTINGS/ENGINE_SETTINGS above (see
   updateSettingsGroup()'s connectedKey === null case). Range mirrors the
   firmware's own clamp in onMqttCommandReceived() (Timberline.cpp). */
const MISC_SETTINGS = [
  /* Key is "telemetryInt", not the more obvious "telemetryInterval" — the
     modem's MQTT-desired-topic-name buffer (Modem::mqttRxName) is only 16
     bytes, and the longer name silently truncated and never matched. */
  { key: 'telemetryInt', label: 'telemetryInterval', min: 5, max: 60, step: 1, unit: ' s' },
  /* UI language — web-app-only, no modem/firmware involvement at all (see
     publishLang()/the 'lang' branch in buildSettingsRow's select handler).
     Persisted as its own retained MQTT message under a "web/" topic instead
     of the usual cmd/desired -> cmd/actual round trip every other setting
     here goes through, since there's no device to confirm it. */
  { key: 'lang', label: 'language', type: 'select', options: LANGUAGES },
  /* Display theme — purely local to this browser, see applyTheme(). */
  { key: 'theme', label: 'theme', type: 'select', options: THEMES, translateOptions: true },
];

let mqttClient = null;
let mqttUsername = null;
let selectedZone = null;  /* unknown until zn<N>/connected data arrives */
let zoneManuallySelected = false;  /* once the user clicks a zone, stop auto-following */
let telemetry = null;     /* decoded "telemetry" blob, see decodeTelemetry() */
const buttonState = {};
const connectedState = {};
const rawStatus = {};

/* Mirrors the 20-byte packed struct built in Timberline::mqttTelemetryHandler
   (modem/User/Timberline.cpp) — byte-for-byte, keep the two in sync.
   Per-zone current temp/fan speed live here (zoneFanPwm/zoneCurrentTemp),
   not in the "zn<N>" actual topics (parseZoneStatus() below) — deliberately:
   those two are the fastest-changing fields, and this blob exists
   specifically so fast-changing values go out as 1 packet on a timer
   instead of many; folding them into the 5 per-zone topics was tried and
   reverted (see mqtt-topic-scheme memory) — it meant every temperature
   tick republishing all 5 zone topics too, undoing exactly what this
   buys. */
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

/* zn<N> ("cmd/actual/zn<N>") — one combined topic per zone, replacing what
   used to be six (connected/state/daySp/nightSp/fanPct/fanManual). Payload:
   "<connected>_<state>_<daySp>_<nightSp>_<fanPct>_<fanManual>" — state is
   the firmware's raw zoneState_t ordinal (0/1/2), mapped back to
   "off"/"heat"/"vent" here so every existing string comparison (zone menu,
   CSS state-<x> class, pending/confirmed tracking keyed by the old
   "zn<N>/state" desired-topic name) keeps working unchanged.
   connected/daySp/nightSp/fanPct/fanManual stay strings too, same reason.
   Deliberately does NOT include current temp/fan speed — those stay in
   `telemetry` (see decodeTelemetry()'s comment). Returns null if nothing's
   arrived yet for this zone, or the payload doesn't parse (e.g. a stale
   value from before this format existed — never valid, same "don't guess"
   precedent as elsewhere). */
const ZONE_STATE_NAMES = ['off', 'heat', 'vent'];
function parseZoneStatus(z) {
  const raw = rawStatus[`zn${z}`];
  if (raw === undefined) return null;
  const parts = raw.split('_');
  if (parts.length !== 6) return null;
  const [connected, state, daySp, nightSp, fanPct, fanManual] = parts;
  return {
    connected,
    state: ZONE_STATE_NAMES[Number(state)] || 'off',
    daySp,
    nightSp,
    fanPct,
    fanManual,
  };
}

function $(id) { return document.getElementById(id); }

function showBox(id) {
  ['authBox', 'registerBox', 'smsBox', 'controlBox'].forEach(b => $(b).classList.toggle('hidden', b !== id));
}

function publishToggle(name, isOn) {
  const next = isOn ? '0' : '1';
  desiredValues[name] = next;
  mqttClient.publish(`${mqttUsername}/cmd/desired/${name}`, next);
  renderButtons(); /* spin the pending indicator right away */
}

function makeSpinner() {
  const spinner = document.createElement('div');
  spinner.className = 'pending-spinner';
  return spinner;
}

function renderIconRow() {
  const container = $('iconRow');
  container.innerHTML = '';
  ICON_BUTTONS.forEach(b => {
    if (b.connectedKey && connectedState[b.connectedKey] !== '1') return;

    const confirmed = buttonState[b.name];
    let pending = desiredValues[b.name];
    /* Device caught up — stop tracking it as pending. */
    if (pending !== undefined && confirmed !== undefined && pending === confirmed) {
      delete desiredValues[b.name];
      pending = undefined;
    }
    /* Display stays at the confirmed state until the device actually
       echoes the change back — only the spinner indicates "in flight".
       effectiveOn (pending if there is one) is just for computing what a
       second click toggles from, so clicking again before confirmation
       flips the *requested* state, not the stale on-screen one. */
    const effectiveOn = (pending !== undefined ? pending : confirmed) === '1';
    const displayOn = confirmed === '1';

    const btn = document.createElement('button');
    btn.className = 'icon-btn' + (displayOn ? ' on' : '');
    const svgAttrs = b.icon.fill
      ? 'fill="currentColor" stroke="none"'
      : 'fill="none" stroke="currentColor" stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round"';
    btn.innerHTML = `<svg viewBox="${b.icon.viewBox}" ${svgAttrs}>${b.icon.path}</svg><span class="icon-label">${t(b.label)}</span>`;
    btn.onclick = () => publishToggle(b.name, effectiveOn);
    if (pending !== undefined) btn.appendChild(makeSpinner());
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
    label.textContent = t(b.label);

    const confirmed = buttonState[b.name];
    let pending = desiredValues[b.name];
    if (pending !== undefined && confirmed !== undefined && pending === confirmed) {
      delete desiredValues[b.name];
      pending = undefined;
    }
    const effectiveOn = (pending !== undefined ? pending : confirmed) === '1';
    /* Asymmetric on purpose: turning ON waits for confirmation (blue in the
       meantime, thumb stays put — same "don't move until confirmed" rule as
       the icon buttons/zones). Turning OFF just flips immediately, no
       pending/blue treatment — matches the original pre-tracking behavior. */
    const isPendingOn = pending === '1';
    const displayOn = isPendingOn ? confirmed === '1' : effectiveOn;
    const btn = document.createElement('button');
    btn.className = 'switch' + (displayOn ? ' on' : '') + (isPendingOn ? ' pending' : '');
    btn.innerHTML = '<span class="switch-thumb"></span>';
    btn.onclick = () => publishToggle(b.name, effectiveOn);

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

/* UI language — web-app-only, the modem/firmware is never involved. Stored
   as a retained message the browser itself publishes/reads directly (not
   the cmd/desired -> cmd/actual round trip the other settings use, which
   only the modem echoes) — the MQTT broker is just acting as small durable
   key-value storage here so the choice survives closing the tab, shared
   across whichever browser/device logs into this account. Lost only if the
   broker itself restarts (acceptable — see the "web" prefix note below). */
function publishLang(value) {
  mqttClient.publish(`${mqttUsername}/web/lang`, value, { retain: true });
}

/* zn<N>'s connected field mirrors Timberline::zoneConnected: 0 = not
   connected, 1 = dependent heater (has a fan), 2 = defrost (always-on, not
   user-controllable), 3 = radiator (no fan). 1 and 3 are shown — matches
   the same filter the firmware's own "status" SMS reply uses. Default (no
   data yet) is "not shown", same pattern as floorConnected/engineConnected. */
function connectedZones() {
  const zones = [];
  for (let z = 1; z <= ZONE_COUNT; z++) {
    const zs = parseZoneStatus(z);
    if (zs && (zs.connected === '1' || zs.connected === '3')) zones.push(z);
  }
  return zones;
}

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
  /* Retained zn<N>/connected messages can arrive in any order over MQTT —
     without the manual-selection guard, whichever zone happens to connect
     first would "win" and stick even after zone 1 shows up later. Auto-pick
     always prefers the lowest-numbered zone until the user actually clicks
     one themselves. */
  if (!zoneManuallySelected || !zones.includes(selectedZone)) selectedZone = zones[0] || null;

  zones.forEach((z) => {
    const zs = parseZoneStatus(z);
    const temp = telemetry ? telemetry.zoneCurrentTemp[z - 1] : undefined;
    const zoneType = zs ? zs.connected : undefined;
    const stateTopic = `zn${z}/state`;
    const confirmedState = zs ? zs.state : undefined;
    let pendingState = desiredValues[stateTopic];
    /* Device caught up — stop tracking it as pending. */
    if (pendingState !== undefined && confirmedState !== undefined && pendingState === confirmedState) {
      delete desiredValues[stateTopic];
      pendingState = undefined;
    }
    /* Card background stays at the confirmed state until the device
       echoes the change back — matches the icon buttons (see
       renderIconRow()): only the spinner indicates "in flight". */
    const state = confirmedState;
    const stateOptions = zoneType === '3' ? ['off', 'heat'] : ['off', 'heat', 'vent'];

    const card = document.createElement('div');
    card.className = 'zone-btn' + (state ? ` state-${state}` : '') + (z === selectedZone ? ' selected' : '');
    card.onclick = () => { selectedZone = z; zoneManuallySelected = true; openZoneMenu = null; renderZoneRow(); renderDrums(); };

    /* zoneType 1 = has a fan, 3 = radiator (no fan) — see connectedZones().
       Fan spins while its live speed (telemetry) is nonzero; a radiator has
       no speed concept, so it just sits static. */
    const zoneIconDef = zoneType === '1' ? ICONS.fan : zoneType === '3' ? ICONS.radiator : null;
    if (zoneIconDef) {
      const fanPwm = zoneType === '1' && telemetry ? telemetry.zoneFanPwm[z - 1] : 0;
      const wrap = document.createElement('div');
      wrap.className = 'zone-icon-wrap';
      const icon = document.createElement('div');
      icon.className = 'zone-icon' + (fanPwm > 0 ? ' spinning' : '');
      icon.innerHTML = `<svg viewBox="${zoneIconDef.viewBox}" fill="currentColor" stroke="none">${zoneIconDef.path}</svg>`;
      wrap.appendChild(icon);
      /* Current fan speed at a glance, without a competing second number on
         an already-tiny card — a slim triangular level meter (narrow/short
         end = low speed, wide/tall end = full speed) instead. Radiators
         have no fan, so no meter for those. */
      if (zoneType === '1') {
        const level = document.createElement('div');
        level.className = 'fan-level';
        const fill = document.createElement('div');
        fill.className = 'fan-level-fill';
        fill.style.height = `${Math.max(0, Math.min(100, fanPwm))}%`;
        level.appendChild(fill);
        wrap.appendChild(level);
      }
      card.appendChild(wrap);
    }

    const info = document.createElement('div');
    info.className = 'zone-info';
    info.innerHTML = `<span class="zone-temp">${temp !== undefined ? temp + '°' : '–'}</span>`;
    card.appendChild(info);

    /* Centered/overlaid rather than inline next to the temperature — at
       the card's small width (esp. 4-5 connected zones, 3 per row) an
       inline spinner was crowding into the fan-level meter. Semi-transparent
       so the temperature/icon underneath stay legible while it spins. */
    if (pendingState !== undefined) {
      const spinner = makeSpinner();
      spinner.classList.add('zone-spinner');
      card.appendChild(spinner);
    }

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
        item.textContent = t(opt);
        item.onclick = (e) => {
          e.stopPropagation();
          desiredValues[stateTopic] = opt;
          mqttClient.publish(`${mqttUsername}/cmd/desired/${stateTopic}`, opt);
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

  const zoneType = selectedZone ? (parseZoneStatus(selectedZone) || {}).connected : null;
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

/* Tracks a value the user just set (slider drag or drum spin) that the
   device hasn't echoed back yet via cmd/actual/<key> — see
   updateSettingsGroup()/buildDrum() below for how this drives the
   confirmed/pending color and pulse. Keyed by topic: plain setting key for
   the non-zone sliders (floorSp, engineDur, ...), full "zn<N>/<key>" for
   drums (daySp, nightSp, fanPct are per-zone). One flat map works for both
   since the key spaces don't overlap. */
const desiredValues = {};

function buildSettingsRow(groupId, s) {
  const row = document.createElement('div');
  row.className = 'setting-row';

  const label = document.createElement('label');
  const text = document.createElement('span');
  text.id = `${groupId}-${s.key}-label`;
  text.textContent = t(s.label);
  const value = document.createElement('span');
  value.className = 'setting-value';
  value.id = `${groupId}-${s.key}-value`;
  label.appendChild(text);
  label.appendChild(value);

  let input;
  if (s.type === 'select') {
    input = document.createElement('select');
    s.options.forEach((o) => {
      const opt = document.createElement('option');
      opt.value = o.value;
      /* LANGUAGES: each language's own native name, never translated.
         THEMES (translateOptions: true): "System/Light/Dark", translated —
         refreshed again on every render, see updateSettingsGroup below. */
      opt.textContent = s.translateOptions ? t(o.label) : o.label;
      input.appendChild(opt);
    });
    input.id = `${groupId}-${s.key}`;
    input.addEventListener('change', () => {
      /* Applied immediately — these are purely local rendering choices with
         no firmware/hardware involved, so there's nothing to wait on. */
      if (s.key === 'lang') {
        uiLang = input.value;
        rawStatus.lang = input.value;
        publishLang(input.value);
        applyStaticTranslations();
        renderButtons();
      } else if (s.key === 'theme') {
        applyTheme(input.value);
        renderSettings();
      }
    });
  } else {
    input = document.createElement('input');
    input.type = 'range';
    input.id = `${groupId}-${s.key}`;
    input.min = String(s.min);
    input.max = String(s.max);
    input.step = String(s.step);
    input.addEventListener('input', () => { value.textContent = s.format ? s.format(input.value) : input.value + s.unit; });
    input.addEventListener('change', () => {
      desiredValues[s.key] = input.value;
      publishValue(s.key, input.value);
      renderSettings(); /* turn the slider blue right away, don't wait for the next unrelated message */
    });
  }

  row.appendChild(label);
  row.appendChild(input);
  return row;
}

function buildSettingsPanels() {
  if (settingsRowsBuilt) return;
  settingsRowsBuilt = true;
  FLOOR_SETTINGS.forEach((s) => $('floorSettings').appendChild(buildSettingsRow('floorSettings', s)));
  ENGINE_SETTINGS.forEach((s) => $('engineSettings').appendChild(buildSettingsRow('engineSettings', s)));
  MISC_SETTINGS.forEach((s) => $('miscSettings').appendChild(buildSettingsRow('miscSettings', s)));
}

function updateSettingsGroup(groupId, connectedKey, settings) {
  const details = $(groupId);
  /* connectedKey === null means "no hardware gate" — always shown (see
     MISC_SETTINGS/miscSettings). */
  const visible = connectedKey ? connectedState[connectedKey] === '1' : true;
  details.classList.toggle('hidden', !visible);
  if (!visible) return;

  settings.forEach((s) => {
    const raw = rawStatus[s.key]; /* last value the device itself confirmed */
    const input = $(`${groupId}-${s.key}`);
    const valueEl = $(`${groupId}-${s.key}-value`);
    $(`${groupId}-${s.key}-label`).textContent = t(s.label);
    if (s.translateOptions) {
      Array.from(input.options).forEach((opt) => {
        const def = s.options.find((o) => o.value === opt.value);
        if (def) opt.textContent = t(def.label);
      });
    }
    let pending = desiredValues[s.key];

    /* Device caught up to what was requested — done, stop tracking it as
       pending. String comparison: raw arrives as text off the wire, pending
       was stored from input.value (also text) — no numeric coercion needed. */
    if (pending !== undefined && raw !== undefined && pending === raw) {
      delete desiredValues[s.key];
      pending = undefined;
    }

    const display = pending !== undefined ? pending : raw;
    if (display === undefined) return;

    if (document.activeElement !== input) {
      if (input.value !== display) input.value = display;
      valueEl.textContent = s.type === 'select' ? '' : (s.format ? s.format(display) : display + s.unit);
    }

    input.classList.toggle('pending', pending !== undefined);
  });
}

function renderSettings() {
  buildSettingsPanels();
  updateSettingsGroup('floorSettings', 'floorConnected', FLOOR_SETTINGS);
  updateSettingsGroup('engineSettings', 'engineConnected', ENGINE_SETTINGS);
  updateSettingsGroup('miscSettings', null, MISC_SETTINGS);
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
  /* topicName/manualTopic are still the "cmd/desired/zn<N>/<prop>" paths —
     that side is untouched (see zonePrefix() in Timberline.cpp), only the
     *confirmed* value now comes from the combined "zn<N>" actual topic
     (parseZoneStatus()) instead of its own dedicated one. */
  const topicName = selectedZone ? `zn${selectedZone}/${d.key}` : null;
  const manualTopic = selectedZone ? `zn${selectedZone}/fanManual` : null;
  const zs = selectedZone ? parseZoneStatus(selectedZone) : null;
  const confirmed = zs ? zs[d.key] : undefined;
  let pending = topicName ? desiredValues[topicName] : undefined;
  /* Device caught up to what was requested — stop tracking it as pending. */
  if (pending !== undefined && confirmed !== undefined && pending === confirmed) {
    delete desiredValues[topicName];
    pending = undefined;
  }
  const isPending = pending !== undefined;
  const raw = isPending ? pending : confirmed;
  const isAuto = !!d.isFan && !!selectedZone && (!zs || zs.fanManual !== '1');

  const drum = document.createElement('div');
  drum.className = 'drum' + (d.isFan && selectedZone ? (isAuto ? ' auto' : ' manual') : '');

  const label = document.createElement('div');
  label.className = 'drum-label';
  label.textContent = t(d.label);

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
    staticEl.textContent = isAuto ? t('auto') : '–';
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
      /* Track as pending rather than mirroring straight into rawStatus —
         rawStatus[topicName] still holds the pre-drag value at this instant
         (the device's own "actual" echo hasn't arrived yet). Marking it
         pending (see buildDrum()) both shows the new value right away
         *and* pulses it, instead of silently pretending it's already
         confirmed. */
      desiredValues[topicName] = String(newVal);
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
   2 ignition/warming (one combined state), 3 work on power. i18n keys, not
   display text — same convention as the *_BUTTONS/*_SETTINGS `label`
   fields above (see t() near the top of the file). */
const HEATER_ICON_LABELS = ['heaterIdle', 'heaterBlowing', 'heaterIgnition', 'heaterWorkOnPower'];
const PUMP_LABELS = ['pump1', 'pump2', 'pump3', 'pump4', 'heaterPump', 'auxPump1', 'auxPump2', 'auxPump3'];

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

  const tel = telemetry; /* not named `t` — that's the global translate function */
  const onPumps = PUMP_LABELS.filter((_, i) => tel.pumps[i]).map(t);
  const errors = rawStatus.errors;

  let rows = '';
  rows += statusRow(t('tankTemp'), `${tel.tankTemp}°`);
  rows += statusRow(t('heaterTemp'), `${tel.heaterTemp}°`);
  rows += statusRow(t('voltage'), `${tel.voltage.toFixed(1)}V`);
  rows += statusRow(t('outdoorTemp'), `${tel.outdoorTemp}°`);
  rows += statusRow(t('heaterState'), t(HEATER_ICON_LABELS[tel.heaterIcon]) || '?');
  rows += statusRow(t('element'), tel.elementState ? t('onState') : t('offState'));
  rows += statusRow(t('domesticWater'), tel.domesticWaterFlow ? t('flowing') : t('offState'));
  rows += statusRow(t('liquidLevel'), `${tel.liquidLevel}/6`);
  rows += statusRow(t('pumpsRunning'), onPumps.length ? onPumps.join(', ') : t('noneLabel'));
  /* Floor/engine are optional hardware — only show once we know it's
     actually installed (same connectedState gate the icon row uses). */
  if (connectedState.floorConnected === '1') {
    rows += statusRow(t('floorTemp'), `${tel.floorTemp}°`);
    rows += statusRow(t('floorPump'), tel.floorPumpState ? t('onState') : t('offState'));
  }
  if (connectedState.engineConnected === '1') {
    rows += statusRow(t('engineTemp'), `${tel.engineTemp}°`);
    rows += statusRow(t('enginePump'), tel.enginePumpState ? t('onState') : t('offState'));
  }
  if (errors && errors !== '0') {
    rows += `<tr class="errors-row"><td colspan="2">${t('errorsLabel')}: ${errors}</td></tr>`;
  }
  table.innerHTML = rows;
}

function renderStatus() {
  $('statusPre').textContent = JSON.stringify(rawStatus, null, 2);
  renderStatusTable();
  renderOtaPanel();
}

/* ── Generalized device firmware OTA ─────────────────────────────────────
   Any device on the CAN bus can show up here, not just MBC-2 — the modem
   publishes "dev<type>_<addr>" = "<v1>.<v2>.<v3>.<v4>" for every device it
   has seen a PGN=18 announcement from (Timberline::recordSeenDevice()).
   One card per device: version list comes from the server (GET
   /firmware/<type>/versions, see server.js) per device type, fetched once
   and cached. Status/progress (rawStatus.otaStatus/otaProgress/
   otaStagedType, rawStatus.canRelayStatus/canRelayProgress) is global —
   the modem can only load/flash one device at a time — so which card it
   belongs to is tracked via otaStagedType (which device's image is
   currently loaded) and lastOtaStartType (client-side, which device this
   browser's own Load click targeted, for showing progress/error on the
   right card while a download is in flight). */
/* Device names come from the server (GET /device-types.json), not a
   hardcoded client table — a bare device type isn't always enough to name
   it (e.g. type 126 is "some control panel", but 126.3 vs 126.6 is the
   difference between PU-28 Timberline and PU-28 Multihot CN; type 34's
   second version byte likewise distinguishes a 12V from a 24V Бинар-10
   build). Keyed by "<type>.<subtype>" (subtype = the version's 2nd byte,
   e.g. "126.6"), falling back to a bare "<type>" entry, then a generic
   "Type N" label — see deviceTypeName() below. Static file, so adding a
   new device variant's name is just an edit + redeploy, no code change. */
let deviceTypeNamesTable = null;
let deviceTypeNamesFetched = false;
async function fetchDeviceTypeNames() {
  if (deviceTypeNamesFetched) return;
  deviceTypeNamesFetched = true;
  try {
    const r = await fetch('/device-types.json');
    deviceTypeNamesTable = await r.json();
  } catch (e) {
    deviceTypeNamesTable = {};
  }
  renderOtaPanel();
}
function deviceTypeName(type, subtype) {
  const table = deviceTypeNamesTable || {};
  if (table[`${type}.${subtype}`]) return table[`${type}.${subtype}`];
  if (table[type]) return table[type];
  return `${t('deviceTypePrefix')} ${type}`;
}

function getSeenDevices() {
  const out = [];
  for (const k in rawStatus) {
    const m = /^dev(\d+)_(\d+)$/.exec(k);
    if (!m) continue;
    const parts = rawStatus[k].split('.');
    out.push({
      type: parseInt(m[1], 10), address: parseInt(m[2], 10),
      subtype: parts[1] !== undefined ? parseInt(parts[1], 10) : null,
      version: rawStatus[k], key: `${m[1]}_${m[2]}`,
    });
  }
  out.sort((a, b) => a.type - b.type || a.address - b.address);
  return out;
}

const otaVersionsByType = {};
const otaVersionsFetchedTypes = new Set();
async function fetchOtaVersionsForType(type) {
  if (otaVersionsFetchedTypes.has(type)) return;
  otaVersionsFetchedTypes.add(type);
  let versions = [];
  try {
    const r = await fetch(`/firmware/${type}/versions`);
    const data = await r.json();
    versions = Array.isArray(data.versions) ? data.versions : [];
  } catch (e) {
    versions = [];
  }
  otaVersionsByType[type] = versions;
  renderOtaPanel();
}

let lastOtaStartType = null;

/* The modem's own firmware — a fixed card, not one generated per seen CAN
   device (see the big comment on selfOtaCard in index.html): downloads go
   through the exact same otaStart/doOta() machinery as any device (see
   Modem::doOta()'s "self" branch, modem/User/Modem.cpp), just with the
   modem's own type as target and no CAN-relay "Flash" step afterward.
   "Own type" is parsed from modemVersion's first dot-segment rather than
   hardcoded here, so this keeps working if the modem's product type ever
   changes. Staged-state comes from rawStatus.selfOtaStaged (published by
   Timberline::mqttActualizerHandler from Modem::selfOta) — independent of
   otaStaged/otaStagedType, which only ever describe the shared
   target-device buffer. */
function updateSelfOtaCard(status, staging) {
  const card = $('selfOtaCard');
  if (!card) return;
  const ownType = rawStatus.modemVersion ? parseInt(rawStatus.modemVersion.split('.')[0], 10) : null;
  if (!ownType) { card.classList.add('hidden'); return; }
  card.classList.remove('hidden');

  fetchOtaVersionsForType(ownType); /* no-op once fetched/in flight for this type */

  card.querySelector('#selfOtaTitle').textContent = t('ownFirmware');
  card.querySelector('#selfOtaVersionLabel').textContent = t('otaVersion');
  const loadBtn = card.querySelector('#selfOtaLoad');
  loadBtn.textContent = t('otaUpdate');
  if (!loadBtn.dataset.wired) {
    loadBtn.dataset.wired = '1';
    loadBtn.onclick = () => {
      const v = card.querySelector('#selfOtaVersion').value;
      if (!v || !mqttClient) return;
      lastOtaStartType = ownType;
      mqttClient.publish(`${mqttUsername}/cmd/desired/otaStart`, `${ownType}:${v}`);
    };
  }

  const versions = otaVersionsByType[ownType];
  const select = card.querySelector('#selfOtaVersion');
  if (document.activeElement !== select) {
    const prevValue = select.value;
    select.innerHTML = '';
    if (versions && versions.length === 0) {
      const opt = document.createElement('option');
      opt.value = '';
      opt.textContent = t('otaNoVersions');
      select.appendChild(opt);
    } else {
      (versions || []).forEach((v) => {
        const opt = document.createElement('option');
        opt.value = v;
        opt.textContent = v;
        select.appendChild(opt);
      });
    }
    if (versions && versions.includes(prevValue)) select.value = prevValue;
  }

  const noVersions = !versions || versions.length === 0;
  select.disabled = staging || noVersions;
  loadBtn.disabled = staging || noVersions;

  const isActiveTarget = lastOtaStartType === ownType;
  const selfStagedValid = !!rawStatus.selfOtaStaged;
  const statusEl = card.querySelector('#selfOtaStatus');
  statusEl.className = (isActiveTarget && status === 'error') ? 'ota-error' : selfStagedValid ? 'ota-done' : '';
  let text = '';
  if (isActiveTarget && status === 'staging') text = `${t('otaStatusStaging')} ${rawStatus.otaProgress || ''}`;
  else if (isActiveTarget && status === 'error') text = t('otaStatusError');
  else text = selfStagedValid ? `${t('otaLoadedPrefix')} ${rawStatus.selfOtaStaged}` : t('otaEmpty');
  statusEl.textContent = text;
}

function renderOtaPanel() {
  const modemVersionEl = $('modemVersionText');
  const listEl = $('deviceList');
  const noneEl = $('otaNoDevicesText');
  if (!modemVersionEl || !listEl || !noneEl) return; /* not logged in yet — controlBox not shown */

  /* rawStatus.modemVersion: the modem's own firmware version — a
     compile-time constant, published once per connection (see
     mqttActualizerHandler), so just show it verbatim once it arrives. */
  if (rawStatus.modemVersion) modemVersionEl.textContent = rawStatus.modemVersion;

  const status = rawStatus.otaStatus; /* idle/staging/done/error, or undefined before the modem's first publish */
  const staging = status === 'staging';

  /* Self-firmware card doesn't depend on any CAN device having been seen —
     update it unconditionally, before the early-return below. */
  updateSelfOtaCard(status, staging);

  const devices = getSeenDevices();
  if (devices.length === 0) {
    listEl.innerHTML = '';
    noneEl.textContent = t('otaNoDevicesFound');
    return;
  }
  noneEl.textContent = '';

  const relayStatus = rawStatus.canRelayStatus;
  const relayStaging = relayStatus === 'staging';
  const stagedType = rawStatus.otaStagedType ? parseInt(rawStatus.otaStagedType, 10) : null;
  const stagedValid = !!rawStatus.otaStaged;

  devices.forEach((dev) => {
    fetchOtaVersionsForType(dev.type); /* no-op once fetched/in flight for this type */
    fetchDeviceTypeNames(); /* no-op once fetched/in flight */

    let card = document.getElementById(`devcard_${dev.key}`);
    if (!card) {
      card = document.createElement('div');
      card.className = 'device-card';
      card.id = `devcard_${dev.key}`;
      card.innerHTML =
        '<div class="device-card-title" data-role="title"></div>' +
        '<div class="device-card-sub" data-role="sub"></div>' +
        '<div class="setting-row"><label><span data-role="versionLabel"></span></label>' +
        '<select data-role="version"></select></div>' +
        '<div class="ota-status-row"><span data-role="status"></span>' +
        '<button data-role="load"></button></div>' +
        '<div class="ota-status-row"><span data-role="relayStatus"></span>' +
        '<button data-role="flash"></button></div>';
      listEl.appendChild(card);
      card.querySelector('[data-role="load"]').onclick = () => {
        const v = card.querySelector('[data-role="version"]').value;
        if (!v || !mqttClient) return;
        lastOtaStartType = dev.type;
        mqttClient.publish(`${mqttUsername}/cmd/desired/otaStart`, `${dev.type}:${v}`);
      };
      card.querySelector('[data-role="flash"]').onclick = () => {
        if (!mqttClient) return;
        mqttClient.publish(`${mqttUsername}/cmd/desired/canRelayStart`, `${dev.type}:${dev.address}`);
      };
    }

    card.querySelector('[data-role="title"]').textContent = deviceTypeName(dev.type, dev.subtype);
    card.querySelector('[data-role="sub"]').textContent =
      `${t('deviceAddressLabel')} ${dev.address} · ${dev.version} ${t('deviceVersionLabel')}`;
    card.querySelector('[data-role="versionLabel"]').textContent = t('otaVersion');
    card.querySelector('[data-role="load"]').textContent = t('otaUpdate');
    card.querySelector('[data-role="flash"]').textContent = t('canRelayFlash');

    const versions = otaVersionsByType[dev.type];
    const select = card.querySelector('[data-role="version"]');
    if (document.activeElement !== select) {
      const prevValue = select.value;
      select.innerHTML = '';
      if (versions && versions.length === 0) {
        const opt = document.createElement('option');
        opt.value = '';
        opt.textContent = t('otaNoVersions');
        select.appendChild(opt);
      } else {
        (versions || []).forEach((v) => {
          const opt = document.createElement('option');
          opt.value = v;
          opt.textContent = v;
          select.appendChild(opt);
        });
      }
      if (versions && versions.includes(prevValue)) select.value = prevValue;
    }

    const noVersions = !versions || versions.length === 0;
    select.disabled = staging || noVersions;
    card.querySelector('[data-role="load"]').disabled = staging || noVersions;

    /* Reflects what's actually loaded in the modem right now for THIS
       device (stagedValid && stagedType matches), rather than the
       ephemeral last-download outcome — "Loaded"/"Empty" wrongly implied
       the *device* was current, when this only ever describes the modem's
       own local copy waiting to be relayed. In-progress/error only show on
       the card this browser itself triggered the Load for. */
    const isActiveTarget = lastOtaStartType === dev.type;
    const thisStaged = stagedValid && stagedType === dev.type;
    const statusEl = card.querySelector('[data-role="status"]');
    statusEl.className = (isActiveTarget && status === 'error') ? 'ota-error' : thisStaged ? 'ota-done' : '';
    let text = '';
    if (isActiveTarget && status === 'staging') text = `${t('otaStatusStaging')} ${rawStatus.otaProgress || ''}`;
    else if (isActiveTarget && status === 'error') text = t('otaStatusError');
    else text = thisStaged ? `${t('otaLoadedPrefix')} ${rawStatus.otaStaged}` : t('otaEmpty');
    statusEl.textContent = text;

    /* rawStatus.canRelayStatus/canRelayProgress: published by
       Timberline::mqttActualizerHandler whenever Timberline::canRelay
       changes — see Timberline::doCanRelay() on the modem side. Flashing
       is only offered once something verified is staged for THIS device
       and nothing else is already running (a download or a relay). */
    const relayEl = card.querySelector('[data-role="relayStatus"]');
    card.querySelector('[data-role="flash"]').disabled = relayStaging || staging || !thisStaged;
    relayEl.className = relayStatus === 'error' ? 'ota-error' : relayStatus === 'done' ? 'ota-done' : '';
    let relayText = '';
    if (relayStatus === 'idle' || !relayStatus) relayText = t('canRelayStatusIdle');
    else if (relayStatus === 'staging') relayText = `${t('canRelayStatusStaging')} ${rawStatus.canRelayProgress || ''}`;
    else if (relayStatus === 'done') relayText = t('canRelayStatusDone');
    else if (relayStatus === 'error') relayText = t('canRelayStatusError');
    relayEl.textContent = relayText;
  });
}

/* Visible, at-a-glance proof that *this browser* actually has a live MQTT
   session — separate from whether the modem itself is connected. The user
   hit a real case where the modem was up but their phone's tab silently
   wasn't (page backgrounded, wifi hiccup, whatever) with nothing on
   screen to reveal that. green = connected, amber/pulsing = connecting or
   reconnecting, red = disconnected. */
let lastConnState = null, lastConnKey = null;
function setConnStatus(state, labelKey) {
  lastConnState = state; lastConnKey = labelKey;
  const dot = $('connDot');
  if (!dot) return;
  dot.className = 'conn-dot' + (state ? ' ' + state : '');
  $('connLabel').textContent = t(labelKey);
}

/* The modem's own presence, via the "online" topic — Last Will (published
   by the broker itself if the modem's session ever drops without a clean
   disconnect) plus an explicit "0" the modem sends on a graceful shutdown
   and "1" right after it connects (Modem.cpp doMqttStart/doMqttTeardown).
   A *different* signal than setConnStatus() above: that's this browser's
   own link to the broker, this is whether the device itself is there at
   all. Unknown (gray, "Device: —") until the first "online" message —
   which arrives immediately on subscribe if it was ever retained. */
let lastDeviceOnline = null;
function setDeviceStatus(online) {
  lastDeviceOnline = online;
  const dot = $('deviceDot');
  if (!dot) return;
  dot.className = 'conn-dot' + (online === null ? '' : online ? ' connected' : ' disconnected');
  $('deviceLabel').textContent = online === null ? t('deviceUnknown') : online ? t('deviceOnline') : t('deviceOffline');
}

/* Re-applies the connection/device status labels in the now-current
   uiLang — setConnStatus()/setDeviceStatus() only run again on their own
   next state change, which a language switch alone doesn't trigger. */
function refreshStatusLabels() {
  if (lastConnKey !== null) setConnStatus(lastConnState, lastConnKey);
  setDeviceStatus(lastDeviceOnline);
}

function connectMqtt(creds) {
  mqttUsername = creds.mqttUsername;
  $('loginLabel').textContent = mqttUsername;
  /* A page loaded over https can't open a plain ws:// connection — the
     browser blocks it as mixed content. Match the page's own scheme. */
  const wsScheme = location.protocol === 'https:' ? 'wss' : 'ws';
  const url = `${wsScheme}://${creds.mqttHost}:${creds.mqttWsPort}`;
  setConnStatus('connecting', 'connConnecting');
  setDeviceStatus(null);
  mqttClient = mqtt.connect(url, {
    username: creds.mqttUsername,
    password: creds.mqttPassword,
    clientId: 'web-' + Math.random().toString(16).slice(2),
  });

  mqttClient.on('connect', () => {
    setConnStatus('connected', 'connConnected');
    mqttClient.subscribe(`${mqttUsername}/cmd/actual/#`);
    /* Separate from cmd/actual/# — that namespace is "modem confirmed this",
       lang is browser-owned storage the modem never touches (see
       publishLang()). Own subscribe call, not folded into a shared
       wildcard, to keep that distinction obvious at the call site. */
    mqttClient.subscribe(`${mqttUsername}/web/lang`);
  });

  mqttClient.on('reconnect', () => setConnStatus('connecting', 'connReconnecting'));
  mqttClient.on('close',     () => setConnStatus('disconnected', 'connDisconnected'));
  mqttClient.on('offline',   () => setConnStatus('disconnected', 'connDisconnected'));

  mqttClient.on('message', (topic, payload) => {
    const value = payload.toString();

    if (topic === `${mqttUsername}/web/lang`) {
      /* Picks up the account's last-saved language on load (retained
         message — arrives right after subscribe) and keeps it in sync if
         changed from another tab/device. Redundant with the optimistic
         apply in buildSettingsRow's own 'change' handler when this tab is
         the one that just changed it — harmless, same value either way. */
      if (LANGUAGES.some(l => l.value === value)) {
        rawStatus.lang = value;
        if (value !== uiLang) { uiLang = value; applyStaticTranslations(); renderButtons(); }
      }
      return;
    }

    const marker = '/cmd/actual/';
    const idx = topic.indexOf(marker);
    if (idx === -1) return;
    const name = topic.slice(idx + marker.length);
    rawStatus[name] = value;
    if (ICON_BUTTONS.some(b => b.name === name) || OTHER_BUTTONS.some(b => b.name === name)) buttonState[name] = value;
    if (name === 'floorConnected' || name === 'engineConnected') connectedState[name] = value;
    if (name === 'telemetry') { telemetry = decodeTelemetry(value); rawStatus.telemetry_decoded = telemetry; }
    if (name === 'online') setDeviceStatus(value === '1');
    renderStatus();
    renderButtons();
  });

  mqttClient.on('error', (e) => { console.error('mqtt error', e); setConnStatus('disconnected', 'connDisconnected'); });

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
        $('err').textContent = t('linkInvalid');
        return false;
      }
      $('err').textContent = t('waitingDevice');
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
    btn.textContent = t('copied');
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

applyTheme(uiTheme); /* re-applies the value the inline <script> in <head> already set on <html> — just populates rawStatus.theme so the Misc select reflects it */
applyStaticTranslations();
showBox('authBox');
tryMagicLink();
