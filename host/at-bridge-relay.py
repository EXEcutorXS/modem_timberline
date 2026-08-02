#!/usr/bin/env python3
"""
AT-bridge relay — run this on the computer physically connected to the
modem's USB port (e.g. at the dealer's site). It forwards raw bytes
between that COM port and the AT-console web page (at-console.html) over
the internet, so someone elsewhere can send AT commands to the modem's
own AT/debug USB console as if they were plugged in locally.

This does NOT go through the modem's cellular connection at all — it only
needs *this computer's* own internet access, which matters if the thing
being debugged is the cellular link itself.

For a full step-by-step setup guide (Python install, PATH checkbox,
finding the COM port, troubleshooting) see AT-BRIDGE-SETUP.md in this
same folder — send that file to whoever is running this on-site.

Quick reference (Windows):
    pip install pyserial websocket-client
    python at-bridge-relay.py COM5 myusername mypassword

Find the COM port in Device Manager -> Ports (COM & LPT) once the modem
is plugged in via USB. username/password are the same login used on the
main control panel website.

Once connected, open https://multihot.duckdns.org/at-console.html in a
browser (from anywhere), log in with the same username/password, and type
"b" first to put the modem's USB console into AT bridge mode (see the
modem firmware's usb_process_line() "b"/"B" command) — "+++" exits it.
"""
import sys
import threading

import serial
import websocket

SERVER = "wss://multihot.duckdns.org/at-bridge"


def main():
    if len(sys.argv) != 4:
        print(f"usage: {sys.argv[0]} <COM_PORT> <username> <password>")
        print(f"example: {sys.argv[0]} COM5 myusername mypassword")
        sys.exit(1)

    port, username, password = sys.argv[1], sys.argv[2], sys.argv[3]

    ser = serial.Serial(port, baudrate=115200, timeout=0.1)
    print(f"Opened {port}. Connecting to {SERVER} ...")

    url = f"{SERVER}?role=relay&username={username}&password={password}"
    ws = websocket.WebSocketApp(
        url,
        on_open=lambda w: print("Connected — relaying. Ctrl+C to stop."),
        on_close=lambda w, code, msg: print(f"Disconnected ({code}: {msg})"),
        on_error=lambda w, err: print(f"WebSocket error: {err}"),
        on_message=lambda w, data: ser.write(data if isinstance(data, bytes) else data.encode()),
    )

    def pump_serial():
        while True:
            chunk = ser.read(256)
            if chunk:
                try:
                    ws.send(chunk, opcode=websocket.ABNF.OPCODE_BINARY)
                except Exception:
                    pass  # ws not connected yet/anymore — drop, next chunk will retry once it is

    threading.Thread(target=pump_serial, daemon=True).start()

    while True:
        try:
            ws.run_forever(reconnect=5)
        except KeyboardInterrupt:
            break


if __name__ == "__main__":
    main()
