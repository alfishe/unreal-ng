#!/usr/bin/env python3
"""POC 051 test programs: Z80 machine code for the Pentagon 2.666 LE timing oracle.

Each program is a 64 KB image (code at 0x0000, DOS on, ROM window writable in the testbench). The same bytes are
loaded by the emulator test into the Gluk ROM page (0x48) so both run the program from 0x0000 with DOS on.

Common frame: IM 1, the handler at 0x0038 drops its return address and jumps to the address stored at PHASE (the
next phase), so every phase ends at an interrupt and the trace has one INTA per phase. The emulator compares the
trace from the first INTA on: the CPU start after reset differs (the board's MCU releases it at an arbitrary raster
position, the emulator at the frame start).

Ports: xx77 = ATM register (bit 3: 28 MHz), EFF7 bit 4 = 3.5 MHz when xx77.3 = 0. Port 0x0077 keeps A9 = 0 (cpm
stays 0: DOS stays on in the emulator) and A14 = 0. Video mode bits 2..0 = 011 (ZX) in every value.
"""
import os
import sys

PHASE = 0x9FFE


class Asm:
    def __init__(self):
        self.mem = bytearray(65536)
        self.pc = 0
        self.labels = {}
        self.fixups = []  # (pos, label, kind)

    def org(self, a):
        self.pc = a

    def label(self, name):
        self.labels[name] = self.pc

    def b(self, *bs):
        for x in bs:
            self.mem[self.pc] = x & 0xFF
            self.pc += 1

    def w(self, v):
        if isinstance(v, str):
            self.fixups.append((self.pc, v, 'w'))
            v = 0
        self.b(v & 0xFF, v >> 8)

    def jr_to(self, op, name):
        self.b(op)
        self.fixups.append((self.pc, name, 'r'))
        self.b(0)

    def resolve(self):
        for pos, name, kind in self.fixups:
            a = self.labels[name]
            if kind == 'w':
                self.mem[pos] = a & 0xFF
                self.mem[pos + 1] = a >> 8
            else:
                d = a - (pos + 1)
                assert -128 <= d <= 127, name
                self.mem[pos] = d & 0xFF

    # instructions
    def nop(self, n=1): self.b(*([0x00] * n))
    def di(self): self.b(0xF3)
    def ei(self): self.b(0xFB)
    def halt(self): self.b(0x76)
    def im1(self): self.b(0xED, 0x56)
    def im2(self): self.b(0xED, 0x5E)
    def ld_sp(self, v): self.b(0x31); self.w(v)
    def ld_hl(self, v): self.b(0x21); self.w(v)
    def ld_bc(self, v): self.b(0x01); self.w(v)
    def ld_a(self, v): self.b(0x3E, v)
    def ld_b(self, v): self.b(0x06, v)
    def ld_i_a(self): self.b(0xED, 0x47)
    def ld_mem_hl(self, a): self.b(0x22); self.w(a)
    def ld_hl_mem(self, a): self.b(0x2A); self.w(a)
    def jp(self, a): self.b(0xC3); self.w(a)
    def jp_hl(self): self.b(0xE9)
    def pop_hl(self): self.b(0xE1)
    def out_n_a(self, n): self.b(0xD3, n)
    def in_a_n(self, n): self.b(0xDB, n)
    def out_c_a(self): self.b(0xED, 0x79)
    def in_a_c(self): self.b(0xED, 0x78)
    def ini(self): self.b(0xED, 0xA2)
    def outi(self): self.b(0xED, 0xA3)
    def inir(self): self.b(0xED, 0xB2)
    def otir(self): self.b(0xED, 0xB3)
    def ld_hl_a(self): self.b(0x77)
    def ld_a_hl(self): self.b(0x7E)
    def inc_hl(self): self.b(0x23)
    def ldir(self): self.b(0xED, 0xB0)
    def ld_de(self, v): self.b(0x11); self.w(v)
    def push_hl(self): self.b(0xE5)
    def ex_sp_hl(self): self.b(0xE3)

    # helpers
    def set_speed(self, mhz):
        """28: xx77.3 = 1; 14: xx77.3 = 0, EFF7.4 = 0; 3.5: xx77.3 = 0, EFF7.4 = 1. EFF7 first, then xx77"""
        eff7 = 0x10 if mhz == 3.5 else 0x00
        x77 = 0xAB if mhz == 28 else 0xA3
        self.ld_bc(0xEFF7); self.ld_a(eff7); self.out_c_a()
        self.ld_bc(0x0077); self.ld_a(x77); self.out_c_a()

    def next_phase(self, name):
        self.ld_hl(name); self.ld_mem_hl(PHASE)

    def io_mix(self):
        """Every I/O instruction kind, some memory work around them"""
        self.nop(3)
        self.in_a_n(0xFE)
        self.nop()
        self.out_n_a(0xFB)          # Covox: harmless
        self.ld_bc(0x00FB)
        self.in_a_c()
        self.out_c_a()
        self.ld_hl(0xA000)
        self.ld_b(2); self.ini()
        self.ld_b(2); self.outi()
        self.ld_hl(0xA010); self.ld_b(3); self.inir()
        self.ld_hl(0xA010); self.ld_b(3); self.ld_bc(0x03FB); self.otir()
        self.ld_hl(0xA020); self.ld_a(0x5A); self.ld_hl_a(); self.ld_a_hl()
        self.ld_hl(0xA000); self.ld_de(0xA100); self.ld_bc(4); self.ldir()
        self.ex_sp_hl(); self.ex_sp_hl()
        self.nop(2)


def common_start(a, im2=False):
    a.org(0x0000)
    a.di()
    a.ld_sp(0xBF00)
    a.jp(0x0100)
    a.org(0x0038)
    a.pop_hl()
    a.ld_hl_mem(PHASE)
    a.jp_hl()
    a.org(0x0100)
    if im2:
        # vector byte 0xFF (PaE.vhd:1493): table entry at I * 256 + 0xFF
        a.ld_a(0x80); a.ld_i_a()
        a.mem[0x80FF] = 0x38
        a.mem[0x8100] = 0x00
        a.im2()
    else:
        a.im1()
    # sync to the first INT at the power-on 28 MHz
    a.next_phase('p0')
    a.ei()
    a.label('wait0')
    a.halt()
    a.jr_to(0x18, 'wait0')


def prog_speeds(im2=False):
    """Phases: each speed runs the I/O mix, then waits for the INT with HALT; then switches in every direction"""
    a = Asm()
    common_start(a, im2)
    order = [28, 14, 3.5, 28, 3.5, 14, 28]
    for i, mhz in enumerate(order):
        a.label('p%d' % i)
        a.set_speed(mhz)
        a.io_mix()
        a.next_phase('p%d' % (i + 1))
        a.ei()
        a.label('h%d' % i)
        a.halt()
        a.jr_to(0x18, 'h%d' % i)
    a.label('p%d' % len(order))
    a.di()
    a.label('end')
    a.halt()
    a.jr_to(0x18, 'end')
    a.resolve()
    return a.mem, len(order) + 2


def prog_busy(im2=False):
    """The INT arrives while the CPU runs: a loop of I/O instructions (slow cycles at turbo) and a NOP loop"""
    a = Asm()
    common_start(a, im2)
    phases = [(28, 'io'), (28, 'nop'), (14, 'io'), (14, 'nop'), (3.5, 'io'), (28, 'mix')]
    for i, (mhz, kind) in enumerate(phases):
        a.label('p%d' % i)
        a.set_speed(mhz)
        a.next_phase('p%d' % (i + 1))
        a.ei()
        a.label('l%d' % i)
        if kind == 'io':
            a.in_a_n(0xFE)
            a.ld_bc(0x00FB); a.out_c_a()
            a.nop()
        elif kind == 'nop':
            a.nop(3)
        else:
            a.ld_hl(0xA000); a.ld_b(5); a.ld_bc(0x05FB); a.inir()
            a.nop()
        a.jr_to(0x18, 'l%d' % i)
    a.label('p%d' % len(phases))
    a.di()
    a.label('end')
    a.halt()
    a.jr_to(0x18, 'end')
    a.resolve()
    return a.mem, len(phases) + 2


PROGRAMS = {
    'speeds_im1': lambda: prog_speeds(False),
    'speeds_im2': lambda: prog_speeds(True),
    'busy_im1': lambda: prog_busy(False),
}

if __name__ == '__main__':
    out = sys.argv[1]
    os.makedirs(out, exist_ok=True)
    for name, fn in PROGRAMS.items():
        mem, frames = fn()
        # only the used part: the testbench zero-fills the rest
        last = max(i for i, v in enumerate(mem) if v) + 1
        with open(os.path.join(out, name + '.bin'), 'wb') as f:
            f.write(mem[:last])
        with open(os.path.join(out, name + '.frames'), 'w') as f:
            f.write('%d\n' % frames)
        print(name, last, 'bytes', frames, 'frames')
