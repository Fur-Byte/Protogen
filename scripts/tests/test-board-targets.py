#!/usr/bin/env python3
"""Reject unqualified S31 pin selection while preserving isolated research."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class BoardTargets(unittest.TestCase):
    def compile(self, definitions):
        with tempfile.TemporaryDirectory() as tmp:
            Path(tmp, "sdkconfig.h").write_text("")
            return subprocess.run(
                [
                    os.getenv("CXX", "clang++"),
                    "-std=c++17",
                    "-x",
                    "c++",
                    "-fsyntax-only",
                    "-DHELMET_RECEIVER",
                    "-DESP_PLATFORM",
                    *["-D" + d for d in definitions],
                    "-I" + tmp,
                    "-I" + str(ROOT / "firmware/esp32/src"),
                    "-I" + str(ROOT / "firmware/PROT_OS7_MAX7219/src/HelmetProtocol"),
                    "-",
                ],
                input='#include "DeviceConfig.h"\n',
                capture_output=True,
                text=True,
            )

    def test_production_c3_passes(self):
        self.assertEqual(self.compile(["CONFIG_IDF_TARGET_ESP32C3=1"]).returncode, 0)

    def test_s31_requires_research_opt_in(self):
        result = self.compile(["CONFIG_IDF_TARGET_ESP32S31=1"])
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("NO_QUALIFIED_BOARD_MAPPING", result.stderr)

    def test_s31_research_remains_buildable(self):
        self.assertEqual(
            self.compile(["CONFIG_IDF_TARGET_ESP32S31=1", "PROTOGEN_S31_PREFLIGHT_ONLY=1"]).returncode, 0
        )

    def test_research_flag_does_not_enable_other_targets(self):
        result = self.compile(["CONFIG_IDF_TARGET_ESP32S3=1", "PROTOGEN_S31_PREFLIGHT_ONLY=1"])
        self.assertNotEqual(result.returncode, 0)


if __name__ == "__main__":
    unittest.main()
