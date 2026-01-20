#!/usr/bin/env python3
"""
Z80Test Step-by-Step Comparison Tool

Compares emulator dump against Python reference data for any z80test case.

Usage:
    python3 z80test_compare.py /tmp/scf_ccf_flags.bin --test "SCF+CCF"
    python3 z80test_compare.py /tmp/scf_flags.bin --test "SCF"
    python3 z80test_compare.py --all                # Compare all available
"""

import argparse
import os
import sys
from pathlib import Path
from typing import Tuple, Optional

# Import reference implementation
from z80_reference import execute_test_state, has_executor, Z80

# Import iteration logic from generator
from z80test_reference_generator import (
    Z80TestIterator, VEC_SIZE, HARDCODED_VECTORS,
    SCF_CCF_BASE, SCF_CCF_COUNTER, SCF_CCF_SHIFTER
)

# ============================================================================
# Flag Formatting
# ============================================================================

FLAG_NAMES = ['C', 'N', 'P', 'X', 'H', 'Y', 'Z', 'S']

def format_flags(f: int) -> str:
    """Format flag byte as human-readable string"""
    result = ''
    for i in range(7, -1, -1):
        if f & (1 << i):
            result += FLAG_NAMES[7-i]
        else:
            result += '-'
    return result

# ============================================================================
# Comparison Logic
# ============================================================================

def compare_with_reference(
    emu_dump_file: str,
    test_name: str,
    base: bytes,
    counter: bytes,
    shifter: bytes,
    max_mismatches: int = 10,
    context_lines: int = 3
) -> Tuple[int, int, Optional[dict]]:
    """
    Compare emulator dump against Python reference.
    
    Returns: (total_iterations, mismatch_count, first_mismatch_info)
    """
    with open(emu_dump_file, 'rb') as f:
        emu_data = f.read()
    
    iterator = Z80TestIterator(counter, shifter)
    
    mismatches = 0
    first_mismatch = None
    iterations_data = []  # Store for context
    
    for iteration, (counter_state, shifter_state) in enumerate(iterator.iterate()):
        if iteration >= len(emu_data):
            print(f"ERROR: Emulator file too short at iteration {iteration}")
            break
        
        # Combine vectors
        combined = bytes([
            base[i] ^ counter_state[i] ^ shifter_state[i]
            for i in range(VEC_SIZE)
        ])
        
        # Get input values
        f_in = combined[5]  # Offset 5 is F in the vector layout
        a_in = combined[6]  # Offset 6 is A
        
        # Actually for z80test vector layout:
        # [0-4]: opcode, [5]: F, [6]: A, [7]: C, [8]: B, ...
        # But the vector builder may use different layout
        # Let's use the standard from z80_reference.py Z80.from_vec()
        f_in = combined[4] if len(combined) > 4 else 0
        a_in = combined[5] if len(combined) > 5 else 0
        
        # Get expected from reference
        state = execute_test_state(test_name, combined)
        expected_f = state[0] if state else 0
        
        # Get emulator value
        emu_f = emu_data[iteration]
        
        # Store for context
        iterations_data.append({
            'iteration': iteration,
            'a_in': a_in,
            'f_in': f_in,
            'expected_f': expected_f,
            'emu_f': emu_f,
            'match': expected_f == emu_f
        })
        
        if expected_f != emu_f:
            if first_mismatch is None:
                first_mismatch = {
                    'iteration': iteration,
                    'a_in': a_in,
                    'f_in': f_in,
                    'expected_f': expected_f,
                    'emu_f': emu_f,
                }
            mismatches += 1
    
    total = len(emu_data)
    
    # Print results
    if first_mismatch:
        print("=" * 70)
        print("FIRST MISMATCH FOUND!")
        print("=" * 70)
        d = first_mismatch
        print(f"Iteration:    {d['iteration']}")
        print(f"Input:        A=0x{d['a_in']:02X}, F=0x{d['f_in']:02X}")
        print(f"Expected F:   0x{d['expected_f']:02X} ({format_flags(d['expected_f'])})")
        print(f"Got F:        0x{d['emu_f']:02X} ({format_flags(d['emu_f'])})")
        diff = d['expected_f'] ^ d['emu_f']
        print(f"Difference:   0x{diff:02X} ({format_flags(diff)})")
        print()
        print(f"Test: {test_name}")
        print("=" * 70)
        print()
        print(f"Total mismatches: {mismatches} / {total}")
        
        # Show context
        if iterations_data:
            idx = d['iteration']
            start = max(0, idx - context_lines)
            end = min(len(iterations_data), idx + context_lines + 2)
            
            print(f"\nContext around iteration {idx}:")
            for i in range(start, end):
                data = iterations_data[i]
                marker = " <-- MISMATCH" if not data['match'] else ""
                print(f"  [{data['iteration']:5d}] A=0x{data['a_in']:02X} F_in=0x{data['f_in']:02X} -> "
                      f"expected=0x{data['expected_f']:02X} got=0x{data['emu_f']:02X}{marker}")
    else:
        print("✅ All values match!")
    
    return (total, mismatches, first_mismatch)

# ============================================================================
# Main
# ============================================================================

def main():
    parser = argparse.ArgumentParser(description='Z80Test Step-by-Step Comparison')
    parser.add_argument('dump_file', nargs='?', help='Emulator dump file to compare')
    parser.add_argument('--test', type=str, help='Test name (e.g., "SCF+CCF")')
    parser.add_argument('--all', action='store_true', help='Compare all available tests')
    parser.add_argument('--expected-dir', type=str, default='expected', 
                        help='Directory with reference .bin files')
    parser.add_argument('--dump-dir', type=str, default='/tmp',
                        help='Directory with emulator dump files')
    args = parser.parse_args()
    
    if args.all:
        # Compare all available tests
        print("Comparing all available tests...\n")
        
        for test_name in HARDCODED_VECTORS.keys():
            safe_name = test_name.replace('+', '_').replace(' ', '_').replace('(', '').replace(')', '')
            dump_file = Path(args.dump_dir) / f"{safe_name.lower()}_flags.bin"
            
            if not dump_file.exists():
                print(f"⚠️  {test_name}: No dump file ({dump_file})")
                continue
            
            base, counter, shifter = HARDCODED_VECTORS[test_name]
            
            print(f"\n{'='*70}")
            print(f"Test: {test_name}")
            print(f"{'='*70}")
            
            total, mismatches, _ = compare_with_reference(
                str(dump_file), test_name, base, counter, shifter, max_mismatches=1
            )
        
        return
    
    if not args.dump_file or not args.test:
        parser.print_help()
        print("\nExample:")
        print("  python3 z80test_compare.py /tmp/scf_ccf_flags.bin --test 'SCF+CCF'")
        return
    
    if args.test not in HARDCODED_VECTORS:
        print(f"Error: Test '{args.test}' not in hardcoded vectors")
        print("Available:", list(HARDCODED_VECTORS.keys()))
        sys.exit(1)
    
    base, counter, shifter = HARDCODED_VECTORS[args.test]
    
    print(f"Comparing: {args.dump_file}")
    print(f"Test: {args.test}")
    print()
    
    compare_with_reference(args.dump_file, args.test, base, counter, shifter)

if __name__ == "__main__":
    main()
