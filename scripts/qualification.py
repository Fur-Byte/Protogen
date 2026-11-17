#!/usr/bin/env python3
"""Bind qualification results to one source commit and immutable artifact bytes."""
import argparse
import datetime
import hashlib
import json
import math
from pathlib import Path
import platform
import re
import subprocess
import sys

from build_support import release_source, write_json

ROOT = Path(__file__).resolve().parents[1]
SCHEMA = json.loads((ROOT / "docs/validation/qualification-v1.schema.json").read_text())
GATES = {
    "software.compatibility": "F1",
    "host.integration": "F2",
    "host.soak-100x24h": "F2",
    "target.compilation": "F0",
    "release.provenance": "F1",
    "hardware.timing": "F3",
    "hardware.audio": "F3",
    "hardware.optical": "F3",
    "hardware.electrical": "F3",
    "hardware.wireless": "F3",
    "hardware.recovery": "F3",
    "hardware.one-hour": "F3",
    "experimental.approval": "F3",
}
TYPES = {"null": (type(None),), "string": (str,), "integer": (int,),
         "number": (int, float), "object": (dict,), "array": (list,)}
MAX_MANIFEST_BYTES = 1024 * 1024


def unique_keys(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise ValueError(f"duplicate JSON key: {key}")
        result[key] = value
    return result


def invalid_constant(value):
    raise ValueError(f"non-finite JSON value: {value}")


def load_manifest(path):
    with Path(path).open("rb") as stream:
        data = stream.read(MAX_MANIFEST_BYTES + 1)
    if len(data) > MAX_MANIFEST_BYTES:
        raise ValueError("qualification manifest exceeds 1 MiB")
    return json.loads(data, object_pairs_hook=unique_keys, parse_constant=invalid_constant)


def check_schema(value, schema, at):
    """Validate the standard JSON Schema keywords used in the bundled v1 spec."""
    if "$ref" in schema:
        check_schema(value, SCHEMA["$defs"][schema["$ref"].removeprefix("#/$defs/")], at)
        return
    if "anyOf" in schema:
        failures = []
        for option in schema["anyOf"]:
            try:
                check_schema(value, option, at)
                return
            except ValueError as error:
                failures.append(str(error))
        raise ValueError(f"{at}: no allowed shape ({'; '.join(failures)})")
    kinds = schema.get("type", [])
    if isinstance(kinds, str):
        kinds = [kinds]
    if kinds and all(type(value) not in TYPES[kind] for kind in kinds):
        raise ValueError(f"{at}: expected {' or '.join(kinds)}")
    if "const" in schema and (type(value) is not type(schema["const"]) or value != schema["const"]):
        raise ValueError(f"{at}: unsupported version")
    if "enum" in schema and value not in schema["enum"]:
        raise ValueError(f"{at}: value outside enum")
    if isinstance(value, str):
        if not schema.get("minLength", 0) <= len(value) <= schema.get("maxLength", 2048):
            raise ValueError(f"{at}: text length out of bounds")
        if "pattern" in schema and not re.fullmatch(schema["pattern"], value):
            raise ValueError(f"{at}: malformed text")
    if type(value) in (float, int):
        if type(value) is float and not math.isfinite(value):
            raise ValueError(f"{at}: non-finite number")
        if value < schema.get("minimum", -math.inf):
            raise ValueError(f"{at}: value below minimum")
    if isinstance(value, list):
        if not schema.get("minItems", 0) <= len(value) <= schema.get("maxItems", 256):
            raise ValueError(f"{at}: array size out of bounds")
        for index, item in enumerate(value):
            check_schema(item, schema["items"], f"{at}[{index}]")
    if isinstance(value, dict):
        if not schema.get("minProperties", 0) <= len(value) <= schema.get("maxProperties", 256):
            raise ValueError(f"{at}: object size out of bounds")
        missing = set(schema.get("required", [])) - value.keys()
        if missing:
            raise ValueError(f"{at}: missing {', '.join(sorted(missing))}")
        for key, item in value.items():
            field = schema.get("properties", {}).get(key, schema.get("additionalProperties", False))
            if field is False:
                raise ValueError(f"{at}: unexpected field {key}")
            check_schema(item, field, f"{at}.{key}")


def artifact_path(root, name):
    components = name.split("/")
    if (any(part in {"", ".", ".."} for part in components)
            or any(character in name for character in "\\:\x00\r\n")):
        raise ValueError(f"unsafe artifact path: {name!r}")
    root = root.resolve()
    file = root
    for component in components:
        file = file / component
        if file.is_symlink():
            raise ValueError(f"unsafe artifact path: {name!r}")
    if not file.resolve().is_relative_to(root):
        raise ValueError(f"unsafe artifact path: {name!r}")
    return file


def artifact_identity(root, name):
    file = artifact_path(root, name)
    if not file.is_file():
        raise ValueError(f"artifact is missing: {name}")
    with file.open("rb") as stream:
        digest = hashlib.file_digest(stream, "sha256").hexdigest()
    return {"path": name, "sha256": digest, "size_bytes": file.stat().st_size}


def validate_full_soak(path, source):
    aggregate = load_manifest(path)
    duration = 86_400_000_000
    target = {"seed_start": 0, "seed_count": 100, "virtual_duration_us": duration}
    blockers = ("failed_seeds", "incomplete_seeds", "missing_seeds", "duplicate_seeds", "unexpected_seeds")
    identity, coverage = aggregate.get("identity") or {}, aggregate.get("coverage") or {}
    if not (aggregate.get("version") == 1 and aggregate.get("status") == "PASS"
            and aggregate.get("evidence_level") == "F2" and aggregate.get("architecture_target_met") is True
            and aggregate.get("request") == target and aggregate.get("errors") == []
            and identity.get("source_sha") == source and identity.get("source_dirty") is False
            and coverage.get("passed_seed_ids") == list(range(100))
            and coverage.get("passed_seed_count") == 100
            and coverage.get("passed_virtual_duration_us") == 100 * duration
            and all(coverage.get(key) == [] for key in blockers)):
        raise ValueError("Release soak evidence must cover all 100 complete full-day seeds on this source")


def validate(document, artifact_root, expected_source):
    check_schema(document, SCHEMA, "manifest")
    if document["source_sha"] != expected_source:
        raise ValueError("manifest does not match expected source SHA")
    seen = set()
    for record in document["records"]:
        name = record["requirement_id"]
        if name in seen:
            raise ValueError(f"duplicate requirement: {name}")
        seen.add(name)
        if record["source_sha"] != expected_source:
            raise ValueError(f"{name}: record does not match expected source SHA")
        executed = record["result"] in {"PASS", "FAIL"}
        artifact, hardware = record["artifact"], record["hardware"]
        if executed and (not artifact or not record["command"]):
            raise ValueError(f"{name}: command and artifact required for an executed result")
        environment = record["environment"]["kind"]
        physical = record["evidence_level"] == "F3"
        if record["evidence_level"] in {"F1", "F2"} and environment != "host":
            raise ValueError(f"{name}: executable and integrated tests require a host environment")
        if physical and environment != "physical":
            raise ValueError(f"{name}: F3 requires a physical environment")
        if not physical and (environment == "physical" or hardware):
            raise ValueError(f"{name}: physical hardware claims require F3")
        if physical and executed:
            if not hardware:
                raise ValueError(f"{name}: F3 executed evidence requires identified hardware")
            identity_fields = hardware["boards"] + hardware["instruments"] + [hardware["operator"]]
            if any(value.strip().lower() in {"", "unknown", "n/a", "unassigned"} for value in identity_fields):
                raise ValueError(f"{name}: F3 executed evidence requires identified hardware and operator")
            datetime.date.fromisoformat(hardware["date"])
            if name == "hardware.one-hour" and record["result"] == "PASS" and hardware["duration_seconds"] < 3600:
                raise ValueError(f"{name}: successful stress run requires a continuous hour")
        if artifact and artifact_identity(artifact_root, artifact["path"]) != artifact:
            raise ValueError(f"{name}: artifact identity mismatch")
        if name == "host.soak-100x24h" and record["result"] == "PASS":
            validate_full_soak(artifact_path(artifact_root, artifact["path"]), expected_source)


def readiness(document):
    by_name = {record["requirement_id"]: record for record in document["records"]}
    gates = {}
    for name, minimum in GATES.items():
        record = by_name.get(name)
        status = record["result"] if record else "NOT_RUN"
        observed = record["evidence_level"] if record else None
        if status == "PASS" and (observed < minimum or record["outstanding_gates"]):
            status = "UNQUALIFIED"
        gates[name] = {"status": status, "minimum_evidence": minimum, "observed_evidence": observed}
    result = "GATES_SATISFIED" if all(gate["status"] == "PASS" for gate in gates.values()) else "PRODUCTION_BLOCKED"
    return {"schema_version": 1, "source_sha": document["source_sha"], "verdict": result, "gates": gates}


def pairs(items, convert=lambda x: x):
    parsed = {}
    for entry in items:
        key, separator, value = entry.partition("=")
        if not separator or not key or not value or key in parsed:
            raise ValueError(f"expected a unique name=value argument: {entry!r}")
        parsed[key] = convert(value)
    return parsed


def record(args):
    identity = release_source(ROOT, required_files=("docs/validation/qualification-v1.schema.json",))
    sha, _ = identity
    args.output.parent.mkdir(parents=True, exist_ok=True)
    log = artifact_path(args.artifact_root, args.log)
    log.parent.mkdir(parents=True, exist_ok=True)
    command = args.command[1:] if args.command and args.command[0] == "--" else args.command
    if not command:
        raise ValueError("record requires a command after --")
    flags = pairs(args.flag, lambda item: int(item) if item in ("0", "1") else item)
    toolchain = {"python": platform.python_version(), **pairs(args.tool)}
    with log.open("xb") as stream:
        command_line = json.dumps(command, ensure_ascii=True) + "\n"
        stream.write(command_line.encode())
        stream.flush()
        result = subprocess.run(command, cwd=ROOT, stdout=stream, stderr=subprocess.STDOUT, check=False)
    if release_source(ROOT, required_files=("docs/validation/qualification-v1.schema.json",)) != identity:
        raise ValueError("Git identity changed while collecting evidence")
    entry = {"requirement_id": args.requirement, "source_sha": sha, "feature_flags": flags, "toolchain": toolchain,
             "command": command, "result": "PASS" if result.returncode == 0 else "FAIL",
             "evidence_level": args.level, "artifact": artifact_identity(args.artifact_root, args.log),
             "environment": {"kind": args.environment, "description": platform.platform()},
             "hardware": None, "limitations": args.limitation, "outstanding_gates": args.outstanding}
    document = {"schema_version": 1, "source_sha": sha, "records": [entry]}
    validate(document, args.artifact_root, sha)
    write_json(args.output, document)
    print(f"{entry['result']}: {args.requirement} ({args.level}), {log}", flush=True)
    return 0 if result.returncode == 0 else 1


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="operation", required=True)
    evidence = commands.add_parser("record", help="execute a host or target-build command with a clean source guard")
    evidence.add_argument("--requirement", required=True)
    evidence.add_argument("--level", choices=("F0", "F1", "F2"), required=True)
    evidence.add_argument("--environment", choices=("static", "host", "target-build"), required=True)
    evidence.add_argument("--artifact-root", required=True, type=Path)
    evidence.add_argument("--log", required=True)
    evidence.add_argument("--output", required=True, type=Path)
    for option in ("flag", "tool", "limitation", "outstanding"):
        evidence.add_argument("--" + option, action="append", default=[])
    evidence.add_argument("command", nargs=argparse.REMAINDER)
    for name in ("verify", "merge"):
        check = commands.add_parser(name, help="check schema, source and every artifact hash")
        check.add_argument("manifest", nargs=1 if name == "verify" else "+", type=Path)
        check.add_argument("--artifact-root", required=True, type=Path)
        check.add_argument("--source", required=True)
        check.add_argument("--output", type=Path, required=name == "merge")
    args = parser.parse_args()
    try:
        if args.operation == "record":
            return record(args)
        entries = []
        for path in args.manifest:
            doc = load_manifest(path)
            validate(doc, args.artifact_root, args.source)
            entries.extend(doc["records"])
        merged = {"schema_version": 1, "source_sha": args.source, "records": entries}
        validate(merged, args.artifact_root, args.source)
        if args.operation == "merge":
            args.output.parent.mkdir(parents=True, exist_ok=True)
            write_json(args.output, merged)
        print(json.dumps(readiness(merged), indent=2))
        return 0
    except (ValueError, OSError) as error:
        parser.exit(1, f"{error}\n")


if __name__ == "__main__":
    sys.exit(main())
