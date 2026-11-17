"""Reset-and-replay timeline preview via the real sketch."""
from pathlib import Path
import json
import subprocess
import sys
import tempfile
import threading
import time

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts/twin"))
import replay
from .compiler import TimelineError, integer

FLAGS = ("-DPROTOGEN_ANIMATION_V2=1 -DPROTOGEN_ANIMATION_TIMELINES=1 "
         "-DPROTOGEN_CACHE_FACE=1 -DPROTOGEN_FLIGHT_RECORDER=1")
PROGRAM = ROOT / "build/twin-timeline/twin"
_LOCK = threading.Lock()
_ready = False


def build():
    global _ready
    if _ready:
        return
    result = subprocess.run(["make", "-C", str(ROOT / "scripts/twin"), "BUILD=../../build/twin-timeline",
                             "EXTRA_FLAGS=" + FLAGS], capture_output=True, text=True, timeout=90)
    if result.returncode:
        raise RuntimeError("Timeline simulator build failed:\n" + result.stderr[-4000:])
    _ready = True


def replay_asset(asset, positions, *, origin=0, inputs=(), full_trace=False):
    requested = list(positions)
    if not requested or len(requested) > 16:
        raise TimelineError("positions_ms", "expected 1–16 preview positions")
    maximum = asset.document["duration_ms"] + 100
    positions = [integer(at, 0, maximum, "positions_ms") for at in requested]
    last = max(positions)
    if not _LOCK.acquire(blocking=False):
        raise TimelineError("$", "simulator busy")
    try:
        build()
        started = time.monotonic()
        selected = [list(event) for event in inputs if event[0] <= last * 1000]
        # Model offsets are from the timer origin. The asset begins at 1 ms
        # after the SPI setup transaction on the pinned 4 MHz model.
        for event in selected:
            event[0] += 1000
        scenario = dict(version=1, origin_us=origin, duration_us=max(1000, last*1000), loop_us=1000,
                        cosmetic_seed=0, epoch=1, events=selected)
        with tempfile.TemporaryDirectory(prefix="protogen-timeline-") as tmp:
            path = Path(tmp) / "clip.bin"
            path.write_bytes(asset.data)
            trace, metrics = replay.execute(scenario, program=PROGRAM, asset=path)
    finally:
        _LOCK.release()
    rows = [line.split("\t") for line in trace.splitlines()]
    boot = next(int(r[0]) for r in rows if r[1] == "timeline-origin")
    if boot != 1000:
        raise RuntimeError(f"unexpected model startup at {boot} us")
    # The twin labels a frame by completion of bounded SPI work. Within the
    # 1 ms opportunity, up to 14*2*8 bytes at 4 MHz take 448 us. Select the
    # frame committed by the opportunity beginning at the requested time.
    display = [(int(r[0]), r) for r in rows if r[1] == "state"]
    power = [(int(r[0]), r[4]) for r in rows
             if r[1] == "spi" and r[3].startswith("0c")]
    frames = []
    for at in positions:
        boundary = boot + (at+1)*1000
        row = next((r for t, r in reversed(display) if t < boundary), None)
        if row is None:
            raise RuntimeError("simulator did not emit an initial frame")
        frames.append({"at_ms": at, "frame": row[-1], "state": row[3:-1],
                       "hidden": next((value == "1" for t, value in reversed(power) if t < boundary), True)})
    final = next(r for r in rows if r[1] == "timeline-final")
    result = {"frames": frames, "progress": {**dict(zip(
        ("state", "next", "started", "rejected", "skipped"), map(int, final[3:8]))),
        "outcomes": [int(code) for code in final[8]]},
        "metrics": {**metrics, "seek_ms": (time.monotonic()-started)*1000,
                    "transactions": int(final[10]), "spi_bytes": int(final[11]),
                    "compositions": int(final[12]), "cache_hits": int(final[13])}}
    if full_trace:
        result["trace"] = trace
    return result
