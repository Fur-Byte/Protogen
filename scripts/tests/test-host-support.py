#!/usr/bin/env python3
"""Host validation must preserve sanitizers on both Linux and macOS."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from host_support import non_pie_flags, analyzer_compilers

class HostSupport(unittest.TestCase):
    def test_non_pie_is_only_requested_on_linux(self):
        self.assertEqual(non_pie_flags('darwin'), [])
        self.assertEqual(non_pie_flags('linux'), ['-fno-pie', '-no-pie'])
    def test_sanitized_host_binary_runs_with_warnings_as_errors(self):
        with tempfile.TemporaryDirectory() as tmp:
            source = Path(tmp) / 'test.c'; binary = Path(tmp) / 'test'
            source.write_text('int main(void) { return 0; }\n')
            subprocess.run([os.getenv('CC','cc'), '-Wall','-Wextra','-Werror',
                            '-fsanitize=address,undefined', *non_pie_flags(),
                            str(source), '-o', str(binary)], check=True)
            subprocess.run([str(binary)], check=True)
    def test_analyzers_are_installed_executables(self):
        for compiler in analyzer_compilers():
            subprocess.run([compiler, '--version'], check=True, capture_output=True)

if __name__ == '__main__': unittest.main()
