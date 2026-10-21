#!/usr/bin/env python3
"""Exercise the installer with a local release and harmless test CLI, no network."""
import hashlib
import importlib.util
import io
import json
from pathlib import Path
import sys
import tarfile
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('toolchain_installer', ROOT/'scripts/install-toolchains.py')
installer = importlib.util.module_from_spec(spec)
spec.loader.exec_module(installer)

class ToolchainIntegrity(unittest.TestCase):
    def run_installer(self, tamper):
        with tempfile.TemporaryDirectory(prefix='protogen-installer-test-') as tmp:
            root = Path(tmp)
            archive = io.BytesIO()
            # Reviewed stand-in for an external tool, used only inside this test.
            harmless_cli = b'#!/bin/sh\nexit 0\n'
            with tarfile.open(fileobj=archive, mode='w:gz') as output:
                member = tarfile.TarInfo('arduino-cli'); member.size = len(harmless_cli)
                output.addfile(member, io.BytesIO(harmless_cli))
            payload = archive.getvalue()
            lock = json.loads((ROOT/'toolchain.lock.json').read_text())
            filename = 'arduino-cli_'+lock['arduino_cli']+'_Linux_64bit.tar.gz'
            manifest = (hashlib.sha256(payload).hexdigest()+'  '+filename+'\n').encode()
            lock['arduino_cli_checksums_sha256'] = hashlib.sha256(manifest).hexdigest()
            (root/'toolchain.lock.json').write_text(json.dumps(lock))
            def download(url, path):
                path.parent.mkdir(parents=True, exist_ok=True)
                data = manifest + (b'# changed release manifest\n' if tamper else b'') if url.endswith('-checksums.txt') else payload
                path.write_bytes(data)
                return path
            with patch.object(installer, 'ROOT', root), patch.object(installer, 'download', download), \
                 patch.object(installer.platform, 'system', return_value='Linux'), \
                 patch.object(installer.platform, 'machine', return_value='x86_64'), \
                 patch.object(sys, 'argv', ['install-toolchains.py', '--teensy-only']):
                if tamper:
                    with self.assertRaisesRegex(RuntimeError, 'checksum manifest'):
                        installer.main()
                    self.assertFalse((root/'build/toolchains/bin/arduino-cli').exists())
                else:
                    self.assertEqual(installer.main(), 0)
                    self.assertEqual((root/'build/toolchains/bin/arduino-cli').read_bytes(), harmless_cli)
                    self.assertTrue((root/'build/toolchains/environment.sh').is_file())
    def test_pinned_release_still_installs(self):
        self.run_installer(False)
    def test_manifest_change_fails_before_binary_is_installed(self):
        self.run_installer(True)

if __name__ == '__main__':
    unittest.main()
