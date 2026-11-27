#!/usr/bin/env python3
"""Read local Teensy diagnostics and configuration; never access pairing storage."""
import argparse
import json
from pathlib import Path
from maintenance_support import validate_profile
from serial_transport import SerialTransport


def portable_profile_from_get(reply):
    """Convert the live firmware GET reply into a strictly allowlisted backup.

    Brightness telemetry is part of GET, but is not portable profile content.
    Reject unexpected fields instead of accidentally exporting future secrets.
    """
    required = {'ok', 'schema', 'config'}
    if (not isinstance(reply, dict) or set(reply) not in (required, required | {'brightness'})
            or reply['ok'] is not True or type(reply['schema']) is not int):
        raise ValueError('Unexpected firmware GET response')
    profile = validate_profile({'schema': reply['schema'], 'config': reply['config']})
    if 'brightness' in reply:
        brightness = reply['brightness']
        fields = {'configured', 'effective', 'maximum'}
        if (not isinstance(brightness, dict) or set(brightness) not in (fields, fields | {'approved'})
                or any(type(value) is not int or not 0 <= value <= 15 for value in brightness.values())
                or brightness['configured'] != profile['config']['brightness']
                or brightness['effective'] > brightness['maximum']):
            raise ValueError('Unexpected firmware brightness metadata')
    return profile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', required=True, help='Physical Teensy USB serial device')
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()
    serial = SerialTransport(args.port)
    try:
        profile = portable_profile_from_get(serial.command('GET'))
        status = serial.command('STATUS');performance = serial.command('PERF')
        # Values from firmware diagnostics are numeric/boolean, with a fixed
        # version string. Unknown future fields require export review.
        allowed_status = {'ok', 'schema', 'features', 'expression', 'sleeping', 'visible', 'display_ready',
            'boop', 'boop_reading', 'speaking', 'envelope', 'noise_floor', 'audio_gate', 'calibrating',
            'audio_drops', 'audio_frames', 'sample_rate_hz', 'spi_hz', 'blink_random_ready', 'blink_entropy_fallbacks',
            'next_blink_ms', 'blink_duration_ms', 'paw_connected', 'paw_rejected', 'microphone_enabled',
            'sensor_faults', 'connector_ready', 'boop_mode'}
        allowed_perf = {'ok', 'efficiency_flags', 'render_attempts', 'compositions', 'cache_skips', 'changed_frames',
            'display_wakes', 'render_total_us', 'render_max_us', 'config_write_attempts',
            'config_verified_writes', 'config_write_failures', 'idle_waits', 'idle_total_us', 'idle_max_us'}
        def checked(value, allowed):
            if not isinstance(value, dict) or value.get('ok') is not True:
                raise ValueError('Diagnostic request failed')
            counters = {'blink_entropy_fallbacks': 32, 'idle_waits': 32,
                        'idle_total_us': 64, 'idle_max_us': 32}
            return {key: item for key, item in value.items()
                    if key in allowed and (type(item) in (int, bool) if key not in counters
                                           else type(item) is int and 0 <= item < 2**counters[key])}
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
