#!/usr/bin/env python3
"""Read local Teensy diagnostics and configuration; never access pairing storage."""
import argparse
import importlib.util
import json
from pathlib import Path
from maintenance_support import validate_profile

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', required=True, help='Physical Teensy USB serial device')
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()
    spec = importlib.util.spec_from_file_location('helmet_tool', ROOT / 'scripts/helmet-tool.py')
    module = importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
    serial = module.SerialTransport(args.port)
    try:
        profile = validate_profile(serial.command('GET'))
        status = serial.command('STATUS');performance = serial.command('PERF')
        # Values from firmware diagnostics are numeric/boolean, with a fixed
        # version string. Unknown future fields require export review.
        allowed_status = {'ok', 'schema', 'features', 'expression', 'sleeping', 'visible', 'display_ready',
            'boop', 'boop_reading', 'speaking', 'envelope', 'noise_floor', 'audio_gate', 'calibrating',
            'audio_drops', 'audio_frames', 'sample_rate_hz', 'spi_hz', 'blink_random_ready',
            'next_blink_ms', 'blink_duration_ms', 'paw_connected', 'paw_rejected', 'microphone_enabled',
            'sensor_faults', 'connector_ready', 'boop_mode'}
        allowed_perf = {'ok', 'efficiency_flags', 'render_attempts', 'compositions', 'cache_skips', 'changed_frames',
            'display_wakes', 'render_total_us', 'render_max_us', 'config_write_attempts',
            'config_verified_writes', 'config_write_failures'}
        def checked(value, allowed):
            if not isinstance(value, dict) or value.get('ok') is not True:
                raise ValueError('Diagnostic request failed')
            return {key: item for key, item in value.items() if key in allowed and type(item) in (int, bool)}
        result = {'measurement': 'physical_diagnostics; external latency and paw energy still required',
                  'configuration': profile, 'status': checked(status, allowed_status),
                  'performance': checked(performance, allowed_perf)}
        with args.output.open('x') as output:
            output.write(json.dumps(result, indent=2) + '\n')
    finally:
        serial.close()
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
