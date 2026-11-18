#!/usr/bin/env python3
"""Check incremental host builds after source and profile changes."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class TwinCompileProfile(unittest.TestCase):
    def test_changed_inputs_in_a_warm_build(self):
        sketch = ROOT / 'firmware/PROT_OS7_MAX7219'
        stubs = ROOT / 'scripts/tests/stubs'
        driver = sketch / 'src/Max72xxPanel/Max72xxPanel.cpp'
        cases = [
            (driver, 'audio-corpus', 1),
            (stubs / 'SPI.h', 'audio-corpus', 1),
            (stubs / 'Arduino.h', 'fft.o', 1),
            (stubs / 'Arduino.h', 'flight-recorder-unit', 1),
            (sketch / 'src/BehaviorCore/Timeline.h', 'behavior-core-unit', 1),
            (sketch / 'Settings.h', 'animation-v2-unit', 1),
            (driver, 'behavior-core-unit', 0),
            (stubs / 'SPI.h', 'fft.o', 0),
        ]
        with tempfile.TemporaryDirectory(prefix='protogen-twin-inputs-') as tmp:
            make = ['make', '-s', '-C', str(ROOT / 'scripts/twin'), f'ROOT={ROOT}',
                    f'BUILD={tmp}', 'EXTRA_FLAGS=-DPROTOGEN_FLIGHT_RECORDER=1']
            targets = sorted({str(Path(tmp) / target) for _, target, _ in cases})
            subprocess.run([*make, *targets], check=True)
            subprocess.run([*make, '-q', *targets], check=True)
            for changed, target, expected in cases:
                with self.subTest(changed=changed.name, target=target):
                    result = subprocess.run([*make, '-q', '-W', str(changed), str(Path(tmp) / target)])
                    self.assertEqual(result.returncode, expected)

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
