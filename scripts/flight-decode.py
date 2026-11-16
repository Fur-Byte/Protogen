#!/usr/bin/env python3
"""Decode a version-1 recorder array captured at an owner-safe debugger point."""
import argparse
import json
from pathlib import Path
import struct

LAYOUT = struct.Struct("<IIIHH")


def records(raw, capacity, used, write, next_sequence):
    if (len(raw) != capacity * LAYOUT.size or not 0 <= used <= capacity
            or not 0 <= write < capacity or not 0 <= next_sequence < 2**32):
        raise ValueError("array length or owner metadata is invalid")
    result = []
    for age in range(used, 0, -1):
        at, sequence, value, event, detail = LAYOUT.unpack_from(raw, (write - age) % capacity * LAYOUT.size)
        if sequence != (next_sequence - age) % (2**32) or not 1 <= event <= 23:
            raise ValueError("sequence or event ID mismatch, capture must be taken at an owner-safe point")
        result.append({"sequence": sequence, "time_us": at, "event": event, "detail": detail, "value": value})
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("raw", type=Path)
    parser.add_argument("--capacity", type=int, choices=(128, 480), default=128)
    parser.add_argument("--used", type=int, required=True)
    parser.add_argument("--write-index", type=int, required=True)
    parser.add_argument("--next", type=int, required=True)
    args = parser.parse_args()
    try:
        output = records(args.raw.read_bytes(), args.capacity, args.used, args.write_index, args.next)
    except (ValueError, OSError) as error:
        parser.exit(1, str(error) + "\n")
    for entry in output:
        print(json.dumps({"v": 1, **entry}, separators=(",", ":")))


if __name__ == "__main__":
    main()
