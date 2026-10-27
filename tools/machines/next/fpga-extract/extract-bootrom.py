#!/usr/bin/env python3
"""Extract the Next's 8K boot ROM from the FPGA sources (cores/zxnext/src/rom/bootrom.vhd).

The VHDL holds the image as a plain byte array (`x"F3",x"ED",...`). Usage:

    extract-bootrom.py <bootrom.vhd> <out.rom>

Prints the MD5 of the image (3.02.03 core: 8c4f0c1b77db8de9ed857f2c873250b8).
"""
import hashlib
import re
import sys


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    text = open(sys.argv[1], encoding="latin-1").read()
    start = text.index("constant ROM")
    body = text[start:]
    data = bytes(int(h, 16) for h in re.findall(r'x"([0-9A-Fa-f]{2})"', body))
    if len(data) != 8192:
        sys.exit(f"expected 8192 bytes, found {len(data)}")
    open(sys.argv[2], "wb").write(data)
    print(len(data), hashlib.md5(data).hexdigest())


main()
