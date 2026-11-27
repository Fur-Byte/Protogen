#!/usr/bin/env python3
"""Offline tests for strict ESP source-tree verification and generated files."""
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from esp_source_integrity import package_source_digest, verify_esp_sources


class EspSourceIntegrity(unittest.TestCase):
    def test_source_mutation_and_generated_content(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            platform = root / "platforms/espressif32"
            framework = root / "packages/framework-espidf"
            for directory in (platform, framework):
                directory.mkdir(parents=True)
                (directory / "source.c").write_text("trusted")
            lock = {
                "esp32": {"esp_idf": "6.1.0"},
                "esp32_source_integrity": {
                    "platforms/espressif32": {"tree_sha256": package_source_digest(platform, {})},
                    "packages/framework-espidf": {"tree_sha256": package_source_digest(framework, {})},
                },
            }
            expected = verify_esp_sources(root, lock)
            (framework / "version.txt").write_text("6.1.0")
            self.assertEqual(expected, verify_esp_sources(root, lock))
            (framework / "version.txt").write_text("wrong")
            with self.assertRaisesRegex(RuntimeError, "generated"):
                verify_esp_sources(root, lock)
            (framework / "version.txt").unlink()
            (platform / "source.c").write_text("mutated")
            with self.assertRaisesRegex(RuntimeError, "source content mismatch"):
                verify_esp_sources(root, lock)

    def test_unknown_generated_file_and_symlink_rejected(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            (root / "source").write_text("known")
            original = package_source_digest(root, {})
            cache = root / "__pycache__"
            cache.mkdir()
            (cache / "source.pyc").write_bytes(b"generated")
            self.assertEqual(original, package_source_digest(root, {}))
            (cache / "unexpected.c").write_bytes(b"not bytecode")
            self.assertNotEqual(original, package_source_digest(root, {}))
            (root / "link").symlink_to(root / "source")
            with self.assertRaisesRegex(RuntimeError, "symlink"):
                package_source_digest(root, {})


if __name__ == "__main__":
    unittest.main()
