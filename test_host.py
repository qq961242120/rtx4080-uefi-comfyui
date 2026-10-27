#!/usr/bin/env python3
"""Isolated x64 hosted tests. All EFI/PCI/MMIO/DMA callbacks are fakes.
No SSH, disk boot changes, or hardware execution. Private fixture inputs are supplied locally via GU_FIXTURE_ROOT.
"""
import argparse,json,subprocess,time,os,sys,hashlib
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--sanitize',action='store_true');p.add_argument('--suite',choices=['all','guards','efi_transaction','display_release','app_flow','boot_io'],default='all');a=p.parse_args()
r=Path(__file__).resolve().parent;os.chdir(r)
n=Path(os.environ.get('GU_FIXTURE_ROOT','fixtures'));cap=n/'UEFI-Captures/third-boot-v4-20261026';assets=n/'offline-stock-assets/derived'
b=r/'build'/('asan' if a.sanitize else 'host');b.mkdir(parents=True,exist_ok=True)
f=['clang','-arch','x86_64','-std=c11','-O1','-g','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-Wno-misleading-indentation','-U_FORTIFY_SOURCE','-D_FORTIFY_SOURCE=0','-D__KERNEL__','-DGU_UEFI_PORT','-DGU_HOST_TEST_PROFILE','-DGU_AD103_TRANSACTION','-DGU_ENTRY_LATCHED_SS1_FULL','-fshort-wchar','-Icompat','-Ivendor/edk2/Include','-Ivendor/edk2/Include/X64']
if a.sanitize:f+=['-fsanitize=address,undefined','-fno-omit-frame-pointer']
src=['port.c','compat.c','checks.c','profile.c','sha256.c','vendor/unlock_610.c','vendor/prep_610.c','vendor/layout_610.c','vendor/vbios_610.c','vendor/gfw_eval.c']
suites={'guards':(src,[cap/'physical-prom.bin',assets]),'efi_transaction':(src,[cap/'physical-prom.bin',assets]),'display_release':(['sha256.c','format.c'],[cap]),'app_flow':([],[]),'boot_io':(['sha256.c','format.c'],[n/'offline-stock-assets/bootmgfw.efi',assets])}
def source_hashes():
 return {str(p.relative_to(r)):hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted(r.rglob('*')) if p.is_file() and p.suffix in ('.c','.h') and 'build' not in p.relative_to(r).parts}
results=[]
start_sources=source_hashes()
for name,(sources,args) in suites.items():
 if a.suite!='all' and a.suite!=name:continue
 exe=b/name;cmd=f+['tests/test_'+name+'.c']+sources+['-o',str(exe)]
 start=time.time();c=subprocess.run(cmd,capture_output=True,text=True)
 rec=dict(suite=name,compile_command=cmd,compile_exit=c.returncode,compile_stdout=c.stdout,compile_stderr=c.stderr)
 if c.returncode:
  results.append(rec);print(name,'COMPILE FAILED',c.stderr);break
 env=dict(os.environ);env['ASAN_OPTIONS']='detect_leaks=0:halt_on_error=1';env['UBSAN_OPTIONS']='halt_on_error=1:print_stacktrace=1'
 cmd=[str(exe)]+[str(x) for x in args];q=subprocess.run(cmd,capture_output=True,text=True,env=env)
 (b/(name+'.log')).write_text(q.stdout+q.stderr)
 rec.update(run_command=cmd,exit_code=q.returncode,seconds=round(time.time()-start,2),executable_sha256=hashlib.sha256(exe.read_bytes()).hexdigest(),last_lines=(q.stdout+q.stderr).splitlines()[-3:]);results.append(rec)
 print(name,q.returncode,' | '.join(rec['last_lines']),flush=True)
 if q.returncode:break
out=dict(sanitized=a.sanitize,architecture='x86_64 Mach-O under Rosetta on Mac',all_hardware_callbacks_simulated=True,hardware_execution=False,source_hashes=start_sources,source_unchanged=source_hashes()==start_sources,leak_sanitizer=False,results=results,success=source_hashes()==start_sources and bool(results) and all(x.get('exit_code')==0 for x in results));(b/('results-'+a.suite+'.json')).write_text(json.dumps(out,indent=2)+'\n');sys.exit(0 if out['success'] else 1)
