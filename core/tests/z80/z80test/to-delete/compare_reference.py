#!/usr/bin/env python3
"""
Generate reference data by running z80test vectors through z80_harness (z80.c)
and comparing against the Python implementation.

This identifies exactly which tests have mismatches and what the expected values are.
"""

import subprocess
import os
from extract_z80tests import extract_tests, TESTS_ASM
from z80test_generator import vec_to_bytes, Z80TestIterator, VEC_SIZE
from z80_reference import execute_test, has_executor

HARNESS_PATH = "/Volumes/TB4-4Tb/Projects/Test/unreal-ng/testdata/z80/z80-reference/z80_harness"

def run_harness(vectors: list) -> list:
    """Run test vectors through z80_harness and get F outputs."""
    input_data = "\n".join(vectors) + "\n"
    result = subprocess.run(
        [HARNESS_PATH],
        input=input_data,
        capture_output=True,
        text=True
    )
    outputs = []
    for line in result.stdout.strip().split('\n'):
        if line and not line.startswith('#'):
            try:
                outputs.append(int(line, 16))
            except ValueError:
                outputs.append(-1)
    return outputs

def vec_to_harness_format(vec: bytes) -> str:
    """Convert z80test vector to harness input format."""
    # Vector format from z80test:
    # 0-3: opcode (4 bytes)
    # 4: F
    # 5: A
    # 6: C
    # 7: B
    # 8: E
    # 9: D
    # 10: L
    # 11: H
    # 12: IXL
    # 13: IXH
    # 14: IYL
    # 15: IYH
    # 16: MEM_LO
    # 17: MEM_HI
    # 18: SPL
    # 19: SPH
    
    # Harness format:
    # OP0 OP1 OP2 OP3 A F B C D E H L IXH IXL IYH IYL SPL SPH MEM_LO MEM_HI
    parts = [
        f"{vec[0]:02X}", f"{vec[1]:02X}", f"{vec[2]:02X}", f"{vec[3]:02X}",
        f"{vec[5]:02X}", f"{vec[4]:02X}",  # A, F
        f"{vec[7]:02X}", f"{vec[6]:02X}",  # B, C
        f"{vec[9]:02X}", f"{vec[8]:02X}",  # D, E
        f"{vec[11]:02X}", f"{vec[10]:02X}",  # H, L
        f"{vec[13]:02X}", f"{vec[12]:02X}",  # IXH, IXL
        f"{vec[15]:02X}", f"{vec[14]:02X}",  # IYH, IYL
        f"{vec[18]:02X}", f"{vec[19]:02X}",  # SPL, SPH
        f"{vec[16]:02X}", f"{vec[17]:02X}"   # MEM_LO, MEM_HI
    ]
    return " ".join(parts)

def main():
    if not os.path.exists(HARNESS_PATH):
        print(f"ERROR: Harness not found at {HARNESS_PATH}")
        print("Please compile: cd z80-reference && gcc -O2 -o z80_harness z80_harness.c z80.c")
        return
    
    tests = extract_tests(TESTS_ASM)
    tests = [t for t in tests if len(t.get('vecs', [])) >= 3]
    
    print(f"Processing {len(tests)} tests...")
    
    passed = 0
    failed = 0
    mismatch_details = []
    
    for t in tests:
        name = t.get('name', '')
        
        if not has_executor(name):
            continue
        
        base = vec_to_bytes(t['vecs'][0])
        counter = vec_to_bytes(t['vecs'][1])
        shifter = vec_to_bytes(t['vecs'][2])
        
        iterator = Z80TestIterator(counter, shifter)
        
        # Collect first N vectors for this test
        MAX_SAMPLES = 10  # Sample first 10 to keep it fast
        vectors = []
        combined_vecs = []
        
        for i, (cs, ss) in enumerate(iterator.iterate()):
            combined = bytes([base[j] ^ cs[j] ^ ss[j] for j in range(VEC_SIZE)])
            vectors.append(vec_to_harness_format(combined))
            combined_vecs.append(combined)
            if i >= MAX_SAMPLES - 1:
                break
        
        # Run through harness
        harness_outputs = run_harness(vectors)
        
        # Compare with Python
        test_passed = True
        for i, combined in enumerate(combined_vecs):
            py_f = execute_test(name, combined)
            c_f = harness_outputs[i] if i < len(harness_outputs) else -1
            
            if py_f != c_f:
                test_passed = False
                if len(mismatch_details) < 50:  # Limit output
                    mismatch_details.append({
                        'test': name,
                        'iter': i,
                        'py_f': py_f,
                        'c_f': c_f,
                        'vec': combined[:8].hex()
                    })
        
        if test_passed:
            passed += 1
        else:
            failed += 1
    
    print(f"\nResults:")
    print(f"  PASSED: {passed}")
    print(f"  FAILED: {failed}")
    
    if mismatch_details:
        print(f"\nFirst {len(mismatch_details)} mismatches:")
        for m in mismatch_details:
            print(f"  {m['test']:30s} iter={m['iter']} py=0x{m['py_f']:02X} c=0x{m['c_f']:02X} vec={m['vec']}")

if __name__ == "__main__":
    main()
