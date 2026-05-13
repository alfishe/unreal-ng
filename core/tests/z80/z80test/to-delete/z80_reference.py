#!/usr/bin/env python3
"""
Z80 Reference Executor - Full Implementation for all z80test tests.
Implements Zilog Z80 instruction behavior to generate expected output flags.
"""

from typing import Callable, Dict
from dataclasses import dataclass

# Flag constants
FLAG_S, FLAG_Z, FLAG_Y, FLAG_H = 0x80, 0x40, 0x20, 0x10
FLAG_X, FLAG_P, FLAG_N, FLAG_C = 0x08, 0x04, 0x02, 0x01

def parity(v): return bin(v & 0xFF).count('1') % 2 == 0
def sz(v): return ((v & 0x80) | (0x40 if v == 0 else 0) | (v & 0x28)) & 0xFF
def szp(v): return sz(v) | (FLAG_P if parity(v) else 0)

@dataclass
class Z80:
    a: int = 0; f: int = 0
    b: int = 0; c: int = 0; d: int = 0; e: int = 0
    h: int = 0; l: int = 0
    ixh: int = 0; ixl: int = 0; iyh: int = 0; iyl: int = 0
    sp: int = 0; mem: int = 0
    q: int = 0  # Internal Q register for XCF behavior
    
    @property
    def bc(self): return (self.b << 8) | self.c
    @property
    def de(self): return (self.d << 8) | self.e
    @property
    def hl(self): return (self.h << 8) | self.l
    @property
    def ix(self): return (self.ixh << 8) | self.ixl
    @property
    def iy(self): return (self.iyh << 8) | self.iyl
    
    @classmethod
    def from_vec(cls, v):
        return cls(a=v[5], f=v[4], c=v[6], b=v[7], e=v[8], d=v[9],
                   l=v[10], h=v[11], ixl=v[12], ixh=v[13], iyl=v[14], iyh=v[15],
                   mem=v[16]|(v[17]<<8), sp=v[18]|(v[19]<<8))

# ============================================================================
# ALU Operations
# ============================================================================

def alu_add(s, n):
    r = (s.a + n) & 0xFF
    f = sz(r)
    if ((s.a & 0xF) + (n & 0xF)) > 0xF: f |= FLAG_H
    if ((s.a ^ n) & 0x80) == 0 and ((s.a ^ r) & 0x80): f |= FLAG_P
    if s.a + n > 0xFF: f |= FLAG_C
    s.a, s.f = r, f
    return s

def alu_adc(s, n):
    c = 1 if s.f & FLAG_C else 0
    r = (s.a + n + c) & 0xFF
    f = sz(r)
    if ((s.a & 0xF) + (n & 0xF) + c) > 0xF: f |= FLAG_H
    if ((s.a ^ n) & 0x80) == 0 and ((s.a ^ r) & 0x80): f |= FLAG_P
    if s.a + n + c > 0xFF: f |= FLAG_C
    s.a, s.f = r, f
    return s

def alu_sub(s, n):
    r = (s.a - n) & 0xFF
    f = FLAG_N | sz(r)
    if (s.a & 0xF) < (n & 0xF): f |= FLAG_H
    if ((s.a ^ n) & 0x80) and ((s.a ^ r) & 0x80): f |= FLAG_P
    if s.a < n: f |= FLAG_C
    s.a, s.f = r, f
    return s

def alu_sbc(s, n):
    c = 1 if s.f & FLAG_C else 0
    r = (s.a - n - c) & 0xFF
    f = FLAG_N | sz(r)
    if (s.a & 0xF) < (n & 0xF) + c: f |= FLAG_H
    if ((s.a ^ n) & 0x80) and ((s.a ^ r) & 0x80): f |= FLAG_P
    if s.a - n - c < 0: f |= FLAG_C
    s.a, s.f = r, f
    return s

def alu_and(s, n):
    r = s.a & n
    s.a, s.f = r, FLAG_H | szp(r)
    return s

def alu_or(s, n):
    r = s.a | n
    s.a, s.f = r, szp(r)
    return s

def alu_xor(s, n):
    r = s.a ^ n
    s.a, s.f = r, szp(r)
    return s

def alu_cp(s, n):
    r = (s.a - n) & 0xFF
    f = FLAG_N | (sz(r) & (FLAG_S | FLAG_Z)) | (n & 0x28)
    if (s.a & 0xF) < (n & 0xF): f |= FLAG_H
    if ((s.a ^ n) & 0x80) and ((s.a ^ r) & 0x80): f |= FLAG_P
    if s.a < n: f |= FLAG_C
    s.f = f
    return s

# ============================================================================
# Rotate/Shift Operations  
# ============================================================================

def rot_rlc(s, v):
    c = (v >> 7) & 1
    r = ((v << 1) | c) & 0xFF
    s.f = szp(r) | c
    return r

def rot_rrc(s, v):
    c = v & 1
    r = ((v >> 1) | (c << 7)) & 0xFF
    s.f = szp(r) | c
    return r

def rot_rl(s, v):
    oc = 1 if s.f & FLAG_C else 0
    c = (v >> 7) & 1
    r = ((v << 1) | oc) & 0xFF
    s.f = szp(r) | c
    return r

def rot_rr(s, v):
    oc = (s.f & FLAG_C) << 7
    c = v & 1
    r = ((v >> 1) | oc) & 0xFF
    s.f = szp(r) | c
    return r

def rot_sla(s, v):
    c = (v >> 7) & 1
    r = (v << 1) & 0xFF
    s.f = szp(r) | c
    return r

def rot_sra(s, v):
    c = v & 1
    r = ((v >> 1) | (v & 0x80)) & 0xFF
    s.f = szp(r) | c
    return r

def rot_sll(s, v):  # SLIA - undocumented
    c = (v >> 7) & 1
    r = ((v << 1) | 1) & 0xFF
    s.f = szp(r) | c
    return r

def rot_srl(s, v):
    c = v & 1
    r = (v >> 1) & 0xFF
    s.f = szp(r) | c
    return r

# Accumulator rotates (only affect C, H, N, X, Y)
def rot_rlca(s):
    c = (s.a >> 7) & 1
    s.a = ((s.a << 1) | c) & 0xFF
    s.f = (s.f & (FLAG_S | FLAG_Z | FLAG_P)) | (s.a & 0x28) | c
    return s

def rot_rrca(s):
    c = s.a & 1
    s.a = ((s.a >> 1) | (c << 7)) & 0xFF
    s.f = (s.f & (FLAG_S | FLAG_Z | FLAG_P)) | (s.a & 0x28) | c
    return s

def rot_rla(s):
    oc = 1 if s.f & FLAG_C else 0
    c = (s.a >> 7) & 1
    s.a = ((s.a << 1) | oc) & 0xFF
    s.f = (s.f & (FLAG_S | FLAG_Z | FLAG_P)) | (s.a & 0x28) | c
    return s

def rot_rra(s):
    oc = (s.f & FLAG_C) << 7
    c = s.a & 1
    s.a = ((s.a >> 1) | oc) & 0xFF
    s.f = (s.f & (FLAG_S | FLAG_Z | FLAG_P)) | (s.a & 0x28) | c
    return s

# ============================================================================
# INC/DEC
# ============================================================================

def inc8(s, v):
    r = (v + 1) & 0xFF
    f = (s.f & FLAG_C) | sz(r)
    if (v & 0xF) == 0xF: f |= FLAG_H
    if v == 0x7F: f |= FLAG_P
    s.f = f
    return r

def dec8(s, v):
    r = (v - 1) & 0xFF
    f = (s.f & FLAG_C) | FLAG_N | sz(r)
    if (v & 0xF) == 0: f |= FLAG_H
    if v == 0x80: f |= FLAG_P
    s.f = f
    return r

# ============================================================================
# 16-bit Arithmetic
# ============================================================================

def add16(s, hl, n):
    r = (hl + n) & 0xFFFF
    f = s.f & (FLAG_S | FLAG_Z | FLAG_P)
    f |= (r >> 8) & 0x28  # XY from high byte
    if ((hl & 0xFFF) + (n & 0xFFF)) > 0xFFF: f |= FLAG_H
    if hl + n > 0xFFFF: f |= FLAG_C
    s.f = f
    return r

def adc16(s, hl, n):
    c = 1 if s.f & FLAG_C else 0
    r = (hl + n + c) & 0xFFFF
    f = (r >> 8) & 0x28
    if r == 0: f |= FLAG_Z
    if r & 0x8000: f |= FLAG_S
    if ((hl & 0xFFF) + (n & 0xFFF) + c) > 0xFFF: f |= FLAG_H
    if ((hl ^ n) & 0x8000) == 0 and ((hl ^ r) & 0x8000): f |= FLAG_P
    if hl + n + c > 0xFFFF: f |= FLAG_C
    s.f = f
    return r

def sbc16(s, hl, n):
    c = 1 if s.f & FLAG_C else 0
    r = (hl - n - c) & 0xFFFF
    f = FLAG_N | ((r >> 8) & 0x28)
    if r == 0: f |= FLAG_Z
    if r & 0x8000: f |= FLAG_S
    if (hl & 0xFFF) < (n & 0xFFF) + c: f |= FLAG_H
    if ((hl ^ n) & 0x8000) and ((hl ^ r) & 0x8000): f |= FLAG_P
    if hl - n - c < 0: f |= FLAG_C
    s.f = f
    return r

# ============================================================================
# BIT/SET/RES
# ============================================================================

def bit_n(s, v, n):
    r = v & (1 << n)
    f = FLAG_H | (s.f & FLAG_C) | (v & 0x28)
    if r == 0: f |= FLAG_Z | FLAG_P
    if n == 7 and r: f |= FLAG_S
    s.f = f
    return s

def bit_n_xy(s, v, n, addr_h):
    r = v & (1 << n)
    f = FLAG_H | (s.f & FLAG_C) | (addr_h & 0x28)
    if r == 0: f |= FLAG_Z | FLAG_P
    if n == 7 and r: f |= FLAG_S
    s.f = f
    return s

# ============================================================================
# Block Operations
# ============================================================================

def ldi_ldd(s, inc):
    n = (s.mem + s.a) & 0xFF
    bc = (s.bc - 1) & 0xFFFF
    s.b, s.c = bc >> 8, bc & 0xFF
    f = s.f & (FLAG_S | FLAG_Z | FLAG_C)
    f |= (n & FLAG_X) | ((n << 4) & FLAG_Y)
    if bc != 0: f |= FLAG_P
    s.f = f
    return s

def cpi_cpd(s, inc):
    r = (s.a - s.mem) & 0xFF
    bc = (s.bc - 1) & 0xFFFF
    s.b, s.c = bc >> 8, bc & 0xFF
    n = (r - (1 if (s.a & 0xF) < (s.mem & 0xF) else 0)) & 0xFF
    f = FLAG_N | (s.f & FLAG_C)
    if r == 0: f |= FLAG_Z
    if r & 0x80: f |= FLAG_S
    if (s.a & 0xF) < (s.mem & 0xF): f |= FLAG_H
    f |= (n & FLAG_X) | ((n << 4) & FLAG_Y)
    if bc != 0: f |= FLAG_P
    s.f = f
    return s

# ============================================================================
# Special
# ============================================================================

def scf(s):
    """SCF - Zilog behavior with Q register"""
    xy = (s.a | (s.f & ~s.q)) & 0x28
    s.f = (s.f & (FLAG_S | FLAG_Z | FLAG_P)) | xy | FLAG_C
    s.q = s.f & 0x28  # Q = new F after flag change
    return s

def ccf(s):
    """CCF - Zilog behavior with Q register"""
    xy = (s.a | (s.f & ~s.q)) & 0x28
    h = FLAG_H if s.f & FLAG_C else 0
    s.f = (s.f & (FLAG_S | FLAG_Z | FLAG_P)) | xy | h | ((s.f ^ FLAG_C) & FLAG_C)
    s.q = s.f & 0x28  # Q = new F after flag change
    return s

def scf_then_ccf(s):
    """SCF followed by CCF - tests Q register persistence"""
    scf(s)  # Sets flags and Q
    ccf(s)  # Uses Q from SCF
    return s

def ccf_then_scf(s):
    """CCF followed by SCF - tests Q register persistence"""
    ccf(s)  # Sets flags and Q
    scf(s)  # Uses Q from CCF
    return s

def cpl(s):
    s.a = (~s.a) & 0xFF
    s.f = (s.f & (FLAG_S | FLAG_Z | FLAG_P | FLAG_C)) | FLAG_H | FLAG_N | (s.a & 0x28)
    return s

def neg(s):
    a = s.a
    r = (0 - a) & 0xFF
    f = FLAG_N | sz(r)
    if (0 - (a & 0xF)) & 0x10: f |= FLAG_H
    if a == 0x80: f |= FLAG_P
    if a != 0: f |= FLAG_C
    s.a, s.f = r, f
    return s

def daa(s):
    a, f = s.a, s.f
    corr, nc = 0, f & FLAG_C
    if (f & FLAG_H) or (a & 0xF) > 9: corr |= 0x06
    if (f & FLAG_C) or a > 0x99: corr |= 0x60; nc = FLAG_C
    if f & FLAG_N: a = (a - corr) & 0xFF
    else: a = (a + corr) & 0xFF
    f = (f & FLAG_N) | nc | szp(a)
    if (s.a ^ a) & 0x10: f |= FLAG_H
    else: f &= ~FLAG_H
    s.a, s.f = a, f
    return s

def rld(s):
    m, a = s.mem & 0xFF, s.a
    nm = ((m << 4) | (a & 0xF)) & 0xFF
    na = (a & 0xF0) | (m >> 4)
    s.a = na
    s.f = szp(na) | (s.f & FLAG_C)
    return s

def rrd(s):
    m, a = s.mem & 0xFF, s.a
    nm = ((a << 4) | (m >> 4)) & 0xFF
    na = (a & 0xF0) | (m & 0xF)
    s.a = na
    s.f = szp(na) | (s.f & FLAG_C)
    return s

def ld_a_i(s, iff2=0):
    # LD A,I test involves executing LD I,A (ED 47) then LD A,I (ED 57)
    # I register is NOT incremented automatically.
    # So I = A.
    
    # Flags: S, Z from I. H=0, N=0. P/V = IFF2
    s.f = sz(s.a) | (FLAG_P if iff2 else 0) | (s.f & FLAG_C)
    return s

def ld_a_r(s, iff2=0):
    # LD A,R test involves executing LD R,A (ED 4F) then LD A,R (ED 5F)
    # 1. LD R,A sets R = A
    # 2. LD A,R reads R.
    # Between the setting and reading, R increments for each opcode fetch.
    # LD A,R has 2 opcode bytes (ED, 5F), so R increments by 2.
    # R is a 7-bit counter, bit 7 is preserved.
    
    # Calculate R value (simulating R=A then +2)
    r_val = (s.a & 0x80) | ((s.a + 2) & 0x7F)
    
    # LD A,R puts R into A
    s.a = r_val
    
    # Flags: S, Z from new A. H=0, N=0. P/V = IFF2
    s.f = sz(s.a) | (FLAG_P if iff2 else 0) | (s.f & FLAG_C)
    return s

def in_r_c(s, v):
    s.f = szp(v) | (s.f & FLAG_C)
    return s

def ini_ind(s, inc):
    # INI/IND - z80.c reference behavior:
    # Preserves S, Y, H, X, P, C flags from input
    # Forces N=1
    # Sets Z based on whether B becomes 0
    
    # 1. Update HL
    if inc == 1: # INI
        hl = (s.h << 8) | s.l
        hl = (hl + 1) & 0xFFFF
        s.h, s.l = hl >> 8, hl & 0xFF
    else: # IND
        hl = (s.h << 8) | s.l
        hl = (hl - 1) & 0xFFFF
        s.h, s.l = hl >> 8, hl & 0xFF
        
    # 2. Decrement B
    s.b = (s.b - 1) & 0xFF
    
    # 3. Set Flags: preserve most, set N=1, set Z based on B
    s.f = s.f | FLAG_N  # Force N=1
    if s.b == 0:
        s.f |= FLAG_Z   # Set Z if B==0
    else:
        s.f &= ~FLAG_Z  # Clear Z if B!=0
    
    return s

def inc8_no_flags(s, v):
    # Helper needed? actually we just inc HL manually
    return (v + 1) & 0xFFFF

def outi_outd(s, inc):
    # OUTI/OUTD - z80.c reference behavior (same as INI/IND)
    # Preserves S, Y, H, X, P, C flags from input
    # Forces N=1, Sets Z based on whether B becomes 0
    
    # 1. Update HL
    if inc == 1: # OUTI
        hl = (s.h << 8) | s.l
        hl = (hl + 1) & 0xFFFF
        s.h, s.l = hl >> 8, hl & 0xFF
    else: # OUTD
        hl = (s.h << 8) | s.l
        hl = (hl - 1) & 0xFFFF
        s.h, s.l = hl >> 8, hl & 0xFF
        
    # 2. Decrement B
    s.b = (s.b - 1) & 0xFF
    
    # 3. Set Flags: preserve most, set N=1, set Z based on B
    s.f = s.f | FLAG_N  # Force N=1
    if s.b == 0:
        s.f |= FLAG_Z   # Set Z if B==0
    else:
        s.f &= ~FLAG_Z  # Clear Z if B!=0
        
    return s

# ============================================================================
# Executor Registry
# ============================================================================

EXEC: Dict[str, Callable] = {}

def reg(name, fn): EXEC[name] = fn

# Basic flag ops
reg("SCF", lambda s, v: scf(s))
reg("CCF", lambda s, v: ccf(s))
reg("SCF+CCF", lambda s, v: scf_then_ccf(s))  # Zilog combo test
reg("CCF+SCF", lambda s, v: ccf_then_scf(s))  # Zilog combo test

# NEC NMOS variant: XY = A & 0x28 (ignores F and Q entirely)
def scf_nec(s):
    xy = s.a & 0x28
    s.f = (s.f & (FLAG_S | FLAG_Z | FLAG_P)) | xy | FLAG_C
    return s

def ccf_nec(s):
    xy = s.a & 0x28
    h = FLAG_H if s.f & FLAG_C else 0
    s.f = (s.f & (FLAG_S | FLAG_Z | FLAG_P)) | xy | h | ((s.f ^ FLAG_C) & FLAG_C)
    return s

reg("SCF (NEC)", lambda s, v: scf_nec(s))
reg("CCF (NEC)", lambda s, v: ccf_nec(s))

# ST CMOS variant: Asymmetric - YF from A|F, XF from A only
def scf_st(s):
    yf = (s.a | s.f) & FLAG_Y  # YF = (A | F) & 0x20
    xf = s.a & FLAG_X          # XF = A & 0x08
    s.f = (s.f & (FLAG_S | FLAG_Z | FLAG_P)) | yf | xf | FLAG_C
    return s

def ccf_st(s):
    yf = (s.a | s.f) & FLAG_Y
    xf = s.a & FLAG_X
    h = FLAG_H if s.f & FLAG_C else 0
    s.f = (s.f & (FLAG_S | FLAG_Z | FLAG_P)) | yf | xf | h | ((s.f ^ FLAG_C) & FLAG_C)
    return s

reg("SCF (ST)", lambda s, v: scf_st(s))
reg("CCF (ST)", lambda s, v: ccf_st(s))

reg("CPL", lambda s, v: cpl(s))
reg("NEG", lambda s, v: neg(s))
reg("NEG'", lambda s, v: neg(s))
reg("DAA", lambda s, v: daa(s))

# ALU immediate
reg("ADD A,N", lambda s, v: alu_add(s, v[1]))
reg("ADC A,N", lambda s, v: alu_adc(s, v[1]))
reg("SUB A,N", lambda s, v: alu_sub(s, v[1]))
reg("SBC A,N", lambda s, v: alu_sbc(s, v[1]))
reg("AND N", lambda s, v: alu_and(s, v[1]))
reg("OR N", lambda s, v: alu_or(s, v[1]))
reg("XOR N", lambda s, v: alu_xor(s, v[1]))
reg("CP N", lambda s, v: alu_cp(s, v[1]))

# INC/DEC A
reg("INC A", lambda s, v: (setattr(s, 'a', inc8(s, s.a)), s)[1])
reg("DEC A", lambda s, v: (setattr(s, 'a', dec8(s, s.a)), s)[1])

# ALU A,A
def alu_aa(s, v):
    op = (v[0] >> 3) & 7
    ops = [alu_add, alu_adc, alu_sub, alu_sbc, alu_and, alu_xor, alu_or, alu_cp]
    return ops[op](s, s.a)
reg("ALO A,A", alu_aa)

# ALU A,r - get operand from register based on opcode
def get_reg(s, r):
    return [s.b, s.c, s.d, s.e, s.h, s.l, s.mem & 0xFF, s.a][r]

def set_reg(s, r, val):
    val &= 0xFF
    if r == 0: s.b = val
    elif r == 1: s.c = val
    elif r == 2: s.d = val
    elif r == 3: s.e = val
    elif r == 4: s.h = val
    elif r == 5: s.l = val
    elif r == 6: s.mem = (s.mem & 0xFF00) | val
    elif r == 7: s.a = val

def get_reg_ix(s, r):
    return [s.b, s.c, s.d, s.e, s.ixh, s.ixl, s.mem & 0xFF, s.a][r]

def get_reg_iy(s, r):
    return [s.b, s.c, s.d, s.e, s.iyh, s.iyl, s.mem & 0xFF, s.a][r]

def alu_ar(s, v):
    op = (v[0] >> 3) & 7
    r = v[0] & 7
    n = get_reg(s, r)
    ops = [alu_add, alu_adc, alu_sub, alu_sbc, alu_and, alu_xor, alu_or, alu_cp]
    return ops[op](s, n)

def alu_ar_ix(s, v):
    op = (v[1] >> 3) & 7
    r = v[1] & 7
    n = get_reg_ix(s, r)
    ops = [alu_add, alu_adc, alu_sub, alu_sbc, alu_and, alu_xor, alu_or, alu_cp]
    return ops[op](s, n)

def alu_ar_iy(s, v):
    op = (v[1] >> 3) & 7
    r = v[1] & 7
    n = get_reg_iy(s, r)
    ops = [alu_add, alu_adc, alu_sub, alu_sbc, alu_and, alu_xor, alu_or, alu_cp]
    return ops[op](s, n)

def alu_hl_mem(s, v):
    # ALO A,(HL) - operand is memory at (HL)
    op = (v[0] >> 3) & 7
    n = s.mem & 0xFF
    ops = [alu_add, alu_adc, alu_sub, alu_sbc, alu_and, alu_xor, alu_or, alu_cp]
    return ops[op](s, n)

def alu_xy_mem(s, v):
    # DD/FD prefix + opcode at v[2]
    op = (v[2] >> 3) & 7
    n = s.mem & 0xFF
    ops = [alu_add, alu_adc, alu_sub, alu_sbc, alu_and, alu_xor, alu_or, alu_cp]
    return ops[op](s, n)

reg("ALO A,[B,C]", alu_ar)
reg("ALO A,[D,E]", alu_ar)
reg("ALO A,[H,L]", alu_ar)
reg("ALO A,(HL)", alu_hl_mem)  # Uses memory operand
reg("ALO A,[HX,LX]", alu_ar_ix)
reg("ALO A,[HY,LY]", alu_ar_iy)
reg("ALO A,(XY)", alu_xy_mem)

# Accumulator rotates
reg("RLCA", lambda s, v: rot_rlca(s))
reg("RRCA", lambda s, v: rot_rrca(s))
reg("RLA", lambda s, v: rot_rla(s))
reg("RRA", lambda s, v: rot_rra(s))

# CB prefix rotates on A
reg("RLC A", lambda s, v: (setattr(s, 'a', rot_rlc(s, s.a)), s)[1])
reg("RRC A", lambda s, v: (setattr(s, 'a', rot_rrc(s, s.a)), s)[1])
reg("RL A", lambda s, v: (setattr(s, 'a', rot_rl(s, s.a)), s)[1])
reg("RR A", lambda s, v: (setattr(s, 'a', rot_rr(s, s.a)), s)[1])
reg("SLA A", lambda s, v: (setattr(s, 'a', rot_sla(s, s.a)), s)[1])
reg("SRA A", lambda s, v: (setattr(s, 'a', rot_sra(s, s.a)), s)[1])
reg("SLIA A", lambda s, v: (setattr(s, 'a', rot_sll(s, s.a)), s)[1])
reg("SRL A", lambda s, v: (setattr(s, 'a', rot_srl(s, s.a)), s)[1])

# Rotates on registers/(HL) - CB prefix
def sro_r(s, v):
    op = (v[1] >> 3) & 7
    r = v[1] & 7
    # r=6 means (HL) which uses mem
    val = s.mem & 0xFF if r == 6 else get_reg(s, r)
    ops = [rot_rlc, rot_rrc, rot_rl, rot_rr, rot_sla, rot_sra, rot_sll, rot_srl]
    ops[op](s, val)  # Sets flags
    return s

# Rotates on (IX+d)/(IY+d) - DDCB/FDCB prefix
def sro_xy(s, v):
    op = (v[3] >> 3) & 7
    val = s.mem & 0xFF
    ops = [rot_rlc, rot_rrc, rot_rl, rot_rr, rot_sla, rot_sra, rot_sll, rot_srl]
    ops[op](s, val)  # Sets flags
    return s

reg("RLC [R,(HL)]", sro_r)
reg("RRC [R,(HL)]", sro_r)
reg("RL [R,(HL)]", sro_r)
reg("RR [R,(HL)]", sro_r)
reg("SLA [R,(HL)]", sro_r)
reg("SRA [R,(HL)]", sro_r)
reg("SLIA [R,(HL)]", sro_r)
reg("SRL [R,(HL)]", sro_r)
reg("SRO (XY)", sro_xy)
reg("SRO (XY),R", sro_xy)

# RLD/RRD
reg("RLD", lambda s, v: rld(s))
reg("RRD", lambda s, v: rrd(s))

# INC/DEC reg/(HL)
# INC/DEC reg/(HL)
def inc_r(s, v):
    r = (v[0] >> 3) & 7
    # r=6 means (HL) which uses mem
    if r == 6:
        s.mem = inc8(s, s.mem & 0xFF) | (s.mem & 0xFF00)
    else:
        val = inc8(s, get_reg(s, r))
        set_reg(s, r, val)
    return s

def dec_r(s, v):
    r = (v[0] >> 3) & 7
    if r == 6:
        s.mem = dec8(s, s.mem & 0xFF) | (s.mem & 0xFF00)
    else:
        val = dec8(s, get_reg(s, r))
        set_reg(s, r, val)
    return s

# INC/DEC with DD/FD prefix on IXH/IXL/IYH/IYL
def inc_x(s, v):
    r = (v[1] >> 3) & 7
    # With DD prefix: r=4=IXH, r=5=IXL; with FD: r=4=IYH, r=5=IYL
    if r == 4: # High (IXH/IYH)
        if v[0] == 0xDD: s.ixh = inc8(s, s.ixh)
        else: s.iyh = inc8(s, s.iyh)
    elif r == 5: # Low (IXL/IYL)
        if v[0] == 0xDD: s.ixl = inc8(s, s.ixl)
        else: s.iyl = inc8(s, s.iyl)
    return s

def dec_x(s, v):
    r = (v[1] >> 3) & 7
    if r == 4: # High
        if v[0] == 0xDD: s.ixh = dec8(s, s.ixh)
        else: s.iyh = dec8(s, s.iyh)
    elif r == 5: # Low
        if v[0] == 0xDD: s.ixl = dec8(s, s.ixl)
        else: s.iyl = dec8(s, s.iyl)
    return s

def inc_xy(s, v):
    # INC (XY+d)
    s.mem = inc8(s, s.mem & 0xFF) | (s.mem & 0xFF00)
    return s

def dec_xy(s, v):
    # DEC (XY+d)
    s.mem = dec8(s, s.mem & 0xFF) | (s.mem & 0xFF00)
    return s

reg("INC [R,(HL)]", inc_r)
reg("DEC [R,(HL)]", dec_r)
reg("INC X", inc_x)
reg("DEC X", dec_x)
reg("INC (XY)", inc_xy)
reg("DEC (XY)", dec_xy)

# 16-bit INC/DEC don't affect flags
def inc_rr(s, v):
    rr = (v[0] >> 4) & 3
    if rr == 0: val = s.bc
    elif rr == 1: val = s.de
    elif rr == 2: val = s.hl
    else: val = s.sp
    
    val = (val + 1) & 0xFFFF
    
    if rr == 0: s.b, s.c = val >> 8, val & 0xFF
    elif rr == 1: s.d, s.e = val >> 8, val & 0xFF
    elif rr == 2: s.h, s.l = val >> 8, val & 0xFF
    else: s.sp = val
    return s

def dec_rr(s, v):
    rr = (v[0] >> 4) & 3
    if rr == 0: val = s.bc
    elif rr == 1: val = s.de
    elif rr == 2: val = s.hl
    else: val = s.sp
    
    val = (val - 1) & 0xFFFF
    
    if rr == 0: s.b, s.c = val >> 8, val & 0xFF
    elif rr == 1: s.d, s.e = val >> 8, val & 0xFF
    elif rr == 2: s.h, s.l = val >> 8, val & 0xFF
    else: s.sp = val
    return s

def inc_xy_16(s, v):
    if v[0] == 0xDD: # IX
        s.ixh, s.ixl = ((s.ix + 1) & 0xFFFF) >> 8, (s.ix + 1) & 0xFF
    else: # IY
        s.iyh, s.iyl = ((s.iy + 1) & 0xFFFF) >> 8, (s.iy + 1) & 0xFF
    return s

def dec_xy_16(s, v):
    if v[0] == 0xDD: # IX
        s.ixh, s.ixl = ((s.ix - 1) & 0xFFFF) >> 8, (s.ix - 1) & 0xFF
    else: # IY
        s.iyh, s.iyl = ((s.iy - 1) & 0xFFFF) >> 8, (s.iy - 1) & 0xFF
    return s

reg("INC RR", inc_rr)
reg("DEC RR", dec_rr)
reg("INC XY", inc_xy_16)
reg("DEC XY", dec_xy_16)
reg("DEC XY", lambda s, v: s)

# 16-bit ADD/ADC/SBC
def add_hl_rr(s, v):
    rr = (v[0] >> 4) & 3
    n = [s.bc, s.de, s.hl, s.sp][rr]
    add16(s, s.hl, n)
    return s

def add_ix_rr(s, v):
    rr = (v[1] >> 4) & 3
    # For IX: rr=2 means IX itself
    n = [s.bc, s.de, s.ix, s.sp][rr]
    add16(s, s.ix, n)
    return s

def add_iy_rr(s, v):
    rr = (v[1] >> 4) & 3
    n = [s.bc, s.de, s.iy, s.sp][rr]
    add16(s, s.iy, n)
    return s

reg("ADD HL,RR", add_hl_rr)
reg("ADD IX,RR", add_ix_rr)
reg("ADD IY,RR", add_iy_rr)

def adc_hl_rr(s, v):
    rr = (v[1] >> 4) & 3
    n = [s.bc, s.de, s.hl, s.sp][rr]
    adc16(s, s.hl, n)
    return s

def sbc_hl_rr(s, v):
    rr = (v[1] >> 4) & 3
    n = [s.bc, s.de, s.hl, s.sp][rr]
    sbc16(s, s.hl, n)
    return s

reg("ADC HL,RR", adc_hl_rr)
reg("SBC HL,RR", sbc_hl_rr)

# BIT
def bit_a(s, v):
    n = (v[1] >> 3) & 7
    return bit_n(s, s.a, n)

def bit_hl(s, v):
    # Handles BIT n,r and BIT n,(HL) for standard CB prefix
    op = v[1]
    n = (op >> 3) & 7
    r = op & 7
    
    if r == 6: # (HL)
        # Value from memory
        val = s.mem & 0xFF
        # X/Y flags from MEMPTR high byte.
        # MEMPTR is updated to HL+1 after reading (HL).
        # So X/Y come from high byte of (HL+1).
        # Since s.h/s.l are 8-bit, we reconstruct HL
        hl = (s.h << 8) | s.l
        memptr = (hl + 1) & 0xFFFF
        return bit_n_xy(s, val, n, memptr >> 8)
    else:
        # Value from register
        val = get_reg(s, r)
        # X/Y flags from register (same as val)
        return bit_n(s, val, n)

def bit_xy(s, v):
    n = (v[3] >> 3) & 7
    # Effective address calculation
    d = v[2]
    if d > 127: d -= 256
    
    base = s.ix if v[0] == 0xDD else s.iy
    addr = (base + d) & 0xFFFF
    
    # MEMPTR is updated to addr+1
    memptr = (addr + 1) & 0xFFFF
    return bit_n_xy(s, s.mem & 0xFF, n, memptr >> 8)

reg("BIT N,A", bit_a)
reg("BIT N,(HL)", bit_hl)
reg("BIT N,[R,(HL)]", bit_hl)
reg("BIT N,(XY)", bit_xy)
reg("BIT N,(XY),-", bit_xy)

# SET/RES don't affect flags
reg("SET N,A", lambda s, v: s)
reg("SET N,(HL)", lambda s, v: s)
reg("SET N,[R,(HL)]", lambda s, v: s)
reg("SET N,(XY)", lambda s, v: s)
reg("SET N,(XY),R", lambda s, v: s)
reg("RES N,A", lambda s, v: s)
reg("RES N,(HL)", lambda s, v: s)
reg("RES N,[R,(HL)]", lambda s, v: s)
reg("RES N,(XY)", lambda s, v: s)
reg("RES N,(XY),R", lambda s, v: s)

# Block transfers
reg("LDI", lambda s, v: ldi_ldd(s, True))
reg("LDD", lambda s, v: ldi_ldd(s, False))
reg("LDIR", lambda s, v: ldi_ldd(s, True))
reg("LDDR", lambda s, v: ldi_ldd(s, False))
reg("LDIR->NOP'", lambda s, v: ldi_ldd(s, True))
reg("LDDR->NOP'", lambda s, v: ldi_ldd(s, False))

reg("CPI", lambda s, v: cpi_cpd(s, True))
reg("CPD", lambda s, v: cpi_cpd(s, False))
reg("CPIR", lambda s, v: cpi_cpd(s, True))
reg("CPDR", lambda s, v: cpi_cpd(s, False))

# I/O
reg("IN A,(N)", lambda s, v: s)  # Flags unchanged
reg("IN R,(C)", lambda s, v: in_r_c(s, s.mem & 0xFF))  # Port value from mem field
reg("IN (C)", lambda s, v: in_r_c(s, s.mem & 0xFF))  # IN F,(C) - same flags
reg("INI", lambda s, v: ini_ind(s, True))
reg("IND", lambda s, v: ini_ind(s, False))
reg("INIR", lambda s, v: ini_ind(s, True))
reg("INDR", lambda s, v: ini_ind(s, False))
reg("INIR->NOP'", lambda s, v: ini_ind(s, True))
reg("INDR->NOP'", lambda s, v: ini_ind(s, False))

reg("OUT (N),A", lambda s, v: s)
reg("OUT (C),R", lambda s, v: s)
reg("OUT (C),0", lambda s, v: s)
reg("OUTI", lambda s, v: outi_outd(s, True))
reg("OUTD", lambda s, v: outi_outd(s, False))
reg("OTIR", lambda s, v: outi_outd(s, True))
reg("OTDR", lambda s, v: outi_outd(s, False))

# Jumps/calls/returns - flags unchanged
for name in ["JP NN", "JP CC,NN", "JP (HL)", "JP (XY)", "JR N", "JR CC,N", 
             "DJNZ N", "CALL NN", "CALL CC,NN", "RET", "RET CC", "RETN", 
             "RETI", "RETI/RETN"]:
    reg(name, lambda s, v: s)

# Stack ops - flags unchanged except POP AF
reg("PUSH+POP RR", lambda s, v: s)
reg("PUSH+POP XY", lambda s, v: s)
reg("POP+PUSH AF", lambda s, v: s)  # F changes but from stack, input F is output

# Exchange - flags unchanged
for name in ["EX DE,HL", "EX AF,AF'", "EXX", "EX (SP),HL", "EX (SP),XY"]:
    reg(name, lambda s, v: s)

# LD ops - mostly flags unchanged
for name in ["LD [R,(HL)],[R,(HL)]", "LD [X,(XY)],[X,(XY)]", "LD R,(XY)", 
             "LD (XY),R", "LD [R,(HL)],N", "LD X,N", "LD (XY),N",
             "LD A,([BC,DE])", "LD ([BC,DE]),A", "LD A,(NN)", "LD (NN),A",
             "LD RR,NN", "LD XY,NN", "LD HL,(NN)", "LD XY,(NN)", "LD RR,(NN)",
             "LD (NN),HL", "LD (NN),XY", "LD (NN),RR", "LD SP,HL", "LD SP,XY",
             "LD I,A", "LD R,A"]:
    reg(name, lambda s, v: s)

# LD A,I and LD A,R affect flags
reg("LD A,I", lambda s, v: ld_a_i(s))
reg("LD A,R", lambda s, v: ld_a_r(s))

# EI/DI, IM - flags unchanged
reg("EI+DI", lambda s, v: s)
reg("IM N", lambda s, v: s)

# Test vectors
reg("CRC TEST", lambda s, v: s)
reg("COUNTER TEST", lambda s, v: s)
reg("SHIFTER TEST", lambda s, v: s)
reg("SELF TEST", lambda s, v: s)

# ============================================================================
# Public API
# ============================================================================

def state_to_bytes(s: Z80) -> bytes:
    """Serialize Z80 state to 16 bytes for CRC (matches z80test idea.asm)
    Order: F, A, C, B, E, D, L, H, XL, XH, YL, YH, MEM_L, MEM_H, SP_L, SP_H
    """
    return bytes([
        s.f & 0xFF,
        s.a & 0xFF,
        s.c & 0xFF,
        s.b & 0xFF,
        s.e & 0xFF,
        s.d & 0xFF,
        s.l & 0xFF,
        s.h & 0xFF,
        s.ixl & 0xFF,
        s.ixh & 0xFF,
        s.iyl & 0xFF,
        s.iyh & 0xFF,
        s.mem & 0xFF,
        (s.mem >> 8) & 0xFF,
        s.sp & 0xFF,
        (s.sp >> 8) & 0xFF,
    ])

def execute_test(name: str, vec: bytes) -> int:
    """Execute test and return output F register only (for backward compat)"""
    if name not in EXEC: return -1
    s = Z80.from_vec(vec)
    EXEC[name](s, vec)
    return s.f

def execute_test_state(name: str, vec: bytes) -> bytes:
    """Execute test and return full 16-byte state after execution for CRC"""
    if name not in EXEC: return b''
    s = Z80.from_vec(vec)
    EXEC[name](s, vec)
    return state_to_bytes(s)

def has_executor(name: str) -> bool:
    return name in EXEC

def list_executors() -> list:
    return list(EXEC.keys())
