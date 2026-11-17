#!/usr/bin/env python3
"""P6 host conformance and Face Studio HTTP → compiled asset → firmware replay."""
import argparse
from fractions import Fraction
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import threading
import time
import urllib.error
import urllib.request

from timelines.compiler import TimelineError, compile_document, load_json
from timelines.simulator import replay_asset

ROOT = Path(__file__).resolve().parents[1]
SAMPLE = ROOT / "tools/helmet/timelines/three-channel.json"
SKETCH = ROOT / "firmware/PROT_OS7_MAX7219"
OUT = ROOT / "build/p6-evidence"


def check_number(curve, first, last, time):
    t = Fraction((256 * time) // 31, 256)
    u = 1-t
    q = (int(256*t), int(256*t*t), 256-int(256*u*u),
         int(256*t*t*(3-2*t)), 256-int(256*u*u*u))[curve]
    shift = Fraction((last-first)*q, 256)
    return first + (int(shift + Fraction(1,2)) if shift >= 0 else int(shift - Fraction(1,2)))


def conformance(asset, temporary):
    (temporary / "TimelineAsset.h").write_text(asset.header())
    target = temporary / "runtime"
    sources = SKETCH / "src/BehaviorCore"
    command = [os.environ.get("CXX", "clang++"), "-std=c++17", "-Wall", "-Wextra", "-Werror", "-pedantic",
               "-fsanitize=address,undefined", "-fno-sanitize-recover=all",
               "-DPROTOGEN_ANIMATION_V2=1", "-DPROTOGEN_ANIMATION_TIMELINES=1",
               "-I"+str(temporary), "-I"+str(SKETCH), "-I"+str(sources)]
    subprocess.run([*command, str(ROOT / "scripts/tests/timeline-runtime.cpp"),
                    str(sources/"AnimationV2.cpp"), str(sources/"Timeline.cpp"), "-o", str(target)], check=True)
    lines = subprocess.check_output([str(target)], text=True).splitlines()
    for line in lines[1:]:
        curve, first, last, time, value = map(int, line.split())
        expected = check_number(curve, first, last, time)
        if expected != value:
            raise RuntimeError(f"curve {curve}, {first}→{last} at {time} ms: expected {expected}, got {value}")
    return {**json.loads(lines[0]), "rounding_cases": len(lines)-1}


def frozen():
    return {r[0][5:]:bytes.fromhex(r[-2]) for r in (line.split() for line in
           (ROOT/"scripts/tests/fixtures/rendering-v1.tsv").read_text().splitlines())
            if r[0].startswith("rest.") and r[0] != "rest.unchanged"}


def independent_eye(base, sprite, dx, dy, closure):
    frame = bytearray(base)
    for y in range(8):
        for x in range(16):
            src_x, src_y = x-dx, y-dy
            lit = (0 <= src_x < 16 and closure <= src_y < 8 and
                   sprite[src_y*14+12+src_x//8] & (128 >> (src_x % 8)))
            offset = y*14+12+x//8
            mask = 128 >> (x % 8)
            frame[offset] = frame[offset] & ~mask | (mask if lit else 0)
    return bytes(frame)


def same_frame(frame, expected, title):
    actual = bytes.fromhex(frame["frame"])
    if actual != expected:
        index = next(i for i,(a,b) in enumerate(zip(actual,expected)) if a != b)
        diff = actual[index] ^ expected[index]
        x = index % 14 * 8 + (8 - diff.bit_length())
        y = index // 14
        raise RuntimeError(f"{title} at {frame['at_ms']} ms diverged at ({x},{y}): "
                           f"expected byte {expected[index]:02x}, actual {actual[index]:02x}")


def trace_prefix(trace, at):
    bound = (at+2)*1000
    return [line for line in trace.splitlines()
            if int(line.split("\t",1)[0]) < bound and line.split("\t")[1] not in
            ("totals","timeline-final","eeprom")]


def compare_seek(asset, positions, origin, inputs):
    continued = replay_asset(asset, positions, origin=origin, inputs=inputs, full_trace=True)
    trials = []
    for time, expected in zip(positions, continued["frames"]):
        fresh = replay_asset(asset, [time], origin=origin, inputs=inputs, full_trace=True)
        if fresh["frames"][0] != expected:
            same_frame(fresh["frames"][0], bytes.fromhex(expected["frame"]), "fresh/continuous seek")
            raise RuntimeError(f"status diverged at {time} ms")
        prefix = trace_prefix(continued["trace"], time)
        actual = trace_prefix(fresh["trace"], time)
        if prefix != actual:
            for i in range(max(len(prefix),len(actual))):
                want, got = (items[i] if i < len(items) else "<end>" for items in (prefix, actual))
                if want != got:
                    raise RuntimeError(f"seek {time} ms event {i+1}: expected {want}, got {got}")
        trials.append({"at_ms": time, "seek_ms": fresh["metrics"]["seek_ms"]})
    return continued, trials


def load_backend():
    spec = importlib.util.spec_from_file_location("helmet_tool", ROOT/"scripts/helmet-tool.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def http_round_trip():
    module = load_backend()
    transport = module.PreviewTransport()
    helmet = module.Helmet(transport)

    def start_server(owner):
        server = module.ThreadingHTTPServer(("127.0.0.1",0), module.make_handler(owner, 0))
        server.RequestHandlerClass = module.make_handler(owner, server.server_port)
        threading.Thread(target=server.serve_forever, daemon=True).start()
        return server

    def request(server, route, data=None, *, expected=200, origin=None, raw=None):
        url = f"http://127.0.0.1:{server.server_port}/api/timeline/{route}"
        headers = {"Content-Type":"application/json"}
        if origin:
            headers["Origin"] = origin
        body = raw if raw is not None else json.dumps(data).encode() if data is not None else None
        try:
            response = urllib.request.urlopen(urllib.request.Request(url, data=body, headers=headers), timeout=30)
        except urllib.error.HTTPError as error:
            response = error
        with response:
            if response.code != expected:
                raise RuntimeError(f"{route}: HTTP {response.code}, expected {expected}: {response.read()!r}")
            return json.load(response)

    server = start_server(helmet)
    try:
        source = request(server,"sample")
        # The editor creates the same document structure before submission.
        extra = {"at_ms": 4608, "duration_ms": 65, "region":"right", "channel":"y",
                 "from":0, "to":1, "easing":"linear", "priority":5}
        source["events"].append(extra)
        exported = request(server,"compile",{"document":source})
        imported = request(server,"compile",{"document_json":exported["json"]})
        if exported != imported:
            raise RuntimeError("editor compile/import round trip changed the asset")
        asset = compile_document(source)
        if exported["header"] != asset.header():
            raise RuntimeError("editor export differs from the shared CLI compiler")
        frames = request(server,"seek",{"document":source,"positions_ms":[0,16,5000]})
        direct = replay_asset(asset,[0,16,5000])
        if frames["frames"] != direct["frames"] or frames["progress"] != direct["progress"]:
            raise RuntimeError("HTTP preview is not the shared compiled asset replay")
        request(server,"compile",{"document_json":'{"version":1,"version":1}'},expected=400)
        request(server,"compile",{"document":source},expected=403,origin="https://example.invalid")
        request(server,"compile",raw=b"["*9+b"]"*9,expected=400)
        request(server,"compile",raw=b" "*32769,expected=400)
        request(server,"seek",{"document":source,"positions_ms":[True]},expected=400)
    finally:
        server.shutdown()
        server.server_close()
        transport.close()
    class LiveStub:
        simulation = False
    class Owner:
        transport = LiveStub()
    server = start_server(Owner())
    try:
        request(server,"seek",{"document":source,"positions_ms":[10]},expected=403)
    finally:
        server.shutdown()
        server.server_close()
    (OUT/"editor-export.json").write_text(exported["json"])
    (OUT/"TimelineAsset.h").write_text(exported["header"])
    return {"asset_sha256":hashlib.sha256(asset.data).hexdigest(),"editor_positions":[0,16,5000]}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--deep", action="store_true")
    args = parser.parse_args()
    OUT.mkdir(parents=True,exist_ok=True)
    source = load_json(SAMPLE.read_bytes())
    started = time.monotonic()
    asset = compile_document(source)
    compile_us = (time.monotonic()-started)*1e6
    with tempfile.TemporaryDirectory(prefix="protogen-timeline-test-") as folder:
        numbers = conformance(asset,Path(folder))
    f = frozen()
    model, timing = compare_seek(asset, [0,5000,1000,5000,16,224,16,5201], 0, ())
    by_time = {frame["at_ms"]:frame for frame in model["frames"]}
    same_frame(by_time[16],independent_eye(f["happy"],f["angry"],-2,0,3),"sprite, gaze and lid")
    same_frame(by_time[224],independent_eye(f["happy"],f["angry"],-2,1,3),"combined vertical clip")
    same_frame(by_time[5201],f["happy"],"clip completion")
    if model["progress"]["state"] != 2 or model["progress"]["outcomes"] != [1]*5:
        raise RuntimeError("sample events did not complete in order")
    environment = [[140000,"boop",100],[300000,"boop",1023],[360000,"visor",1],
                   [800000,"visor",0],[1000000,"audio",300],[1300000,"audio",0]]
    extra, extra_timing = compare_seek(asset, [250, 1200, 620, 1200, 990], (2**32-200)*1000, environment)
    frames = {frame["at_ms"]:frame for frame in extra["frames"]}
    same_frame(frames[250],f["vwv"],"boop masks all channels")
    if not frames[620]["hidden"] or frames[990]["hidden"]:
        raise RuntimeError("visibility transition did not survive rollover")
    if frames[1200]["state"][4] != "1" or extra["metrics"]["audio_dropped"]:
        raise RuntimeError("speaking or audio service missing during timeline playback")
    if args.deep:
        for at in range(0, 5001, 250):
            compare_seek(asset, [at,50,at], (2**32-200)*1000, environment)
    api = http_round_trip()
    (OUT/"sample.tsv").write_text(model["trace"])
    (OUT/"rollover.tsv").write_text(extra["trace"])
    report = {"asset_sha256":hashlib.sha256(asset.data).hexdigest(),"compiler_us":compile_us,
              "conformance":numbers,"seek_trials":timing+extra_timing,"editor":api,"model":model["metrics"]}
    (OUT/("deep.json" if args.deep else "fast.json")).write_text(json.dumps(report,indent=2)+"\n")
    print("PASS: canonical asset, independent three-channel pixels, exact random seeks, rollover, and Face Studio HTTP import/export")

if __name__ == "__main__":
    try:
        main()
    except (RuntimeError, TimelineError, ValueError, OSError, subprocess.SubprocessError) as error:
        sys.exit(f"{error}\nReproduce: python3 scripts/check-timelines.py")
