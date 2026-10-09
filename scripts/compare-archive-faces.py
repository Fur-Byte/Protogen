#!/usr/bin/env python3
"""Compare real current renderer output with all four resting faces in the ZIP."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import tempfile
import zipfile

ROOT = Path(__file__).resolve().parents[1]
NAMES = ("Happy", "Surprised", "Angry", "vwv")
WIDTH, HEIGHT = 112, 8


def arrays(source):
    source = re.sub(r"/\*.*?\*/|//[^\n]*", "", source, flags=re.S)
    result = {}
    for name, body in re.findall(r"\b(\w+)\s*\[\s*\d*\s*\]\s*(?:PROGMEM\s*)?=\s*\{([^}]*)\}", source):
        values = re.findall(r"\b(?:B[01]+|0x[0-9a-fA-F]+|0b[01]+|\d+)\b", body)
        result[name] = bytes(int(v[1:], 2) if v.startswith("B") else int(v, 0) for v in values)
    return result


def archived_frames(main, artwork):
    positions = {name: int(number) for name, number in re.findall(
        r"signed\s+int\s+(\w+)\s*=\s*(\d+)", main)}
    rotations = {int(tile): int(rotation) for tile, rotation in re.findall(
        r"matrix\.setRotation\((\d+),\s*(\d+)\)", main)}
    if set(rotations) != set(range(14)):
        raise ValueError("Archive must explicitly configure all 14 panel rotations")
    # First state switch is the archive's resting expression selection. Read its
    # bitmap names directly instead of assuming today's name/mirroring choices.
    cases = re.findall(r"case\s+([0-3])\s*:(.*?)break\s*;", main, re.S)[:4]
    if [int(number) for number, _ in cases] != list(range(4)):
        raise ValueError("Could not locate the archive's four resting expressions")
    mouths = (("MawLeft", "mawL", 32), ("MawRight", "maw", 32),
              ("NoseLeft", "noseL", 8), ("NoseRight", "nose", 8))
    frames = []
    for _, case in cases:
        pixels = [[False] * WIDTH for _ in range(HEIGHT)]
        eyes = re.findall(r"matrix\.drawBitmap\((EyeLeft|EyeRight),\s*0,\s*(\w+),\s*16,\s*8,\s*HIGH\)", case)
        if len(eyes) != 2:
            raise ValueError("Archive expression must draw both eyes")
        for position, name, width in (*mouths, *((p, n, 16) for p, n in eyes)):
            data = artwork[name]
            if len(data) != width * HEIGHT // 8:
                raise ValueError("Unexpected archived bitmap size: " + name)
            for y in range(HEIGHT):
                for x in range(width):
                    pixels[y][positions[position] + x] |= bool(data[y * (width // 8) + x // 8] & (0x80 >> (x & 7)))
        logical = bytes(sum(int(pixels[y][x + bit]) << (7 - bit) for bit in range(8))
                        for y in range(HEIGHT) for x in range(0, WIDTH, 8))
        physical = bytearray(112)
        for y in range(HEIGHT):
            for x in range(WIDTH):
                if not pixels[y][x]:
                    continue
                tile, col, bit = x // 8, x % 8, y
                rotation = rotations[tile]
                if rotation >= 2: col = 7 - col
                if rotation in (1, 2): bit = 7 - bit
                if rotation & 1: col, bit = bit, col
                physical[tile * 8 + col] |= 1 << bit
        frames.append((logical, bytes(physical)))
    return frames, positions, rotations


def current_frames():
    with tempfile.TemporaryDirectory(prefix="protogen-archive-faces-") as tmp:
        executable = Path(tmp) / "faces"
        subprocess.run([os.environ.get("CXX", "clang++"), "-std=c++17", "-Wall", "-Wextra", "-Werror",
                        "-fsanitize=" + os.environ.get("PROTOGEN_HOST_SANITIZERS", "address,undefined"), "-fno-sanitize-recover=all",
                        "-DARDUINO=10819", "-DARDUINO_TEENSY40",
                        "-I" + str(ROOT / "scripts/tests/stubs"),
                        "-I" + str(ROOT / "firmware/PROT_OS7_MAX7219"),
                        str(ROOT / "scripts/tests/archive-faces.cpp"),
                        str(ROOT / "firmware/PROT_OS7_MAX7219/src/Max72xxPanel/Max72xxPanel.cpp"),
                        "-o", str(executable)], check=True, capture_output=True, text=True)
        output = subprocess.run([str(executable)], check=True, capture_output=True, text=True).stdout
    frames = [tuple(bytes.fromhex(value) for value in line.split()) for line in output.splitlines()]
    if len(frames) != 4 or any(len(frame) != 2 or any(len(v) != 112 for v in frame) for frame in frames):
        raise ValueError("Current renderer returned incomplete frames")
    return frames


def comparison_svg(archive, current):
    parts = ['<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 1680 570">',
             '<rect width="1680" height="570" fill="#101613"/>',
             '<g fill="#ecf5ef" font-family="sans-serif" font-size="22">',
             '<text x="30" y="35">Original archive</text><text x="860" y="35">Current firmware</text></g>']
    for row, name in enumerate(NAMES):
        top = 70 + row * 122
        difference = sum((a ^ b).bit_count() for a, b in zip(archive[row][0], current[row][0]))
        parts.append(f'<text x="30" y="{top}" fill="#b5c9bc" font-family="sans-serif" font-size="18">{name} · {difference} different pixels</text>')
        for col, frames in enumerate((archive, current)):
            frame = frames[row][0]
            for y in range(8):
                for x in range(112):
                    color = "#78eba6" if frame[y * 14 + x // 8] & (0x80 >> (x & 7)) else "#1e2c23"
                    parts.append(f'<rect x="{30 + col * 830 + x * 7}" y="{top + 14 + y * 7}" width="5" height="5" rx="1" fill="{color}"/>')
    return "\n".join(parts) + "\n</svg>\n"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=ROOT / "build/archive-face-comparison.json")
    parser.add_argument("--svg", type=Path, default=ROOT / "build/archive-face-comparison.svg")
    args = parser.parse_args()
    archive_path = ROOT / "archive/PROT_OS7_original.zip"
    with zipfile.ZipFile(archive_path) as archived:
        members = [name for name in archived.namelist() if name.endswith("/PROT_OS7_MAX7219/Icons.h") and not name.startswith("__MACOSX/")]
        if len(members) != 1:
            raise ValueError("Expected one original Icons.h in the archive")
        base = members[0].rsplit("/", 1)[0] + "/"
        icons = archived.read(base + "Icons.h")
        original, positions, rotations = archived_frames(archived.read(base + "PROT_OS7_MAX7219.ino").decode(), arrays(icons.decode()))
    current = current_frames()
    faces = []
    for name, expected, actual in zip(NAMES, original, current):
        logical, physical = (sum((a ^ b).bit_count() for a, b in zip(e, c)) for e, c in zip(expected, actual))
        faces.append({"expression": name, "logical_pixels_checked": 896, "different_logical_pixels": logical,
                      "physical_pixels_checked": 896, "different_physical_pixels": physical,
                      "archive_frame": expected[0].hex(), "current_frame": actual[0].hex()})
        print(f"{name}: {logical}/896 logical and {physical}/896 physical pixels differ")
    identical = all(not f["different_logical_pixels"] and not f["different_physical_pixels"] for f in faces)
    report = {"identical": identical, "scope": "Four resting expressions; animation and speech inactive",
              "archive_sha256": hashlib.sha256(archive_path.read_bytes()).hexdigest(),
              "archive_icons_sha256": hashlib.sha256(icons).hexdigest(),
              "current_icons_sha256": hashlib.sha256((ROOT / "firmware/PROT_OS7_MAX7219/Icons.h").read_bytes()).hexdigest(),
              "archive_positions": positions, "archive_rotations": rotations, "faces": faces}
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2) + "\n")
    args.svg.parent.mkdir(parents=True, exist_ok=True)
    args.svg.write_text(comparison_svg(original, current))
    return 0 if identical else 1


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except subprocess.CalledProcessError as error:
        raise SystemExit(error.stderr or str(error)) from error
