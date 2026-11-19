#!/usr/bin/env python3
"""Link the canonical compiler's header into pinned Teensy images."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess

from build_support import BuildRun, check_toolchain, find_cli, write_json
from sketch_features import ALL_FEATURES, configure_features
from timelines.compiler import MAX_BYTES, compile_document, load_json

ROOT = Path(__file__).resolve().parents[1]
SAMPLE = ROOT / "tools/helmet/timelines/three-channel.json"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--document", type=Path, default=SAMPLE)
    args = parser.parse_args()
    with args.document.open("rb") as stream:
        asset = compile_document(load_json(stream.read(MAX_BYTES + 1)))
    run = BuildRun(ROOT, "timeline-footprint")
    try:
        sketch = run.snapshot()
        (sketch / "TimelineAsset.h").write_text(asset.header())
        source = sketch / "PROT_OS7_MAX7219.ino"
        source.write_text(configure_features(source.read_text(), ALL_FEATURES, ALL_FEATURES))
        cli = find_cli()
        run.record["toolchain"] = check_toolchain(ROOT, cli)
        common = ("-Werror -fstack-usage -DPROTOGEN_ANIMATION_V2=1 -DPROTOGEN_FLIGHT_RECORDER=1 "
                  "-DPROTOGEN_CACHE_FACE=1")
        properties = subprocess.check_output([cli, "compile", "--fqbn", run.record["toolchain"]["fqbn"],
                                              "--show-properties=expanded", str(sketch)], text=True)
        link = next(line.split("=",1)[1] for line in properties.splitlines() if line.startswith("build.flags.ld="))
        for enabled in (False, True):
            flags = common + (" -DPROTOGEN_ANIMATION_TIMELINES=1 -DPROTOGEN_LINK_TIMELINE_ASSET=1" if enabled else "")
            keep = ("build.flags.ld=" + link + " -Wl,--undefined=protogenStartLinkedTimeline",) if enabled else ()
            build = run.compile(cli, sketch, ALL_FEATURES, jobs=4, extra_flags=flags,
                                build_properties=keep)
            result = run.record["results"][-1]
            result["timeline_enabled"] = enabled
            if result["exit_code"] or result["warnings"] or result.get("dependency_error"):
                raise RuntimeError(f"timeline target compile failed: {run.log}")
            objects = build / "objects"
            units = json.loads((objects / "compile_commands.json").read_text())
            tools = Path(units[0]["arguments"][0]).parent
            elf = objects / "PROT_OS7_MAX7219.ino.elf"
            symbols = subprocess.check_output([str(tools / "arm-none-eabi-nm"), "-S", "-C",
                                               "--defined-only", str(elf)], text=True)
            match = re.search(r"^\w+ (\w+) \w behavior$", symbols, re.M)
            if not match:
                raise RuntimeError("linked behavior state missing")
            result["behavior_state_bytes"] = int(match[1], 16)
            for symbol in ("protogenStartLinkedTimeline", "ProtogenAsset::clip", "Timeline::Player::advance"):
                if enabled != (symbol in symbols):
                    raise RuntimeError(f"unexpected linkage for {symbol}, enabled={enabled}")
            if enabled:
                objcopy = tools / "arm-none-eabi-objcopy"
                binary = run.path / "linked-image.bin"
                subprocess.run([str(objcopy), "-O", "binary", str(elf), str(binary)], check=True)
                found = binary.read_bytes().count(asset.data)
                if found != 1:
                    raise RuntimeError(f"expected one exact binary asset in Teensy image, got {found}")
            result["stack_usage"] = [line.split("/", 1)[-1] for report in sorted(objects.rglob("*.su"))
                for line in report.read_text().splitlines()
                if re.search(r"Timeline::Player|protogenStartLinkedTimeline|renderFrame|\bloop\(", line)]
            run.publish()
        if not run.finish():
            raise RuntimeError(f"timeline source changed: {run.log}")
    except Exception as error:
        run.finish(str(error))
        raise
    output = ROOT / "build/p6-evidence"
    output.mkdir(parents=True, exist_ok=True)
    write_json(output / "target.json", {"source": run.record["git_head"], "run": str(run.path),
        "asset_sha256": hashlib.sha256(asset.data).hexdigest(), "asset_bytes": len(asset.data),
        "cases": run.record["results"]})
    print("PASS: exact compiled asset linked with explicit start function, no firmware start caller")


if __name__ == "__main__":
    main()
