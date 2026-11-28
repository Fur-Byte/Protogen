#!/usr/bin/env python3
"""Validate reviewed parent-tool constraints in the locked Python environment."""

import argparse
import importlib.metadata
import json
from pathlib import Path
import re

from packaging.specifiers import SpecifierSet

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("group", choices=("platformio", "idf", "esptool-contrib"))
    parser.add_argument("--target", type=Path)
    args = parser.parse_args()
    if (args.group == "esptool-contrib") != bool(args.target):
        parser.error("Only the contrib group requires --target")
    constraints = json.loads((ROOT / "scripts/requirements/esp-compatibility.json").read_text())[args.group]
    distributions = (
        importlib.metadata.distributions(path=[str(args.target)])
        if args.target
        else importlib.metadata.distributions()
    )
    actual = {re.sub(r"[-_.]+", "-", d.metadata["Name"]).lower(): d.version for d in distributions}
    for name, constraint in constraints.items():
        if name not in actual or not SpecifierSet(constraint).contains(actual[name], prereleases=False):
            parser.exit(1, f"{args.group}: {name} does not satisfy reviewed constraint {constraint}\n")
    if args.group == "platformio":
        from platformio.dependencies import get_core_dependencies
        from platformio.package.meta import PackageSpec
        from semantic_version import Version

        tools = json.loads((ROOT / "scripts/requirements/esp-tools-linux-x86_64.json").read_text())
        version = tools["tool-scons"]["version"]
        spec = PackageSpec(
            owner="platformio", name="tool-scons", requirements=get_core_dependencies()["tool-scons"]
        )
        if not spec.requirements.match(Version(version)):
            parser.exit(1, "Locked SCons version does not satisfy PlatformIO core\n")
    print(f"{args.group}: reviewed compatibility constraints satisfied")


if __name__ == "__main__":
    main()
