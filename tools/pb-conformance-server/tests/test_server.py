from http.client import HTTPConnection
import json
from pathlib import Path
import sys
import threading
import unittest
import uuid


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))

from server import create_server  # noqa: E402


TOKEN = "test-token"
SERIAL = "PB-CONFORMANCE-HTTP"


class ServerTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.server = create_server("127.0.0.1", 0, TOKEN)
        cls.thread = threading.Thread(target=cls.server.serve_forever, daemon=True)
        cls.thread.start()
        cls.port = cls.server.server_address[1]

    @classmethod
    def tearDownClass(cls) -> None:
        cls.server.shutdown()
        cls.server.server_close()
        cls.thread.join(timeout=2)

    def request(
        self,
        method: str,
        path: str,
        body: dict | None = None,
        authorized: bool = True,
        firmware: bool = False,
    ) -> tuple[int, dict]:
        headers = {}
        if authorized:
            headers["Authorization"] = f"Bearer {TOKEN}"
        if firmware:
            headers["X-PB-Firmware"] = "pb-runtime/0.3.0"
        encoded = None
        if body is not None:
            headers["Content-Type"] = "application/json"
            encoded = json.dumps(body).encode("utf-8")
        connection = HTTPConnection("127.0.0.1", self.port, timeout=2)
        connection.request(method, path, body=encoded, headers=headers)
        response = connection.getresponse()
        payload = json.loads(response.read().decode("utf-8"))
        connection.close()
        return response.status, payload

    def test_device_state_requires_authentication(self) -> None:
        status, payload = self.request(
            "GET",
            f"/api/pb/v1/devices/{SERIAL}/state",
            authorized=False,
            firmware=True,
        )
        self.assertEqual(401, status)
        self.assertFalse(payload["ok"])

    def test_device_state_requires_firmware_header(self) -> None:
        status, payload = self.request(
            "GET",
            f"/api/pb/v1/devices/{SERIAL}/state",
        )
        self.assertEqual(400, status)
        self.assertIn("X-PB-Firmware", payload["error"])

    def test_state_and_event_routes_match_runtime_contract(self) -> None:
        status, state = self.request(
            "GET",
            f"/api/pb/v1/devices/{SERIAL}/state",
            firmware=True,
        )
        self.assertEqual(200, status)

        event = {
            "event_id": str(uuid.uuid4()),
            "app": state["app"],
            "state_revision": state["revision"],
            "type": "timer",
            "event": "started",
            "duration_seconds": 90,
            "remaining_seconds": 90,
        }
        status, response = self.request(
            "POST",
            f"/api/pb/v1/devices/{SERIAL}/events",
            event,
            firmware=True,
        )
        self.assertEqual(200, status)
        self.assertTrue(response["ok"])

    def test_control_command_changes_binding(self) -> None:
        status, response = self.request(
            "POST",
            f"/conformance/v1/devices/{SERIAL}/commands",
            {"command": "activate_action"},
        )
        self.assertEqual(200, status)
        self.assertEqual("pb.conformance.action", response["state"]["app"])


if __name__ == "__main__":
    unittest.main(verbosity=2)
