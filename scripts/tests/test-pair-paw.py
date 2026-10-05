#!/usr/bin/env python3
import importlib.util
from pathlib import Path
import unittest
root=Path(__file__).resolve().parents[2]
spec=importlib.util.spec_from_file_location("pair_paw",root/"scripts/pair-paw.py")
pair=importlib.util.module_from_spec(spec);spec.loader.exec_module(pair)

class ProvisionChecks(unittest.TestCase):
    def test_unique_key_material_and_wire_bounds(self):
        a,b=pair.credentials(),pair.credentials()
        self.assertNotEqual(a,b)
        command=pair.pair_command("020304050607",a,6)
        self.assertLess(len(command),192)
        self.assertEqual(len(command.split()),6)
        self.assertEqual([len(a[k]) for k in ("psk","lmk","pmk")],[64,32,32])

    def test_role_and_input_rejection(self):
        self.assertEqual(pair.validate_info({"role":1,"mac":"AABBCCDDEEFF"},1),"aabbccddeeff")
        for info in ({"role":2,"mac":"020304050607"},{"role":1,"mac":"bad"}):
            with self.assertRaises(ValueError):pair.validate_info(info,1)
        for peer,channel in (("bad",6),("020304050607",0),("020304050607",12)):
            with self.assertRaises(ValueError):pair.pair_command(peer,pair.credentials(),channel)

if __name__=="__main__":unittest.main()
