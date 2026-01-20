from extract_z80tests import extract_tests, TESTS_ASM
from z80test_generator import vec_to_bytes, Z80TestIterator, VEC_SIZE
from z80_reference import execute_test

tests = extract_tests(TESTS_ASM)
for t in tests:
    if t.get('name') != 'LD A,R': continue
    
    print(f"Test: {t.get('name')}")
    
    base = vec_to_bytes(t['vecs'][0])
    counter = vec_to_bytes(t['vecs'][1])
    shifter = vec_to_bytes(t['vecs'][2])
    
    print(f"Directives: {t}")
    
    iterator = Z80TestIterator(counter, shifter)
    
    for i, (cs, ss) in enumerate(iterator.iterate()):
        combined = bytes([base[j] ^ cs[j] ^ ss[j] for j in range(VEC_SIZE)])
        
        # opcode is ED 4F ED 5F
        
        f_out = execute_test('LD A,R', combined)
        
        if i < 5:
            print(f"[{i}] Opcode: {combined[0]:02X} {combined[1]:02X}")
            print(f"    F_out: {f_out:02X}")
            
    break
