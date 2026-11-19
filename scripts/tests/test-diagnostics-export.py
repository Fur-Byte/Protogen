#!/usr/bin/env python3
"""Contract regression for the firmware GET -> portable diagnostics backup."""
import importlib.util
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import Mock, call, patch

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'scripts'))
from maintenance_support import config_ranges

spec = importlib.util.spec_from_file_location('export_diagnostics', ROOT / 'scripts/export-diagnostics.py')
export = importlib.util.module_from_spec(spec)
spec.loader.exec_module(export)


class DiagnosticsGetContract(unittest.TestCase):
    def reply(self):
        fields = config_ranges()
        config = {key: low for key, (low, _) in fields.items()}
        # The real firmware requires a release threshold above its trigger,
        # and a blink interval longer than the jitter.
        config['boop_release'] = fields['boop_release'][1]
        config['blink_interval_ms'] = fields['blink_interval_ms'][1]
        config['brightness'] = 15
        return {'ok': True, 'schema': 2, 'config': config,
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
                result = json.loads(output.read_text())
                self.assertEqual(result['configuration'], {
                    'schema': 2, 'name': 'Helmet configuration backup', 'config': reply['config']})
                self.assertEqual(result['status'], {'ok': True, 'sleeping': False})
                self.assertEqual(result['performance'], {'ok': True, 'render_attempts': 11})

    def test_unexpected_get_fields_fail_closed(self):
        for key, value in [('pairing_key', 'secret'), ('name', 'injected')]:
            with self.subTest(key=key):
                reply = self.reply()
                reply[key] = value
                with self.assertRaises(ValueError):
                    export.portable_profile_from_get(reply)

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
        reply = self.reply()
        reply['config']['boop_release'] = reply['config']['boop_trigger']
        with self.assertRaises(ValueError):
            export.portable_profile_from_get(reply)


if __name__ == '__main__':
    unittest.main()
