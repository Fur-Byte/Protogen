#!/usr/bin/env python3
"""Run or aggregate bounded, deterministic P8 host twin soak shards."""
import argparse
from collections import Counter
import hashlib
import json
import math
import os
from pathlib import Path
import platform
import re
import selectors
import shlex
import signal
import subprocess
import sys
import tempfile
import time

if __package__:
    from .replay import METRICS, PROGRAM, ROOT, SOAK_DURATION_US, encode, integer
else:
    from replay import METRICS, PROGRAM, ROOT, SOAK_DURATION_US, encode, integer

VERSION = 1
TARGET_SEEDS = 100
FAULT_ROUND_MS = 6500
FAULT_INTERVAL_MS = 900_000
MAX_SHARD_BYTES = 256 * 1024
MAX_INPUT_BYTES = 1024 * 1024
COMPACT_BYTES = 8192
TRACE_BYTES = 512 * 1024
TRACE_WINDOW_US = 500_000
CONTINUOUS_CHECKS = {"audio-queue-limit", "spi-brightness-limit"}
PEAKS = {name for name in METRICS if name.endswith("_peak")}
QUEUE_LIMITS = {"uart_wire_in_peak": 512, "uart_wire_out_peak": 63,
                "uart_rx_peak": 319, "usb_rx_peak": 512, "audio_ring_peak": 512}
DROPS = {name for name in METRICS if name.endswith("_dropped")} | {"uart_rejected"}
SOAK_METRICS = ("virtual_duration_us", "feature_mask", "lost_acks", "uart_accepted_acks",
                "uart_duplicate_acks", "uart_expired_acks", "audio_retries", "audio_recoveries",
                "sensor_retries", "sensor_recoveries", "spi_transactions", "uart_rejected")
COUNTS = (set(METRICS) | set(SOAK_METRICS)) - PEAKS - DROPS - {"virtual_duration_us", "feature_mask"}
FEATURES = ("flight_recorder", "animation_v2", "animation_timelines", "audio_v2", "visor_connector",
            "supervision", "uart_auth", "microphone")


def digest(data):
    return hashlib.sha256(data).hexdigest()


def canonical(value):
    return json.dumps(value, sort_keys=True, separators=(",", ":"), allow_nan=False).encode()


def read_bounded(path, limit):
    with Path(path).open("rb") as stream:
        data = stream.read(limit + 1)
    if len(data) > limit:
        raise ValueError(f"{path} exceeds the {limit}-byte input limit")
    return data


def write_json(path, data, limit=MAX_SHARD_BYTES):
    payload = canonical(data) + b"\n"
    if len(payload) > limit:
        raise ValueError(f"{path.name} exceeds its {limit}-byte artifact limit")
    with tempfile.NamedTemporaryFile(dir=path.parent, delete=False) as stream:
        temporary = Path(stream.name)
    try:
        temporary.write_bytes(payload)
        temporary.replace(path)
    finally:
        temporary.unlink(missing_ok=True)


def tape(seed, duration_ms):
    events = []
    for round_index, start in enumerate(range(0, duration_ms - FAULT_ROUND_MS + 1, FAULT_INTERVAL_MS)):
        jitter, pose, audio = hashlib.sha256(f"protogen-p8/{VERSION}/{seed}/{round_index}".encode()).digest()[:3]
        selected = 1 + pose % 5
        stale = 1 + selected % 5
        recovered = 1 + stale % 5
        count = round_index + 1
        steps = [
            (20, "usb", "SET brightness=15"), (60, "expect", ["effective-brightness", 1]),
            (70, "usb", "SET brightness_approval=15"), (110, "expect", ["brightness-approval", 1]),
            (150, "hello", 2 * count - 1), (240, "drop-ack", 1), (260, "expression", selected),
            (320, "expect", ["expression", selected]), (340, "duplicate", 0),
            (460, "expect", ["lost-acks", count]), (470, "expect", ["uart-duplicate", count]),
            (490, "audio", 180 + audio % 101),
            (510, "hold", (510 + jitter % 30) * 1000), (550, "expression", stale),
            (1180, "expect", ["expression", selected]), (1190, "expect", ["uart-expired", 2 * count - 1]),
            (1250, "hold", 145000), (1260, "uart", "01" * 600 + "00"),
            (1450, "uart", "01" * 4), (1620, "uart", "00"),
            (1650, "sensor-stall", 1), (1650, "audio-stall", 1),
            (1910, "expect", ["sensor-faults", 6]), (2030, "sensor-stall", 0), (2030, "audio-stall", 0),
            (2540, "expect", ["sensor-faults", 0]),
            (2560, "audio", 0), (2560, "sample", 0),
            (2760, "expect", ["sensor-faults", 4]), (2770, "expect", ["audio-failure-reason", 3]),
            (2790, "sample", 510), (3100, "expect", ["sensor-faults", 0]),
            (3110, "expect", ["audio-running", 1]), (3150, "boop", 100),
            (3210, "expect", ["boop-active", 1]), (3220, "expect", ["audio-running", 0]),
            (3240, "boop", 1023), (3300, "visor", 1),
            (3510, "expect", ["visor-visible", 0]), (3520, "expect", ["display-blanked", 1]),
            (3570, "visor", 0), (3770, "expect", ["visor-visible", 1]),
            (3800, "button", 0), (5100, "expect", ["sleeping", 1]),
            (5110, "expect", ["display-blanked", 1]), (5120, "expect", ["audio-running", 0]),
            (5130, "button", 1), (5230, "button", 0), (5300, "button", 1),
            (5420, "expect", ["sleeping", 0]), (5430, "expect", ["display-blanked", 0]),
            (5450, "disconnect", 1), (5560, "disconnect", 0), (5730, "hello", 2 * count),
            (5900, "expression", recovered), (6040, "expect", ["uart-accepted", 2 * count - 1]),
            (6050, "reorder", 0), (6160, "expect", ["uart-bad-session", count]),
            (6170, "expect", ["expression", recovered]),
            # Keep a real old command within the current bridge and replay it out of order.
            (6200, "expression", recovered), (6290, "reorder", 0),
            (6440, "expect", ["uart-expired", 2 * count]), (6450, "expect", ["expression", recovered]),
        ]
        events.extend([(start + at) * 1000, operation, value] for at, operation, value in steps)
    # Project both millis() and micros() through a rollover in the first round.
    return {"version": 1, "origin_us": 2**32 * 1000 - 521_000 + seed % 17,
            "duration_us": duration_ms * 1000, "loop_us": 1000,
            "cosmetic_seed": seed, "epoch": 1 + seed % (2**32 - 1), "events": events}


def expected_checks(replay):
    return Counter(value[0] for _, op, value in replay["events"] if op == "expect")


def bounded_process(command, encoded, deadline, cap):
    """Read at most cap total bytes and kill the child group if either budget expires."""
    start = time.monotonic()
    buffers = {"stdout": bytearray(), "stderr": bytearray()}
    if start >= deadline:
        return None, "timeout", b"", b"", 0.0
    with tempfile.TemporaryFile() as input_file:
        input_file.write(encoded)
        input_file.seek(0)
        with subprocess.Popen(command, stdin=input_file, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                              start_new_session=True) as child, selectors.DefaultSelector() as selector:
            selector.register(child.stdout, selectors.EVENT_READ, "stdout")
            selector.register(child.stderr, selectors.EVENT_READ, "stderr")
            outcome = None
            size = 0
            try:
                while selector.get_map() and outcome is None:
                    if (left := deadline - time.monotonic()) <= 0:
                        outcome = "timeout"
                        break
                    for key, _ in selector.select(left):
                        chunk = os.read(key.fd, min(65536, cap - size + 1))
                        if not chunk:
                            selector.unregister(key.fd)
                        elif size + len(chunk) > cap:
                            outcome = "output-limit"
                            break
                        else:
                            buffers[key.data].extend(chunk)
                            size += len(chunk)
                if outcome is None:
                    child.wait(timeout=max(0, deadline - time.monotonic()))
            except subprocess.TimeoutExpired:
                outcome = "timeout"
            finally:
                if child.returncode is None:
                    try:
                        os.killpg(child.pid, signal.SIGKILL)
                    except ProcessLookupError:
                        pass
                    child.wait()
    return child.returncode, outcome, bytes(buffers["stdout"]), bytes(buffers["stderr"]), time.monotonic() - start


def observed_summary(raw, replay):
    state, checks, soak, eeprom, totals = [line.split("\t") for line in raw.decode("ascii").splitlines()[-5:]]
    if [state[1], checks[1], soak[1], eeprom[1], totals[1]] != ["state", "checks", "soak", "eeprom", "totals"]:
        raise ValueError("missing terminal twin records")
    metrics = dict(zip(METRICS, map(int, totals[3:]), strict=True))
    metrics.update(zip(SOAK_METRICS, map(int, soak[3:]), strict=True))
    if metrics["virtual_duration_us"] != replay["duration_us"] or metrics["inputs_processed"] != len(replay["events"]):
        raise ValueError("twin ended before the requested duration or skipped inputs")
    if any(metrics[name] > maximum for name, maximum in QUEUE_LIMITS.items()):
        raise ValueError("twin reported occupancy beyond a modeled queue bound")
    values = {}
    for entry in checks[3:]:
        name, value = entry.split("=")
        if name in values:
            raise ValueError("duplicate checkpoint summary")
        values[name] = int(value)
    for name in CONTINUOUS_CHECKS:
        if values.pop(name, 0) < 1:
            raise ValueError(f"no observations for {name}")
    if values != expected_checks(replay):
        raise ValueError("incomplete invariant coverage")
    mask = integer(metrics.pop("feature_mask"), 0, (1 << len(FEATURES)) - 1, "feature mask")
    return {"virtual_duration_us": metrics["virtual_duration_us"],
            "feature_flags": {flag: bool(mask & 1 << bit) for bit, flag in enumerate(FEATURES)},
            "invariant_counts": {name: int(value) for name, value in (entry.split("=") for entry in checks[3:])},
            "event_counts": {name: metrics[name] for name in COUNTS},
            "queue_peaks": {name: metrics[name] for name in PEAKS},
            "drop_and_rejection_counts": {name: metrics[name] for name in DROPS},
            "eeprom_sha256": digest(bytes.fromhex(eeprom[3])),
            "terminal_state_sha256": digest("\t".join(state[3:]).encode())}


def require_fault_activity(record):
    for queue in ("audio_ring_peak", "uart_wire_in_peak", "uart_rx_peak"):
        if record["queue_peaks"][queue] != QUEUE_LIMITS[queue]:
            raise ValueError(f"fault workload did not fill {queue}")
    for counter in ("audio_dropped", "uart_wire_dropped", "uart_rx_dropped", "uart_rejected"):
        if record["drop_and_rejection_counts"][counter] <= 0:
            raise ValueError(f"fault workload did not exercise {counter}")


def execute_replay(program, encoded, deadline, *, trace_from=None, asset=None):
    args = [str(program), "--soak"]
    if trace_from is not None:
        args += ["--trace-from-us", str(trace_from)]
    if asset is not None:
        args.append(str(asset))
    return bounded_process(args, encoded, deadline, TRACE_BYTES if trace_from is not None else COMPACT_BYTES)


def invariant_failure(stderr):
    for line in stderr.decode(errors="replace").splitlines():
        if match := re.fullmatch(r"P8-INVARIANT\t([0-9]+)\t([0-9]+)\t([a-z-]+)\t([0-9]+)\t([0-9]+)", line):
            at, event, name, expected, observed = match.groups()
            return {"virtual_us": int(at), "input_sequence": int(event), "invariant": name,
                    "expected": int(expected), "observed": int(observed)}
    return None


def replay_command(program, original):
    def local(path):
        resolved = path.resolve()
        return str(resolved.relative_to(ROOT) if resolved.is_relative_to(ROOT) else path)

    return shlex.join(["python3", "scripts/twin/soak.py", "replay", local(original), "--program", local(program),
                       "--wall-clock-budget-sec", "1200", "--output-dir", "build/p8-reproduction"])


def retain_failure(output, program, replay, encoded, record, deadline):
    original = output / "failure-input.json"
    write_json(original, replay, MAX_INPUT_BYTES)
    detail = {"original": record, "reproduce": replay_command(program, original)}
    if failure := record.get("first_divergence"):
        # Trim the suffix while retaining the exact time and event sequence of divergence.
        at = failure["virtual_us"]
        prefix = {**replay, "duration_us": min(replay["duration_us"], max(1000, at)),
                  "events": [event for event in replay["events"] if event[0] <= at]}
        compact = encode(prefix, soak=True).encode()
        if compact != encoded and time.monotonic() < deadline:
            code, outcome, _, stderr, _ = execute_replay(program, compact, deadline)
            if not outcome and code == 3 and invariant_failure(stderr) == failure:
                reduced = output / "failure-prefix.json"
                write_json(reduced, prefix, MAX_INPUT_BYTES)
                detail.update(reduced_input_sha256=digest(compact), reduced_reproduce=replay_command(program, reduced))
                encoded = compact
        if time.monotonic() < deadline:
            code, outcome, trace, stderr, _ = execute_replay(
                program, encoded, deadline, trace_from=max(0, at - TRACE_WINDOW_US))
            if trace:
                (output / "failure-tail.tsv").write_bytes(trace)
                detail.update(tail_sha256=digest(trace), tail_complete=outcome is None and code == 3 and
                              invariant_failure(stderr) == failure)
    write_json(output / "failure.json", detail)


def file_hash(path):
    value = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(65536), b""):
            value.update(block)
    return value.hexdigest()


def identity(program):
    def git(*args):
        return subprocess.check_output(["git", *args], cwd=ROOT, timeout=5).strip()

    return {"source_sha": git("rev-parse", "--verify", "HEAD").decode(),
            "source_dirty": bool(git("status", "--porcelain", "--untracked-files=normal")),
            "program_sha256": file_hash(program),
            "runner_sha256": digest(Path(__file__).read_bytes() + Path(__file__).with_name("replay.py").read_bytes())}


def semantic_hash(record):
    return digest(canonical({key: value for key, value in record.items()
                             if key not in {"semantic_sha256", "runtime_seconds", "diagnostic"}}))


def run(args):
    deadline = time.monotonic() + args.wall_clock_budget_sec
    source = identity(args.program)
    output = args.output_dir
    output.mkdir(parents=True)
    report = {"version": VERSION, "evidence_level": "F2", "identity": source,
              "environment": {"platform": platform.platform(), "python": platform.python_version()},
              "command": ["python3", *sys.argv],
              "request": {"seed_start": args.seed_start, "seed_count": args.seed_count,
                          "virtual_duration_us": args.virtual_duration_ms * 1000},
              "budgets": {"wall_clock_sec": args.wall_clock_budget_sec, "per_seed_sec": args.per_seed_timeout_sec,
                          "compact_output_bytes": COMPACT_BYTES, "failure_tail_bytes": TRACE_BYTES,
                          "max_retained_failures": 1},
              "status": "INCOMPLETE",
              "seeds": [{"seed": seed, "status": "INCOMPLETE", "reason": "not-started",
                         "virtual_duration_us": None} for seed in range(args.seed_start, args.seed_start + args.seed_count)]}
    for record in report["seeds"]:
        record["semantic_sha256"] = semantic_hash(record)
    manifest = output / "shard.json"
    write_json(manifest, report)
    retained = False
    for record in report["seeds"]:
        if time.monotonic() >= deadline:
            break
        seed = record["seed"]
        replay = tape(seed, args.virtual_duration_ms)
        encoded = encode(replay, soak=True).encode()
        seed_deadline = min(deadline, time.monotonic() + args.per_seed_timeout_sec)
        code, outcome, stdout, stderr, runtime = execute_replay(args.program, encoded, seed_deadline)
        record.update(input_sha256=digest(encoded), runtime_seconds=runtime)
        if outcome == "timeout":
            record["reason"] = "wall-clock-budget" if seed_deadline == deadline else "seed-timeout"
        elif outcome == "output-limit":
            record.update(status="FAIL", reason=outcome)
        elif code != 0:
            record.update(status="FAIL", reason="twin-exited", diagnostic=stderr.decode(errors="replace")[:512])
            if failure := invariant_failure(stderr):
                record.update(reason="invariant", first_divergence=failure, virtual_duration_us=failure["virtual_us"])
        else:
            try:
                summary = observed_summary(stdout, replay)
                require_fault_activity(summary)
                record.update(summary, status="PASS")
                del record["reason"]
            except (IndexError, ValueError) as error:
                record.update(status="FAIL", reason="invalid-twin-output", diagnostic=str(error)[:512])
        record["semantic_sha256"] = semantic_hash(record)
        write_json(manifest, report)
        if record["status"] == "FAIL" and not retained:
            retain_failure(output, args.program, replay, encoded, record, seed_deadline)
            retained = True
    counts = Counter(seed["status"] for seed in report["seeds"])
    report["status"] = "FAIL" if counts["FAIL"] else "INCOMPLETE" if counts["INCOMPLETE"] else "PASS"
    report["coverage"] = {"passed_seed_count": counts["PASS"],
                          "passed_virtual_duration_us": counts["PASS"] * report["request"]["virtual_duration_us"]}
    write_json(manifest, report)
    print(f"{report['status']} {counts['PASS']}/{args.seed_count} completed seeds. {manifest}")
    return {"PASS": 0, "FAIL": 1, "INCOMPLETE": 2}[report["status"]]


def validate_shard(shard, target_duration):
    if shard["version"] != VERSION or shard["evidence_level"] != "F2":
        raise ValueError("incompatible shard schema")
    source = shard["identity"]
    if set(source) != {"source_sha", "source_dirty", "program_sha256", "runner_sha256"} or \
            not re.fullmatch(r"[0-9a-f]{40}", source["source_sha"]) or type(source["source_dirty"]) is not bool or \
            any(not re.fullmatch(r"[0-9a-f]{64}", source[key]) for key in ("program_sha256", "runner_sha256")):
        raise ValueError("invalid shard identity")
    req = shard["request"]
    start = integer(req["seed_start"], 0, 2**32 - 1, "shard seed start")
    count = integer(req["seed_count"], 1, min(TARGET_SEEDS, 2**32 - start), "shard seed count")
    if req["virtual_duration_us"] != target_duration or len(shard["seeds"]) != count:
        raise ValueError("shard duration or seed count differs from declared request")
    for seed, record in zip(range(start, start + count), shard["seeds"], strict=True):
        if type(record["seed"]) is not int or record["seed"] != seed or record["semantic_sha256"] != semantic_hash(record):
            raise ValueError("shard seed identity or semantic hash mismatch")
        status = record["status"]
        if record.get("reason") != "not-started":
            runtime = record["runtime_seconds"]
            if type(runtime) not in (int, float) or not math.isfinite(runtime) or runtime < 0:
                raise ValueError("invalid seed runtime")
            if not re.fullmatch(r"[0-9a-f]{64}", record["input_sha256"]):
                raise ValueError("invalid workload hash")
        if status == "INCOMPLETE":
            if record["virtual_duration_us"] is not None or record["reason"] not in {
                "not-started", "seed-timeout", "wall-clock-budget"
            }:
                raise ValueError("invalid incomplete seed")
        elif status == "PASS":
            if record["virtual_duration_us"] != target_duration:
                raise ValueError("passing seed has incomplete duration")
            replay = tape(seed, target_duration // 1000)
            expected = expected_checks(replay)
            inv = record["invariant_counts"]
            if (set(inv) != expected.keys() | CONTINUOUS_CHECKS or
                    any(type(value) is not int or value < 1 for value in inv.values()) or
                    any(inv[name] != count for name, count in expected.items()) or
                    record["input_sha256"] != digest(encode(replay, soak=True).encode()) or
                    record["event_counts"]["inputs_processed"] != len(replay["events"]) or
                    set(record["feature_flags"]) != set(FEATURES) or
                    any(type(value) is not bool for value in record["feature_flags"].values())):
                raise ValueError("passing seed has incomplete or mismatched invariant coverage")
            for key, names in (("event_counts", COUNTS), ("queue_peaks", PEAKS), ("drop_and_rejection_counts", DROPS)):
                if set(record[key]) != names or any(type(n) is not int or n < 0 for n in record[key].values()):
                    raise ValueError(f"invalid {key}")
            if any(record["queue_peaks"][name] > maximum for name, maximum in QUEUE_LIMITS.items()):
                raise ValueError("modeled queue capacity exceeded")
            require_fault_activity(record)
            for key in ("eeprom_sha256", "terminal_state_sha256"):
                if not re.fullmatch(r"[0-9a-f]{64}", record[key]):
                    raise ValueError(f"invalid {key}")
        elif status != "FAIL":
            raise ValueError("invalid seed outcome")
    statuses = Counter(record["status"] for record in shard["seeds"])
    status = "FAIL" if statuses["FAIL"] else "INCOMPLETE" if statuses["INCOMPLETE"] else "PASS"
    # An interruption can leave the initial INCOMPLETE manifest in place.
    if shard["status"] != status and shard["status"] != "INCOMPLETE":
        raise ValueError("shard status conflicts with its records")
    if "coverage" in shard and shard["coverage"] != {
        "passed_seed_count": statuses["PASS"], "passed_virtual_duration_us": statuses["PASS"] * target_duration
    }:
        raise ValueError("shard coverage summary conflicts with its records")
    return source, shard["status"]


def aggregate(args):
    target_duration = args.virtual_duration_ms * 1000
    requested = set(range(args.seed_start, args.seed_start + args.seed_count))
    records = {}
    duplicate_seeds, unexpected_seeds = set(), set()
    source = None
    errors, shards = [], []
    profiles = set()
    for path in args.shards:
        try:
            raw = read_bounded(path, MAX_SHARD_BYTES)
            shard = json.loads(raw)
            shard_source, status = validate_shard(shard, target_duration)
            if source is not None and source != shard_source:
                raise ValueError("shard source or executable identity differs")
            source = shard_source
            shards.append({"path": str(path), "sha256": digest(raw), "status": status})
            for record in shard["seeds"]:
                seed = record["seed"]
                if record["status"] == "PASS":
                    profiles.add(canonical(record["feature_flags"]))
                if seed not in requested:
                    unexpected_seeds.add(seed)
                elif seed in records:
                    duplicate_seeds.add(seed)
                else:
                    records[seed] = record
        except (OSError, IndexError, KeyError, TypeError, ValueError) as error:
            errors.append({"path": str(path), "error": str(error)[:512]})
    passed = sorted(seed for seed, record in records.items() if record["status"] == "PASS" and seed not in duplicate_seeds)
    failed = sorted(seed for seed, record in records.items() if record["status"] == "FAIL")
    incomplete = sorted(seed for seed, record in records.items() if record["status"] == "INCOMPLETE")
    missing = sorted(requested - records.keys())
    if len(profiles) > 1:
        errors.append({"error": "passing seeds disagree on feature flags"})
    unfinished = [shard["path"] for shard in shards if shard["status"] == "INCOMPLETE"]
    status = ("FAIL" if errors or duplicate_seeds or unexpected_seeds or failed
              else "INCOMPLETE" if missing or incomplete or unfinished else "PASS")
    architecture_target_met = (status == "PASS" and args.seed_start == 0 and args.seed_count == TARGET_SEEDS
                               and target_duration == SOAK_DURATION_US and not source["source_dirty"])
    args.output_dir.mkdir(parents=True)
    manifest = args.output_dir / "aggregate.json"
    write_json(manifest, {"version": VERSION, "evidence_level": "F2", "identity": source, "status": status,
                          "request": {"seed_start": args.seed_start, "seed_count": args.seed_count,
                                      "virtual_duration_us": target_duration},
                          "architecture_target_met": architecture_target_met, "shards": shards, "errors": errors,
                          "unfinished_shards": unfinished,
                          "coverage": {"passed_seed_count": len(passed), "passed_seed_ids": passed,
                                       "passed_virtual_duration_us": len(passed) * target_duration,
                                       "failed_seeds": failed, "incomplete_seeds": incomplete, "missing_seeds": missing,
                                       "unexpected_seeds": sorted(unexpected_seeds), "duplicate_seeds": sorted(duplicate_seeds)}})
    print(f"{status} {len(passed)}/{args.seed_count} completed seeds. {manifest}")
    return {"PASS": 0, "FAIL": 1, "INCOMPLETE": 2}[status]


def replay_file(args):
    replay = json.loads(read_bounded(args.input, MAX_INPUT_BYTES))
    encoded = encode(replay, soak=True).encode()
    deadline = time.monotonic() + args.wall_clock_budget_sec
    code, outcome, stdout, stderr, runtime = execute_replay(
        args.program, encoded, deadline, trace_from=args.trace_from_us, asset=args.asset)
    args.output_dir.mkdir(parents=True)
    report = {"input_sha256": digest(encoded), "runtime_seconds": runtime, "returncode": code, "reason": outcome,
              "program_sha256": file_hash(args.program), "asset_sha256": file_hash(args.asset) if args.asset else None,
              "first_divergence": invariant_failure(stderr), "diagnostic": stderr.decode(errors="replace")[:512]}
    if args.trace_from_us is not None:
        (args.output_dir / "trace.tsv").write_bytes(stdout)
    if code == 0 and outcome is None:
        report.update(observed_summary(stdout, replay))
    write_json(args.output_dir / "replay.json", report)
    return 2 if outcome == "timeout" else int(bool(outcome or code))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="action", required=True)
    run_parser = commands.add_parser("run", help="execute a seed shard")
    aggregate_parser = commands.add_parser("aggregate", help="verify exact coverage of a requested seed range")
    replay_parser = commands.add_parser("replay", help="replay a reproducible fault input")
    for item in (run_parser, aggregate_parser):
        item.add_argument("--seed-start", type=int, required=True)
        item.add_argument("--seed-count", type=int, required=True)
        item.add_argument("--virtual-duration-ms", type=int, required=True)
    for item in (run_parser, aggregate_parser, replay_parser):
        item.add_argument("--output-dir", type=Path, required=True, help="new artifact directory")
    for item in (run_parser, replay_parser):
        item.add_argument("--program", type=Path, default=PROGRAM)
        item.add_argument("--wall-clock-budget-sec", type=float, required=True)
    run_parser.add_argument("--per-seed-timeout-sec", type=float, required=True)
    aggregate_parser.add_argument("shards", nargs="*", type=Path)
    replay_parser.add_argument("input", type=Path)
    replay_parser.add_argument("--asset", type=Path)
    replay_parser.add_argument("--trace-from-us", type=int)
    args = parser.parse_args()
    try:
        if args.action != "replay":
            integer(args.seed_start, 0, 2**32 - 1, "seed start")
            integer(args.seed_count, 1, min(TARGET_SEEDS, 2**32 - args.seed_start), "seed count")
            integer(args.virtual_duration_ms, FAULT_ROUND_MS, SOAK_DURATION_US // 1000, "virtual duration in ms")
        elif args.trace_from_us is not None:
            integer(args.trace_from_us, 0, SOAK_DURATION_US, "trace start in microseconds")
        if args.action != "aggregate":
            args.program = args.program.resolve(strict=True)
            budget = args.wall_clock_budget_sec
            if not math.isfinite(budget) or not 0 < budget <= 7200:
                raise ValueError("wall-clock budget must be positive and at most 7200 seconds")
        if args.action == "run" and not 0 < args.per_seed_timeout_sec <= budget:
            raise ValueError("per-seed timeout must be positive and within the wall-clock budget")
        if args.action == "aggregate" and len(args.shards) > TARGET_SEEDS:
            raise ValueError("too many shard inputs")
        return {"run": run, "aggregate": aggregate, "replay": replay_file}[args.action](args)
    except (OSError, ValueError, subprocess.SubprocessError) as error:
        print(f"Soak error: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
