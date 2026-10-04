#!/usr/bin/env python3
"""Exercise desktop preview against the actual firmware and POSIX USB framing."""
import importlib.util
import json
import os
from pathlib import Path
import pty
import threading
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location("helmet_tool", ROOT / "scripts/helmet-tool.py")
tool = importlib.util.module_from_spec(spec)
spec.loader.exec_module(tool)


class PreviewChecks(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.transport = tool.PreviewTransport()
        cls.helmet = tool.Helmet(cls.transport)

    @classmethod
    def tearDownClass(cls):
        cls.transport.close()

    def test_profiles_round_trip_through_firmware(self):
        for file in sorted((ROOT / "profiles").glob("*.json")):
            profile = json.loads(file.read_text())
            self.assertEqual(profile["schema"], 1)
            state = self.helmet.action({"action": "set", "config": profile["config"]})
            self.assertEqual(state["config"], profile["config"])
            self.assertEqual(len(state["display"]["frame"]), 224)
            self.assertTrue(state["simulation"])
        self.helmet.action({"action": "defaults"})

    def test_blink_diagnostics_and_defaults(self):
        self.helmet.action({"action": "defaults"})
        self.transport.command("tick 10")
        state = self.helmet.state()
        self.assertEqual(state["status"]["firmware"], "8.1")
        self.assertEqual(state["config"]["blink_interval_ms"], 4000)
        self.assertTrue(state["status"]["blink_random_ready"])
        self.assertGreaterEqual(state["status"]["next_blink_ms"], 2000)
        self.assertLessEqual(state["status"]["next_blink_ms"], 6000)
        self.assertGreaterEqual(state["status"]["blink_duration_ms"], 210)
        self.assertLessEqual(state["status"]["blink_duration_ms"], 305)

    def test_invalid_edits_do_not_partially_apply(self):
        before = dict(self.helmet.state()["config"])
        for config in ({"brightness": 3, "boop_trigger": 900, "boop_release": 800},
                       {"brightness": 256}, {"invented": 1}, {"brightness": True}):
            with self.assertRaises(ValueError):
                self.helmet.action({"action": "set", "config": config})
            self.assertEqual(self.helmet.state()["config"], before)

    def test_amplitude_preview_reaches_full_opening(self):
        self.helmet.action({"action": "set", "config": {"mouth_mode": 1}})
        self.helmet.action({"action": "simulate", "sensor": "audio", "value": 500})
        self.transport.command("tick 300")
        state = self.helmet.state()
        self.assertTrue(state["status"]["speaking"])
        self.assertGreater(state["status"]["envelope"], state["status"]["audio_gate"])
        # A fully open amplitude mouth puts its central upper lip on row 0.
        frame = bytes.fromhex(state["display"]["frame"])
        self.assertTrue(frame[28 // 8] & (0x80 >> (28 & 7)))
        self.helmet.action({"action": "simulate", "sensor": "audio", "value": 0})
        self.transport.command("tick 300")
        self.helmet.action({"action": "defaults"})

    def test_reactions_and_save_load(self):
        self.helmet.action({"action": "expression", "value": 4})
        self.transport.command("tick 200")
        self.assertEqual(self.helmet.state()["status"]["expression"], 4)
        state = self.helmet.action({"action": "sleep", "value": 1})
        self.assertTrue(state["display"]["hidden"])
        self.assertEqual(set(state["display"]["frame"]), {"0"})
        self.helmet.action({"action": "sleep", "value": 0})
        state = self.helmet.action({"action": "simulate", "sensor": "boop", "value": 1})
        self.assertTrue(state["status"]["boop"])
        self.helmet.action({"action": "simulate", "sensor": "visor", "value": 1})
        for _ in range(7):
            self.transport.command("tick 1000")
        state = self.helmet.action({"action": "simulate", "sensor": "visor", "value": 0})
        self.assertFalse(state["status"]["boop"])
        self.helmet.action({"action": "simulate", "sensor": "boop", "value": 0})
        self.helmet.action({"action": "set", "config": {"brightness": 3}})
        self.helmet.action({"action": "save"})
        self.helmet.action({"action": "defaults"})
        self.assertEqual(self.helmet.action({"action": "load"})["config"]["brightness"], 3)


class SerialChecks(unittest.TestCase):
    def test_fragmented_usb_reply(self):
        # A disposable pseudo-terminal exercises raw-mode I/O, framing, and
        # request/response parsing. It is not a physical Teensy validation.
        master, slave = pty.openpty()
        transport = None
        try:
            with patch("fcntl.ioctl"):
                transport = tool.SerialTransport(os.ttyname(slave))
            received = []
            def board():
                data = b""
                while b"\n" not in data:
                    data += os.read(master, 64)
                received.append(data)
                os.write(master, b'{"ok":')
                os.write(master, b'true,"schema":1}\n')
            worker = threading.Thread(target=board, daemon=True)
            worker.start()
            self.assertEqual(transport.command("GET"), {"ok": True, "schema": 1})
            worker.join(timeout=3)
            self.assertEqual(received, [b"GET\n"])
        finally:
            if transport:
                transport.close()
            os.close(slave)
            os.close(master)


if __name__ == "__main__":
    unittest.main()
