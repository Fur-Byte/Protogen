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

    def test_unqualified_host_rejected(self):
        with patch("native_esp.platform.system", return_value="Darwin"):
            with self.assertRaisesRegex(RuntimeError, "qualified"):
                native_esp.require_host()


if __name__ == "__main__":
    unittest.main()
