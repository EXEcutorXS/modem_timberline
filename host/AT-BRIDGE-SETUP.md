# AT Bridge — Setup Guide

This lets us send AT commands to the modem remotely, over your own internet
connection, while it's plugged into your PC via USB. It does **not** use the
modem's cellular/SIM connection at all — that's on purpose, in case the
cellular side itself is what we're troubleshooting.

You'll need about 10 minutes and one file we'll send you (`at-bridge-relay.py`).

## 1. Install Python

1. Go to [python.org/downloads](https://www.python.org/downloads/) and download
   the latest Python 3 installer for Windows.
2. Run the installer. **On the very first screen, check the box at the
   bottom that says "Add python.exe to PATH"** before clicking Install.
   This step is easy to miss and is the most common reason things don't
   work afterward — if you skip it, Windows won't be able to find the
   `python` and `pip` commands.
3. Once installation finishes, open a **new** Command Prompt (press
   `Win`, type `cmd`, press Enter) and check it worked:
   ```
   python --version
   ```
   You should see something like `Python 3.12.x`. If you get an error like
   "not recognized", Python wasn't added to PATH — re-run the installer,
   choose "Modify", and enable that checkbox.

## 2. Install the two required packages

In the same Command Prompt:
```
pip install pyserial websocket-client
```

## 3. Connect the modem and find its COM port

1. Plug the modem into a USB port on your PC.
2. Open **Device Manager** (right-click the Start button → Device Manager).
3. Expand **Ports (COM & LPT)**. You should see an entry like
   `USB Serial Device (COM5)` — note the COM number (yours may differ).

## 4. Run the relay script

Put `at-bridge-relay.py` somewhere convenient (e.g. your Desktop), then in
Command Prompt, `cd` to that folder and run:
```
python at-bridge-relay.py COM5 <username> <password>
```
Replace `COM5` with the port number from step 3, and `<username>`/
`<password>` with the login we gave you (same one used on the control panel
website).

You should see:
```
Opened COM5. Connecting to wss://multihot.duckdns.org/at-bridge ...
Connected — relaying. Ctrl+C to stop.
```
Leave this window open — closing it disconnects the bridge. Press `Ctrl+C`
in that window whenever you want to stop.

## 5. We take it from here

Once you see "Connected — relaying", let us know — we'll open the web
console on our side and send `b` first to switch the modem's USB console
into AT bridge mode (this is a one-time step per session; sending `+++`
exits it). From that point on, any AT commands we send will go straight to
the modem through your PC, and you'll see the modem's own replies too.

## Troubleshooting

- **`python` not recognized** — Python wasn't added to PATH during install;
  re-run the installer and choose "Modify" → enable "Add python.exe to PATH".
- **`pip install` fails / "Access is denied"** — try running Command Prompt
  as Administrator (right-click → "Run as administrator").
- **Script can't open the COM port ("Access is denied" / "PermissionError")**
  — another program (e.g. a terminal app you had open) is already using that
  port. Close it and try again.
- **Script prints "Disconnected" repeatedly** — check your internet
  connection; the script will keep retrying automatically every 5 seconds,
  no need to restart it.
