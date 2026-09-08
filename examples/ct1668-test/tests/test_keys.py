"""Run the actual pure-C key decoder/filter on Windows, without touching COM5.

Uses the installed Espressif host clang/lld-link to build a CRT-free test DLL.
Only esp_err.h constants are substituted; the production C source is compiled.
These tests cover software masks/events, not physical matrix rollover.
"""
import ctypes as c
import itertools
from pathlib import Path
import subprocess
import unittest

ROOT = Path(__file__).resolve().parents[1]
LLVM = Path(r"C:/Espressif/tools/esp-clang/16.0.1-fe4f10a809/esp-clang/bin")
OUT = ROOT / "build" / "host-keys"
OUT.mkdir(parents=True, exist_ok=True)
subprocess.run([
    str(LLVM / "clang.exe"), "--target=x86_64-pc-windows-msvc", "-ffreestanding",
    "-fno-builtin", "-std=c11", "-Wall", "-Wextra", "-Werror", "-O1",
    "-I", str(ROOT / "tests" / "host_stubs"), "-I", str(ROOT / "main"),
    "-c", str(ROOT / "main" / "sdc251_keys.c"), "-o", str(OUT / "keys.obj"),
], check=True)
subprocess.run([
    str(LLVM / "lld-link.exe"), "/dll", "/noentry", "/nodefaultlib",
    "/out:" + str(OUT / "keys.dll"), str(OUT / "keys.obj"),
    "/export:sdc251_keys_decode", "/export:sdc251_keys_update", "/export:sdc251_key_name",
], check=True)
lib = c.CDLL(str(OUT / "keys.dll"))
Raw = c.c_uint8 * 5


class State(c.Structure):
    _fields_ = [("held", c.c_uint8), ("candidate", c.c_uint8),
                ("matching", c.c_uint8 * 7)]


class Events(c.Structure):
    _fields_ = [("held", c.c_uint8), ("down", c.c_uint8), ("up", c.c_uint8)]


lib.sdc251_keys_decode.argtypes = [c.POINTER(c.c_uint8), c.POINTER(c.c_uint8)]
lib.sdc251_keys_decode.restype = c.c_int
lib.sdc251_keys_update.argtypes = [c.POINTER(State), c.POINTER(c.c_uint8), c.POINTER(Events)]
lib.sdc251_keys_update.restype = c.c_int
lib.sdc251_key_name.argtypes = [c.c_uint8]
lib.sdc251_key_name.restype = c.c_char_p

# Independent fixtures transcribed from the seven measured button logs.
FIXTURES = [
    ("MENU", 0x01, (0, 0x10, 0, 0, 0)),
    ("EXIT", 0x02, (0, 0x08, 0, 0, 0)),
    ("OK",   0x04, (0, 0x01, 0, 0, 0)),
    ("VOL-", 0x08, (0x02, 0, 0, 0, 0)),
    ("VOL+", 0x10, (0x01, 0, 0, 0, 0)),
    ("CH-",  0x20, (0x08, 0, 0, 0, 0)),
    ("CH+",  0x40, (0x10, 0, 0, 0, 0)),
]


class KeyTests(unittest.TestCase):
    def step(self, state, raw, expected_error=0):
        event = Events(0xFF, 0xFF, 0xFF)
        self.assertEqual(lib.sdc251_keys_update(c.byref(state), Raw(*raw), c.byref(event)), expected_error)
        return event.held, event.down, event.up

    def test_measured_keys_and_names(self):
        for name, flag, raw in FIXTURES:
            with self.subTest(name=name):
                result = c.c_uint8(0xFF)
                self.assertEqual(lib.sdc251_keys_decode(Raw(*raw), c.byref(result)), 0)
                self.assertEqual(result.value, flag)
                self.assertEqual(lib.sdc251_key_name(flag).decode(), name)
        self.assertEqual(lib.sdc251_key_name(0), b"UNKNOWN")
        self.assertEqual(lib.sdc251_key_name(3), b"UNKNOWN")

    def test_all_128_software_mask_combinations(self):
        for selected in itertools.product((False, True), repeat=7):
            expected = 0
            raw = [0] * 5
            for enabled, (_, flag, fixture) in zip(selected, FIXTURES):
                if enabled:
                    expected |= flag
                    raw = [a | b for a, b in zip(raw, fixture)]
            result = c.c_uint8()
            self.assertEqual(lib.sdc251_keys_decode(Raw(*raw), c.byref(result)), 0)
            self.assertEqual(result.value, expected)

    def test_unknown_bits_and_floating_ff(self):
        known = (0x1B, 0x19, 0, 0, 0)
        for byte in range(5):
            for bit in range(8):
                if known[byte] & (1 << bit):
                    continue
                raw = [0] * 5
                raw[byte] = 1 << bit
                result = c.c_uint8(0xFF)
                self.assertEqual(lib.sdc251_keys_decode(Raw(*raw), c.byref(result)), 0x108)
                self.assertEqual(result.value, 0)
        state = State()
        self.assertEqual(self.step(state, [255] * 5, 0x108), (0, 0, 0))

    def test_debounce_press_hold_release_all_keys(self):
        for name, flag, raw in FIXTURES:
            with self.subTest(name=name):
                state = State()
                # A held-at-startup key reports DOWN only after three samples.
                self.assertEqual(self.step(state, raw), (0, 0, 0))
                self.assertEqual(self.step(state, raw), (0, 0, 0))
                self.assertEqual(self.step(state, raw), (flag, flag, 0))
                for _ in range(300):
                    self.assertEqual(self.step(state, raw), (flag, 0, 0))
                self.assertEqual(self.step(state, [0] * 5), (flag, 0, 0))
                self.assertEqual(self.step(state, [0] * 5), (flag, 0, 0))
                self.assertEqual(self.step(state, [0] * 5), (0, 0, flag))
                self.assertEqual(self.step(state, [0] * 5), (0, 0, 0))

    def test_bounce_and_independent_buttons(self):
        state = State()
        menu, both = (0, 0x10, 0, 0, 0), (0, 0x18, 0, 0, 0)
        # EXIT chatters, but MENU must still settle on its third sample.
        self.assertEqual(self.step(state, menu), (0, 0, 0))
        self.assertEqual(self.step(state, both), (0, 0, 0))
        self.assertEqual(self.step(state, menu), (1, 1, 0))
        self.assertEqual(self.step(state, both), (1, 0, 0))
        self.assertEqual(self.step(state, both), (1, 0, 0))
        self.assertEqual(self.step(state, both), (3, 2, 0))
        # Short release bounce must not emit UP, or another DOWN on recovery.
        self.assertEqual(self.step(state, menu), (3, 0, 0))
        self.assertEqual(self.step(state, both), (3, 0, 0))
        self.assertEqual(self.step(state, both), (3, 0, 0))
        self.assertEqual(self.step(state, both), (3, 0, 0))

    def test_invalid_sample_breaks_streak_and_preserves_held(self):
        state = State()
        menu = FIXTURES[0][2]
        self.step(state, menu)
        self.step(state, menu)
        self.assertEqual(self.step(state, [255] * 5, 0x108), (0, 0, 0))
        self.assertEqual(self.step(state, menu), (0, 0, 0))
        self.assertEqual(self.step(state, menu), (0, 0, 0))
        self.assertEqual(self.step(state, menu), (1, 1, 0))
        self.assertEqual(self.step(state, [255] * 5, 0x108), (1, 0, 0))
        self.assertEqual(self.step(state, [0] * 5), (1, 0, 0))
        self.assertEqual(self.step(state, [0] * 5), (1, 0, 0))
        self.assertEqual(self.step(state, [0] * 5), (0, 0, 1))

    def test_null_arguments(self):
        result = c.c_uint8(0xFF)
        self.assertEqual(lib.sdc251_keys_decode(None, c.byref(result)), 0x102)
        self.assertEqual(result.value, 0)
        self.assertEqual(lib.sdc251_keys_decode(Raw(), None), 0x102)
        state, event = State(), Events()
        self.assertEqual(lib.sdc251_keys_update(None, Raw(), c.byref(event)), 0x102)
        self.assertEqual(lib.sdc251_keys_update(c.byref(state), Raw(), None), 0x102)
        self.assertEqual(lib.sdc251_keys_update(c.byref(state), None, c.byref(event)), 0x102)


if __name__ == "__main__":
    unittest.main(verbosity=2)
