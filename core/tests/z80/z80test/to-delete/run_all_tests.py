#!/usr/bin/env python3
from extract_z80tests import extract_tests, TESTS_ASM
from z80test_generator import run_test

tests = extract_tests(TESTS_ASM)
tests = [t for t in tests if len(t.get('vecs', [])) >= 3]
passed, failed = 0, []
for t in tests:
    name = t.get('name', '')
    r = run_test(t, verbose=False)
    if not r.get('has_reference'):
        continue  # Skip tests without reference
    if r.get('crc_match'):
        passed += 1
    else:
        failed.append((name, r['expected_crc_allflags'], r.get('output_flags_crc', 0)))

print(f"PASSED: {passed}")
print(f"FAILED: {len(failed)}")
print(f"TOTAL:  {passed + len(failed)}")
print()
print("=" * 60)
print("FAILING TESTS:")
print("=" * 60)
for name, exp, got in failed:
    print(f"  {name:40s} exp=0x{exp:08X} got=0x{got:08X}")
