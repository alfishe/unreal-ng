from extract_z80tests import extract_tests, TESTS_ASM
from z80test_generator import vec_to_bytes, Z80TestIterator, VEC_SIZE
from z80_reference import execute_test, Z80

tests = extract_tests(TESTS_ASM)
t = [t for t in tests if t.get('name') == 'BIT N,(HL)'][0]

print(f"Test: {t['name']}")
base = vec_to_bytes(t['vecs'][0])
counter = vec_to_bytes(t['vecs'][1])
shifter = vec_to_bytes(t['vecs'][2])

iterator = Z80TestIterator(counter, shifter)

print("Tracing first 10 iterations...")
for i, (cs, ss) in enumerate(iterator.iterate()):
    combined = bytes([base[j] ^ cs[j] ^ ss[j] for j in range(VEC_SIZE)])
    
    op_cb = combined[1] # Opcode is CB xx
    bit = (op_cb >> 3) & 7
    r = op_cb & 7
    
    f_in = combined[4]
    
    mem_val = combined[16] # (HL) value
    # Check HL value
    hl = combined[10] | (combined[11] << 8)
    
    f_out = execute_test('BIT N,(HL)', combined)
    
    if i < 10:
        print(f"[{i}] Op=CB {op_cb:02X} (Bit {bit}, R={r})")
        print(f"    HL={hl:04X} (HL)={mem_val:02X}")
        print(f"    F_in={f_in:02X} -> F_out={f_out:02X}")
        
    if i > 20: break
