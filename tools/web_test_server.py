#!/usr/bin/env python3
"""Tiny test server that serves index.html and a mock /state JSON.

Mocks the espAGC HTTP API surface so we can browser-test the web DSKY
without needing the ESP32 hardware online. The state cycles through a
short script (boot blank → V35E digits → V37E00E digits) so the polling
animation can be verified visually.

Usage:
  py tools/web_test_server.py             # listens on 127.0.0.1:8765
  py tools/web_test_server.py --port 9000
"""
import argparse
import json
import threading
import time
from http.server import BaseHTTPRequestHandler, HTTPServer
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
INDEX = REPO / 'components' / 'dsky_input' / 'web' / 'index.html'

# State script: list of (label, state_dict). Each entry holds for ~1.5s,
# producing a visible animation when the web client polls at 5 Hz.
STATES = [
    # Boot — everything blank, PROG ALARM lit (canonical post-RSET pattern).
    ('boot', {
        "prog":"__", "verb":"__", "noun":"__",
        "r1":"_____", "r2":"_____", "r3":"_____",
        "ca":0, "up":0, "temp":0, "noatt":0, "gl":0, "pa":1,
        "rstr":1, "trk":0, "krel":0, "oe":0, "stby":0, "fvn":0,
    }),
    # V35E lamp test — every digit "8", all lamps on.
    ('lamp_test', {
        "prog":"88", "verb":"88", "noun":"88",
        "r1":"+88888", "r2":"-88888", "r3":"+88888",
        "ca":1, "up":1, "temp":1, "noatt":1, "gl":1, "pa":1,
        "rstr":1, "trk":1, "krel":1, "oe":1, "stby":1, "fvn":1,
    }),
    # V37E00E succeeded — PROG=00, VERB=37, NOUN flashing.
    ('p00_loaded', {
        "prog":"00", "verb":"37", "noun":"__",
        "r1":"_____", "r2":"_____", "r3":"_____",
        "ca":1, "up":0, "temp":0, "noatt":0, "gl":0, "pa":0,
        "rstr":0, "trk":0, "krel":0, "oe":0, "stby":0, "fvn":1,
    }),
    # V16N36 displaying time — three +0 registers.
    ('time_display', {
        "prog":"00", "verb":"16", "noun":"36",
        "r1":"+00012", "r2":"+00034", "r3":"+00056",
        "ca":1, "up":0, "temp":0, "noatt":0, "gl":0, "pa":0,
        "rstr":0, "trk":0, "krel":0, "oe":0, "stby":0, "fvn":0,
    }),
]

g_idx = 0
g_gen = 0
g_lock = threading.Lock()

def advance_state():
    global g_idx, g_gen
    while True:
        time.sleep(1.5)
        with g_lock:
            g_idx = (g_idx + 1) % len(STATES)
            g_gen += 1
            print(f"  >> state advanced: idx={g_idx} ({STATES[g_idx][0]}) gen={g_gen}")

class Handler(BaseHTTPRequestHandler):
    def log_message(self, fmt, *args): pass     # quiet
    def do_GET(self):
        if self.path == '/' or self.path == '/index.html':
            body = INDEX.read_bytes()
            self.send_response(200)
            self.send_header('Content-Type', 'text/html; charset=utf-8')
            self.send_header('Content-Length', str(len(body)))
            self.end_headers()
            self.wfile.write(body)
        elif self.path == '/state':
            with g_lock:
                payload = {"gen": g_gen, **STATES[g_idx][1]}
            body = json.dumps(payload).encode()
            self.send_response(200)
            self.send_header('Content-Type', 'application/json')
            self.send_header('Cache-Control', 'no-store')
            self.send_header('Content-Length', str(len(body)))
            self.end_headers()
            self.wfile.write(body)
        elif self.path == '/seqs':
            body = b'[]'
            self.send_response(200)
            self.send_header('Content-Type', 'application/json')
            self.send_header('Content-Length', str(len(body)))
            self.end_headers()
            self.wfile.write(body)
        else:
            self.send_error(404)
    def do_POST(self):
        if self.path == '/key':
            n = int(self.headers.get('Content-Length', '0'))
            body = self.rfile.read(n).decode() if n else ''
            print(f"  >> /key {body!r}")
            self.send_response(200)
            self.send_header('Content-Type', 'text/plain')
            self.end_headers()
            self.wfile.write(b'ok')
        else:
            self.send_error(404)

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--port', type=int, default=8765)
    args = ap.parse_args()
    if not INDEX.exists():
        raise SystemExit(f"missing {INDEX}")
    threading.Thread(target=advance_state, daemon=True).start()
    server = HTTPServer(('127.0.0.1', args.port), Handler)
    print(f"web test server: http://127.0.0.1:{args.port}/")
    print(f"  /state cycles every 1.5s through: {[s[0] for s in STATES]}")
    server.serve_forever()

if __name__ == '__main__':
    main()
