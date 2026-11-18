#!/usr/bin/env python3
"""Package matching development firmware and independently identified rollback evidence."""
import argparse
import hashlib
import io
import json
import os
from pathlib import Path
import re
import runpy
import shlex
import stat
import sys
import tempfile
import zipfile

from build_support import FQBN, locked_library_versions, release_source, source_digest
from maintenance_support import validate_profile

ROOT = Path(__file__).resolve().parents[1]
STATUS = "development; physical qualification pending"
TEENSY_EVIDENCE = "evidence/teensy-build.json"
ESP_EVIDENCE = "evidence/esp32-build.json"
PREVIOUS = "recovery/previous-release.zip"
CONFIGURATION = "configuration/helmet.json"
LOCK = "toolchain.lock.json"
SKETCH = "firmware/PROT_OS7_MAX7219/PROT_OS7_MAX7219.ino"
DOCS = ("docs/HARDWARE_WIRING.md", "hardware/connector-map.csv", "hardware/calibration-records.csv",
        "docs/EFFICIENCY_QUALIFICATION.md", "docs/RELEASE_RECOVERY.md", "firmware/esp32/README.md")
ROLES = ("helmet", "paw")
RADIO_IMAGES = ("firmware.bin", "bootloader.bin", "partitions.bin")
RADIO_BINARIES = {f"{role}/{name}" for role in ROLES for name in RADIO_IMAGES}
RADIO_ELFS = {f"build/esp32/{role}/firmware.elf" for role in ROLES}
VENDORED = ("firmware/PROT_OS7_MAX7219/src/Max72xxPanel/", "firmware/PROT_OS7_MAX7219/src/UartCrypto/",
            "firmware/esp32/protocol_v4/vendor/")
TOOLCHAIN_DEPENDENCIES = ("build/toolchains/user/libraries/", "build/toolchains/data/packages/teensy/hardware/")
FEATURES = ("VisorSensor", "BoopSensor", "Microphone", "WirelessPaw", "VisorConnector")
MAX_ARCHIVE_BYTES = 256 * 1024 * 1024


def digest(data):
    return hashlib.sha256(data).hexdigest()


def is_sha256(value):
    return isinstance(value, str) and re.fullmatch(r"[0-9a-f]{64}", value) is not None


def is_commit(value):
    return isinstance(value, str) and re.fullmatch(r"[0-9a-f]{40}(?:[0-9a-f]{24})?", value) is not None


def valid(condition, message):
    if not condition:
        raise ValueError(message)


def checked_json(raw, description):
    def unique(pairs):
        result = {}
        for key, value in pairs:
            valid(key not in result, f"Duplicate key in {description} manifest")
            result[key] = value
        return result

    def invalid_constant(_):
        raise ValueError(f"Invalid number in {description} manifest")

    try:
        result = json.loads(raw, object_pairs_hook=unique, parse_constant=invalid_constant)
    except (UnicodeError, json.JSONDecodeError) as error:
        raise ValueError(f"Invalid {description} manifest") from error
    valid(isinstance(result, dict), f"Invalid {description} manifest")
    return result


def safe_path(name):
    return (isinstance(name, str) and all(part not in ("", ".", "..") for part in name.split("/"))
            and not re.search(r"[\\:\x00-\x1f\x7f]", name))


def clean_build(record):
    return (record["state"] == "success" and record["working_tree_dirty"] is False
            and record["git_status_porcelain"] == [] and is_commit(record["git_head"]))


def source_commit(teensy, esp, lock_bytes, expected=None):
    valid(clean_build(teensy) and clean_build(esp), "Rebuild both targets from a clean, identified Git source")
    valid(teensy["git_head"] == esp["git_head"] and teensy["source"] == esp["source"],
          "Teensy and ESP32 build evidence source snapshots differ")
    source = teensy["source"]
    files = source["files"]
    valid(isinstance(files, dict) and all(safe_path(name) and is_sha256(value) for name, value in files.items())
          and source_digest(files) == source["sha256"] and files[LOCK] == digest(lock_bytes),
          "Build source file hashes or toolchain lock do not match source identity")
    valid(not any(name.startswith("firmware/") and Path(name).name.casefold() == "protogenuartkey.h" for name in files),
          "Move the UART provisioning header outside the firmware checkout before packaging")
    identity = (teensy["git_head"], source)
    valid(expected is None or expected == identity, "Build source or Git commit mismatch; rebuild from the current source")
    return identity


def locked_toolchains(teensy, esp, selected, lock):
    toolchain = teensy["toolchain"]
    pins = ("arduino_cli", "platform", "libraries", "bundled_libraries", "bundled_source_libraries", "fqbn")
    valid(all(toolchain[key] == lock[key] for key in pins)
          and teensy["fqbn"] == selected["fqbn"] == lock["fqbn"] == FQBN,
          "Teensy toolchain or FQBN differs from the lock")
    expected = locked_library_versions(lock, selected["features"])
    libraries = selected["selected_libraries"]
    valid(all(libraries.get(name, {}).get("version") == version for name, version in expected.items())
          and all(libraries.get(name, {}).get("source_sha256") == files
                  for name, files in lock["bundled_source_libraries"].items()),
          "Teensy toolchain selected libraries differ from the lock")
    radio = lock["esp32"]
    resolved = esp["resolved"]
    valid(all(esp[key] == radio[key] for key in ("platform", "esp_idf", "mbedtls"))
          and all(resolved[key] == radio[key] for key in ("esp_idf", "mbedtls"))
          and resolved["platformio"].rsplit(" ", 1)[-1] == radio["platformio"],
          "ESP32 toolchain differs from the lock")


def teensy_authentication(command):
    valid(isinstance(command, list) and all(isinstance(item, str) for item in command),
          "Invalid Teensy build command")
    properties = [command[index + 1] for index, argument in enumerate(command) if argument == "--build-property"]
    definitions = [prop.removeprefix("build.flags.defs=") for prop in properties if prop.startswith("build.flags.defs=")]
    valid(len(properties) == len(definitions) == 1 and "compile" in command and "--clean" in command
          and command[command.index("--fqbn") + 1] == FQBN, "Teensy command differs from the locked build")
    flags = shlex.split(definitions[0])
    auth_flags = []
    for index, token in enumerate(flags):
        if token in ("-D", "-U"):
            token += flags[index + 1]
        if not token.startswith(("-D", "-U")):
            continue
        macro = token[2:].split("=", 1)[0]
        if macro == "PROTOGEN_UART_AUTH":
            auth_flags.append(token)
        else:
            valid(macro not in FEATURES and not macro.startswith("PROTOGEN_"),
                  "Teensy compiler feature mask overrides the tracked configuration")
    valid(auth_flags in ([], ["-DPROTOGEN_UART_AUTH=1"], ["-DPROTOGEN_UART_AUTH"]),
          "Invalid Teensy UART authentication mode")
    return bool(auth_flags)


def compatibility(teensy, esp, lock, expected_features=None, legacy=False):
    results = teensy["results"]
    valid(type(teensy["schema"]) is int and teensy["schema"] == 1 and teensy["kind"] == "build" and len(results) == 1,
          "Release requires one identified Teensy export")
    selected = results[0]
    valid(type(selected["exit_code"]) is int and selected["exit_code"] == 0
          and type(selected["warnings"]) is int and selected["warnings"] == 0
          and not selected.get("dependency_error") and not teensy.get("error"),
          "Release requires a successful warning-free Teensy export")
    features = selected["features"]
    valid(isinstance(features, list) and features == [name for name in FEATURES if name in features]
          and (expected_features is None or features == expected_features)
          and selected["configured_source_sha256"] == teensy["source"]["files"][SKETCH],
          "Teensy feature mask or configured sketch hash differs from the source")
    locked_toolchains(teensy, esp, selected, lock)
    auth = teensy_authentication(selected["command"])
    valid(type(esp["uart_auth"]) is bool and
          ("WirelessPaw" not in features or auth == esp["uart_auth"]),
          "Teensy and ESP32 UART authentication modes differ")
    valid(type(esp["exit_code"]) is int and esp["exit_code"] == 0
          and type(esp["warnings"]) is int and esp["warnings"] == 0
          and not esp.get("error"), "Release requires successful warning-free ESP32 exports")
    valid(esp.get("platformio_overrides", [] if legacy else None) == [],
          "ESP32 build did not record a standard PlatformIO environment")
    boards = lock["esp32"]["boards"]
    valid(set(esp["targets"]) == set(ROLES) and len(boards) == len(ROLES), "ESP32 build evidence requires both role targets")
    for role, board, console in zip(ROLES, boards, ("ESP_CONSOLE_UART_DEFAULT", "ESP_CONSOLE_USB_SERIAL_JTAG")):
        target = esp["targets"][role]
        settings = target["sdk_settings"]
        disabled = ("NVS_ENCRYPTION", "SECURE_BOOT", "SECURE_FLASH_ENC_ENABLED", "PROTOGEN_PRODUCTION")
        enabled = (console, "PARTITION_TABLE_SINGLE_APP_LARGE", "MBEDTLS_PSK_MODES",
                   "MBEDTLS_KEY_EXCHANGE_PSK", "MBEDTLS_SSL_PROTO_DTLS", "MBEDTLS_SSL_PROTO_TLS1_2")
        valid(target["board"] == board and target["application_partition_bytes"] == 1536000
              and target["uart_auth_linked"] is (role == "helmet" and esp["uart_auth"])
              and all(settings[key] is False for key in disabled)
              and all(settings[key] is True for key in enabled),
              f"ESP32 {role} target, SDK settings or linked UART authentication mode differs")
    return {"teensy_features": features, "wireless_bridge_enabled": "WirelessPaw" in features,
            "teensy_uart_auth": auth, "esp_uart_auth": esp["uart_auth"], "physical_qualification": "pending"}


def artifact_checksums(teensy, esp):
    teensy_files = teensy["artifacts"]
    root = "PROT_OS7_MAX7219.ino."
    allowed = {root + suffix for suffix in ("hex", "elf", "lst", "eep", "sym")}
    valid(root + "hex" in teensy_files and set(teensy_files) <= allowed,
          "Missing Teensy HEX or unexpected Teensy artifact name")
    checksums = {"teensy/" + name: checksum for name, checksum in teensy_files.items()}
    radio_files = esp["artifacts"]
    required = {"build/esp32/" + name for name in RADIO_BINARIES}
    valid(set(radio_files) == required | RADIO_ELFS,
          "Missing ESP32 artifact or unexpected role artifact name")
    for name in RADIO_BINARIES:
        checksums[name] = radio_files["build/esp32/" + name]
    valid(all(is_sha256(value) for value in (*checksums.values(), *radio_files.values())), "Invalid build artifact hashes")
    return checksums


def verify_artifacts(content, checksums, esp):
    for name, expected in checksums.items():
        target = "Teensy" if name.startswith("teensy/") else "ESP32"
        raw = content.get(name)
        valid(raw, f"Missing {target} artifact: {name}")
        valid(digest(raw) == expected, f"{target} artifact checksum mismatch: {name}")
    for role in ROLES:
        length = esp["targets"][role]["binary_bytes"]
        valid(type(length) is int and length == len(content[f"{role}/firmware.bin"]),
              f"ESP32 {role} firmware size differs from the build evidence")


def dependency(name):
    if name.startswith(VENDORED):
        return True
    path = Path(name)
    metadata = path.name.lower().startswith(("license", "copying")) or path.name in ("UPSTREAM.json", "PROVENANCE.json")
    return metadata and name.startswith(("firmware/", *TOOLCHAIN_DEPENDENCIES))


def regular_file(path):
    valid(path.is_file(), f"Missing or invalid file: {path.name}")
    return path.read_bytes()


def local_file(name):
    path = ROOT / name
    valid(path.resolve() == path, f"Unexpected symbolic link in input path: {name}")
    return regular_file(path)


def validate_previous(raw, expected):
    valid(digest(raw) == expected, "Previous archive SHA-256 differs from the retained expected digest")
    with zipfile.ZipFile(io.BytesIO(raw)) as archive:
        members = archive.infolist()
        names = [member.filename for member in members]
        valid(len({name.casefold() for name in names}) == len(names), "Duplicate previous archive entries")
        valid(all(safe_path(member.orig_filename) and not member.is_dir()
                  and stat.S_IFMT(member.external_attr >> 16) in (0, stat.S_IFREG) for member in members),
              "Unsafe previous archive path or entry type")
        valid(sum(member.file_size for member in members) <= MAX_ARCHIVE_BYTES, "Previous archive exceeds size limit")
        valid("release.json" in names, "Missing previous release manifest")
        manifest = checked_json(archive.read("release.json"), "previous release")
        valid(type(manifest["schema"]) is int and manifest["schema"] in (1, 2) and manifest["status"] == STATUS,
              "Unsupported previous release manifest")
        checksums = manifest["sha256"]
        valid(isinstance(checksums, dict) and set(names) == set(checksums) | {"release.json"}
              and "release.json" not in checksums and all(is_sha256(value) for value in checksums.values()),
              "Previous release archive and manifest entries differ")
        content = {name: archive.read(name) for name in checksums}
        valid(all(digest(content[name]) == expected for name, expected in checksums.items()),
              "Previous release failed archive checksum verification")

    required = {LOCK, TEENSY_EVIDENCE, ESP_EVIDENCE, *RADIO_BINARIES, *DOCS, "teensy/PROT_OS7_MAX7219.ino.hex"}
    valid(required <= set(content), "Previous release is incomplete or missing required entries")
    teensy = checked_json(content[TEENSY_EVIDENCE], "previous Teensy build")
    esp = checked_json(content[ESP_EVIDENCE], "previous ESP32 build")
    head, source = source_commit(teensy, esp, content[LOCK])
    legacy = manifest["schema"] == 1
    valid(manifest["source_sha256"] == source["sha256"] and (legacy or manifest["source_git_head"] == head),
          "Previous release manifest source differs from its build evidence")
    compatible = compatibility(teensy, esp, checked_json(content[LOCK], "previous toolchain lock"), legacy=legacy)
    valid(json.dumps(manifest["compatibility"], sort_keys=True) == json.dumps(compatible, sort_keys=True),
          "Previous release compatibility entries differ from its build evidence")
    artifacts = artifact_checksums(teensy, esp)
    verify_artifacts(content, artifacts, esp)
    previous = manifest["previous_release_sha256"]
    valid((previous is None and PREVIOUS not in content) or
          (is_sha256(previous) and checksums.get(PREVIOUS) == previous), "Previous release recovery entry is inconsistent")
    backup = manifest["configuration_backup_included"]
    valid(type(backup) is bool and backup == (CONFIGURATION in content), "Previous release configuration entry is inconsistent")
    if backup:
        profile = checked_json(content[CONFIGURATION], "previous configuration")
        valid(validate_profile(profile) == profile, "Previous release configuration is not a validated helmet backup")
    allowed = required | set(artifacts) | {PREVIOUS, CONFIGURATION}
    for name in set(content) - allowed:
        path = name.removeprefix("dependencies/")
        valid(path != name and dependency(path), "Unexpected entry outside the rollback package allowlist")
        if path.startswith("firmware/"):
            valid(source["files"].get(path) == checksums[name], "Previous release dependency differs from source identity")


def package(output, previous=None, initial=False, config_path=None, previous_sha256=None):
    valid(bool(previous) != bool(initial), "Supply --previous or explicitly mark --initial-release")
    valid((previous is not None and is_sha256(previous_sha256)) or (initial and previous_sha256 is None),
          "--previous requires the retained --previous-sha256 and --initial-release takes no prior digest")
    if output.exists() or output.is_symlink():
        raise ValueError("Output already exists")
    identity = release_source(ROOT, DOCS)
    head, source = identity
    lock_bytes = local_file(LOCK)
    teensy_bytes = local_file("build/teensy40/manifest.json")
    esp_bytes = local_file("build/esp32-build.json")
    teensy = checked_json(teensy_bytes, "Teensy build")
    esp = checked_json(esp_bytes, "ESP32 build")
    source_commit(teensy, esp, lock_bytes, identity)
    sketch = local_file(SKETCH)
    valid(digest(sketch) == source["files"][SKETCH], "Teensy sketch changed during packaging")
    enabled_features = runpy.run_path(str(ROOT / "scripts/check-features.py"))["enabled_features"]
    compatible = compatibility(teensy, esp, checked_json(lock_bytes, "toolchain lock"), enabled_features(sketch.decode()))
    checksums = artifact_checksums(teensy, esp)
    content = {}
    for name in sorted(checksums):
        path = "build/teensy40/" + name.removeprefix("teensy/") if name.startswith("teensy/") else "build/esp32/" + name
        try:
            content[name] = local_file(path)
        except ValueError as error:
            raise ValueError(f"Missing or invalid release artifact: {name}") from error
    verify_artifacts(content, checksums, esp)
    for path in sorted(RADIO_ELFS):
        valid(digest(local_file(path)) == esp["artifacts"][path], "ESP32 ELF artifact checksum mismatch")
    content.update({LOCK: lock_bytes, TEENSY_EVIDENCE: teensy_bytes, ESP_EVIDENCE: esp_bytes})
    content.update((name, local_file(name)) for name in DOCS)
    for path, expected in source["files"].items():
        if dependency(path):
            raw = local_file(path)
            valid(digest(raw) == expected, "Dependency changed during packaging")
            content["dependencies/" + path] = raw
    for directory in TOOLCHAIN_DEPENDENCIES:
        for file in sorted((ROOT / directory).rglob("*")):
            path = file.relative_to(ROOT).as_posix()
            if file.is_file() and dependency(path):
                valid(safe_path(path), "Unsafe toolchain dependency path")
                content["dependencies/" + path] = local_file(path)
    if config_path:
        backup = validate_profile(checked_json(regular_file(config_path), "helmet configuration"))
        content[CONFIGURATION] = (json.dumps(backup, indent=2) + "\n").encode()
    if previous:
        # Allow ZIP headers and compression overhead in addition to payload bytes.
        valid(previous.stat().st_size <= MAX_ARCHIVE_BYTES + 1024 * 1024, "Previous archive exceeds size limit")
        raw = regular_file(previous)
        validate_previous(raw, previous_sha256)
        content[PREVIOUS] = raw
    manifest = {"schema": 2, "source_git_head": head, "source_sha256": source["sha256"], "status": STATUS,
                "compatibility": compatible, "previous_release_sha256": previous_sha256,
                "configuration_backup_included": bool(config_path),
                "sha256": {name: digest(raw) for name, raw in content.items()}}
    manifest_bytes = (json.dumps(manifest, indent=2) + "\n").encode()
    valid(sum(map(len, content.values())) + len(manifest_bytes) <= MAX_ARCHIVE_BYTES, "Release archive exceeds size limit")
    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix=".protogen-release-", dir=output.parent) as temp:
        staged = Path(temp) / "bundle.zip"
        # The output is a hard link to this inode. Secure its permissions at
        # creation, independent of a service workstation's ordinary umask.
        fd = os.open(staged, os.O_CREAT | os.O_EXCL | os.O_WRONLY, 0o600)
        with os.fdopen(fd, "wb") as stream:
            with zipfile.ZipFile(stream, "w", compression=zipfile.ZIP_DEFLATED) as archive:
                for name, raw in sorted(content.items()):
                    archive.writestr(name, raw)
                archive.writestr("release.json", manifest_bytes)
        valid(release_source(ROOT, DOCS) == identity, "Git source changed during packaging")
        os.link(staged, output)
    return manifest


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--previous", type=Path)
    parser.add_argument("--previous-sha256", help="Independently retained SHA-256 of the verified prior bundle")
    parser.add_argument("--initial-release", action="store_true")
    parser.add_argument("--configuration", type=Path, help="Helmet GET exported as a validated profile")
    args = parser.parse_args()
    try:
        package(args.output, args.previous, args.initial_release, args.configuration, args.previous_sha256)
    except (OSError, ValueError, TypeError, KeyError, IndexError, AttributeError, RuntimeError, zipfile.BadZipFile) as error:
        print(f"Packaging refused: {error}", file=sys.stderr)
        return 1
    print(f"Checksummed development recovery package: {args.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
