#!/usr/bin/env python3
"""Exercise the installer with a local release and harmless test CLI, no network."""
import hashlib
import importlib.util
import io
import json
from pathlib import Path
import sys
import os
import shlex
import tarfile
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'scripts'))
spec = importlib.util.spec_from_file_location('toolchain_installer', ROOT/'scripts/install-toolchains.py')
installer = importlib.util.module_from_spec(spec)
spec.loader.exec_module(installer)

class ToolchainIntegrity(unittest.TestCase):
    def run_installer(self, tamper=False, changed_source=False):
        with tempfile.TemporaryDirectory(prefix='protogen-installer-test-') as tmp:
            root = Path(tmp)
            lock = json.loads((ROOT/'toolchain.lock.json').read_text())
            data = root / 'build/toolchains/data'
            vendor, architecture = lock['platform']['id'].split(':')
            core = data / 'packages' / vendor / 'hardware' / architecture / lock['platform']['version']

            def fixture_tree(directory, filename, content):
                directory.mkdir(parents=True, exist_ok=True)
                (directory / filename).write_bytes(content)
                files = {filename: hashlib.sha256(content).hexdigest()}
                return hashlib.sha256(json.dumps(files, sort_keys=True, separators=(',', ':')).encode()).hexdigest()

            lock['source_integrity']['platform']['tree_sha256'] = fixture_tree(
                core, 'platform.txt', b'synthetic platform')
            for name, pin in lock['source_integrity']['tools'].items():
                directory = data / 'packages' / vendor / 'tools' / name / pin['version']
                pin['linux_x86_64_tree_sha256'] = fixture_tree(directory, 'tool', b'synthetic tool')
                pin["other_host_tree_sha256"] = {host: pin["linux_x86_64_tree_sha256"]
                                                     for host in pin["other_host_tree_sha256"]}
            records = []
            for name, version in lock['libraries'].items():
                directory = root / 'build/toolchains/user/libraries' / name
                metadata = f'name={name}\nversion={version}\n'.encode()
                lock['source_integrity']['libraries'][name]['tree_sha256'] = fixture_tree(
                    directory, 'library.properties', metadata)
                records.append({'library': {'name': name, 'version': version, 'install_dir': str(directory)}})
            if changed_source:
                (core / 'platform.txt').write_bytes(b'changed platform')

            # The stand-in answers inspection commands without executing external tools.
            responses = {
                'version --format': {'VersionString': lock['arduino_cli']},
                'core list': {'platforms': [{'id': lock['platform']['id'],
                                            'installed_version': lock['platform']['version']}]},
                'lib list': {'installed_libraries': records},
                'config dump': {'config': {'directories': {'data': str(data)}}},
            }
            harmless_cli = ('#!/bin/sh\ncase "$1 $2" in\n' + ''.join(
                f'{shlex.quote(command)} ) printf "%s\\n" {shlex.quote(json.dumps(value))};;\n'
                for command, value in responses.items()
            ) + 'esac\nexit 0\n').encode()
            archive = io.BytesIO()
            with tarfile.open(fileobj=archive, mode='w:gz') as output:
                member = tarfile.TarInfo('arduino-cli'); member.size = len(harmless_cli)
                output.addfile(member, io.BytesIO(harmless_cli))
            payload = archive.getvalue()
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
                 patch.object(sys, 'argv', ['install-toolchains.py', '--teensy-only']), \
                 patch.dict(os.environ):
                if tamper:
                    with self.assertRaisesRegex(RuntimeError, 'checksum manifest'):
                        installer.main()
                    self.assertFalse((root/'build/toolchains/bin/arduino-cli').exists())
                elif changed_source:
                    with self.assertRaisesRegex(RuntimeError, 'platform content mismatch'):
                        installer.main()
                    self.assertFalse((root/'build/toolchains/environment.sh').exists())
                else:
                    self.assertEqual(installer.main(), 0)
                    self.assertEqual((root/'build/toolchains/bin/arduino-cli').read_bytes(), harmless_cli)
                    self.assertTrue((root/'build/toolchains/environment.sh').is_file())
    def test_pinned_release_still_installs(self):
        self.run_installer(False)
    def test_manifest_change_fails_before_binary_is_installed(self):
        self.run_installer(True)
    def test_installed_source_change_fails_before_environment_is_published(self):
        self.run_installer(changed_source=True)

if __name__ == '__main__':
    unittest.main()
