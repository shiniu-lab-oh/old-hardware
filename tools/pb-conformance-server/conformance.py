"""Deterministic PB App Protocol v2 fixture state."""

from __future__ import annotations

from copy import deepcopy
import json
import threading
import uuid


MAX_REVISION = 9_007_199_254_740_991
MAX_SERIAL_LENGTH = 64
TIMER_APP = "pb.conformance.timer"
ACTION_APP = "pb.conformance.action"


class ConformanceError(Exception):
    def __init__(self, status: int, message: str):
        super().__init__(message)
        self.status = status
        self.message = message


def _is_integer(value: object) -> bool:
    return isinstance(value, int) and not isinstance(value, bool)


def _timer_state() -> dict:
    return {
        "revision": 1,
        "app": TIMER_APP,
        "view": {
            "value": 7,
            "leading_zeroes": True,
            "brightness": 100,
            "blink": False,
            "leds": [False],
        },
        "timer": {
            "enabled": True,
            "default_seconds": 90,
            "presets_seconds": [90],
        },
    }


def _action_state() -> dict:
    return {
        "revision": 1,
        "app": ACTION_APP,
        "view": {
            "value": 8,
            "leading_zeroes": True,
            "brightness": 100,
            "blink": False,
            "leds": [False],
        },
        "timer": {"enabled": False},
    }


class DeviceFixture:
    def __init__(self, serial: str):
        self.serial = serial
        self.state = _timer_state()
        self.history = {1: TIMER_APP}
        self.events: list[dict] = []
        self.event_ids: dict[str, dict] = {}
        self.duplicate_count = 0
        self.state_delivery_enabled = True
        self.event_delivery_enabled = True
        self.next_state_fault: str | None = None

    def _replace_state(self, template: dict) -> None:
        revision = self.state["revision"] + 1
        if revision > MAX_REVISION:
            raise ConformanceError(409, "revision limit reached")
        self.state = deepcopy(template)
        self.state["revision"] = revision
        self.history[revision] = self.state["app"]

    def _advance_state(self) -> None:
        revision = self.state["revision"] + 1
        if revision > MAX_REVISION:
            raise ConformanceError(409, "revision limit reached")
        self.state["revision"] = revision
        self.state.pop("overlay", None)
        self.history[revision] = self.state["app"]

    def reset_session(self) -> None:
        revision = self.state["revision"] + 1
        if revision > MAX_REVISION:
            raise ConformanceError(409, "revision limit reached")
        self.state = _timer_state()
        self.state["revision"] = revision
        self.history = {revision: TIMER_APP}
        self.events.clear()
        self.event_ids.clear()
        self.duplicate_count = 0
        self.state_delivery_enabled = True
        self.event_delivery_enabled = True
        self.next_state_fault = None

    def state_response(self) -> dict:
        if not self.state_delivery_enabled:
            raise ConformanceError(503, "state delivery is disabled")

        result = deepcopy(self.state)
        fault = self.next_state_fault
        self.next_state_fault = None
        if fault == "stale":
            result["revision"] = max(0, result["revision"] - 1)
        elif fault == "conflict":
            result["view"]["value"] += 1
        return result

    def status(self) -> dict:
        return {
            "serial": self.serial,
            "state": deepcopy(self.state),
            "state_delivery_enabled": self.state_delivery_enabled,
            "event_delivery_enabled": self.event_delivery_enabled,
            "next_state_fault": self.next_state_fault,
            "event_count": len(self.events),
            "duplicate_count": self.duplicate_count,
            "events": deepcopy(self.events),
        }

    def command(self, name: str) -> None:
        if name == "reset":
            self.reset_session()
        elif name == "activate_timer":
            self._replace_state(_timer_state())
        elif name == "activate_action":
            self._replace_state(_action_state())
        elif name == "overlay":
            self._advance_state()
            self.state["overlay"] = {
                "value": 666,
                "duration_ms": 2400,
                "blink": True,
            }
        elif name == "stale_once":
            self.next_state_fault = "stale"
        elif name == "conflict_once":
            self.next_state_fault = "conflict"
        elif name == "state_delivery_on":
            self.state_delivery_enabled = True
        elif name == "state_delivery_off":
            self.state_delivery_enabled = False
        elif name == "event_delivery_on":
            self.event_delivery_enabled = True
        elif name == "event_delivery_off":
            self.event_delivery_enabled = False
        else:
            raise ConformanceError(400, f"unknown command: {name}")

    def accept_event(self, payload: dict) -> dict:
        if not self.event_delivery_enabled:
            raise ConformanceError(503, "event delivery is disabled")
        event_app = self._validate_event(payload)

        event_id = payload["event_id"]
        fingerprint = json.dumps(payload, sort_keys=True, separators=(",", ":"))
        previous = self.event_ids.get(event_id)
        if previous is not None:
            if previous["fingerprint"] != fingerprint:
                raise ConformanceError(409, "event_id was reused with a different payload")
            self.duplicate_count += 1
            return deepcopy(previous["response"])

        if (
            payload["type"] == "action"
            and event_app == ACTION_APP
            and self.state["app"] == ACTION_APP
        ):
            self._advance_state()
            if payload["action"] == "primary_long":
                self.state["view"]["value"] = 99
            else:
                self.state["view"]["value"] = (
                    self.state["view"]["value"] + 1
                ) % 1000

        response = {"ok": True, "revision": self.state["revision"]}
        record = {
            "payload": deepcopy(payload),
            "response": deepcopy(response),
        }
        self.events.append(record)
        self.event_ids[event_id] = {
            "fingerprint": fingerprint,
            "response": deepcopy(response),
        }
        return response

    def _validate_event(self, payload: dict) -> str:
        if not isinstance(payload, dict):
            raise ConformanceError(400, "event body must be a JSON object")

        event_id = payload.get("event_id")
        if not isinstance(event_id, str):
            raise ConformanceError(422, "event_id must be a UUID string")
        try:
            canonical_id = str(uuid.UUID(event_id))
        except ValueError as error:
            raise ConformanceError(422, "event_id must be a UUID string") from error
        if canonical_id != event_id:
            raise ConformanceError(422, "event_id must use canonical lowercase UUID form")

        occurred_at = payload.get("occurred_at")
        if occurred_at is not None and (
            not _is_integer(occurred_at) or occurred_at < 0
        ):
            raise ConformanceError(422, "occurred_at must be a non-negative integer")

        has_app = "app" in payload
        has_revision = "state_revision" in payload
        if has_app != has_revision:
            raise ConformanceError(422, "app and state_revision must appear together")
        if has_app:
            app = payload["app"]
            revision = payload["state_revision"]
            if not isinstance(app, str) or not app or len(app) > 32:
                raise ConformanceError(422, "app is invalid")
            if not _is_integer(revision) or not 0 <= revision <= MAX_REVISION:
                raise ConformanceError(422, "state_revision is invalid")
            if revision > self.state["revision"]:
                raise ConformanceError(409, "event references a future revision")
            if self.history.get(revision) != app:
                raise ConformanceError(409, "event App does not match revision history")
        else:
            app = self.state["app"]

        event_type = payload.get("type")
        if event_type == "action":
            if payload.get("action") not in {"primary", "primary_long"}:
                raise ConformanceError(422, "action is invalid")
            return app
        if event_type != "timer":
            raise ConformanceError(422, "type is invalid")
        if payload.get("event") not in {"started", "paused", "resumed", "finished"}:
            raise ConformanceError(422, "timer event is invalid")
        duration = payload.get("duration_seconds")
        remaining = payload.get("remaining_seconds")
        if not _is_integer(duration) or duration <= 0:
            raise ConformanceError(422, "duration_seconds must be positive")
        if not _is_integer(remaining) or not 0 <= remaining <= duration:
            raise ConformanceError(422, "remaining_seconds is invalid")
        return app


class ConformanceStore:
    def __init__(self):
        self._devices: dict[str, DeviceFixture] = {}
        self._lock = threading.RLock()

    def _get_device(self, serial: str) -> DeviceFixture:
        if not serial or len(serial) > MAX_SERIAL_LENGTH:
            raise ConformanceError(404, "device serial is invalid")
        return self._devices.setdefault(serial, DeviceFixture(serial))

    def fetch_state(self, serial: str) -> dict:
        with self._lock:
            return self._get_device(serial).state_response()

    def post_event(self, serial: str, payload: dict) -> dict:
        with self._lock:
            return self._get_device(serial).accept_event(payload)

    def issue_command(self, serial: str, command: str) -> dict:
        with self._lock:
            device = self._get_device(serial)
            device.command(command)
            result = device.status()
            result["command"] = command
            return result

    def status(self, serial: str) -> dict:
        with self._lock:
            return self._get_device(serial).status()
