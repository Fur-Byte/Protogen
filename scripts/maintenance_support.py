"""Allowlisted, secret-free helmet configuration backups."""
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]


def config_ranges():
    header = (ROOT / 'firmware/PROT_OS7_MAX7219/Configuration.h').read_text()
    entries = re.findall(r'\{"([a-z_]+)", &RuntimeConfig::\w+, (\d+), (\d+|ExpressionCount - 1)\}', header)
    if len(entries) != 17:
        raise ValueError('Configuration schema changed; review the export allowlist')
    return {key: (int(low), 5 if high == 'ExpressionCount - 1' else int(high)) for key, low, high in entries}


def validate_profile(value):
    if not isinstance(value, dict) or set(value) - {'ok', 'schema', 'name', 'config'} or value.get('schema') != 2:
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
