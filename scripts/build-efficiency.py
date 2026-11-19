#!/usr/bin/env python3
"""Build an isolated bench candidate without replacing a production export."""
import argparse
import configparser
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
from build_support import BuildRun, FQBN, check_toolchain, find_cli
from sketch_features import ALL_FEATURES, configure_features

ROOT = Path(__file__).resolve().parents[1]
NAMES = ('cache_face', 'case_2hz', 'oled_1hz', 'oled_pages', 'checkpoint_5min',
         'wearer_deadlines', 'main_events', 'oled_protection')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('optimization', choices=('baseline',) + NAMES + ('teensy_clock',))
    parser.add_argument('--with', dest='combined', action='append', choices=NAMES, default=[],
                        help='Combine bench switches for compile verification only')
    parser.add_argument('--teensy-mhz', type=int, default=600,
                        help='Explicit bench clock; board-supported values only, no automatic selection')
    args = parser.parse_args()
    if args.optimization != 'teensy_clock' and args.teensy_mhz != 600:
        parser.error('Clock changes must be benchmarked independently')
    if args.teensy_mhz not in (24, 60, 90, 120, 150, 180, 240, 396, 450, 528, 600) or (args.optimization == 'teensy_clock' and args.teensy_mhz == 600):
        parser.error('Select an independent lower board clock candidate')
    if args.combined and args.optimization not in NAMES:
        parser.error('Combined checks require a named bench switch')
    names = [name for name in NAMES if name in [args.optimization, *args.combined]]
    if args.combined and len(names) != len(args.combined) + 1:
        parser.error('Each bench switch may appear only once')
    evidence = BuildRun(ROOT, 'efficiency')
    try:
        sketch = evidence.snapshot()
        main = sketch / 'PROT_OS7_MAX7219.ino'
        main.write_text(configure_features(main.read_text(), ALL_FEATURES, ALL_FEATURES))
        flag = ''.join(' -DPROTOGEN_' + name.upper() + '=1' for name in names)
        mask = sum(1 << NAMES.index(name) for name in names)
        optimization = '+'.join(names) if args.combined else args.optimization
        evidence.record.update(optimization=optimization, efficiency_flags=mask, bench_switches=names,
                               teensy_mhz=args.teensy_mhz,
                               status='bench only; physical qualification pending')
        cli = find_cli();evidence.record['toolchain'] = check_toolchain(ROOT, cli)
        fqbn = FQBN.replace('speed=600', 'speed=' + str(args.teensy_mhz))
        evidence.compile(cli, sketch, list(ALL_FEATURES), extra_flags='-Werror' + flag, fqbn=fqbn)
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
            command = [pio, 'run', '--project-dir', str(project), '-e', 'helmet', '-e', 'paw']
            if jobs := os.environ.get('PROTOGEN_PIO_JOBS'):
                command += ['-j', jobs]
            result = subprocess.run(command, env=env, capture_output=True, text=True)
            log = result.stdout + result.stderr
            (evidence.path / 'esp32.log').write_text(log)
            evidence.record['results'].append({'features': ['helmet', 'paw'], 'exit_code': result.returncode,
                                               'warnings': log.lower().count('warning:')})
        evidence.record['artifacts'] = {str(p.relative_to(evidence.path)): hashlib.sha256(p.read_bytes()).hexdigest()
            for p in evidence.path.rglob('*') if p.is_file() and p.suffix in ('.hex', '.bin')}
        success = evidence.finish()
        print(json.dumps({'state': evidence.record['state'], 'run_directory': str(evidence.path),
                          'optimization': optimization, 'physical_measurements': None}, indent=2))
        return 0 if success else 1
    except (Exception, KeyboardInterrupt) as error:
        evidence.finish(str(error));print(error);return 1


if __name__ == '__main__':
    raise SystemExit(main())
