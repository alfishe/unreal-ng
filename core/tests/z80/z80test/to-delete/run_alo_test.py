#!/usr/bin/env python3
from extract_z80tests import extract_tests, TESTS_ASM
from z80test_generator import run_test

tests = extract_tests(TESTS_ASM)
for t in tests:
    if t.get('name') == 'ALO A,(HL)':
        run_test(t, verbose=True)
