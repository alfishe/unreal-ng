from extract_z80tests import extract_tests, TESTS_ASM
from z80test_generator import vec_to_bytes, Z80TestIterator, VEC_SIZE
from z80_reference import execute_test, Z80

t = [t for t in extract_tests(TESTS_ASM) if t.get('name') == 'LDI'][0]

base = vec_to_bytes(t['vecs'][0])
counter = vec_to_bytes(t['vecs'][1])
shifter = vec_to_bytes(t['vecs'][2])

iterator = Z80TestIterator(counter, shifter)

print("Searching for LDI iterations with BC=1 (PV changes to 0)")
for i, (cs, ss) in enumerate(iterator.iterate()):
    combined = bytes([base[j] ^ cs[j] ^ ss[j] for j in range(VEC_SIZE)])
    bc = combined[7] << 8 | combined[6]
    
    if bc == 1:
        f_out = execute_test('LDI', combined)
        pv = (f_out >> 2) & 1
        print(f"[{i}] BC={bc} -> BC'=0. P/V={pv} (Expect 0)")
        
    if bc == 2:
        f_out = execute_test('LDI', combined)
        pv = (f_out >> 2) & 1
        print(f"[{i}] BC={bc} -> BC'=1. P/V={pv} (Expect 1)")
        
    if i > 2500: break
