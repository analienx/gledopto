#!/usr/bin/env python3
"""Hosted SDK NV rotation/power-cut isolation test. HAL flash is simulated."""
import argparse
from pathlib import Path
import re
import subprocess
from test_glsd301p_startup_target import function


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--sdk-root',type=Path,required=True)
    p.add_argument('--out',type=Path,required=True)
    a=p.parse_args(); sdk=a.sdk_root.resolve(); out=a.out.resolve()
    root=Path(__file__).resolve().parents[1]
    (out/'proj/drivers').mkdir(parents=True,exist_ok=True)
    for name in ('drv_nv.c','drv_nv.h'):
        (out/'proj/drivers'/name).write_bytes((sdk/'proj/drivers'/name).read_bytes())
    (out/'proj/tl_common.h').write_bytes((root/'tests/host/startup_nv_sdk_shim.h').read_bytes())
    utility=(sdk/'proj/common/utility.c').read_text()
    crc=re.search(r'static const unsigned int crc32_table\[\]\s*=\s*\{.*?\};',utility,re.S).group()
    (out/'sdk_crc.c').write_text(crc+'\n'+function(utility,'xcrc32'))
    source=(root/'tests/host/startup_nv_harness.c').read_text()
    (out/'test.c').write_text(source)
    exe=out/'startup_nv'
    subprocess.run(['cc','-m32','-std=c11','-g','-Isrc','-Itests/host','-I'+str(out),
                    str(out/'test.c'),str(out/'sdk_crc.c'),'src/glsd301p_stack_nv.c',
                    '-o',str(exe)],cwd=root,check=True)
    subprocess.run([str(exe)],check=True)


if __name__=='__main__':main()
