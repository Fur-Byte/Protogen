#!/usr/bin/env python3
"""Ensure one host output directory cannot silently reuse a different profile."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class TwinCompileProfile(unittest.TestCase):
    def test_signature_changes_on_flag_switch_and_back(self):
        with tempfile.TemporaryDirectory(prefix='protogen-twin-signature-') as tmp:
            output = Path(tmp) / 'twin'
            target = output / '.compile-settings'

            def signature(*options):
                subprocess.run(['make', '-s', '-C', str(ROOT / 'scripts/twin'),
                                'BUILD=' + str(output),
                                'ARDUINO_FFT_SRC=' + tmp, *options, str(target)],
                               check=True, capture_output=True, text=True)
                return target.read_bytes(), target.stat().st_mtime_ns

            standard, standard_at = signature('MICROPHONE=-DMicrophone')
            same, same_at = signature('MICROPHONE=-DMicrophone')
            self.assertEqual((standard, standard_at), (same, same_at))
            muted, muted_at = signature('MICROPHONE=')
            self.assertNotEqual(standard, muted)
            self.assertIn(b'-DMicrophone', standard)
            self.assertNotIn(b'-DMicrophone', muted)
            restored, _ = signature('MICROPHONE=-DMicrophone')
            self.assertEqual(restored, standard)
            self.assertGreaterEqual(muted_at, standard_at)
            changed, _ = signature('MICROPHONE=-DMicrophone',
                                   'EXTRA_FLAGS=-DPROTOGEN_AUDIO_V2=1')
            self.assertNotEqual(changed, standard)


if __name__ == '__main__':
    unittest.main()
