const express = require('express');
const path = require('path');
const crypto = require('crypto');
const bcrypt = require('bcryptjs');
const Database = require('better-sqlite3');
const mqtt = require('mqtt');

const app = express();
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
app.use(express.static(path.join(__dirname, 'public')));

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
app.post('/api/register', async (req, res) => {
    const { login, password, email } = req.body || {};
    if (!login || !password) return res.status(400).json({ error: 'login and password required' });
    if (login.length < LOGIN_MIN || login.length > LOGIN_MAX)
        return res.status(400).json({ error: `login must be ${LOGIN_MIN}-${LOGIN_MAX} characters` });
    if (login.startsWith('_')) return res.status(400).json({ error: 'login cannot start with "_"' });

    if (getUserStmt.get(login)) return res.status(409).json({ error: 'login already taken' });

    const passwordHash = await bcrypt.hash(password, 10);
    insertUserStmt.run(login, passwordHash, email || null);
    res.json({ ok: true });
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

/* ── mosquitto-go-auth HTTP backend ───────────────────────────────────────
   Called by the broker itself (auth_opt_http_response_mode "status" — a
   2xx/4xx status code is the whole answer, no body needed) on every MQTT
   CONNECT/SUBSCRIBE/PUBLISH. */
app.post('/auth/user', async (req, res) => {
    const { username, password } = req.body || {};
    if (!username || !password) return res.sendStatus(403);

    const user = getUserStmt.get(username);
    if (user && await bcrypt.compare(password, user.password_hash)) return res.sendStatus(200);

    const session = getSessionStmt.get(username);
    if (session && session.expires_at > Date.now() && await bcrypt.compare(password, session.session_hash)) {
        return res.sendStatus(200);
    }
    res.sendStatus(403);
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

app.listen(PORT, () => console.log(`timberline-web listening on :${PORT}`));
