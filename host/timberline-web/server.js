const express = require('express');
const path = require('path');
const fs = require('fs');
const crypto = require('crypto');
const bcrypt = require('bcryptjs');
const Database = require('better-sqlite3');
const mqtt = require('mqtt');
const http = require('http');
const { WebSocketServer } = require('ws');

const app = express();
const server = http.createServer(app);
const PORT = process.env.PORT || 3000;

/* MQTT_HOST is deliberately NOT hardcoded: the broker and this app are
   co-located today, but nothing here should assume that stays true forever,
   or that this app only ever runs on one specific box. If MQTT_HOST isn't
   set, each response falls back to req.hostname — whatever host the browser
   actually used to reach this app — so a plain redeploy to a different
   server/IP/domain needs zero code changes. Set MQTT_HOST explicitly only
   if the broker ever ends up on a *different* host than this app.
   MQTT_BROKER_URL is the separate case of *this process* (the link
   observer) reaching the broker — defaults to localhost since that
   connection is genuinely local today. */
const MQTT_HOST = process.env.MQTT_HOST || null;
const MQTT_WS_PORT = process.env.MQTT_WS_PORT || 8083;
const MQTT_BROKER_URL = process.env.MQTT_BROKER_URL || 'mqtt://localhost:1883';
const LOGIN_MIN = 1;
const LOGIN_MAX = 15; /* matches Modem::mqttUsername[16] and the "login" SMS command bound */
const LINK_OBSERVER_USER = '_linkobserver'; /* reserved, registration rejects a leading "_" */
const SESSION_TTL_MS = 30 * 24 * 60 * 60 * 1000; /* magic-link session password validity */

const db = new Database(path.join(__dirname, 'users.db'));
db.pragma('journal_mode = WAL');
db.exec(`
    CREATE TABLE IF NOT EXISTS users (
        login TEXT PRIMARY KEY,
        password_hash TEXT NOT NULL,
        email TEXT
    );
    CREATE TABLE IF NOT EXISTS link_sessions (
        login TEXT PRIMARY KEY,
        session_hash TEXT NOT NULL,
        expires_at INTEGER NOT NULL
    );
`);
const getUserStmt = db.prepare('SELECT login, password_hash, email FROM users WHERE login = ?');
const insertUserStmt = db.prepare('INSERT INTO users (login, password_hash, email) VALUES (?, ?, ?)');
const upsertServiceUserStmt = db.prepare(`
    INSERT INTO users (login, password_hash, email) VALUES (?, ?, NULL)
    ON CONFLICT(login) DO UPDATE SET password_hash = excluded.password_hash
`);
const getSessionStmt = db.prepare('SELECT session_hash, expires_at FROM link_sessions WHERE login = ?');
const upsertSessionStmt = db.prepare(`
    INSERT INTO link_sessions (login, session_hash, expires_at) VALUES (?, ?, ?)
    ON CONFLICT(login) DO UPDATE SET session_hash = excluded.session_hash, expires_at = excluded.expires_at
`);

app.use(express.json());

/* ── Firmware slice endpoint ──────────────────────────────────────────────
   The modem stages an OTA firmware.bin page by page (2048 bytes each) but
   its AT+HTTP command set turned out not to support HTTP Range requests on
   real hardware (AT+HTTPPARA="USERDATA" — the usual SIMCOM way to inject a
   custom header — came back ERROR). Rather than depend on the module
   supporting Range at all, offset/len are handled server-side: the modem
   just asks for firmware.bin?offset=X&len=Y and gets back a plain 200
   response containing exactly those bytes, same as any other small file it
   already knows how to fetch. Registered before express.static below so it
   intercepts requests carrying the query params; a plain request with
   neither falls through to static's normal full-file serving (browsers
   downloading the whole image, the CRC-sidecar fetch, etc). */
const FIRMWARE_SLICE_TYPE_RE = /^[a-z0-9_-]+$/i;
const FIRMWARE_SLICE_VERSION_RE = /^[0-9.]+$/;
const FIRMWARE_SLICE_MAX_LEN = 4096;
app.get('/firmware/:type/:version/firmware.bin', (req, res, next) => {
    if (req.query.offset === undefined || req.query.len === undefined) return next();
    if (!FIRMWARE_SLICE_TYPE_RE.test(req.params.type) || !FIRMWARE_SLICE_VERSION_RE.test(req.params.version)) {
        return res.status(400).end();
    }
    const offset = Number(req.query.offset);
    const len = Number(req.query.len);
    if (!Number.isInteger(offset) || !Number.isInteger(len) || offset < 0 || len <= 0 || len > FIRMWARE_SLICE_MAX_LEN) {
        return res.status(400).end();
    }
    const filePath = path.join(__dirname, 'public', 'firmware', req.params.type, req.params.version, 'firmware.bin');
    fs.open(filePath, 'r', (openErr, fd) => {
        if (openErr) return res.status(404).end();
        const buf = Buffer.alloc(len);
        fs.read(fd, buf, 0, len, offset, (readErr, bytesRead) => {
            fs.close(fd, () => {});
            if (readErr) return res.status(500).end();
            res.set('Content-Type', 'application/octet-stream');
            res.status(200).send(buf.subarray(0, bytesRead));
        });
    });
});

app.use(express.static(path.join(__dirname, 'public')));

/* ── Firmware OTA listing ──────────────────────────────────────────────────
   The actual .bin/.crc32 files are served by express.static above (they
   just live under public/firmware/<type>/<version>/) — this is only the
   directory-listing piece static serving doesn't provide on its own.
   Version directory names follow this org's device version scheme: 4
   dot-separated bytes matching what the CAN bootloader itself reports
   (VER_PRODUCT_TYPE.VER_VOLTAGE.VER_PRODUCT_SUBTYPE.VER_ASSEMBLAGE_NUMBER —
   see the OmniProtocol PGN=6 param 18 query), e.g. "125.0.0.15" for MBC-2
   (type 125; VER_VOLTAGE unused for this device, always 0). No auth — same
   as the firmware files themselves and the rest of public/. */
const FIRMWARE_TYPE_RE = /^[a-z0-9_-]+$/i;
app.get('/firmware/:type/versions', (req, res) => {
    if (!FIRMWARE_TYPE_RE.test(req.params.type)) return res.status(400).json({ error: 'invalid type' });
    const typeDir = path.join(__dirname, 'public', 'firmware', req.params.type);
    let entries;
    try {
        entries = fs.readdirSync(typeDir, { withFileTypes: true });
    } catch (e) {
        return res.json({ versions: [] }); /* type not published yet — not an error */
    }
    const versions = entries
        .filter((e) => e.isDirectory())
        .map((e) => e.name)
        .filter((name) => fs.existsSync(path.join(typeDir, name, 'firmware.bin'))
                        && fs.existsSync(path.join(typeDir, name, 'firmware.crc32')))
        .sort();
    res.json({ versions });
});

/* ── getlink / magic-link support ─────────────────────────────────────────
   The modem publishes a token to "<username>/cmd/actual/linkToken" (retained)
   when it handles the "getlink" SMS command. This backend observes that
   topic across *every* account via its own reserved MQTT account (broader
   ACL than a normal user gets — see /auth/acl) and caches the latest token
   per username in memory. Retained messages are redelivered on resubscribe,
   so the cache rebuilds itself automatically after a restart — no separate
   persistence needed for the cache itself. */
const linkTokenCache = new Map();

async function startLinkObserver() {
    const password = crypto.randomBytes(24).toString('hex');
    const hash = await bcrypt.hash(password, 10);
    upsertServiceUserStmt.run(LINK_OBSERVER_USER, hash);

    const client = mqtt.connect(MQTT_BROKER_URL, {
        username: LINK_OBSERVER_USER,
        password,
        clientId: 'link-observer',
    });
    client.on('connect', () => client.subscribe('+/cmd/actual/linkToken'));
    client.on('message', (topic, payload) => {
        const m = topic.match(/^([^/]+)\/cmd\/actual\/linkToken$/);
        if (m) linkTokenCache.set(m[1], payload.toString());
    });
    client.on('error', (e) => console.error('link observer MQTT error:', e.message));
}
startLinkObserver();

/* Each account's `login` IS its MQTT username — the same value the owner
   types into their modem via the "login <username>" SMS command. No
   separate device registry needed: the account's own password (checked via
   /auth/user below) is what the modem/browser authenticate to the broker
   with, matching the "password" SMS command on the firmware side. */
/* Shared by POST /api/register (browser form) and GET /api/register-device
   (modem auto-registration, see below) — same rules, just a different
   trigger and response shape. Returns null on success, or an
   {status, error} object to send back as-is on failure. */
async function registerAccount(login, password, email) {
    if (!login || !password) return { status: 400, error: 'login and password required' };
    if (login.length < LOGIN_MIN || login.length > LOGIN_MAX)
        return { status: 400, error: `login must be ${LOGIN_MIN}-${LOGIN_MAX} characters` };
    if (login.startsWith('_')) return { status: 400, error: 'login cannot start with "_"' };
    if (getUserStmt.get(login)) return { status: 409, error: 'login already taken' };

    const passwordHash = await bcrypt.hash(password, 10);
    insertUserStmt.run(login, passwordHash, email || null);
    return null;
}

app.post('/api/register', async (req, res) => {
    const { login, password, email } = req.body || {};
    const err = await registerAccount(login, password, email);
    if (err) return res.status(err.status).json({ error: err.error });
    res.json({ ok: true });
});

/* GET, not POST: called by the modem itself (AT+HTTPACTION=0), which has
   never implemented HTTP POST/AT+HTTPDATA — see doAutoRegister() in
   Modem.cpp. The modem only ever inspects the numeric HTTP status (same
   pattern already used for the plain internet-connectivity check), so no
   response body is required; login/password ride in the query string
   instead of a JSON body, same trade-off already accepted for the
   magic-link token in GET /api/go/:username/:token below. */
app.get('/api/register-device', async (req, res) => {
    const { login, password } = req.query || {};
    const err = await registerAccount(login, password, undefined);
    if (err) return res.sendStatus(err.status);
    res.sendStatus(200);
});

app.post('/api/login', async (req, res) => {
    const { login, password } = req.body || {};
    if (!login || !password) return res.status(400).json({ error: 'login and password required' });

    const user = getUserStmt.get(login);
    if (!user) return res.status(401).json({ error: 'invalid credentials' });

    const ok = await bcrypt.compare(password, user.password_hash);
    if (!ok) return res.status(401).json({ error: 'invalid credentials' });

    /* No session/JWT here — MVP. The browser gets the MQTT credentials
       directly (the same password it just proved it knows) and talks to
       the broker itself. */
    res.json({
        mqttHost: MQTT_HOST || req.hostname,
        mqttWsPort: MQTT_WS_PORT,
        mqttUsername: login,
        mqttPassword: password,
    });
});

/* Magic-link redemption — called by the frontend (app.js), not navigated to
   directly (that's GET /go/:username/:token below, which just serves the
   page shell). Never puts the real account password anywhere: on a token
   match it mints a fresh, independent session password (bcrypt-hashed,
   stored in link_sessions with an expiry) and hands that to the browser
   instead. Revisiting the same bookmarked link keeps working (and quietly
   refreshes the session password) as long as the token still matches — a
   new "getlink" overwrites the retained token and invalidates old links. */
app.get('/api/go/:username/:token', async (req, res) => {
    const { username, token } = req.params;
    if (!linkTokenCache.has(username) || linkTokenCache.get(username) !== token) {
        return res.status(404).json({ error: 'invalid or expired link' });
    }

    const sessionPassword = crypto.randomBytes(16).toString('hex');
    const sessionHash = await bcrypt.hash(sessionPassword, 10);
    upsertSessionStmt.run(username, sessionHash, Date.now() + SESSION_TTL_MS);

    res.json({
        mqttHost: MQTT_HOST || req.hostname,
        mqttWsPort: MQTT_WS_PORT,
        mqttUsername: username,
        mqttPassword: sessionPassword,
    });
});

/* Bookmarkable page — serves the same SPA shell; app.js reads the
   username/token back out of location.pathname and calls the API above. */
app.get('/go/:username/:token', (req, res) => {
    res.sendFile(path.join(__dirname, 'public', 'index.html'));
});

/* Shared by /auth/user below and the AT-bridge WebSocket handshake — same
   two things count as valid: the account's real password, or a still-valid
   magic-link session password (see /api/go/:username/:token above). */
async function verifyCredentials(username, password) {
    if (!username || !password) return false;

    const user = getUserStmt.get(username);
    if (user && await bcrypt.compare(password, user.password_hash)) return true;

    const session = getSessionStmt.get(username);
    if (session && session.expires_at > Date.now() && await bcrypt.compare(password, session.session_hash)) {
        return true;
    }
    return false;
}

/* ── mosquitto-go-auth HTTP backend ───────────────────────────────────────
   Called by the broker itself (auth_opt_http_response_mode "status" — a
   2xx/4xx status code is the whole answer, no body needed) on every MQTT
   CONNECT/SUBSCRIBE/PUBLISH. */
app.post('/auth/user', async (req, res) => {
    const ok = await verifyCredentials(req.body?.username, req.body?.password);
    res.sendStatus(ok ? 200 : 403);
});

app.post('/auth/acl', (req, res) => {
    const { username, topic } = req.body || {};
    if (!username || !topic) return res.sendStatus(403);

    /* The link-observer service account may additionally read every
       device's linkToken topic, across all usernames. */
    if (username === LINK_OBSERVER_USER && /^[^/]+\/cmd\/actual\/linkToken$/.test(topic)) {
        return res.sendStatus(200);
    }
    /* Mirrors the old /etc/mosquitto/acl's "pattern readwrite %u/#" exactly —
       every account may only read/write topics under its own username
       namespace. */
    if (topic.startsWith(`${username}/`)) return res.sendStatus(200);
    res.sendStatus(403);
});

/* ── AT-bridge relay ───────────────────────────────────────────────────────
   Raw byte pipe between a "relay" (the small script a dealer/technician runs
   next to the physical modem, forwarding its USB-CDC COM port) and one or
   more "viewers" (a browser tab open on /at-console.html) — for sending AT
   commands to a modem that's physically somewhere else, over its own
   independent internet connection rather than through the cellular modem
   itself (which may be exactly what's broken and needs debugging). This
   server only relays bytes; it doesn't parse or store anything.

   One room per account (same login used for the main site), so two
   different accounts' sessions can never see each other's traffic. Auth
   reuses verifyCredentials() — same credentials as the site login, checked
   once at connect time (WebSocket has no per-message auth, unlike HTTP). */
const atBridgeRooms = new Map(); /* username -> { relay: ws|null, viewers: Set<ws> } */

function atBridgeRoom(username) {
    let room = atBridgeRooms.get(username);
    if (!room) {
        room = { relay: null, viewers: new Set() };
        atBridgeRooms.set(username, room);
    }
    return room;
}

const wss = new WebSocketServer({ server, path: '/at-bridge' });

wss.on('connection', async (ws, req) => {
    const url = new URL(req.url, 'http://localhost');
    const username = url.searchParams.get('username');
    const password = url.searchParams.get('password');
    const role = url.searchParams.get('role'); /* "relay" or "viewer" */

    if ((role !== 'relay' && role !== 'viewer') || !(await verifyCredentials(username, password))) {
        ws.close(4001, 'unauthorized');
        return;
    }

    const room = atBridgeRoom(username);

    if (role === 'relay') {
        /* Only one relay per account at a time — a second one connecting
           (e.g. a dealer restarting their script) replaces the stale one
           rather than fighting over who's authoritative. */
        if (room.relay) room.relay.close(4002, 'replaced by a new relay connection');
        room.relay = ws;
        ws.on('message', (data) => {
            for (const viewer of room.viewers) viewer.send(data);
        });
        ws.on('close', () => { if (room.relay === ws) room.relay = null; });
    } else {
        room.viewers.add(ws);
        ws.on('message', (data) => { if (room.relay) room.relay.send(data); });
        ws.on('close', () => room.viewers.delete(ws));
    }
});

server.listen(PORT, () => console.log(`timberline-web listening on :${PORT}`));
