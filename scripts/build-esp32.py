#!/usr/bin/env python3
"""Compile both pinned ESP32 targets; record source identity and artifact hashes."""
import datetime
import hashlib
import json
import os
from pathlib import Path
import subprocess
import re
import configparser
import struct
from build_support import file_sha256, git_identity, source_identity, write_json
from uart_key import validate_uart_key_directory
from esp_source_integrity import verify_esp_sources


def main():
    root = Path(__file__).resolve().parents[1]
    build = root / "build"
    build.mkdir(exist_ok=True)
    pio = os.environ.get("PIO", str(build / "esp32-venv/bin/pio"))
    env = dict(os.environ, PLATFORMIO_CORE_DIR=str(build / "platformio"), IDF_COMPONENT_MANAGER="0")
    lock = json.loads((root / "toolchain.lock.json").read_text())
    locked = lock["esp32"]
    git = git_identity(root)
    # Local overrides remain available to developers. Record names only so
    # release packaging can reject these builds without exporting values.
    overrides = sorted(
        key
        for key in env
        if key.startswith("PLATFORMIO_")
        and key != "PLATFORMIO_CORE_DIR"
        and not key.startswith("PLATFORMIO_SETTING_")
    )
    record = {
        "state": "running",
        "started_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
        "source": source_identity(root),
        **git,
        "platformio_overrides": overrides,
        "platform": locked["platform"],
        "esp_idf": locked["esp_idf"],
        "mbedtls": locked["mbedtls"],
    }
    target = build / "esp32-build.json"
    write_json(target, record)
    try:
        uart_include = os.environ.get("PROTOGEN_UART_KEY_DIR")
        if uart_include:
            directory = validate_uart_key_directory(uart_include)
            env["PROTOGEN_UART_KEY_DIR"] = str(directory)
            record["uart_auth"] = True
        else:
            record["uart_auth"] = False
        record["source_integrity"] = verify_esp_sources(build / "platformio", lock)
        version = subprocess.run(
            [pio, "--version"], env=env, capture_output=True, text=True, check=True
        ).stdout.strip()
        if not version.endswith(" " + locked["platformio"]):
            raise RuntimeError("PlatformIO version differs from toolchain.lock.json")
        ini = configparser.ConfigParser(interpolation=None)
        ini.read(root / "firmware/esp32/platformio.ini")
        if ini["env"]["platform"] != locked["platform"]:
            raise RuntimeError("ESP32 platform differs from the lock")
        command = [pio, "run", "--project-dir", str(root / "firmware/esp32"), "-e", "helmet", "-e", "paw"]
        if jobs := os.environ.get("PROTOGEN_PIO_JOBS"):
            command += ["-j", jobs]
        record["command"] = command
        run = subprocess.run(command, env=env, capture_output=True, text=True)
        output = run.stdout + run.stderr
        (build / "esp32-build.log").write_text(output)
        record["exit_code"] = run.returncode
        record["warnings"] = output.lower().count("warning:")
        if run.returncode:
            raise RuntimeError("PlatformIO compilation failed; see esp32-build.log")
        # PlatformIO runs these environments in command order. Record linker
        # occupancy separately from the generated binary's file size.
        sizes = {
            name: re.findall(rf"^{name}: .*\(used (\d+) bytes from (\d+) bytes\)", output, re.M)
            for name in ("RAM", "Flash")
        }
        if any(len(values) != 2 for values in sizes.values()):
            raise RuntimeError("PlatformIO did not report memory occupancy for both roles")
        framework = build / "platformio/packages/framework-espidf"
        idf = (framework / "components/esp_common/include/esp_idf_version.h").read_text()
        resolved_idf = ".".join(
            re.search(r"ESP_IDF_VERSION_" + part + r"\s+(\d+)", idf)[1]
            for part in ("MAJOR", "MINOR", "PATCH")
        )
        mbed = (framework / "components/mbedtls/mbedtls/CMakeLists.txt").read_text()
        resolved_mbed = re.search(r"set\(MBEDTLS_VERSION (\d+\.\d+\.\d+)\)", mbed)[1]
        record["resolved"] = {"platformio": version, "esp_idf": resolved_idf, "mbedtls": resolved_mbed}
        if resolved_idf != locked["esp_idf"] or resolved_mbed != locked["mbedtls"]:
            raise RuntimeError("Resolved ESP-IDF/Mbed TLS differs from the lock")
        record["artifacts"] = {}
        record["targets"] = {}
        for index, role in enumerate(("helmet", "paw")):
            elf = build / "esp32" / role / "firmware.elf"
            nm = build / "platformio/packages/toolchain-riscv32-esp/bin/riscv32-esp-elf-nm"
            symbols = subprocess.check_output([str(nm), "--defined-only", "-C", str(elf)], text=True)
            uart_linked = any("::uartMac(" in symbol for symbol in symbols.splitlines())
            if uart_linked != (role == "helmet" and record["uart_auth"]):
                raise RuntimeError(f"{role}: linked UART authentication differs from the requested build")
            sdk_bytes = (build / "esp32" / role / "config/sdkconfig.json").read_bytes()
            sdk = json.loads(sdk_bytes)
            required = {
                "FREERTOS_HZ": 1000,
                "ESPTOOLPY_FLASHSIZE": "4MB",
                "PARTITION_TABLE_SINGLE_APP_LARGE": True,
                "MBEDTLS_PSK_MODES": True,
                "MBEDTLS_KEY_EXCHANGE_PSK": True,
                "MBEDTLS_SSL_PROTO_DTLS": True,
                "MBEDTLS_SSL_PROTO_TLS1_2": True,
                "MBEDTLS_SSL_COOKIE_C": True,
                "MBEDTLS_SSL_PROTO_TLS1_3": False,
                "MBEDTLS_SSL_RENEGOTIATION": False,
                "MBEDTLS_CLIENT_SSL_SESSION_TICKETS": False,
                "MBEDTLS_SERVER_SSL_SESSION_TICKETS": False,
                "MBEDTLS_ASYMMETRIC_CONTENT_LEN": False,
                "MBEDTLS_SSL_MAX_CONTENT_LEN": 1024,
                "ESP_WIFI_SOFTAP_SUPPORT": False,
                "MBEDTLS_GCM_C": True,
                "MBEDTLS_ECDH_C": True,
                "MBEDTLS_ECP_DP_SECP256R1_ENABLED": True,
                "NVS_ENCRYPTION": False,
                "SECURE_BOOT": False,
                "SECURE_FLASH_ENC_ENABLED": False,
                "PROTOGEN_PRODUCTION": False,
            }
            required["ESP_CONSOLE_UART_DEFAULT" if role == "helmet" else "ESP_CONSOLE_USB_SERIAL_JTAG"] = True
            if any(sdk.get(key) != value for key, value in required.items()):
                raise RuntimeError(
                    f"{role}: resolved SDK settings differ from the intended security/board configuration; regenerate build/sdkconfig.{role}"
                )
            table = (build / "esp32" / role / "partitions.bin").read_bytes()
            app_size = 0
            for offset in range(0, len(table) - 31, 32):
                magic, kind, subtype, address, size, label, flags = struct.unpack_from(
                    "<HBBII16sI", table, offset
                )
                if magic != 0x50AA:
                    break
                if kind == 0 and subtype == 0:
                    app_size = size
            if app_size != 1536000:
                raise RuntimeError(
                    f"{role}: generated factory partition differs from the 1500 KiB configuration"
                )
            record["targets"][role] = {
                "board": ini["env:" + role]["board"],
                "application_partition_bytes": app_size,
                "uart_auth_linked": uart_linked,
                "memory": {
                    name.lower() + "_bytes": dict(zip(("used", "capacity"), map(int, values[index])))
                    for name, values in sizes.items()
                },
                "binary_bytes": (build / "esp32" / role / "firmware.bin").stat().st_size,
                "sdk_settings": required,
                "sdkconfig_sha256": hashlib.sha256(sdk_bytes).hexdigest(),
            }
            for name in ("firmware.bin", "firmware.elf", "bootloader.bin", "partitions.bin"):
                path = build / "esp32" / role / name
                if path.is_file():
                    record["artifacts"][str(path.relative_to(root))] = file_sha256(path)
        if verify_esp_sources(build / "platformio", lock) != record["source_integrity"]:
            raise RuntimeError("ESP dependency sources changed during the run.")
        if source_identity(root) != record["source"] or git_identity(root) != git:
            raise RuntimeError("Sources changed during the run.")
        record["state"] = "success" if run.returncode == 0 and record["warnings"] == 0 else "failed"
    except (Exception, KeyboardInterrupt) as error:
        record.update(state="failed", error=str(error))
    record["finished_utc"] = datetime.datetime.now(datetime.timezone.utc).isoformat()
    write_json(target, record)
    print(json.dumps({k: record[k] for k in ("state", "platform", "esp_idf")}, indent=2))
    if record["state"] != "success":
        print("See build/esp32-build.log and build/esp32-build.json")
    return 0 if record["state"] == "success" else 1


if __name__ == "__main__":
    raise SystemExit(main())
