#!/usr/bin/env python3
"""Unpack prosiak.exe (Wild Sound XM player for the Sprinter, "Boot Loader For Sprinter By Rob F.").

    unpack_prosiak.py prosiak.exe OUTDIR

Writes OUTDIR/loader.bin (the primary loader, run at #4327), stage2.bin (the loader VM, run at #0000),
the blocks the VM script names (block0.bin = the player at #4500, block1..7 = module data) and
script.txt (the VM script). Every LZSA2 stream is checked against the length word in front of it.
The format is in ../README.md."""
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from lzsa2 import Lzsa2  # noqa: E402


def unpack(exe, outdir, verbose=True):
    if exe[:3] != b"EXE":
        raise SystemExit("not a DSS EXE file")
    loader_off, loader_len = struct.unpack_from("<II", exe, 4)
    load, start, sp = struct.unpack_from("<HHH", exe, 0x10)
    loader = exe[loader_off:loader_off + loader_len]
    os.makedirs(outdir, exist_ok=True)
    open(os.path.join(outdir, "loader.bin"), "wb").write(loader)
    lines = [f"DSS EXE: primary loader at file {loader_off:#x}, {loader_len:#x} bytes, "
             f"load {load:#06x}, start {start:#06x}, SP {sp:#06x}"]

    # Stage 2: the loader unpacks the stream at #44EA to #0000 (window 0, a page from GetMem)
    stage2 = Lzsa2(loader, 0x44EA - load).decode()
    open(os.path.join(outdir, "stage2.bin"), "wb").write(stage2)
    lines.append(f"stage 2 (loader VM): {len(stage2):#x} bytes at #0000")

    # The VM script: the file's second 256-byte sector
    script, i, n = exe[0x100:0x200], 0, 0
    while i < len(script) and script[i]:
        op = script[i]
        if op == 0x05:  # load a block and unpack it in place
            a, de, hl, bc = struct.unpack_from("<BHHH", script, i + 1)
            off = 0x100 * (de + 1) + a
            blk = exe[off:off + bc]
            ulen = blk[0] | blk[1] << 8
            dec = Lzsa2(blk, 2)
            data = dec.decode()
            if len(data) != ulen or dec.pos != len(blk):
                raise SystemExit(f"block {n}: {len(data):#x} bytes from {dec.pos:#x} of {len(blk):#x}, "
                                 f"header says {ulen:#x}")
            open(os.path.join(outdir, f"block{n}.bin"), "wb").write(data)
            lines.append(f"{0x100 + i:04X}: 05 {a:02X} {de:04X} {hl:04X} {bc:04X}  load file {off:#07x}, "
                         f"{bc:#06x} bytes to {hl:#06x}; unpack {ulen:#06x} bytes to {hl + bc - ulen:#06x} "
                         f"-> block{n}.bin")
            n, i = n + 1, i + 8
        elif op == 0x24:  # call
            addr = script[i + 1] | script[i + 2] << 8
            lines.append(f"{0x100 + i:04X}: 24 {addr:04X}            call {addr:#06x}")
            i += 3
        else:
            lines.append(f"{0x100 + i:04X}: {op:02X} (opcode not decoded; stop)")
            break
    open(os.path.join(outdir, "script.txt"), "w").write("\n".join(lines) + "\n")
    if verbose:
        print("\n".join(lines))


if __name__ == "__main__":
    if len(sys.argv) != 3:
        raise SystemExit(__doc__)
    unpack(open(sys.argv[1], "rb").read(), sys.argv[2])
