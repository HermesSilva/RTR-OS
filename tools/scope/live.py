#!/usr/bin/env python3
"""RTR-OS - live virtual oscilloscope for the emulator.

Connects to the rtr-scope probe inside qemu-pi4 (a TCP character device that
streams "<ns> <pin> <level>" lines as the GPIO pins change), keeps the
recent transitions, and serves the oscilloscope page on a local HTTP port.
The page polls the new transitions and draws them as they arrive.

Usage: live.py [--probe host:port] [--listen port] [--vcd file]
  --probe   where the probe listens (default 127.0.0.1:5555)
  --listen  local port of the oscilloscope page (default 8090)
  --vcd     also write every transition to a VCD file (for PulseView or GTKWave)
"""
import argparse
import http.server
import json
import os
import socket
import sys
import threading
import time
from collections import deque

EVENTS_MAX = 2_000_000
PINS = 58


class Capture:
    """Transitions received from the probe, with a sequence number per event."""

    def __init__(self, vcd_path=None):
        self.lock = threading.Lock()
        self.events = deque(maxlen=EVENTS_MAX)   # (seq, t, pin, level)
        self.seq = 0
        self.level = [None] * PINS
        self.connected = False
        self.received = 0
        self.vcd = None
        if vcd_path:
            self.vcd = open(vcd_path, "w", encoding="ascii")
            self.vcd.write("$timescale 1 ns $end\n$scope module gpio $end\n")
            for pin in range(PINS):
                self.vcd.write(f"$var wire 1 {self.vcd_id(pin)} gpio{pin} $end\n")
            self.vcd.write("$upscope $end\n$enddefinitions $end\n")
            self.vcd_time = -1

    @staticmethod
    def vcd_id(pin):
        return chr(33 + pin)

    def add(self, t, pin, level):
        with self.lock:
            self.seq += 1
            self.events.append((self.seq, t, pin, level))
            self.level[pin] = level
            self.received += 1
            if self.vcd:
                if t != self.vcd_time:
                    self.vcd.write(f"#{t}\n")
                    self.vcd_time = t
                self.vcd.write(f"{level}{self.vcd_id(pin)}\n")

    def since(self, after, limit=200_000):
        with self.lock:
            out = []
            for seq, t, pin, level in reversed(self.events):
                if seq <= after:
                    break
                out.append((t, pin, level))
                if len(out) >= limit:
                    break
            out.reverse()
            return {"seq": self.seq, "events": out, "connected": self.connected,
                    "received": self.received, "levels": self.level}


def reader(capture, host, port):
    """Keeps a connection to the probe, asking for a snapshot on every (re)connection."""
    while True:
        try:
            with socket.create_connection((host, port), timeout=5) as sock:
                sock.settimeout(None)
                sock.sendall(b"?\n")
                capture.connected = True
                buffer = b""
                while True:
                    chunk = sock.recv(65536)
                    if not chunk:
                        break
                    buffer += chunk
                    lines = buffer.split(b"\n")
                    buffer = lines.pop()
                    for line in lines:
                        parts = line.split()
                        if len(parts) == 3:
                            try:
                                capture.add(int(parts[0]), int(parts[1]), int(parts[2]))
                            except ValueError:
                                pass
        except OSError:
            pass
        capture.connected = False
        time.sleep(1)


def make_handler(capture, page):
    class Handler(http.server.BaseHTTPRequestHandler):
        def log_message(self, *args):
            pass

        def send(self, status, body, content_type):
            self.send_response(status)
            self.send_header("Content-Type", content_type)
            self.send_header("Content-Length", str(len(body)))
            self.send_header("Cache-Control", "no-store")
            self.end_headers()
            self.wfile.write(body)

        def do_GET(self):
            if self.path.startswith("/api/events"):
                after = 0
                if "after=" in self.path:
                    try:
                        after = int(self.path.split("after=")[1].split("&")[0])
                    except ValueError:
                        after = 0
                self.send(200, json.dumps(capture.since(after)).encode(), "application/json")
            elif self.path == "/" or self.path.startswith("/?"):
                self.send(200, page, "text/html; charset=utf-8")
            else:
                self.send(404, b"No such route.\n", "text/plain")
    return Handler


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--probe", default="127.0.0.1:5555")
    parser.add_argument("--listen", type=int, default=8090)
    parser.add_argument("--vcd")
    args = parser.parse_args()

    host, _, port = args.probe.rpartition(":")
    capture = Capture(args.vcd)
    threading.Thread(target=reader, args=(capture, host or "127.0.0.1", int(port)), daemon=True).start()

    with open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "live.html"), "rb") as f:
        page = f.read()
    server = http.server.ThreadingHTTPServer(("127.0.0.1", args.listen), make_handler(capture, page))
    print(f"RTR-OS virtual oscilloscope: http://localhost:{args.listen}/  (probe at {args.probe})")
    if args.vcd:
        print(f"VCD export: {args.vcd}")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    if capture.vcd:
        capture.vcd.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
