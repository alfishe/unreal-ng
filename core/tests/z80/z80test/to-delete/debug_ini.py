from extract_z80tests import extract_tests, TESTS_ASM
from z80test_generator import vec_to_bytes, Z80TestIterator, VEC_SIZE
from z80_reference import execute_test, Z80

tests = extract_tests(TESTS_ASM)
t = [t for t in tests if t.get('name') == 'INI'][0]

print(f"Test: {t['name']}")
base = vec_to_bytes(t['vecs'][0])
counter = vec_to_bytes(t['vecs'][1])
shifter = vec_to_bytes(t['vecs'][2])

iterator = Z80TestIterator(counter, shifter)

print("Tracing first 10 iterations...")
for i, (cs, ss) in enumerate(iterator.iterate()):
    combined = bytes([base[j] ^ cs[j] ^ ss[j] for j in range(VEC_SIZE)])
    
    op = (combined[0], combined[1]) # ED A2
        
    f_in = combined[4]
    b = combined[7]
    c = combined[6]
    hl = combined[10] | (combined[11] << 8)
    mem_val = combined[16] # This is likely the value 'read' from IO or at (HL)?
    
    # In z80test for INI, does 'mem' represent the IO value read?
    # Usually z80test uses 'mem' to supply data for 'read' cycles.
    
    f_out = execute_test('INI', combined)
    
    if i < 10:
        print(f"[{i}] B={b:02X} C={c:02X} HL={hl:04X} MEM={mem_val:02X}")
        print(f"    F_in={f_in:02X} -> F_out={f_out:02X}")
        
    if i > 20: break
