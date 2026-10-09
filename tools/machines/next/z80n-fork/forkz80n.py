#!/usr/bin/env python3
"""Make core/src/3rdparty/unreal-next-z80 from the z84c15 core (itself a fork of unreal-z80 0.5.0).

The z84c15 fork already is "callback bus only, read callback with the bus-cycle kind, the clock taken back after every
callback, halted M1 read through the host, INT / NMI acknowledge through the callbacks"; the Next's CPU library needs the
same interface. This script copies the core files of that fork under the new names and leaves everything else (the chip block,
the wait generator, the CMOS defaults) to the hand edits listed in the library's README.

  forkz80n.py [--from core/src/3rdparty/z84c15] [--to core/src/3rdparty/unreal-next-z80]

Renames: C API prefix Z84Cpu -> Z80nCpu, type Z84CPU -> Z80nCPU, namespaces Z84Lib / Z84Cb -> Z80nLib / Z80nCb, macros Z84* -> Z80N*,
other Z84* helpers -> Z80n*, files z84* -> z80n*. Run it again on a newer z84c15 core to re-derive (then redo the hand edits).
"""
import argparse
import os
import re
import shutil

FILES = [
    'LICENSE', 'z84cpu.h', 'z84cpu.cpp', 'z84cpu-internal.h', 'z84cpu-dispatch.h', 'z84cpu-opcodes.inc', 'z84step.inc',
    'z84tables-data.inc', 'z84tables.cpp', 'z84daa.cpp', 'opcodes-base.inc', 'opcodes-cb.inc', 'opcodes-dd.inc',
    'opcodes-ed.inc', 'opcodes-fd.inc', 'opcodes-xxcb.inc', 'opcodes-callback.cpp',
]


def rename(text):
    text = re.sub(r'Z84CPU_', 'Z80NCPU_', text)
    text = re.sub(r'\bZ84CPU\b', 'Z80nCPU', text)
    text = text.replace('Z84Cpu', 'Z80nCpu')
    text = text.replace('Z84Lib', 'Z80nLib').replace('Z84Cb', 'Z80nCb')
    text = re.sub(r'Z84(?=_|[A-Z]{2})', 'Z80N', text)
    text = text.replace('Z84', 'Z80n')
    text = text.replace('z84', 'z80n')
    return text


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--from', dest='src', default='core/src/3rdparty/z84c15')
    ap.add_argument('--to', dest='dst', default='core/src/3rdparty/unreal-next-z80')
    a = ap.parse_args()
    os.makedirs(a.dst, exist_ok=True)
    for f in FILES:
        text = open(os.path.join(a.src, f), encoding='utf8').read()
        out = f if f == 'LICENSE' else rename(f)
        open(os.path.join(a.dst, out), 'w', encoding='utf8').write(text if f == 'LICENSE' else rename(text))
        print(f, '->', out)


if __name__ == '__main__':
    main()
