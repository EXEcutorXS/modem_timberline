#!/usr/bin/env bash
# certbot deploy-hook: copies the domain's cert into a location the
# "mosquitto" user can read (the letsencrypt live/ dir's privkey.pem is
# root-only by default) and reloads mosquitto so its wss listener (8083)
# picks up the renewed cert. Install at
# /etc/letsencrypt/renewal-hooks/deploy/timberline-mosquitto.sh — certbot
# runs every script in that directory after each successful issue/renewal.
set -euo pipefail

# The cert *lineage* name, not necessarily the only hostname it covers —
# additional domains (e.g. multihot.online) get added as SANs to this same
# lineage via `certbot --expand` rather than as a separate certificate, so
# this stays the one path to update regardless of how many domains the
# site answers to. See host/README.md's TLS section.
CERT_DOMAIN="multihot.duckdns.org"
SRC="/etc/letsencrypt/live/$CERT_DOMAIN"
DST="/etc/mosquitto/certs"

mkdir -p "$DST"
cp "$SRC/fullchain.pem" "$DST/fullchain.pem"
cp "$SRC/privkey.pem" "$DST/privkey.pem"
chown mosquitto:mosquitto "$DST/fullchain.pem" "$DST/privkey.pem"
chmod 600 "$DST/fullchain.pem" "$DST/privkey.pem"

systemctl reload mosquitto || systemctl restart mosquitto
