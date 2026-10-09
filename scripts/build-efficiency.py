#!/usr/bin/env python3
"""Build one independent bench candidate; never replace a production export."""
import argparse
import configparser
import hashlib
import json
import os
from pathlib import Path
import runpy
import shutil
import subprocess
from build_support import BuildRun, FQBN, check_toolchain, find_cli

ROOT = Path(__file__).resolve().parents[1]
NAMES = ('cache_face', 'case_2hz', 'oled_1hz', 'oled_pages', 'checkpoint_5min',
         'wearer_deadlines', 'main_events', 'oled_protection')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('optimization', choices=('baseline',) + NAMES + ('teensy_clock',))
    parser.add_argument('--teensy-mhz', type=int, default=600,
                        help='Explicit bench clock; board-supported values only, no automatic selection')
    args = parser.parse_args()
    if args.optimization != 'teensy_clock' and args.teensy_mhz != 600:
        parser.error('Clock changes must be benchmarked independently')
    if args.teensy_mhz not in (24, 60, 90, 120, 150, 180, 240, 396, 450, 528, 600) or (args.optimization == 'teensy_clock' and args.teensy_mhz == 600):
        parser.error('Select an independent lower board clock candidate')
    evidence = BuildRun(ROOT, 'efficiency')
    try:
        sketch = evidence.snapshot()
        features = runpy.run_path(str(ROOT / 'scripts/check-features.py'))
        main = sketch / 'PROT_OS7_MAX7219.ino'
        enabled = features['ALL_FEATURES']
        main.write_text(features['configure_features'](main.read_text(), enabled, enabled))
        flag = '' if args.optimization in ('baseline', 'teensy_clock') else ' -DPROTOGEN_' + args.optimization.upper() + '=1'
        mask = 0 if not flag else 1 << NAMES.index(args.optimization)
        evidence.record.update(optimization=args.optimization, efficiency_flags=mask, teensy_mhz=args.teensy_mhz,
                               status='bench only; physical qualification pending')
        cli = find_cli();evidence.record['toolchain'] = check_toolchain(ROOT, cli)
        fqbn = FQBN.replace('speed=600', 'speed=' + str(args.teensy_mhz))
        evidence.compile(cli, sketch, list(enabled), extra_flags='-Werror' + flag, fqbn=fqbn)
        if args.optimization != 'teensy_clock':
            project = evidence.path / 'esp32'
            shutil.copytree(ROOT / 'firmware/esp32', project)
            component = project / 'src/CMakeLists.txt'
            component.write_text(component.read_text().replace('../../PROT_OS7_MAX7219/src/HelmetProtocol',
                                                               str(sketch / 'src/HelmetProtocol')))
            ini = configparser.ConfigParser(interpolation=None)
            ini.read(project / 'platformio.ini')
            ini['platformio']['build_dir'] = str(evidence.path / 'esp32-artifacts')
            ini['env']['extra_scripts'] = 'pre:' + str(ROOT / 'scripts/esp32-uart-build.py')
            ini['env']['build_flags'] = '-std=gnu++17 -I' + str(sketch / 'src/HelmetProtocol')
            for role in ('helmet', 'paw'):
                ini['env:' + role]['build_flags'] += flag
                ini['env:' + role]['board_build.esp-idf.sdkconfig_path'] = str(evidence.path / ('sdkconfig.' + role))
            with (project / 'platformio.ini').open('w') as output:
                ini.write(output)
            env = dict(os.environ, PLATFORMIO_CORE_DIR=str(ROOT / 'build/platformio'), IDF_COMPONENT_MANAGER='0')
            pio = os.environ.get('PIO', str(ROOT / 'build/esp32-venv/bin/pio'))
            result = subprocess.run([pio, 'run', '--project-dir', str(project), '-e', 'helmet', '-e', 'paw'],
                                    env=env, capture_output=True, text=True)
            log = result.stdout + result.stderr
            (evidence.path / 'esp32.log').write_text(log)
            evidence.record['results'].append({'features': ['helmet', 'paw'], 'exit_code': result.returncode,
                                               'warnings': log.lower().count('warning:')})
        evidence.record['artifacts'] = {str(p.relative_to(evidence.path)): hashlib.sha256(p.read_bytes()).hexdigest()
            for p in evidence.path.rglob('*') if p.is_file() and p.suffix in ('.hex', '.bin')}
        success = evidence.finish()
        print(json.dumps({'state': evidence.record['state'], 'run_directory': str(evidence.path),
                          'optimization': args.optimization, 'physical_measurements': None}, indent=2))
        return 0 if success else 1
    except (Exception, KeyboardInterrupt) as error:
        evidence.finish(str(error));print(error);return 1


if __name__ == '__main__':
    raise SystemExit(main())
