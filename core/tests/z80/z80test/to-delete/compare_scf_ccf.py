#!/usr/bin/env python3
"""
Compare emulator SCF+CCF dump with Python z80_reference.py expected values.

Usage:
    python3 compare_scf_ccf.py /tmp/scf_ccf_flags.bin
"""

import sys
import os

# Add path to z80_reference module
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from z80_reference import Z80, scf, ccf, FLAG_S, FLAG_Z, FLAG_Y, FLAG_H, FLAG_X, FLAG_P, FLAG_N, FLAG_C

VEC_SIZE = 20

# Base vector from z80test_vectors.cpp for SCF+CCF
SCF_CCF_BASE = bytes([
    0x37, 0x3F, 0x00, 0x00,  # opcode: SCF (0x37), CCF (0x3F)
    0xFF,                     # F
    0xAA,                     # A
    0xCC, 0xBB,              # BC = 0xBBCC
    0xEE, 0xDD,              # DE = 0xDDEE
    0x11, 0x44,              # HL = 0x4411
    0x88, 0xDD,              # IX = 0xDD88
    0x77, 0xFD,              # IY = 0xFD77
    0x34, 0x12,              # MEM = 0x1234
    0x00, 0xC0,              # SP = 0xC000
])

SCF_CCF_COUNTER = bytes([
    0x00, 0x00, 0x00, 0x00,  # opcode
    0xFF,                     # F: all 8 bits
    0x28,                     # A: bits 3 and 5
    0x00, 0x00,              # BC
    0x00, 0x00,              # DE
    0x00, 0x00,              # HL
    0x00, 0x00,              # IX
    0x00, 0x00,              # IY
    0x00, 0x00,              # MEM
    0x00, 0x00,              # SP
])

SCF_CCF_SHIFTER = bytes([
    0x00, 0x00, 0x00, 0x00,
    0x00,                     # F
    0xD7,                     # A: bits 0,1,2,4,6,7
    0x00, 0x00,              
    0x00, 0x00,              
    0x00, 0x00,              
    0x00, 0x00,              
    0x00, 0x00,              
    0x00, 0x00,              
    0x00, 0x00,              
])

def format_flags(f):
    return (
        ('S' if f & FLAG_S else '-') +
        ('Z' if f & FLAG_Z else '-') +
        ('Y' if f & FLAG_Y else '-') +
        ('H' if f & FLAG_H else '-') +
        ('X' if f & FLAG_X else '-') +
        ('P' if f & FLAG_P else '-') +
        ('N' if f & FLAG_N else '-') +
        ('C' if f & FLAG_C else '-')
    )

class Z80TestIterator:
    def __init__(self, counter_mask, shifter_mask):
        self.counter_mask = list(counter_mask)
        self.shifter_mask = list(shifter_mask)
        self.vec_size = len(counter_mask)
        
    def iterate(self):
        counter = self.counter_mask.copy()
        shifter = [0] * (self.vec_size + 1)
        shifter_pos = 0
        
        while True:
            while True:
                yield (bytes(counter), bytes(shifter[:self.vec_size]))
                if not self._next_counter(counter):
                    counter = self.counter_mask.copy()
                    break
            if not self._next_shifter(shifter, shifter_pos):
                return
            shifter_pos = self._find_shifter_pos(shifter)
    
    def _next_counter(self, counter):
        for i in range(self.vec_size):
            if counter[i] == 0:
                counter[i] = self.counter_mask[i]
                continue
            counter[i] = (counter[i] - 1) & self.counter_mask[i]
            return True
        return False
    
    def _next_shifter(self, shifter, pos):
        while pos < self.vec_size:
            val = shifter[pos]
            mask = self.shifter_mask[pos]
            if mask == 0:
                pos += 1
                continue
            if val == 0:
                new_val = ((mask - 1) ^ mask) & mask
            else:
                neg_2val = (0 - (val << 1)) & 0xFF
                new_val = ((neg_2val + mask) ^ mask) & mask
            if new_val != 0:
                shifter[pos] = new_val
                for j in range(pos):
                    shifter[j] = 0
                return True
            shifter[pos] = 0
            pos += 1
        return False
    
    def _find_shifter_pos(self, shifter):
        for i in range(self.vec_size):
            if shifter[i] != 0:
                return i
        return 0

def execute_scf_ccf_reference(a_in, f_in):
    """Execute SCF+CCF using z80_reference.py (the verified implementation)"""
    s = Z80(a=a_in, f=f_in, q=0)
    scf(s)  # Execute SCF, updates Q
    ccf(s)  # Execute CCF, uses Q from SCF
    return s.f

def compare_with_emulator(emu_bin_file):
    """Compare Python reference with emulator dump, find first mismatch"""
    
    with open(emu_bin_file, 'rb') as f:
        emu_flags = f.read()
    
    print(f"Loaded {len(emu_flags)} emulator flag bytes from {emu_bin_file}")
    print()
    
    iterator = Z80TestIterator(SCF_CCF_COUNTER, SCF_CCF_SHIFTER)
    
    mismatches = 0
    first_mismatch = None
    first_mismatch_data = None
    
    for iteration, (counter_state, shifter_state) in enumerate(iterator.iterate()):
        # Combine vectors
        combined = bytes([
            SCF_CCF_BASE[i] ^ counter_state[i] ^ shifter_state[i]
            for i in range(VEC_SIZE)
        ])
        
        f_in = combined[4]
        a_in = combined[5]
        
        # Get expected value from Python reference
        expected_f = execute_scf_ccf_reference(a_in, f_in)
        
        # Get emulator value
        if iteration >= len(emu_flags):
            print(f"ERROR: Emulator file too short at iteration {iteration}")
            break
        
        emu_f = emu_flags[iteration]
        
        if emu_f != expected_f:
            if first_mismatch is None:
                first_mismatch = iteration
                first_mismatch_data = {
                    'iteration': iteration,
                    'a_in': a_in,
                    'f_in': f_in,
                    'expected_f': expected_f,
                    'emu_f': emu_f,
                }
            mismatches += 1
    
    total = len(emu_flags)
    
    if first_mismatch_data:
        print("=" * 70)
        print("FIRST MISMATCH FOUND!")
        print("=" * 70)
        d = first_mismatch_data
        print(f"Iteration:    {d['iteration']}")
        print(f"Input:        A=0x{d['a_in']:02X}, F=0x{d['f_in']:02X}")
        print(f"Expected F:   0x{d['expected_f']:02X} ({format_flags(d['expected_f'])})")
        print(f"Got F:        0x{d['emu_f']:02X} ({format_flags(d['emu_f'])})")
        diff = d['expected_f'] ^ d['emu_f']
        print(f"Difference:   0x{diff:02X} ({format_flags(diff)})")
        print()
        print("Instructions executed: SCF (0x37) then CCF (0x3F)")
        print("=" * 70)
        print()
        print(f"Total mismatches: {mismatches} / {total}")
        
        # Show context
        print(f"\nContext around first mismatch (iteration {first_mismatch}):")
        start = max(0, first_mismatch - 3)
        end = min(total, first_mismatch + 5)
        
        iterator2 = Z80TestIterator(SCF_CCF_COUNTER, SCF_CCF_SHIFTER)
        for i, (c, s) in enumerate(iterator2.iterate()):
            if i < start:
                continue
            if i >= end:
                break
            combined = bytes([SCF_CCF_BASE[j] ^ c[j] ^ s[j] for j in range(VEC_SIZE)])
            f_in = combined[4]
            a_in = combined[5]
            exp_f = execute_scf_ccf_reference(a_in, f_in)
            emu = emu_flags[i]
            marker = " <-- MISMATCH" if emu != exp_f else ""
            print(f"  [{i:4d}] A=0x{a_in:02X} F_in=0x{f_in:02X} -> "
                  f"expected=0x{exp_f:02X} got=0x{emu:02X}{marker}")
    else:
        print("✅ All values match!")

if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("Usage: python3 compare_scf_ccf.py /tmp/scf_ccf_flags.bin")
        sys.exit(1)
    
    print("SCF+CCF Step-by-Step Comparison")
    print("Using z80_reference.py as ground truth")
    print()
    
    compare_with_emulator(sys.argv[1])
