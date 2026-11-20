#!/usr/bin/env python3
"""Zrzuty ekranu i naciśnięcia przycisków przez debuger WebSocket PPSSPP.

Uruchom emulator z debugerem, np.:
  PPSSPPHeadless --debugger=45000 --timeout=120 build/EBOOT.PBP
i wtedy:
  tools/ppsspp_shot.py --port 45000 --out shots/ --script "wait 3; shot start; wait 8; shot panel; press right; shot wybor"

Polecenia skryptu (rozdzielone średnikami):
  wait SEKUNDY    czekaj (czas rzeczywisty)
  shot NAZWA      zapisz ekran jako NAZWA.png
  press PRZYCISK  naciśnij i puść (cross, circle, up, down, left, right, ltrigger, rtrigger, start...)
Bez zewnętrznych bibliotek: minimalny klient WebSocket.
"""

import argparse
import base64
import json
import os
import socket
import struct
import time


class WS:
    def __init__(self, host, port, path="/debugger"):
        self.s = socket.create_connection((host, port), timeout=30)
        key = base64.b64encode(os.urandom(16)).decode()
        req = (f"GET {path} HTTP/1.1\r\nHost: {host}:{port}\r\nUpgrade: websocket\r\n"
               f"Connection: Upgrade\r\nSec-WebSocket-Key: {key}\r\nSec-WebSocket-Version: 13\r\n\r\n")
        self.s.sendall(req.encode())
        resp = b""
        while b"\r\n\r\n" not in resp:
            chunk = self.s.recv(4096)
            if not chunk:
                raise ConnectionError("brak odpowiedzi na handshake WebSocket")
            resp += chunk
        if b" 101 " not in resp.split(b"\r\n", 1)[0]:
            raise ConnectionError(resp.split(b"\r\n", 1)[0].decode(errors="replace"))
        self.buf = resp.split(b"\r\n\r\n", 1)[1]

    def send(self, obj):
        data = json.dumps(obj).encode()
        mask = os.urandom(4)
        n = len(data)
        if n < 126:
            hdr = struct.pack("!BB", 0x81, 0x80 | n)
        elif n < 65536:
            hdr = struct.pack("!BBH", 0x81, 0x80 | 126, n)
        else:
            hdr = struct.pack("!BBQ", 0x81, 0x80 | 127, n)
        masked = bytes(b ^ mask[i % 4] for i, b in enumerate(data))
        self.s.sendall(hdr + mask + masked)

    def _read(self, n):
        while len(self.buf) < n:
            chunk = self.s.recv(65536)
            if not chunk:
                raise ConnectionError("połączenie zamknięte")
            self.buf += chunk
        out, self.buf = self.buf[:n], self.buf[n:]
        return out

    def recv(self):
        b1, b2 = self._read(2)
        n = b2 & 0x7F
        if n == 126:
            n = struct.unpack("!H", self._read(2))[0]
        elif n == 127:
            n = struct.unpack("!Q", self._read(8))[0]
        payload = self._read(n)
        if b1 & 0x0F == 0x8:
            raise ConnectionError("serwer zamknął WebSocket")
        if b1 & 0x0F != 0x1:
            return self.recv()
        return json.loads(payload)

    def call(self, event, **params):
        self.send({"event": event, **params})
        while True:
            msg = self.recv()
            if msg.get("event") == event or msg.get("event") == "error":
                return msg


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", type=int, default=45000)
    ap.add_argument("--out", default=".")
    ap.add_argument("--script", required=True)
    args = ap.parse_args()
    os.makedirs(args.out, exist_ok=True)

    ws = None
    for _ in range(50):  # emulator może jeszcze startować
        try:
            ws = WS("127.0.0.1", args.port)
            break
        except OSError:
            time.sleep(0.2)
    if not ws:
        raise SystemExit("nie udało się połączyć z debugerem PPSSPP")

    ws.call("cpu.resume")  # --debugger zatrzymuje emulację na starcie

    for cmd in [c.strip() for c in args.script.split(";") if c.strip()]:
        op, _, arg = cmd.partition(" ")
        if op == "wait":
            time.sleep(float(arg))
        elif op == "press":
            r = ws.call("input.buttons.press", button=arg, duration=3)
            if r.get("event") == "error":
                print("błąd:", r.get("message"))
        elif op == "shot":
            ws.call("cpu.stepping")  # zrzut wymaga zatrzymanej emulacji
            r = ws.call("gpu.buffer.screenshot", type="base64", display=True)
            ws.call("cpu.resume")
            if r.get("event") == "error" or "base64" not in r:
                print("błąd zrzutu:", r.get("message", r))
                continue
            path = os.path.join(args.out, f"{arg}.png")
            with open(path, "wb") as f:
                f.write(base64.b64decode(r["base64"]))
            print("zapisano", path, f"{r.get('width')}x{r.get('height')}")
        else:
            print("nieznane polecenie:", cmd)


if __name__ == "__main__":
    main()
