#!/usr/bin/env python3
"""Run the configuration sanitizer suite with only declared host prerequisites."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class PortableRunner(unittest.TestCase):
    def test_configuration_runner_does_not_require_ripgrep(self):
        with tempfile.TemporaryDirectory(prefix="protogen-portable-path-") as tmp:
            commands = Path(tmp)
            for name in ("bash", "dirname", "mktemp", "rm", "cat", "grep", "python3", "node"):
                executable = shutil.which(name)
                self.assertIsNotNone(executable, name)
                (commands / name).symlink_to(executable)
            compiler = shutil.which(os.environ.get("CXX", "clang++"))
            self.assertIsNotNone(compiler)
            env = dict(os.environ, PATH=str(commands), CXX=compiler)
            self.assertIsNone(shutil.which("rg", path=env["PATH"]))
            result = subprocess.run(
                ["/bin/bash", str(ROOT / "scripts/test-configuration-safety.sh")],
                env=env,
                capture_output=True,
                text=True,
            )
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn("narrowing-overflow compile-time ceilings rejected", result.stdout)


if __name__ == "__main__":
    unittest.main()
