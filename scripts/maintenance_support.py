"""Allowlisted, secret-free helmet configuration backups."""
import hashlib
import json
from pathlib import Path

METADATA_PATH = Path(__file__).resolve().parents[1] / 'tools/helmet/config-metadata.json'
EXPORT_FIELDS = frozenset({
    'brightness', 'startup_expression', 'mouth_mode', 'sensitivity', 'noise_margin',
    'noise_floor', 'boop_trigger', 'boop_release', 'blink_interval_ms', 'blink_jitter_ms',
    'transition_ms', 'attack_ms', 'release_ms', 'natural_blinks', 'idle_motion',
    'boop_mode', 'boop_duration_ms',
})
# Revisit this pin after reviewing generator changes, including otherwise valid range changes.
REVIEWED_METADATA_SHA256 = 'f89f3eb087116e155d7845f12373b058de962baaba296854d42392c21c693876'


def config_ranges():
    raw = METADATA_PATH.read_bytes()
    metadata = json.loads(raw)
    if (not isinstance(metadata, dict) or set(metadata) != {'schema', 'fields', 'storage', 'brightness'}
            or type(metadata['schema']) is not int or metadata['schema'] != 2
            or not isinstance(metadata['fields'], list)):
        raise ValueError('Expected schema-2 configuration metadata and a field list')
    ranges = {}
    for field in metadata['fields']:
        if (not isinstance(field, dict) or set(field) != {'name', 'minimum', 'maximum', 'default', 'eeprom_offset'}
                or not isinstance(field['name'], str)):
            raise ValueError('Invalid configuration field declaration')
        name = field['name']
        if name not in EXPORT_FIELDS or name in ranges:
            raise ValueError('Unknown or duplicate configuration field in metadata')
        low, high = field['minimum'], field['maximum']
        if type(low) is not int or type(high) is not int or not 0 <= low <= high <= 65535:
            raise ValueError('Invalid configuration range for ' + name)
        ranges[name] = (low, high)
    if ranges.keys() != EXPORT_FIELDS:
        raise ValueError('Configuration metadata is missing an exportable field')
    if hashlib.sha256(raw).hexdigest() != REVIEWED_METADATA_SHA256:
        raise ValueError('Configuration metadata changed; check the generated file and review it for export')
    return ranges


def validate_profile(value):
    if (not isinstance(value, dict) or set(value) - {'ok', 'schema', 'name', 'config'}
            or type(value.get('schema')) is not int or value['schema'] != 2):
        raise ValueError('Expected schema-2 helmet configuration only; pairing exports are not supported')
    fields = config_ranges()
    config = value.get('config')
    if not isinstance(config, dict) or set(config) != set(fields):
        raise ValueError('Incomplete or unknown helmet configuration fields')
    for key, number in config.items():
        low, high = fields[key]
        if type(number) is not int or not low <= number <= high:
            raise ValueError('Invalid helmet setting: ' + key)
    if config['boop_trigger'] >= config['boop_release'] or config['blink_jitter_ms'] >= config['blink_interval_ms']:
        raise ValueError('Invalid cross-field configuration')
    return {'schema': 2, 'name': 'Helmet configuration backup', 'config': config}
