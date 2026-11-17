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
SOURCE_DIRECTORIES = ("firmware", "scripts", "tools", "profiles")


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
    for directory in SOURCE_DIRECTORIES:
        for path in sorted((root / directory).rglob("*")):
            relative = path.relative_to(root).as_posix()
            if (path.is_file() and relative != "firmware/esp32/compile_commands.json"
                    and not {"__pycache__", ".pio-mcp-workspace"}.intersection(path.parts)):
                files[relative] = hashlib.sha256(path.read_bytes()).hexdigest()
    lock = root / "toolchain.lock.json"
    if lock.is_file():
        files[lock.name] = hashlib.sha256(lock.read_bytes()).hexdigest()
    return {"sha256": source_digest(files), "files": files}


def source_digest(files):
    return hashlib.sha256(json.dumps(files, sort_keys=True, separators=(",", ":")).encode()).hexdigest()


def _git_reader(root):
    # Read the checkout's own index and objects, including from linked worktrees.
    relocated = {"GIT_DIR", "GIT_COMMON_DIR", "GIT_WORK_TREE", "GIT_INDEX_FILE",
                 "GIT_OBJECT_DIRECTORY", "GIT_ALTERNATE_OBJECT_DIRECTORIES",
                 "GIT_CONFIG_PARAMETERS", "GIT_CONFIG_COUNT"}
    env = {key: value for key, value in os.environ.items() if key not in relocated}

    def read(*args):
        result = subprocess.run(
            ["git", "--no-replace-objects", "-c", "core.fsmonitor=false", *args],
            cwd=root, env=env, capture_output=True, check=True)
        return os.fsdecode(result.stdout)

    return read


def git_identity(root):
    read = _git_reader(root)
    try:
        if Path(read("rev-parse", "--show-toplevel").removesuffix("\n")).resolve() != root.resolve():
            raise ValueError("Expected the Git checkout root")
        head = read("rev-parse", "--verify", "HEAD^{commit}").strip()
        status = read("status", "--porcelain=v1", "-z", "--untracked-files=all",
                      "--ignore-submodules=none").split("\0")[:-1]
    except (OSError, ValueError, subprocess.CalledProcessError):
        return {"git_head": None, "working_tree_dirty": None, "git_status_porcelain": None}
    return {"git_head": head, "working_tree_dirty": bool(status), "git_status_porcelain": status}


def release_source(root, required_files=()):
    """Verify Git cleanliness and exclude ignored inputs from release evidence."""
    identity = git_identity(root)
    if not identity["git_head"]:
        raise ValueError("Cannot identify the source Git checkout and commit")
    if identity["working_tree_dirty"]:
        raise ValueError("Git source checkout is dirty; commit or remove local changes before packaging")
    read = _git_reader(root)
    try:
        tracked = read("ls-files", "-v", "--stage", "-z").split("\0")[:-1]
        ignored = read("ls-files", "--others", "--ignored", "--exclude-standard", "-z", "--",
                       *SOURCE_DIRECTORIES, "toolchain.lock.json").split("\0")[:-1]
    except (OSError, subprocess.CalledProcessError) as error:
        raise ValueError("Cannot identify tracked and ignored Git source inputs") from error
    tracked_source = set()
    regular_files = set()
    source_prefixes = tuple(directory + "/" for directory in SOURCE_DIRECTORIES)
    for entry in tracked:
        metadata, name = entry.split("\t", 1)
        flag, mode, _, _ = metadata.split()
        if flag != "H":
            raise ValueError("Cannot verify Git checkout with hidden index entries")
        if mode in ("100644", "100755"):
            regular_files.add(name)
        if name == "toolchain.lock.json" or name.startswith(source_prefixes):
            tracked_source.add(name)
    if not (tracked_source | set(required_files)) <= regular_files:
        raise ValueError("Release inputs must be regular tracked files")
    for name in ignored:
        path = Path(name)
        cache = path.parts[0] in ("scripts", "tools") and "__pycache__" in path.parts and path.suffix == ".pyc"
        if not (cache or name == "firmware/esp32/compile_commands.json"):
            raise ValueError("Ignored files within source directories must be removed before packaging")
    source = source_identity(root)
    if set(source["files"]) != tracked_source:
        raise ValueError("Build source inventory differs from tracked Git source files")
    if git_identity(root) != identity:
        raise ValueError("Git source changed while identifying release inputs")
    return identity["git_head"], source


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


def locked_library_versions(toolchain, features):
    versions = {name: version for name, version in toolchain["libraries"].items()
                if name != "arduinoFFT" or "Microphone" in features}
    if set(features) & {"Microphone", "BoopSensor", "VisorConnector"}:
        versions.update(toolchain["bundled_libraries"])
    return versions


def memory_metrics(output):
    result = {}
    flash = re.search(r"FLASH: code:(\d+), data:(\d+), headers:(\d+)", output)
    ram = re.search(r"RAM1: variables:(\d+).*free for local variables:(\d+)", output)
    ram2 = re.search(r"RAM2: variables:(\d+).*free for malloc/new:(\d+)", output)
    if flash:
        result["flash_bytes"] = sum(map(int, flash.groups()))
    if ram:
        result["ram1_variables_bytes"], result["ram1_available_bytes"] = map(int, ram.groups())
    if ram2:
        result["ram2_variables_bytes"], result["ram2_available_bytes"] = map(int, ram2.groups())
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
        self.record.update(git_identity(self.root))
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

    def compile(self, cli, sketch, features, output_directory=None, extra_flags=None, fqbn=FQBN,
                reuse_cache=False, jobs=None, build_properties=()):
        output_directory = output_directory or self.path / ("artifacts-" + str(len(self.record["results"])))
        command = [cli, "compile", "--fqbn", fqbn, "--warnings", "all",
                   "--build-path", str(output_directory / "objects"), "--output-dir", str(output_directory), str(sketch)]
        if not reuse_cache:
            command.insert(2, "--clean")
        if jobs is not None:
            command[2:2] = ["--jobs", str(jobs)]
        for value in build_properties:
            command[2:2] = ["--build-property", value]
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
            expected = locked_library_versions(self.record["toolchain"], features)
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
        if self.kind == "build" and "git_head" in self.record and any(
                self.record[key] != value for key, value in git_identity(self.root).items()):
            error = "Git checkout changed during this run; rerun for current evidence"
        self.record["state"] = "failed" if error or not valid else "success"
        self.record["finished_utc"] = datetime.datetime.now(datetime.timezone.utc).isoformat()
        if error:
            self.record["error"] = str(error)
            with self.log.open("a") as log:
                log.write("FAILED: " + str(error) + "\n")
            shutil.copy2(self.log, self.compat_log)
        self.publish()
        return self.record["state"] == "success"
