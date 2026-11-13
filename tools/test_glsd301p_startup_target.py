#!/usr/bin/env python3
"""Hosted-only executable tests of actual target functions and SDK flash guards.

Extracts whole function bodies without rewriting them; hardware/stack seams are
stubbed. This is wiring evidence, not a target voltage or power-cut experiment.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess


def function(text, name):
    match = re.search(r'(?m)^(?:static )?(?:bool|void|unsigned int) ' + name + r'\([^;]*?\)\s*\{', text)
    if not match:
        raise ValueError('missing function ' + name)
    start = text.index('{', match.start())
    # Lex comments and strings so braces in comments cannot truncate a body.
    depth = 0
    for token in re.finditer(r'/\*.*?\*/|//[^\n]*|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|[{}]', text[start:], re.S):
        value = token.group()
        if value == '{': depth += 1
        if value == '}':
            depth -= 1
            if depth == 0:
                return text[match.start():start + token.end()]
    raise ValueError('unbalanced function ' + name)


def generate(root, sdk):
    target = (root/'firmware/glsd301p-ed/glsd301p_telink_target.c').read_text()
    app = (root/'firmware/glsd301p-ed/app_cfg.h').read_text()
    defs = []
    for text, name in ((target,'GLSD301P_STARTUP_NV_MODULE'),(target,'GLSD301P_NV_ITEM_STARTUP'),
                       (app,'VOLTAGE_DETECT_ENABLE')):
        defs.append(re.search(r'^#define\s+'+name+r'\s+[^\n]+', text, re.M).group())
    bodies = '\n'.join(function(target, name) for name in
                       ('glsd_startup_write','glsd_startup_maintenance','user_init'))
    flash = (sdk/'proj/drivers/drv_flash.c').read_text()
    bodies += '\n'+function(flash,'flash_write')+'\n'+function(flash,'flash_erase')
    threshold=re.search(r'^#define\s+BATTERY_SAFETY_THRESHOLD\s+[^\n]+',
                        (sdk/'proj/drivers/drv_hw.h').read_text(),re.M).group()
    defs.append(threshold)
    template = (root/'tests/host/startup_target_harness.c').read_text()
    return template.replace('/* TARGET_DEFINES */','\n'.join(defs)).replace('/* TARGET_BODIES */',bodies)


def run(root, sdk, out):
    out.mkdir(parents=True, exist_ok=True)
    source = out/'startup_target.c'
    source.write_text(generate(root,sdk))
    exe = out/'startup_target'
    subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-Wno-unused-variable',
                    '-Wno-unused-function','-Wno-unused-parameter','-Isrc','-Itests/host',
                    '-I'+str(sdk/'proj/drivers'),str(source),'src/glsd301p_startup.c',
                    '-o',str(exe)], cwd=root, check=True)
    result = subprocess.run([str(exe)],check=True,text=True,capture_output=True)
    evidence = json.loads(result.stdout)
    evidence['target_source_sha256']=hashlib.sha256((root/'firmware/glsd301p-ed/glsd301p_telink_target.c').read_bytes()).hexdigest()
    evidence['sdk_flash_source_sha256']=hashlib.sha256((sdk/'proj/drivers/drv_flash.c').read_bytes()).hexdigest()
    (out/'startup-target-evidence.json').write_text(json.dumps(evidence,sort_keys=True,indent=2)+'\n')
    print(json.dumps(evidence,sort_keys=True))
    return source


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--sdk-root',type=Path,required=True)
    p.add_argument('--out',type=Path,required=True)
    p.add_argument('--mutants',action='store_true')
    a=p.parse_args(); root=Path(__file__).resolve().parents[1]
    source=run(root,a.sdk_root.resolve(),a.out.resolve())
    if a.mutants:
        original=source.read_text()
        mutations={
            'wrong-module':('NV_MODULE_ZCL','NV_MODULE_APP'),
            'missing-voltage':('if (drv_get_adc_data() < BATTERY_SAFETY_THRESHOLD) return false;',''),
            'missing-readback-compare':('memcmp(record, check, sizeof(check)) == 0','true'),
            'unsafe-dma':('!g_uart_service.inflight_active','true'),
            'startup-overwritten':('    zb_init();','    glsd_init_power_stage_io();\n    zb_init();'),
        }
        for name,(old,new) in mutations.items():
            if old not in original: raise ValueError('mutant anchor missing: '+name)
            mutant=source.with_name('mutant-'+name+'.c'); mutant.write_text(original.replace(old,new,1))
            exe=mutant.with_suffix('')
            subprocess.run(['cc','-std=c11','-w','-Isrc','-Itests/host','-I'+str(a.sdk_root.resolve()/'proj/drivers'),
                            str(mutant),'src/glsd301p_startup.c','-o',str(exe)],cwd=root,check=True)
            if subprocess.run([str(exe)],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL).returncode==0:
                raise ValueError('mutation survived: '+name)
            print('STARTUP_WIRING_MUTANT_KILLED='+name)


if __name__=='__main__':main()
