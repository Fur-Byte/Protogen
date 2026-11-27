#!/usr/bin/env python3
"""Offline regression tests for ESP package identity checks."""

from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from esp_tool_integrity import tool_tree_digest, verify_esp_tools


class ToolIntegrity(unittest.TestCase):
    def test_tool_paths_and_content(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            tool = root / "packages/toolchain-riscv32-esp"
            tool.mkdir(parents=True)
            binary = tool / "compiler"
            binary.write_bytes(b"trusted")
            link = tool / "cc"
            link.symlink_to("compiler")
            digest = tool_tree_digest(tool)
            pins = {"toolchain-riscv32-esp": {"tree_sha256": digest}}
            with patch("esp_tool_integrity.platform.system", return_value="Linux"), patch(
                "esp_tool_integrity.platform.machine", return_value="x86_64"
            ):
                self.assertEqual(verify_esp_tools(root, pins)["trees"], {"toolchain-riscv32-esp": digest})
                binary.write_bytes(b"changed")
                with self.assertRaisesRegex(RuntimeError, "content mismatch"):
                    verify_esp_tools(root, pins)
                binary.write_bytes(b"trusted")
                link.unlink()
                link.symlink_to("other")
                with self.assertRaisesRegex(RuntimeError, "content mismatch"):
                    verify_esp_tools(root, pins)

    def test_generated_scope(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            (root / "original.py").write_text("original")
            original = tool_tree_digest(root, esptool=True)
            (root / "_contrib").mkdir()
            (root / "_contrib" / "installed.py").write_text("generated")
            self.assertEqual(original, tool_tree_digest(root, esptool=True))
            self.assertNotEqual(original, tool_tree_digest(root))
            (root / "extra.py").write_text("changed")
            self.assertNotEqual(original, tool_tree_digest(root, esptool=True))

    def test_host_scope(self):
        with patch("esp_tool_integrity.platform.system", return_value="Darwin"):
            self.assertEqual(verify_esp_tools(Path("/missing"))["state"], "unverified_host")


if __name__ == "__main__":
    unittest.main()
