#!/usr/bin/env python3
import hashlib
import json
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from qualification import GATES, load_manifest, readiness, validate

SOURCE = "1" * 40


class Qualification(unittest.TestCase):
    def setUp(self):
        folder = tempfile.TemporaryDirectory()
        self.addCleanup(folder.cleanup)
        self.root = Path(folder.name)
        self.artifact = self.root / "proof.txt"
        self.artifact.write_bytes(b"fixture evidence\n")
        self.artifact_id = {"path": "proof.txt", "sha256": hashlib.sha256(self.artifact.read_bytes()).hexdigest(),
                            "size_bytes": self.artifact.stat().st_size}

    def record(self, requirement="host.integration", level="F2", result="PASS"):
        return {"requirement_id": requirement, "source_sha": SOURCE,
                "feature_flags": {"PROTOGEN_AUDIO_V2": 0}, "toolchain": {"python": "3.13.2"},
                "command": ["fixture-runner"], "result": result, "evidence_level": level,
                "artifact": self.artifact_id.copy() if result == "PASS" else None,
                "environment": {"kind": "physical" if level == "F3" else "host", "description": "test fixture"},
                "hardware": None, "limitations": [], "outstanding_gates": []}

    def manifest(self, *records):
        return {"schema_version": 1, "source_sha": SOURCE, "records": records and list(records) or []}

    def test_hash_and_source(self):
        document = self.manifest(self.record())
        validate(document, self.root, SOURCE)
        self.artifact.write_bytes(b"changed")
        with self.assertRaisesRegex(ValueError, "artifact identity"):
            validate(document, self.root, SOURCE)
        with self.assertRaisesRegex(ValueError, "expected source"):
            validate(document, self.root, "2" * 40)

    def test_invalid_shapes(self):
        for name, value in (("result", "SKIPPED"), ("evidence_level", "F4"), ("feature_flags", {"X": True}),
                            ("unknown", "value")):
            with self.subTest(field=name):
                record = self.record()
                record[name] = value
                with self.assertRaises(ValueError):
                    validate(self.manifest(record), self.root, SOURCE)
        wrong_environment = self.record()
        wrong_environment["environment"]["kind"] = "static"
        with self.assertRaisesRegex(ValueError, "host environment"):
            validate(self.manifest(wrong_environment), self.root, SOURCE)
        with self.assertRaisesRegex(ValueError, "duplicate requirement"):
            validate(self.manifest(self.record(), self.record()), self.root, SOURCE)
        with self.assertRaisesRegex(ValueError, "artifact required"):
            record = self.record()
            record["artifact"] = None
            validate(self.manifest(record), self.root, SOURCE)

    def test_unsafe_artifacts_and_duplicate_json(self):
        doc = self.manifest(self.record())
        for path in ("../proof.txt", "/proof.txt", "foo/../proof.txt", "foo//proof.txt", "foo\\proof.txt"):
            doc["records"][0]["artifact"]["path"] = path
            with self.subTest(path=path), self.assertRaisesRegex(ValueError, "unsafe artifact"):
                validate(doc, self.root, SOURCE)
        outside = tempfile.NamedTemporaryFile()
        self.addCleanup(outside.close)
        (self.root / "outside").symlink_to(outside.name)
        doc["records"][0]["artifact"]["path"] = "outside"
        with self.assertRaisesRegex(ValueError, "unsafe artifact"):
            validate(doc, self.root, SOURCE)
        manifest = self.root / "manifest.json"
        manifest.write_text('{"schema_version":1,"schema_version":2}')
        with self.assertRaisesRegex(ValueError, "duplicate JSON"):
            load_manifest(manifest)

    def test_physical_gates_require_physical_evidence(self):
        doc = self.manifest(self.record("hardware.timing"))
        validate(doc, self.root, SOURCE)
        gates = readiness(doc)["gates"]
        self.assertEqual(gates["hardware.timing"]["status"], "UNQUALIFIED")
        self.assertEqual(gates["hardware.audio"]["status"], "NOT_RUN")
        invalid = self.record("hardware.timing", "F3")
        with self.assertRaisesRegex(ValueError, "identified hardware"):
            validate(self.manifest(invalid), self.root, SOURCE)
        invalid["environment"]["kind"] = "host"
        with self.assertRaisesRegex(ValueError, "physical environment"):
            validate(self.manifest(invalid), self.root, SOURCE)

    def test_gated_readiness(self):
        records = []
        for name, minimum in GATES.items():
            record = self.record(name, minimum)
            if minimum == "F3":
                record["hardware"] = {"boards": ["fixture-board"], "firmware_sha256": ["a" * 64],
                                      "instruments": ["fixture-probe"], "operator": "fixture operator",
                                      "date": "2026-11-17", "duration_seconds": 3600,
                                      "conditions": "isolated fixture", "measurements": {"test": 1}}
            records.append(record)
        complete = self.manifest(*records)
        validate(complete, self.root, SOURCE)
        self.assertEqual(readiness(complete)["verdict"], "GATES_SATISFIED")
        complete["records"][-1]["outstanding_gates"] = ["separate approval"]
        self.assertEqual(readiness(complete)["verdict"], "PRODUCTION_BLOCKED")


if __name__ == "__main__":
    unittest.main()
