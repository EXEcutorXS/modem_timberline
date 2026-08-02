#!/usr/bin/env bash
# Timberline VPS setup — Mosquitto + mosquitto-go-auth + the web control panel
# + TLS (Let's Encrypt via nginx). Written for Ubuntu 22.04 (that's what the
# live server actually runs, despite earlier assumptions of 24).
#
# TLS needs a domain name whose DNS A record already points at this box's IP
# — point it *before* running this script (e.g. a free DuckDNS subdomain).
# Run as root on a fresh box:
#
#   scp -r host root@<new-vps-ip>:/root/timberline-host
#   ssh root@<new-vps-ip> 'cd /root/timberline-host && bash setup.sh <domain> <email>'
#
# See README.md in this folder for the "why" behind each step, known
# gotchas, and the manual steps this script does NOT do (seeding the first
# real account, DNS/domain itself, etc).
set -euo pipefail
cd "$(dirname "$0")"

DOMAIN="${1:?Usage: bash setup.sh <domain> <email-for-letsencrypt>}"
EMAIL="${2:?Usage: bash setup.sh <domain> <email-for-letsencrypt>}"

echo "== 1/13: swapfile =="
# This box has very little RAM. Building native npm modules (better-sqlite3)
# or a Go plugin from source under memory pressure with no swap has already
# caused a real disk-I/O stall that briefly took mosquitto down with it
# (see README.md "Known incidents"). Add swap before anything memory-heavy.
if ! swapon --show | grep -q '/swapfile'; then
    fallocate -l 2G /swapfile
    chmod 600 /swapfile
    mkswap /swapfile
    swapon /swapfile
    grep -q '/swapfile' /etc/fstab || echo '/swapfile none swap sw 0 0' >> /etc/fstab
fi

echo "== 2/13: base packages =="
apt-get update -qq
DEBIAN_FRONTEND=noninteractive apt-get install -y -qq \
    mosquitto mosquitto-clients ufw unzip build-essential \
    nginx certbot python3-certbot-nginx

echo "== 3/13: Node.js (NodeSource — Ubuntu 22.04's apt version is too old) =="
if ! command -v node >/dev/null 2>&1; then
    curl -fsSL https://deb.nodesource.com/setup_20.x | bash -
    DEBIAN_FRONTEND=noninteractive apt-get install -y -qq nodejs
fi

echo "== 4/13: mosquitto-go-auth plugin (prebuilt binary) =="
# Deliberately NOT building from source: needs Go >=1.24 plus a large single-
# file C compile if you go that route too (via better-sqlite3, unrelated but
# same risk class) — both are avoidable, and building from source on this
# VPS is what caused the swap-related incident in the first place. If this
# release/arch combination ever stops matching (mosquitto ABI bump), see
# README.md "Fallback: building go-auth from source".
if [ ! -f /etc/mosquitto/go-auth.so ]; then
    curl -fsSL -o /tmp/go-auth.zip \
        https://github.com/iegomez/mosquitto-go-auth/releases/download/3.0.0/linux-amd64.zip
    unzip -oq /tmp/go-auth.zip -d /tmp/go-auth-extracted
    cp /tmp/go-auth-extracted/linux-amd64/go-auth.so /etc/mosquitto/go-auth.so
    chmod 644 /etc/mosquitto/go-auth.so
    rm -rf /tmp/go-auth.zip /tmp/go-auth-extracted
fi

echo "== 5/13: firewall =="
ufw allow OpenSSH
ufw allow 1883/tcp   # MQTT (modem + anything else speaking raw MQTT)
ufw allow 8083/tcp   # MQTT over WebSocket/TLS (browser)
ufw allow 3000/tcp   # web app direct (register/login/API) — nginx also fronts this on 80/443
ufw allow 80/tcp     # ACME HTTP-01 challenge + redirect to https
ufw allow 443/tcp    # web app over TLS
ufw --force enable

echo "== 6/13: web app =="
mkdir -p /opt/timberline-web/public
cp timberline-web/server.js timberline-web/package.json /opt/timberline-web/
cp timberline-web/public/index.html timberline-web/public/app.js /opt/timberline-web/public/
# firmware/ is user-managed content (published per device/version via
# make_firmware_crc.js, see README) — copy whatever's already staged
# locally, if anything, but don't fail the setup run if there's nothing yet.
mkdir -p /opt/timberline-web/public/firmware
cp -r timberline-web/public/firmware/. /opt/timberline-web/public/firmware/ 2>/dev/null || true
( cd /opt/timberline-web && npm install --omit=dev )
cp systemd/timberline-web.service /etc/systemd/system/timberline-web.service
systemctl daemon-reload
systemctl enable --now timberline-web
sleep 1
systemctl is-active --quiet timberline-web && echo "timberline-web is running on :3000"

echo "== 7/13: nginx + Let's Encrypt =="
sed "s/multihot\.duckdns\.org/$DOMAIN/" nginx/timberline-web.conf > /etc/nginx/sites-available/timberline-web
rm -f /etc/nginx/sites-enabled/default
ln -sf /etc/nginx/sites-available/timberline-web /etc/nginx/sites-enabled/timberline-web
nginx -t
systemctl reload nginx
# --nginx: obtains the cert AND edits the site config in place to add the
# 443/TLS server block + the 80->443 redirect. Requires DOMAIN's DNS to
# already point here (see header comment).
certbot --nginx -d "$DOMAIN" -m "$EMAIL" --agree-tos --redirect -n

echo "== 8/13: cert for mosquitto's wss listener =="
# mosquitto's own TLS (listener 8083) reads the cert directly rather than
# going through nginx — nginx only fronts the web app. Let's Encrypt's
# privkey.pem is root-only by default, so copy it somewhere the "mosquitto"
# user can read; a certbot deploy-hook keeps that copy fresh on renewal.
sed "s/multihot\.duckdns\.org/$DOMAIN/" letsencrypt/timberline-mosquitto.sh \
    > /etc/letsencrypt/renewal-hooks/deploy/timberline-mosquitto.sh
chmod +x /etc/letsencrypt/renewal-hooks/deploy/timberline-mosquitto.sh
bash /etc/letsencrypt/renewal-hooks/deploy/timberline-mosquitto.sh

echo "== 9/13: mosquitto config (TLS on 8083) =="
# Back up whatever was there before touching the live config — a bad
# auth_plugin or TLS config can stop mosquitto from starting at all.
if [ -f /etc/mosquitto/conf.d/timberline.conf ]; then
    cp /etc/mosquitto/conf.d/timberline.conf \
       "/etc/mosquitto/conf.d/timberline.conf.bak-$(date +%s)"
fi
cp mosquitto/timberline.conf /etc/mosquitto/conf.d/timberline.conf
systemctl restart mosquitto
sleep 2
if ! systemctl is-active --quiet mosquitto; then
    echo "mosquitto failed to start — check 'journalctl -u mosquitto' and" >&2
    echo "/var/log/mosquitto/mosquitto.log. Could be an auth_plugin ABI" >&2
    echo "mismatch (see README.md fallback) or a missing/unreadable cert" >&2
    echo "under /etc/mosquitto/certs/." >&2
    exit 1
fi

echo "== 10/13: verify renewal =="
certbot renew --dry-run

echo "== 11/13: log limits =="
# journald has no cap by default and will happily eat the whole disk (this
# box only has 20GB) — in practice most of that growth is internet-wide SSH
# scanning noise hitting port 22 (see README.md "Known incidents"), not
# anything this app does. mosquitto and nginx already ship their own bounded
# logrotate configs (see their conf.d files); this closes the one gap that
# doesn't have one.
mkdir -p /etc/systemd/journald.conf.d
cat > /etc/systemd/journald.conf.d/timberline-limit.conf <<'JOURNALEOF'
[Journal]
SystemMaxUse=200M
SystemMaxFileSize=50M
JOURNALEOF
systemctl restart systemd-journald
journalctl --vacuum-size=200M >/dev/null
# btmp (failed login attempts) ships with only a monthly rotation trigger —
# under sustained scanning that can still grow large before it ever fires.
# Add a size trigger on top of (not instead of) the existing monthly one.
grep -q 'maxsize 20M' /etc/logrotate.d/btmp 2>/dev/null || \
    sed -i '/monthly/a\    maxsize 20M' /etc/logrotate.d/btmp

echo "== 12/13: fail2ban =="
# Bans an IP after too many failed SSH attempts in a short window — this box
# gets constant internet-wide SSH scanning (see README.md "Known
# incidents"), which is also the main driver behind the log-volume problem
# step 11 works around. jail.local (not jail.conf, which apt overwrites on
# upgrade) enables the sshd jail with tighter-than-default numbers.
DEBIAN_FRONTEND=noninteractive apt-get install -y -qq fail2ban
cat > /etc/fail2ban/jail.local <<'F2BEOF'
[DEFAULT]
bantime = 1h
findtime = 10m
maxretry = 5

[sshd]
enabled = true
F2BEOF
systemctl enable --now fail2ban
systemctl restart fail2ban

echo "== 13/13: SSH hardening (disable password auth) =="
# Only if key-based access is already set up — never disable the only way in
# on a fresh box that doesn't have a key yet, that's a guaranteed lockout.
if [ -s /root/.ssh/authorized_keys ]; then
    mkdir -p /etc/ssh/sshd_config.d
    # Filename sorts BEFORE Ubuntu's own 50-cloud-init.conf and
    # 60-cloudimg-settings.conf on purpose. sshd keeps the FIRST value it
    # sees per directive (not the last), Include is near the top of the main
    # sshd_config too, and those two cloud-init files disagree with each
    # other (yes vs no) — so anything set anywhere else, including the main
    # sshd_config file, silently loses regardless of what it says. Found
    # this the hard way: editing sshd_config directly looked like it worked
    # (grep showed the new value, `sshd -t` passed) but `sshd -T` (the
    # actual effective config) still said `passwordauthentication yes`.
    cat > /etc/ssh/sshd_config.d/00-timberline-hardening.conf <<'SSHEOF'
PasswordAuthentication no
PermitRootLogin prohibit-password
SSHEOF
    sshd -t && systemctl reload ssh
    echo "SSH password auth disabled — key-only from now on. Verify with:"
    echo "  sshd -T | grep -i passwordauthentication   (should say 'no')"
else
    echo "No /root/.ssh/authorized_keys found — SKIPPING password-auth" >&2
    echo "disable so this script can't lock you out. Add your public key" >&2
    echo "first (ssh-copy-id, or paste it into that file by hand), confirm" >&2
    echo "you can log in with it, then apply this step's commands manually" >&2
    echo "(see README.md)." >&2
fi

cat <<EOF

== Done ==
Manual steps still needed — see README.md:
  1. Register the real device's MQTT account (POST /api/register).
  2. Point the modem at this server (SMS: server $DOMAIN,login <login>,password <pass>).
     Must be the domain, not a bare IP — the firmware's "getlink" builds an
     https:// URL from it, which needs to match the cert's hostname.
EOF
