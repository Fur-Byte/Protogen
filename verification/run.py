"""Run pinned formal checks; never treat incomplete analysis as verification."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
HERE = ROOT / "verification"
ESP = ROOT / "firmware/esp32/src"
UART = ROOT / "firmware/PROT_OS7_MAX7219/src/HelmetProtocol"
PANEL = ROOT / "firmware/PROT_OS7_MAX7219/src/Max72xxPanel"
ANIMATION = ROOT / "firmware/PROT_OS7_MAX7219/src/BehaviorCore"
TOOLS = Path(os.environ.get("PROTOGEN_VERIFY_TOOLS", Path.home() / ".local/share/protogen-verification"))
CBMC = TOOLS / "cbmc-6.11.0/usr/bin/cbmc"
LEAN = TOOLS / "lean-4.36.0-linux/bin"
OUT = ROOT / "build/formal"
AXIOMS = {"propext", "Classical.choice", "Quot.sound"}

# (case, source, input limit, unwinding bound)
FAST = (
    ("replay-step", "replay_step", None, 5),
    ("cobs", "cobs", 4, 8),
    ("cobs-malformed", "cobs_malformed", 4, 8),
    ("wire-integers", "wire_integers", None, 8),
    ("sensor-current", "sensor_current", None, 8),
    ("display", "display", None, 114),
    ("packet-header", "packet_header", None, 6),
    ("cobs-writer-step", "cobs_writer_step", None, 4),
    ("cobs-reader-step", "cobs_reader_step", None, 4),
    ("crc-byte", "crc_byte", None, 10),
    ("nonce", "nonce", None, 16),
    ("timers", "timers", None, 4),
    ("easing", "easing", None, 4),
    ("audio-ring", "audio_ring", None, 4),
    ("config-layout", "config_layout", None, 4),
    ("brightness", "brightness", None, 4),
    ("action-cursor", "action_cursor", None, 4),
    ("action-fields", "action_fields", None, 4),
)
EXTENDED = (
    ("replay-three", "replay", None, 5),
    ("display-existing", "display_existing", None, 114),
    ("cobs-extended", "cobs", 12, 16),
    ("cobs-malformed-extended", "cobs_malformed", 12, 16),
)

def run(command, name, seconds, cwd=ROOT):
    path = OUT / (name + ".log")
    with path.open("w") as log:
        try:
            result = subprocess.run(command, cwd=cwd, stdout=log, stderr=subprocess.STDOUT,
                                    timeout=seconds, check=False)
            return result.returncode, path
        except subprocess.TimeoutExpired:
            return None, path
        except OSError as exc:
            log.write(str(exc))
            return -1, path

def report(name, status, **details):
    result = {"case": name, "status": status, **details}
    print(f"{name}: {status}", flush=True)
    return result

def classify_cbmc(code, path):
    if code is None:
        return "TIMEOUT", []
    try:
        messages = json.loads(path.read_text())
        properties = [p for m in messages for p in m.get("result", [])]
        statuses = [m.get("cProverStatus") for m in messages if "cProverStatus" in m]
    except (ValueError, TypeError, AttributeError):
        return "INCONCLUSIVE", []
    failed = [p for p in properties if p.get("status") == "FAILURE"]
    if code == 10 and failed:
        if any(p.get("sourceLocation", {}).get("propertyClass") == "unwind"
               or ".unwind." in p.get("property", "") for p in failed):
            return "INCONCLUSIVE", properties
        return "COUNTEREXAMPLE", properties
    if code == 0 and properties and statuses == ["success"] and all(
            p.get("status") == "SUCCESS" for p in properties):
        return "VERIFIED", properties
    if any(m.get("messageType") == "ERROR" and
           any(word in m.get("messageText", "").lower() for word in ("unsupported", "not supported", "parsing error"))
           for m in messages):
        return "UNSUPPORTED", properties
    return "INCONCLUSIVE", properties

def verify_case(case, *, mutation=None):
    name, source, limit, unwind = case
    with tempfile.TemporaryDirectory(prefix="protogen-formal-") as tmp:
        command = [str(CBMC), str(HERE / "cbmc" / (source + ".c")),
                   "--i386-linux", "--unwind", str(unwind), "--unwinding-assertions",
                   "--json-ui", "--trace", "--bounds-check", "--pointer-check",
                   "--signed-overflow-check", "--div-by-zero-check", "--undefined-shift-check"]
        if mutation:
            header, old, new = mutation
            content = header.read_text()
            if content.count(old) != 1:
                return report(name, "INCONCLUSIVE", reason="mutation point changed")
            (Path(tmp) / header.name).write_text(content.replace(old, new))
            command += ["-I", tmp]
        command += ["-I", str(ESP), "-I", str(UART), "-I", str(PANEL), "-I", str(ANIMATION), "-I", str(UART.parent.parent)]
        # Debian installations without the system multilib package can use its
        # extracted headers. CBMC models i386, not actual CPU instructions.
        extracted = TOOLS / "ilp32-headers/usr/include/x86_64-linux-gnu"
        if extracted.is_dir():
            command += ["-I", str(extracted), "-I", "/usr/include/x86_64-linux-gnu"]
        if limit is not None:
            command += [f"-DINPUT_LIMIT={limit}", "--unsigned-overflow-check"]
        seconds = 900 if name == "cobs-extended" else 180
        code, path = run(command, name, seconds)
    status, properties = classify_cbmc(code, path)
    failures = [p.get("property") for p in properties if p.get("status") == "FAILURE"]
    if mutation:
        # Require an actual harness assertion failure, not a parse error,
        # unwinding failure, or other incidental error.
        killed = status == "COUNTEREXAMPLE" and any(
            p.startswith("main.assertion.") for p in failures if p)
        status = "MUTATION_DETECTED" if killed else "INCONCLUSIVE"
    return report(name, status, properties=len(properties), failures=failures,
                  exit_code=code, unwind=unwind, input_limit=limit, timeout_seconds=seconds, artifact=path.name)

def verify_lean():
    source = HERE / "lean/Protogen.lean"
    text = source.read_text()
    if re.search(r"\b(sorry|admit|axiom|native_decide)\b", text):
        return report("lean", "INCONCLUSIVE", reason="forbidden proof construct")
    names = re.findall(r"(?m)^theorem (\w+)", text)
    # Run Lean directly so the axiom report is fresh even if Lake has cached a build.
    code, path = run([str(LEAN / "lean"), str(source)], "lean", 120, HERE / "lean")
    if code is None:
        return report("lean", "TIMEOUT", artifact=path.name)
    audit = {}
    for name, dependencies in re.findall(
            r"'Protogen\.(\w+)' depends on axioms: \[([^\]]*)\]", path.read_text()):
        audit[name] = set(dependencies.split(", ")) if dependencies else set()
    for name in re.findall(r"'Protogen\.(\w+)' does not depend on any axioms", path.read_text()):
        audit[name] = set()
    passed = code == 0 and bool(names) and set(names) == set(audit) and all(
        dependencies <= AXIOMS for dependencies in audit.values())
    return report("lean", "VERIFIED" if passed else "INCONCLUSIVE",
                  theorems=len(audit), exit_code=code, artifact=path.name,
                  axioms={name: sorted(deps) for name, deps in audit.items()})

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--tier", choices=("fast", "extended"), default="fast")
    args = parser.parse_args()
    OUT.mkdir(parents=True, exist_ok=True)
    def source_identity():
        # Cover all tracked production/verification sources and new files,
        # excluding generated caches and ignored build output.
        paths = subprocess.check_output(["git", "ls-files", "-z", "--cached", "--others", "--exclude-standard", "--", "firmware", "verification", "scripts/tests", ".github/workflows/formal-verification.yml"], cwd=ROOT).decode().split("\0")
        return {name: hashlib.sha256((ROOT / name).read_bytes()).hexdigest()
                for name in sorted(set(paths) - {""}) if (ROOT / name).is_file()}
    source = source_identity()
    commit = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip()
    results = []
    for tool, expected in ((CBMC, "6.11.0"), (LEAN / "lean", "4.36.0")):
        code, path = run([str(tool), "--version"], tool.name + "-version", 15)
        if code != 0 or not re.search(r"\b" + re.escape(expected) + r"\b", path.read_text()):
            results.append(report(tool.name, "UNSUPPORTED", reason="missing or incorrect pinned tool"))
    if not results:
        results.append(verify_lean())
        for case in FAST + (EXTENDED if args.tier == "extended" else ()):
            results.append(verify_case(case))
        if args.tier == "fast":
            results.extend(report(case[0], "NOT_RUN") for case in EXTENDED)
        results.append(verify_case(("replay-mutation", "replay_step", None, 5),
            mutation=(ESP / "ReplayCore.h",
                      "*bits = delta >= PROTOGEN_REPLAY_WIDTH ? 1 : (*bits << delta) | 1;",
                      "*bits = 0;")))
        results.append(verify_case(("display-mutation", "display", None, 114),
            mutation=(PANEL / "PanelGeometry.h", "const uint8_t mask = 1u << y;", "const uint8_t mask = 0;")))
        results.append(verify_case(("cobs-mutation", "cobs", 4, 8),
            mutation=(UART / "Cobs.h", "out[s->used] = 0;", "out[s->used] = 1;")))
        results.append(verify_case(("nonce-mutation", "nonce", None, 16),
            mutation=(ESP / "NonceCore.h", "nonce[2] = role;", "nonce[2] = 0;")))
        results.append(verify_case(("timer-mutation", "timers", None, 4),
            mutation=(UART / "TimeCore.h", "elapsed >= interval ? 0 : interval - elapsed", "elapsed >= interval ? 1 : interval - elapsed")))
        results.append(verify_case(("ring-mutation", "audio_ring", None, 4),
            mutation=(UART.parent.parent / "AudioRing.h", "return (uint16_t)(counter + 1);", "return counter;")))
        results.append(verify_case(("action-mutation", "action_fields", None, 4),
            mutation=(UART / "PacketCore.h", "duration <= 6000", "duration <= 65535")))
    else:
        results.extend(report(case[0], "NOT_RUN") for case in FAST + EXTENDED)
        results.append(report("lean", "NOT_RUN"))
    unchanged = source == source_identity()
    if not unchanged:
        results.append(report("source-integrity", "INCONCLUSIVE", reason="inputs changed during verification"))
    summary = {"tier": args.tier, "commit": commit, "source": source,
               "inputs_unchanged": unchanged, "results": results}
    (OUT / "summary.json").write_text(json.dumps(summary, indent=2) + "\n")
    return int(any(row["status"] not in {"VERIFIED", "MUTATION_DETECTED", "NOT_RUN"}
                   for row in results))

if __name__ == "__main__":
    sys.exit(main())
