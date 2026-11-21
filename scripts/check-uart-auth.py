#!/usr/bin/env python3
"""Build actual Teensy auth-enabled variants with a unique temporary dev key."""
from pathlib import Path
import json
import shutil
import subprocess
import tempfile
from build_support import BuildRun, check_toolchain, find_cli
from sketch_features import ALL_FEATURES, SENSOR_FEATURES, configure_features

root = Path(__file__).resolve().parents[1]
evidence = BuildRun(root, "uart-auth")
try:
    snapshot = evidence.snapshot()
    source = (snapshot / "PROT_OS7_MAX7219.ino").read_text()
    cli = find_cli()
    evidence.record["toolchain"] = check_toolchain(root, cli)
    with tempfile.TemporaryDirectory(prefix="protogen-private-uart-") as tmp:
        directory = Path(tmp)
        private = directory / "private"
        subprocess.run(
            ["python3", str(root / "scripts/provision-uart-auth.py"), "--output", str(private)], check=True
        )
        for sensors, connector in [((), False), (SENSOR_FEATURES, False), (SENSOR_FEATURES, True)]:
            sketch = directory / "PROT_OS7_MAX7219"
            if sketch.exists():
                shutil.rmtree(sketch)
            shutil.copytree(snapshot, sketch)
            extras = ("WirelessPaw",) + (("VisorConnector",) if connector else ())
            changed = configure_features(source, sensors + extras, ALL_FEATURES)
            (sketch / "PROT_OS7_MAX7219.ino").write_text(changed)
            features = (
                list(sensors)
                + ["WirelessPaw", "PROTOGEN_UART_AUTH"]
                + (["VisorConnector"] if connector else [])
            )
            artifact = evidence.compile(
                cli, sketch, features, extra_flags="-DPROTOGEN_UART_AUTH=1 -I" + str(private) + " -Werror"
            )
            if evidence.record["results"][-1]["exit_code"]:
                raise RuntimeError("Authenticated Teensy compilation failed; see build log")
            commands = json.loads((artifact / "objects/compile_commands.json").read_text())
            compiler = Path(commands[0]["arguments"][0])
            nm = compiler.parent / "arm-none-eabi-nm"
            elf = artifact / "objects/PROT_OS7_MAX7219.ino.elf"
            symbols = subprocess.run(
                [str(nm), "--defined-only", "-C", str(elf)], capture_output=True, text=True, check=True
            ).stdout
            for symbol in [
                "protogen_uart_mbedtls_md_hmac_starts",
                "protogen_uart_mbedtls_md_hmac_finish",
                "PawLink::uartMac",
            ]:
                if symbol not in symbols:
                    raise RuntimeError("Authenticated UART implementation missing from linked firmware")
    raise SystemExit(0 if evidence.finish() else 1)
except (Exception, KeyboardInterrupt) as error:
    evidence.finish(str(error))
    print(error)
    raise SystemExit(1)
