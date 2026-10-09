#!/usr/bin/env python3
"""Install the lock's development toolchains inside build/, without flashing."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import shlex
import subprocess
import tarfile
import tempfile
import urllib.request

ROOT = Path(__file__).resolve().parents[1]


def download(url, path):
    path.parent.mkdir(parents=True, exist_ok=True)
    with urllib.request.urlopen(url, timeout=120) as response:
        path.write_bytes(response.read())
    return path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--teensy-only', action='store_true')
    args = parser.parse_args()
    lock = json.loads((ROOT / 'toolchain.lock.json').read_text())
    folder = ROOT / 'build/toolchains'
    folder.mkdir(parents=True, exist_ok=True)
    system = {'Linux': 'Linux', 'Darwin': 'macOS'}.get(platform.system())
    arch = {'x86_64': '64bit', 'AMD64': '64bit', 'aarch64': 'ARM64',
            'arm64': 'ARM64', 'armv7l': 'ARMv7'}.get(platform.machine())
    if not system or not arch:
        raise RuntimeError('Use a supported Linux/macOS host or install the lock manually')
    version = lock['arduino_cli']
    name = f'arduino-cli_{version}_{system}_{arch}.tar.gz'
    checksums = download(f'https://github.com/arduino/arduino-cli/releases/download/v{version}/{version}-checksums.txt',
                         folder / 'downloads/cli-checksums.txt').read_text()
    expected = {line.split()[1]: line.split()[0] for line in checksums.splitlines() if len(line.split()) == 2}
    if name not in expected:
        raise RuntimeError('Pinned CLI release does not support this host')
    archive = folder / 'downloads' / name
    if not archive.is_file() or hashlib.sha256(archive.read_bytes()).hexdigest() != expected[name]:
        download('https://downloads.arduino.cc/arduino-cli/' + name, archive)
    if hashlib.sha256(archive.read_bytes()).hexdigest() != expected[name]:
        raise RuntimeError('Arduino CLI checksum mismatch')
    cli = folder / 'bin/arduino-cli'
    cli.parent.mkdir(exist_ok=True)
    with tarfile.open(archive) as source:
        member = source.extractfile('arduino-cli')
        if member is None:
            raise RuntimeError('CLI binary missing from archive')
        binary = member.read()
    if not cli.is_file() or cli.read_bytes() != binary:
        with tempfile.NamedTemporaryFile(dir=cli.parent, delete=False) as temporary:
            temporary.write(binary)
            replacement = Path(temporary.name)
        replacement.chmod(0o755)
        os.replace(replacement, cli)
    variables = {'ARDUINO_CLI': str(cli), 'ARDUINO_DIRECTORIES_DATA': str(folder / 'data'),
                 'ARDUINO_BUILD_CACHE_PATH': str(folder / 'cache'),
                 'ARDUINO_DIRECTORIES_DOWNLOADS': str(folder / 'downloads'),
                 'ARDUINO_DIRECTORIES_USER': str(folder / 'user'),
                 'ARDUINO_BOARD_MANAGER_ADDITIONAL_URLS': lock['platform']['index_url'],
                 'ARDUINO_FFT_SRC': str(folder / 'user/libraries/arduinoFFT/src'),
                 'PIO': str(ROOT / 'build/esp32-venv/bin/pio'),
                 'PLATFORMIO_CORE_DIR': str(ROOT / 'build/platformio'), 'IDF_COMPONENT_MANAGER': '0'}
    env = dict(os.environ, **variables)
    def run(*command):
        subprocess.run(command, env=env, check=True)
    run(str(cli), 'core', 'update-index')
    run(str(cli), 'core', 'install', lock['platform']['id'] + '@' + lock['platform']['version'])
    for name, version in lock['libraries'].items():
        run(str(cli), 'lib', 'install', name + '@' + version)
    if not args.teensy_only:
        run('python3', '-m', 'venv', str(ROOT / 'build/esp32-venv'))
        python = str(ROOT / 'build/esp32-venv/bin/python')
        run(python, '-m', 'pip', 'install', 'platformio==' + lock['esp32']['platformio'])
        run(variables['PIO'], 'pkg', 'install', '--project-dir', str(ROOT / 'firmware/esp32'))
        # Let PlatformIO establish its own venv metadata before adding IDF's
        # constrained core dependencies. A broken prior setup may fail its
        # configuration step; the explicit install below repairs it.
        subprocess.run([variables['PIO'], 'run', '--project-dir', str(ROOT / 'firmware/esp32'),
                        '-e', 'helmet', '-t', 'compiledb'], env=env, check=False)
        # Package installation alone does not prepare IDF's separate Python
        # environment. Install it explicitly, including on interrupted retries.
        sdk_version = lock['esp32']['esp_idf']
        sdk_python_folder = ROOT / 'build/platformio/penv' / ('.espidf-' + sdk_version)
        run('python3', '-m', 'venv', str(sdk_python_folder))
        sdk_python = str(sdk_python_folder / 'bin/python')
        branch = '.'.join(sdk_version.split('.')[:2])
        constraints = download('https://dl.espressif.com/dl/esp-idf/espidf.constraints.v' + branch + '.txt',
                               folder / 'downloads/idf-constraints.txt')
        requirements = ROOT / 'build/platformio/packages/framework-espidf/tools/requirements/requirements.core.txt'
        run(sdk_python, '-m', 'pip', 'install', '-c', str(constraints), '-r', str(requirements), 'urllib3<2')
        (folder / 'idf-python-dependencies.txt').write_text(
            subprocess.check_output([sdk_python, '-m', 'pip', 'freeze'], env=env, text=True))
        freeze = subprocess.check_output([python, '-m', 'pip', 'freeze'], env=env, text=True)
        (folder / 'python-dependencies.txt').write_text(freeze)
    (folder / 'environment.sh').write_text(''.join('export ' + key + '=' + shlex.quote(value) + '\n'
                                                   for key, value in variables.items()))
    (folder / 'lock.json').write_text(json.dumps(lock, indent=2) + '\n')
    print('Installed. Run: source build/toolchains/environment.sh')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
