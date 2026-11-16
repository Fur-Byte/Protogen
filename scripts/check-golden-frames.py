#!/usr/bin/env python3
"""Compare the host sketch's logical frames and physical writes to frozen bytes."""
import argparse
import difflib
import os
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
BASELINE = "520fca6b3858d5d711366e8c11afdf340e7b7e57"
FIXTURE = ROOT / "scripts/tests/fixtures/rendering-v1.tsv"
HEADER = (f"# protogen-rendering-v1 baseline={BASELINE}\n"
          "# checkpoint\tshutdown\tdirty_digits\tshutdown_writes\twake_writes\tlogical_hex\tphysical_hex\n")


def frames(source, recorder_enabled=False):
    sketch = source / "firmware/PROT_OS7_MAX7219"
    fft = Path(os.environ.get("ARDUINO_FFT_SRC", Path.home() / "Documents/Arduino/libraries/arduinoFFT/src"))
    with tempfile.TemporaryDirectory(prefix="protogen-golden-") as tmp:
        program, fft_object = Path(tmp) / "golden", Path(tmp) / "fft.o"
        flags = ["-std=c++17", "-g", "-Wall", "-Wextra", "-Werror",
                 "-fsanitize=" + os.environ.get("PROTOGEN_HOST_SANITIZERS", "address,undefined"),
                 "-fno-sanitize-recover=all", "-DARDUINO=10819", "-DARDUINO_TEENSY40",
                 "-DMicrophone", "-DVisorSensor", f"-I{source / 'scripts/tests/stubs'}",
                 f"-I{sketch}", f"-I{fft}"]
        if recorder_enabled:
            flags.append("-DPROTOGEN_FLIGHT_RECORDER=1")
        subprocess.run(["bash", "-c",
                        'source "$1"; shift; fft="$1"; object="$2"; shift 2; FLAGS=("$@"); compile_host_fft "$fft" "$object"',
                        "compile", str(source / "scripts/host-fft.sh"), str(fft), str(fft_object), *flags],
                       check=True)
        core = sketch / "src/BehaviorCore/BehaviorCore.cpp"
        sources = [str(core)] if core.is_file() else []
        subprocess.run([os.environ.get("CXX", "clang++"), *flags, "-I", str(source / "scripts/tests"),
                        str(ROOT / "scripts/tests/golden-rendering.cpp"),
                        str(sketch / "src/Max72xxPanel/Max72xxPanel.cpp"), *sources, str(fft_object), "-o", str(program)],
                       check=True)
        return HEADER + subprocess.check_output([str(program)], text=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--capture-baseline", type=Path, help="clean isolated worktree at the original commit")
    parser.add_argument("--output", type=Path, help="new review file for explicit capture")
    parser.add_argument("--recorder-enabled", action="store_true", help="verify the same frozen bytes with P3 enabled")
    args = parser.parse_args()
    if args.capture_baseline:
        if args.recorder_enabled:
            parser.error("baseline capture requires the original build profile")
        if not args.output or args.output.resolve() == FIXTURE.resolve() or args.output.exists():
            parser.error("capture requires a new review path separate from the committed fixture")
        source = args.capture_baseline.resolve()
        head = subprocess.check_output(["git", "-C", str(source), "rev-parse", "HEAD"], text=True).strip()
        status = subprocess.check_output(["git", "-C", str(source), "status", "--porcelain", "--untracked-files=all"])
        if source == ROOT or head != BASELINE or status:
            parser.error("capture requires a separate clean worktree at " + BASELINE)
        captured = frames(source)
        with args.output.open("x") as output:
            output.write(captured)
        return 0
    if args.output:
        parser.error("--output requires --capture-baseline")
    expected = FIXTURE.read_text()
    actual = frames(ROOT, args.recorder_enabled)
    if expected != actual:
        sys.stderr.writelines(difflib.unified_diff(expected.splitlines(True), actual.splitlines(True),
                                                  fromfile=str(FIXTURE), tofile="current renderer"))
        return 1
    print(f"PASS: {len(actual.splitlines()) - 2} frozen logical/physical frame and SPI checkpoints")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
