#!/usr/bin/env python3
"""Exercise the release CLI in temporary Git repositories with synthetic images."""
import hashlib
import os
import stat
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest
import warnings
from zipfile import ZipFile

ROOT = Path(__file__).resolve().parents[2]
SKETCH = "firmware/PROT_OS7_MAX7219/PROT_OS7_MAX7219.ino"
TEENSY = "build/teensy40/manifest.json"
ESP = "build/esp32-build.json"
HEX = "PROT_OS7_MAX7219.ino.hex"
FEATURES = ("VisorSensor", "BoopSensor", "Microphone", "WirelessPaw", "VisorConnector")
DOCS = ("docs/HARDWARE_WIRING.md", "hardware/connector-map.csv", "hardware/calibration-records.csv",
        "docs/EFFICIENCY_QUALIFICATION.md", "docs/RELEASE_RECOVERY.md", "firmware/esp32/README.md")
ROLES = {"helmet": ("esp32-c3-devkitm-1", "ESP_CONSOLE_UART_DEFAULT"),
         "paw": ("seeed_xiao_esp32c3", "ESP_CONSOLE_USB_SERIAL_JTAG")}


def sha(data):
    return hashlib.sha256(data).hexdigest()


class PackagingTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix="protogen-release-fixture-")
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name) / "checkout"
        self.root.mkdir()
        self.output = Path(temporary.name) / "synthetic-release-fixture.zip"
        for name in (".gitignore", "toolchain.lock.json", "scripts/build_support.py", "scripts/uart_key.py",
                     "scripts/package-release.py", "scripts/sketch_features.py", "scripts/maintenance_support.py",
                     "tools/helmet/config-metadata.json"):
            target = self.root / name
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(ROOT / name, target)
        for name in DOCS:
            self.write(name, b"Synthetic release packaging test fixture\n")
        self.write(SKETCH, "".join(f"// #define {name}\n" for name in FEATURES).encode())
        self.git("init", "-q")
        self.git("add", ".")
        self.commit("Synthetic fixture")
        self.create_builds()
        self.write("build/temporary-cache/input.tmp", b"Legitimate ignored build output.")
        self.write("firmware/esp32/compile_commands.json", b"[]")

    def git(self, *args):
        return subprocess.check_output(["git", "-C", str(self.root), *args], text=True).strip()

    def commit(self, message):
        self.git("-c", "user.name=Fixture", "-c", "user.email=fixture@example.invalid",
                 "commit", "-q", "-m", message)

    def write(self, name, data):
        path = self.root / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)

    def create_builds(self, features=()):
        git_head = self.git("rev-parse", "HEAD")
        tracked = self.git("ls-files").splitlines()
        files = {name: sha((self.root / name).read_bytes()) for name in tracked
                 if name.startswith(("firmware/", "scripts/", "tools/", "profiles/")) or name == "toolchain.lock.json"}
        source = {"files": files, "sha256": sha(json.dumps(files, sort_keys=True, separators=(",", ":")).encode())}
        lock = json.loads((self.root / "toolchain.lock.json").read_bytes())
        base = dict(state="success", source=source, git_head=git_head, working_tree_dirty=False,
                    git_status_porcelain=[])
        fixture_hex = b"Synthetic HEX fixture. Never flash."
        self.write("build/teensy40/" + HEX, fixture_hex)
        selected = {name: {"version": version} for name, version in lock["libraries"].items()}
        selected.update({name: {"version": version} for name, version in lock["bundled_libraries"].items()})
        selected["Entropy"] = {"source_sha256": lock["bundled_source_libraries"]["Entropy"]}
        command = ["arduino-cli", "compile", "--build-property", "build.flags.defs=-D__IMXRT1062__ -Werror",
                   "--fqbn", lock["fqbn"], "--clean", "snapshot/PROT_OS7_MAX7219"]
        toolchain = {name: lock[name] for name in ("arduino_cli", "platform", "libraries", "bundled_libraries",
                                                  "bundled_source_libraries", "fqbn")}
        toolchain["cli"] = "arduino-cli"
        teensy = dict(base, schema=1, kind="build", fqbn=lock["fqbn"], toolchain=toolchain,
                      results=[dict(fqbn=lock["fqbn"], features=list(features), exit_code=0, warnings=0, command=command,
                                    configured_source_sha256=files[SKETCH], selected_libraries=selected)],
                      artifacts={HEX: sha(fixture_hex)})
        esp_lock = lock["esp32"]
        esp = dict(base, platform=esp_lock["platform"], esp_idf=esp_lock["esp_idf"], mbedtls=esp_lock["mbedtls"],
                   resolved=dict(esp_idf=esp_lock["esp_idf"], mbedtls=esp_lock["mbedtls"],
                                 platformio="PlatformIO Core, version " + esp_lock["platformio"]),
                   uart_auth=False, platformio_overrides=[], exit_code=0, warnings=0, targets={}, artifacts={},
                   command=["pio", "run", "--project-dir", "firmware/esp32", "-e", "helmet", "-e", "paw"])
        for role, (board, console) in ROLES.items():
            for name in ("firmware.bin", "firmware.elf", "bootloader.bin", "partitions.bin"):
                key = f"build/esp32/{role}/{name}"
                raw = f"Synthetic fixture for {key}. Never flash.".encode()
                self.write(key, raw)
                esp["artifacts"][key] = sha(raw)
                if name == "firmware.bin":
                    esp["targets"][role] = dict(
                        board=board, application_partition_bytes=1536000, uart_auth_linked=False, binary_bytes=len(raw),
                        sdk_settings={console: True, "FREERTOS_HZ": 1000, "ESPTOOLPY_FLASHSIZE": "4MB",
                                      "PARTITION_TABLE_SINGLE_APP_LARGE": True,
                                      "MBEDTLS_PSK_MODES": True, "MBEDTLS_KEY_EXCHANGE_PSK": True,
                                      "MBEDTLS_SSL_PROTO_DTLS": True, "MBEDTLS_SSL_PROTO_TLS1_2": True,
                                      "MBEDTLS_SSL_COOKIE_C": True, "MBEDTLS_SSL_PROTO_TLS1_3": False,
                                      "MBEDTLS_SSL_RENEGOTIATION": False, "MBEDTLS_CLIENT_SSL_SESSION_TICKETS": False,
                                      "MBEDTLS_SERVER_SSL_SESSION_TICKETS": False, "MBEDTLS_ASYMMETRIC_CONTENT_LEN": False,
                                      "MBEDTLS_SSL_MAX_CONTENT_LEN": 1024, "ESP_WIFI_SOFTAP_SUPPORT": False,
                                      "MBEDTLS_GCM_C": True, "MBEDTLS_ECDH_C": True, "MBEDTLS_ECP_DP_SECP256R1_ENABLED": True,
                                      "NVS_ENCRYPTION": False, "SECURE_BOOT": False,
                                      "SECURE_FLASH_ENC_ENABLED": False, "PROTOGEN_PRODUCTION": False})
            target = esp["targets"][role]
            sdk = json.dumps({**target["sdk_settings"], "ESP_MAIN_TASK_STACK_SIZE": 12288}).encode()
            self.write(f"build/esp32/{role}/config/sdkconfig.json", sdk)
            target["sdkconfig_sha256"] = sha(sdk)
        self.write(TEENSY, json.dumps(teensy).encode())
        self.write(ESP, json.dumps(esp).encode())

    def edit_manifest(self, name, edit):
        record = json.loads((self.root / name).read_bytes())
        edit(record)
        self.write(name, json.dumps(record).encode())

    def run_packager(self, *args, output=None):
        return subprocess.run([sys.executable, str(self.root / "scripts/package-release.py"),
                               "--output", str(output or self.output), *map(str, args)],
                              cwd=self.root, capture_output=True, text=True, timeout=20)

    def package_initial(self, output=None):
        result = self.run_packager("--initial-release", output=output)
        self.assertEqual(result.returncode, 0, result.stderr)

    def rejected(self, expected, *args):
        result = self.run_packager(*(args or ("--initial-release",)))
        self.assertNotEqual(result.returncode, 0, result.stdout)
        self.assertRegex(result.stderr, expected)
        self.assertFalse(self.output.exists(), "Rejected packaging left a partial bundle")

    def prior(self):
        path = self.output.with_name("synthetic-prior-fixture.zip")
        self.package_initial(path)
        return path

    def rewrite_prior(self, path, edit, *, update_checksums=False):
        with ZipFile(path) as archive:
            entries = {name: archive.read(name) for name in archive.namelist()}
        edit(entries)
        if update_checksums:
            record = json.loads(entries.pop("release.json"))
            record["sha256"] = {name: sha(raw) for name, raw in entries.items()}
            entries["release.json"] = json.dumps(record).encode()
        with ZipFile(path, "w") as archive:
            for name, raw in entries.items():
                archive.writestr(name, raw)

    def prior_args(self, path):
        return "--previous", path, "--previous-sha256", sha(path.read_bytes())

    def test_clean_source_and_matching_evidence(self):
        self.package_initial()
        with ZipFile(self.output) as archive:
            record = json.loads(archive.read("release.json"))
            self.assertEqual(record["source_git_head"], self.git("rev-parse", "HEAD"))
            self.assertEqual(record["status"], "development; physical qualification pending")
            self.assertEqual(archive.read("teensy/" + HEX), b"Synthetic HEX fixture. Never flash.")
            self.assertEqual(set(archive.namelist()), set(record["sha256"]) | {"release.json"})
            self.assertTrue(all(sha(archive.read(name)) == expected for name, expected in record["sha256"].items()))
            for role in ROLES:
                self.assertEqual(archive.read(f"evidence/sdkconfig.{role}.json"),
                                 (self.root / f"build/esp32/{role}/config/sdkconfig.json").read_bytes())
            self.assertFalse(any("compile_commands" in name or "temporary-cache" in name for name in archive.namelist()))

    def test_output_inode_is_private_under_ordinary_umask(self):
        original_umask = os.umask(0o022)
        try:
            self.bridge_builds(teensy_auth=True, esp_auth=True)
            prior = self.prior()
            self.assertEqual(stat.S_IMODE(prior.stat().st_mode), 0o600)
            result = self.run_packager(*self.prior_args(prior))
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(stat.S_IMODE(self.output.stat().st_mode), 0o600)
            with ZipFile(self.output) as archive:
                self.assertEqual(archive.read('recovery/previous-release.zip'), prior.read_bytes())
        finally:
            os.umask(original_umask)

    def test_output_under_ignored_build_directory(self):
        output = self.root / "build/releases/synthetic-release-fixture.zip"
        self.package_initial(output=output)
        self.assertTrue(output.is_file())
        self.assertEqual(self.git("status", "--porcelain"), "")

    def test_dirty_tracked_source(self):
        self.write(SKETCH, (self.root / SKETCH).read_bytes() + b"// Dirty\n")
        self.rejected(r"(?i)dirty")

    def test_hidden_tracked_source(self):
        self.git("update-index", "--assume-unchanged", SKETCH)
        self.write(SKETCH, (self.root / SKETCH).read_bytes() + b"// Hidden\n")
        self.rejected(r"(?i)hidden index")

    def test_untracked_build_relevant_source(self):
        self.write("firmware/extra_untracked.h", b"// fixture")
        self.rejected(r"(?i)dirty")

    def test_ignored_build_relevant_source(self):
        self.write("firmware/ProtogenUartKey.h", b"// Synthetic placeholder, no key")
        self.create_builds()
        self.rejected(r"(?i)ignored.*source")

    def test_tracked_uart_provisioning_header(self):
        name = "firmware/PROT_OS7_MAX7219/src/UartCrypto/ProtogenUartKey.h"
        self.write(name, b"// Synthetic placeholder, no key")
        self.git("add", "-f", name)
        self.commit("Fixture with a misplaced provisioning header")
        self.create_builds()
        self.rejected(r"(?i)provisioning.*header")

    def test_unidentified_source(self):
        shutil.rmtree(self.root / ".git")
        self.rejected(r"(?i)identify.*git|git.*identif")

    def test_missing_artifact(self):
        (self.root / "build/esp32/paw/partitions.bin").unlink()
        self.rejected(r"(?i)missing.*artifact")

    def test_altered_teensy_hash(self):
        self.write("build/teensy40/" + HEX, b"Altered fixture")
        self.rejected(r"(?i)teensy.*checksum")

    def test_altered_esp32_artifact(self):
        self.write("build/esp32/helmet/firmware.bin", b"Altered fixture")
        self.rejected(r"(?i)esp32.*checksum")

    def test_altered_esp32_link_evidence(self):
        self.write("build/esp32/paw/firmware.elf", b"Altered fixture")
        self.rejected(r"(?i)esp32.*checksum")

    def test_mismatched_feature_mask(self):
        self.edit_manifest(TEENSY, lambda record: record["results"][0]["features"].append("Microphone"))
        self.rejected(r"(?i)feature.*mask")

    def bridge_builds(self, *, teensy_auth=False, esp_auth=False):
        self.write(SKETCH, (self.root / SKETCH).read_bytes().replace(b"// #define WirelessPaw", b"#define WirelessPaw"))
        self.git("add", SKETCH)
        self.commit("Enable the bridge in fixture source")
        self.create_builds(features=("WirelessPaw",))
        if teensy_auth:
            def authenticate_teensy(record):
                record["results"][0]["command"][3] += " -DPROTOGEN_UART_AUTH=1"
            self.edit_manifest(TEENSY, authenticate_teensy)
        if esp_auth:
            def authenticate_esp(record):
                record["uart_auth"] = True
                record["targets"]["helmet"]["uart_auth_linked"] = True
            self.edit_manifest(ESP, authenticate_esp)

    def test_conflicting_uart_authentication(self):
        self.bridge_builds(esp_auth=True)
        self.rejected(r"(?i)uart.*mode")

    def test_reverse_uart_authentication_conflict(self):
        self.bridge_builds(teensy_auth=True)
        self.rejected(r"(?i)uart.*mode")

    def test_matching_uart_authentication(self):
        self.bridge_builds(teensy_auth=True, esp_auth=True)
        self.package_initial()
        with ZipFile(self.output) as archive:
            modes = json.loads(archive.read("release.json"))["compatibility"]
            self.assertIs(modes["wireless_bridge_enabled"], True)
            self.assertIs(modes["teensy_uart_auth"], True)
            self.assertIs(modes["esp_uart_auth"], True)

    def test_corrupt_manifest(self):
        self.write(TEENSY, b'{"state": "success", "state": "success"}')
        self.rejected(r"(?i)(duplicate|invalid).*manifest|manifest.*(duplicate|invalid)")

    def test_corrupt_previous_manifest(self):
        path = self.prior()
        self.rewrite_prior(path, lambda entries: entries.update({"release.json": b'{'}))
        self.rejected(r"(?i)manifest", *self.prior_args(path))

    def test_duplicate_archive_entries(self):
        path = self.prior()
        with warnings.catch_warnings():
            warnings.simplefilter("ignore", UserWarning)
            with ZipFile(path, "a") as archive:
                archive.writestr("helmet/firmware.bin", b"Synthetic duplicate")
        self.rejected(r"(?i)duplicate", *self.prior_args(path))

    def test_traversal_archive_entries(self):
        path = self.prior()
        self.rewrite_prior(path, lambda entries: entries.update({"../fixture.txt": b"Synthetic traversal entry"}),
                           update_checksums=True)
        self.rejected(r"(?i)unsafe|traversal", *self.prior_args(path))

    def test_incomplete_prior_release(self):
        path = self.prior()
        self.rewrite_prior(path, lambda entries: entries.pop("paw/partitions.bin"), update_checksums=True)
        self.rejected(r"(?i)previous.*(incomplete|missing)", *self.prior_args(path))

    def test_prior_build_and_manifest_hash_conflict(self):
        path = self.prior()
        self.rewrite_prior(path, lambda entries: entries.update({"helmet/firmware.bin": b"Altered fixture"}),
                           update_checksums=True)
        self.rejected(r"(?i)esp32.*checksum", *self.prior_args(path))

    def test_prior_payload_checksum_mismatch(self):
        path = self.prior()
        self.rewrite_prior(path, lambda entries: entries.update({"helmet/firmware.bin": b"Altered fixture"}))
        self.rejected(r"(?i)previous.*checksum", *self.prior_args(path))

    def test_prior_extra_recovery_payload(self):
        path = self.prior()
        self.rewrite_prior(path, lambda entries: entries.update({"configuration/pairing.json": b"{}"}),
                           update_checksums=True)
        self.rejected(r"(?i)unexpected.*entry", *self.prior_args(path))

    def test_configuration_payload_is_allowlisted(self):
        profile = json.loads((ROOT / "profiles/everyday.json").read_bytes())
        path = self.output.with_name("synthetic-configuration.json")
        path.write_text(json.dumps(dict(profile, psk="placeholder fixture field")))
        self.rejected(r"(?i)configuration|pairing", "--initial-release", "--configuration", path)
        path.write_text(json.dumps(profile))
        result = self.run_packager("--initial-release", "--configuration", path)
        self.assertEqual(result.returncode, 0, result.stderr)
        with ZipFile(self.output) as archive:
            backup = json.loads(archive.read("configuration/helmet.json"))
            self.assertEqual(set(backup), {"schema", "name", "config"})
            self.assertEqual(backup["config"], profile["config"])
            self.assertIs(json.loads(archive.read("release.json"))["configuration_backup_included"], True)

    def test_existing_output_is_preserved(self):
        self.output.write_bytes(b"Existing file")
        result = self.run_packager("--initial-release")
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(self.output.read_bytes(), b"Existing file")

    def test_source_sha_mismatch_after_commit(self):
        self.write("docs/extra.md", b"New unrelated tracked file\n")
        self.git("add", "docs/extra.md")
        self.commit("Source content hash unchanged while Git commit changes")
        self.rejected(r"(?i)(source|commit).*(mismatch|match)|rebuild")

    def test_toolchain_conflict(self):
        self.edit_manifest(ESP, lambda record: record["resolved"].update(platformio="PlatformIO Core, version 0.0.0"))
        self.rejected(r"(?i)toolchain")

    def test_platformio_override(self):
        self.edit_manifest(ESP, lambda record: record.update(platformio_overrides=["PLATFORMIO_BUILD_FLAGS"]))
        self.rejected(r"(?i)platformio")

    def test_current_build_requires_both_sdkconfig_digests(self):
        self.edit_manifest(ESP, lambda record: record["targets"]["paw"].pop("sdkconfig_sha256", None))
        self.rejected(r"(?i)sdkconfig")

    def test_sdkconfig_drift_outside_the_checked_settings(self):
        path = "build/esp32/helmet/config/sdkconfig.json"
        config = json.loads((self.root / path).read_bytes())
        config["ESP_MAIN_TASK_STACK_SIZE"] += 1024
        self.write(path, json.dumps(config).encode())
        self.rejected(r"(?i)sdkconfig")

    def test_legacy_prior_bundle_with_complete_evidence(self):
        path = self.prior()

        def legacy_layout(entries):
            manifest = json.loads(entries["release.json"])
            manifest["schema"] = 1
            del manifest["source_git_head"]
            entries["release.json"] = json.dumps(manifest).encode()
            evidence = json.loads(entries["evidence/esp32-build.json"])
            del evidence["platformio_overrides"]
            for role in ROLES:
                del evidence["targets"][role]["sdkconfig_sha256"]
                del entries[f"evidence/sdkconfig.{role}.json"]
            entries["evidence/esp32-build.json"] = json.dumps(evidence).encode()

        self.rewrite_prior(path, legacy_layout, update_checksums=True)
        result = self.run_packager(*self.prior_args(path))
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_initial_and_previous_paths(self):
        path = self.prior()
        self.rejected(r"(?i)initial.release", "--previous", path, "--initial-release")
        self.rejected(r"(?i)sha256", "--previous", path)
        self.rejected(r"(?i)sha-?256|checksum", "--previous", path, "--previous-sha256", "0" * 64)
        prior = path.read_bytes()
        result = self.run_packager(*self.prior_args(path))
        self.assertEqual(result.returncode, 0, result.stderr)
        with ZipFile(path) as archive:
            self.assertIsNone(json.loads(archive.read("release.json"))["previous_release_sha256"])
            self.assertNotIn("recovery/previous-release.zip", archive.namelist())
        with ZipFile(self.output) as archive:
            self.assertEqual(archive.read("recovery/previous-release.zip"), prior)
            self.assertEqual(json.loads(archive.read("release.json"))["previous_release_sha256"], sha(prior))
        self.assertEqual(path.read_bytes(), prior)


if __name__ == "__main__":
    unittest.main()
