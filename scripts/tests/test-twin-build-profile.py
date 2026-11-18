#!/usr/bin/env python3
"""Ensure one host output directory cannot silently reuse a different profile."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class TwinCompileProfile(unittest.TestCase):
    def test_default_goal_rebuilds_the_executable_when_profile_changes(self):
        with tempfile.TemporaryDirectory(prefix='protogen-twin-binary-') as tmp:
            program = Path(tmp) / 'twin'

            def build_and_observe(audio_v2):
                subprocess.run(['make', '-s', '-C', str(ROOT / 'scripts/twin'),
                                f'BUILD={tmp}', f'EXTRA_FLAGS=-DPROTOGEN_AUDIO_V2={audio_v2}'],
                               check=True)
                result = subprocess.run([str(program), '--soak'], check=True,
                                        input='0 3000 1000 7 1 0 - 0 0\n',
                                        capture_output=True, text=True)
                fields = next(row.split('\t') for row in result.stdout.splitlines()
                              if '\tsoak\t' in row)
                return bool(int(fields[4]) & (1 << 3)), program.stat().st_mtime_ns

            enabled, built_at = build_and_observe(1)
            self.assertTrue(enabled)
            self.assertEqual(build_and_observe(1), (enabled, built_at))
            self.assertFalse(build_and_observe(0)[0])
            self.assertTrue(build_and_observe(1)[0])

if __name__ == '__main__':
    unittest.main()
