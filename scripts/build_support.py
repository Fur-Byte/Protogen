"""Shared source identity, pinned dependency checks, and per-run build evidence."""
import datetime
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import shlex
import subprocess
import uuid
from uart_key import compiler_options, validate_uart_key_flags

FQBN = "teensy:avr:teensy40:usb=serial,speed=600,opt=o2std"


def find_cli():
    override = os.environ.get("ARDUINO_CLI")
    candidates = [override] if override else [
        "/Applications/Arduino IDE.app/Contents/Resources/app/lib/backend/resources/arduino-cli", "arduino-cli"]
    for candidate in candidates:
        resolved = shutil.which(candidate)
        if resolved:
            return resolved
    raise RuntimeError("Arduino CLI not found; set ARDUINO_CLI to an executable path or command.")


def source_identity(root):
    files = {}
    for directory in ("firmware", "scripts", "tools", "profiles"):
        for path in sorted((root / directory).rglob("*")):
            if path.is_file() and "__pycache__" not in path.parts:
                files[str(path.relative_to(root))] = hashlib.sha256(path.read_bytes()).hexdigest()
    lock = root / "toolchain.lock.json"
    if lock.is_file():
        files[lock.name] = hashlib.sha256(lock.read_bytes()).hexdigest()
    digest = hashlib.sha256(json.dumps(files, sort_keys=True, separators=(",", ":")).encode()).hexdigest()
    return {"sha256": digest, "files": files}


def write_json(path, value):
    temporary = path.with_name(path.name + "." + uuid.uuid4().hex + ".tmp")
    temporary.write_text(json.dumps(value, indent=2) + "\n")
    os.replace(temporary, path)


def cli_json(cli, *args):
    run = subprocess.run([cli, *args, "--format", "json"], capture_output=True, text=True)
    if run.returncode:
        raise RuntimeError("Toolchain inspection failed: " + run.stderr[-2000:])
    try:
        return json.loads(run.stdout)
    except ValueError as error:
        raise RuntimeError("Toolchain inspection returned invalid JSON") from error


def check_toolchain(root, cli):
    locked = json.loads((root / "toolchain.lock.json").read_text())
    version = cli_json(cli, "version").get("VersionString")
    platforms = cli_json(cli, "core", "list").get("platforms", [])
    installed = {p["id"]: p.get("installed_version") for p in platforms}
    libraries = cli_json(cli, "lib", "list").get("installed_libraries", [])
    available = {p["library"]["name"]: p["library"].get("version") for p in libraries}
    expected = locked["platform"]
    problems = []
    if version != locked["arduino_cli"]:
        problems.append(f"Arduino CLI: expected {locked['arduino_cli']}, found {version}")
    if installed.get(expected["id"]) != expected["version"]:
        problems.append(f"{expected['id']}: expected {expected['version']}, found {installed.get(expected['id'])}")
    for name, required in locked["libraries"].items():
        if available.get(name) != required:
            problems.append(f"{name}: expected {required}, found {available.get(name)}")
    if locked["fqbn"] != FQBN:
        problems.append("Lock FQBN does not match the build target")
    if problems:
        raise RuntimeError("Pinned toolchain mismatch. Install the versions in toolchain.lock.json:\n" + "\n".join(problems))
    return {"cli": str(cli), "arduino_cli": version, "platform": expected,
            "libraries": locked["libraries"], "bundled_libraries": locked["bundled_libraries"],
            "bundled_source_libraries": locked.get("bundled_source_libraries", {}), "fqbn": FQBN}


def memory_metrics(output):
    result = {}
    flash = re.search(r"FLASH: code:(\d+), data:(\d+), headers:(\d+)", output)
    ram = re.search(r"RAM1: variables:(\d+).*free for local variables:(\d+)", output)
    if flash:
        result["flash_bytes"] = sum(map(int, flash.groups()))
    if ram:
        result["ram1_variables_bytes"], result["ram1_available_bytes"] = map(int, ram.groups())
    return result


class BuildRun:
    def __init__(self, root, kind):
        self.root, self.kind = root, kind
        self.build = root / "build"
        self.build.mkdir(exist_ok=True)
        timestamp = datetime.datetime.now(datetime.timezone.utc).strftime("%Y%m%dT%H%M%SZ")
        self.path = self.build / "runs" / (timestamp + "-" + kind + "-" + uuid.uuid4().hex[:8])
        self.path.mkdir(parents=True)
        self.current = self.build / ("current-build.json" if kind == "build" else "feature-results.json" if kind == "features" else kind+"-results.json")
        self.log = self.path / "compile.log"
        self.compat_log = self.build / ("compile.log" if kind == "build" else "feature-checks.log" if kind == "features" else kind+"-checks.log")
        self.record = {"schema": 1, "kind": kind, "state": "running", "started_utc": timestamp,
                       "run_directory": str(self.path), "fqbn": FQBN, "results": []}
        self.publish()
        self.log.write_text("")
        self.compat_log.write_text("")

    def publish(self):
        write_json(self.path / "manifest.json", self.record)
        write_json(self.current, self.record)

    def snapshot(self):
        self.record["source"] = source_identity(self.root)
        try:
            self.record["git_head"] = subprocess.run(
                ["git", "rev-parse", "HEAD"], cwd=self.root, capture_output=True, text=True).stdout.strip() or None
            self.record["working_tree_dirty"] = bool(subprocess.run(
                ["git", "status", "--porcelain"], cwd=self.root, capture_output=True, text=True).stdout.strip())
        except OSError:
            self.record["git_head"] = None
        sketch = self.path / "snapshot" / "PROT_OS7_MAX7219"
        shutil.copytree(self.root / "firmware/PROT_OS7_MAX7219", sketch)
        # Detect an edit during copying, before any source-bound claim is made.
        for path in sketch.rglob("*"):
            if path.is_file():
                key = "firmware/PROT_OS7_MAX7219/" + str(path.relative_to(sketch))
                if hashlib.sha256(path.read_bytes()).hexdigest() != self.record["source"]["files"][key]:
                    raise RuntimeError("Source changed while snapshotting; rerun the check")
        self.publish()
        return sketch

    def compile(self, cli, sketch, features, output_directory=None, extra_flags=None, fqbn=FQBN):
        output_directory = output_directory or self.path / ("artifacts-" + str(len(self.record["results"])))
        command = [cli, "compile", "--fqbn", fqbn, "--warnings", "all", "--clean",
                   "--build-path", str(output_directory / "objects"), "--output-dir", str(output_directory), str(sketch)]
        extra_flags=extra_flags or "-Werror"
        auth_enabled = "PROTOGEN_UART_AUTH" in features
        if extra_flags:
            # Teensy's recipes consume build.flags.defs, not build.extra_flags.
            # Preserve the installed board's definitions rather than replacing
            # its MCU/Teensyduino flags with our security build options.
            properties = subprocess.run(
                [cli, "compile", "--fqbn", fqbn, "--show-properties=expanded", str(sketch)],
                capture_output=True, text=True, check=True)
            definitions = next((line.partition("=")[2] for line in properties.stdout.splitlines()
                                if line.startswith("build.flags.defs=")), None)
            if not definitions:
                raise RuntimeError("Teensy board compiler definitions are missing")
            auth_enabled = auth_enabled or "PROTOGEN_UART_AUTH" in compiler_options(definitions + " " + extra_flags)[0]
            if auth_enabled:
                validate_uart_key_flags(definitions + " " + extra_flags, [sketch, sketch / "src"])
                # Upstream Mbed TLS public headers use canonical mbedtls/... paths.
                extra_flags += " -I" + shlex.quote(str(sketch)) + " -I" + shlex.quote(str(sketch / "src/UartCrypto"))
            command[2:2]=["--build-property", "build.flags.defs="+definitions+" "+extra_flags]
        run = subprocess.run(command, capture_output=True, text=True)
        output = run.stdout + run.stderr
        result = {"features": list(features), "fqbn": fqbn, "exit_code": run.returncode,
                  "warnings": len(re.findall(r"\bwarning:", output)), "command": command,
                  "configured_source_sha256": hashlib.sha256((sketch / "PROT_OS7_MAX7219.ino").read_bytes()).hexdigest(),
                  "memory": memory_metrics(output)}
        if run.returncode == 0:
            database = output_directory / "objects" / "compile_commands.json"
            units = json.loads(database.read_text()) if database.is_file() else []
            required = ["-Werror"] + (["-DPROTOGEN_UART_AUTH=1"] if auth_enabled else [])
            if not units or any(not all(flag in unit.get("arguments", []) for flag in required) for unit in units):
                result["dependency_error"] = "Required security flags absent from actual compiler commands"
            elif auth_enabled:
                try:
                    for unit in units:
                        arguments = unit.get("arguments", [])
                        if "PROTOGEN_UART_AUTH" not in compiler_options(arguments)[0]:
                            raise RuntimeError("Required security flags absent from actual compiler commands")
                        validate_uart_key_flags(arguments, [sketch, sketch / "src"])
                except RuntimeError as error:
                    result["dependency_error"] = str(error)
        # libraries.cache records the paths actually selected by Arduino CLI.
        if run.returncode == 0:
            cache = output_directory / "objects" / "libraries.cache"
            selected = json.loads(cache.read_text()) if cache.is_file() else []
            resolved = {}
            for library in selected:
                properties = Path(library) / "library.properties"
                if not properties.is_file() and Path(library).name == "src":
                    properties = Path(library).parent / "library.properties"
                if properties.is_file():
                    text = properties.read_text()
                    name = re.search(r"^name=(.*)$", text, re.MULTILINE)
                    version = re.search(r"^version=(.*)$", text, re.MULTILINE)
                    if name:
                        resolved[name[1]] = {"version": version[1] if version else None, "path": library}
                # Teensy's legacy Entropy library has no library.properties.
                # Identify and verify its actually selected source files instead.
                for name, files in self.record["toolchain"].get("bundled_source_libraries", {}).items():
                    if Path(library).name == name:
                        resolved[name] = {"path": library, "source_sha256": {
                            filename: hashlib.sha256((Path(library) / filename).read_bytes()).hexdigest()
                            for filename in files if (Path(library) / filename).is_file()}}
            result["selected_libraries"] = resolved
            expected = {name: version for name, version in self.record["toolchain"]["libraries"].items()
                        if name != "arduinoFFT" or "Microphone" in features}
            if set(features) & {"Microphone", "BoopSensor", "VisorConnector"}:
                expected.update(self.record["toolchain"]["bundled_libraries"])
            problems = [name for name, version in expected.items() if resolved.get(name, {}).get("version") != version]
            problems += [name for name, files in self.record["toolchain"].get("bundled_source_libraries", {}).items()
                         if resolved.get(name, {}).get("source_sha256") != files]
            if problems:
                result["dependency_error"] = "Selected libraries missing or different from lock: " + ", ".join(problems)
        self.record["results"].append(result)
        with self.log.open("a") as log:
            log.write(json.dumps(result) + "\n" + output + "\n")
        shutil.copy2(self.log, self.compat_log)
        self.publish()
        print({k: result[k] for k in ("features", "exit_code", "warnings", "memory")}, flush=True)
        return output_directory

    def finish(self, error=None):
        results = self.record["results"]
        valid = bool(results) and all(r["exit_code"] == 0 and r["warnings"] == 0 and not r.get("dependency_error") for r in results)
        if self.kind == "features" and len(results) != 8:
            valid = False
        if "source" in self.record and source_identity(self.root) != self.record["source"]:
            error = "Source changed during this run; rerun for current evidence"
        self.record["state"] = "failed" if error or not valid else "success"
        self.record["finished_utc"] = datetime.datetime.now(datetime.timezone.utc).isoformat()
        if error:
            self.record["error"] = str(error)
            with self.log.open("a") as log:
                log.write("FAILED: " + str(error) + "\n")
            shutil.copy2(self.log, self.compat_log)
        self.publish()
        return self.record["state"] == "success"
