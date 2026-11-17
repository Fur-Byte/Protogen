#!/usr/bin/env python3
"""Compare real legacy and Audio V2 firmware against the frozen P7 protocol."""
import argparse
import json
from pathlib import Path
import subprocess

from audio.corpus import SCENARIOS, signal

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT/"build/p7-evidence"


def build():
    for name,flag in (("audio-legacy",""),("audio-v2","-DPROTOGEN_AUDIO_V2=1")):
        subprocess.run(["make","-C",str(ROOT/"scripts/twin"),"BUILD=../../build/"+name,
                        "EXTRA_FLAGS="+flag,"audio-corpus"],check=True,stdout=subprocess.DEVNULL)


def score(samples, labels, executable, output):
    result = subprocess.run([str(executable)],input="\n".join(map(str,samples))+"\n",
                            capture_output=True,text=True,check=True,timeout=90)
    output.write_text(result.stdout)
    rows = [[float(x) for x in line.split()] for line in result.stdout.splitlines()]
    expected = [sum(labels[i-64:i])>=32 for i in range(64,len(labels)+1,64)]
    detected = [bool(row[1]) for row in rows]
    assert len(rows)==len(expected)
    tp = sum(a and b for a,b in zip(expected,detected))*16
    fp = sum(not a and b for a,b in zip(expected,detected))*16
    missed = sum(a and not b for a,b in zip(expected,detected))*16
    onsets,releases=[],[]
    for i in range(len(expected)):
        if expected[i] and (i==0 or not expected[i-1]):
            stop = next((j for j in range(i,len(expected)) if not expected[j]),len(expected))
            found = next((j for j in range(i,stop) if detected[j]),stop)
            onsets.append((found-i)*16)
        if not expected[i] and i>0 and expected[i-1]:
            stop = next((j for j in range(i,len(expected)) if expected[j]),len(expected))
            found = next((j for j in range(i,stop) if not detected[j]),stop)
            releases.append((found-i)*16)
    return {"true_ms":tp,"false_ms":fp,"missed_ms":missed,
            "precision":tp/(tp+fp) if tp+fp else 1,"recall":tp/(tp+missed) if tp+missed else 1,
            "onsets_ms":onsets,"releases_ms":releases,
            "mouth_transitions":sum(a[2]!=b[2] for a,b in zip(rows,rows[1:])),
            "noise_at_8s":rows[499][3],"noise_final":rows[-1][3],
            "runtime":json.loads(result.stderr)}


def accepts(results):
    regular = [v for k,v in results.items() if not k.endswith("ambiguous_modulated_noise")]
    checks={}
    total={}
    for mode in ("legacy","v2"):
        total[mode]={key:sum(v[mode][key] for v in regular) for key in ("true_ms","false_ms","missed_ms")}
        t=total[mode]
        t["precision"]=t["true_ms"]/(t["true_ms"]+t["false_ms"]) if t["true_ms"]+t["false_ms"] else 1
        t["recall"]=t["true_ms"]/(t["true_ms"]+t["missed_ms"]) if t["true_ms"]+t["missed_ms"] else 1
    v2,old=total["v2"],total["legacy"]
    checks["precision"]=v2["precision"]>=.85
    checks["recall"]=v2["recall"]>=.90
    checks["false_open_reduction"]=v2["false_ms"]<=old["false_ms"]*.60
    checks["miss_allowance"]=v2["missed_ms"]<=old["missed_ms"]+.05*(v2["true_ms"]+v2["missed_ms"])
    for key,values in results.items():
        v,old=values["v2"],values["legacy"]
        checks[key+"/onset"]=not v["onsets_ms"] or v["onsets_ms"][0]<=96
        checks[key+"/release"]=not v["releases_ms"] or v["releases_ms"][-1]<=256
        checks[key+"/drops"]=v["runtime"]["dropped"]==old["runtime"]["dropped"]==0
        if key.endswith("sustained_voiced"): checks[key+"/sustain"]=v["recall"]>=.95
        if key.endswith("constant_step"): checks[key+"/stationary"]=v["false_ms"]<=3000
        if key.endswith("impulses"): checks[key+"/impulse"]=v["false_ms"]<=64
        if key.endswith("ambiguous_modulated_noise"): checks[key+"/ambiguity"]=v["false_ms"]<=old["false_ms"]+256
    return total,checks


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--held-out",action="store_true")
    args=parser.parse_args()
    OUT.mkdir(parents=True,exist_ok=True)
    build()
    results={}
    for seed in ((37,53) if args.held_out else (11,19)):
        for name in SCENARIOS:
            samples,labels=signal(name,seed)
            key=f"{seed}-{name}"
            results[key]={mode:score(samples,labels,ROOT/"build"/directory/"audio-corpus",OUT/(key+"-"+mode+".tsv"))
                          for mode,directory in (("legacy","audio-legacy"),("v2","audio-v2"))}
    total,checks=accepts(results)
    report={"protocol":"iteration 1","held_out":args.held_out,"aggregate":total,"checks":checks,"cases":results}
    (OUT/("held-out.json" if args.held_out else "development.json")).write_text(json.dumps(report,indent=2)+"\n")
    print(json.dumps(total))
    failed=[key for key,value in checks.items() if not value]
    if failed:
        print("FAILED: "+", ".join(failed))
        return 1
    print("PASS: frozen audio activity criteria")
    return 0


if __name__=="__main__":
    raise SystemExit(main())
