from extract_z80tests import extract_tests, TESTS_ASM

tests = extract_tests(TESTS_ASM)
t = [t for t in tests if t.get('name') == 'LDI'][0]

print("Counter vector (vec 1):")
print(t['vecs'][1])
print(f"Mem val in vec 1: 0x{t['vecs'][1]['mem']:04X}")

from z80test_generator import vec_to_bytes
vb = vec_to_bytes(t['vecs'][1])
print(f"Bytes 16,17: {vb[16]:02X} {vb[17]:02X}")

# Debug raw asm
print("Raw ASM check:")
idx = TESTS_ASM.find('a,0x09')
if idx != -1:
    print(f"Found 'a,0x09' at {idx}")
    print(f"Context: {TESTS_ASM[idx-50:idx+50]}")
else:
    print("'a,0x09' NOT FOUND in TESTS_ASM!")
