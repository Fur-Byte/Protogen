#!/usr/bin/env python3
"""Create one per-helmet development UART key; never display it or burn fuses."""
import argparse
import os
from pathlib import Path
import secrets

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--output', type=Path, required=True, help='Private include directory outside tracked source')
args = parser.parse_args()
root = Path(__file__).resolve().parents[1]
dest = args.output.resolve()
if dest.is_relative_to(root) and not dest.is_relative_to(root / 'build'):
    parser.error('Use an external private directory or ignored build directory')
dest.mkdir(parents=True, exist_ok=True, mode=0o700)
header = dest / 'ProtogenUartKey.h'
fd = os.open(header, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
with os.fdopen(fd, 'w') as f:
    f.write('#pragma once\n#include <stdint.h>\nstatic const uint8_t PROTOGEN_UART_AUTH_KEY[32] = {' +
            ','.join(str(v) for v in secrets.token_bytes(32)) + '};\n')
print('Created unique private UART provisioning header:', header)
print('Compile BOTH controllers with -DPROTOGEN_UART_AUTH=1 and this private include directory.')
