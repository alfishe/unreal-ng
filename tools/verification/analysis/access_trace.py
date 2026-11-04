#!/usr/bin/env python3
"""Read the CPU access trace of a window of a recording (`analysis trace`, core/src/debugger/analyzers/analysis/
accesstrace.h): the header, the start memory, the instructions with their registers and the bus cycles of each.

    import access_trace
    t = access_trace.load('pass.uatr')
    for x in t.instructions:            # Instruction(frame, t, pc, sp, af, bc, de, hl, ix, iy, flags, cycles)
        for c in x.cycles:              # Cycle(tag, address, value, dt): tag F fetch, R read, W write, I / O port, E edit
            ...
    mem = t.memory_at(index)            # the 64 K view before instruction `index` (the start memory plus the writes)

    python3 access_trace.py pass.uatr [--summary] [--pc 8000-80FF]     # a listing: one line an instruction

Every replay in one pass: memory_at walks the writes once, so ask for increasing indexes (an animation) or keep the
copy it gives.
"""
import argparse
import struct
import sys
from dataclasses import dataclass, field

MAGIC = b'UATR'
HEADER = struct.Struct('<4sHHQIIQIIQQB7x')
INSTRUCTION = struct.Struct('<IIHHHHHHHHB')
CYCLE = struct.Struct('<HBH')


@dataclass
class Cycle:
    tag: str
    address: int
    value: int
    dt: int


@dataclass
class Instruction:
    frame: int
    t: int
    pc: int
    sp: int
    af: int
    bc: int
    de: int
    hl: int
    ix: int
    iy: int
    flags: int
    cycles: list = field(default_factory=list)

    @property
    def interrupted(self):
        return bool(self.flags & 1)

    @property
    def halted(self):
        return bool(self.flags & 2)

    def reg(self, name):
        pairs = {'a': ('af', 8), 'f': ('af', 0), 'b': ('bc', 8), 'c': ('bc', 0), 'd': ('de', 8), 'e': ('de', 0),
                 'h': ('hl', 8), 'l': ('hl', 0)}
        if name in pairs:
            pair, shift = pairs[name]
            return getattr(self, pair) >> shift & 0xFF
        return getattr(self, name)


@dataclass
class Trace:
    from_frame: int
    from_t: int
    to_frame: int
    to_t: int
    units_per_t: int
    frame_t: int
    truncated: bool
    pages: list
    memory: bytes
    instructions: list

    def __post_init__(self):
        self._mem = bytearray(self.memory)
        self._at = 0

    def memory_at(self, index):
        """The 64 K view before instruction `index` (a bytearray; copy it to keep it)"""
        if index < self._at:
            self._mem = bytearray(self.memory)
            self._at = 0
        while self._at < index:
            for c in self.instructions[self._at].cycles:
                if c.tag in 'WE':
                    self._mem[c.address] = c.value
            self._at += 1
        return self._mem

    def cycle_t(self, x, c):
        """A cycle's moment as (frame, CPU T-state in the frame)"""
        t = x.t // max(1, self.units_per_t) + c.dt
        return (x.frame + t // self.frame_t, t % self.frame_t) if self.frame_t else (x.frame, t)


def load(path):
    data = open(path, 'rb').read()
    (magic, version, header, from_frame, from_t, units, to_frame, to_t, frame_t, n_instr, n_cycles,
     truncated) = HEADER.unpack_from(data, 0)
    if magic != MAGIC or version != 1:
        raise ValueError(f'{path}: not an access trace (magic {magic!r}, version {version})')
    at = header
    pages = list(struct.unpack_from('<4H', data, at))
    at += 8
    memory = data[at:at + 0x10000]
    at += 0x10000
    instructions = []
    end = len(data)
    while at < end:
        tag = data[at]
        at += 1
        if tag == 0x58:  # 'X'
            instructions.append(Instruction(*INSTRUCTION.unpack_from(data, at)))
            at += INSTRUCTION.size
        else:
            address, value, dt = CYCLE.unpack_from(data, at)
            at += CYCLE.size
            instructions[-1].cycles.append(Cycle(chr(tag), address, value, dt))
    if len(instructions) != n_instr:
        raise ValueError(f'{path}: {len(instructions)} instructions, the header says {n_instr}')
    return Trace(from_frame, from_t, to_frame, to_t, units, frame_t, bool(truncated), pages, memory, instructions)


def main():
    p = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    p.add_argument('path')
    p.add_argument('--summary', action='store_true', help='counts only')
    p.add_argument('--pc', help='only instructions in this hex range, e.g. 8000-80FF')
    args = p.parse_args()
    t = load(args.path)
    cycles = sum(len(x.cycles) for x in t.instructions)
    print(f'{args.path}: {t.from_frame}:{t.from_t} .. {t.to_frame}:{t.to_t}, {len(t.instructions)} instructions, '
          f'{cycles} cycles{", truncated" if t.truncated else ""}')
    if args.summary:
        return
    lo, hi = 0, 0xFFFF
    if args.pc:
        a, _, b = args.pc.partition('-')
        lo, hi = int(a, 16), int(b or a, 16)
    for x in t.instructions:
        if not lo <= x.pc <= hi:
            continue
        acc = ' '.join(f'{c.tag}{c.address:04X}={c.value:02X}' for c in x.cycles if c.tag != 'F')
        print(f'{x.frame}:{x.t:5} {x.pc:04X} af={x.af:04X} bc={x.bc:04X} de={x.de:04X} hl={x.hl:04X} '
              f'{"INT " if x.interrupted else ""}{acc}')


if __name__ == '__main__':
    try:
        sys.exit(main())
    except BrokenPipeError:
        # `| head` closed the pipe: the listing is cut where the reader wanted it
        sys.stderr.close()
        sys.exit(0)
