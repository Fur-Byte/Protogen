#!/usr/bin/env python3
"""Isolated host validation; never activates v4 or modifies ESP-IDF.

Build outputs and logs stay under ignored build/v4-*. Nothing is uploaded.
"""
import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import shlex
import subprocess

ROOT=Path(__file__).resolve().parents[1]
V4=ROOT/"firmware/esp32/protocol_v4"
B=ROOT/"build/v4-validation"

def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def run(args, name):
    result=subprocess.run([str(x) for x in args],cwd=ROOT,text=True,capture_output=True,timeout=1800)
    (B/(name+".log")).write_text(result.stdout+result.stderr)
    if result.returncode:raise RuntimeError(name+" failed; inspect build/v4-validation/"+name+".log")
    return result.stdout

def main():
    parser=argparse.ArgumentParser();parser.add_argument("--skip-host",action="store_true");args=parser.parse_args()
    B.mkdir(parents=True,exist_ok=True)
    manifest=json.loads((V4/"vendor/mlkem-native/PROVENANCE.json").read_text())
    for p,h in manifest["source_sha256"].items():
        if sha(V4/"vendor/mlkem-native"/p)!=h:raise RuntimeError("Vendor hash mismatch: "+p)
    spec=importlib.util.spec_from_file_location("pq_preflight",ROOT/"scripts/check-mlkem-preflight.py")
    pre=importlib.util.module_from_spec(spec);spec.loader.exec_module(pre)
    upstream=pre.BUILD/("mlkem-native-"+pre.PINS["commit"])
    if not upstream.is_dir():
        # Existing pinned preflight fetches/verifies/extracts its official archive.
        run(["python3",ROOT/"scripts/check-mlkem-preflight.py"],"bootstrap-pinned-inputs")
    for p,h in manifest["source_sha256"].items():
        if sha(upstream/p)!=h:raise RuntimeError("Upstream/vendor mismatch: "+p)
    common=[os.getenv("CC","cc"),"-std=c11","-O2","-Wall","-Wextra","-Werror",
            "-fsanitize=address,undefined","-fno-sanitize-recover=all","-fno-pie","-no-pie",
            '-DMLK_CONFIG_FILE="'+str(V4/"NativeConfig.h")+'"',
            "-I"+str(V4/"vendor/mlkem-native/mlkem"),V4/"vendor/mlkem-native/mlkem/mlkem_native.c",
            upstream/"test/notrandombytes/notrandombytes.c"]
    vectors=B/"nist-acvp";functional=B/"upstream-functional"
    run(common+[upstream/"test/acvp/acvp_mlkem.c","-o",vectors],"vectors-build")
    run(common+[upstream/"test/src/test_mlkem.c","-o",functional],"functional-build")
    counts=pre.vectors(vectors)
    functional_result=run([functional],"functional")
    run(["python3",ROOT/"scripts/tests/v4/make_vectors.py","--check"],"independent-vectors")
    host_result=None
    if not args.skip_host:host_result=run(["bash",ROOT/"scripts/test-v4.sh"],"host-sanitized")
    clang=ROOT.parent/"toolchains/clang/usr/lib/llvm-19/bin/clang"
    analyses=[]
    database=json.loads((ROOT/"build/v4-test/compile_commands.json").read_text())
    for entry in database:
        p=Path(entry["file"])
        if V4 not in p.parents:continue
        cmd=shlex.split(entry["command"])[1:];filtered=[];skip=False
        for flag in cmd:
            if skip:skip=False;continue
            if flag=="-o":skip=True;continue
            if flag=="-c" or flag.startswith("-fsanitize=") or flag in ["-fno-sanitize-recover=all","-fstack-usage"]:continue
            filtered.append(flag)
        compiler=clang if p.suffix==".c" else clang.with_name("clang++")
        result=run([compiler,"--analyze","-Xanalyzer","-analyzer-output=text"]+filtered,"analyze-"+p.stem)
        log=(B/("analyze-"+p.stem+".log")).read_text()
        if log.strip():raise RuntimeError("Analyzer diagnostics: "+p.name)
        analyses.append(p.name)
    # Active production target explicitly enumerates v3 source; no v4 component.
    source=ROOT/"firmware/esp32/src/CMakeLists.txt"
    if "protocol_v4" in source.read_text():raise RuntimeError("v4 referenced by production component")
    for macro,header,reason in [("ESP_PLATFORM","Interfaces.hpp","EXPERIMENTAL_V4_PLATFORM_ACTIVATION_BLOCKED"),
                               ("PROTOGEN_V4_ACTIVATE_S31","S31Adapter.hpp","NOT_IMPLEMENTED_UNTIL_SUPPORTED_S31_SDK")]:
        probe=B/(macro+".cpp");probe.write_text('#include "'+str(V4/header)+'"\n')
        denied=subprocess.run(["c++","-std=c++17","-D"+macro,"-fsyntax-only",
          "-I"+str(ROOT/"firmware/PROT_OS7_MAX7219/src/HelmetProtocol"),str(probe)],capture_output=True,text=True)
        if denied.returncode==0 or reason not in denied.stderr:raise RuntimeError("Activation guard failed: "+macro)
    baseline="8e6c0301b1bdbb231c3fb11083d976818fd3ecf0"
    unchanged=run(["git","diff",baseline,"--","firmware/esp32",":!firmware/esp32/protocol_v4",":!firmware/esp32/README.md",
                   "firmware/PROT_OS7_MAX7219","toolchain.lock.json"],"production-isolation")
    if unchanged.strip():raise RuntimeError("Production firmware differs from v3 baseline")
    report={"scope":"hardware-independent experimental v4; production v3 not selected or edited",
            "backend":{k:manifest[k] for k in ["upstream","version","commit","license_choice","backend"]},
            "vendor_files_verified":len(manifest["source_sha256"]),"authoritative_vectors":counts,
            "functional_result":functional_result.strip(),"sanitizers":"ASan + UBSan, implementation and core",
            "static_analyzer":analyses,"host_result":host_result,
            "production_component_sha256":sha(source),"production_unchanged_baseline":baseline,
            "activation_negative_compile_tests":2,"hardware_measurements":"NOT PERFORMED"}
    (B/"report.json").write_text(json.dumps(report,indent=2)+"\n")
    print("v4 backend: 145 authoritative cases, upstream functional, hashes/license, independent project vectors, and static analysis passed")
if __name__=="__main__":main()
