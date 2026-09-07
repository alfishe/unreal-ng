#!/usr/bin/env python3
"""
Hand-assemble paging_test.asm - COMPACT version with page restore.
"""

code = []

def emit(b):
    code.append(b)

def here():
    return len(code)

# ORG $5CD0 (23760)

# Write signature $DE $AD $BE $EF to $C000
# LD HL, $C000
emit(0x21); emit(0x00); emit(0xC0)
# LD (HL), $DE
emit(0x36); emit(0xDE)
# INC HL: LD (HL), $AD
emit(0x23); emit(0x36); emit(0xAD)
# INC HL: LD (HL), $BE
emit(0x23); emit(0x36); emit(0xBE)
# INC HL: LD (HL), $EF
emit(0x23); emit(0x36); emit(0xEF)

# Try switch to page 1
# LD BC, $7FFD
emit(0x01); emit(0xFD); emit(0x7F)
# LD A, 1
emit(0x3E); emit(0x01)
# OUT (C), A
emit(0xED); emit(0x79)

# Check if signature still visible at $C000
# LD A, ($C000): CP $DE
emit(0x3A); emit(0x00); emit(0xC0)
emit(0xFE); emit(0xDE)
# JR NZ, ok (signature gone = paging works)
jr1 = here()
emit(0x20); emit(0x00)  # patch later

# LD A, ($C001): CP $AD
emit(0x3A); emit(0x01); emit(0xC0)
emit(0xFE); emit(0xAD)
jr2 = here()
emit(0x20); emit(0x00)

# LD A, ($C002): CP $BE
emit(0x3A); emit(0x02); emit(0xC0)
emit(0xFE); emit(0xBE)
jr3 = here()
emit(0x20); emit(0x00)

# LD A, ($C003): CP $EF
emit(0x3A); emit(0x03); emit(0xC0)
emit(0xFE); emit(0xEF)
jr4 = here()
emit(0x20); emit(0x00)

# All matched = LOCKED
# XOR A: OUT (C), A  (restore page 0)
emit(0xAF)
emit(0xED); emit(0x79)
# LD A, 1 (return 1 = locked)
emit(0x3E); emit(0x01)
# RET
emit(0xC9)

# ok: paging works
ok = here()
# XOR A: OUT (C), A (restore page 0)
emit(0xAF)
emit(0xED); emit(0x79)
# XOR A (return 0 = OK)
emit(0xAF)
# RET
emit(0xC9)

# Patch JR offsets
for jr in [jr1, jr2, jr3, jr4]:
    offset = ok - (jr + 2)
    code[jr + 1] = offset

print(f"Code size: {len(code)} bytes")
print(f"ORG: $5CD0 (23760)")
print()

# Output as hex dump
print("Hex dump:")
for i in range(0, len(code), 16):
    addr = 0x5CD0 + i
    row = code[i:i+16]
    hex_str = " ".join(f"{b:02X}" for b in row)
    print(f"${addr:04X}: {hex_str}")
print()

# Output as BASIC
print(f"BASIC loader lines ({len(code)} bytes of MC):")
data_str = ",".join(str(b) for b in code)
print(f'11 FOR i=0 TO {len(code)-1}: READ d: POKE 23760+i,d: NEXT i')
print(f"12 DATA {data_str}")
print('13 IF USR 23760 THEN PRINT "PAGING LOCKED!": PRINT "Boot with RESET=BASIC": STOP')
print()

# Save binary
with open("docs/disasm/demo/scroller/paging_test.bin", "wb") as f:
    f.write(bytes(code))
print(f"Saved: paging_test.bin")
