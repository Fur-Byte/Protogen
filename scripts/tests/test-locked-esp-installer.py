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
