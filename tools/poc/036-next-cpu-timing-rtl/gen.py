#!/usr/bin/env python3
"""POC 036: the test ROMs. Each is 16K, put at ROM 0-3 (SRAM #00000-#0FFFF) by the testbench and by NextRtlTiming_Test.

    gen.py <out dir>        writes <name>.hex (one byte a line, for the testbench) and <name>.bin for every program

Every program: DI, SP in bank 2, the body copied to its address, IM 1, EI, three HALTs (the INT handler at #38 is EI / RET), then
the body runs for one frame and ends in DI / HALT.
"""
import os
import sys


class Asm:
    def __init__(self, org):
        self.org = org
        self.b = bytearray()
        self.labels = {}
        self.fix = []

    @property
    def pc(self):
        return self.org + len(self.b)

    def label(self, name):
        self.labels[name] = self.pc

    def db(self, *v):
        for x in v:
            self.b.append(x & 0xFF)

    def dw(self, v):
        self.db(v & 0xFF, v >> 8)

    def jr(self, op, name):
        self.db(op, 0)
        self.fix.append(("rel", len(self.b) - 1, name))

    def abs16(self, op, name):
        self.db(*op)
        self.db(0, 0)
        self.fix.append(("abs", len(self.b) - 2, name))

    def done(self):
        for kind, at, name in self.fix:
            v = self.labels[name]
            if kind == "rel":
                d = v - (self.org + at + 1)
                assert -128 <= d <= 127, name
                self.b[at] = d & 0xFF
            else:
                self.b[at] = v & 0xFF
                self.b[at + 1] = v >> 8
        return bytes(self.b)


BODY_SRC = 0x0100  # the body in the ROM, copied to its run address


def rom(body, run_at):
    """The prologue and the INT handler around a body assembled for run_at"""
    r = bytearray(0x4000)
    p = Asm(0)
    p.db(0xF3)                                  # DI
    p.db(0x31); p.dw(0xBFF0)                    # LD SP,#BFF0
    if run_at != BODY_SRC:
        p.db(0x21); p.dw(BODY_SRC)              # LD HL,src
        p.db(0x11); p.dw(run_at)                # LD DE,dst
        p.db(0x01); p.dw(len(body))             # LD BC,len
        p.db(0xED, 0xB0)                        # LDIR
    p.db(0xED, 0x56)                            # IM 1
    p.db(0xFB)                                  # EI
    p.db(0x76, 0x76, 0x76)                      # HALT x3: the third INT starts the body
    p.db(0xC3); p.dw(run_at)                    # JP body
    code = p.done()
    assert len(code) <= 0x38
    r[0:len(code)] = code
    r[0x38:0x3A] = bytes([0xFB, 0xC9])          # EI / RET
    assert BODY_SRC + len(body) <= 0x4000
    r[BODY_SRC:BODY_SRC + len(body)] = body
    return bytes(r)


def body_mix(run_at, nextreg, i_reg=None):
    """A loop of contended and uncontended memory and port cycles that walks through every phase of the line: 256 iterations of an
    odd length cover the frame's paper area. nextreg adds the NEXTREG forms (ED 91 / ED 92) of Chg8kBan; i_reg loads I first (IR on
    the bus in the internal T-states after M1: INC HL, PUSH)"""
    a = Asm(run_at)
    if i_reg is not None:
        a.db(0x3E, i_reg, 0xED, 0x47)           # LD A,n / LD I,A
    a.db(0x21); a.dw(0x4000)                    # LD HL,#4000 (bank 5: contended on every timing)
    a.db(0x11); a.dw(0x8000)                    # LD DE,#8000 (bank 2)
    a.db(0x06, 0)                               # LD B,0 (256 iterations)
    a.label("loop")
    a.db(0x7E)                                  # LD A,(HL)
    a.db(0x77)                                  # LD (HL),A
    a.db(0x23)                                  # INC HL (the address on the bus in the internal cycles)
    a.db(0x1A)                                  # LD A,(DE)
    a.db(0xD3, 0xFE)                            # OUT (#FE),A
    a.db(0xDB, 0xFE)                            # IN A,(#FE)
    a.db(0xC5)                                  # PUSH BC
    a.db(0x01); a.dw(0x40FF)                    # LD BC,#40FF: a contended high byte, A0 = 1
    a.db(0xED, 0x79)                            # OUT (C),A
    a.db(0xED, 0x78)                            # IN A,(C)
    a.db(0x0E, 0xFE)                            # LD C,#FE: B = #40, a contended high byte, A0 = 0
    a.db(0xED, 0x79)                            # OUT (C),A
    a.db(0x01); a.dw(0x80FF)                    # LD BC,#80FF: nothing contended
    a.db(0xED, 0x79)                            # OUT (C),A
    a.db(0xC1)                                  # POP BC
    a.db(0xE5, 0xE1)                            # PUSH HL / POP HL (stack in bank 2)
    a.db(0xEB, 0xEB)                            # EX DE,HL x2
    a.db(0x36, 0x55)                            # LD (HL),#55
    a.db(0x34)                                  # INC (HL)
    a.db(0xCB, 0x46)                            # BIT 0,(HL)
    a.db(0x00)                                  # NOP
    if nextreg:
        a.db(0xED, 0x91, 0x57, 0x01)            # NEXTREG #57,#01 (slot 7 keeps page 1)
        a.db(0x3E, 0x01)                        # LD A,1
        a.db(0xED, 0x92, 0x57)                  # NEXTREG #57,A
    a.jr(0x10, "loop")                          # DJNZ loop
    a.db(0xF3, 0x76)                            # DI / HALT
    return a.done()


def body_dma(run_at, mode_port, continuous):
    """DMA from #8000 (bank 2) to port #FE, 256 bytes, then NOPs: the CPU is held for the transfer"""
    a = Asm(run_at)
    # the source: border colours 0-7
    a.db(0x21); a.dw(0x8000)
    a.db(0x06, 0)
    a.label("fill")
    a.db(0x7D, 0xE6, 0x07, 0x77, 0x23)          # LD A,L / AND 7 / LD (HL),A / INC HL
    a.jr(0x10, "fill")
    # WR6 reset, WR0 A->B transfer from #8000 len 256, WR1 port A memory incrementing, WR2 port B I/O fixed, WR4 mode port B #00FE,
    # WR5, WR6 load, enable
    wr4 = 0xAD if continuous else 0xCD          # continuous / burst, port B address follows
    prog = [0xC3, 0x7D, 0x00, 0x80, 0x00, 0x01, 0x14, 0x28, wr4, 0xFE, 0x00, 0x82, 0xCF, 0x87]  # WR1 #14 memory +1, WR2 #28 I/O fixed
    a.db(0x21); a.abs16([], "prog")             # LD HL,prog
    a.db(0x01); a.dw(mode_port | (len(prog) << 8))  # LD BC,len:port
    a.db(0xED, 0xB3)                            # OTIR
    for _ in range(64):
        a.db(0x00)
    a.db(0xF3, 0x76)
    a.label("prog")
    a.db(*prog)
    return a.done()


def body_dmapaper(run_at):
    """The DMA in the paper area: 256 bytes from #4000 (contended on every timing) to port #FE (the ULA port), then 256 bytes from
    #8000 to #4000 (contended writes); a delay loop first takes the run into the paper"""
    a = Asm(run_at)
    a.db(0x06, 0)
    a.label("wait")                             # 256 x 61 T
    for _ in range(12):
        a.db(0x00)
    a.jr(0x10, "wait")
    progs = [
        # WR6 reset, WR0 A->B from #4000 len 256, WR1 memory +1, WR2 I/O fixed, WR4 continuous port B #00FE, WR5, load, enable
        [0xC3, 0x7D, 0x00, 0x40, 0x00, 0x01, 0x14, 0x28, 0xAD, 0xFE, 0x00, 0x82, 0xCF, 0x87],
        # the same from #8000 to memory #4000 +1
        [0xC3, 0x7D, 0x00, 0x80, 0x00, 0x01, 0x14, 0x10, 0xAD, 0x00, 0x40, 0x82, 0xCF, 0x87],
    ]
    for i, prog in enumerate(progs):
        a.db(0x21); a.abs16([], "prog%d" % i)
        a.db(0x01); a.dw(0x6B | (len(prog) << 8))
        a.db(0xED, 0xB3)                        # OTIR
        for _ in range(8):
            a.db(0x00)
    a.db(0xF3, 0x76)
    for i, prog in enumerate(progs):
        a.label("prog%d" % i)
        a.db(*prog)
    return a.done()


def body_nr08(run_at):
    """Contention switched off by NEXTREG #08,#40 in the middle of a paper line, on again 40 lines later, the contended loop around it"""
    a = Asm(run_at)
    a.db(0x21); a.dw(0x4000)
    a.db(0x06, 0)
    a.label("l1")                               # 256 x 69 T: into the paper area
    for _ in range(8):
        a.db(0x7E)
    a.jr(0x10, "l1")
    a.db(0xED, 0x91, 0x08, 0x40)                # NEXTREG #08,#40: contention off
    a.db(0x06, 60)
    a.label("l2")
    for _ in range(8):
        a.db(0x7E)
    a.jr(0x10, "l2")
    a.db(0x01); a.dw(0x243B)                    # the same through the ports: OUT (#243B),8 / OUT (#253B),0
    a.db(0x3E, 0x08, 0xED, 0x79, 0x04, 0x3E, 0x00, 0xED, 0x79)
    a.db(0x06, 60)
    a.label("l3")
    for _ in range(8):
        a.db(0x7E)
    a.jr(0x10, "l3")
    a.db(0xF3, 0x76)
    return a.done()


PROGRAMS = {
    "mix": lambda: rom(body_mix(0x8100, False), 0x8100),
    "mix5": lambda: rom(body_mix(0x6000, False), 0x6000),
    "nreg5": lambda: rom(body_mix(0x6000, True), 0x6000),
    "nreg2": lambda: rom(body_mix(0x8100, True), 0x8100),
    "mixir": lambda: rom(body_mix(0x8100, False, 0x40), 0x8100),
    "dmaburst": lambda: rom(body_dma(0x8100, 0x6B, False), 0x8100),
    "dmacont": lambda: rom(body_dma(0x8100, 0x6B, True), 0x8100),
    "zdmacont": lambda: rom(body_dma(0x8100, 0x0B, True), 0x8100),
    "nr08": lambda: rom(body_nr08(0x8100), 0x8100),
    "dmapaper": lambda: rom(body_dmapaper(0x8100), 0x8100),
}

if __name__ == "__main__":
    out = sys.argv[1]
    os.makedirs(out, exist_ok=True)
    for name, make in PROGRAMS.items():
        image = make()
        open(os.path.join(out, name + ".bin"), "wb").write(image)
        with open(os.path.join(out, name + ".hex"), "w") as f:
            for _ in range(4):  # ROM 0-3: the same image whatever ROM the machine type selects
                f.write("\n".join("%02X" % b for b in image) + "\n")
    print(len(PROGRAMS), "programs in", out)
