#!/usr/bin/env python3
"""Minimal static server for local development.

Serving over http://localhost counts as a secure context, which is what
Web Bluetooth, WebUSB, WebHID, Web Serial and Geolocation all require.
Opening index.html with a file:// URL will not work: ES modules are blocked
by CORS and every device API refuses to run.

    python3 serve.py [port]
"""

import http.server
import socketserver
import sys
from functools import partial
from pathlib import Path

ROOT = Path(__file__).resolve().parent


class Handler(http.server.SimpleHTTPRequestHandler):
    def end_headers(self):
        # Never cache during development.
        self.send_header("Cache-Control", "no-store")
        super().end_headers()

    def log_message(self, fmt, *args):
        sys.stderr.write("  %s\n" % (fmt % args))


def main() -> int:
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 8000
    handler = partial(Handler, directory=str(ROOT))

    socketserver.TCPServer.allow_reuse_address = True
    with socketserver.TCPServer(("127.0.0.1", port), handler) as httpd:
        print(f"AEGIS serving {ROOT} at http://localhost:{port}")
        print("Ctrl-C to stop.")
        try:
            httpd.serve_forever()
        except KeyboardInterrupt:
            print("\nstopped")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
