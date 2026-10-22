"""Client-only HTTP checks; NOT a regression test for the C++ storage service.

Usage: python verify_reference.py <absolute-path-to-bridge-demo> [--deadline]
Qt runtime DLLs must be on PATH. Creates only temporary test files.
"""
import base64
import http.server
import pathlib
import socket
import subprocess
import sys
import tempfile
import threading
import time
import urllib.parse


class Handler(http.server.BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"
    mode = "normal"
    stored = {}

    def log_message(self, *_):
        pass

    def respond(self, status, body):
        self.send_response(status)
        self.send_header("Content-Type", "application/octet-stream")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        try:
            self.wfile.write(body)
        except (BrokenPipeError, ConnectionResetError, ConnectionAbortedError):
            pass

    def do_POST(self):
        body = self.rfile.read(int(self.headers["Content-Length"]))
        assert self.path == "/upload"
        assert self.headers["StorageType"] == "low"
        assert self.headers["Content-Type"] == "application/octet-stream"
        name = base64.b64decode(self.headers["FileName"], validate=True).decode("utf-8")
        if self.mode == "upload-error":
            self.respond(500, b"simulated failure")
            return
        self.stored[name] = body
        self.respond(200, b"")

    def do_GET(self):
        assert self.path.startswith("/download/")
        assert not self.headers.get("Range")
        assert not self.headers.get("If-Range")
        name = urllib.parse.unquote(self.path[len("/download/"):])
        if self.mode == "delayed":
            time.sleep(32)
        if self.mode == "download-error":
            self.respond(500, b"simulated error; must not replace local file")
        elif self.mode == "partial-status":
            self.respond(206, b"wrong response")
        elif self.mode == "truncated":
            self.send_response(200)
            self.send_header("Content-Length", "100")
            self.send_header("Connection", "close")
            self.end_headers()
            self.wfile.write(b"short")
            self.close_connection = True
        elif self.mode == "oversized":
            self.respond(200, b"x" * (1048576 + 1))
        else:
            self.respond(200, self.stored[name])


def main():
    exe = str(pathlib.Path(sys.argv[1]).resolve())
    server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Handler)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    origin = f"http://127.0.0.1:{server.server_port}"
    passed = 0

    def run(label, args, expected, env=None):
        nonlocal passed
        completed = subprocess.run([exe, *map(str, args)], capture_output=True,
                                   timeout=40, env=env)
        assert completed.returncode == expected, (label, completed.returncode,
                                                 completed.stdout, completed.stderr)
        passed += 1
        print(f"PASS {label}", flush=True)
        return completed

    try:
        with tempfile.TemporaryDirectory(prefix="qt-http-bridge-") as tmp:
            root = pathlib.Path(tmp)
            for name, body in [("demo.bin", b"\x00\xffhello\x00\n"),
                               ("测试 #%.txt", "Qt 与云存储\n".encode("utf-8"))]:
                source, target = root / name, root / ("download-" + name)
                source.write_bytes(body)
                run("upload " + name, ["upload", origin, source], 0)
                assert Handler.stored[name] == body
                run("download " + name, ["download", origin, name, target], 0)
                assert target.read_bytes() == body

            empty, large = root / "empty.bin", root / "large.bin"
            empty.write_bytes(b"")
            large.write_bytes(b"x" * (1048576 + 1))
            run("empty input", ["upload", origin, empty], 2)
            run("oversized input", ["upload", origin, large], 2)
            run("invalid origin", ["upload", origin + "/wrong", root / "demo.bin"], 2)
            run("missing source", ["upload", origin, root / "missing.bin"], 2)
            run("unwritable destination", ["download", origin, "demo.bin", root], 2)

            Handler.mode = "upload-error"
            run("upload HTTP 500", ["upload", origin, root / "demo.bin"], 1)
            target = root / "existing.bin"
            for mode in ["download-error", "partial-status", "truncated", "oversized"]:
                target.write_bytes(b"keep existing content")
                Handler.mode = mode
                run(mode, ["download", origin, "demo.bin", target], 1)
                assert target.read_bytes() == b"keep existing content"

            import os
            Handler.mode = "delayed"
            env = dict(os.environ, BRIDGE_CANCEL_AFTER_MS="100")
            run("cancel", ["download", origin, "demo.bin", target], 1, env)
            assert target.read_bytes() == b"keep existing content"
            if "--deadline" in sys.argv:
                result = run("inactivity timeout", ["download", origin, "demo.bin", target], 1)
                output = result.stdout + result.stderr
                assert b"Inactivity deadline exceeded" in output, output
                assert target.read_bytes() == b"keep existing content"

            with socket.socket() as unused:
                unused.bind(("127.0.0.1", 0))
                unused_port = unused.getsockname()[1]
            run("connection refused", ["upload", f"http://127.0.0.1:{unused_port}",
                                       root / "demo.bin"], 1)
        print(f"{passed} checks passed (simulated HTTP only)")
    finally:
        server.shutdown()
        server.server_close()


if __name__ == "__main__":
    main()
