#!/usr/bin/env python3
"""Review exact 6.1.0 to 6.1.1 C3 changes, never certify S31 or production."""

import argparse
import json
from pathlib import Path

from native_esp import compare_configuration

# The reference CI builds without reproducibility. The official candidate uses
# reproducibility; these three timestamp settings are intentionally different.
EXPECTED = {
    "IDF_INIT_VERSION": ["6.1.0", "6.1.1"],
    "MBEDTLS_CONSTANT_TIME_PRIME_GEN": [None, False],
    "SD_READY_POLL_PERIOD_START_US": [None, 100],
    "UHCI_RECV_FUNC_IN_IRAM": [None, False],
    "APP_REPRODUCIBLE_BUILD": [False, True],
    "APP_COMPILE_TIME_DATE": [True, None],
    "BOOTLOADER_COMPILE_TIME_DATE": [True, None],
}
SIZE_LIMITS = {"helmet": 780000, "paw": 704000}


def compare(reference, candidate, role):
    record = compare_configuration(reference, candidate)
    if record["configuration_differences"] != EXPECTED or not record["partition_table_identical"]:
        raise RuntimeError(f"Unreviewed official SDK differences: {record}")
    size = (candidate / "protogen_wireless.bin").stat().st_size
    if size > SIZE_LIMITS[role]:
        raise RuntimeError(f"Unreviewed official SDK size increase: {role} {size}")
    return dict(record, binary_bytes=size, reviewed_candidate=True, production_qualified=False)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("reference", type=Path)
    parser.add_argument("candidate", type=Path)
    args = parser.parse_args()
    for role in SIZE_LIMITS:
        print(role, json.dumps(compare(args.reference / role, args.candidate / role, role)))


if __name__ == "__main__":
    main()
