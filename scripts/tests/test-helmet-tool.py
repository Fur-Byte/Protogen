#!/usr/bin/env python3
"""Exercise desktop preview against the actual firmware and POSIX USB framing."""
import importlib.util
import io
import json
import os
from pathlib import Path
import pty
import stat
import tempfile
import threading
import unittest
from unittest.mock import Mock, patch

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
            self.assertEqual(profile["schema"], 2)
            state = self.helmet.action({"action": "set", "config": profile["config"]})
            self.assertEqual(state["config"], profile["config"])
            self.assertEqual(len(state["display"]["frame"]), 224)
            self.assertTrue(state["simulation"])
        self.helmet.action({"action": "defaults"})

    def test_requested_brightness_and_effective_ceiling_are_distinct(self):
        state = self.helmet.action({"action": "set", "config": {"brightness": 15}})
        self.assertEqual(state["config"]["brightness"], 15)
        self.assertEqual(state["status"]["brightness"], {"configured": 15, "effective": 1, "maximum": 1, "approved": 1})
        reply = self.transport.command("BOOST 0 100")
        self.assertTrue(reply["ok"])
        self.assertEqual(reply["brightness"], {"configured": 15, "requested": 0, "effective": 0, "maximum": 1, "approved": 1})
        self.assertEqual(self.helmet.state()["status"]["brightness"]["effective"], 0)
        self.transport.command("tick 110")
        self.assertEqual(self.helmet.state()["status"]["brightness"]["effective"], 1)
        self.helmet.action({"action": "defaults"})

    def test_blink_diagnostics_and_defaults(self):
        self.helmet.action({"action": "defaults"})
        self.transport.command("tick 10")
        state = self.helmet.state()
        self.assertEqual(state["status"]["firmware"], "8.2")
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

    def test_microphone_mute_retry_and_boop_modes(self):
        state = self.helmet.action({"action": "microphone", "value": 0})
        self.assertFalse(state["status"]["microphone_enabled"])
        state = self.helmet.action({"action": "microphone", "value": 1})
        self.assertTrue(state["status"]["microphone_enabled"])
        state = self.helmet.action({"action": "set", "config": {"boop_mode": 1, "boop_duration_ms": 1200}})
        self.assertEqual(state["config"]["boop_mode"], 1)
        self.helmet.action({"action": "defaults"})

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
        # Explicit simulation time, independent of compiler/host speed.
        with patch.object(tool.time, "monotonic", return_value=self.transport.updated):
            self.helmet.action({"action": "expression", "value": 4})
            self.transport.command("tick 200")
            self.assertEqual(self.helmet.state()["status"]["expression"], 4)
            state = self.helmet.action({"action": "sleep", "value": 1})
            self.assertTrue(state["display"]["hidden"])
            self.assertEqual(set(state["display"]["frame"]), {"0"})
            self.helmet.action({"action": "sleep", "value": 0})
            # Wake invalidates the cached IR sample. Observe a fresh released
            # reading before simulating a new approach, as the rearm policy
            # requires; host wall-clock delays must not supply this implicitly.
            self.transport.command("tick 10")
            released = self.helmet.state()
            self.assertGreaterEqual(released["status"]["boop_reading"], released["config"]["boop_release"])
            self.assertFalse(released["status"]["boop"])
            self.assertEqual(released["status"]["sensor_faults"], 0)
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

    def test_wake_boop_fresh_release_sampling_phases(self):
        with patch.object(tool.time, "monotonic", return_value=self.transport.updated):
            for phase in range(16):
                with self.subTest(phase=phase):
                    self.helmet.action({"action": "sleep", "value": 0})
                    self.helmet.action({"action": "simulate", "sensor": "boop", "value": 0})
                    self.transport.command("tick " + str(phase))
                    self.helmet.action({"action": "sleep", "value": 1})
                    self.helmet.action({"action": "sleep", "value": 0})
                    self.transport.command("tick 10")
                    released = self.helmet.state()
                    self.assertGreaterEqual(released["status"]["boop_reading"], released["config"]["boop_release"])
                    self.assertFalse(released["status"]["boop"])
                    self.assertEqual(released["status"]["sensor_faults"], 0)
                    state = self.helmet.action({"action": "simulate", "sensor": "boop", "value": 1})
                    self.assertTrue(state["status"]["boop"])
            self.helmet.action({"action": "simulate", "sensor": "boop", "value": 0})


class HandlerChecks(unittest.TestCase):
    def setUp(self):
        self.helmet = Mock()
        self.helmet.transport.simulation = True
        self.helmet.state.return_value = {"status": {"sleeping": False}}
        self.helmet.action.return_value = {"status": {"sleeping": True}}
        self.handler = tool.make_handler(self.helmet, 8765)

    def request(self, method, path, headers=None, payload=None):
        body = json.dumps(payload).encode() if payload is not None else b""
        request_headers = {"Host": "127.0.0.1:8765", "Connection": "close"}
        if payload is not None:
            request_headers.update({"Content-Type": "application/json", "Content-Length": str(len(body))})
        request_headers.update(headers or {})
        raw = (f"{method} {path} HTTP/1.1\r\n" +
               "".join(f"{key}: {value}\r\n" for key, value in request_headers.items()) + "\r\n").encode() + body
        # Exercise the real HTTP parser and response writer without opening a socket.
        connection = Mock()
        connection.makefile.return_value = io.BytesIO(raw)
        response = bytearray()
        connection.sendall.side_effect = response.extend
        self.handler(connection, ("127.0.0.1", 40000), Mock())
        head, response_body = bytes(response).split(b"\r\n\r\n", 1)
        lines = head.decode().split("\r\n")
        response_headers = dict(line.split(": ", 1) for line in lines[1:])
        return int(lines[0].split()[1]), response_headers, response_body

    def assert_framing_blocked(self, headers):
        self.assertEqual(headers.get("Content-Security-Policy"), "frame-ancestors 'none'")
        self.assertEqual(headers.get("X-Frame-Options"), "DENY")

    def test_control_html_rejects_framing(self):
        for host in ("127.0.0.1:8765", "localhost:8765"):
            with self.subTest(host=host):
                status, headers, body = self.request("GET", "/", {
                    "Host": host, "Sec-Fetch-Site": "cross-site",
                    "Sec-Fetch-Dest": "iframe", "Sec-Fetch-Mode": "navigate"})
                self.assertEqual(status, 200)
                self.assert_framing_blocked(headers)
                self.assertEqual(headers["Content-Type"], "text/html; charset=utf-8")
                self.assertEqual(body, (ROOT / "tools/helmet/index.html").read_bytes())
                self.assertEqual(int(headers["Content-Length"]), len(body))

    def test_inherited_html_errors_reject_framing(self):
        for method in ("HEAD", "OPTIONS"):
            with self.subTest(method=method):
                status, headers, body = self.request(method, "/")
                self.assertEqual(status, 501)
                self.assert_framing_blocked(headers)
                self.assertTrue(headers["Content-Type"].startswith("text/html"))
                self.assertEqual(bool(body), method != "HEAD")

    def test_local_api_controls_remain_available(self):
        for host in ("127.0.0.1:8765", "localhost:8765"):
            with self.subTest(host=host):
                local_headers = {"Host": host, "Origin": "http://" + host}
                status, headers, body = self.request("GET", "/api/state", local_headers)
                self.assertEqual(status, 200)
                self.assertEqual(headers["Content-Type"], "application/json")
                self.assertEqual(json.loads(body), self.helmet.state.return_value)
                payload = {"action": "sleep", "value": 1}
                status, _, body = self.request("POST", "/api/action", local_headers, payload)
                self.assertEqual(status, 200)
                self.assertEqual(json.loads(body), self.helmet.action.return_value)
                self.helmet.action.assert_called_with(payload)
        self.assertEqual(self.helmet.action.call_count, 2)

    def test_foreign_host_and_origin_cannot_dispatch_controls(self):
        for headers in ({"Host": "attacker.invalid:8765"},
                        {"Host": "127.0.0.1.attacker.invalid:8765"},
                        {"Origin": "http://attacker.invalid"}, {"Origin": "null"}):
            for method, path, payload in (("GET", "/", None),
                                          ("POST", "/api/action", {"action": "sleep", "value": 1})):
                with self.subTest(headers=headers, method=method):
                    status, _, body = self.request(method, path, headers, payload)
                    self.assertEqual(status, 403)
                    self.assertEqual(json.loads(body), {"error": "Local access only"})
        self.helmet.action.assert_not_called()
        self.helmet.state.assert_not_called()

    def test_usb_apis_require_the_launch_credential(self):
        self.helmet.transport.simulation = False
        token = 'A' * 43
        self.handler = tool.make_handler(self.helmet, 8765, token)
        payload = {"action": "sleep", "value": 1}
        self.assertEqual(self.request("GET", "/")[0], 200)
        self.assertEqual(self.request("GET", "/api/state")[0], 403)
        self.assertEqual(self.request("POST", "/api/action", payload=payload)[0], 403)
        self.helmet.state.assert_not_called()
        self.helmet.action.assert_not_called()
        headers = {"Authorization": "Bearer " + token}
        self.assertEqual(self.request("GET", "/api/state", headers)[0], 200)
        self.assertEqual(self.request("POST", "/api/action", headers, payload)[0], 200)
        self.helmet.action.assert_called_once_with(payload)

    def test_usb_launcher_is_private(self):
        with tempfile.TemporaryDirectory() as directory:
            previous_umask = os.umask(0o022)
            try:
                token = 'A' * 43
                path = tool.write_usb_launcher(directory, 8765, token)
            finally:
                os.umask(previous_umask)
            self.assertEqual(stat.S_IMODE(path.stat().st_mode), 0o600)
            self.assertIn(f"http://127.0.0.1:8765/#usb={token}", path.read_text())

    def test_invalid_action_json_still_rejected(self):
        status, headers, body = self.request("POST", "/api/action", payload=[])
        self.assertEqual(status, 400)
        self.assertEqual(headers["Content-Type"], "application/json")
        self.assertEqual(json.loads(body), {"error": "Expected an object"})
        self.helmet.action.assert_not_called()


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
