#!/usr/bin/env python3
"""Local-only host viewer for real FreeInkUI 1bpp C++ frames.

Security: this server is READ ONLY. Never forwards credentials or executes
arbitrary paths/commands. All argv are whitelisted and subprocess uses no shell.
"""
import argparse
import base64
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from io import BytesIO
import json
from pathlib import Path
import subprocess
from tempfile import TemporaryDirectory
from urllib.parse import parse_qs, urlsplit
from PIL import Image

HERE = Path(__file__).resolve().parent
RENDER = Path("/tmp/ybx-native-freeink-sim/ybx-native-render")
PROJECT = HERE.parents[1]
REF = PROJECT / "docs/images/ybx-reader-20261028"
OPTIONS = {
    "screen": {"sources", "pair", "settings"},
    "scenario": {"linked", "all", "empty", "offline", "unpaired", "removed",
                 "expired", "provider", "loading", "codeExpired", "approved", "denied"},
    "model": {"x3", "x4", "x4pro"},
    "orientation": {"portrait", "landscape", "portrait-inverted", "landscape-inverted"},
}
DEFAULTS = dict(screen="sources", scenario="linked", model="x4pro", orientation="portrait")
MAX_QUERY_LENGTH = 512

class Handler(BaseHTTPRequestHandler):
    def send_bytes(self, status, mime, data):
        self.send_response(status)
        self.send_header("Content-Type", mime)
        self.send_header("Content-Length", str(len(data)))
        self.send_header("Cache-Control", "no-store")
        self.send_header("X-Content-Type-Options", "nosniff")
        self.send_header("Referrer-Policy", "no-referrer")
        self.end_headers()
        self.wfile.write(data)

    def do_GET(self):
        try:
            self.route()
        except (BrokenPipeError, ConnectionResetError):
            return
        except (ValueError, subprocess.CalledProcessError, subprocess.TimeoutExpired) as ex:
            self.send_bytes(400, "application/json", json.dumps({"error": str(ex)}).encode())
        except Exception as ex:
            self.log_error("Rendering failed: %r", ex)
            self.send_bytes(500, "application/json", b'{"error":"Render failed"}')

    def route(self):
        url = urlsplit(self.path)
        if len(url.query) > MAX_QUERY_LENGTH:
            raise ValueError("query too long")
        if url.path in ("/", "/index.html"):
            return self.send_bytes(200, "text/html; charset=utf-8", (HERE / "index.html").read_bytes())
        if url.path == "/health":
            return self.send_bytes(200, "application/json", json.dumps(
                {"engine": "FreeInkUI::DisplayTarget", "binary": RENDER.exists(),
                 "scope": "host-only; NOT ESP32 firmware execution"}).encode())
        if url.path.startswith("/reference/"):
            name = url.path.removeprefix("/reference/")
            if name not in {"06-sources.png", "07-pair.png", "08-settings.png"}:
                raise ValueError("unknown reference")
            return self.send_bytes(200, "image/png", (REF / name).read_bytes())
        if url.path != "/frame":
            return self.send_bytes(404, "text/plain", b"Not found")
        query = parse_qs(url.query, keep_blank_values=True)
        if set(query) - (set(OPTIONS) | {"focus"}):
            raise ValueError("unexpected field")
        opts = DEFAULTS.copy()
        for key, values in query.items():
            if len(values) != 1:
                raise ValueError("duplicate parameter")
            if key in OPTIONS:
                if values[0] not in OPTIONS[key]:
                    raise ValueError("invalid " + key)
                opts[key] = values[0]
        focus_str = query.get("focus", ["1"])[0]
        if not focus_str.isdigit() or not (0 <= int(focus_str) <= 8):
            raise ValueError("invalid focus")
        if not RENDER.is_file():
            raise ValueError("renderer not built: run build.sh")
        with TemporaryDirectory(prefix="ybx-native-render-") as temp:
            frame = Path(temp) / "frame.pgm"
            hits = Path(temp) / "hits.json"
            args = [str(RENDER)]
            for key, value in opts.items():
                args += ["--" + key, value]
            args += ["--focus", focus_str, "--out", str(frame), "--meta", str(hits)]
            subprocess.run(args, check=True, timeout=8, stdout=subprocess.DEVNULL,
                           stderr=subprocess.PIPE, cwd=temp)
            with Image.open(frame) as img:
                # Renderer outputs PANEL-native framebuffer. Rotate it back
                # to logical UI coordinates, including true 90°/180° paths.
                angle = {"portrait": -90, "portrait-inverted": 90,
                         "landscape": 0, "landscape-inverted": 180}[opts["orientation"]]
                logical = img.rotate(angle, expand=True) if angle else img.copy()
                buf = BytesIO()
                logical.save(buf, format="PNG", optimize=True)
            body = json.loads(hits.read_text())
            body["image"] = "data:image/png;base64," + base64.b64encode(buf.getvalue()).decode()
            body["renderer"] = "Native FreeInkUI DisplayTarget / YbxAccountLayout"
        self.send_bytes(200, "application/json", json.dumps(body, ensure_ascii=False).encode())

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=8768)
    args = parser.parse_args()
    if not RENDER.is_file():
        raise SystemExit("Build first: sh tools/ybx-native-sim/build.sh")
    print("Native FreeInkUI viewer http://%s:%d/" % (args.host, args.port), flush=True)
    ThreadingHTTPServer((args.host, args.port), Handler).serve_forever()

if __name__ == "__main__":
    main()
