#!/usr/bin/env python3
"""Real Teensy builds for the optional radio and protected removable display."""
from pathlib import Path
import shutil
import tempfile
from build_support import BuildRun, check_toolchain, find_cli
import runpy
feature_checks = runpy.run_path(str(Path(__file__).with_name("check-features.py")))
configure = feature_checks["configure_features"]

def main():
    root=Path(__file__).resolve().parents[1]
    evidence=BuildRun(root,"extensions")
    try:
        snapshot=evidence.snapshot();source=(snapshot/"PROT_OS7_MAX7219.ino").read_text()
        cli=find_cli();evidence.record["toolchain"]=check_toolchain(root,cli)
        for sensors,extras in [((),("WirelessPaw",)),((),("VisorConnector",)),
                (("VisorSensor","BoopSensor","Microphone"),("WirelessPaw",)),
                (("VisorSensor","BoopSensor","Microphone"),("VisorConnector",)),
                (("VisorSensor","BoopSensor","Microphone"),("WirelessPaw","VisorConnector"))]:
            with tempfile.TemporaryDirectory(prefix="protogen-extensions-") as temp:
                sketch=Path(temp)/"PROT_OS7_MAX7219";shutil.copytree(snapshot,sketch)
                changed=configure(source,sensors+extras,feature_checks["ALL_FEATURES"])
                (sketch/"PROT_OS7_MAX7219.ino").write_text(changed)
                evidence.compile(cli,sketch,list(sensors+extras))
        return 0 if evidence.finish() else 1
    except (Exception,KeyboardInterrupt) as error:
        evidence.finish(str(error));print(error);return 1

if __name__=="__main__":raise SystemExit(main())
