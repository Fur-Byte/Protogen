#!/usr/bin/env python3
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
import sys
import zipfile

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('analysis', ROOT / 'scripts/analyze-efficiency.py')
analysis = importlib.util.module_from_spec(spec)
spec.loader.exec_module(analysis)
sys.path.insert(0,str(ROOT / 'scripts'))
from maintenance_support import validate_profile
package_spec = importlib.util.spec_from_file_location('package', ROOT / 'scripts/package-release.py')
package = importlib.util.module_from_spec(package_spec);package_spec.loader.exec_module(package)
CHECKS = ('artwork_and_timing_equivalent', 'microphone_timing_equivalent',
          'radio_reliability_preserved', 'security_preserved', 'sleep_wake_rollover_reconnect_pass',
          'packet_loss_storage_interruption_pass', 'no_missed_command_deadlines',
          'no_memory_growth', 'no_watchdog_resets', 'no_audio_overruns')


class QualificationTests(unittest.TestCase):
    def test_recovery_checksums_and_secret_free_profiles(self):
        original=json.loads((ROOT / 'profiles/everyday.json').read_text())
        self.assertEqual(validate_profile(original)['config'],original['config'])
        original['psk']='secret'
        with self.assertRaises(ValueError):validate_profile(original)
        del original['psk'];original['config']['paired']=1
        with self.assertRaises(ValueError):validate_profile(original)
        with tempfile.TemporaryDirectory() as tmp:
            path=Path(tmp)/'release.zip'
            raw=b'firmware fixture'
            with zipfile.ZipFile(path,'w') as archive:
                archive.writestr('teensy/fixture.hex',raw)
                archive.writestr('release.json',json.dumps({'sha256':{'teensy/fixture.hex':package.digest(raw)}}))
            self.assertEqual(package.previous_release(path),package.digest(path.read_bytes()))
            with zipfile.ZipFile(path,'w') as archive:
                archive.writestr('teensy/fixture.hex',b'corrupted')
                archive.writestr('release.json',json.dumps({'sha256':{'teensy/fixture.hex':package.digest(raw)}}))
            with self.assertRaises(ValueError):package.previous_release(path)

    def fixture(self, folder, candidate=False, latency=90000):
        # Synthetic parser fixtures never ship as qualification records.
        run = {key: 'fixture' for key in analysis.MATCH}
        run.update(measurement='physical', teensy_mhz=600, trial=1,
                   uncertainty_confidence=.95,energy_uncertainty_j=2,
                   flags=1 if candidate else 0, optimization='cache_face' if candidate else 'baseline',
                   checks={k: True for k in CHECKS}, energy_csv='energy.csv', latency_csv='latency.csv')
        folder.mkdir(exist_ok=True)
        (folder / 'run.json').write_text(json.dumps(run))
        (folder / 'energy.csv').write_text('time_s,helmet_j,paw_j\n0,0,0\n600,' +
                                         ('500' if candidate else '600') + ',60\n')
        (folder / 'latency.csv').write_text('condition,press_us,action_edge_us,visible_expression_us\n' +
            ''.join(f'{group},0,30000,{latency}\n' for group in ('online', 'idle_wake') for _ in range(1000)))
        return folder / 'run.json'

    def test_whole_system_and_latency(self):
        with tempfile.TemporaryDirectory() as tmp:
            base = Path(tmp)
            before = analysis.load_run(self.fixture(base / 'a'))
            after = analysis.load_run(self.fixture(base / 'b', True))
            self.assertEqual(before[1]['whole_system_j'], 660)
            self.assertEqual(before[1]['latency']['idle_wake']['physical_press_p99_ms'], 90)
            self.assertEqual(before[1]['latency']['online']['physical_action_edge_p99_ms'], 60)
            baselines, candidates = [], []
            for trial in range(3):
                for source, destination in ((before, baselines), (after, candidates)):
                    run = dict(source[0], trial=trial)
                    destination.append((run, source[1].copy()))
            report = analysis.compare(baselines, candidates)
            self.assertTrue(report['candidate_passes_recorded_checks'])
            self.assertAlmostEqual(report['mean_reduction_percent'], 100 * 100 / 660)
            candidates[0][1]['uncertainty_watts']=1
            self.assertFalse(analysis.compare(baselines,candidates)['candidate_passes_recorded_checks'])
            candidates[0][1]['uncertainty_watts']=2/600
            candidates[0][0]['flags'] = 3
            with self.assertRaises(ValueError): analysis.compare(baselines, candidates)
            for run, _ in candidates:
                run.update(flags=0, optimization='teensy_clock', teensy_mhz=528,
                           steady_cpu_headroom_percent=60)
            self.assertTrue(analysis.compare(baselines, candidates)['candidate_passes_recorded_checks'])
            candidates[0][0]['teensy_mhz']=450
            with self.assertRaises(ValueError): analysis.compare(baselines, candidates)
            for run, _ in candidates:run['teensy_mhz']=720
            with self.assertRaises(ValueError): analysis.compare(baselines, candidates)

    def test_physical_hold_is_included(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = self.fixture(Path(tmp), latency=110000)
            run, report = analysis.load_run(path)
            self.assertFalse(report['latency_pass'])
            self.assertEqual(report['latency']['online']['physical_action_edge_p99_ms'], 80)
            run['measurement'] = 'simulated'
            path.write_text(json.dumps(run))
            with self.assertRaises(ValueError): analysis.load_run(path)

    def test_missing_responses_invalid_energy_and_checks(self):
        with tempfile.TemporaryDirectory() as tmp:
            folder = Path(tmp);path = self.fixture(folder)
            capture = folder / 'latency.csv'
            capture.write_text(capture.read_text() + 'idle_wake,0,30000,\n')
            _, report = analysis.load_run(path)
            self.assertFalse(report['latency_pass']);self.assertEqual(report['missed_responses'], 1)
            (folder / 'energy.csv').write_text('time_s,helmet_j,paw_j\n0,0,0\n600,nan,60\n')
            with self.assertRaises(ValueError): analysis.load_run(path)
            self.fixture(folder)
            run = json.loads(path.read_text());run['checks']['no_audio_overruns'] = 'yes'
            path.write_text(json.dumps(run))
            with self.assertRaises(ValueError): analysis.load_run(path)


if __name__ == '__main__':
    unittest.main()
