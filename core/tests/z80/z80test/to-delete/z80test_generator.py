#!/usr/bin/env python3
"""
Z80Test Full Suite Reference Generator

Generates expected values for all z80test tests by:
1. Reusing the parser from extract_z80tests.py
2. Running the exact iteration logic from idea.asm
3. Outputting reference data (.bin and .csv) for emulator comparison

Usage:
    python3 z80test_generator.py --list              # List all tests
    python3 z80test_generator.py --verify            # Verify iteration counts
    python3 z80test_generator.py --test SCF          # Generate SCF test data
    python3 z80test_generator.py --all               # Generate all test data

Output goes to ./expected/ subdirectory by default.
"""

import os
import sys
import argparse
import json
from typing import List, Dict, Tuple, Iterator

# Import the existing parser
from extract_z80tests import extract_tests, TESTS_ASM

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
    return CRC_TABLE[(crc ^ byte) & 0xFF] ^ (crc >> 8)

def crc32_update_buf(crc: int, data: bytes) -> int:
    for b in data:
        crc = CRC_TABLE[(crc ^ b) & 0xFF] ^ (crc >> 8)
    return crc

# =============================================================================
# Convert parsed test to 20-byte vectors
# =============================================================================

VEC_SIZE = 20

def vec_to_bytes(vec: dict) -> bytes:
    """Convert parsed vec dict to 20-byte array matching idea.asm layout"""
    # Layout: [op0,op1,op2,op3, F, A, C,B, E,D, L,H, XL,XH, YL,YH, MEMlo,MEMhi, SPlo,SPhi]
    return bytes([
        vec['opcode'][0] & 0xff,
        vec['opcode'][1] & 0xff,
        vec['opcode'][2] & 0xff,
        vec['opcode'][3] & 0xff,
        vec['f'] & 0xff,
        vec['a'] & 0xff,
        vec['bc'] & 0xff, (vec['bc'] >> 8) & 0xff,
        vec['de'] & 0xff, (vec['de'] >> 8) & 0xff,
        vec['hl'] & 0xff, (vec['hl'] >> 8) & 0xff,
        vec['ix'] & 0xff, (vec['ix'] >> 8) & 0xff,
        vec['iy'] & 0xff, (vec['iy'] >> 8) & 0xff,
        vec['mem'] & 0xff, (vec['mem'] >> 8) & 0xff,
        vec['sp'] & 0xff, (vec['sp'] >> 8) & 0xff,
    ])

def count_bits(data: bytes) -> int:
    """Count total set bits in byte array"""
    return sum(bin(b).count('1') for b in data)

def iteration_count(counter: bytes, shifter: bytes) -> int:
    """Calculate total iterations for a test"""
    counter_bits = count_bits(counter)
    shifter_bits = count_bits(shifter)
    return (1 << counter_bits) * (shifter_bits + 1)

# =============================================================================
# Iteration Engine (exact port of idea.asm)
# =============================================================================

class Z80TestIterator:
    """Exact port of the counter/shifter logic from idea.asm"""
    
    def __init__(self, counter_mask: bytes, shifter_mask: bytes):
        self.counter_mask = list(counter_mask)
        self.shifter_mask = list(shifter_mask)
        self.vec_size = len(counter_mask)
        
    def iterate(self) -> Iterator[Tuple[bytes, bytes]]:
        """Generate all (counter_state, shifter_state) pairs"""
        counter = self.counter_mask.copy()
        shifter = [0] * (self.vec_size + 1)
        
        while True:
            while True:
                yield (bytes(counter), bytes(shifter[:self.vec_size]))
                if not self._next_counter(counter):
                    counter = self.counter_mask.copy()
                    break
            
            if not self._next_shifter(shifter):
                return
    
    def _next_counter(self, counter: list) -> bool:
        for i in range(self.vec_size):
            if counter[i] == 0:
                counter[i] = self.counter_mask[i]
                continue
            counter[i] = (counter[i] - 1) & self.counter_mask[i]
            return True
        return False
    
    def _next_shifter(self, shifter: list) -> bool:
        """Walk through shifter bits sequentially across all bytes (idea.asm lines 208-233)"""
        # Find current position: the byte with the current active bit
        pos = 0
        for i in range(self.vec_size):
            if shifter[i] != 0:
                pos = i
                break
        
        while pos < self.vec_size:
            val = shifter[pos]
            mask = self.shifter_mask[pos]
            
            if mask == 0:
                pos += 1
                continue
            
            if val == 0:
                # First bit of this byte: isolate lowest set bit
                new_val = ((mask - 1) ^ mask) & mask
            else:
                # Walk to next bit in same byte: dual formula from idea.asm
                neg_2val = (0 - (val << 1)) & 0xFF
                new_val = ((neg_2val + mask) ^ mask) & mask
            
            if new_val != 0:
                shifter[pos] = new_val
                return True
            
            # This byte exhausted, move to next byte with mask bits
            shifter[pos] = 0
            pos += 1
        
        return False

# =============================================================================
# Test Runner
# =============================================================================

# Try to import reference executor
try:
    from z80_reference import execute_test, has_executor, list_executors
    HAS_REFERENCE = True
except ImportError:
    HAS_REFERENCE = False
    def execute_test(name, vec): return -1
    def has_executor(name): return False
    def list_executors(): return []

def run_test(test: dict, output_dir: str = None, verbose: bool = False) -> dict:
    """
    Run a single test and generate reference data.
    
    If a reference executor is available, computes expected output flags
    and verifies CRC against expected value from tests.asm.
    
    Returns dict with results.
    """
    if len(test.get('vecs', [])) < 3:
        return {'error': 'Incomplete test - fewer than 3 vectors'}
    
    base = vec_to_bytes(test['vecs'][0])
    counter = vec_to_bytes(test['vecs'][1])
    shifter = vec_to_bytes(test['vecs'][2])
    
    name = test.get('name', test.get('label', 'unknown'))
    crcs = test.get('crcs', {})
    expected_iter = iteration_count(counter, shifter)
    
    iterator = Z80TestIterator(counter, shifter)
    
    # Check if we have a reference executor for this test
    has_ref = has_executor(name)
    
    # Collect all combined vectors and expected F values
    combined_list = []
    input_flags = []
    output_flags = []
    
    for counter_state, shifter_state in iterator.iterate():
        combined = bytes([
            base[i] ^ counter_state[i] ^ shifter_state[i]
            for i in range(VEC_SIZE)
        ])
        combined_list.append(combined)
        input_flags.append(combined[4])  # F register at position 4
        
        # Execute reference if available
        if has_ref:
            f_out = execute_test(name, combined)
            output_flags.append(f_out if f_out >= 0 else combined[4])
        else:
            output_flags.append(-1)  # No reference available
    
    actual_iter = len(combined_list)
    
    # Compute CRC on input flags
    input_crc = crc32_init()
    for f in input_flags:
        input_crc = crc32_update(input_crc, f)
    
    # Compute CRC on output flags (if reference available)
    output_crc = 0
    if has_ref:
        output_crc = crc32_init()
        for f in output_flags:
            output_crc = crc32_update(output_crc, f)
    
    expected_crc = crcs.get('allflags', 0)
    crc_match = has_ref and (output_crc == expected_crc)
    
    result = {
        'name': name,
        'label': test.get('label', ''),
        'iterations': actual_iter,
        'expected_iterations': expected_iter,
        'iteration_match': actual_iter == expected_iter,
        'has_reference': has_ref,
        'input_flags_crc': input_crc,
        'output_flags_crc': output_crc if has_ref else None,
        'expected_crc_allflags': expected_crc,
        'crc_match': crc_match,
    }
    
    if verbose:
        print(f"  First 5 iterations:")
        for i in range(min(5, len(combined_list))):
            c = combined_list[i]
            f_out = output_flags[i] if has_ref else '??'
            f_out_str = f"0x{f_out:02X}" if isinstance(f_out, int) and f_out >= 0 else f_out
            print(f"    [{i}] F_in=0x{c[4]:02X} A=0x{c[5]:02X} -> F_out={f_out_str}")
    
    # Save to files if output directory specified
    if output_dir:
        safe_name = name.replace(' ', '_').replace('/', '_').replace(',', '').replace("'", "").replace("(", "").replace(")", "").replace('[', '').replace(']', '')
        
        # Binary file: all combined vectors
        bin_path = os.path.join(output_dir, f"{safe_name}.bin")
        with open(bin_path, 'wb') as f:
            for vec in combined_list:
                f.write(vec)
        
        # CSV file: Combined input AND expected output
        csv_path = os.path.join(output_dir, f"{safe_name}.csv")
        with open(csv_path, 'w') as f:
            if has_ref:
                f.write("# Z80Test Reference Values (Zilog Z80)\n")
                f.write("# iteration,F_in,A_in,F_expected\n")
                for i, vec in enumerate(combined_list):
                    f_in = vec[4]
                    a_in = vec[5]
                    f_exp = output_flags[i]
                    f.write(f"{i},{f_in:02X},{a_in:02X},{f_exp:02X}\n")
            else:
                f.write("# Z80Test Input Vectors (no reference executor)\n")
                f.write("# iteration,F_in,A_in,BC_in,DE_in,HL_in\n")
                for i, vec in enumerate(combined_list):
                    f_in = vec[4]
                    a_in = vec[5]
                    bc_in = vec[6] | (vec[7] << 8)
                    de_in = vec[8] | (vec[9] << 8)
                    hl_in = vec[10] | (vec[11] << 8)
                    f.write(f"{i},{f_in:02X},{a_in:02X},{bc_in:04X},{de_in:04X},{hl_in:04X}\n")
        
        # Also save expected flags binary for tests with reference
        if has_ref:
            exp_bin_path = os.path.join(output_dir, f"{safe_name}_expected.bin")
            with open(exp_bin_path, 'wb') as f:
                f.write(bytes(output_flags))
            result['expected_bin_file'] = exp_bin_path
        
        result['bin_file'] = bin_path
        result['csv_file'] = csv_path
    
    return result

# =============================================================================
# Main
# =============================================================================

def main():
    parser = argparse.ArgumentParser(description='Z80Test Reference Generator')
    parser.add_argument('--list', action='store_true', help='List all tests')
    parser.add_argument('--verify', action='store_true', help='Verify all test iteration counts')
    parser.add_argument('--test', type=str, help='Generate data for specific test (partial match)')
    parser.add_argument('--all', action='store_true', help='Generate data for all tests')
    parser.add_argument('--output', type=str, default='./expected', help='Output directory')
    parser.add_argument('--verbose', '-v', action='store_true', help='Verbose output')
    args = parser.parse_args()
    
    # Parse tests using existing extractor
    print(f"Parsing {TESTS_ASM}...")
    tests = extract_tests(TESTS_ASM)
    # Filter out incomplete tests
    tests = [t for t in tests if len(t.get('vecs', [])) >= 3]
    print(f"Found {len(tests)} complete tests\n")
    
    if args.list:
        print("Available tests:")
        print("-" * 70)
        for t in tests:
            base = vec_to_bytes(t['vecs'][0])
            counter = vec_to_bytes(t['vecs'][1])
            shifter = vec_to_bytes(t['vecs'][2])
            iters = iteration_count(counter, shifter)
            name = t.get('name', t.get('label', '?'))
            print(f"  {name:40s} {iters:10d} iterations")
        return
    
    if args.verify:
        print("Verifying test iteration counts...")
        print("-" * 70)
        passed = 0
        failed = 0
        for t in tests:
            base = vec_to_bytes(t['vecs'][0])
            counter = vec_to_bytes(t['vecs'][1])
            shifter = vec_to_bytes(t['vecs'][2])
            expected = iteration_count(counter, shifter)
            
            iterator = Z80TestIterator(counter, shifter)
            actual = sum(1 for _ in iterator.iterate())
            
            name = t.get('name', t.get('label', '?'))
            if actual == expected:
                passed += 1
                if args.verbose:
                    print(f"  ✅ {name:40s} {expected:10d} iterations")
            else:
                failed += 1
                print(f"  ❌ {name:40s} expected={expected} actual={actual}")
        
        print("-" * 70)
        print(f"Passed: {passed}, Failed: {failed}")
        return
    
    # Generate test data
    test_list = tests
    if args.test:
        test_list = [t for t in tests 
                     if args.test.lower() in t.get('name', '').lower() 
                     or args.test.lower() in t.get('label', '').lower()]
        if not test_list:
            print(f"No tests matching '{args.test}'")
            return
    elif not args.all:
        print("Use --list, --verify, --test NAME, or --all")
        return
    
    # Create output directory
    output_dir = os.path.abspath(args.output)
    os.makedirs(output_dir, exist_ok=True)
    print(f"Output directory: {output_dir}\n")
    
    print(f"Generating reference data for {len(test_list)} tests...")
    print("=" * 70)
    
    summary = []
    for test in test_list:
        name = test.get('name', test.get('label', '?'))
        crcs = test.get('crcs', {})
        
        print(f"\n{name}")
        result = run_test(test, output_dir=output_dir, verbose=args.verbose)
        
        if 'error' in result:
            print(f"  ❌ {result['error']}")
            continue
        
        print(f"  Iterations: {result['iterations']}")
        print(f"  Expected CRC (allflags): 0x{crcs.get('allflags', 0):08X}")
        
        if result.get('bin_file'):
            print(f"  Output: {os.path.basename(result['bin_file'])}")
        
        summary.append(result)
    
    print("\n" + "=" * 70)
    print(f"Generated {len(summary)} test reference files in {output_dir}")
    
    # Save summary JSON
    summary_path = os.path.join(output_dir, "_summary.json")
    with open(summary_path, 'w') as f:
        json.dump(summary, f, indent=2, default=str)
    print(f"Summary: {summary_path}")

if __name__ == "__main__":
    main()
