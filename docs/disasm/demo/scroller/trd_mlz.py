#!/usr/bin/env python3
"""Ground-truth tool for scroller_by_demarche.trd.

1. Extracts all files from the TRD.
2. Runs the REAL MegaLZ depacker bytes (from SCROLL00.C at $6244, right after
   LOADTBL) inside a tiny Z80 interpreter (closed opcode set of the depacker),
   producing the exact expected contents of every demo RAM page.
"""
import os

TRD = "/Volumes/TB4-4Tb/Projects/Test/unreal-ng/testdata/sound/covox/scroller_by_demarche.trd"
OUT = "/Volumes/TB4-4Tb/Projects/Test/unreal-ng/build/trd_files"


def extract_catalog(data):
    # TRDFile layout (core/src/emulator/io/fdc/trdos.h):
    # name[8] | type | params u16 | lengthInBytes u16 | sizeInSectors | startSector | startTrack
    files = []
    for i in range(128):
        e = data[i * 16:(i + 1) * 16]
        if e[0] in (0, 1):
            break
        name = e[0:8].decode('latin1').rstrip()
        ext = e[8:9].decode('latin1').rstrip()
        params = e[9] | (e[10] << 8)
        length = e[11] | (e[12] << 8)
        sectors = e[13]
        files.append((name, ext, params, length, sectors, e[14], e[15]))
    return files


def read_file(data, sectors, ssec, strk):
    out = bytearray()
    t, s = strk, ssec
    for _ in range(sectors):
        out += data[t * 4096 + s * 256: t * 4096 + (s + 1) * 256]
        s += 1
        if s >= 16:
            s = 0
            t += 1
    return out


class Z80:
    """Minimal interpreter: exactly the opcode set of the MegaLZ depacker."""

    def __init__(self, mem):
        self.mem = mem
        self.A = self.B = self.C = self.D = self.E = self.H = self.L = 0
        self.Ap = 0
        self.Cp = self.Zp = 0        # AF' flags (buffer side)
        self.Cf = self.Zf = 0        # current AF flags
        self.PC = 0
        self.SP = 0xFF00
        self.stack = []

    def rd(self, a):
        return self.mem[a & 0xFFFF]

    def wr(self, a, v):
        self.mem[a & 0xFFFF] = v & 0xFF

    def push(self, v):
        self.stack.append(v & 0xFFFF)

    def pop(self):
        return self.stack.pop() & 0xFFFF

    def setz(self, r):
        self.Zf = 1 if (r & 0xFF) == 0 else 0

    def run(self, entry, maxsteps=50_000_000):
        self.PC = entry
        self.push(0xFFFF)  # sentinel return
        steps = 0
        while True:
            steps += 1
            if steps > maxsteps:
                raise RuntimeError("runaway")
            op = self.rd(self.PC)
            pc0 = self.PC
            self.PC = (self.PC + 1) & 0xFFFF
            if op == 0x3E:                                   # LD A,n
                self.A = self.rd(self.PC); self.PC += 1
            elif op == 0x08:                                 # EX AF,AF'
                self.A, self.Ap = self.Ap, self.A
                self.Cf, self.Cp = self.Cp, self.Cf
                self.Zf, self.Zp = self.Zp, self.Zf
            elif op == 0xED:
                sub = self.rd(self.PC); self.PC += 1
                if sub == 0xA0:                              # LDI
                    hl = (self.H << 8) | self.L
                    de = (self.D << 8) | self.E
                    bc = ((self.B << 8) | self.C) - 1
                    self.wr(de, self.rd(hl))
                    hl = (hl + 1) & 0xFFFF
                    de = (de + 1) & 0xFFFF
                    self.H, self.L = hl >> 8, hl & 0xFF
                    self.D, self.E = de >> 8, de & 0xFF
                    self.B, self.C = bc >> 8, bc & 0xFF
                elif sub == 0xB0:                            # LDIR
                    while True:
                        hl = (self.H << 8) | self.L
                        de = (self.D << 8) | self.E
                        bc = ((self.B << 8) | self.C) - 1
                        self.wr(de, self.rd(hl))
                        hl = (hl + 1) & 0xFFFF
                        de = (de + 1) & 0xFFFF
                        self.H, self.L = hl >> 8, hl & 0xFF
                        self.D, self.E = de >> 8, de & 0xFF
                        self.B, self.C = bc >> 8, bc & 0xFF
                        if bc == 0:
                            break
                else:
                    raise RuntimeError(f"ED {sub:02X} at {pc0:04X}")
            elif op == 0x01:                                 # LD BC,nn
                self.C = self.rd(self.PC)
                self.B = self.rd(self.PC + 1)
                self.PC += 2
            elif op == 0x87:                                 # ADD A,A
                r = (self.A << 1) & 0xFF
                self.Cf = (self.A >> 7) & 1
                self.A = r
                self.setz(r)
            elif op in (0x20, 0x30, 0x28, 0x38, 0x18, 0x10):  # JR cond / DJNZ
                d = self.rd(self.PC) if self.rd(self.PC) < 0x80 else self.rd(self.PC) - 256
                self.PC += 1
                if op == 0x18:
                    taken = True
                elif op == 0x20:
                    taken = self.Zf == 0
                elif op == 0x28:
                    taken = self.Zf == 1
                elif op == 0x30:
                    taken = self.Cf == 0
                elif op == 0x38:
                    taken = self.Cf == 1
                else:                                        # DJNZ
                    self.B = (self.B - 1) & 0xFF
                    taken = self.B != 0
                if taken:
                    self.PC = (self.PC + d) & 0xFFFF
            elif op == 0x7E:                                 # LD A,(HL)
                self.A = self.rd((self.H << 8) | self.L)
            elif op == 0x23:                                 # INC HL
                hl = (((self.H << 8) | self.L) + 1) & 0xFFFF
                self.H, self.L = hl >> 8, hl & 0xFF
            elif op == 0x17:                                 # RLA
            # RLA: A = A<<1 | Cf, Cf = old bit7 (Z unaffected!)
                c = self.Cf
                self.Cf = (self.A >> 7) & 1
                self.A = ((self.A << 1) | c) & 0xFF
            elif op == 0xCB:
                sub = self.rd(self.PC); self.PC += 1
                if sub == 0x11:                              # RL C
                    c = self.Cf
                    self.Cf = (self.C >> 7) & 1
                    self.C = ((self.C << 1) | c) & 0xFF
                    self.setz(self.C)
                elif sub == 0x10:                            # RL B
                    c = self.Cf
                    self.Cf = (self.B >> 7) & 1
                    self.B = ((self.B << 1) | c) & 0xFF
                    self.setz(self.B)
                elif sub == 0x29:                            # SRA C
                    self.Cf = self.C & 1
                    self.C = (self.C >> 1) | (self.C & 0x80)
                    self.setz(self.C)
                elif sub == 0x39:                            # SRL C
                    self.Cf = self.C & 1
                    self.C = self.C >> 1
                    self.setz(self.C)
                elif sub == 0x19:                            # RR C
                    c = self.Cf
                    self.Cf = self.C & 1
                    self.C = (self.C >> 1) | (c << 7)
                    self.setz(self.C)
                else:
                    raise RuntimeError(f"CB {sub:02X} at {pc0:04X}")
            elif op == 0x0C:                                 # INC C
                self.C = (self.C + 1) & 0xFF
                self.setz(self.C)
            elif op == 0x3C:                                 # INC A
                self.A = (self.A + 1) & 0xFF
                self.setz(self.A)
            elif op == 0x04:                                 # INC B
                self.B = (self.B + 1) & 0xFF
                self.setz(self.B)
            elif op == 0x05:                                 # DEC B
                self.B = (self.B - 1) & 0xFF
                self.setz(self.B)
            elif op == 0xD8:                                 # RET C
                if self.Cf:
                    self.PC = self.pop()
                    if self.PC == 0xFFFF:
                        return
            elif op == 0xC9:                                 # RET
                self.PC = self.pop()
                if self.PC == 0xFFFF:
                    return
            elif op == 0x80:                                 # ADD A,B
                r = self.A + self.B
                self.Cf = 1 if r > 0xFF else 0
                self.A = r & 0xFF
                self.setz(self.A)
            elif op == 0x81:                                 # ADD A,C
                r = self.A + self.C
                self.Cf = 1 if r > 0xFF else 0
                self.A = r & 0xFF
                self.setz(self.A)
            elif op == 0x06:                                 # LD B,n
                self.B = self.rd(self.PC); self.PC += 1
            elif op == 0x41:                                 # LD B,C
                self.B = self.C
            elif op == 0x4E:                                 # LD C,(HL)
                self.C = self.rd((self.H << 8) | self.L)
            elif op == 0xE5:                                 # PUSH HL
                self.push((self.H << 8) | self.L)
            elif op == 0xE1:                                 # POP HL
                hl = self.pop()
                self.H, self.L = hl >> 8, hl & 0xFF
            elif op == 0x69:                                 # LD L,C
                self.L = self.C
            elif op == 0x60:                                 # LD H,B
                self.H = self.B
            elif op == 0x19:                                 # ADD HL,DE
            # carry out of bit 15; Z unaffected
                hl = (self.H << 8) | self.L
                de = (self.D << 8) | self.E
                r = hl + de
                self.Cf = 1 if r > 0xFFFF else 0
                r &= 0xFFFF
                self.H, self.L = r >> 8, r & 0xFF
            elif op == 0x4F:                                 # LD C,A
                self.C = self.A
            else:
                raise RuntimeError(f"opcode {op:02X} at {pc0:04X}")


def depack(depacker_bin, dec40_addr, src, srcoff, dstoff):
    mem = bytearray(65536)
    mem[dec40_addr:dec40_addr + len(depacker_bin)] = depacker_bin
    mem[srcoff:srcoff + len(src)] = src
    cpu = Z80(mem)
    cpu.H, cpu.L = srcoff >> 8, srcoff & 0xFF
    cpu.D, cpu.E = dstoff >> 8, dstoff & 0xFF
    cpu.run(dec40_addr)
    return mem


def fill_stats(blob, base, size):
    nz = [i for i in range(size) if blob[i] != 0]
    if not nz:
        return "EMPTY"
    last = max(nz)
    return (f"last_nonzero=${base + last:04X}  nonzero={len(nz)}/{size} "
            f"({100 * len(nz) // size}%)")


def main():
    data = open(TRD, 'rb').read()
    os.makedirs(OUT, exist_ok=True)
    files = extract_catalog(data)
    print("CATALOG:")
    catalog = {}
    for name, ext, params, length, sectors, ssec, strk in files:
        print(f"  {name}.{ext}  addr=${params:04X} len={length} sectors={sectors} "
              f"start=(trk {strk}, sec {ssec})")
        blob = read_file(data, sectors, ssec, strk)
        open(f"{OUT}/{name}.{ext}", 'wb').write(blob)
        catalog[f"{name}.{ext}"] = blob
        print(f"    first: {' '.join(f'{b:02X}' for b in blob[:8])}")

    loader = catalog["SCROLL00.C"]
    dec40 = 0x6244  # LOADER $6206 + code up to LOADTBL $6226 + 6*5 = $6244
    print(f"\nloader[0x44..0x60] (DEPACK head): {' '.join(f'{b:02X}' for b in loader[0x44:0x60])}")
    depacker_bin = loader[0x44:0x100]  # rest of sector (padded)

    parts = [
        ("SCROLL15.C", 0x8000, 0x62B2, 0x8000 - 0x62B2, "bank1/page5 $62B2-"),
        ("SCROLL10.C", 0x8000, 0xC000, 0x4000, "page 0 $C000-"),
        ("SCROLL11.C", 0x8000, 0xC000, 0x4000, "page 1 $C000-"),
        ("SCROLL13.C", 0x8000, 0xC000, 0x4000, "page 3 $C000-"),
        ("SCROLL17.C", 0x8000, 0xDB00, 0x2500, "page 7 $DB00-"),
        ("SCROLL12.C", 0xC000, 0x8000, 0x4000, "bank2/page2 $8000-"),
    ]
    for fname, srcoff, dstoff, size, label in parts:
        blob = catalog[fname]
        try:
            mem = depack(depacker_bin, dec40, blob, srcoff, dstoff)
            out = mem[dstoff:dstoff + size]
            open(f"{OUT}/{fname}.depacked", 'wb').write(out)
            print(f"\n{fname} -> {label}: packed={len(blob)}B")
            print(f"  first 12: {' '.join(f'{b:02X}' for b in out[:12])}")
            print(f"  {fill_stats(out, dstoff, size)}")
            if fname == "SCROLL13.C":
                print(f"  TEXT@+0x10: {' '.join(f'{b:02X}' for b in out[0x10:0x40])}")
                print(f"  FONT@$F600: {' '.join(f'{b:02X}' for b in mem[0xF600:0xF618])}")
            if fname == "SCROLL12.C":
                print(f"  $A000: {' '.join(f'{b:02X}' for b in mem[0xA000:0xA010])}")
                print(f"  $BE00: {' '.join(f'{b:02X}' for b in mem[0xBE00:0xBE010])}")
                print(f"  $BFBF: {' '.join(f'{b:02X}' for b in mem[0xBFBF:0xBFD0])}")
        except RuntimeError as e:
            print(f"\n{fname}: FAILED: {e}")


if __name__ == '__main__':
    main()
