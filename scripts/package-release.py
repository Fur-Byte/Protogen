#!/usr/bin/env python3
"""Package matching verified development firmware and a previous recovery release."""
import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
import zipfile
from build_support import source_identity

ROOT = Path(__file__).resolve().parents[1]


def digest(data):
    return hashlib.sha256(data).hexdigest()


def previous_release(path):
    with zipfile.ZipFile(path) as archive:
        manifest = json.loads(archive.read('release.json'))
        files = manifest['sha256']
        if not files or len(archive.namelist()) != len(set(archive.namelist())) or set(archive.namelist()) != set(files) | {'release.json'}:
            raise ValueError('Previous release contains missing or unrecorded files')
        for name, expected in files.items():
            p = PurePosixPath(name)
            if p.is_absolute() or '..' in p.parts or digest(archive.read(name)) != expected:
                raise ValueError('Previous release failed checksum verification')
        return digest(path.read_bytes())


def package(output, previous=None, initial=False, config_path=None):
    if bool(previous) == bool(initial):
        raise ValueError('Supply --previous or explicitly mark --initial-release')
    source = source_identity(ROOT)
    teensy = json.loads((ROOT / 'build/teensy40/manifest.json').read_text())
    esp = json.loads((ROOT / 'build/esp32-build.json').read_text())
    if any(record.get('state') != 'success' or record.get('source') != source for record in (teensy, esp)):
        raise ValueError('Rebuild both roles and Teensy from the current matching source')
    content = {}
    for name, expected in teensy['artifacts'].items():
        if Path(name).name != name:
            raise ValueError('Invalid Teensy artifact name')
        raw = (ROOT / 'build/teensy40' / name).read_bytes()
        if digest(raw) != expected:
            raise ValueError('Teensy artifact checksum mismatch')
        content['teensy/' + name] = raw
    if not any(name.endswith('.hex') for name in content):
        raise ValueError('Missing Teensy HEX')
    for role in ('helmet', 'paw'):
        for name in ('firmware.bin', 'bootloader.bin', 'partitions.bin'):
            key = f'build/esp32/{role}/{name}'
            raw = (ROOT / key).read_bytes()
            if digest(raw) != esp['artifacts'].get(key):
                raise ValueError('ESP32 artifact checksum mismatch or missing role')
            content[f'{role}/{name}'] = raw
    for name in ('toolchain.lock.json', 'docs/HARDWARE_WIRING.md', 'hardware/connector-map.csv', 'hardware/calibration-records.csv',
                 'docs/EFFICIENCY_QUALIFICATION.md', 'docs/RELEASE_RECOVERY.md', 'firmware/esp32/README.md'):
        content[name] = (ROOT / name).read_bytes()
    for directory in ('firmware', 'build/toolchains/user/libraries', 'build/toolchains/data/packages/teensy/hardware'):
        for path in (ROOT / directory).rglob('*'):
            if path.is_file() and (path.name.lower().startswith(('license', 'copying')) or path.name in ('UPSTREAM.json', 'PROVENANCE.json')):
                content['dependencies/' + str(path.relative_to(ROOT))] = path.read_bytes()
    # Preserve project dependency sources and their licenses, without arbitrary
    # build directories, NVS dumps, key headers, or pairing/configuration blobs.
    for directory in ('firmware/PROT_OS7_MAX7219/src/Max72xxPanel',
                      'firmware/PROT_OS7_MAX7219/src/UartCrypto', 'firmware/esp32/protocol_v4/vendor'):
        for path in (ROOT / directory).rglob('*'):
            if path.is_file():
                content['dependencies/' + str(path.relative_to(ROOT))] = path.read_bytes()
    if config_path:
        backup = json.loads(config_path.read_text())
        from maintenance_support import validate_profile
        backup = validate_profile(backup)
        content['configuration/helmet.json'] = (json.dumps(backup, indent=2) + '\n').encode()
    previous_hash = None
    if previous:
        previous_hash = previous_release(previous)
        content['recovery/previous-release.zip'] = previous.read_bytes()
    content['evidence/teensy-build.json'] = json.dumps(teensy, indent=2).encode()
    content['evidence/esp32-build.json'] = json.dumps(esp, indent=2).encode()
    manifest = {'schema': 1, 'source_sha256': source['sha256'], 'status': 'development; physical qualification pending',
                'previous_release_sha256': previous_hash, 'configuration_backup_included': bool(config_path),
                'sha256': {name: digest(raw) for name, raw in content.items()}}
    output.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(output, 'x', compression=zipfile.ZIP_DEFLATED) as archive:
        for name, raw in content.items():
            archive.writestr(name, raw)
        archive.writestr('release.json', json.dumps(manifest, indent=2) + '\n')
    return manifest


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--previous', type=Path)
    parser.add_argument('--initial-release', action='store_true')
    parser.add_argument('--configuration', type=Path, help='Helmet GET exported as a validated profile; never a pairing record')
    args = parser.parse_args()
    package(args.output, args.previous, args.initial_release, args.configuration)
    print('Checksummed recovery package: ' + str(args.output))
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
