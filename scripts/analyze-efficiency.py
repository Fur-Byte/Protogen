#!/usr/bin/env python3
"""Compare one independent optimization using physical energy/latency captures."""
import argparse
import csv
import hashlib
import json
import math
from pathlib import Path
import statistics

FLAGS = ('cache_face', 'case_2hz', 'oled_1hz', 'oled_pages', 'checkpoint_5min',
         'wearer_deadlines', 'main_events', 'oled_protection')
LOWER_CLOCKS = (24, 60, 90, 120, 150, 180, 240, 396, 450, 528)
MATCH = ('source_sha256', 'workload_sha256', 'settings_sha256', 'security_sha256',
         'rig_id', 'calibration_id', 'operating_range', 'hardware', 'uncertainty_confidence')


def number(value):
    if type(value) is bool:
        raise ValueError('A boolean is not a numerical measurement')
    result = float(value)
    if not math.isfinite(result) or result < 0:
        raise ValueError('Measurements must be finite and nonnegative')
    return result


def percentile(values, fraction):
    if not values:
        raise ValueError('No physical latency samples')
    return sorted(values)[math.ceil(fraction * len(values)) - 1]


def load_run(path):
    run = json.loads(path.read_text())
    if run.get('measurement') != 'physical':
        raise ValueError('Only physical captures may qualify energy/latency')
    for key in MATCH + ('teensy_mhz', 'flags', 'optimization', 'trial', 'checks'):
        if key not in run:
            raise ValueError('Missing run metadata: ' + key)
    for key in MATCH:
        if not run[key]:
            raise ValueError('Empty run metadata: ' + key)
    if type(run['flags']) is not int or run['flags'] < 0 or run['flags'] > 255:
        raise ValueError('Invalid efficiency flag mask')
    if type(run['teensy_mhz']) is not int or run['teensy_mhz'] not in LOWER_CLOCKS + (600,):
        raise ValueError('Unsupported bench clock')
    hashes = {}
    captures = {}
    for key in ('energy_csv', 'latency_csv'):
        file = path.parent / run[key]
        raw = file.read_bytes()
        hashes[key] = hashlib.sha256(raw).hexdigest()
        captures[key] = list(csv.DictReader(raw.decode().splitlines()))
    energy = captures['energy_csv']
    if len(energy) < 2:
        raise ValueError('Need cumulative helmet and paw energy over the complete trial')
    readings = [(number(r['time_s']), number(r['helmet_j']), number(r['paw_j'])) for r in energy]
    if any(b[0] <= a[0] or b[1] < a[1] or b[2] < a[2] for a, b in zip(readings, readings[1:])):
        raise ValueError('Energy trace has a reset, gap in time ordering, or negative counter delta')
    duration = readings[-1][0] - readings[0][0]
    if duration < 600:
        raise ValueError('Energy trials must cover at least ten minutes')
    joules = sum(readings[-1][i] - readings[0][i] for i in (1, 2))
    if joules <= 0:
        raise ValueError('Zero energy is not a whole-system measurement')
    uncertainty = number(run['energy_uncertainty_j'])
    confidence = number(run['uncertainty_confidence'])
    if uncertainty <= 0 or uncertainty >= joules or confidence < .95 or confidence >= 1:
        raise ValueError('Record meaningful whole-system energy uncertainty at >=95% confidence')
    latencies = {group: {'press': [], 'action': []} for group in ('online', 'idle_wake')}
    missed = 0
    for row in captures['latency_csv']:
        group = row['condition']
        if group not in latencies:
            raise ValueError('Unknown latency condition')
        press = number(row['press_us']); action = number(row['action_edge_us'])
        if action < press:
            raise ValueError('Action edge precedes physical press')
        # Missing responses remain failures; they are never removed from p99.
        if not row['visible_expression_us']:
            missed += 1
            continue
        visible = number(row['visible_expression_us'])
        if visible < action:
            raise ValueError('Visible expression precedes the triggering physical edge')
        latencies[group]['press'].append((visible - press) / 1000)
        latencies[group]['action'].append((visible - action) / 1000)
    latency_report = {}
    latency_pass = missed == 0
    for group, values in latencies.items():
        count = len(values['press'])
        p99 = percentile(values['press'], .99) if count else None
        latency_report[group] = {'samples': count, 'physical_press_p99_ms': p99,
            'physical_action_edge_p99_ms': percentile(values['action'], .99) if count else None,
            'physical_press_max_ms': max(values['press']) if count else None}
        latency_pass &= count >= 1000 and p99 is not None and p99 < 100
    checks = run['checks']
    required = ('artwork_and_timing_equivalent', 'microphone_timing_equivalent',
                'radio_reliability_preserved', 'security_preserved', 'sleep_wake_rollover_reconnect_pass',
                'packet_loss_storage_interruption_pass', 'no_missed_command_deadlines',
                'no_memory_growth', 'no_watchdog_resets', 'no_audio_overruns')
    if set(checks) != set(required) or any(type(checks[k]) is not bool for k in required):
        raise ValueError('Supply explicit physical validation results for every required check')
    return run, {'duration_s': duration, 'whole_system_j': joules, 'mean_watts': joules / duration,
                 'energy_uncertainty_j': uncertainty, 'uncertainty_watts': uncertainty / duration,
                 'latency': latency_report, 'missed_responses': missed, 'capture_sha256': hashes,
                 'checks_pass': all(checks.values()), 'latency_pass': latency_pass}


def compare(baselines, candidates):
    if len(baselines) != len(candidates) or len(baselines) < 3:
        raise ValueError('Need at least three matched baseline/candidate trials')
    reports = []
    trials = set()
    optimization = candidates[0][0]['optimization']
    candidate_clock = candidates[0][0]['teensy_mhz']
    for (base, before), (candidate, after) in zip(baselines, candidates):
        if base['trial'] != candidate['trial'] or base['trial'] in trials:
            raise ValueError('Trial pairs must match and be unique')
        trials.add(base['trial'])
        if base['optimization'] != 'baseline' or candidate['optimization'] != optimization:
            raise ValueError('Compare exactly one named independent optimization')
        if any(base[k] != candidate[k] for k in MATCH):
            raise ValueError('Rig, source, workload, settings, security or operating range differs')
        if base['flags'] != 0:
            raise ValueError('Baseline must have all bench flags disabled')
        if base['teensy_mhz'] != 600 or candidate['teensy_mhz'] != candidate_clock:
            raise ValueError('Use the production clock baseline and one fixed candidate clock')
        if optimization == 'teensy_clock':
            if candidate['flags'] != 0 or candidate['teensy_mhz'] not in LOWER_CLOCKS:
                raise ValueError('Clock test must change only the Teensy clock')
            reserve = number(candidate.get('steady_cpu_headroom_percent', 0))
            if reserve > 100:
                raise ValueError('CPU headroom cannot exceed 100%')
            if reserve < 50:
                after['checks_pass'] = False
        elif optimization not in FLAGS or candidate['flags'] != 1 << FLAGS.index(optimization) or base['teensy_mhz'] != candidate['teensy_mhz']:
            raise ValueError('Independent trial must enable only its named switch')
        if abs(before['duration_s'] - after['duration_s']) > .01 * before['duration_s']:
            raise ValueError('Repeatable workload durations differ by more than 1%')
        reduction = 100 * (1 - after['mean_watts'] / before['mean_watts'])
        resolved = after['mean_watts'] + after['uncertainty_watts'] < before['mean_watts'] - before['uncertainty_watts']
        reports.append({'trial': base['trial'], 'baseline': before, 'candidate': after,
                        'whole_system_reduction_percent': reduction, 'reduction_exceeds_uncertainty': resolved})
    reductions = [r['whole_system_reduction_percent'] for r in reports]
    passed = all(r['baseline']['checks_pass'] and r['baseline']['latency_pass'] and
                 r['candidate']['checks_pass'] and r['candidate']['latency_pass'] and
                 r['reduction_exceeds_uncertainty'] for r in reports)
    return {'measurement': 'physical', 'optimization': optimization, 'trials': reports,
            'mean_reduction_percent': statistics.mean(reductions),
            'minimum_reduction_percent': min(reductions),
            'candidate_passes_recorded_checks': passed and min(reductions) > 0,
            'production_activation': 'requires review of raw captures, uncertainty and qualification gates'}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--baseline', type=Path, nargs='+', required=True)
    parser.add_argument('--candidate', type=Path, nargs='+', required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    report = compare([load_run(p) for p in args.baseline], [load_run(p) for p in args.candidate])
    args.output.write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps({k: v for k, v in report.items() if k != 'trials'}, indent=2))
    return 0 if report['candidate_passes_recorded_checks'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
