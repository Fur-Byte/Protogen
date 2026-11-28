#!/usr/bin/env python3
"""Offline checks for pinned download and package provisioning boundaries."""

import hashlib
import importlib.util
import io
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

SCRIPTS = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(SCRIPTS))
spec = importlib.util.spec_from_file_location("locked_esp", SCRIPTS / "install-locked-esp.py")
installer = importlib.util.module_from_spec(spec)
spec.loader.exec_module(installer)


class LockedEspInstaller(unittest.TestCase):
    def test_fresh_and_cached_artifact(self):
        with tempfile.TemporaryDirectory() as folder:
            destination = Path(folder) / "artifact"
            digest = hashlib.sha256(b"verified").hexdigest()
            with patch.object(installer.urllib.request, "urlopen", return_value=io.BytesIO(b"verified")):
                installer.fetch("https://example.invalid/artifact", destination, digest)
            self.assertEqual(destination.read_bytes(), b"verified")
            with patch.object(installer.urllib.request, "urlopen") as download:
                installer.fetch("https://example.invalid/artifact", destination, digest)
                download.assert_not_called()
            destination.write_bytes(b"changed")
            with self.assertRaisesRegex(RuntimeError, "Cached artifact"):
                installer.fetch("https://example.invalid/artifact", destination, digest)
            self.assertEqual(destination.read_bytes(), b"changed")

    def test_registry_client_header_is_scoped(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            digest = hashlib.sha256(b"verified").hexdigest()
            with patch.object(
                installer.urllib.request, "urlopen", side_effect=lambda *a, **k: io.BytesIO(b"verified")
            ) as download:
                installer.fetch("https://dl.registry.platformio.org/download/pkg", root / "registry", digest)
                request = download.call_args.args[0]
                version = installer.json.loads((installer.ROOT / "toolchain.lock.json").read_text())["esp32"][
                    "platformio"
                ]
                self.assertEqual(request.get_header("User-agent"), f"PlatformIO/{version}")
                installer.fetch("https://example.invalid/pkg", root / "other", digest)
                self.assertIsNone(download.call_args.args[0].get_header("User-agent"))

    def test_multiple_exact_hashes(self):
        first, second = "a" * 64, "b" * 64
        self.assertEqual(
            installer.parse_locked_requirement(f"pkg==1.0 --hash=sha256:{first} --hash=sha256:{second}"),
            ("pkg", "1.0", {first, second}),
        )
        for line in ("pkg>=1.0", "pkg==1.0", "pkg==1.0 --hash=md5:abc", "pkg==1.0 --index-url=x"):
            with self.assertRaises(RuntimeError):
                installer.parse_locked_requirement(line)

    def test_download_mismatch_is_not_published(self):
        with tempfile.TemporaryDirectory() as folder:
            destination = Path(folder) / "artifact"
            digest = hashlib.sha256(b"verified").hexdigest()
            with patch.object(installer.urllib.request, "urlopen", return_value=io.BytesIO(b"wrong")):
                with self.assertRaisesRegex(RuntimeError, "Downloaded artifact"):
                    installer.fetch("https://example.invalid/artifact", destination, digest)
            self.assertEqual(list(Path(folder).iterdir()), [])


if __name__ == "__main__":
    unittest.main()
