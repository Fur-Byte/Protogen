#!/usr/bin/env python3
"""Verify the FFT exception cannot weaken first-party warning checks."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class CompilerIsolation(unittest.TestCase):
    def test_dependency_warning_and_first_party_error(self):
        compiler = os.environ.get('CXX', 'clang++')
        with tempfile.TemporaryDirectory() as tmp:
            directory = Path(tmp)
            dependency = directory / 'arduinoFFT.cpp'
            dependency.write_text('#include <cstdlib>\nfloat unusedFFT(float x) {return abs(x);}\n')
            first_party = directory / 'first.cpp'
            first_party.write_text('int firstParty() { int unused; return 0; }\n')
            script = '''set -euo pipefail
source "$1/scripts/host-fft.sh"
FLAGS=(-std=c++17 -Wall -Wextra -Werror)
compile_host_fft "$2" "$2/fft.o"
"${CXX:-clang++}" "${FLAGS[@]}" -c "$2/first.cpp" -o "$2/first.o"
'''
            result = subprocess.run(['bash', '-c', script, 'isolation', str(ROOT), str(directory)],
                                    env=dict(os.environ, CXX=compiler), text=True, capture_output=True)
            self.assertTrue((directory / 'fft.o').is_file(), result.stderr)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn('unused', result.stderr)
            self.assertFalse((directory / 'first.o').exists())
            self.assertEqual(dependency.read_text(), '#include <cstdlib>\nfloat unusedFFT(float x) {return abs(x);}\n')


if __name__ == '__main__':
    unittest.main()
