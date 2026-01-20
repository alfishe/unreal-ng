#!/usr/bin/env python3
"""
Z80Test SCF Instruction Unwrapper - Exact z80test Iteration Match

Reverse-engineers the z80test SCF test case to generate explicit expected
values for each iteration, allowing comparison against emulator behavior.

This version exactly replicates the iteration order from idea.asm:
- Counter: decrement-with-borrow within mask, reset to mask on zero
- Shifter: bit-walking using dual formulas
- Both operate on 20-byte vectors, byte-by-byte

SCF (Set Carry Flag) Opcode: 0x37
Expected CRC (allflags): 0x3ec05634
"""

import struct
from dataclasses import dataclass
from typing import List, Tuple, Iterator

# =============================================================================
# CRC-32 Implementation (matches z80test exactly)
# =============================================================================

def make_crc_table():
    """Standard CRC-32 reflected polynomial 0xEDB88320"""
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

def crc32_init() -> int:
    return 0xFFFFFFFF

def crc32_update(crc: int, byte: int) -> int:
    """Update CRC with a single byte"""
    return CRC_TABLE[(crc ^ byte) & 0xFF] ^ (crc >> 8)

def crc32_update_buf(crc: int, data: bytes) -> int:
    """Update CRC with multiple bytes"""
    for b in data:
        crc = CRC_TABLE[(crc ^ b) & 0xFF] ^ (crc >> 8)
    return crc

# =============================================================================
# Vector Constants (20 bytes each, matching idea.asm layout)
# =============================================================================
# Layout: [opcode0, opcode1, opcode2, opcode3, F, A, C, B, E, D, L, H, XL, XH, YL, YH, MEMlo, MEMhi, SPlo, SPhi]

VEC_SIZE = 20
DATA_SIZE = 16

# Base vector for SCF test
SCF_BASE = bytes([
    0x37, 0x00, 0x00, 0x00,  # opcode: SCF (0x37), padded
    0xff,                     # F
    0xaa,                     # A
    0xcc, 0xbb,              # BC = 0xbbcc
    0xee, 0xdd,              # DE = 0xddee
    0x11, 0x44,              # HL = 0x4411
    0x88, 0xdd,              # IX = 0xdd88
    0x77, 0xfd,              # IY = 0xfd77
    0x34, 0x12,              # MEM = 0x1234
    0x00, 0xc0,              # SP = 0xc000
])

# Counter mask for SCF test
SCF_COUNTER = bytes([
    0x00, 0x00, 0x00, 0x00,  # opcode: no variation
    0xff,                     # F: all 8 bits
    0x28,                     # A: bits 3 and 5 (0x08 | 0x20)
    0x00, 0x00,              # BC: no variation
    0x00, 0x00,              # DE: no variation
    0x00, 0x00,              # HL: no variation
    0x00, 0x00,              # IX: no variation
    0x00, 0x00,              # IY: no variation
    0x00, 0x00,              # MEM: no variation
    0x00, 0x00,              # SP: no variation
])

# Shifter mask for SCF test
SCF_SHIFTER = bytes([
    0x00, 0x00, 0x00, 0x00,  # opcode: no variation
    0x00,                     # F: no variation
    0xd7,                     # A: bits 0,1,2,4,6,7 (0x01|0x02|0x04|0x10|0x40|0x80)
    0x00, 0x00,              # BC: no variation
    0x00, 0x00,              # DE: no variation
    0x00, 0x00,              # HL: no variation
    0x00, 0x00,              # IX: no variation
    0x00, 0x00,              # IY: no variation
    0x00, 0x00,              # MEM: no variation
    0x00, 0x00,              # SP: no variation
])

EXPECTED_CRC = 0x3ec05634

# =============================================================================
# Iteration Engine (exact port of idea.asm)
# =============================================================================

class Z80TestIterator:
    """
    Exact port of the counter/shifter logic from idea.asm.
    
    The iteration order is:
    1. For each shifter state (starts at 0, walks through bits):
       2. For each counter state (starts at mask, decrements to 0):
          - yield (counter_state, shifter_state)
    """
    
    def __init__(self, counter_mask: bytes, shifter_mask: bytes):
        self.counter_mask = list(counter_mask)
        self.shifter_mask = list(shifter_mask)
        self.vec_size = len(counter_mask)
        
    def iterate(self) -> Iterator[Tuple[bytes, bytes]]:
        """Generate all (counter_state, shifter_state) pairs"""
        
        # Initialize counter to mask values (idea.asm starts counter at mask)
        counter = self.counter_mask.copy()
        
        # Initialize shifter to zero (idea.asm starts with no shifter bits set)
        shifter = [0] * (self.vec_size + 1)  # Extra byte for shiftend sentinel
        shifter_pos = 0
        
        while True:
            # Run through all counter combinations with current shifter
            while True:
                yield (bytes(counter), bytes(shifter[:self.vec_size]))
                
                # NextCounter: Decrement counter with borrow
                if not self._next_counter(counter):
                    # Counter exhausted, reset to mask
                    counter = self.counter_mask.copy()
                    break
            
            # NextShifter: Advance shifter to next bit
            if not self._next_shifter(shifter, shifter_pos):
                # Shifter exhausted, we're done
                return
            
            # Find new shifter_pos (first non-zero byte's position after update)
            shifter_pos = self._find_shifter_pos(shifter)
    
    def _next_counter(self, counter: list) -> bool:
        """
        Multibyte counter decrement (idea.asm lines 192-206).
        Returns True if more combinations available, False if exhausted.
        """
        for i in range(self.vec_size):
            if counter[i] == 0:
                # Reset to mask and carry to next byte
                counter[i] = self.counter_mask[i]
                continue
            # Decrement within mask
            counter[i] = (counter[i] - 1) & self.counter_mask[i]
            return True
        return False
    
    def _next_shifter(self, shifter: list, pos: int) -> bool:
        """
        Multibyte shifter advance (idea.asm lines 208-233).
        Uses dual-formula bit walking.
        """
        while pos < self.vec_size:
            val = shifter[pos]
            mask = self.shifter_mask[pos]
            
            if mask == 0:
                pos += 1
                continue
            
            # Dual formula from idea.asm:
            # If val == 0: new = ((mask - 1) ^ mask) & mask (lowest bit)
            # If val != 0: new = ((mask - 2*val) ^ mask) & mask (walk to next)
            if val == 0:
                new_val = ((mask - 1) ^ mask) & mask
            else:
                # Note: mask - 2*val may underflow, that's intentional
                neg_2val = (0 - (val << 1)) & 0xFF
                new_val = ((neg_2val + mask) ^ mask) & mask
            
            if new_val != 0:
                shifter[pos] = new_val
                # Clear all bytes before this position
                for j in range(pos):
                    shifter[j] = 0
                return True
            
            # This byte exhausted, clear and move to next
            shifter[pos] = 0
            pos += 1
        
        return False
    
    def _find_shifter_pos(self, shifter: list) -> int:
        """Find the position of the active shifter byte"""
        for i in range(self.vec_size):
            if shifter[i] != 0:
                return i
        return 0

# =============================================================================
# SCF Execution Reference
# =============================================================================

def execute_scf_reference(f_in: int, a: int) -> int:
    """
    Reference SCF implementation matching Zilog Z80 behavior.
    
    SCF behavior:
    - C = 1 (set)
    - N = 0 (reset)
    - H = 0 (reset)
    - S, Z, P/V = unchanged
    - X (bit 3), Y (bit 5) = (A | (F & 0x28)) & 0x28
    
    Note: This matches the "all" variant of z80test which tests
    the undocumented XY flags. The XY behavior for SCF on Zilog:
    XY = (A OR (F_prev AND 0x28))
    """
    FLAG_C = 0x01
    FLAG_N = 0x02
    FLAG_H = 0x10
    FLAG_XY = 0x28  # bits 3 and 5
    
    f_out = f_in
    
    # Clear H, N
    f_out &= ~(FLAG_H | FLAG_N)
    
    # Set C
    f_out |= FLAG_C
    
    # XY flags: combine from A and previous F
    # This is the Zilog behavior - XY = (A | prev_F) & 0x28
    xy = (a | (f_in & FLAG_XY)) & FLAG_XY
    f_out = (f_out & ~FLAG_XY) | xy
    
    return f_out

# =============================================================================
# Main Test Runner
# =============================================================================

def run_scf_test(verbose: bool = False, dump_file: str = None):
    """
    Run through all SCF test iterations.
    Returns (results, computed_crc).
    """
    iterator = Z80TestIterator(SCF_COUNTER, SCF_SHIFTER)
    
    crc = crc32_init()
    results = []
    expected_flags = []  # For binary dump
    iteration = 0
    
    for counter_state, shifter_state in iterator.iterate():
        # Combine: working = base XOR counter XOR shifter
        combined = bytes([
            SCF_BASE[i] ^ counter_state[i] ^ shifter_state[i]
            for i in range(VEC_SIZE)
        ])
        
        # Extract register values from combined vector
        # Layout: [op0,op1,op2,op3, F, A, C,B, E,D, L,H, XL,XH, YL,YH, MEMlo,MEMhi, SPlo,SPhi]
        f_in = combined[4]
        a_in = combined[5]
        bc = combined[6] | (combined[7] << 8)
        de = combined[8] | (combined[9] << 8)
        hl = combined[10] | (combined[11] << 8)
        ix = combined[12] | (combined[13] << 8)
        iy = combined[14] | (combined[15] << 8)
        mem = combined[16] | (combined[17] << 8)
        sp = combined[18] | (combined[19] << 8)
        
        # Execute SCF
        f_out = execute_scf_reference(f_in, a_in)
        
        # Build 16-byte state for CRC (order: F,A,C,B,E,D,L,H,XL,XH,YL,YH,MEMlo,MEMhi,SPlo,SPhi)
        state = bytes([
            f_out,
            a_in,  # A unchanged by SCF
            bc & 0xff, (bc >> 8) & 0xff,
            de & 0xff, (de >> 8) & 0xff,
            hl & 0xff, (hl >> 8) & 0xff,
            ix & 0xff, (ix >> 8) & 0xff,
            iy & 0xff, (iy >> 8) & 0xff,
            mem & 0xff, (mem >> 8) & 0xff,
            sp & 0xff, (sp >> 8) & 0xff,
        ])
        
        # Collect expected flags for binary dump
        expected_flags.append(f_out)
        
        # Update CRC with flags only (for "allflags" mode)
        # Note: "all" mode would use crc32_update_buf(crc, state)
        crc = crc32_update(crc, f_out)
        
        results.append({
            'iteration': iteration,
            'a_in': a_in,
            'f_in': f_in,
            'f_out': f_out,
            'state': state,
        })
        
        if verbose and iteration < 20:
            print(f"[{iteration:4d}] A=0x{a_in:02X}, F_in=0x{f_in:02X} -> F_out=0x{f_out:02X}")
        
        iteration += 1
    
    print(f"\nTotal iterations: {iteration}")
    print(f"Computed CRC: 0x{crc:08X}")
    print(f"Expected CRC: 0x{EXPECTED_CRC:08X}")
    
    if crc == EXPECTED_CRC:
        print("✅ CRC MATCH!")
    else:
        print("❌ CRC MISMATCH")
        # Check if it's the allflags CRC
        allflags_crc = 0x3ec05634
        if crc == allflags_crc:
            print("   (It matches allflags CRC)")
    
    if dump_file:
        # Write CSV for human inspection
        with open(dump_file, 'w') as f:
            f.write("# SCF Test Expected Values\n")
            f.write("# iteration,A_in,F_in,F_expected\n")
            for r in results:
                f.write(f"{r['iteration']},{r['a_in']:02X},{r['f_in']:02X},{r['f_out']:02X}\n")
        print(f"Wrote {len(results)} results to {dump_file}")
        
        # Write binary for direct comparison with emulator dump
        bin_file = dump_file.replace('.csv', '.bin').replace('.txt', '.bin')
        if bin_file == dump_file:
            bin_file = dump_file + '.bin'
        with open(bin_file, 'wb') as f:
            f.write(bytes(expected_flags))
        print(f"Wrote {len(expected_flags)} expected flags to {bin_file}")
    
    return results, crc, expected_flags

def count_iterations():
    """Calculate expected iteration count"""
    counter_bits = sum(bin(b).count('1') for b in SCF_COUNTER)
    shifter_bits = sum(bin(b).count('1') for b in SCF_SHIFTER)
    
    counter_iters = 1 << counter_bits  # 2^bits
    shifter_iters = shifter_bits + 1   # bits + 1 for initial zero
    
    print(f"Counter bits: {counter_bits} -> {counter_iters} iterations")
    print(f"Shifter bits: {shifter_bits} -> {shifter_iters} iterations")
    print(f"Total: {counter_iters * shifter_iters}")
    
    return counter_iters * shifter_iters

if __name__ == "__main__":
    import sys
    
    verbose = "-v" in sys.argv or "--verbose" in sys.argv
    dump_file = None
    for arg in sys.argv[1:]:
        if not arg.startswith("-"):
            dump_file = arg
            break
    
    print("SCF Test Analysis")
    print("=" * 60)
    count_iterations()
    print()
    
    results, crc, expected = run_scf_test(verbose=verbose, dump_file=dump_file)
