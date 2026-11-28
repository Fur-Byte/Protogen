#!/usr/bin/env python3
"""Native backend admission and parity checks without a target SDK download."""

import json
import os
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import native_esp


class NativeEspTests(unittest.TestCase):
    def fixture(self, directory, config, partition_name):
        directory.mkdir()
        (directory / "config").mkdir()
        (directory / "config/sdkconfig.json").write_text(json.dumps(config))
        partition = directory / partition_name
        partition.parent.mkdir(exist_ok=True)
        partition.write_bytes(b"partition")

    def test_exact_configuration_and_partition_comparison(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            self.fixture(root / "pio", {"FREERTOS_HZ": 1000}, "partitions.bin")
            self.fixture(root / "native", {"FREERTOS_HZ": 1000}, "partition_table/partition-table.bin")
            self.assertTrue(
                native_esp.compare_configuration(root / "pio", root / "native")["equivalent_configuration"]
            )
            (root / "native/config/sdkconfig.json").write_text('{"FREERTOS_HZ": 100}')
            result = native_esp.compare_configuration(root / "pio", root / "native")
            self.assertFalse(result["equivalent_configuration"])
            self.assertEqual(result["configuration_differences"], {"FREERTOS_HZ": [1000, 100]})
            (root / "native/config/sdkconfig.json").write_text('{"FREERTOS_HZ": 1000}')
            (root / "native/partition_table/partition-table.bin").write_bytes(b"other")
            self.assertFalse(
                native_esp.compare_configuration(root / "pio", root / "native")["equivalent_configuration"]
            )

    def test_missing_key_is_not_equivalent_to_null(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            self.fixture(root / "pio", {"KEY": None}, "partitions.bin")
            self.fixture(root / "native", {}, "partition_table/partition-table.bin")
            self.assertFalse(
                native_esp.compare_configuration(root / "pio", root / "native")["equivalent_configuration"]
            )

    def test_environment_drops_unreviewed_compiler_and_python_overrides(self):
        with patch.dict(
            os.environ,
            {
                "CFLAGS": "-DWRONG",
                "CMAKE_TOOLCHAIN_FILE": "/tmp/evil",
                "PYTHONPATH": "/tmp/evil",
                "IDF_TARGET": "esp32s31",
                "PATH": "/tmp/evil",
            },
            clear=True,
        ):
            env = native_esp.native_environment(Path("/sdk"), Path("/venv/bin/python"))
        for key in ("CFLAGS", "CMAKE_TOOLCHAIN_FILE", "PYTHONPATH", "IDF_TARGET"):
            self.assertNotIn(key, env)
        self.assertNotIn("/tmp/evil", env["PATH"])
        self.assertEqual(env["IDF_COMPONENT_MANAGER"], "0")
        self.assertEqual(env["PIP_NO_INDEX"], "1")

    def test_sdk_version_marker_refuses_missing_wrong_and_symlink(self):
        with tempfile.TemporaryDirectory() as folder:
            core = Path(folder)
            sdk = core / native_esp.FRAMEWORK
            sdk.mkdir(parents=True)
            with self.assertRaises(RuntimeError):
                native_esp.sdk_version_marker(core, "6.1.0")
            native_esp.sdk_version_marker(core, "6.1.0", initialize=True)
            self.assertEqual((sdk / "version.txt").read_bytes(), b"6.1.0")
            with self.assertRaises(RuntimeError):
                native_esp.sdk_version_marker(core, "6.2.0", initialize=True)
            (sdk / "version.txt").unlink()
            (sdk / "actual.txt").write_bytes(b"6.1.0")
            (sdk / "version.txt").symlink_to(sdk / "actual.txt")
            with self.assertRaises(RuntimeError):
                native_esp.sdk_version_marker(core, "6.1.0", initialize=True)

    def test_sbom_inventory_never_uses_external_exclusions_or_vex(self):
        with tempfile.TemporaryDirectory() as folder:
            output = Path(folder)
            command, env = native_esp.sbom_command(
                Path("/venv/bin/python"), output, {"SBOM_EXCLUDED_CVES_FILE": "/untrusted/cache"}
            )
            self.assertIn("--no-sync-excluded-cves", command)
            self.assertEqual(command[command.index("--vex-format") + 1], "none")
            self.assertEqual(Path(env["SBOM_EXCLUDED_CVES_FILE"]).read_text(), "{}\n")

    def test_unqualified_host_rejected(self):
        with patch("native_esp.platform.system", return_value="Darwin"):
            with self.assertRaisesRegex(RuntimeError, "qualified"):
                native_esp.require_host()


if __name__ == "__main__":
    unittest.main()
