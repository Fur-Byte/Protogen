#!/usr/bin/env python3
"""Analyze the same portable wireless modules with Clang Static Analyzer.

First configure/build scripts/test-wireless.sh with CMAKE_EXPORT_COMPILE_COMMANDS
(as the script does). CXX selects clang++; no radio stubs or key material needed.
"""
import json
import os
from pathlib import Path
import shlex
import subprocess

def main():
    root=Path(__file__).resolve().parents[1]
    commands=json.loads((root/'build/wireless-test/compile_commands.json').read_text())
    compiler=os.environ.get('CXX','clang++')
    results=[]
    for entry in commands:
        path=Path(entry['file'])
        if path.parent!=root/'firmware/esp32/src':continue
        args=entry.get('arguments') or shlex.split(entry['command'])
        selected=[];skip=False
        for arg in args[1:]:
            if skip:skip=False;continue
            if arg=='-o':skip=True;continue
            if arg=='-c' or arg.startswith(('-fsanitize=','-fno-sanitize-')):continue
            selected.append(arg)
        run=subprocess.run([compiler,'--analyze','-Xanalyzer','-analyzer-output=text',*selected],
                           cwd=entry['directory'],capture_output=True,text=True)
        results.append({'file':str(path.relative_to(root)), 'exit_code':run.returncode,
                        'diagnostics':run.stdout+run.stderr})
    (root/'build/wireless-analysis.json').write_text(json.dumps(results,indent=2)+'\n')
    okay=bool(results) and all(r['exit_code']==0 and not r['diagnostics'].strip() for r in results)
    print(f"{'PASS' if okay else 'FAIL'}: Clang Static Analyzer, {len(results)} wireless source modules; see build/wireless-analysis.json")
    return 0 if okay else 1
if __name__=='__main__':raise SystemExit(main())
