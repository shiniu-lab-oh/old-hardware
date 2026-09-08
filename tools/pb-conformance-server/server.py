#!/usr/bin/env python3
"""Standard-library HTTP server for PB Runtime conformance testing."""

from __future__ import annotations

import argparse
import hmac
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
import os
from urllib.parse import unquote, urlsplit

from conformance import ConformanceError, ConformanceStore


MAX_REQUEST_BODY = 16 * 1024


class ConformanceHTTPServer(ThreadingHTTPServer):
    daemon_threads = True

    def __init__(self, address: tuple[str, int], token: str):
        super().__init__(address, ConformanceHandler)
        self.token = token
        self.store = ConformanceStore()


class ConformanceHandler(BaseHTTPRequestHandler):
    server: ConformanceHTTPServer
    server_version = "PBConformance/0.1"

    def do_GET(self) -> None:
        try:
            self._authorize()
            parts = self._path_parts()
            if len(parts) == 6 and parts[:4] == ["api", "pb", "v1", "devices"]:
                if parts[5] != "state":
                    raise ConformanceError(404, "route not found")
                self._require_firmware_header()
                self._send_json(200, self.server.store.fetch_state(parts[4]))
                return
            if len(parts) == 4 and parts[:3] == ["conformance", "v1", "devices"]:
                self._send_json(200, self.server.store.status(parts[3]))
                return
            raise ConformanceError(404, "route not found")
        except ConformanceError as error:
            self._send_error_json(error)

    def do_POST(self) -> None:
        try:
            self._authorize()
            parts = self._path_parts()
            if len(parts) == 6 and parts[:4] == ["api", "pb", "v1", "devices"]:
                if parts[5] != "events":
                    raise ConformanceError(404, "route not found")
                self._require_firmware_header()
                self._send_json(
                    200,
                    self.server.store.post_event(parts[4], self._read_json()),
                )
                return
            if (
                len(parts) == 5
                and parts[:3] == ["conformance", "v1", "devices"]
                and parts[4] == "commands"
            ):
                body = self._read_json()
                command = body.get("command") if isinstance(body, dict) else None
                if not isinstance(command, str) or not command:
                    raise ConformanceError(400, "command must be a non-empty string")
                self._send_json(
                    200,
                    self.server.store.issue_command(parts[3], command),
                )
                return
            raise ConformanceError(404, "route not found")
        except ConformanceError as error:
            self._send_error_json(error)

    def _authorize(self) -> None:
        expected = f"Bearer {self.server.token}"
        provided = self.headers.get("Authorization", "")
        if not hmac.compare_digest(provided, expected):
            raise ConformanceError(401, "invalid bearer token")

    def _require_firmware_header(self) -> None:
        firmware = self.headers.get("X-PB-Firmware", "")
        if not firmware.startswith("pb-runtime/"):
            raise ConformanceError(400, "X-PB-Firmware header is required")

    def _path_parts(self) -> list[str]:
        path = urlsplit(self.path).path
        return [unquote(part) for part in path.split("/") if part]

    def _read_json(self) -> object:
        content_type = self.headers.get("Content-Type", "")
        if content_type.split(";", 1)[0].strip().lower() != "application/json":
            raise ConformanceError(415, "Content-Type must be application/json")
        try:
            length = int(self.headers.get("Content-Length", "0"))
        except ValueError as error:
            raise ConformanceError(400, "Content-Length is invalid") from error
        if length <= 0 or length > MAX_REQUEST_BODY:
            raise ConformanceError(413, "request body size is invalid")
        try:
            return json.loads(self.rfile.read(length).decode("utf-8"))
        except (UnicodeDecodeError, json.JSONDecodeError) as error:
            raise ConformanceError(400, "request body is not valid JSON") from error

    def _send_error_json(self, error: ConformanceError) -> None:
        self._send_json(error.status, {"ok": False, "error": error.message})

    def _send_json(self, status: int, payload: object) -> None:
        body = json.dumps(
            payload,
            ensure_ascii=False,
            separators=(",", ":"),
        ).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(body)

    def log_message(self, format_string: str, *args: object) -> None:
        print(f"[{self.log_date_time_string()}] {self.address_string()} {format_string % args}")


def create_server(host: str, port: int, token: str) -> ConformanceHTTPServer:
    if not token:
        raise ValueError("token must not be empty")
    return ConformanceHTTPServer((host, port), token)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=8080)
    parser.add_argument(
        "--token",
        default=os.environ.get("PB_CONFORMANCE_TOKEN", "pb-conformance-local"),
    )
    args = parser.parse_args()

    server = create_server(args.host, args.port, args.token)
    host, port = server.server_address[:2]
    print("PB Conformance Mock Cloud")
    print(f"Listening on http://{host}:{port}")
    print("Press Ctrl+C to stop.")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\nStopping.")
    finally:
        server.server_close()


if __name__ == "__main__":
    main()
