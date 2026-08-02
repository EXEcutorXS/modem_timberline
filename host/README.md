# Timberline server (VPS): Mosquitto + auth + web control panel

This folder is a snapshot of what's deployed on the VPS that the modem's
MQTT client and the web control panel talk to. It's kept here so the server
setup is versioned alongside the firmware, and so it can be reproduced on a
new box if needed. It is **not** wired into any CI/build — deploying is a
manual `scp` + `setup.sh` run (see below), not automatic.

Current live server: `185.238.189.192`, reachable at `multihot.duckdns.org`
(Ubuntu 22.04 LTS, 957Mi RAM, 20GB disk, 2GB swapfile).

**Portability**: nothing in `server.js` hardcodes this IP or domain.
`mqttHost` in every API response is derived from the request's own `Host`
header by default (whatever address the browser used to reach the app,
`multihot.duckdns.org` today — nginx passes the header through unchanged),
so deploying this app to a different server/IP/domain needs zero code
changes. Set the `MQTT_HOST` (and `MQTT_BROKER_URL`, `MQTT_WS_PORT`)
environment variables — see the commented-out lines in
`systemd/timberline-web.service` — only if the MQTT broker ever ends up on
a *different* host than this app; today they're co-located and the
defaults assume that.

## Layout

```
host/
  setup.sh                          # provisioning script for a fresh VPS
  mosquitto/timberline.conf         # -> /etc/mosquitto/conf.d/timberline.conf
  nginx/timberline-web.conf         # -> /etc/nginx/sites-available/ (pre-certbot template)
  letsencrypt/timberline-mosquitto.sh  # -> /etc/letsencrypt/renewal-hooks/deploy/
  systemd/timberline-web.service    # -> /etc/systemd/system/
  at-bridge-relay.py                # run by whoever is physically next to the modem
  tools/make_firmware_crc.js        # generates firmware.crc32 sidecar files, see "Firmware OTA" below
  timberline-web/                   # -> /opt/timberline-web on the server
    server.js
    package.json
    public/index.html
    public/app.js
    public/at-console.html
    public/firmware/<deviceType>/<version>/firmware.bin
    public/firmware/<deviceType>/<version>/firmware.crc32
```

## Architecture, in one paragraph

Mosquitto is the MQTT broker (port 1883 plain for the modem, 8083
WebSocket **+ TLS** for browsers). It has **no built-in password file** —
authentication is fully delegated to `mosquitto-go-auth` (a plugin,
`/etc/mosquitto/go-auth.so`) configured with only its `http` backend, which
calls back into the Node/Express app (`timberline-web`, port 3000) on
`localhost` for every connect/subscribe/publish. That same Node app is also
the end-user website: register an account (`login` + `password` — `login`
*is* the MQTT username), log in, get handed MQTT credentials, and the
browser then talks to Mosquitto **directly** over WebSocket (no proxy)
using `mqtt.js`. Accounts live in `users.db` (SQLite, via `better-sqlite3`)
on the server, not in this repo. **nginx** sits in front of the Node app
only (reverse proxy on 80/443, TLS termination for the website) — it is
not in the path for MQTT/WebSocket traffic, which mosquitto terminates
its own TLS for directly (see "TLS" below).

The modem side of this (SMS commands to point a device at a server/login/
password, MQTT topic scheme, the `getlink` magic-link flow) is documented in
the firmware itself (`Library/Sms/timberline_sms.cpp`,
`modem/User/Timberline.cpp`) — this README only covers the server half.

## Web control panel

`public/index.html` + `public/app.js` — no build step, edit and `scp` the two
files straight to `/opt/timberline-web/public/` on the VPS (static, served by
Express — no service restart needed). Besides the zone/button controls,
there are two collapsible "spoilers" (native `<details>`, collapsed by
default) for settings that aren't per-zone: **Floor heating** (setpoint,
hysteresis) and **Engine heater** (setpoint, run time). Each is only shown
once the matching hardware reports present (`floorConnected`/
`engineConnected` — same gate the Floor/Engine icon buttons use).

Slider ranges (`FLOOR_SETTINGS`/`ENGINE_SETTINGS` in `app.js`) are meant to
mirror the firmware's own validation in `Timberline.cpp`'s
`onMqttCommandReceived()` exactly — **if one changes, the other must too**,
or the slider will happily let the user pick a value the modem silently
rejects. `engineDur` (engine run time) is a real gotcha here: it's a
2-byte field on the wire (`D[4]*256+D[5]`, up to 1450 minutes, >1440 reads
as "unlimited" — same convention as `SystemTimeLimitHours`), so
`engineDurationMinutes` in `Timberline.h` has to stay a `uint16_t`, not
`uint8_t` — it silently wrapped before this was caught.

**Desired vs. confirmed, and why the UI tracks both**: publishing to
`cmd/desired/<topic>` doesn't mean the device applied it — the message
could get dropped, or the device could reject it. Early versions of this
UI updated the slider/drum optimistically and had no way to show the user
when a change silently didn't take effect. Now every setting slider and
zone drum (day/night/fan) tracks the value the user just set (`desiredValues`
in `app.js`) separately from the last confirmed value the device actually
echoed back over `cmd/actual/<topic>`:
- Sliders: green `accent-color` once confirmed, blue while only "desired"
  (pending) — see `.setting-row input[type=range].pending` in
  `index.html`.
- Drums: the reel window pulses (reuses the `conn-dot`'s `conn-pulse`
  keyframes) while pending, holds steady once confirmed.

Both clear back to normal the moment the device's echoed value matches what
was requested — there's no timeout, just "confirmed" vs "not yet."

## AT-bridge relay (remote AT console)

For sending raw AT commands to a modem that's physically somewhere else
(e.g. at a dealer/reseller), over an internet connection that isn't the
modem's own cellular link — useful precisely when the cellular link itself
is what needs debugging, so a solution that tunnels through it would be
useless exactly when needed.

Three pieces:
- **`server.js`**'s `/at-bridge` WebSocket endpoint — a plain byte relay, one
  room per account (same login as the main site). Doesn't parse or store
  anything; `role=relay` and `role=viewer` connections in the same room just
  get piped to each other.
- **`at-bridge-relay.py`** — a small script the person physically next to
  the modem runs on their own computer (Windows: `pip install pyserial
  websocket-client`, then `python at-bridge-relay.py COM5 <username>
  <password>`). Opens the modem's USB-CDC COM port and relays bytes to/from
  the server above.
- **`public/at-console.html`**— the viewer side: log in with the same
  account, get a live raw terminal. Type `b` first to put the modem's own
  USB console into AT bridge mode (see `usb_process_line()`'s `b`/`B`
  command in the firmware — forwards every byte straight to the cellular
  module's AT UART); `+++` exits it, same as any other terminal session on
  that console.

Auth reuses `verifyCredentials()` (same check as `/auth/user` and the login
form) — checked once at WebSocket connect time, not per-message. `nginx`
needs the `Upgrade`/`Connection` headers for this one path specifically
(see `nginx/timberline-web.conf`) — everything else on this site is plain
HTTP through nginx (MQTT's own wss on 8083 bypasses nginx entirely).

## Firmware OTA (MBC-2)

The modem downloads a firmware image for the MBC-2 module it talks to over
CAN, from this server, over plain HTTP (`AT+HTTPREAD`) — no MQTT involved.
It stages the image page-by-page in its own spare flash, verifying each
page's CRC32 before writing (and skipping any page whose flash contents
already match — makes an interrupted download resumable, and re-running an
`otaStart` for the same version a no-op past the first successful run), then
replays the CAN bootloader protocol onto MBC-2 itself. See
`modem/User/Modem.cpp`/`Timberline.cpp` for the modem side.

**Version naming**: this org's device version scheme is 8 dot-separated
bytes; for firmware directories only the first 4 are used, matching what
the CAN bootloader itself reports on request (OmniProtocol PGN=6, param 18 —
`VER_PRODUCT_TYPE.VER_VOLTAGE.VER_PRODUCT_SUBTYPE.VER_ASSEMBLAGE_NUMBER`).
For MBC-2 (device type 125, `VER_VOLTAGE` unused/always 0) that looks like
`125.0.0.15`. Using the bootloader's own reported tuple as the directory
name means "what version is currently on the device" and "what's the latest
available" are always directly comparable strings, no separate mapping.

Publishing a new version:
```bash
mkdir -p host/timberline-web/public/firmware/mbc2/125.0.0.15
cp your-built-firmware.bin host/timberline-web/public/firmware/mbc2/125.0.0.15/firmware.bin
node host/tools/make_firmware_crc.js host/timberline-web/public/firmware/mbc2/125.0.0.15/firmware.bin
scp -r host/timberline-web/public/firmware/mbc2/125.0.0.15 user@vps:/opt/timberline-web/public/firmware/mbc2/
```
`make_firmware_crc.js` pads `firmware.bin` in place to a multiple of 2048
bytes (0xFF filler — so the modem's last downloaded page is always exactly
2048 bytes, no special-casing a short one) and writes `firmware.crc32` next
to it: one little-endian uint32 CRC32 per page, same algorithm
(`Flash_C::crc32OtaPage()` in `Library/Flash/flash.cpp`) the modem uses to
verify what it downloaded. This CRC32 is only for the HTTP download's
integrity — it has nothing to do with the separate checksum MBC-2's own CAN
bootloader computes during the actual flashing step.

Files under `public/firmware/` are served by the same unauthenticated
`express.static` as everything else in `public/` — no login, same as
`index.html`/`app.js` today. The version string a device requests
(`cmd/desired/otaStart` payload) becomes the `<version>` path segment
verbatim, so keep it filesystem-safe (no `/`, no `..`).

**Listing available versions**: `GET /firmware/<type>/versions` (added in
`server.js`, since static file serving alone doesn't do directory listings)
returns `{"versions": ["125.0.0.15", ...]}` — every subdirectory under
`public/firmware/<type>/` that actually has both `firmware.bin` and
`firmware.crc32`, sorted. No auth, same as the files themselves. E.g.:
```bash
curl https://multihot.duckdns.org/firmware/mbc2/versions
```

## TLS (Let's Encrypt)

Requires a domain name whose DNS A record points at the VPS — Let's Encrypt
does not issue certificates for bare IPs. The live server uses a free
DuckDNS subdomain (`multihot.duckdns.org`); any domain/dynamic-DNS provider
works the same way.

Two independent TLS terminations share the one certificate:
- **The website** (port 443): nginx reverse-proxies to the Node app on
  `localhost:3000`, obtained/configured by `certbot --nginx` (edits the
  nginx site config in place, adds the 80→443 redirect, sets up the
  `certbot.timer` auto-renewal systemd timer).
- **MQTT WebSocket** (port 8083, now `wss://` not `ws://`): mosquitto reads
  the cert directly via `certfile`/`keyfile` in `mosquitto/timberline.conf`
  — it does **not** go through nginx, preserving the existing "browser
  talks to mosquitto directly" architecture. Let's Encrypt's `privkey.pem`
  is root-only by default and mosquitto runs as its own user, so
  `letsencrypt/timberline-mosquitto.sh` (installed as a certbot
  **deploy-hook**, runs after every issue/renewal) copies both cert files
  into `/etc/mosquitto/certs/`, `chown`s them to the `mosquitto` user, and
  reloads mosquitto.
- Plain MQTT on **1883 stays unencrypted** — that's the modem's own raw
  connection, and the SIMCOM AT-command set this firmware uses has no TLS
  support wired up. Only the browser-facing pieces (website, WebSocket) got
  TLS.

**Consequence for `mqttBroker`**: the firmware's `getlink` SMS command
builds a link as `"https://" + mqttBroker + "/go/..."` (`Timberline.cpp`).
For that URL to present a valid certificate, `mqttBroker` must be set to
the **domain**, not a bare IP — mismatched hostnames get a browser TLS
warning. New registrations via the website already embed the domain
automatically (`app.js` uses `location.hostname`); an **already-configured
device still pointed at the bare IP** needs a one-time
`server multihot.duckdns.org` SMS to fix its `getlink` links going forward.
The modem's own raw MQTT connection (port 1883) works the same either way,
since 1883 doesn't care about hostnames vs. IPs or certs.

## Running `setup.sh` on a fresh VPS

```bash
scp -r host root@<new-vps-ip>:/root/timberline-host
ssh root@<new-vps-ip> 'cd /root/timberline-host && bash setup.sh <domain> <email>'
```

`<domain>`'s DNS must already point at the new VPS's IP before running this
— TLS setup (`certbot --nginx`) needs it reachable. `<email>` is used only
for Let's Encrypt renewal-failure notifications.

It installs Node.js, Mosquitto, the go-auth plugin (prebuilt binary — see
"Known incidents" below for why not built from source), nginx + certbot,
a firewall, the web app as a systemd service, and TLS end-to-end (website +
MQTT WebSocket). It is mostly idempotent (checks before reinstalling
Node/the plugin/swap) but **does back up and overwrite**
`/etc/mosquitto/conf.d/timberline.conf` and the nginx site config — if
you've hand-tuned either on a running server, diff before rerunning.

### Manual steps `setup.sh` does not do

1. **Point the domain's DNS at the VPS** before running the script (a free
   dynamic-DNS provider like DuckDNS works fine — no need to own/buy a
   domain).
2. **Seed the first real account** (the script only sets up empty
   infrastructure):
   ```bash
   curl -X POST http://localhost:3000/api/register \
     -H 'Content-Type: application/json' \
     -d '{"login":"<mqtt-username>","password":"<a-real-password>"}'
   ```
3. **Point a device at this server** via SMS (see
   `Library/Sms/timberline_sms.cpp`):
   `server <domain>,login <mqtt-username>,password <a-real-password>`
   (matches whatever you registered in step 2 — must be the domain, not the
   IP, see "TLS" above).

## Known incidents / gotchas (read before touching this in production)

- **`mosquitto-go-auth`'s `files` backend doesn't work with this
  mosquitto's password hashes.** `mosquitto_passwd` on Mosquitto 2.0.11
  writes `$7$<iterations>$<salt>$<hash>` (PBKDF2-SHA512 format version 7);
  the plugin's `files` backend didn't validate it, rejecting *every*
  connection including already-working ones — a real (brief) outage during
  rollout. Fixed by not using the `files` backend at all: every account,
  including the original hardware device, is a normal row in `users.db`,
  checked via the `http` backend only. The `mosquitto-go-auth` GitHub repo
  is **archived** (read-only, no more fixes) as of 2025-06-08, so this isn't
  worth re-attempting.
- **This VPS has very little RAM and, originally, no swap.** Building
  `better-sqlite3` (needs a C++ compiler + a large single-file C compile)
  under memory pressure with no swap once caused an ext4 journal commit
  stall (`D`-state / uninterruptible sleep) that **took mosquitto down with
  it** for a couple of minutes. `setup.sh` adds a 2GB swapfile as step 1,
  specifically to avoid repeating this. If you ever see a process stuck in
  `D` state on this box: don't `kill -9` (it won't work anyway), don't pile
  on more disk I/O, just wait — it has always resolved itself within a few
  minutes once observed.
- **The go-auth plugin is a prebuilt binary on purpose.** Building it from
  source needs Go ≥1.24 (not in Ubuntu 22.04's apt), and is exactly the kind
  of heavy build that caused the incident above. The release's
  `linux-amd64.zip` prebuilt `go-auth.so` loads fine against this
  mosquitto/libc — if a future mosquitto upgrade breaks plugin ABI
  compatibility (mosquitto logs a clear "unsupported auth plugin version"
  and simply refuses to start — a safe failure mode, not silent corruption),
  the fallback is: add swap first (already done), install Go via the
  official tarball (apt's is too old), `git clone` the repo, `make`.
- **`journald` has no size cap by default and this box only has 20GB total.**
  Found it sitting at ~900MB with no ceiling set — most of that turned out to
  be internet-wide SSH scanning noise (thousands of "Failed password" /
  invalid-user attempts a week, entirely normal for any public IP), not
  anything the app itself logs (`server.js` logs almost nothing in its
  request-handling paths). `mosquitto` and `nginx` already ship their own
  bounded `logrotate` configs; `journald` and `/etc/logrotate.d/btmp` (failed
  logins, only rotated monthly by default) didn't. `setup.sh` step 11 sets an
  explicit `SystemMaxUse=200M` journald cap (via a drop-in at
  `/etc/systemd/journald.conf.d/timberline-limit.conf`) and adds a
  `maxsize 20M` trigger to `/etc/logrotate.d/btmp` so it rotates sooner under
  sustained scanning instead of waiting a full month. This is log-volume
  hygiene only — it does **not** address the scanning itself; `fail2ban` +
  disabling SSH password auth is a separate, not-yet-done step for that (see
  chat history, not in this script). `setup.sh` step 12 addresses the
  scanning itself: installs `fail2ban` with an `sshd` jail (`jail.local`, not
  `jail.conf` — apt overwrites the latter on upgrade), banning an IP for 1h
  after 5 failed attempts in 10 minutes. This is independent of whether SSH
  password auth is enabled or not — it works either way, and doesn't change
  how anyone currently logs in.
- **Disabling SSH password auth needs a specific file, not just editing
  `sshd_config`.** Ubuntu's cloud images ship `Include
  /etc/ssh/sshd_config.d/*.conf` near the top of the main `sshd_config`, and
  that directory already contains `50-cloud-init.conf`
  (`PasswordAuthentication yes`) and `60-cloudimg-settings.conf`
  (`PasswordAuthentication no`) — they disagree with each other, and sshd
  keeps the **first** value it sees for a directive (not the last), so
  whichever of those two sorts first wins over everything else, including
  edits made directly to `sshd_config` itself. Editing `sshd_config` looked
  like it worked (`grep` showed the new value, `sshd -t` passed clean) but
  `sshd -T` — the actual resolved effective config — still said
  `passwordauthentication yes`. Fix: drop a file that sorts before `50-`,
  e.g. `/etc/ssh/sshd_config.d/00-timberline-hardening.conf`, with
  `PasswordAuthentication no` and `PermitRootLogin prohibit-password`. Always
  verify with `sshd -T | grep -i passwordauthentication` after changing
  anything here — `grep`-ing the files themselves isn't proof of the
  effective result. `setup.sh` step 13 does this, but only if
  `/root/.ssh/authorized_keys` already has a key — otherwise it skips and
  warns, rather than risk locking out a fresh box that has no key yet.
- **Frontend gotcha**: `public/index.html` loads `/app.js` with an
  **absolute** path on purpose. A relative `src="app.js"` breaks on the
  `/go/:username/:token` magic-link page — the browser resolves it as
  `/go/<username>/app.js`, which matches the same Express route pattern and
  serves `index.html` again instead of the real script.

## Known limitations (all deliberate, for the current single/few-user stage)

- No server-side sessions — login/magic-link just hands the browser MQTT
  credentials directly.
- MQTT session passwords (from magic links) are stored/transmitted the same
  way as everything else here — fine for now, not for real multi-tenant
  production use.
