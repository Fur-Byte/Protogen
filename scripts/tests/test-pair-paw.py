#!/usr/bin/env python3
"""The retired USB provisioner must never connect or generate plaintext keys."""
from pathlib import Path
import subprocess
import sys
import unittest
ROOT=Path(__file__).resolve().parents[2]
class PairingEntryPoint(unittest.TestCase):
    def test_wireless_pairing_guidance_without_usb_access(self):
        run=subprocess.run([sys.executable,str(ROOT/'scripts/pair-paw.py'),
                            '--helmet','/nonexistent','--paw','/nonexistent'],capture_output=True,text=True)
        self.assertEqual(run.returncode,2)
        self.assertIn('entirely wireless', (ROOT/'scripts/pair-paw.py').read_text())
        self.assertIn('compare all six LED digits',run.stderr)
        self.assertNotIn('Traceback',run.stderr)
if __name__=='__main__':unittest.main()
