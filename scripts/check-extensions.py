#!/usr/bin/env python3
"""Real Teensy builds for the optional radio and protected removable display."""
from pathlib import Path
import shutil
import tempfile
from build_support import BuildRun, check_toolchain, find_cli
from sketch_features import ALL_FEATURES, SENSOR_FEATURES, configure_features

def main():
    root=Path(__file__).resolve().parents[1]
    evidence=BuildRun(root,"extensions")
    try:
        snapshot=evidence.snapshot();source=(snapshot/"PROT_OS7_MAX7219.ino").read_text()
        cli=find_cli();evidence.record["toolchain"]=check_toolchain(root,cli)
        for sensors,extras in [((),("WirelessPaw",)),((),("VisorConnector",)),
                (SENSOR_FEATURES,("WirelessPaw",)),
                (SENSOR_FEATURES,("VisorConnector",)),
                (SENSOR_FEATURES,("WirelessPaw","VisorConnector"))]:
            with tempfile.TemporaryDirectory(prefix="protogen-extensions-") as temp:
                sketch=Path(temp)/"PROT_OS7_MAX7219";shutil.copytree(snapshot,sketch)
                changed=configure_features(source,sensors+extras,ALL_FEATURES)
                (sketch/"PROT_OS7_MAX7219.ino").write_text(changed)
                evidence.compile(cli,sketch,list(sensors+extras))
        return 0 if evidence.finish() else 1
    except (Exception,KeyboardInterrupt) as error:
        evidence.finish(str(error));print(error);return 1

if __name__=="__main__":raise SystemExit(main())
