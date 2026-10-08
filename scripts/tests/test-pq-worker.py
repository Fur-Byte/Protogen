#!/usr/bin/env python3
"""Check qualification control flow with synthetic public buffers, including -O.

These doubles test the checker, not ML-KEM correctness or target performance.
"""
import ctypes
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import Mock, patch

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location("pq_worker", ROOT / "scripts/check-pq-worker.py")
worker = importlib.util.module_from_spec(spec)
spec.loader.exec_module(worker)

PK, SK, CT, SS = (bytes([value]) * size for value, size in
                  ((0x11, 1568), (0x22, 3168), (0x33, 1568), (0x44, 32)))


class NativeDouble:
    def __init__(self):
        self.calls = dict.fromkeys(("keygen", "encaps", "decaps"), 0)
        self.bad_return = None
        self.corrupt = None
        self.fault = 0
        self.ignore_fault = False
        self.qual_keygen = Mock(side_effect=lambda pk, sk, coins:
                                self.operation("keygen", ((pk, PK), (sk, SK))))
        self.qual_enc = Mock(side_effect=lambda ct, ss, pk, message:
                             self.operation("encaps", ((ct, CT), (ss, SS))))
        self.qual_dec = Mock(side_effect=lambda ss, ct, sk:
                             self.operation("decaps", ((ss, SS),)))
        self.qual_peak = Mock(return_value=512)
        self.qual_attempts = Mock(return_value=2)
        self.qual_fail_at = Mock(side_effect=self.set_fault)

    def operation(self, name, outputs):
        self.calls[name] += 1
        if self.bad_return == (name, self.calls[name]):
            return -1
        if self.fault and not self.ignore_fault:
            return -2
        for target, value in outputs:
            ctypes.memmove(target, value, len(value))
        if name == self.corrupt:
            outputs[0][0][0] ^= 1
        return 0

    def set_fault(self, position):
        self.fault = position


def write_vectors(root, count=35):
    directory = root / "vectors" / "synthetic"
    directory.mkdir(parents=True)
    groups, expected = [], []
    for index, function in enumerate(("keyGen", "encapsulation", "decapsulation")):
        cases, results = [], []
        for case_id in range(count):
            case = {"tcId": case_id}
            result = {"tcId": case_id}
            if function == "keyGen":
                case.update(d="00" * 32, z="01" * 32)
                result.update(ek=PK.hex().upper(), dk=SK.hex().upper())
            elif function == "encapsulation":
                case.update(ek=PK.hex().upper(), m="00" * 32)
                result.update(c=CT.hex().upper(), k=SS.hex().upper())
            else:
                case.update(c=CT.hex().upper())
                if case_id == 0:
                    case["dk"] = SK.hex().upper()
                else:
                    case.update(d="00" * 32, z="01" * 32)
                result["k"] = SS.hex().upper()
            cases.append(case)
            results.append(result)
        groups.append({"tgId": index, "parameterSet": "ML-KEM-1024",
                       "function": function, "tests": cases})
        expected.append({"tgId": index, "tests": results})
    (directory / "prompt.json").write_text(json.dumps({"testGroups": groups}))
    (directory / "expectedResults.json").write_text(json.dumps({"testGroups": expected}))


class WorkerChecks(unittest.TestCase):
    def vectors(self, api, count=35):
        with tempfile.TemporaryDirectory(prefix="protogen-pq-checker-") as temporary:
            root = Path(temporary)
            write_vectors(root, count)
            with patch.object(worker.pre, "BUILD", root), patch.object(worker.ctypes, "CDLL", return_value=api):
                return worker.direct_vectors("synthetic.so")

    def test_vectors_and_benchmarks_actually_call_native_functions(self):
        api = NativeDouble()
        returned, record = self.vectors(api)
        self.assertIs(returned, api)
        self.assertEqual(record["vectors"], {"keygen": 35, "encaps": 35, "decaps": 35})
        self.assertEqual(record["vectors_total"], 105)
        self.assertEqual(api.calls, {"keygen": 269, "encaps": 235, "decaps": 235})
        self.assertTrue(all(timing["iterations"] == 200 for timing in record["host_ms"].values()))

    def test_vector_mismatches_are_rejected(self):
        for operation in ("keygen", "encaps", "decaps"):
            with self.subTest(operation=operation):
                api = NativeDouble()
                api.corrupt = operation
                with self.assertRaisesRegex(RuntimeError, "vector mismatch"):
                    self.vectors(api)

    def test_native_vector_errors_are_rejected(self):
        for operation, call in (("keygen", 1), ("encaps", 1), ("decaps", 1), ("keygen", 36)):
            with self.subTest(operation=operation, call=call):
                api = NativeDouble()
                api.bad_return = (operation, call)
                with self.assertRaisesRegex(RuntimeError, "failed"):
                    self.vectors(api)

    def test_reduced_vector_set_is_rejected(self):
        with self.assertRaisesRegex(RuntimeError, "Expected 105 vectors; checked 102"):
            self.vectors(NativeDouble(), count=34)

    def test_native_benchmark_errors_are_rejected(self):
        for operation, call in (("keygen", 70), ("encaps", 36), ("decaps", 36)):
            with self.subTest(operation=operation):
                api = NativeDouble()
                api.bad_return = (operation, call)
                with self.assertRaisesRegex(RuntimeError, "Benchmark operation failed"):
                    self.vectors(api)

    def test_allocation_faults_and_recovery_actually_execute(self):
        api = NativeDouble()
        result = worker.allocation_faults(api)
        self.assertEqual(result, {"peak_bytes": dict.fromkeys(api.calls, 512),
                                  "allocation_fault_cases": 6})
        self.assertEqual(api.calls, {"keygen": 4, "encaps": 5, "decaps": 4})
        self.assertEqual([call.args[0] for call in api.qual_fail_at.call_args_list], [1, 2, 0] * 3)

    def test_unreported_allocation_fault_is_rejected(self):
        api = NativeDouble()
        api.ignore_fault = True
        with self.assertRaisesRegex(RuntimeError, "allocation fault 1 was not reported"):
            worker.allocation_faults(api)

    def test_arena_baseline_and_recovery_errors_are_rejected(self):
        for operation, call in (("keygen", 1), ("keygen", 4), ("encaps", 1),
                                ("encaps", 2), ("encaps", 5), ("decaps", 1), ("decaps", 4)):
            with self.subTest(operation=operation, call=call):
                api = NativeDouble()
                api.bad_return = (operation, call)
                with self.assertRaisesRegex(RuntimeError, "failed"):
                    worker.allocation_faults(api)


if __name__ == "__main__":
    unittest.main()
