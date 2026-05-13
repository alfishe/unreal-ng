#!/usr/bin/env python3
"""
SCF+CCF Step-by-Step Reference Test

This script generates expected Z80 state for each iteration of the SCF+CCF 
z80test test case, comparing against the unreal-ng emulator output to find
the exact iteration where discrepancies occur.

The SCF+CCF test executes:
  1. SCF (opcode 0x37) - Set Carry Flag
  2. CCF (opcode 0x3F) - Complement Carry Flag (immediately after)

This tests the internal Q register behavior for XF/YF flag handling.
"""

import sys
import subprocess
import struct
from dataclasses import dataclass
from typing import List, Tuple, Iterator, Optional

# =============================================================================
# Constants matching z80test SCF+CCF test
# =============================================================================

VEC_SIZE = 20  # 4 opcode + 16 data bytes

# Base vector from z80test_vectors.cpp for SCF+CCF
SCF_CCF_BASE = bytes([
    0x37, 0x3F, 0x00, 0x00,  # opcode: SCF (0x37), CCF (0x3F), padding
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

# Counter mask (varies over all 2^N combinations in decrement order)
SCF_CCF_COUNTER = bytes([
    0x00, 0x00, 0x00, 0x00,  # opcode: no variation
    0xFF,                     # F: all 8 bits
    0x28,                     # A: bits 3 and 5 (0x08 | 0x20)
    0x00, 0x00,              # BC: no variation
    0x00, 0x00,              # DE: no variation
    0x00, 0x00,              # HL: no variation
    0x00, 0x00,              # IX: no variation
    0x00, 0x00,              # IY: no variation
    0x00, 0x00,              # MEM: no variation
    0x00, 0x00,              # SP: no variation
])

# Shifter mask (walks through each bit one at a time)
SCF_CCF_SHIFTER = bytes([
    0x00, 0x00, 0x00, 0x00,  # opcode: no variation
    0x00,                     # F: no variation
    0xD7,                     # A: bits 0,1,2,4,6,7 (0x01|0x02|0x04|0x10|0x40|0x80)
    0x00, 0x00,              # BC: no variation
    0x00, 0x00,              # DE: no variation
    0x00, 0x00,              # HL: no variation
    0x00, 0x00,              # IX: no variation
    0x00, 0x00,              # IY: no variation
    0x00, 0x00,              # MEM: no variation
    0x00, 0x00,              # SP: no variation
])

# Expected CRC for allflags mode
EXPECTED_CRC = 0xe0d3c7bf

# Flag constants
FLAG_S, FLAG_Z, FLAG_Y, FLAG_H = 0x80, 0x40, 0x20, 0x10
FLAG_X, FLAG_P, FLAG_N, FLAG_C = 0x08, 0x04, 0x02, 0x01

# =============================================================================
# CRC-32 Implementation (matches z80test)
# =============================================================================

def make_crc_table():
    table = []
    for i in range(256):
        crc = i
        for _ in range(8):
            if crc & 1:
                crc = (crc >> 1) ^ 0xEDB88320
            else:
                crc >>= 1
        table.append(crc)
    return table

CRC_TABLE = make_crc_table()

def crc32_update(crc: int, byte: int) -> int:
    return CRC_TABLE[(crc ^ byte) & 0xFF] ^ (crc >> 8)

# =============================================================================
# Reference Implementation: Zilog Z80 SCF+CCF with Q register
# =============================================================================

def execute_scf_ccf_zilog(a: int, f_in: int, q_initial: int = 0) -> Tuple[int, int]:
    """
    Execute SCF followed by CCF using Zilog Z80 behavior with Q register.
    
    Returns: (F_out, Q_out)
    
    SCF Behavior (Zilog):
      - C = 1, N = 0, H = 0
      - XY = (A | (F & ~Q)) & 0x28
      - Q = F & 0x28 (after update)
      
    CCF Behavior (Zilog):
      - C = ~C, N = 0, H = old_C
      - XY = (A | (F & ~Q)) & 0x28  
      - Q = F & 0x28 (after update)
    """
    # Initial Q (usually 0 at start of test)
    q = q_initial
    f = f_in
    
    # Execute SCF (opcode 0x37)
    xy = (a | (f & ~q)) & 0x28
    f = (f & (FLAG_S | FLAG_Z | FLAG_P)) | xy | FLAG_C  # Set C, clear H and N
    q = f & 0x28  # Update Q after flag change
    
    # Execute CCF (opcode 0x3F) immediately after
    old_c = f & FLAG_C
    xy = (a | (f & ~q)) & 0x28
    h = FLAG_H if old_c else 0
    f = (f & (FLAG_S | FLAG_Z | FLAG_P)) | xy | h | ((f ^ FLAG_C) & FLAG_C)
    q = f & 0x28
    
    return f, q

def execute_scf_ccf_simple(a: int, f_in: int) -> int:
    """
    Execute SCF followed by CCF using simple NEC-style behavior (XY from A only).
    This is what the z80.c reference does.
    
    Returns: F_out
    """
    f = f_in
    
    # SCF: XY = A & 0x28
    xy = a & 0x28
    f = (f & (FLAG_S | FLAG_Z | FLAG_P)) | xy | FLAG_C
    
    # CCF: XY = A & 0x28, H = old_C, C = ~C
    old_c = f & FLAG_C
    xy = a & 0x28
    h = FLAG_H if old_c else 0
    f = (f & (FLAG_S | FLAG_Z | FLAG_P)) | xy | h | ((f ^ FLAG_C) & FLAG_C)
    
    return f

# =============================================================================
# Iteration Engine (exact port from z80test idea.asm)
# =============================================================================

class Z80TestIterator:
    """Exact port of counter/shifter logic from idea.asm"""
    
    def __init__(self, counter_mask: bytes, shifter_mask: bytes):
        self.counter_mask = list(counter_mask)
        self.shifter_mask = list(shifter_mask)
        self.vec_size = len(counter_mask)
        
    def iterate(self) -> Iterator[Tuple[bytes, bytes]]:
        """Generate all (counter_state, shifter_state) pairs"""
        
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
    
    def _next_counter(self, counter: list) -> bool:
        for i in range(self.vec_size):
            if counter[i] == 0:
                counter[i] = self.counter_mask[i]
                continue
            counter[i] = (counter[i] - 1) & self.counter_mask[i]
            return True
        return False
    
    def _next_shifter(self, shifter: list, pos: int) -> bool:
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
    
    def _find_shifter_pos(self, shifter: list) -> int:
        for i in range(self.vec_size):
            if shifter[i] != 0:
                return i
        return 0

# =============================================================================
# Main Test Runner
# =============================================================================

def run_scf_ccf_step_test(max_iterations: Optional[int] = None, verbose: bool = False):
    """
    Run through all SCF+CCF iterations, computing expected flags and CRC.
    
    Returns list of results with:
      - iteration
      - a_in, f_in
      - f_expected (Zilog behavior)
      - f_simple (NEC behavior)
    """
    iterator = Z80TestIterator(SCF_CCF_COUNTER, SCF_CCF_SHIFTER)
    
    crc_zilog = 0xFFFFFFFF
    crc_simple = 0xFFFFFFFF
    results = []
    iteration = 0
    
    for counter_state, shifter_state in iterator.iterate():
        if max_iterations and iteration >= max_iterations:
            break
            
        # Combine: working = base XOR counter XOR shifter
        combined = bytes([
            SCF_CCF_BASE[i] ^ counter_state[i] ^ shifter_state[i]
            for i in range(VEC_SIZE)
        ])
        
        # Extract register values
        f_in = combined[4]
        a_in = combined[5]
        
        # Compute expected flags
        f_zilog, _ = execute_scf_ccf_zilog(a_in, f_in)
        f_simple = execute_scf_ccf_simple(a_in, f_in)
        
        # Update CRCs
        crc_zilog = crc32_update(crc_zilog, f_zilog)
        crc_simple = crc32_update(crc_simple, f_simple)
        
        results.append({
            'iteration': iteration,
            'a_in': a_in,
            'f_in': f_in,
            'f_zilog': f_zilog,
            'f_simple': f_simple,
        })
        
        if verbose and iteration < 20:
            print(f"[{iteration:4d}] A=0x{a_in:02X}, F_in=0x{f_in:02X} -> "
                  f"F_zilog=0x{f_zilog:02X}, F_simple=0x{f_simple:02X}")
        
        iteration += 1
    
    print(f"\nTotal iterations: {iteration}")
    print(f"CRC (Zilog Q-aware): 0x{crc_zilog:08X}")
    print(f"CRC (Simple A-only): 0x{crc_simple:08X}")
    print(f"Expected CRC:        0x{EXPECTED_CRC:08X}")
    
    if crc_zilog == EXPECTED_CRC:
        print("✅ Zilog behavior CRC MATCH!")
    elif crc_simple == EXPECTED_CRC:
        print("✅ Simple behavior CRC MATCH (unexpected)!")
    else:
        print("❌ Neither CRC matches expected")
    
    return results

def generate_csv_reference(filename: str, max_iterations: Optional[int] = None):
    """Generate CSV file with expected F values for each iteration."""
    results = run_scf_ccf_step_test(max_iterations)
    
    with open(filename, 'w') as f:
        f.write("# SCF+CCF Reference Values\n")
        f.write("# iteration,A_in,F_in,F_expected_zilog,F_expected_simple\n")
        for r in results:
            f.write(f"{r['iteration']},{r['a_in']:02X},{r['f_in']:02X},"
                    f"{r['f_zilog']:02X},{r['f_simple']:02X}\n")
    
    print(f"\nWrote {len(results)} reference values to {filename}")

def generate_bin_reference(filename: str, max_iterations: Optional[int] = None):
    """Generate binary file with expected F values (Zilog behavior)."""
    results = run_scf_ccf_step_test(max_iterations)
    
    flags = bytes([r['f_zilog'] for r in results])
    with open(filename, 'wb') as f:
        f.write(flags)
    
    print(f"Wrote {len(flags)} flag bytes to {filename}")

def compare_with_emulator_flags(emulator_bin: str, max_iterations: Optional[int] = None):
    """
    Compare reference Zilog values with emulator-generated flags.
    Finds first mismatch and reports the iteration/state.
    """
    results = run_scf_ccf_step_test(max_iterations, verbose=False)
    
    with open(emulator_bin, 'rb') as f:
        emu_flags = f.read()
    
    print(f"\nComparing {len(results)} reference values with {len(emu_flags)} emulator values...")
    
    mismatches = 0
    first_mismatch = None
    
    for i, r in enumerate(results):
        if i >= len(emu_flags):
            print(f"ERROR: Emulator file too short, only {len(emu_flags)} bytes")
            break
            
        emu_f = emu_flags[i]
        expected_f = r['f_zilog']
        
        if emu_f != expected_f:
            if first_mismatch is None:
                first_mismatch = i
                print("\n" + "=" * 70)
                print("FIRST MISMATCH FOUND!")
                print("=" * 70)
                print(f"Iteration:    {i}")
                print(f"Input:        A=0x{r['a_in']:02X}, F=0x{r['f_in']:02X}")
                print(f"Expected F:   0x{expected_f:02X} ({format_flags(expected_f)})")
                print(f"Got F:        0x{emu_f:02X} ({format_flags(emu_f)})")
                print(f"Difference:   0x{expected_f ^ emu_f:02X} ({format_flags(expected_f ^ emu_f)})")
                print()
                print("Instructions: SCF (0x37) then CCF (0x3F)")
                print(f"Simple (NEC): 0x{r['f_simple']:02X} ({format_flags(r['f_simple'])})")
                if emu_f == r['f_simple']:
                    print("=> Emulator matches NEC behavior, not Zilog Q register behavior")
                print("=" * 70)
            mismatches += 1
    
    if mismatches == 0:
        print("✅ All values match!")
    else:
        print(f"\n❌ Total mismatches: {mismatches} / {len(results)}")
        # Show context around first mismatch
        if first_mismatch is not None and first_mismatch > 0:
            print(f"\nContext around first mismatch (iteration {first_mismatch}):")
            start = max(0, first_mismatch - 2)
            end = min(len(results), first_mismatch + 3)
            for i in range(start, end):
                emu_f = emu_flags[i] if i < len(emu_flags) else 0
                r = results[i]
                marker = " <-- MISMATCH" if emu_f != r['f_zilog'] else ""
                print(f"  [{i:4d}] A=0x{r['a_in']:02X} F_in=0x{r['f_in']:02X} "
                      f"-> expected=0x{r['f_zilog']:02X} got=0x{emu_f:02X}{marker}")

def format_flags(f: int) -> str:
    """Format flag byte as readable string."""
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

if __name__ == "__main__":
    import argparse
    
    parser = argparse.ArgumentParser(description='SCF+CCF step-by-step test')
    parser.add_argument('-v', '--verbose', action='store_true', help='Show first 20 iterations')
    parser.add_argument('-n', '--max', type=int, help='Maximum iterations to run')
    parser.add_argument('--csv', type=str, help='Generate CSV reference file')
    parser.add_argument('--bin', type=str, help='Generate binary reference file')
    parser.add_argument('--compare', type=str, help='Compare with emulator binary file')
    
    args = parser.parse_args()
    
    print("SCF+CCF Step-by-Step Test")
    print("=" * 60)
    print("Test: SCF (0x37) followed by CCF (0x3F)")
    print("Expected CRC: 0xe0d3c7bf (z80test allflags)")
    print()
    
    if args.csv:
        generate_csv_reference(args.csv, args.max)
    elif args.bin:
        generate_bin_reference(args.bin, args.max)
    elif args.compare:
        compare_with_emulator_flags(args.compare, args.max)
    else:
        run_scf_ccf_step_test(args.max, args.verbose)
