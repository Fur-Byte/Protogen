#!/usr/bin/env python3
import importlib.util
import json
import csv
import copy
from pathlib import Path
import tempfile
import unittest
import sys

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location("analysis", ROOT / "scripts/analyze-efficiency.py")
analysis = importlib.util.module_from_spec(spec)
spec.loader.exec_module(analysis)
sys.path.insert(0, str(ROOT / "scripts"))
from maintenance_support import validate_profile

CHECKS = (
    "artwork_and_timing_equivalent",
    "microphone_timing_equivalent",
    "radio_reliability_preserved",
    "security_preserved",
    "sleep_wake_rollover_reconnect_pass",
    "packet_loss_storage_interruption_pass",
    "no_missed_command_deadlines",
    "no_memory_growth",
    "no_watchdog_resets",
    "no_audio_overruns",
)


class QualificationTests(unittest.TestCase):
    def test_secret_free_profiles(self):
        original = json.loads((ROOT / "profiles/everyday.json").read_text())
        self.assertEqual(validate_profile(original)["config"], original["config"])
        original["psk"] = "secret"
        with self.assertRaises(ValueError):
            validate_profile(original)
        del original["psk"]
        original["config"]["paired"] = 1
        with self.assertRaises(ValueError):
            validate_profile(original)

    def fixture(
        self,
        folder,
        candidate=False,
        reaction=90000,
        complete=230000,
        hold=30000,
        action="navigation",
        edge="release",
        trial=1,
        start=0,
    ):
        # Synthetic parser fixtures never ship as qualification records.
        run = {key: "fixture" for key in analysis.MATCH}
        run.update(
            measurement="physical",
            latency_schema=2,
            capture_method="photodiode",
            teensy_mhz=600,
            trial=trial,
            uncertainty_confidence=0.95,
            energy_uncertainty_j=2,
            flags=1 if candidate else 0,
            optimization="cache_face" if candidate else "baseline",
            checks={k: True for k in CHECKS},
            energy_csv="energy.csv",
            latency_csv="latency.csv",
        )
        folder.mkdir(exist_ok=True)
        (folder / "run.json").write_text(json.dumps(run))
        (folder / "energy.csv").write_text(
            f"time_s,helmet_j,paw_j\n{start},0,0\n{start + 600}," + ("500" if candidate else "600") + ",60\n"
        )
        with (folder / "latency.csv").open("w", newline="") as stream:
            writer = csv.DictWriter(stream, fieldnames=analysis.LATENCY_FIELDS)
            writer.writeheader()
            for group in ("online", "idle_wake"):
                for _ in range(1000):
                    writer.writerow(
                        dict(
                            condition=group,
                            action=action,
                            trigger_edge=edge,
                            press_us=start,
                            action_edge_us=start + hold,
                            first_reaction_us=start + hold + reaction if reaction is not None else "",
                            complete_expression_us=start + hold + complete if complete is not None else "",
                        )
                    )
        return folder / "run.json"

    def test_whole_system_and_latency(self):
        with tempfile.TemporaryDirectory() as tmp:
            base = Path(tmp)
            baselines, candidates = [], []
            for trial in range(3):
                for candidate, destination in ((False, baselines), (True, candidates)):
                    path = self.fixture(
                        base / f"{trial}-{candidate}",
                        candidate,
                        trial=trial,
                        start=trial * 2 + int(candidate),
                    )
                    destination.append(analysis.load_run(path))
            before = baselines[0]
            self.assertEqual(before[1]["whole_system_j"], 660)
            self.assertEqual(before[1]["latency"]["idle_wake"]["press_to_complete"]["p99_ms"], 260)
            self.assertEqual(before[1]["latency"]["online"]["action_to_first_reaction"]["p99_ms"], 90)
            report = analysis.compare(baselines, candidates)
            self.assertTrue(report["candidate_passes_recorded_checks"])
            self.assertFalse(report["hardware_qualified"])
            self.assertAlmostEqual(report["mean_reduction_percent"], 100 * 100 / 660)
            # Different metadata and filenames cannot turn a copied capture into another trial.
            for field in ("energy_csv", "latency_csv"):
                run, _ = candidates[-1]
                folder = base / "2-True"
                capture = folder / run[field]
                original = capture.read_bytes()
                capture.write_bytes((base / "0-False" / run[field]).read_bytes())
                duplicate = analysis.load_run(folder / "run.json")
                with self.subTest(capture=field), self.assertRaisesRegex(ValueError, "Reused " + field):
                    analysis.compare(baselines, [*candidates[:-1], duplicate])
                capture.write_bytes(original)
            candidates[0][1]["uncertainty_watts"] = 1
            self.assertFalse(analysis.compare(baselines, candidates)["candidate_passes_recorded_checks"])
            candidates[0][1]["uncertainty_watts"] = 2 / 600
            candidates[0][0]["flags"] = 3
            with self.assertRaises(ValueError):
                analysis.compare(baselines, candidates)
            for run, _ in candidates:
                run.update(
                    flags=0, optimization="teensy_clock", teensy_mhz=528, steady_cpu_headroom_percent=60
                )
            self.assertTrue(analysis.compare(baselines, candidates)["candidate_passes_recorded_checks"])
            original_reports = copy.deepcopy(candidates)
            candidates[0][0]["steady_cpu_headroom_percent"] = 49
            self.assertFalse(analysis.compare(baselines, candidates)["candidate_passes_recorded_checks"])
            candidates[0][0]["steady_cpu_headroom_percent"] = 60
            self.assertEqual(candidates, original_reports)
            self.assertTrue(analysis.compare(baselines, candidates)["candidate_passes_recorded_checks"])
            candidates[0][0]["teensy_mhz"] = 450
            with self.assertRaises(ValueError):
                analysis.compare(baselines, candidates)
            for run, _ in candidates:
                run["teensy_mhz"] = 720
            with self.assertRaises(ValueError):
                analysis.compare(baselines, candidates)

    def test_reaction_and_complete_intervals(self):
        with tempfile.TemporaryDirectory() as tmp:
            folder = Path(tmp)
            _, report = analysis.load_run(self.fixture(folder))
            self.assertTrue(report["latency_pass"])
            self.assertEqual(report["latency"]["online"]["action_to_complete"]["p99_ms"], 230)
            for metric in ("press_to_complete", "action_to_first_reaction", "action_to_complete"):
                self.assertEqual(
                    set(report["latency"]["online"][metric]),
                    {"samples", "p50_ms", "p95_ms", "p99_ms", "max_ms"},
                )
            _, report = analysis.load_run(self.fixture(folder, reaction=110000))
            self.assertFalse(report["latency_pass"])
            _, report = analysis.load_run(self.fixture(folder, reaction=100000))
            self.assertFalse(report["latency_pass"])
            _, report = analysis.load_run(self.fixture(folder, hold=2000000))
            self.assertTrue(report["latency_pass"])
            self.assertEqual(report["latency"]["online"]["press_to_complete"]["p99_ms"], 2230)
            self.assertEqual(report["latency"]["online"]["action_to_first_reaction"]["p99_ms"], 90)

    def test_missing_reactions_and_completed_expressions(self):
        with tempfile.TemporaryDirectory() as tmp:
            folder = Path(tmp)
            for reaction, complete, missed_first, missed_complete in (
                (None, 230000, 2000, 0),
                (90000, None, 0, 2000),
                (None, None, 2000, 2000),
            ):
                with self.subTest(reaction=reaction, complete=complete):
                    _, report = analysis.load_run(self.fixture(folder, reaction=reaction, complete=complete))
                    self.assertFalse(report["latency_pass"])
                    self.assertEqual(report["missed_reactions"], missed_first)
                    self.assertEqual(report["missed_complete_expressions"], missed_complete)
                    self.assertEqual(report["latency"]["online"]["attempts"], 1000)
            path = self.fixture(folder)
            with (folder / "latency.csv").open("a") as stream:
                stream.write("idle_wake,navigation,release,0,30000,,\n")
            _, report = analysis.load_run(path)
            self.assertFalse(report["latency_pass"])
            self.assertEqual(report["latency"]["idle_wake"]["attempts"], 1001)
            self.assertEqual(report["missed_reactions"], 1)
            self.assertEqual(report["missed_complete_expressions"], 1)

    def row(self, **updates):
        row = dict(
            condition="online",
            action="navigation",
            trigger_edge="release",
            press_us="0",
            action_edge_us="30000",
            first_reaction_us="120000",
            complete_expression_us="260000",
        )
        row.update(updates)
        return row

    def test_action_classification(self):
        rows = [
            self.row(),
            self.row(action="boop", trigger_edge="press", action_edge_us="0"),
            self.row(condition="idle_wake", action="wake", trigger_edge="press", action_edge_us="0"),
            self.row(condition="idle_wake", action="wake"),
        ]
        report, passed = analysis.analyze_latency(rows)
        self.assertFalse(passed)  # Insufficient samples, even with quick responses.
        self.assertEqual(report["online"]["classifications"], {"navigation:release": 1, "boop:press": 1})
        self.assertEqual(report["idle_wake"]["classifications"], {"wake:press": 1, "wake:release": 1})
        for updates in (
            {"trigger_edge": "press"},
            {"action": "boop"},
            {"action": "wake"},
            {"action": "unknown"},
            {"condition": "offline"},
            {"trigger_edge": "hold"},
        ):
            with self.subTest(updates=updates), self.assertRaises(ValueError):
                analysis.analyze_latency([self.row(**updates)])

    def test_chronology_and_malformed_timestamps(self):
        for field in ("press_us", "action_edge_us", "first_reaction_us", "complete_expression_us"):
            for value in ("nan", "inf", "-1", "abc", "1e1000", None, " 1", "1_000"):
                with self.subTest(field=field, value=value), self.assertRaises(ValueError):
                    analysis.analyze_latency([self.row(**{field: value})])
        for field in ("press_us", "action_edge_us"):
            with self.assertRaises(ValueError):
                analysis.analyze_latency([self.row(**{field: ""})])
        for updates in (
            {"press_us": "40000"},
            {"action_edge_us": "0"},
            {"first_reaction_us": "20000"},
            {"complete_expression_us": "20000"},
            {"complete_expression_us": "100000"},
            {"action": "boop", "trigger_edge": "press"},
        ):
            with self.subTest(updates=updates), self.assertRaises(ValueError):
                analysis.analyze_latency([self.row(**updates)])
        for field in analysis.LATENCY_FIELDS:
            row = self.row()
            del row[field]
            with self.assertRaises(ValueError):
                analysis.analyze_latency([row])
        # Large instrument epochs must retain small, contradictory deltas.
        with self.assertRaises(ValueError):
            analysis.analyze_latency(
                [
                    self.row(
                        press_us="10000000000000000",
                        action_edge_us="10000000000000030",
                        first_reaction_us="10000000000000029",
                        complete_expression_us="10000000000230000",
                    )
                ]
            )

    def test_csv_fields_old_schema_and_sample_size(self):
        with tempfile.TemporaryDirectory() as tmp:
            folder = Path(tmp)
            path = self.fixture(folder)
            capture = folder / "latency.csv"
            valid = capture.read_text()
            for content in (
                "condition,press_us,action_edge_us,visible_expression_us\nonline,0,30000,260000\n",
                valid.replace("first_reaction_us,", ""),
                valid + "online,navigation,release,0,30000,120000\n",
                valid + "online,navigation,release,0,30000,120000,260000,extra\n",
                valid.replace("first_reaction_us", "complete_expression_us"),
            ):
                capture.write_text(content)
                with self.assertRaises(ValueError):
                    analysis.load_run(path)
            capture.write_text("\n".join(valid.splitlines()[:-1]) + "\n")
            _, report = analysis.load_run(path)
            self.assertFalse(report["latency_pass"])
            self.assertEqual(report["latency"]["idle_wake"]["attempts"], 999)

    def test_nearest_rank_tail_and_missing_samples(self):
        self.assertEqual(analysis.percentile(list(range(1, 1001)), 0.99), 990)
        rows = [self.row(condition=group) for group in ("online", "idle_wake") for _ in range(1000)]
        # Eleven slow samples put p99 over the limit; ten occupy the upper 1%.
        for index in range(10):
            rows[index]["first_reaction_us"] = "140000"
        self.assertTrue(analysis.analyze_latency(rows)[1])
        rows[10]["first_reaction_us"] = "140000"
        self.assertFalse(analysis.analyze_latency(rows)[1])
        rows[10]["first_reaction_us"] = ""
        self.assertFalse(analysis.analyze_latency(rows)[1])

    def test_invalid_energy_metadata_and_checks(self):
        with tempfile.TemporaryDirectory() as tmp:
            folder = Path(tmp)
            path = self.fixture(folder)
            for trace in (
                "0,0,0\n600,nan,60",
                "0,0,0\n600,-1,60",
                "0,0,0\n599,600,60",
                "0,0,0\n0,600,60",
                "0,600,0\n600,500,60",
                "0,0,0\n600,0,0",
                "0,0\n600,600,60",
            ):
                (folder / "energy.csv").write_text("time_s,helmet_j,paw_j\n" + trace + "\n")
                with self.subTest(trace=trace), self.assertRaises(ValueError):
                    analysis.load_run(path)
            self.fixture(folder)
            original = json.loads(path.read_text())
            for updates in (
                {"measurement": "synthetic"},
                {"measurement": "simulated"},
                {"capture_method": "spi"},
                {"latency_schema": 1},
                {"energy_uncertainty_j": 0},
                {"uncertainty_confidence": 0.9},
                {"flags": True},
                {"trial": True},
                {"energy_csv": "../energy.csv"},
            ):
                path.write_text(json.dumps(dict(original, **updates)))
                with self.subTest(updates=updates), self.assertRaises(ValueError):
                    analysis.load_run(path)
            for key in (
                "latency_schema",
                "capture_method",
                "energy_csv",
                "latency_csv",
                "energy_uncertainty_j",
            ):
                run = dict(original)
                del run[key]
                path.write_text(json.dumps(run))
                with self.assertRaises(ValueError):
                    analysis.load_run(path)
            run = copy.deepcopy(original)
            run["checks"]["no_audio_overruns"] = "yes"
            path.write_text(json.dumps(run))
            with self.assertRaises(ValueError):
                analysis.load_run(path)


if __name__ == "__main__":
    unittest.main()
