#!/usr/bin/env python3
"""Real Teensy builds for the optional radio and protected removable display."""
from pathlib import Path
import re
import shutil
import tempfile
from build_support import BuildRun, check_toolchain, find_cli
import runpy
configure = runpy.run_path(str(Path(__file__).with_name("check-features.py")))["configure_features"]

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
                changed=configure(source,sensors)
                for name in ("WirelessPaw","VisorConnector"):
                    pattern=r"^\s*(?://\s*)?#define "+name+r"(?=\s|$)([^\r\n]*)$"
                    changed,count=re.subn(pattern,lambda m:("#define " if name in extras else "// #define ")+name+m[1],changed,flags=re.M)
                    if count!=1:raise ValueError("Ambiguous feature: "+name)
                (sketch/"PROT_OS7_MAX7219.ino").write_text(changed)
                evidence.compile(cli,sketch,list(sensors+extras))
        return 0 if evidence.finish() else 1
    except (Exception,KeyboardInterrupt) as error:
        evidence.finish(str(error));print(error);return 1

if __name__=="__main__":raise SystemExit(main())
