#!/usr/bin/env python3
"""Companion to trd_mlz.py: SCROLL12 truncation matrix.

Answers "what does the demo look like if only the first k sectors of
SCROLL12.C reached page 4?" - i.e. the state after the 128K editor SWAP hook
flips bank3 mid-LOAD (see TRIAGE.md section 9). Runs the same real depacker
bytes in the same mini-interpreter, but:

  - the source stream is the first k*256 real bytes followed by zeroes
    (exactly what page 4 contains after the flip), and
  - writes below $4000 are dropped, because in the emulator that window is
    ROM.

Note: for k < 48 the depacker never reaches its real terminator; the zero
tail makes it grind on (its source pointer eventually wraps into ROM
content, which the mini-model does not have). Snapshots for k < 48 are
therefore mid-grind states. What is stable across runs is the decoded
VALID-PREFIX boundary: output below it is byte-identical to ground truth,
output above it is garbage/zeroes.
"""
import importlib.util
import os

HERE = os.path.dirname(os.path.abspath(__file__))
spec = importlib.util.spec_from_file_location("trd_mlz", os.path.join(HERE, "trd_mlz.py"))
m = importlib.util.module_from_spec(spec)
spec.loader.exec_module(m)

data = open(m.TRD, 'rb').read()
files = m.extract_catalog(data)
cat = {f"{n}.{e}": m.read_file(data, sec, ss, st) for n, e, p, l, sec, ss, st in files}

loader = cat["SCROLL00.C"]
DEPACKER = loader[0x44:0x100]
S12 = cat["SCROLL12.C"]

LANDMARKS = [
    (0x8000, "INIT1"),
    (0x9B6B, "menu"),
    (0x9CD6, "STARTDEMO"),
    (0xBF02, "IM2INI"),
    (0xBFBF, "IM2hnd"),
]

# Ground-truth landmark bytes from a full depack (k = 48)
FULL = {0x8000: "CD 93 85 FB 76 3E", 0x9B6B: "FB 76 AF D3 FE 21",
        0x9CD6: "FB 76 FB 76 3E 08", 0xBF02: "F3 3E BE ED 47 ED",
        0xBFBF: "F5 3E 00 B7 28 27"}


class Z80ROM(m.Z80):
    """Same interpreter, but the ROM window is not writable (as in the emulator)."""

    def wr(self, a, v):
        a &= 0xFFFF
        if a < 0x4000:
            return
        self.mem[a] = v & 0xFF


def run_k(k, maxsteps=20_000_000):
    mem = bytearray(65536)
    mem[0x6244:0x6244 + len(DEPACKER)] = DEPACKER
    src = bytearray(len(S12))
    avail = min(k * 256, len(S12))
    src[:avail] = S12[:avail]
    mem[0xC000:0xC000 + len(src)] = src
    cpu = Z80ROM(mem)
    cpu.H, cpu.L = 0xC0, 0x00
    cpu.D, cpu.E = 0x80, 0x00
    reason = "RET (clean terminator)"
    try:
        cpu.run(0x6244, maxsteps)
    except RuntimeError:
        reason = f"no terminator within {maxsteps} steps (pc=${cpu.PC:04X})"
    out = mem[0x8000:0xC000]
    nz = sum(1 for b in out if b)
    marks = []
    for addr, name in LANDMARKS:
        chunk = " ".join(f"{b:02X}" for b in mem[addr:addr + 6])
        marks.append(f"{name}={'VALID' if chunk == FULL[addr] else 'corrupt'}")
    print(f"k={k:>2} ({k * 256:>5}B real): {reason}")
    print(f"  page2 nonzero={nz}/16384   " + "  ".join(marks))


if __name__ == "__main__":
    print(f"SCROLL12.C: {len(S12)} bytes / 48 sectors."
          " Valid-prefix boundaries per split offset k:\n")
    for k in [0, 3, 24, 40, 47, 48]:
        run_k(k)
