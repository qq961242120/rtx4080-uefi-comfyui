#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Compile UEFI COFF objects locally; optionally link with an existing lld-link.
Never deploys, opens SSH, writes boot entries, or executes the EFI output.
"""
from pathlib import Path
import argparse,hashlib,json,os,shutil,subprocess,sys
r=Path(__file__).resolve().parent
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--objects-only',action='store_true')
p.add_argument('--host-test-profile',action='store_true',help='Synthetic GUID for compile checks only; never deploy the output')
p.add_argument('--clang',default='clang')
p.add_argument('--linker',default='lld-link',help='Existing LLVM lld-link path')
a=p.parse_args();os.chdir(r)
if not a.host_test_profile and not (r/'local/boot_profile.h').is_file():p.error('Missing local/boot_profile.h; see boot_profile.example.h and README. Do not publish your local profile.')
b=r/'build';coff=b/'coff';coff.mkdir(parents=True,exist_ok=True)
sources=['port','compat','checks','profile','sha256','runtime','format','vendor/unlock_610','vendor/prep_610','vendor/layout_610','vendor/vbios_610','vendor/gfw_eval','app','boot_io','display_release']
flags=[a.clang,'--target=x86_64-pc-windows-msvc','-std=c11','-O2','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-Wno-misleading-indentation','-D__KERNEL__','-DGU_UEFI_PORT','-DGU_AD103_TRANSACTION','-DGU_ENTRY_LATCHED_SS1_FULL','-Icompat','-Ivendor/edk2/Include','-Ivendor/edk2/Include/X64','-ffreestanding','-fno-builtin','-fno-stack-protector','-mno-stack-arg-probe','-mno-red-zone','-fshort-wchar','-fstack-usage']
if a.host_test_profile:flags+=['-DGU_HOST_TEST_PROFILE']
objects=[]
for s in sources:
 obj=coff/(s.replace('/','_')+'.obj');subprocess.run(flags+['-c',s+'.c','-o',str(obj)],check=True);objects.append(obj)
manifest={'objects_compiled':len(objects),'synthetic_profile':a.host_test_profile,'hardware_executed':False,'objects':{x.name:hashlib.sha256(x.read_bytes()).hexdigest() for x in objects}}
if not a.objects_only:
 if not shutil.which(a.linker):p.error('COFF objects built; linker unavailable. Supply --linker or use --objects-only.')
 subprocess.run([a.linker,'/nologo','/subsystem:efi_application','/entry:UefiMain','/machine:x64','/nodefaultlib','/incremental:no','/fixed:no','/dynamicbase:no','/nxcompat:no','/brepro','/out:'+str(b/'AD103-EXPERIMENTAL.EFI')]+list(map(str,objects)),check=True)
 subprocess.run([sys.executable,'tools/verify_experimental.py'],check=True)
 manifest['linked']=True
(b/'public-build.json').write_text(json.dumps(manifest,indent=2)+'\n');print(json.dumps({k:v for k,v in manifest.items() if k!='objects'}))
