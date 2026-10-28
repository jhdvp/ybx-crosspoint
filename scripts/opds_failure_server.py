#!/usr/bin/env python3
"""Controllable OPDS endpoint for hardware acceptance of catalog timeouts.

Serves a minimal valid Atom feed on `/` so the reader can load a catalog, plus
paths that misbehave on purpose:

  /            the mode selected with --mode (default: ok)
  /ok          a complete feed, answered immediately
  /stall       accept the connection, never send a byte
  /slow        send the feed body a few bytes at a time, forever
  /loop        302 back to the same path, forever

Point the reader's OPDS server URL at one of these, e.g.
http://192.168.1.50:8099/stall. Only the Python standard library is used.

Run `python3 scripts/opds_failure_server.py --help` for options.
"""

import argparse
import socket
import sys
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

FEED = """<?xml version="1.0" encoding="utf-8"?>
<feed xmlns="http://www.w3.org/2005/Atom" xmlns:opds="http://opds-spec.org/2010/catalog">
  <id>urn:uuid:6f1d0e4e-5f7c-4b7e-9d2a-0b6f3c9a1e11</id>
  <title>OPDS failure server</title>
  <updated>2026-01-01T00:00:00Z</updated>
  <link rel="self" href="/" type="application/atom+xml"/>
  <link rel="search" href="/search?q={searchTerms}" type="application/atom+xml"/>
  <entry>
    <id>urn:uuid:0b5f8c0e-1b0d-4f2e-8b7a-2c7e0d1a3f01</id>
    <title>Stall: server never answers</title>
    <updated>2026-01-01T00:00:00Z</updated>
    <author><name>Failure server</name></author>
    <content type="text">Accepts the connection and sends nothing.</content>
    <link rel="subsection" href="/stall" type="application/atom+xml"/>
  </entry>
  <entry>
    <id>urn:uuid:8a2c4e6f-3d1b-4a5c-9e7f-1b3d5f7a9c02</id>
    <title>Slow body: bytes trickle forever</title>
    <updated>2026-01-01T00:00:00Z</updated>
    <author><name>Failure server</name></author>
    <content type="text">Sends the feed a few bytes at a time so a per-read timeout never trips.</content>
    <link rel="subsection" href="/slow" type="application/atom+xml"/>
  </entry>
  <entry>
    <id>urn:uuid:2e4a6c8e-5f7d-4b9a-8c1e-3a5c7e9b1d03</id>
    <title>Redirect loop</title>
    <updated>2026-01-01T00:00:00Z</updated>
    <author><name>Failure server</name></author>
    <content type="text">Answers every request with a redirect to itself.</content>
    <link rel="subsection" href="/loop" type="application/atom+xml"/>
  </entry>
  <entry>
    <id>urn:uuid:4c6e8a0c-7b9f-4d1b-a3c5-9e7f1b3d5f04</id>
    <title>A book whose download stalls</title>
    <updated>2026-01-01T00:00:00Z</updated>
    <author><name>Failure server</name></author>
    <content type="text">Clicking this starts a download that never finishes; Back must cancel it.</content>
    <link rel="http://opds-spec.org/acquisition/open-access" href="/stall" type="application/epub+zip"/>
  </entry>
</feed>
"""

SLOW_CHUNK = 8
SLOW_INTERVAL_S = 2.0
STALL_HOLD_S = 600.0


class Handler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"
    server_version = "OpdsFailureServer/1.0"

    def log_message(self, fmt, *args):
        sys.stderr.write("%s %s\n" % (self.address_string(), fmt % args))

    def do_GET(self):
        path = self.path.split("?", 1)[0]
        self.close_connection = True
        if path == "/":
            path = "/" + self.server.mode.replace("slow-body", "slow")
        if path == "/ok":
            self._ok()
        elif path == "/stall":
            self._stall()
        elif path == "/slow":
            self._slow()
        elif path == "/loop":
            self._loop()
        else:
            self._status(404, b"not found\n")

    def do_HEAD(self):
        self.do_GET()

    def _ok(self):
        body = FEED.encode("utf-8")
        self.send_response(200)
        self.send_header("Content-Type", "application/atom+xml; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        if self.command != "HEAD":
            self.wfile.write(body)

    def _status(self, code, body):
        self.send_response(code)
        self.send_header("Content-Type", "text/plain; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        if self.command != "HEAD":
            self.wfile.write(body)

    def _stall(self):
        # Never write: the client waits for the status line until its own
        # socket timeout or deadline fires. Hold the thread long enough to be
        # observed, then let it go so handlers cannot pile up forever.
        end = time.monotonic() + STALL_HOLD_S
        while time.monotonic() < end:
            time.sleep(1.0)

    def _slow(self):
        body = FEED.encode("utf-8")
        self.send_response(200)
        self.send_header("Content-Type", "application/atom+xml; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        try:
            for start in range(0, len(body), SLOW_CHUNK):
                self.wfile.write(body[start:start + SLOW_CHUNK])
                self.wfile.flush()
                time.sleep(SLOW_INTERVAL_S)
        except (BrokenPipeError, ConnectionResetError):
            pass

    def _loop(self):
        self.send_response(302)
        self.send_header("Location", "/loop")
        self.send_header("Content-Length", "0")
        self.end_headers()


class Server(ThreadingHTTPServer):
    daemon_threads = True
    allow_reuse_address = True

    def __init__(self, address, mode):
        self.mode = mode
        super().__init__(address, Handler)


def local_addresses():
    addresses = set()
    try:
        probe = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        probe.connect(("192.0.2.1", 9))  # no packet is sent
        addresses.add(probe.getsockname()[0])
        probe.close()
    except OSError:
        pass
    try:
        addresses.update(socket.gethostbyname_ex(socket.gethostname())[2])
    except OSError:
        pass
    return sorted(address for address in addresses if not address.startswith("127."))


def main():
    parser = argparse.ArgumentParser(description="Controllable OPDS endpoint for timeout testing.")
    parser.add_argument(
        "--mode",
        choices=["ok", "stall", "slow-body", "redirect-loop"],
        default="ok",
        help="behaviour of / (dedicated /ok /stall /slow /loop paths always exist)",
    )
    parser.add_argument("--port", type=int, default=8099, help="TCP port (default: 8099)")
    args = parser.parse_args()

    server = Server(("0.0.0.0", args.port), args.mode)
    host = server.server_address[0]
    print("OPDS failure server listening on %s:%d (mode: %s)" % (host, server.server_address[1], args.mode))
    for address in local_addresses():
        print("  http://%s:%d/%s" % (address, server.server_address[1], "" if args.mode == "ok" else args.mode))
    print("Paths: /ok /stall /slow /loop. Point the reader's OPDS URL at one of them.")
    sys.stdout.flush()
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\nstopped")
    finally:
        server.server_close()


if __name__ == "__main__":
    main()
