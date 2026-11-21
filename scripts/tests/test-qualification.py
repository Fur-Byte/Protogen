#!/usr/bin/env python3
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from qualification import GATES, ROOT, load_manifest, readiness, validate

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

    def candidate(self):
        return {"source_sha": SOURCE, "assembly_id": "helm-001",
                "boards": {"teensy": "teensy-01", "helmet": "c3-01"},
                "firmware_sha256": {"teensy": "a" * 64, "helmet": "b" * 64},
                "feature_flags": {"PROTOGEN_AUDIO_V2": 0}}

    def physical(self, name, instrument="fixture-probe"):
        item = self.record(name, "F3")
        candidate = self.candidate()
        item["hardware"] = {key: candidate[key] for key in ("assembly_id", "boards", "firmware_sha256")}
        item["hardware"].update(instruments=[instrument], operator="bench operator", date="2026-11-18",
                                duration_seconds=3600, conditions="controlled bench", measurements={"value": 1})
        return item

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

    def soak_artifact(self, aggregate_overrides=None, **overrides):
        duration = 86_400_000_000
        artifact = self.root / "soak.json"
        values = {"passed_seed_count": 100, "passed_seed_ids": list(range(100)),
                  "passed_virtual_duration_us": 100 * duration, "failed_seeds": [], "incomplete_seeds": [],
                  "missing_seeds": [], "duplicate_seeds": [], "unexpected_seeds": []}
        artifact.write_text(json.dumps({"version": 1, "evidence_level": "F2", "status": "PASS",
            "architecture_target_met": True, "errors": [], "unfinished_shards": [],
            "shards": [{"path": "sample/shard.json", "sha256": "d" * 64, "status": "PASS"}],
            "identity": {"source_sha": SOURCE, "source_dirty": False,
                         "program_sha256": "b" * 64, "runner_sha256": "c" * 64},
            "request": {"seed_start": 0, "seed_count": 100, "virtual_duration_us": duration},
            "coverage": {**values, **overrides}, **(aggregate_overrides or {})}))
        return {"path": "soak.json", "sha256": hashlib.sha256(artifact.read_bytes()).hexdigest(),
                "size_bytes": artifact.stat().st_size}

    def test_partial_soak_cannot_unlock_release_gate(self):
        record = self.record("host.soak-100x24h")
        record["artifact"] = self.soak_artifact(passed_seed_count=1)
        with self.assertRaisesRegex(ValueError, "100 complete full-day seeds"):
            validate(self.manifest(record), self.root, SOURCE)
        record["artifact"] = self.soak_artifact()
        validate(self.manifest(record), self.root, SOURCE)
        for changes in ({"shards": []}, {"unfinished_shards": ["shard-0"]},
                        {"identity": {"source_sha": SOURCE, "source_dirty": False}}):
            record["artifact"] = self.soak_artifact(aggregate_overrides=changes)
            with self.subTest(changes=changes), self.assertRaises(ValueError):
                validate(self.manifest(record), self.root, SOURCE)

    def test_release_soak_requires_retained_shard_files(self):
        record = self.record("host.soak-100x24h")
        record["artifact"] = self.soak_artifact()
        document = self.manifest(record)
        validate(document, self.root, SOURCE)
        with self.assertRaisesRegex(ValueError, "missing.*shard"):
            validate(document, self.root, SOURCE, require_soak_shards=True)

    def test_release_soak_recomputes_coverage_and_identity(self):
        from twin import soak

        source = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip()
        runner = b"".join(subprocess.check_output(["git", "show", f"{source}:scripts/twin/{name}"], cwd=ROOT)
                          for name in ("soak.py", "replay.py"))
        identity = {"source_sha": source, "source_dirty": False, "program_sha256": "b" * 64,
                    "runner_sha256": soak.digest(runner)}
        duration = soak.SOAK_DURATION_US

        def seed_record(seed):
            tape = soak.tape(seed, duration // 1000)
            record = {"seed": seed, "status": "PASS", "runtime_seconds": 0,
                      "virtual_duration_us": duration, "input_sha256": soak.digest(soak.encode(tape, soak=True).encode()),
                      "invariant_counts": {**soak.expected_checks(tape), **dict.fromkeys(soak.CONTINUOUS_CHECKS, 1)},
                      "event_counts": {**dict.fromkeys(soak.COUNTS, 0), "inputs_processed": len(tape["events"])},
                      "queue_peaks": {name: soak.QUEUE_LIMITS.get(name, 0) for name in soak.PEAKS},
                      "drop_and_rejection_counts": dict.fromkeys(soak.DROPS, 1),
                      "feature_flags": dict.fromkeys(soak.FEATURES, False),
                      "eeprom_sha256": "e" * 64, "terminal_state_sha256": "f" * 64}
            record["semantic_sha256"] = soak.semantic_hash(record)
            return record

        def write_shard(name, start, count, shard_identity=identity):
            path = self.root / name
            document = {"version": 1, "evidence_level": "F2", "identity": shard_identity, "status": "PASS",
                        "request": {"seed_start": start, "seed_count": count, "virtual_duration_us": duration},
                        "seeds": [seed_record(seed) for seed in range(start, start + count)]}
            raw = json.dumps(document).encode()
            path.write_bytes(raw)
            return {"path": name, "sha256": soak.digest(raw), "status": "PASS"}

        def verify(shards):
            record = self.record("host.soak-100x24h")
            record["source_sha"] = source
            record["artifact"] = self.soak_artifact({"identity": identity, "shards": shards})
            document = self.manifest(record)
            document["source_sha"] = source
            validate(document, self.root, source, require_soak_shards=True)

        head = write_shard("first.json", 0, 50)
        tail = write_shard("last.json", 50, 50)
        verify([head, tail])
        with self.assertRaisesRegex(ValueError, "aggregate coverage"):
            verify([head])
        with self.assertRaisesRegex(ValueError, "identity differs"):
            foreign = write_shard("foreign.json", 50, 1, {**identity, "runner_sha256": "0" * 64})
            verify([foreign])
        (self.root / "last.json").write_text("{}")
        with self.assertRaisesRegex(ValueError, "digest mismatch"):
            verify([tail])

    def test_mixed_physical_candidates_are_rejected(self):
        optical = self.physical("hardware.optical", "photodiode-01")
        electrical = self.physical("hardware.electrical", "meter-02")
        validate(self.manifest(optical, electrical), self.root, SOURCE)
        for change in (
            lambda r: r["hardware"].update(assembly_id="helm-002"),
            lambda r: r["hardware"]["boards"].update(helmet="different-c3"),
            lambda r: r["hardware"]["boards"].update(helmet="teensy-01"),
            lambda r: r["hardware"]["firmware_sha256"].update(teensy="b" * 64, helmet="a" * 64),
            lambda r: r["hardware"]["firmware_sha256"].pop("teensy"),
            lambda r: r["feature_flags"].update(PROTOGEN_AUDIO_V2=1)
        ):
            other = self.physical("hardware.electrical", "meter-02")
            change(other)
            with self.assertRaisesRegex(ValueError, "physical candidate"):
                validate(self.manifest(optical, other), self.root, SOURCE)

    def test_legacy_pairing_and_placeholder(self):
        a, b = self.physical("hardware.optical"), self.physical("hardware.timing")
        for record in (a, b):
            record["hardware"].update(boards=["t-1", "c-1"], firmware_sha256=["a" * 64, "b" * 64])
        b["hardware"]["boards"].reverse()
        b["hardware"]["firmware_sha256"].reverse()
        document = self.manifest(a, b)
        validate(document, self.root, SOURCE)
        self.assertEqual(readiness(document)["gates"]["hardware.timing"]["status"], "UNQUALIFIED")
        with self.assertRaisesRegex(ValueError, "named release candidate"):
            readiness(document, self.candidate())
        b["hardware"]["firmware_sha256"].reverse()
        with self.assertRaisesRegex(ValueError, "physical candidate"):
            validate(document, self.root, SOURCE)
        for placeholder in (" UNKNOWN ", "  ", "N/A"):
            a["hardware"]["assembly_id"] = placeholder
            with self.subTest(placeholder=placeholder), self.assertRaisesRegex(ValueError, "identified hardware"):
                validate(self.manifest(a), self.root, SOURCE)

    def test_verify_and_merge_independent_candidate(self):
        paths = []
        for name in ("hardware.optical", "hardware.timing"):
            path = self.root / f"{name}.json"
            path.write_text(json.dumps(self.manifest(self.physical(name))))
            paths.append(path)
        expected = self.root / "candidate.json"
        merged = self.root / "merged.json"
        candidate = self.candidate()
        candidate["firmware_sha256"]["helmet"] = "c" * 64
        expected.write_text(json.dumps(candidate))
        common = ["--candidate", str(expected), "--source", SOURCE, "--artifact-root", str(self.root)]
        script = [sys.executable, str(ROOT / "scripts/qualification.py")]
        merge = [*script, "merge", *map(str, paths), *common, "--output", str(merged)]
        failure = subprocess.run(merge, capture_output=True, text=True)
        self.assertNotEqual(failure.returncode, 0)
        self.assertIn("named release candidate", failure.stderr)
        self.assertFalse(merged.exists())
        expected.write_text(json.dumps(self.candidate()))
        output = subprocess.check_output(merge, text=True)
        self.assertTrue(json.loads(output)["physical_candidate_matched"])
        verified = subprocess.check_output([*script, "verify", str(merged), *common], text=True)
        self.assertEqual(json.loads(verified), json.loads(output))
        self.assertEqual(json.loads(verified)["verdict"], "PRODUCTION_BLOCKED")

    def test_gated_readiness(self):
        records = []
        for name, minimum in GATES.items():
            record = self.record(name, minimum)
            if name == "host.soak-100x24h":
                record["artifact"] = self.soak_artifact()
            if minimum == "F3":
                record = self.physical(name)
            records.append(record)
        complete = self.manifest(*records)
        validate(complete, self.root, SOURCE)
        self.assertEqual(readiness(complete)["verdict"], "PRODUCTION_BLOCKED")
        self.assertEqual(readiness(complete, self.candidate())["verdict"], "GATES_SATISFIED")
        complete["records"][-1]["outstanding_gates"] = ["separate approval"]
        self.assertEqual(readiness(complete, self.candidate())["verdict"], "PRODUCTION_BLOCKED")


if __name__ == "__main__":
    unittest.main()
