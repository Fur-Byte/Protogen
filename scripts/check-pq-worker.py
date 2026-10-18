#!/usr/bin/env python3
"""Isolated ML-KEM worker qualification. No production source edits or uploads.

Requires the pinned ESP32 baseline build and check-mlkem-preflight.py inputs.
Outputs are ignored, separate ELF files; app_main never invokes the probe.
"""
import ctypes
import datetime
import importlib.util
import json
import os
from pathlib import Path
import re
import shlex
import shutil
import statistics
import subprocess
import tarfile
import time
from host_support import non_pie_flags

ROOT = Path(__file__).resolve().parents[1]
B = ROOT / 'build/pq-worker'
T = ROOT / 'scripts/tests/pq-worker'
spec = importlib.util.spec_from_file_location('preflight', ROOT/'scripts/check-mlkem-preflight.py')
pre = importlib.util.module_from_spec(spec)
spec.loader.exec_module(pre)
run = pre.run
CC = ROOT/'build/platformio/packages/toolchain-riscv32-esp/bin/riscv32-esp-elf-gcc'
CXX = CC.with_name('riscv32-esp-elf-g++')
SIZE = CC.with_name('riscv32-esp-elf-size')
NM = CC.with_name('riscv32-esp-elf-nm')
NINJA = ROOT/'build/platformio/packages/tool-ninja/ninja'
SOURCE = pre.BUILD / ('mlkem-native-'+pre.PINS['commit'])


def frames(path):
    d = {}
    for line in path.read_text().splitlines():
        label, size, kind = line.split('\t')
        if kind not in ('static', 'dynamic,bounded'):
            raise RuntimeError('Unbounded frame '+label)
        match=re.match(r'^.*?:\d+:\d+:(.*)$',label)
        if not match:raise RuntimeError('Unexpected GCC stack label')
        d[match[1]] = {'bytes':int(size), 'kind':kind}
    return d


def object_record(obj):
    sizes = list(map(int,run([str(SIZE),str(obj)]).splitlines()[1].split()[:3]))
    return {'text':sizes[0], 'data':sizes[1], 'bss':sizes[2], 'frames':frames(obj.with_suffix('.su'))}


def call_paths(paths):
    """Conservative sum of known GCC frames; external/platform frames unknown."""
    nodes,edges={},{}
    for path in paths:
        for line in path.read_text().splitlines():
            m=re.match(r'node: \{ title: "([^"]+)" label: "([^"\\]+).*?\\n(\d+) bytes',line)
            if m:nodes[m[1]]=(m[2],int(m[3]))
            m=re.match(r'edge: \{ sourcename: "([^"]+)" targetname: "([^"]+)"',line)
            if m:edges.setdefault(m[1],set()).add(m[2])
    def visit(n,chain=()):
        if n in chain:raise RuntimeError('Recursion in static call graph')
        name,size=nodes.get(n,(n,0))
        child=max((visit(v,chain+(n,)) for v in edges.get(n,())),default=(0,[]))
        return size+child[0],[(name,size)]+child[1]
    return {nodes[n][0]:{'known_frames_sum_bytes':visit(n)[0],'path':visit(n)[1]}
            for n in nodes if re.fullmatch(r'(?:(?:pqqual|pqarena|pq768)_(?:keypair|enc|dec)|qual_(?:keygen|enc|dec))',nodes[n][0])}


def target_flags():
    return ['@'+str(ROOT/'build/esp32/helmet/toolchain/cflags'),'-Og',
            '-fno-shrink-wrap','-fstrict-volatile-bitfields','-fno-jump-tables',
            '-fno-tree-switch-conversion','-ffunction-sections','-fdata-sections',
            '-Wall','-Wextra','-Werror','-fstack-usage','-fcallgraph-info=su']


def require(condition, message):
    """Qualification checks must execute even under Python optimization."""
    if not condition:
        raise RuntimeError(message)


def direct_vectors(lib):
    api=ctypes.CDLL(str(lib)); ptr=ctypes.POINTER(ctypes.c_ubyte)
    for name, n in [('keygen',3),('enc',4),('dec',3)]:
        f=getattr(api,'qual_'+name); f.argtypes=[ptr]*n; f.restype=ctypes.c_int
    def buf(v): return (ctypes.c_ubyte*len(v)).from_buffer_copy(v)
    def hx(s): return buf(bytes.fromhex(s))
    counts={'keygen':0,'encaps':0,'decaps':0}
    for directory in sorted((pre.BUILD/'vectors').iterdir()):
        prompt=json.loads((directory/'prompt.json').read_text())
        expected=json.loads((directory/'expectedResults.json').read_text())
        groups={g['tgId']:{t['tcId']:t for t in g['tests']} for g in expected['testGroups']}
        for g in prompt['testGroups']:
            if g['parameterSet']!='ML-KEM-1024': continue
            fn=g.get('function','keyGen')
            if fn not in ('keyGen','encapsulation','decapsulation'): continue
            for case in g['tests']:
                exp=groups[g['tgId']][case['tcId']]
                pk,sk,ct,ss=[(ctypes.c_ubyte*n)() for n in (1568,3168,1568,32)]
                context=f'{directory.name} group {g["tgId"]} case {case["tcId"]}'
                if fn=='keyGen':
                    require(api.qual_keygen(pk,sk,hx(case['d']+case['z']))==0,
                            'Key generation failed: '+context)
                    require(bytes(pk).hex().upper()==exp['ek'] and bytes(sk).hex().upper()==exp['dk'],
                            'Key generation vector mismatch: '+context)
                    counts['keygen']+=1
                elif fn=='encapsulation':
                    require(api.qual_enc(ct,ss,hx(case['ek']),hx(case['m']))==0,
                            'Encapsulation failed: '+context)
                    require(bytes(ct).hex().upper()==exp['c'] and bytes(ss).hex().upper()==exp['k'],
                            'Encapsulation vector mismatch: '+context)
                    counts['encaps']+=1
                else:
                    if 'dk' not in case:
                        require(api.qual_keygen(pk,sk,hx(case['d']+case['z']))==0,
                                'Decapsulation fixture key generation failed: '+context)
                    else: sk=hx(case['dk'])
                    require(api.qual_dec(ss,hx(case['c']),sk)==0,
                            'Decapsulation failed: '+context)
                    require(bytes(ss).hex().upper()==exp['k'],
                            'Decapsulation vector mismatch: '+context)
                    counts['decaps']+=1
    require(sum(counts.values())==105, f'Expected 105 vectors; checked {sum(counts.values())}')
    timings={}
    coins=buf(os.urandom(64)); message=buf(os.urandom(32))
    samples={k:[] for k in ('keygen','encaps','decaps')}
    for _ in range(200):
        for name,fn,args in [('keygen',api.qual_keygen,(pk,sk,coins)),
                             ('encaps',api.qual_enc,(ct,ss,pk,message)),
                             ('decaps',api.qual_dec,(ss,ct,sk))]:
            start=time.perf_counter_ns()
            require(fn(*args)==0, 'Benchmark operation failed: '+name)
            samples[name].append((time.perf_counter_ns()-start)/1e6)
    for name,s in samples.items():timings[name]={'mean_ms':statistics.mean(s),'max_ms':max(s),'iterations':200}
    return api,{'vectors':counts,'vectors_total':105,'host_ms':timings,
                'limitation':'40 validation fixtures remain covered by native default preflight only'}


def allocation_faults(api):
    api.qual_peak.restype=ctypes.c_size_t
    api.qual_attempts.restype=ctypes.c_size_t
    pk,sk,ct,ss,coins,m=[(ctypes.c_ubyte*n)() for n in (1568,3168,1568,32,64,32)]
    peaks={};failures=0
    for name,fn,args in [('keygen',api.qual_keygen,(pk,sk,coins)),
                         ('encaps',api.qual_enc,(ct,ss,pk,m)),
                         ('decaps',api.qual_dec,(ss,ct,sk))]:
        require(fn(*args)==0, 'Arena baseline operation failed: '+name)
        peaks[name]=api.qual_peak(); attempts=api.qual_attempts()
        for fault in range(1,attempts+1):
            api.qual_fail_at(fault)
            require(fn(*args)==-2, f'Arena allocation fault {fault} was not reported: {name}')
            failures+=1
        api.qual_fail_at(0)
        require(fn(*args)==0, 'Arena recovery operation failed: '+name)
        if name=='keygen':
            require(api.qual_enc(ct,ss,pk,m)==0, 'Arena fixture encapsulation failed')
    return {'peak_bytes':peaks, 'allocation_fault_cases':failures}


def section_record(elf):
    sections={}
    for line in run([str(SIZE),'-A',str(elf)]).splitlines():
        cols=line.split()
        if len(cols)==3 and cols[0].startswith('.'):
            sections[cols[0]]=int(cols[1])
    symbols={}
    for line in run([str(NM),'-n',str(elf)]).splitlines():
        cols=line.split()
        if len(cols)==3 and cols[2] in ('_heap_start','_bss_end'):
            symbols[cols[2]]=int(cols[0],16)
    return {'sections':sections,'symbols':symbols,
            'static_data_bss':sum(sections.get(n,0) for n in ('.dram0.data','.dram0.bss')),
            'linker_contiguous_tail_before_runtime_allocations':0x3fcce710-symbols['_heap_start']}


def relink(native_obj):
    result={}
    for role in ('helmet','paw'):
        directory=ROOT/f'build/esp32/{role}'
        sdk=json.loads((directory/'config/sdkconfig.json').read_text())
        entry=next(c for c in json.loads((directory/'compile_commands.json').read_text()) if c['file'].endswith('/main.cpp'))
        flags=shlex.split(entry['command'])
        # Compile commands contain only compiler arguments, never a shell command.
        del flags[flags.index('-o'):flags.index('-o')+2]
        del flags[flags.index('-c'):flags.index('-c')+2]
        flags+=['-Wall','-Wextra','-Werror','-fstack-usage','-I'+str(T),'-I'+str(SOURCE/'mlkem'),
                '-DMLK_CONFIG_FILE="native_config.h"']
        kem=B/f'{role}-kem.o'
        run(flags+['-c',str(T/'kem.cpp'),'-o',str(kem)],B/f'{role}-kem.log',directory)
        link=run([str(NINJA),'-C',str(directory),'-t','commands','protogen_wireless.elf']).splitlines()[-1]
        # Ninja link command is "compiler ... && :". No shell execution here.
        args=shlex.split(link.strip().removeprefix(': && ').split(' && ')[0])
        # PlatformIO compiles application sources directly instead of CMake's
        # libsrc.a/dummy object. All ESP-IDF archives remain those of the baseline.
        args=[a for a in args if not a.endswith('project_elf_src_esp32c3.c.obj')]
        expanded=[]
        for a in args:
            expanded.extend(map(str,sorted((directory/'src').glob('*.o')))) if a=='esp-idf/src/libsrc.a' else expanded.append(a)
        args=expanded
        original=section_record(directory/'firmware.elf')
        result[role]={'baseline':original,'cpu_mhz':sdk['ESP_DEFAULT_CPU_FREQ_MHZ'],
          'sdk_stacks':{k:v for k,v in sdk.items() if 'STACK' in k and isinstance(v,int)},
          'main_stack_unchanged':sdk['ESP_MAIN_TASK_STACK_SIZE'],'candidates':{}}
        for stack in (24576,32768):
            obj=B/f'{role}-probe-{stack}.o';elf=B/f'{role}-probe-{stack}.elf'
            run(flags+[f'-DPQ_STACK_BYTES={stack}','-c',str(T/'freertos_probe.cpp'),'-o',str(obj)],B/f'{obj.stem}.log',directory)
            isolated=list(args)
            isolated[isolated.index('-o')+1]=str(elf)
            isolated=[('-Wl,--Map='+str(elf.with_suffix('.map'))) if a.startswith('-Wl,--Map=') else a for a in isolated]
            isolated += [str(obj),str(kem),str(native_obj),'-Wl,-u,pq_qualification_probe',
                         '-Wl,-u,pq_qualification_submit','-Wl,-u,pq_qualification_cancel','-Wl,-u,pq_qualification_take']
            run(isolated,B/f'{obj.stem}-link.log',directory)
            record=section_record(elf)
            record.update(stack_allocation=stack,probe=object_record(obj),
                          static_ram_increase=record['static_data_bss']-original['static_data_bss'],
                          allocated_flash_sections_delta=sum(record['sections'].get(k,0)-original['sections'].get(k,0)
                          for k in ('.flash.text','.flash.rodata','.iram0.text','.dram0.data')))
            result[role]['candidates'][str(stack)]=record
    return result


def main():
    B.mkdir(parents=True,exist_ok=True)
    inputs=sorted(p for p in T.iterdir() if p.is_file())+[Path(__file__),
        ROOT/'scripts/check-mlkem-preflight.py',ROOT/'scripts/tests/mlkem-preflight-pins.json',
        ROOT/'scripts/tests/pq-worker-pins.json']
    initial_hashes={str(p.relative_to(ROOT)):pre.digest(p) for p in inputs}
    for name,sha in pre.PINS['source_sha256'].items():
        if pre.digest(SOURCE/name)!=sha:raise RuntimeError('Native source pin mismatch '+name)
    pins=json.loads((ROOT/'scripts/tests/pq-worker-pins.json').read_text())
    archive=B/'wolfssl.tar.gz'; pre.fetch(pins['archive_url'],archive,pins['archive_sha256'])
    wolf=B/'wolfssl'
    # Use verified archive, not the mutable research clone.
    extracted=B/('wolfssl-'+pins['commit'])
    if not extracted.exists():
        with tarfile.open(archive) as tar:tar.extractall(B,filter='data')
    wolf=extracted
    for name,sha in pins['source_sha256'].items():
        if pre.digest(wolf/name)!=sha:raise RuntimeError('Wolf source pin mismatch '+name)
    config=B/'config'; config.mkdir(exist_ok=True)
    shutil.copyfile(T/'wolf_settings.h',config/'user_settings.h')
    report={'scope':'qualification only; unchanged production v3; no upload',
            'date_utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),
            'native_commit':pre.PINS['commit'],'wolf_commit':pins['commit'],'target_compiler':run([str(CC),'--version']).splitlines()[0]}
    native_flags=['-I'+str(T),'-I'+str(SOURCE/'mlkem'),'-DMLK_CONFIG_FILE="native_config.h"']
    host_flags=['-O2','-Wall','-Wextra','-Werror']
    san=['-g','-fno-omit-frame-pointer','-fsanitize=address,undefined','-fno-sanitize-recover=all']
    nobj=B/'native-host.o'
    run(['cc']+host_flags+san+native_flags+['-c',str(SOURCE/'mlkem/mlkem_native.c'),'-o',str(nobj)],B/'native-host.log')
    stress=B/'stress'
    run(['c++','-std=c++17']+host_flags+san+native_flags+non_pie_flags()+['-pthread',str(T/'stress.cpp'),str(T/'kem.cpp'),str(nobj),'-o',str(stress)],B/'stress-build.log')
    report['stress_asan_ubsan']=json.loads(run([str(stress)],B/'stress.log'))
    report['entropy_fault_cases']=2
    acvp=B/'acvp-pct-sanitized'
    run(['cc']+host_flags+san+non_pie_flags()+native_flags+
        [str(SOURCE/'mlkem/mlkem_native.c'),str(SOURCE/'test/notrandombytes/notrandombytes.c'),
         str(SOURCE/'test/acvp/acvp_mlkem.c'),'-o',str(acvp)],B/'acvp-pct-build.log')
    report['native_pct_acvp']=pre.vectors(acvp)
    report['native_pct_acvp_total']=sum(report['native_pct_acvp'].values())
    native_library=B/'native.so'
    run(['cc']+host_flags+['-fPIC','-shared']+native_flags+[str(T/'native_shim.c'),str(SOURCE/'mlkem/mlkem_native.c'),'-o',str(native_library)],B/'native-shared-build.log')
    _,report['native_host']=direct_vectors(native_library)
    report['native_target']={}
    for name,extra in [('stack-pct',native_flags),('stack-no-pct',['-I'+str(SOURCE/'mlkem'),'-DMLK_CONFIG_PARAMETER_SET=1024']),
                       ('768-comparison-only',['-I'+str(SOURCE/'mlkem'),'-DMLK_CONFIG_PARAMETER_SET=768',
                            '-DMLK_CONFIG_NAMESPACE_PREFIX=pq768','-DMLK_CONFIG_KEYGEN_PCT']),
                       ('arena-pct',['-I'+str(T),'-I'+str(SOURCE/'mlkem'),'-DMLK_CONFIG_FILE="arena_config.h"'])]:
        obj=B/(name+'.o')
        run([str(CC)]+target_flags()+extra+['-c',str(SOURCE/'mlkem/mlkem_native.c'),'-o',str(obj)],B/(name+'.log'))
        report['native_target'][name]=object_record(obj)
        report['native_target'][name]['known_call_paths']=call_paths([obj.with_suffix('.ci')])
    report['isolated_links']=relink(B/'stack-pct.o')
    arena=B/'arena.so'
    aflags=['-I'+str(T),'-I'+str(SOURCE/'mlkem'),'-DMLK_CONFIG_FILE="arena_config.h"']
    run(['cc']+host_flags+['-fPIC','-shared']+aflags+[str(T/'arena_shim.c'),str(SOURCE/'mlkem/mlkem_native.c'),'-o',str(arena)],B/'arena-build.log')
    api,record=direct_vectors(arena); report['arena_host']=record
    report['arena_host'].update(allocation_faults(api))
    wflags=['-DWOLFSSL_USER_SETTINGS','-I'+str(config),'-I'+str(wolf)]
    files=[wolf/'wolfcrypt/src'/n for n in ('wc_mlkem.c','wc_mlkem_poly.c','sha3.c')]
    library=B/'wolf.so'
    run(['cc']+host_flags+['-fPIC','-shared']+wflags+[str(T/'wolf_shim.c')]+list(map(str,files))+['-o',str(library)],B/'wolf-host.log')
    wapi,wrecord=direct_vectors(library); wapi.qual_context_size.restype=ctypes.c_size_t
    wrecord['host_context_bytes']=wapi.qual_context_size(); report['wolf_host']=wrecord
    report['wolf_target']={}
    for source in files+[T/'wolf_shim.c']:
        obj=B/(source.stem+'-rv32.o')
        run([str(CC)]+target_flags()+wflags+['-c',str(source),'-o',str(obj)],B/(source.stem+'-rv32.log'))
        report['wolf_target'][source.name]=object_record(obj)
    report['wolf_known_call_paths']=call_paths([B/(p.stem+'-rv32.ci') for p in files+[T/'wolf_shim.c']])
    report['backend_sanitizer_stress']={}
    for name,shim,extra,sources in [('arena',T/'arena_shim.c',aflags,[SOURCE/'mlkem/mlkem_native.c']),
                                   ('wolf',T/'wolf_shim.c',wflags,files)]:
        binary=B/(name+'-sanitizer-test')
        run(['cc']+host_flags+san+non_pie_flags()+(['-DQUAL_ARENA'] if name=='arena' else [])+extra+[str(shim),str(T/'primitive_stress.c')]+
            list(map(str,sources))+['-o',str(binary)],B/(name+'-sanitizer-build.log'))
        report['backend_sanitizer_stress'][name]=run([str(binary)],B/(name+'-sanitizer.log')).strip()
    guard=B/'guarded-stack'
    run(['cc']+host_flags+native_flags+['-pthread',str(T/'guarded_stack.c'),str(SOURCE/'mlkem/mlkem_native.c'),'-o',str(guard)],B/'guarded-stack-build.log')
    report['host_guarded_stack']={}
    for stack in (24576,32768):
        page=os.sysconf("SC_PAGESIZE");allocated=(stack+page-1)//page*page
        process=subprocess.run([str(guard),str(allocated)],cwd=B,text=True,capture_output=True,timeout=30)
        report['host_guarded_stack'][str(stack)]={'exit_code':process.returncode,'requested_stack_bytes':stack,'allocated_stack_bytes':allocated,
            'measurements':json.loads(process.stdout) if process.returncode==0 else None,
            'limitation':'POSIX host pthread startup and written canary extent; not MCU high-water or reserved-frame depth'}
        if process.returncode not in (0,-11):raise RuntimeError('Unexpected guarded stack failure')
    clang=os.environ.get('MLKEM_CLANG') or shutil.which('clang')
    if not clang:
        packaged=ROOT.parent/'toolchains/clang/usr/lib/llvm-19/bin/clang'
        clang=str(packaged) if packaged.is_file() else None
    if clang:
        run([clang,'--analyze','-Xanalyzer','-analyzer-output=text','-std=c++17','-Wall','-Wextra','-Werror']+native_flags+[str(T/'kem.cpp'),str(T/'stress.cpp')],B/'worker-analyzer.log')
        if (B/'worker-analyzer.log').read_text().strip():raise RuntimeError('Analyzer findings')
        report['worker_analyzer']=run([clang,'--version']).splitlines()[0]+'; no diagnostics; host core/harness only'
    else: raise RuntimeError('Clang analyzer unavailable; qualification check incomplete')
    report['limits']={'physical_sram_bytes':409600,'cache_excluded_diram_bytes':393216,
       'linker_app_diram_region_bytes':321296,'normal_link_object_rv32_bytes':2664,
       'worker_slot_bytes':6336,'queue_depth':1,'inflight_operations':1,
       'candidate_stack_bytes':32768,'stack_approval':'pending measured MCU high-water',
       'wearer_io_stack_bytes':6144,'interrupt_stack_bytes':1536,
       'heap_delta_creation':'static worker buffers already reserved at boot; dynamic IDF/libc overhead unmeasured',
       'startup_reserved_region_later_released_bytes':71920,'rtc_optional_region_less_reserved_bytes':8168,
       'actual_idf_main_stack_macro_bytes':12800,
       'configured_main_stack_bytes':12288}
    report['v3_regression']={'run_by_this_tool':False,'separate_command':'bash scripts/test-wireless.sh',
        'task_run_result':'See qualification document for separately observed results; this tool does not assert a regression pass'}
    report['input_hashes']={str(p.relative_to(ROOT)):pre.digest(p) for p in inputs}
    if report['input_hashes']!=initial_hashes:raise RuntimeError('Qualification inputs changed during run')
    report['runtime_mcu']={k:None for k in ['heap_before','heap_after','minimum_heap_keygen','minimum_heap_encaps','minimum_heap_decaps','largest_heap_block','stack_high_water','watchdogs','uart_latency','radio_latency','operation_times']}
    report['verdict']='ML-KEM-1024 NOT SAFE TO INTEGRATE'
    report['reason']='MCU stack high-water, post-Wi-Fi heap fragmentation, operation timing and scheduling/watchdog gates have not been measured; static/host evidence cannot approve integration.'
    (B/'results.json').write_text(json.dumps(report,indent=2)+'\n')
    print('PASS isolated qualification tests; MCU integration remains NO-GO')
    print('Evidence: build/pq-worker/results.json')

if __name__=='__main__':main()
