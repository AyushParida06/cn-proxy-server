#!/usr/bin/env python3
"""Tiny local web server used as the 'real website' behind the proxy.

Usage: python3 origin_server.py [port]      (default port 9000)

Paths:
  /fast      small text page, answers immediately
  /slow      small text page, answers after DELAY seconds (simulates a slow site)
  /binary    2048 bytes containing zero bytes (checks binary-safe caching)
  /nostore   page with 'Cache-Control: no-store' (must never be cached)
  anything else: small text page

It speaks HTTP/1.1 with Content-Length, because the proxy only caches
responses that begin with 'HTTP/1.1 200'.
"""
import sys
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

DELAY = 0.2  # seconds


class Handler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def log_message(self, *args):
        pass  # keep the terminal quiet

    def do_GET(self):
        path = self.path.split("?")[0]
        extra = {}
        ctype = "text/plain"
        if path == "/binary":
            body = bytes(range(256)) * 8
            ctype = "application/octet-stream"
        elif path == "/nostore":
            body = b"not cacheable\n"
            extra["Cache-Control"] = "no-store"
        else:
            if path == "/slow":
                time.sleep(DELAY)
            body = ("hello from origin: %s\n" % path).encode()
        self.send_response(200)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(body)))
        for k, v in extra.items():
            self.send_header(k, v)
        self.end_headers()
        self.wfile.write(body)


if __name__ == "__main__":
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 9000
    server = ThreadingHTTPServer(("127.0.0.1", port), Handler)
    server.daemon_threads = True
    server.serve_forever()
