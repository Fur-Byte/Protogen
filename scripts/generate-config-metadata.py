#!/usr/bin/env python3
"""Export metadata from the compiled firmware field table and storage constants."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / "tools/helmet/config-metadata.json"


def metadata():
    with tempfile.TemporaryDirectory(prefix="protogen-config-metadata-") as tmp:
        executable = Path(tmp) / "fields"
        subprocess.run([os.environ.get("CXX", "clang++"), "-std=c++17", "-Wall", "-Wextra", "-Werror",
                        f"-I{ROOT / 'scripts/tests/stubs'}", f"-I{ROOT / 'firmware/PROT_OS7_MAX7219'}",
                        str(ROOT / "scripts/tests/config-metadata.cpp"), "-o", str(executable)], check=True)
        header, *fields = subprocess.check_output([str(executable)], text=True).splitlines()
    (schema, record, slot, header_bytes, payload_bytes, approval_offset, crc_offset,
     commit_offset, record_bytes, legacy_approval, ceiling, default_approval) = map(int, header.split())
    entries = []
    for index, line in enumerate(fields):
        name, minimum, maximum, default = line.split()
        entries.append({"name": name, "minimum": int(minimum), "maximum": int(maximum),
                        "default": int(default), "eeprom_offset": header_bytes + 2 * index})
    return {
        "schema": schema,
        "fields": entries,
        "storage": {"version": record, "slot_bytes": slot, "header_bytes": header_bytes,
                    "payload_bytes": payload_bytes, "approval_offset": approval_offset,
                    "crc_offset": crc_offset, "commit_offset": commit_offset, "record_bytes": record_bytes},
        "brightness": {"compiled_maximum": ceiling, "default_approval": default_approval,
                       "legacy_approval": legacy_approval},
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument("--check", action="store_true", help="fail on generated-file drift")
    mode.add_argument("--write", action="store_true", help="explicitly update committed metadata")
    args = parser.parse_args()
    expected = json.dumps(metadata(), indent=2) + "\n"
    if args.write:
        OUTPUT.write_bytes(expected.encode())
    elif not OUTPUT.is_file() or OUTPUT.read_bytes() != expected.encode():
        parser.exit(1, "Configuration metadata drift; review the table changes and regenerate deliberately.\n")
    print(f"PASS: {OUTPUT.relative_to(ROOT)} matches compiled field table and storage constants")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
