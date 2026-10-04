#!/usr/bin/env python3
"""Local face preview and USB settings tool. Python stdlib only; no cloud services."""
import argparse
import glob
import importlib.util
import json
import os
from pathlib import Path
import select
import subprocess
import tempfile
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

ROOT = Path(__file__).resolve().parents[1]


def configure_preview(source):
    spec = importlib.util.spec_from_file_location("feature_checks", ROOT / "scripts/check-features.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module.configure_features(source, module.FEATURES)


class PreviewTransport:
    simulation = True

    def __init__(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="protogen-preview-")
        temporary = Path(self.temporary.name)
        sketch = temporary / "PROT_OS7_MAX7219"
        import shutil
        shutil.copytree(ROOT / "firmware/PROT_OS7_MAX7219", sketch)
        main = sketch / "PROT_OS7_MAX7219.ino"
        main.write_text(configure_preview(main.read_text()))
        fft = Path(os.environ.get("ARDUINO_FFT_SRC", str(Path.home() / "Documents/Arduino/libraries/arduinoFFT/src")))
        if not (fft / "arduinoFFT.cpp").is_file():
            self.temporary.cleanup()
            raise RuntimeError("Set ARDUINO_FFT_SRC to the installed arduinoFFT src directory for desktop preview.")
        executable = temporary / "preview"
        run = subprocess.run([os.environ.get("CXX", "clang++"), "-std=c++17", "-O2", "-Wall", "-Wextra", "-Werror",
                              "-DARDUINO=10819", "-DARDUINO_TEENSY40", "-I" + str(ROOT / "scripts/tests/stubs"),
                              "-I" + str(sketch), "-I" + str(fft), str(ROOT / "scripts/preview.cpp"),
                              str(sketch / "src/Max72xxPanel/Max72xxPanel.cpp"), str(fft / "arduinoFFT.cpp"),
                              "-o", str(executable)], capture_output=True, text=True)
        if run.returncode:
            self.temporary.cleanup()
            raise RuntimeError("Preview compile failed:\n" + run.stderr)
        self.process = subprocess.Popen([str(executable)], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                        stderr=subprocess.PIPE, text=True, bufsize=1)
        self.updated = time.monotonic()

    def command(self, text):
        if self.process.poll() is not None:
            raise RuntimeError("Preview stopped: " + self.process.stderr.read()[-2000:])
        self.process.stdin.write(text + "\n")
        self.process.stdin.flush()
        if not select.select([self.process.stdout], [], [], 3)[0]:
            raise RuntimeError("Preview did not respond.")
        line = self.process.stdout.readline()
        if not line:
            raise RuntimeError("Preview exited before responding.")
        return json.loads(line)

    def tick(self):
        now = time.monotonic()
        elapsed = min(1000, int((now - self.updated) * 1000))
        if elapsed:
            self.command("tick " + str(elapsed))
            self.updated = now

    def close(self):
        self.process.stdin.close()
        try:
            self.process.wait(timeout=3)
        except subprocess.TimeoutExpired:
            self.process.terminate()
            self.process.wait(timeout=3)
        self.temporary.cleanup()


class SerialTransport:
    simulation = False

    def __init__(self, port):
        if os.name != "posix":
            raise RuntimeError("USB mode currently supports macOS/Linux. Desktop preview is available here.")
        import termios
        self.termios = termios
        self.fd = os.open(port, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
        try:
            settings = termios.tcgetattr(self.fd)
            settings[0] = settings[1] = settings[3] = 0
            settings[2] = termios.CS8 | termios.CREAD | termios.CLOCAL
            settings[4] = settings[5] = termios.B115200
            settings[6][termios.VMIN] = settings[6][termios.VTIME] = 0
            termios.tcsetattr(self.fd, termios.TCSANOW, settings)
            termios.tcflush(self.fd, termios.TCIOFLUSH)
            import array
            import fcntl
            fcntl.ioctl(self.fd, termios.TIOCMBIS, array.array("i", [termios.TIOCM_DTR]))
        except Exception:
            os.close(self.fd)
            raise
        self.pending = b""

    def command(self, text):
        data = (text + "\n").encode("ascii")
        deadline = time.monotonic() + 3
        while data:
            remaining = deadline - time.monotonic()
            if remaining <= 0 or not select.select([], [self.fd], [], remaining)[1]:
                raise RuntimeError("USB write timed out. Close other serial monitors and check the cable.")
            try:
                data = data[os.write(self.fd, data):]
            except BlockingIOError:
                continue
        while b"\n" not in self.pending:
            remaining = deadline - time.monotonic()
            if remaining <= 0 or not select.select([self.fd], [], [], remaining)[0]:
                self.termios.tcflush(self.fd, self.termios.TCIFLUSH)
                self.pending = b""
                raise RuntimeError("Helmet did not respond. Upload firmware 8.0 with USB Type Serial first.")
            try:
                chunk = os.read(self.fd, 4096)
            except BlockingIOError:
                continue
            if not chunk:
                raise RuntimeError("Helmet disconnected.")
            self.pending += chunk
            if len(self.pending) > 8192:
                self.pending = b""
                raise RuntimeError("Unexpected USB response length.")
        line, self.pending = self.pending.split(b"\n", 1)
        try:
            return json.loads(line)
        except (ValueError, UnicodeDecodeError) as error:
            raise RuntimeError("Unexpected firmware response. Check that firmware 8.0 is installed.") from error

    def tick(self):
        pass

    def close(self):
        os.close(self.fd)


class Helmet:
    def __init__(self, transport):
        self.transport = transport
        self.lock = threading.RLock()
        self.config = self.checked("GET")
        if self.config.get("schema") != 1:
            raise RuntimeError("Unsupported settings schema; this tool expects firmware 8.0 / schema 1.")
        self.fields = set(self.config["config"])
        self.calibrating = False

    def checked(self, command):
        result = self.transport.command(command)
        if not isinstance(result, dict):
            raise RuntimeError("Expected a JSON object from firmware 8.0")
        if not result.get("ok"):
            raise ValueError(result.get("error", "Request failed"))
        return result

    def state(self):
        with self.lock:
            self.transport.tick()
            status = self.checked("STATUS")
            if self.calibrating and not status.get("calibrating"):
                self.config = self.checked("GET")
            self.calibrating = status.get("calibrating", False)
            return {"simulation": self.transport.simulation, "status": status,
                    "display": self.checked("FRAME"), "config": self.config["config"]}

    def action(self, payload):
        with self.lock:
            action = payload.get("action")
            if action == "set":
                value = payload.get("config")
                if not isinstance(value, dict) or not value or set(value) - self.fields:
                    raise ValueError("Unknown or empty settings")
                if any(type(number) is not int or not 0 <= number <= 65535 for number in value.values()):
                    raise ValueError("Settings must be nonnegative integers")
                # Firmware validates every range and cross-field constraint, atomically.
                self.config = self.checked("SET " + " ".join(f"{key}={number}" for key, number in value.items()))
            elif action in ("save", "load", "defaults", "calibrate"):
                self.checked(action.upper())
                if action == "calibrate":
                    self.calibrating = True
                self.config = self.checked("GET")
            elif action in ("expression", "sleep"):
                value = payload.get("value")
                maximum = 5 if action == "expression" else 1
                if type(value) is not int or not 0 <= value <= maximum:
                    raise ValueError("Invalid control value")
                self.checked(("EXPR " if action == "expression" else "SLEEP ") + str(value))
            elif action == "simulate" and self.transport.simulation:
                sensor, value = payload.get("sensor"), payload.get("value")
                maximum = 500 if sensor == "audio" else 1
                if sensor not in ("audio", "visor", "boop", "tap", "hold") or type(value) is not int or not 0 <= value <= maximum:
                    raise ValueError("Invalid simulated input")
                self.checked(f"sim {sensor} {value}")
            else:
                raise ValueError("Unknown action")
            return self.state()


def make_handler(helmet, port):
    class Handler(BaseHTTPRequestHandler):
        def log_message(self, *_):
            pass

        def respond(self, status, body, mime="application/json"):
            data = json.dumps(body).encode() if mime == "application/json" else body
            self.send_response(status)
            self.send_header("Content-Type", mime)
            self.send_header("Content-Length", str(len(data)))
            self.send_header("Cache-Control", "no-store")
            self.send_header("X-Content-Type-Options", "nosniff")
            self.end_headers()
            try:
                self.wfile.write(data)
            except (BrokenPipeError, ConnectionResetError):
                pass

        def local_request(self):
            hosts = (f"127.0.0.1:{port}", f"localhost:{port}")
            origin = self.headers.get("Origin")
            return self.headers.get("Host") in hosts and (not origin or origin in tuple("http://" + host for host in hosts))

        def do_GET(self):
            if not self.local_request():
                self.respond(403, {"error": "Local access only"}); return
            try:
                if self.path == "/":
                    self.respond(200, (ROOT / "tools/helmet/index.html").read_bytes(), "text/html; charset=utf-8")
                elif self.path == "/api/state":
                    self.respond(200, helmet.state())
                elif self.path == "/api/profiles":
                    self.respond(200, [json.loads(p.read_text()) for p in sorted((ROOT / "profiles").glob("*.json"))])
                elif self.path == "/favicon.ico":
                    self.respond(204, b"", "image/x-icon")
                else:
                    self.respond(404, {"error": "Not found"})
            except (OSError, RuntimeError, ValueError) as error:
                self.respond(503, {"error": str(error)})

        def do_POST(self):
            if not self.local_request():
                self.respond(403, {"error": "Local access only"}); return
            if self.path != "/api/action":
                self.respond(404, {"error": "Not found"}); return
            try:
                length = int(self.headers.get("Content-Length", "0"))
                if not 0 < length <= 4096 or self.headers.get("Content-Type") != "application/json":
                    raise ValueError("Expected a small JSON request")
                payload = json.loads(self.rfile.read(length))
                if not isinstance(payload, dict):
                    raise ValueError("Expected an object")
                self.respond(200, helmet.action(payload))
            except (ValueError, TypeError, OSError, RuntimeError) as error:
                self.respond(400, {"error": str(error)})
    return Handler


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", help="Teensy USB serial device; omit for desktop simulation")
    parser.add_argument("--http-port", type=int, default=8765)
    parser.add_argument("--list-ports", action="store_true")
    args = parser.parse_args()
    if args.list_ports:
        for device in sorted(set(glob.glob("/dev/cu.usbmodem*") + glob.glob("/dev/ttyACM*"))):
            print(device)
        return 0
    if not 1 <= args.http_port <= 65535:
        parser.error("HTTP port must be 1..65535")
    transport = None
    try:
        transport = SerialTransport(args.port) if args.port else PreviewTransport()
        helmet = Helmet(transport)
        server = ThreadingHTTPServer(("127.0.0.1", args.http_port), make_handler(helmet, args.http_port))
        print(f"{'Desktop preview' if transport.simulation else 'USB helmet controls'}: http://127.0.0.1:{args.http_port}", flush=True)
        print("Press Ctrl-C to stop. Settings are saved only with the explicit Save action.", flush=True)
        try:
            server.serve_forever()
        except KeyboardInterrupt:
            pass
        finally:
            server.server_close()
        return 0
    except (OSError, RuntimeError, ValueError) as error:
        print(str(error), file=os.sys.stderr)
        return 1
    finally:
        if transport:
            transport.close()


if __name__ == "__main__":
    raise SystemExit(main())
