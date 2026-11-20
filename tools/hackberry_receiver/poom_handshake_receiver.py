#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 THE POOM
#
# POOM handshake receiver for the Hackberry Pi (CM5 cracking rig).
#
# Pairs with the POOM "HS VAULT" app: the POOM joins the LAN and does
#     POST http://<pi>:<port>/upload?name=<file>.22000
# with the raw hashcat .22000 as the body (Content-Type: application/octet-stream).
# This listener saves the capture and auto-launches `hashcat -m 22000` against a
# wordlist, logging the run.
#
# Scope: this is for cracking handshakes YOU captured on YOUR OWN gear, or in a
# consented CTF. Don't point it at third-party captures.
#
# Dependencies: Python 3 standard library only. hashcat must be installed and on
# PATH (or set HASHCAT_BIN). Run:  python3 poom_handshake_receiver.py

import json
import os
import re
import subprocess
import sys
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import urlparse, parse_qs

# ----------------------------------------------------------------------------
# Config — override any of these via environment variables.
# ----------------------------------------------------------------------------
HOST = os.environ.get("POOM_RX_HOST", "0.0.0.0")
PORT = int(os.environ.get("POOM_RX_PORT", "8080"))
SAVE_DIR = os.path.expanduser(os.environ.get("POOM_RX_DIR", "~/poom_handshakes"))
WORDLIST = os.path.expanduser(
    os.environ.get("POOM_RX_WORDLIST", "/usr/share/wordlists/rockyou.txt")
)
HASHCAT_BIN = os.environ.get("POOM_RX_HASHCAT", "hashcat")
# Extra hashcat args, space-separated (e.g. "-w 3 -O"). Empty by default.
HASHCAT_EXTRA = os.environ.get("POOM_RX_HASHCAT_EXTRA", "").split()
# Potfile holds cracked results; `hashcat --show` reads it back later.
POTFILE = os.path.expanduser(os.environ.get("POOM_RX_POTFILE", "~/poom_handshakes/poom.potfile"))
AUTO_CRACK = os.environ.get("POOM_RX_AUTOCRACK", "1") != "0"
MAX_UPLOAD = int(os.environ.get("POOM_RX_MAX_BYTES", str(1 * 1024 * 1024)))  # 1 MiB cap

# Only accept simple capture filenames; never trust the client's path.
_SAFE_NAME = re.compile(r"^[A-Za-z0-9._-]{1,96}$")


def _safe_name(raw):
    """Reduce the client-supplied name to a safe basename ending in .22000."""
    base = os.path.basename(raw or "")
    if not base or not _SAFE_NAME.match(base):
        base = "capture_%d.22000" % int(time.time())
    if not base.endswith(".22000"):
        base += ".22000"
    return base


def _unique_path(directory, name):
    """Never overwrite an existing capture; suffix with a counter if needed."""
    path = os.path.join(directory, name)
    if not os.path.exists(path):
        return path
    stem = name[: -len(".22000")]
    i = 1
    while True:
        cand = os.path.join(directory, "%s_%d.22000" % (stem, i))
        if not os.path.exists(cand):
            return cand
        i += 1


def _launch_hashcat(capture_path):
    """Kick off a background hashcat run; return the log path."""
    log_path = capture_path + ".log"
    cmd = [
        HASHCAT_BIN, "-m", "22000", "-a", "0",
        "--potfile-path", POTFILE,
        capture_path, WORDLIST,
    ] + HASHCAT_EXTRA
    try:
        logf = open(log_path, "ab")
        logf.write(("\n=== %s ===\n$ %s\n" % (time.ctime(), " ".join(cmd))).encode())
        logf.flush()
        subprocess.Popen(cmd, stdout=logf, stderr=subprocess.STDOUT, close_fds=True)
        print("[hashcat] launched for %s -> %s" % (capture_path, log_path))
    except FileNotFoundError:
        print("[hashcat] ERROR: '%s' not found on PATH; saved capture only" % HASHCAT_BIN)
    except OSError as exc:
        print("[hashcat] ERROR: %s" % exc)
    return log_path


class Handler(BaseHTTPRequestHandler):
    def _reply(self, code, payload):
        body = json.dumps(payload).encode()
        self.send_response(code)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):  # noqa: N802 (http.server API)
        self._reply(200, {"ok": True, "service": "poom-handshake-receiver",
                          "save_dir": SAVE_DIR, "auto_crack": AUTO_CRACK})

    def do_POST(self):  # noqa: N802 (http.server API)
        parsed = urlparse(self.path)
        if parsed.path != "/upload":
            self._reply(404, {"ok": False, "error": "post to /upload"})
            return

        length = int(self.headers.get("Content-Length", "0") or "0")
        if length <= 0:
            self._reply(400, {"ok": False, "error": "empty body"})
            return
        if length > MAX_UPLOAD:
            self._reply(413, {"ok": False, "error": "too large"})
            return

        data = self.rfile.read(length)
        qs = parse_qs(parsed.query)
        name = _safe_name(qs.get("name", [""])[0])

        os.makedirs(SAVE_DIR, exist_ok=True)
        path = _unique_path(SAVE_DIR, name)
        with open(path, "wb") as fh:
            fh.write(data)
        print("[rx] saved %d bytes -> %s" % (len(data), path))

        result = {"ok": True, "saved": os.path.basename(path), "bytes": len(data)}
        if AUTO_CRACK:
            result["log"] = os.path.basename(_launch_hashcat(path))
            result["cracking"] = True
        self._reply(200, result)

    def log_message(self, fmt, *args):  # quieter default logging
        sys.stderr.write("%s - %s\n" % (self.address_string(), fmt % args))


def main():
    os.makedirs(SAVE_DIR, exist_ok=True)
    srv = ThreadingHTTPServer((HOST, PORT), Handler)
    print("POOM handshake receiver on http://%s:%d/upload" % (HOST, PORT))
    print("  save dir : %s" % SAVE_DIR)
    print("  wordlist : %s" % WORDLIST)
    print("  autocrack: %s (%s)" % (AUTO_CRACK, HASHCAT_BIN))
    try:
        srv.serve_forever()
    except KeyboardInterrupt:
        print("\nbye")
        srv.shutdown()


if __name__ == "__main__":
    main()
