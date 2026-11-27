#!/usr/bin/env python3
"""Contract regression for the firmware GET -> portable diagnostics backup."""
import hashlib
import importlib.util
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import Mock, call, patch

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'scripts'))
import maintenance_support

spec = importlib.util.spec_from_file_location('export_diagnostics', ROOT / 'scripts/export-diagnostics.py')
export = importlib.util.module_from_spec(spec)
spec.loader.exec_module(export)


# Schema-2 wire fixture, independent of the metadata used by the validator.
CONFIG = {
    'brightness': 15, 'startup_expression': 3, 'mouth_mode': 1, 'sensitivity': 740,
    'noise_margin': 81, 'noise_floor': 310, 'boop_trigger': 455, 'boop_release': 690,
    'blink_interval_ms': 12200, 'blink_jitter_ms': 4200, 'transition_ms': 235,
    'attack_ms': 40, 'release_ms': 340, 'natural_blinks': 0, 'idle_motion': 1,
    'boop_mode': 1, 'boop_duration_ms': 3400,
}
EXPECTED_RANGES = {
    'brightness': (0, 15), 'startup_expression': (0, 5), 'mouth_mode': (0, 1),
    'sensitivity': (0, 1023), 'noise_margin': (1, 1023), 'noise_floor': (0, 1023),
    'boop_trigger': (0, 1022), 'boop_release': (1, 1023), 'blink_interval_ms': (1000, 60000),
    'blink_jitter_ms': (0, 5000), 'transition_ms': (0, 1000), 'attack_ms': (1, 2000),
    'release_ms': (1, 2000), 'natural_blinks': (0, 1), 'idle_motion': (0, 1),
    'boop_mode': (0, 1), 'boop_duration_ms': (100, 6000),
}


class DiagnosticsGetContract(unittest.TestCase):
    def reply(self):
        return {'ok': True, 'schema': 2, 'config': CONFIG.copy(),
                'brightness': {'configured': 15, 'effective': 0,
                               'maximum': 1, 'approved': 1}}

    def test_successful_live_get_projects_only_portable_settings(self):
        reply = self.reply()
        profile = export.portable_profile_from_get(reply)
        self.assertEqual(set(profile), {'schema', 'name', 'config'})
        self.assertEqual(profile['config'], reply['config'])
        self.assertNotIn('brightness', profile)

    def test_complete_export_for_all_supported_schema2_get_forms(self):
        for metadata in ('absent', 'ceiling', 'approval'):
            with self.subTest(metadata=metadata), tempfile.TemporaryDirectory() as folder:
                reply = self.reply()
                if metadata == 'absent':
                    del reply['brightness']
                elif metadata == 'ceiling':
                    del reply['brightness']['approved']
                serial = Mock()
                serial.command.side_effect = [
                    reply, {'ok': True, 'sleeping': False, 'private_future_field': 12345},
                    {'ok': True, 'render_attempts': 11, 'unknown_key': 'ignored'}]
                output = Path(folder) / 'diagnostics.json'
                argv = ['export-diagnostics.py', '--port', 'mock', '--output', str(output)]
                with patch.object(sys, 'argv', argv), patch.object(export, 'SerialTransport', return_value=serial):
                    self.assertEqual(export.main(), 0)
                self.assertEqual(serial.command.call_args_list, [call('GET'), call('STATUS'), call('PERF')])
                serial.close.assert_called_once_with()
                expected = {
                    'measurement': 'physical_diagnostics; external latency and paw energy still required',
                    'configuration': {'schema': 2, 'name': 'Helmet configuration backup', 'config': CONFIG},
                    'status': {'ok': True, 'sleeping': False},
                    'performance': {'ok': True, 'render_attempts': 11},
                }
                self.assertEqual(output.read_bytes(), (json.dumps(expected, indent=2) + '\n').encode())

    def test_reviewed_counters_are_exported_without_other_fields(self):
        for valid in (True, False):
            with self.subTest(valid=valid), tempfile.TemporaryDirectory() as folder:
                serial = Mock()
                serial.command.side_effect = [
                    self.reply(),
                    {'ok': True, 'blink_entropy_fallbacks': 2**32-1 if valid else True,
                     'future_secret': 123},
                    {'ok': True, 'idle_waits': 12 if valid else -1,
                     'idle_total_us': 2**64-1 if valid else 2**64,
                     'idle_max_us': 42 if valid else '42', 'future_secret': 123},
                ]
                output = Path(folder) / 'diagnostics.json'
                argv = ['export-diagnostics.py', '--port', 'mock', '--output', str(output)]
                with patch.object(sys, 'argv', argv), patch.object(export, 'SerialTransport', return_value=serial):
                    self.assertEqual(export.main(), 0)
                result = json.loads(output.read_text())
                self.assertEqual(result['status'], dict(ok=True, blink_entropy_fallbacks=2**32-1)
                                 if valid else dict(ok=True))
                self.assertEqual(result['performance'], dict(ok=True, idle_waits=12, idle_total_us=2**64-1,
                                                              idle_max_us=42) if valid else dict(ok=True))
                self.assertEqual(serial.command.call_args_list, [call('GET'), call('STATUS'), call('PERF')])
                serial.close.assert_called_once_with()

    def test_unexpected_get_fields_fail_closed(self):
        for key, value in [('pairing_key', 'secret'), ('name', 'injected')]:
            with self.subTest(key=key):
                reply = self.reply()
                reply[key] = value
                with self.assertRaises(ValueError):
                    export.portable_profile_from_get(reply)

    def test_backup_requires_exactly_the_exportable_fields(self):
        for config in (dict(CONFIG, pairing_key=123), {k: v for k, v in CONFIG.items() if k != 'noise_floor'}):
            with self.subTest(keys=list(config)), self.assertRaises(ValueError):
                export.portable_profile_from_get(dict(self.reply(), config=config))

    def test_invalid_brightness_metadata_fails_closed(self):
        for payload in (None, {'configured': '1'}, {'configured': 15, 'effective': 1, 'maximum': 1, 'key': 1},
                        {'configured': 15, 'effective': 0, 'maximum': 1, 'approved': True},
                        {'configured': 15, 'effective': 12, 'maximum': 1},
                        {'configured': 1, 'effective': 1, 'maximum': 1}):
            with self.subTest(payload=payload):
                reply = self.reply()
                reply['brightness'] = payload
                with self.assertRaises(ValueError):
                    export.portable_profile_from_get(reply)

    def test_invalid_firmware_config_is_not_silently_repaired(self):
        for edits in ({'boop_release': 455}, {'boop_release': 454},
                      {'blink_interval_ms': 4200}, {'blink_interval_ms': 4199}):
            with self.subTest(edits=edits), self.assertRaises(ValueError):
                export.portable_profile_from_get(dict(self.reply(), config=dict(CONFIG, **edits)))


class BackupRanges(unittest.TestCase):
    def test_backup_schema_is_an_integer(self):
        for schema in (True, 2.0, '2'):
            with self.subTest(schema=schema), self.assertRaises(ValueError):
                maintenance_support.validate_profile({'schema': schema, 'config': CONFIG.copy()})

    def test_reviewed_bounds_and_endpoints(self):
        self.assertEqual(maintenance_support.config_ranges(), EXPECTED_RANGES)
        for config in ({name: low for name, (low, _) in EXPECTED_RANGES.items()},
                       {name: high for name, (_, high) in EXPECTED_RANGES.items()}):
            profile = maintenance_support.validate_profile({'schema': 2, 'config': config})
            self.assertEqual(profile['config'], config)

    def test_invalid_values_for_each_field(self):
        for name, (low, high) in EXPECTED_RANGES.items():
            for invalid in (low - 1, high + 1, False, True, float(low)):
                with self.subTest(field=name, value=invalid), self.assertRaisesRegex(ValueError, name):
                    maintenance_support.validate_profile({'schema': 2, 'config': dict(CONFIG, **{name: invalid})})


class MetadataChecks(unittest.TestCase):
    def setUp(self):
        tmp = tempfile.TemporaryDirectory()
        self.addCleanup(tmp.cleanup)
        self.path = Path(tmp.name) / 'tools/helmet/config-metadata.json'
        self.path.parent.mkdir(parents=True)
        self.raw = (ROOT / 'tools/helmet/config-metadata.json').read_bytes()
        location = patch.object(maintenance_support, 'METADATA_PATH', self.path)
        location.start()
        self.addCleanup(location.stop)

    def test_missing_and_malformed_metadata(self):
        with self.assertRaises(FileNotFoundError):
            maintenance_support.config_ranges()
        duplicate_key = self.raw.replace(b'"schema": 2', b'"schema": 1, "schema": 2', 1)
        for raw in (b'', b'{', b'null', b'[]', b'{}', b'{"schema": 2, "fields": {}}', duplicate_key):
            with self.subTest(raw=raw), self.assertRaises(ValueError):
                self.path.write_bytes(raw)
                maintenance_support.config_ranges()

    def assert_invalid_declaration(self, metadata):
        raw = (json.dumps(metadata, indent=2) + '\n').encode()
        self.path.write_bytes(raw)
        # Even a matching file pin must not grant permission to export an unreviewed field.
        with patch.object(maintenance_support, 'REVIEWED_METADATA_SHA256', hashlib.sha256(raw).hexdigest()):
            with self.assertRaises(ValueError):
                maintenance_support.config_ranges()

    def test_schema_and_field_declarations(self):
        metadata = json.loads(self.raw)
        fields = metadata['fields']
        for schema in (1, 3, True, 2.0, '2'):
            with self.subTest(schema=schema):
                self.assert_invalid_declaration(dict(metadata, schema=schema))
        for label, changed in (
                ('missing', fields[1:]), ('duplicate', fields + [fields[0]]),
                ('unreviewed replacement', [dict(fields[0], name='pairing_key'), *fields[1:]]),
                ('missing name', [{}] + fields[1:]), ('non-object field', [None] + fields[1:])):
            with self.subTest(label=label):
                self.assert_invalid_declaration(dict(metadata, fields=changed))

    def test_invalid_metadata_ranges(self):
        for low, high in ((False, 15), (0, True), (0.0, 15), (0, '15'), (-1, 15), (4, 3), (0, 65536)):
            metadata = json.loads(self.raw)
            metadata['fields'][0].update(minimum=low, maximum=high)
            with self.subTest(low=low, high=high):
                self.assert_invalid_declaration(metadata)

    def test_plausible_drift_rejected_by_export_and_compiler_check(self):
        metadata = json.loads(self.raw)
        metadata['fields'][0]['maximum'] = 16
        self.path.write_text(json.dumps(metadata, indent=2) + '\n')
        with self.assertRaises(ValueError):
            maintenance_support.validate_profile({'schema': 2, 'config': dict(CONFIG, brightness=16)})

        # Run the generator against the real compiler inputs and the stale temporary metadata.
        checkout = self.path.parents[2]
        scripts = checkout / 'scripts'
        scripts.mkdir()
        generator = scripts / 'generate-config-metadata.py'
        shutil.copyfile(ROOT / 'scripts' / generator.name, generator)
        (scripts / 'tests').symlink_to(ROOT / 'scripts/tests', target_is_directory=True)
        (checkout / 'firmware').symlink_to(ROOT / 'firmware', target_is_directory=True)
        result = subprocess.run([sys.executable, str(generator), '--check'], cwd=checkout, capture_output=True, text=True)
        self.assertEqual(result.returncode, 1, result.stderr)
        self.assertIn('Configuration metadata drift', result.stderr)


if __name__ == '__main__':
    unittest.main()
