#!/usr/bin/env python3
"""Make the binaries of targets.py from a provisioned Next SD tree and the FPGA sources.

  extract.py --sd <folder holding machines/next/>  --vhdl <path of cores/zxnext/src/rom/bootrom.vhd>

The SD tree is the `tbblue` repository (https://gitlab.com/thesmog358/tbblue) or an unpacked distribution;
the VHDL is https://gitlab.com/SpectrumNext/ZX_Spectrum_Next_FPGA. Prints the checksums.
"""
import argparse
import os
import re
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--sd', required=True)
    ap.add_argument('--vhdl', required=True)
    a = ap.parse_args()
    nx = os.path.join(a.sd, 'machines', 'next')
    zx = open(os.path.join(nx, 'enNextZX.rom'), 'rb').read()
    assert len(zx) == 65536, len(zx)
    out = {'next-rom%d.bin' % i: zx[i * 16384:(i + 1) * 16384] for i in range(4)}
    out['next-nxtmmc.bin'] = open(os.path.join(nx, 'enNxtmmc.rom'), 'rb').read()
    text = open(a.vhdl).read()
    out['next-boot.bin'] = bytes(int(x, 16) for x in re.findall(r'x"([0-9A-Fa-f]{2})"', text))
    for name, data in out.items():
        open(os.path.join(HERE, name), 'wb').write(data)
        print(f'{name}: {len(data)} bytes, CRC32 {zlib.crc32(data):08x}')


if __name__ == '__main__':
    main()
