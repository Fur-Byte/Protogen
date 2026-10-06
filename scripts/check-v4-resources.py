#!/usr/bin/env python3
"""Unlinked S31 compiler estimates only; does not configure/patch an SDK.

Requires the existing isolated --preview S31 research build for its official
toolchain response files. The ESP_PLATFORM activation guard remains intact.
"""
import hashlib
import json
from pathlib import Path
import re
import subprocess

ROOT=Path(__file__).resolve().parents[1]
V4=ROOT/"firmware/esp32/protocol_v4"
B=ROOT/"build/v4-resources"
TOOLS=ROOT/"build/platformio/packages/toolchain-riscv32-esp/bin"

def run(args,name):
    p=subprocess.run([str(x) for x in args],cwd=ROOT,text=True,capture_output=True,timeout=120)
    (B/(name+".log")).write_text(p.stdout+p.stderr)
    if p.returncode or "warning:" in p.stderr.lower():raise RuntimeError(name+" failed; inspect its log")
    return p.stdout

def graph(path):
    nodes,edges={},{}
    for line in path.read_text().splitlines():
        m=re.match(r'node: \{ title: "([^"]+)" label: "([^"\\]+).*?\\n(\d+) bytes',line)
        if m:nodes[m[1]]=(m[2],int(m[3]))
        m=re.match(r'edge: \{ sourcename: "([^"]+)" targetname: "([^"]+)"',line)
        if m:edges.setdefault(m[1],set()).add(m[2])
    def visit(n,chain=()):
        if n in chain:raise RuntimeError("recursive static call graph")
        name,size=nodes.get(n,(n,0));child=max((visit(v,chain+(n,)) for v in edges.get(n,())),default=(0,[]))
        return size+child[0],[(name,size)]+child[1]
    return {nodes[n][0]:{"known_frames_sum_bytes":visit(n)[0],"path":visit(n)[1]}
            for n in nodes if nodes[n][0] in ["pqv4_keypair","pqv4_enc","pqv4_dec","pqv4_native"]}

def main():
    B.mkdir(parents=True,exist_ok=True)
    response=ROOT/"build/s31-probe/helmet/toolchain"
    if not response.is_dir():raise RuntimeError("Run existing S31 preflight --preview first; this is not production integration")
    flags=["-Og","-fno-shrink-wrap","-ffunction-sections","-fdata-sections","-Wall","-Wextra","-Werror",
           "-fstack-usage","-fcallgraph-info=su","-I"+str(V4),
           "-I"+str(ROOT/"firmware/PROT_OS7_MAX7219/src/HelmetProtocol")]
    objects=[];frames={}
    for stem in ["Core","Derivation","Fragments","Storage","Traffic","Worker","Sizes","Native"]:
        native=stem=="Native";source=V4/(stem+".c" if native else stem+".cpp")
        if stem=="Sizes":source=ROOT/"scripts/tests/v4/Sizes.cpp"
        compiler=TOOLS/("riscv32-esp-elf-gcc" if native else "riscv32-esp-elf-g++")
        extra=['-DMLK_CONFIG_FILE="'+str(V4/"NativeConfig.h")+'"'] if native else ["-fno-exceptions"]
        obj=B/(stem+".o")
        run([compiler,"@"+str(response/("cflags" if native else "cxxflags")),"-std=c11" if native else "-std=c++17"]+
            flags+extra+["-c",source,"-o",obj],stem)
        size=run([TOOLS/"riscv32-esp-elf-size",obj],stem+"-size").splitlines()[-1].split()
        for line in obj.with_suffix(".su").read_text().splitlines():
            parts=line.rsplit("\t",2)
            if len(parts)==3:frames[parts[0]]={"bytes":int(parts[1]),"kind":parts[2]}
        objects.append({"source":str(source.relative_to(ROOT)),"text":int(size[0]),"data":int(size[1]),"bss":int(size[2]),
                        "object_sha256":hashlib.sha256(obj.read_bytes()).hexdigest()})
    sizes={}
    for line in run([TOOLS/"riscv32-esp-elf-nm","-S","--radix=d",B/"Sizes.o"],"sizes-symbols").splitlines():
        parts=line.split()
        if len(parts)==4 and parts[3].startswith("pqv4_size_"):sizes[parts[3][10:]]=int(parts[1])
    paths=graph(B/"Native.ci")
    # Reserve is a qualification allowance, NOT a measured RTOS overhead.
    maximum=paths["pqv4_native"]["known_frames_sum_bytes"]
    stack=49152;platform_reserve=6144
    baseline=json.loads((ROOT/"build/s31-probe/results.json").read_text())
    roles={}
    text=sum(o["text"] for o in objects if not o["source"].endswith("Sizes.cpp"))
    fixed=sizes["core"]+sizes["worker"]
    for role in ["helmet","paw"]:
        old=baseline["roles"][role]["baseline"]
        tail=old["contiguous_linker_tail_before_runtime_allocations_bytes"]
        roles[role]={"v3_baseline_static_data_bss":old["static_data_bss_bytes"],"v3_baseline_contiguous_tail":tail,
                     "core_worker_stack_static_reservation":fixed+stack,
                     "conservative_unlinked_tail_after_all_core_text_and_6k_reserve":tail-fixed-stack-text-platform_reserve}
    report={"scope":"UNLINKED S31 compiler static estimates; no platform activation, runtime or firmware image",
      "compiler":run([TOOLS/"riscv32-esp-elf-gcc","--version"],"compiler").splitlines()[0],
      "architecture_response_sha256":hashlib.sha256((response/"cflags").read_bytes()).hexdigest(),
      "sizes":sizes,"objects":objects,"frames":frames,"native_call_paths":paths,
      "recommended_future_worker_stack":stack,"platform_stack_allowance":platform_reserve,
      "known_native_maximum_path":maximum,"stack_margin_after_allowance":stack-maximum-platform_reserve,
      "unlinked_total_text_estimate":text,"roles":roles,
      "hardware_high_water_heap_timings_watchdog_radio_uart":"HARDWARE MEASUREMENT REQUIRED"}
    (B/"report.json").write_text(json.dumps(report,indent=2)+"\n")
    print(json.dumps({k:report[k] for k in ["sizes","known_native_maximum_path","stack_margin_after_allowance","unlinked_total_text_estimate","roles"]},indent=2))
if __name__=="__main__":main()
