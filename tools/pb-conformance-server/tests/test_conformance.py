from pathlib import Path
import sys
import unittest
import uuid


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))

from conformance import (  # noqa: E402
    ACTION_APP,
    TIMER_APP,
    ConformanceError,
    ConformanceStore,
)


SERIAL = "PB-CONFORMANCE-001"


def event_id() -> str:
    return str(uuid.uuid4())


def action_event(app: str, revision: int, identifier: str | None = None) -> dict:
    return {
        "event_id": identifier or event_id(),
        "app": app,
        "state_revision": revision,
        "type": "action",
        "action": "primary",
    }


class ConformanceStoreTests(unittest.TestCase):
    def setUp(self) -> None:
        self.store = ConformanceStore()

    def test_initial_state_is_timer_binding(self) -> None:
        state = self.store.fetch_state(SERIAL)
        self.assertEqual(TIMER_APP, state["app"])
        self.assertEqual(1, state["revision"])
        self.assertEqual(7, state["view"]["value"])
        self.assertEqual(90, state["timer"]["default_seconds"])

    def test_action_binding_advances_view_after_primary(self) -> None:
        status = self.store.issue_command(SERIAL, "activate_action")
        revision = status["state"]["revision"]
        response = self.store.post_event(
            SERIAL,
            action_event(ACTION_APP, revision),
        )
        current = self.store.fetch_state(SERIAL)
        self.assertEqual(revision + 1, response["revision"])
        self.assertEqual(9, current["view"]["value"])

    def test_duplicate_event_returns_original_response_once(self) -> None:
        state = self.store.fetch_state(SERIAL)
        payload = action_event(TIMER_APP, state["revision"])
        first = self.store.post_event(SERIAL, payload)
        second = self.store.post_event(SERIAL, payload)
        status = self.store.status(SERIAL)
        self.assertEqual(first, second)
        self.assertEqual(1, status["event_count"])
        self.assertEqual(1, status["duplicate_count"])

    def test_reused_event_id_with_different_payload_is_rejected(self) -> None:
        state = self.store.fetch_state(SERIAL)
        identifier = event_id()
        self.store.post_event(
            SERIAL,
            action_event(TIMER_APP, state["revision"], identifier),
        )
        changed = action_event(TIMER_APP, state["revision"], identifier)
        changed["action"] = "primary_long"
        with self.assertRaisesRegex(ConformanceError, "different payload"):
            self.store.post_event(SERIAL, changed)

    def test_event_keeps_old_binding_context_after_app_switch(self) -> None:
        old_state = self.store.fetch_state(SERIAL)
        action_state = self.store.issue_command(SERIAL, "activate_action")["state"]
        response = self.store.post_event(
            SERIAL,
            action_event(TIMER_APP, old_state["revision"]),
        )
        self.assertTrue(response["ok"])
        self.assertEqual(action_state, self.store.fetch_state(SERIAL))

    def test_future_revision_is_rejected(self) -> None:
        state = self.store.fetch_state(SERIAL)
        with self.assertRaisesRegex(ConformanceError, "future revision"):
            self.store.post_event(
                SERIAL,
                action_event(TIMER_APP, state["revision"] + 1),
            )

    def test_overlay_is_a_new_revision(self) -> None:
        previous = self.store.fetch_state(SERIAL)
        status = self.store.issue_command(SERIAL, "overlay")
        current = status["state"]
        self.assertEqual(previous["revision"] + 1, current["revision"])
        self.assertEqual(666, current["overlay"]["value"])

    def test_stale_and_conflict_faults_are_one_shot(self) -> None:
        current = self.store.issue_command(SERIAL, "activate_action")["state"]
        self.store.issue_command(SERIAL, "stale_once")
        self.assertEqual(current["revision"] - 1, self.store.fetch_state(SERIAL)["revision"])
        self.assertEqual(current, self.store.fetch_state(SERIAL))

        self.store.issue_command(SERIAL, "conflict_once")
        conflict = self.store.fetch_state(SERIAL)
        self.assertNotEqual(current["view"]["value"], conflict["view"]["value"])
        self.assertEqual(current, self.store.fetch_state(SERIAL))

    def test_delivery_failure_retains_fixture_state(self) -> None:
        state = self.store.fetch_state(SERIAL)
        self.store.issue_command(SERIAL, "event_delivery_off")
        with self.assertRaisesRegex(ConformanceError, "disabled"):
            self.store.post_event(
                SERIAL,
                action_event(TIMER_APP, state["revision"]),
            )
        self.assertEqual(0, self.store.status(SERIAL)["event_count"])

    def test_serials_are_isolated(self) -> None:
        self.store.issue_command(SERIAL, "activate_action")
        other = self.store.fetch_state("PB-CONFORMANCE-002")
        self.assertEqual(TIMER_APP, other["app"])
        self.assertEqual(1, other["revision"])


if __name__ == "__main__":
    unittest.main(verbosity=2)
