#!/usr/bin/env python3
"""Compile/relink an UNCALLED S31 UART research adapter. Never upload or activate v4.

Requires the pinned existing --preview baseline and SDK. All outputs are ignored.
No board mapping is supplied; default profiles are rejected before driver I/O.
"""
import argparse
import importlib.util
import json
import os
from pathlib import Path
import shlex
import subprocess

ROOT=Path(__file__).resolve().parents[1]
B=ROOT/'build/s31-uart'
PLATFORM=ROOT/'firmware/esp32/platform_research'

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--preview',action='store_true')
    if not parser.parse_args().preview:parser.error('--preview is required; no production qualification')
    B.mkdir(parents=True,exist_ok=True)
    spec=importlib.util.spec_from_file_location('s31_probe',ROOT/'scripts/check-s31-preflight.py')
    q=importlib.util.module_from_spec(spec);spec.loader.exec_module(q)
    q.ENV=dict(os.environ)
    before=q.production_hashes()
    pins=json.loads((ROOT/'scripts/tests/s31-preflight-pins.json').read_text())
    for name,expected in pins['source_sha256'].items():
        if q.digest(q.FRAMEWORK/name)!=expected:raise RuntimeError('SDK source mismatch: '+name)
    directory=q.BUILD/'helmet'
    entries=json.loads((directory/'compile_commands.json').read_text())
    entry=next(e for e in entries if e['file'].endswith('/main.cpp'))
    flags=q.compiler_flags(entry)
    config=json.loads((directory/'config/sdkconfig.json').read_text())
    if config.get('IDF_TARGET')!='esp32s31':raise RuntimeError('Expected existing S31 preview baseline')
    source=PLATFORM/'S31Uart.cpp';obj=B/'S31Uart.o'
    extra=['-I'+str(PLATFORM),'-Wall','-Wextra','-Werror','-fstack-usage','-fcallgraph-info=su']
    # Missing research opt-in is a negative compile, not a production fallback.
    denied=subprocess.run(flags+extra+['-fsyntax-only',str(source)],cwd=directory,capture_output=True,text=True)
    (B/'guard-denied.log').write_text(denied.stdout+denied.stderr)
    if not denied.returncode or 'S31_UART_RESEARCH_ONLY_NO_PRODUCTION_ADAPTER' not in denied.stderr:
        raise RuntimeError('UART research activation guard did not reject default compilation')
    compiled=q.run(flags+extra+['-DPROTOGEN_S31_UART_RESEARCH=1','-c',source,'-o',obj],B/'compile.log',directory)
    if 'warning:' in compiled.lower():raise RuntimeError('UART research compiler warning')
    # Reuse the existing official link command, replacing only output/map and
    # rooting one uncalled adapter function. Original baseline files stay intact.
    commands=q.run([q.NINJA,'-C',directory,'-t','commands','protogen_s31_baseline_probe.elf'],B/'commands.log')
    link=commands.splitlines()[-1]
    args=shlex.split(link.strip().removeprefix(': && ').split(' && ')[0])
    elf=B/'uart-research.elf';args[args.index('-o')+1]=str(elf)
    args=['-Wl,--Map='+str(elf.with_suffix('.map')) if a.startswith('-Wl,--Map=') else a for a in args]
    args += [str(obj),'-Wl,-u,protogen_s31_uart_research_probe']
    q.run(args,B/'link.log',directory)
    baseline=q.sections(directory/'protogen_s31_baseline_probe.elf')
    linked=q.sections(elf)
    # Type-only S31 ABI evidence. ESP_PLATFORM is absent ONLY from this isolated
    # compiler invocation; the source guard stays untouched and no v4 code links.
    types=B/'types.cpp'
    types.write_text('#include "Core.hpp"\n#include "Worker.hpp"\n#include "AuthenticatedUart.hpp"\n#include "BoardProfile.hpp"\n'
      'extern "C" {\n'
      'char research_core[sizeof(ProtogenV4::Core)];\n'
      'char research_worker[sizeof(ProtogenV4::SlotWorker)];\n'
      'char research_material[sizeof(ProtogenV4::Material)];\n'
      'char research_exchange[sizeof(ProtogenV4::Exchange)];\n'
      'char research_result[sizeof(ProtogenV4::Result)];\n'
      'char research_uart[sizeof(ProtogenResearch::AuthenticatedUart)];\n'
      'char research_uart_endpoint[sizeof(UartSecurity::Endpoint)];\n'
      'char research_board_profile[sizeof(ProtogenResearch::BoardProfile)];\n}\n')
    typeobj=B/'types.o'
    q.run([q.CXX,'@'+str(directory/'toolchain/cxxflags'),'-std=c++17','-Wall','-Wextra','-Werror',
      '-I'+str(ROOT/'firmware/esp32/protocol_v4'),'-I'+str(PLATFORM),
      '-I'+str(ROOT/'firmware/PROT_OS7_MAX7219/src/HelmetProtocol'),'-c',types,'-o',typeobj],B/'types.log',directory)
    symbols=q.run([q.NM,'-S','--radix=d',typeobj],B/'types-symbols.log')
    sizes={parts[3][9:]:int(parts[1]) for line in symbols.splitlines()
           if len(parts:=line.split())==4 and parts[3].startswith('research_')}
    if q.production_hashes()!=before:raise RuntimeError('Firmware changed during UART research validation; rerun when stable')
    for name,expected in pins['source_sha256'].items():
        if q.digest(q.FRAMEWORK/name)!=expected:raise RuntimeError('SDK source changed: '+name)
    frames={}
    for line in obj.with_suffix('.su').read_text().splitlines():
        parts=line.rsplit('\t',2)
        if len(parts)==3:frames[parts[0]]={'bytes':int(parts[1]),'kind':parts[2]}
    report={'scope':'UNCALLED preview UART relink and UNLINKED type-only S31 v4 ABI; NOT production or hardware qualification',
      'pins':pins,'baseline':baseline,'uart_relink':linked,'s31_abi_sizes':sizes,
      'activation_guard_negative_compile':True,'no_profile_or_pins_supplied':True,
      'v4_not_linked_into_app':True,'production_firmware_unchanged_during_run':True,
      'inputs':{str(p.relative_to(ROOT)):q.digest(p) for p in [Path(__file__),*sorted(PLATFORM.glob('*'))] if p.is_file()},
      'uart_compiler_frames':frames,'hardware_metrics':None}
    (B/'report.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps({'scope':report['scope'],'s31_abi_sizes':sizes,
      'uncalled_adapter_static_data_bss_delta':linked['static_data_bss_bytes']-baseline['static_data_bss_bytes'],
      'uart_compiler_frames':frames,'hardware_metrics':None},indent=2))
if __name__=='__main__':main()
