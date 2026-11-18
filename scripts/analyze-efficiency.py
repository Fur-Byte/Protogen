#!/usr/bin/env python3
"""Compare one independent optimization using physical energy/latency captures."""
import argparse
import csv
from decimal import Decimal, InvalidOperation
import io
import hashlib
import json
import math
import re
from pathlib import Path
import statistics

FLAGS = ('cache_face', 'case_2hz', 'oled_1hz', 'oled_pages', 'checkpoint_5min',
         'wearer_deadlines', 'main_events', 'oled_protection')
LOWER_CLOCKS = (24, 60, 90, 120, 150, 180, 240, 396, 450, 528)
MATCH = ('source_sha256', 'workload_sha256', 'settings_sha256', 'security_sha256',
         'rig_id', 'calibration_id', 'operating_range', 'hardware', 'uncertainty_confidence')


LATENCY_FIELDS = ('condition', 'action', 'trigger_edge', 'press_us', 'action_edge_us',
                  'first_reaction_us', 'complete_expression_us')
ENERGY_FIELDS = ('time_s', 'helmet_j', 'paw_j')
MIN_SAMPLES = 1000


def number(value):
    if type(value) is bool or value is None:
        raise ValueError('A missing value or boolean is not a numerical measurement')
    if isinstance(value, str) and not re.fullmatch(r'(?:\d+(?:\.\d*)?|\.\d+)(?:[eE][+-]?\d+)?', value):
        raise ValueError('Malformed numerical measurement')
    try:
        result = float(value)
    except (TypeError, ValueError, OverflowError) as error:
        raise ValueError('Malformed numerical measurement') from error
    if not math.isfinite(result) or result < 0:
        raise ValueError('Measurements must be finite and nonnegative')
    return result


def timestamp(value):
    # Decimal preserves ordering and small deltas on large instrument clocks.
    if not isinstance(value, str) or not re.fullmatch(r'(?:\d+(?:\.\d*)?|\.\d+)(?:[eE][+-]?\d+)?', value):
        raise ValueError('Missing or malformed physical timestamp')
    try:
        result = Decimal(value)
    except InvalidOperation as error:
        raise ValueError('Malformed physical timestamp') from error
    if not result.is_finite() or result < 0 or result > 2**64 - 1:
        raise ValueError('Physical timestamps must be finite and nonnegative')
    return result


def percentile(values, fraction):
    if not values or not 0 < fraction <= 1:
        raise ValueError('Need samples and a percentile in (0, 1]')
    # Nearest rank, explicitly reported; no interpolation hides tail failures.
    return sorted(values)[math.ceil(fraction * len(values)) - 1]


def distribution(values):
    return {'samples': len(values),
            **{f'p{int(p * 100)}_ms': percentile(values, p) if values else None
               for p in (.50, .95, .99)},
            'max_ms': max(values) if values else None}


def csv_rows(raw, required):
    try:
        records = list(csv.reader(io.StringIO(raw.decode('utf-8')), strict=True))
    except (csv.Error, UnicodeError) as error:
        raise ValueError('Malformed CSV capture') from error
    if not records or records[0] != list(required):
        raise ValueError('CSV requires exact versioned fields: ' + ','.join(required))
    if any(len(row) != len(required) for row in records[1:]):
        raise ValueError('Missing or extra CSV measurement fields')
    return [dict(zip(required, row)) for row in records[1:]]


def analyze_latency(rows):
    groups = {group: {'press_to_complete': [], 'action_to_first_reaction': [],
                     'action_to_complete': [], 'attempts': 0,
                     'missed_reactions': 0, 'missed_complete_expressions': 0,
                     'classifications': {}}
              for group in ('online', 'idle_wake')}
    for row in rows:
        if set(row) != set(LATENCY_FIELDS) or any(v is None for v in row.values()):
            raise ValueError('Missing or extra latency measurement fields')
        group, action, edge = row['condition'], row['action'], row['trigger_edge']
        if group not in groups or action not in ('navigation', 'boop', 'wake'):
            raise ValueError('Unknown condition or physical action')
        if edge not in ('press', 'release') or (action == 'navigation' and edge != 'release') or (action == 'boop' and edge != 'press'):
            raise ValueError('Navigation triggers on release; boop triggers on press')
        if action == 'wake' and group != 'idle_wake':
            raise ValueError('Wake actions require an idle_wake condition')
        press, trigger = timestamp(row['press_us']), timestamp(row['action_edge_us'])
        if trigger < press or (edge == 'press' and trigger != press) or (edge == 'release' and trigger <= press):
            raise ValueError('Trigger edge contradicts the initial physical press')
        first = timestamp(row['first_reaction_us']) if row['first_reaction_us'] != '' else None
        complete = timestamp(row['complete_expression_us']) if row['complete_expression_us'] != '' else None
        if (first is not None and first < trigger) or (complete is not None and complete < trigger) or (first is not None and complete is not None and complete < first):
            raise ValueError('Physical optical timestamps are out of chronological order')
        values = groups[group]
        values['attempts'] += 1
        classification = action + ':' + edge
        values['classifications'][classification] = values['classifications'].get(classification, 0) + 1
        if first is None:
            values['missed_reactions'] += 1
        else:
            values['action_to_first_reaction'].append(number((first - trigger) / 1000))
        if complete is None:
            values['missed_complete_expressions'] += 1
        else:
            values['press_to_complete'].append(number((complete - press) / 1000))
            values['action_to_complete'].append(number((complete - trigger) / 1000))
    reports = {}
    passed = True
    for group, values in groups.items():
        reports[group] = {key: distribution(values[key]) for key in
                          ('press_to_complete', 'action_to_first_reaction', 'action_to_complete')}
        reports[group].update({key: values[key] for key in
                              ('attempts', 'missed_reactions', 'missed_complete_expressions', 'classifications')})
        reaction = reports[group]['action_to_first_reaction']
        group_pass = (values['attempts'] >= MIN_SAMPLES and reaction['samples'] >= MIN_SAMPLES and
                      values['missed_reactions'] == 0 and values['missed_complete_expressions'] == 0 and
                      reaction['p99_ms'] is not None and reaction['p99_ms'] < 100)
        reports[group]['reaction_target_pass'] = group_pass
        passed &= group_pass
    return reports, passed


def load_run(path):
    path = Path(path)
    run = json.loads(path.read_text())
    if not isinstance(run, dict):
        raise ValueError('Run metadata must be an object')
    if run.get('measurement') != 'physical':
        raise ValueError('Only physical captures may qualify energy/latency')
    for key in MATCH + ('teensy_mhz', 'flags', 'optimization', 'trial', 'checks',
                        'latency_schema', 'capture_method', 'energy_csv', 'latency_csv',
                        'energy_uncertainty_j'):
        if key not in run:
            raise ValueError('Missing run metadata: ' + key)
    for key in MATCH:
        if not run[key]:
            raise ValueError('Empty run metadata: ' + key)
    if type(run['flags']) is not int or run['flags'] < 0 or run['flags'] > 255:
        raise ValueError('Invalid efficiency flag mask')
    if type(run['teensy_mhz']) is not int or run['teensy_mhz'] not in LOWER_CLOCKS + (600,):
        raise ValueError('Unsupported bench clock')
    if type(run['latency_schema']) is not int or run['latency_schema'] != 2:
        raise ValueError('Use latency schema 2; old completed-expression captures cannot supply first reaction')
    if run['capture_method'] not in ('photodiode', 'high_speed_video'):
        raise ValueError('Latency requires physical emitted-light capture, not ACK/SPI/render timestamps')
    if type(run['trial']) is not int or run['trial'] < 0:
        raise ValueError('Trial must be a nonnegative integer')
    hashes = {}
    captures = {}
    for key in ('energy_csv', 'latency_csv'):
        relative = Path(run[key])
        if relative.is_absolute() or '..' in relative.parts:
            raise ValueError('Capture paths must be relative to the run directory')
        file = path.parent / relative
        raw = file.read_bytes()
        hashes[key] = hashlib.sha256(raw).hexdigest()
        captures[key] = csv_rows(raw, ENERGY_FIELDS if key == 'energy_csv' else LATENCY_FIELDS)
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
    number(duration); number(joules)
    if joules <= 0:
        raise ValueError('Zero energy is not a whole-system measurement')
    uncertainty = number(run['energy_uncertainty_j'])
    confidence = number(run['uncertainty_confidence'])
    if uncertainty <= 0 or uncertainty >= joules or confidence < .95 or confidence >= 1:
        raise ValueError('Record meaningful whole-system energy uncertainty at >=95% confidence')
    latency_report, latency_pass = analyze_latency(captures['latency_csv'])
    checks = run['checks']
    required = ('artwork_and_timing_equivalent', 'microphone_timing_equivalent',
                'radio_reliability_preserved', 'security_preserved', 'sleep_wake_rollover_reconnect_pass',
                'packet_loss_storage_interruption_pass', 'no_missed_command_deadlines',
                'no_memory_growth', 'no_watchdog_resets', 'no_audio_overruns')
    if not isinstance(checks, dict) or set(checks) != set(required) or any(type(checks[k]) is not bool for k in required):
        raise ValueError('Supply explicit physical validation results for every required check')
    return run, {'duration_s': duration, 'whole_system_j': joules, 'mean_watts': joules / duration,
                 'energy_uncertainty_j': uncertainty, 'uncertainty_watts': uncertainty / duration,
                 'latency': latency_report,
                 'missed_reactions': sum(r['missed_reactions'] for r in latency_report.values()),
                 'missed_complete_expressions': sum(r['missed_complete_expressions'] for r in latency_report.values()),
                 'capture_sha256': hashes,
                 'checks_pass': all(checks.values()), 'latency_pass': latency_pass}


def compare(baselines, candidates):
    if len(baselines) != len(candidates) or len(baselines) < 3:
        raise ValueError('Need at least three matched baseline/candidate trials')
    reports = []
    trials = set()
    seen_captures = {"energy_csv": set(), "latency_csv": set()}
    optimization = candidates[0][0]['optimization']
    candidate_clock = candidates[0][0]['teensy_mhz']
    for (base, before), (candidate, after) in zip(baselines, candidates):
        if base.get('measurement') != 'physical' or candidate.get('measurement') != 'physical':
            raise ValueError('Synthetic records cannot establish physical qualification')
        if base['trial'] != candidate['trial'] or base['trial'] in trials:
            raise ValueError('Trial pairs must match and be unique')
        trials.add(base['trial'])
        for metrics in (before, after):
            for capture, seen in seen_captures.items():
                digest = metrics['capture_sha256'][capture]
                if digest in seen:
                    raise ValueError(f'Reused {capture} capture in trial {base["trial"]}')
                seen.add(digest)
        if base['optimization'] != 'baseline' or candidate['optimization'] != optimization:
            raise ValueError('Compare exactly one named independent optimization')
        if any(base[k] != candidate[k] for k in MATCH):
            raise ValueError('Rig, source, workload, settings, security or operating range differs')
        if base['flags'] != 0:
            raise ValueError('Baseline must have all bench flags disabled')
        if base['teensy_mhz'] != 600 or candidate['teensy_mhz'] != candidate_clock:
            raise ValueError('Use the production clock baseline and one fixed candidate clock')
        headroom_pass = True
        if optimization == 'teensy_clock':
            if candidate['flags'] != 0 or candidate['teensy_mhz'] not in LOWER_CLOCKS:
                raise ValueError('Clock test must change only the Teensy clock')
            reserve = number(candidate.get('steady_cpu_headroom_percent', 0))
            if reserve > 100:
                raise ValueError('CPU headroom cannot exceed 100%')
            headroom_pass = reserve >= 50
        elif optimization not in FLAGS or candidate['flags'] != 1 << FLAGS.index(optimization) or base['teensy_mhz'] != candidate['teensy_mhz']:
            raise ValueError('Independent trial must enable only its named switch')
        if abs(before['duration_s'] - after['duration_s']) > .01 * before['duration_s']:
            raise ValueError('Repeatable workload durations differ by more than 1%')
        reduction = 100 * (1 - after['mean_watts'] / before['mean_watts'])
        resolved = after['mean_watts'] + after['uncertainty_watts'] < before['mean_watts'] - before['uncertainty_watts']
        reports.append({'trial': base['trial'], 'baseline': before, 'candidate': after,
                        'whole_system_reduction_percent': reduction, 'reduction_exceeds_uncertainty': resolved,
                        'cpu_headroom_pass': headroom_pass})
    reductions = [r['whole_system_reduction_percent'] for r in reports]
    passed = all(r['baseline']['checks_pass'] and r['baseline']['latency_pass'] and
                 r['candidate']['checks_pass'] and r['candidate']['latency_pass'] and
                 r['reduction_exceeds_uncertainty'] and r['cpu_headroom_pass'] for r in reports)
    return {'measurement': 'physical', 'hardware_qualified': False,
            'qualification_status': 'raw physical evidence and instrument provenance require independent review',
            'latency_target': 'action_to_first_reaction p99 < 100 ms; completion metrics are diagnostic',
            'percentile_method': 'nearest_rank', 'optimization': optimization, 'trials': reports,
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
