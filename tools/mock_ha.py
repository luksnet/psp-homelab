#!/usr/bin/env python3
"""Atrapa REST API Home Assistant do testów psp-homelab.

Obsługuje (z nagłówkiem Authorization: Bearer <token>):
  GET  /api/states/<entity_id>          stan encji (też group.psp z listą encji)
  POST /api/template                    tylko szablon label_entities('psp')
  GET  /api/history/period/<start>      historia (filter_entity_id, end_time)
Mówi HTTP/1.1 z keep-alive; opcja --close-after N zamyka połączenie po N
żądaniach, żeby sprawdzić wznawianie sesji TLS po stronie PSP.

Użycie:
  tools/mock_ha.py --cert server.pem --key server.key --token test-token
"""

import argparse
import json
import math
import random
import re
import ssl
import time
from datetime import datetime, timedelta, timezone
from urllib.parse import parse_qs, unquote, urlsplit
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

STATES = {
    "light.salon": ("on", {"friendly_name": "Salon swiatlo"}),
    "light.sypialnia": ("off", {"friendly_name": "Sypialnia swiatlo"}),
    "sensor.temperatura_salon": (
        "21.5",
        {"friendly_name": "Temperatura salon", "unit_of_measurement": "°C"},
    ),
    "binary_sensor.drzwi_wejsciowe": ("off", {"friendly_name": "Drzwi wejsciowe"}),
    "sensor.proxmox_cpu": ("12", {"friendly_name": "Proxmox CPU", "unit_of_measurement": "%"}),
    "switch.lab_ad": ("unavailable", {"friendly_name": "Lab AD"}),
    "sensor.nas_dysk": ("63", {"friendly_name": "NAS dysk", "unit_of_measurement": "%"}),
    "sensor.router_ping": ("4.2", {"friendly_name": "Router ping", "unit_of_measurement": "ms"}),
}

# Encje z etykietą "psp" (POST /api/template) i członkowie group.psp.
LABELS = {"psp": ["sensor.router_ping", "sensor.nas_dysk", "light.salon"]}
STATES["group.psp"] = ("on", {"friendly_name": "PSP", "entity_id": [
    "sensor.proxmox_cpu", "sensor.nas_dysk", "binary_sensor.drzwi_wejsciowe"]})
# Grupa z pomocnika w interfejsie HA: encje jednego typu, identyfikator light.*.
STATES["light.psp"] = ("on", {"friendly_name": "PSP światła", "entity_id": ["light.salon"]})


def history(entity_id, start, end):
    """Sztuczna historia: temperatura i CPU co 5 min, drzwi otwierane co ~2 h."""
    out = []
    t = start
    if entity_id == "binary_sensor.drzwi_wejsciowe":
        out.append(("off", start))
        t = start + timedelta(minutes=37)
        while t < end:
            out.append(("on", t))
            out.append(("off", t + timedelta(minutes=3)))
            t += timedelta(minutes=125)
        return out
    while t < end:
        h = t.hour + t.minute / 60
        if entity_id == "sensor.temperatura_salon":
            v = f"{21 + 1.5 * math.sin((h - 9) / 24 * 2 * math.pi) + random.uniform(-0.15, 0.15):.1f}"
        elif entity_id == "sensor.proxmox_cpu":
            v = str(max(1, int(8 + 20 * (1 if 1 <= h < 2 else 0) + random.gauss(0, 3))))
        elif entity_id in STATES:
            v = STATES[entity_id][0]
        else:
            return []
        if random.random() < 0.01:
            v = "unavailable"
        out.append((v, t))
        t += timedelta(minutes=5)
    return out


def iso(t):
    return t.astimezone(timezone.utc).isoformat()


class Handler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"
    server_version = "mock-ha/0.1"

    def log_message(self, fmt, *args):
        print(f"[mock-ha] {self.client_address[0]} {fmt % args}", flush=True)

    def send_json(self, code, payload):
        body = json.dumps(payload).encode()
        self.send_response(code)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.requests_on_conn = getattr(self, "requests_on_conn", 0) + 1
        close_after = self.server.close_after
        if close_after and self.requests_on_conn >= close_after:
            self.send_header("Connection", "close")
            self.close_connection = True
        self.end_headers()
        self.wfile.write(body)

    def authorized(self):
        if self.headers.get("Authorization") != f"Bearer {self.server.token}":
            self.send_json(401, {"message": "Unauthorized"})
            return False
        return True

    def send_text(self, code, text):
        body = text.encode()
        self.send_response(code)
        self.send_header("Content-Type", "text/plain; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def do_POST(self):
        length = int(self.headers.get("Content-Length", "0"))
        body = self.rfile.read(length)
        if not self.authorized():
            return
        if self.path != "/api/template":
            self.send_json(404, {"message": "Not found"})
            return
        if self.server.template_admin_only:
            self.send_json(401, {"message": "Unauthorized"})
            return
        tpl = json.loads(body).get("template", "")
        m = re.fullmatch(r"\{\{ label_entities\('([a-z0-9_]+)'\) \| join\(','\) \}\}", tpl)
        if not m:
            self.send_json(400, {"message": f"mock: nieobsługiwany szablon {tpl!r}"})
            return
        self.send_text(200, ",".join(LABELS.get(m.group(1), [])))

    def do_GET(self):
        if not self.authorized():
            return
        url = urlsplit(self.path)
        if url.path.startswith("/api/history/period/"):
            q = parse_qs(url.query, keep_blank_values=True)
            start = datetime.fromisoformat(unquote(url.path.rsplit("/", 1)[1]))
            end = datetime.fromisoformat(q["end_time"][0]) if "end_time" in q else datetime.now(timezone.utc)
            entity_id = q.get("filter_entity_id", [""])[0]
            points = history(entity_id, start, end)
            rows = [{"state": v, "last_changed": iso(t)} for v, t in points]
            if rows:
                rows[0] = {"entity_id": entity_id, "attributes": {}, **rows[0]}
            self.send_json(200, [rows] if rows else [])
            return
        prefix = "/api/states/"
        if not self.path.startswith(prefix):
            self.send_json(404, {"message": "Not found"})
            return
        entity_id = self.path[len(prefix):]
        if entity_id not in STATES:
            self.send_json(404, {"message": "Entity not found."})
            return
        state, attrs = STATES[entity_id]
        if entity_id == "sensor.temperatura_salon":
            state = f"{20 + random.random() * 3:.1f}"
        if entity_id == "sensor.proxmox_cpu":
            state = str(random.randint(3, 40))
        self.send_json(200, {
            "entity_id": entity_id,
            "state": state,
            "attributes": attrs,
            "last_changed": time.strftime("%Y-%m-%dT%H:%M:%S+00:00", time.gmtime()),
        })


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--cert", required=True)
    ap.add_argument("--key", required=True)
    ap.add_argument("--token", default="test-token")
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--port", type=int, default=8123)
    ap.add_argument("--template-admin-only", action="store_true",
                    help="odrzucaj /api/template kodem 401 (jak HA dla tokenu bez admina)")
    ap.add_argument("--close-after", type=int, default=0,
                    help="zamknij połączenie po N żądaniach (0 = keep-alive bez limitu)")
    args = ap.parse_args()

    ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    ctx.load_cert_chain(args.cert, args.key)
    ctx.minimum_version = ssl.TLSVersion.TLSv1_2

    srv = ThreadingHTTPServer((args.host, args.port), Handler)
    srv.token = args.token
    srv.close_after = args.close_after
    srv.template_admin_only = args.template_admin_only
    srv.socket = ctx.wrap_socket(srv.socket, server_side=True)
    print(f"[mock-ha] https://localhost:{args.port}/ (close_after={args.close_after})", flush=True)
    srv.serve_forever()


if __name__ == "__main__":
    main()
