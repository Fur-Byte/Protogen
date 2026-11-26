#!/usr/bin/env python3
"""Build opt-in recovery candidates and verify their reserved OCRAM layout."""

import hashlib
import json
from pathlib import Path
import secrets
import subprocess

from build_support import BuildRun, check_toolchain, find_cli, file_sha256
from sketch_features import ALL_FEATURES, configure_features

ROOT = Path(__file__).resolve().parents[1]


def inspect_image(objects, enabled):
    units = json.loads((objects / "compile_commands.json").read_text())
    tools = Path(units[0]["arguments"][0]).parent
    elf = objects / "PROT_OS7_MAX7219.ino.elf"
    symbols = subprocess.check_output([str(tools / "arm-none-eabi-nm"), "-S", "-C", str(elf)], text=True)

    def symbol(name):
        rows = [line.split() for line in symbols.splitlines() if line.endswith(" " + name)]
        if len(rows) != 1:
            raise RuntimeError(f"Expected one symbol for {name}: {rows}")
        return rows[0]

    if not enabled:
        if "Recovery::retained" in symbols:
            raise RuntimeError("Retained journal linked into default image")
        return {}
    retained = symbol("Recovery::retained")
    start, size = int(retained[0], 16), int(retained[1], 16)
    heap = int(symbol("_heap_start")[0], 16)
    if size != 128 or start % 32 or not 0x20200000 <= start < start + size <= heap < 0x2027FF80:
        raise RuntimeError("Invalid retained journal layout")
    sections = subprocess.check_output([str(tools / "arm-none-eabi-readelf"), "-SW", str(elf)], text=True)
    dma = [line.split() for line in sections.splitlines() if ".bss.dma " in line]
    if len(dma) != 1:
        raise RuntimeError("Expected one DMA section")
    row = dma[0]
    i = row.index(".bss.dma")
    address, length = int(row[i + 2], 16), int(row[i + 4], 16)
    if row[i + 1] != "NOBITS" or not address <= start < start + size <= address + length:
        raise RuntimeError("Retained journal must be wholly inside the NOLOAD DMA section")
    return {"address": hex(start), "bytes": size, "heap_start": hex(heap)}


def main():
    run = BuildRun(ROOT, "controller-recovery")
    try:
        sketch = run.snapshot()
        cli = find_cli()
        run.record["toolchain"] = check_toolchain(ROOT, cli)
        main_file = sketch / "PROT_OS7_MAX7219.ino"
        original = main_file.read_text()
        for name, features, enabled in (
            ("default", (), False),
            ("bare", (), True),
            ("all", ALL_FEATURES, True),
        ):
            main_file.write_text(configure_features(original, features, ALL_FEATURES))
            flags = "-Werror"
            identity = None
            if enabled:
                flags += (
                    " -DPROTOGEN_CONTROLLER_RECOVERY=1 -DPROTOGEN_SUPERVISION_OBSERVE=1"
                    " -DPROTOGEN_IDLE_RUN_MODE=1"
                )
                identity = hashlib.sha256(
                    json.dumps(
                        {
                            "source": run.record["source"]["sha256"],
                            "configured_sketch": file_sha256(main_file),
                            "toolchain": run.record["toolchain"],
                            "flags": flags,
                            "nonce": secrets.token_hex(16),
                        },
                        sort_keys=True,
                    ).encode()
                ).hexdigest()
                flags += f" -DPROTOGEN_RECOVERY_BUILD_ID=0x{identity[:16]}ULL"
            output = run.compile(cli, sketch, features, extra_flags=flags, jobs=4)
            result = run.record["results"][-1]
            if result["exit_code"] or result["warnings"] or result.get("dependency_error"):
                raise RuntimeError(f"Recovery profile {name} failed")
            result["profile"] = name
            result["recovery_identity"] = identity
            result["recovery_layout"] = inspect_image(output / "objects", enabled)
            result["hex_sha256"] = file_sha256(next(output.glob("*.hex")))
            run.publish()
        if not run.finish():
            raise RuntimeError("Recovery source changed during build")
    except Exception as error:
        run.finish(str(error))
        raise
    print("PASS: default excludes retention; bare/all-feature recovery images and OCRAM layout checked")


if __name__ == "__main__":
    main()
