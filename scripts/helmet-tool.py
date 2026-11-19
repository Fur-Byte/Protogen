#!/usr/bin/env python3
"""Local face preview and USB settings tool. Python stdlib only; no cloud services."""
import argparse
import glob
import json
import os
from pathlib import Path
import secrets
import select
import sys
import subprocess
import tempfile
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
# Support the existing importlib-based host tests as well as CLI execution.
scripts_directory = str(Path(__file__).resolve().parent)
if scripts_directory not in sys.path:
    sys.path.insert(0, scripts_directory)
from sketch_features import SENSOR_FEATURES, configure_features
from timelines.compiler import MAX_BYTES as TIMELINE_MAX_BYTES, TimelineError, compile_document, fields, load_json
from timelines.simulator import replay_asset


ROOT = Path(__file__).resolve().parents[1]


class PreviewTransport:
    simulation = True

    def __init__(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="protogen-preview-")
        temporary = Path(self.temporary.name)
        sketch = temporary / "PROT_OS7_MAX7219"
        import shutil
        shutil.copytree(ROOT / "firmware/PROT_OS7_MAX7219", sketch)
        main = sketch / "PROT_OS7_MAX7219.ino"
        main.write_text(configure_features(main.read_text(), SENSOR_FEATURES))
        fft = Path(os.environ.get("ARDUINO_FFT_SRC", str(Path.home() / "Documents/Arduino/libraries/arduinoFFT/src")))
        if not (fft / "arduinoFFT.cpp").is_file():
            self.temporary.cleanup()
            raise RuntimeError("Set ARDUINO_FFT_SRC to the installed arduinoFFT src directory for desktop preview.")
        executable = temporary / "preview"
        compiler = os.environ.get("CXX", "clang++")
        flags = ["-std=c++17", "-O2", "-Wall", "-Wextra", "-Werror",
                 "-DARDUINO=10819", "-DARDUINO_TEENSY40",
                 "-I" + str(ROOT / "scripts/tests/stubs"), "-I" + str(sketch), "-I" + str(fft)]
        dependency_flags = (["-Wno-error=absolute-value"] if "clang" in
                            subprocess.check_output([compiler, "--version"], text=True).lower() else [])
        fft_object = temporary / "arduinoFFT.o"
        run = subprocess.run([compiler, *flags, *dependency_flags, "-c",
                              str(fft / "arduinoFFT.cpp"), "-o", str(fft_object)],
                             capture_output=True, text=True)
        if run.returncode == 0:
            run = subprocess.run([compiler, *flags, str(ROOT / "scripts/preview.cpp"),
                                  str(sketch / "src/Max72xxPanel/Max72xxPanel.cpp"),
                                  str(sketch / "src/BehaviorCore/BehaviorCore.cpp"),
                                  str(fft_object), "-o", str(executable)], capture_output=True, text=True)
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
        if self.config.get("schema") not in (1, 2):
            raise RuntimeError("Unsupported settings schema; this tool supports schemas 1 and 2.")
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
            elif action in ("expression", "sleep", "microphone"):
                value = payload.get("value")
                maximum = 5 if action == "expression" else 1
                if type(value) is not int or not 0 <= value <= maximum:
                    raise ValueError("Invalid control value")
                self.checked(("EXPR " if action == "expression" else "MIC " if action == "microphone" else "SLEEP ") + str(value))
            elif action == "simulate" and self.transport.simulation:
                sensor, value = payload.get("sensor"), payload.get("value")
                maximum = 500 if sensor == "audio" else 1
                if sensor not in ("audio", "visor", "boop", "tap", "hold") or type(value) is not int or not 0 <= value <= maximum:
                    raise ValueError("Invalid simulated input")
                self.checked(f"sim {sensor} {value}")
            else:
                raise ValueError("Unknown action")
            return self.state()


class StudioServer(ThreadingHTTPServer):
    """Bound the number of connections waiting for headers or serial access."""

    def __init__(self, address, handler):
        self.slots = threading.BoundedSemaphore(16)
        super().__init__(address, handler)

    def process_request(self, request, client_address):
        if not self.slots.acquire(blocking=False):
            self.shutdown_request(request)
            return
        try:
            super().process_request(request, client_address)
        except BaseException:
            self.slots.release()
            raise

    def process_request_thread(self, request, client_address):
        try:
            super().process_request_thread(request, client_address)
        finally:
            self.slots.release()


def make_handler(helmet, port, usb_token=None):
    class Handler(BaseHTTPRequestHandler):
        timeout = 5

        def log_message(self, *_):
            pass

        def end_headers(self):
            self.send_header("Content-Security-Policy", "frame-ancestors 'none'")
            self.send_header("X-Frame-Options", "DENY")
            self.send_header("Referrer-Policy", "no-referrer")
            super().end_headers()

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

        def allow_request(self):
            hosts = (f"127.0.0.1:{port}", f"localhost:{port}")
            host = self.headers.get("Host")
            origins = self.headers.get_all("Origin", [])
            if (len(self.headers.get_all("Host", [])) != 1 or host not in hosts
                    or (origins and origins != ["http://" + host])):
                self.respond(403, {"error": "Local access only"})
                return False
            if not helmet.transport.simulation and self.path.startswith("/api/"):
                values = self.headers.get_all("Authorization", [])
                if (not usb_token or len(values) != 1 or not values[0].isascii()
                        or not secrets.compare_digest(values[0], "Bearer " + usb_token)):
                    self.respond(403, {"error": "Open the private USB controls link printed by Face Studio."})
                    return False
            return True

        def do_GET(self):
            if not self.allow_request():
                return
            try:
                if self.path == "/":
                    self.respond(200, (ROOT / "tools/helmet/index.html").read_bytes(), "text/html; charset=utf-8")
                elif self.path == "/timeline-editor.js":
                    self.respond(200, (ROOT / "tools/helmet/timeline-editor.js").read_bytes(), "text/javascript; charset=utf-8")
                elif self.path == "/api/timeline/sample" and helmet.transport.simulation:
                    sample = ROOT / "tools/helmet/timelines/three-channel.json"
                    self.respond(200, load_json(sample.read_bytes()))
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
            if not self.allow_request():
                return
            if self.path in ("/api/timeline/compile", "/api/timeline/seek"):
                if not helmet.transport.simulation:
                    self.respond(403, {"error": "Timeline editing is simulator-only"})
                    return
                self.timeline_request()
                return
            if self.path != "/api/action":
                self.respond(404, {"error": "Not found"}); return
            try:
                if (len(self.headers.get_all("Content-Length", [])) != 1 or self.headers.get("Transfer-Encoding")
                        or self.headers.get("Content-Type") != "application/json"):
                    raise ValueError("Expected a small JSON request")
                length = int(self.headers["Content-Length"])
                if not 0 < length <= 4096:
                    raise ValueError("Expected a small JSON request")
                raw = self.rfile.read(length)
                if len(raw) != length:
                    raise ValueError("Incomplete request")
                payload = json.loads(raw)
                if not isinstance(payload, dict):
                    raise ValueError("Expected an object")
                self.respond(200, helmet.action(payload))
            except (ValueError, TypeError, OSError, RuntimeError) as error:
                self.respond(400, {"error": str(error)})
        def timeline_request(self):
            try:
                length = int(self.headers.get("Content-Length", "0"))
                if (len(self.headers.get_all("Content-Length", [])) != 1 or self.headers.get("Transfer-Encoding")
                        or self.headers.get("Content-Type") != "application/json"):
                    raise TimelineError("$", "expected fixed-length application/json")
                if not 0 < length <= TIMELINE_MAX_BYTES:
                    raise TimelineError("$", "request exceeds the timeline size limit")
                raw = self.rfile.read(length)
                if len(raw) != length:
                    raise TimelineError("$", "incomplete request")
                request = load_json(raw)
                seeking = self.path == "/api/timeline/seek"
                raw_import = not seeking and type(request) is dict and "document_json" in request
                fields(request, ("document", "positions_ms") if seeking else
                       ("document_json",) if raw_import else ("document",), "$")
                if seeking and type(request["positions_ms"]) is not list:
                    raise TimelineError("positions_ms", "expected an array")
                if raw_import and type(request["document_json"]) is not str:
                    raise TimelineError("document_json", "expected a JSON source string")
                try:
                    document = load_json(request["document_json"].encode("utf-8")) if raw_import else request["document"]
                except UnicodeError as error:
                    raise TimelineError("document_json", "invalid UTF-8 source") from error
                asset = compile_document(document)
                result = replay_asset(asset, request["positions_ms"]) if seeking else {
                    "document": asset.document, "json": asset.source(), "header": asset.header(),
                    "asset_bytes": len(asset.data)}
                self.respond(200, result)
            except TimelineError as error:
                self.respond(400, {"error": error.diagnostic()})
            except (OSError, RuntimeError, ValueError, subprocess.SubprocessError) as error:
                self.respond(503, {"error": {"path": "$", "message": str(error)}})
    return Handler


def write_usb_launcher(directory, port, token):
    # Only this local file contains the launch URL. Fragments do not go into HTTP requests.
    path = Path(directory) / "open-helmet.html"
    fd = os.open(path, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
    with os.fdopen(fd, "w", encoding="utf-8") as output:
        output.write('<!doctype html><title>Face Studio</title>'
                     f'<a rel="noreferrer" href="http://127.0.0.1:{port}/#usb={token}">Open USB helmet controls</a>\n')
    return path


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
        token = None if transport.simulation else secrets.token_urlsafe(32)
        handler = make_handler(helmet, args.http_port, token)
        with StudioServer(("127.0.0.1", args.http_port), handler) as server:
            print("Press Ctrl-C to stop. Settings are saved only with the explicit Save action.", flush=True)
            try:
                if transport.simulation:
                    print(f"Desktop preview: http://127.0.0.1:{args.http_port}", flush=True)
                    server.serve_forever()
                else:
                    with tempfile.TemporaryDirectory(prefix="protogen-usb-") as directory:
                        launcher = write_usb_launcher(directory, args.http_port, token)
                        print(f"Open this private USB controls link in your browser: {launcher.as_uri()}", flush=True)
                        server.serve_forever()
            except KeyboardInterrupt:
                pass
        return 0
    except (OSError, RuntimeError, ValueError) as error:
        print(str(error), file=os.sys.stderr)
        return 1
    finally:
        if transport:
            transport.close()


if __name__ == "__main__":
    raise SystemExit(main())
