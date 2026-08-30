#!/usr/bin/env python3
"""Generate src/main.sna from the same bytes as src/main.asm - a fallback so the
project is runnable WITHOUT any assembler. (With sjasmplus, the tasks.json
variant `make-sna (sjasmplus)` also builds src/main.sld; DeZog needs the .sld
for source-level mapping, but the emulator only needs the .sna.)

48K .SNA layout: 27-byte header + 48 KB RAM (0x4000..0xFFFF). A .sna has no
saved PC: loaders boot by executing RETN, which pops PC from (SP); we therefore
seed (SP)=start so the first RETN lands on `start` (address 0x8000).
"""
from pathlib import Path

START = 0x8000
# Hand-assembled to match src/main.asm (addresses follow from ORG 0x8000):
#   main_loop = 0x8004, delay = 0x8012, delay_loop = 0x8015,
#   counter = 0x801B, stack_top = 0x809C (stack seeded at SP=0x809A)
program = bytes([
    0xF3,                    # di
    0x31, 0x9C, 0x80,        # ld sp, stack_top (0x809C)
    0xD3, 0xFE,              # out (0xFE), a       <- main_loop = 0x8004
    0x32, 0x1B, 0x80,        # ld (counter), a
    0x3C,                    # inc a
    0xE6, 0x07,              # and 0x07
    0xCD, 0x12, 0x80,        # call delay (0x8012)
    0xC3, 0x04, 0x80,        # jp main_loop
    0x01, 0x00, 0x40,        # ld bc, 0x4000       <- delay = 0x8012
    0x0B,                    # dec bc              <- delay_loop = 0x8015
    0x78,                    # ld a, b
    0xB1,                    # or c
    0x20, 0xFB,              # jr nz, delay_loop
    0xC9,                    # ret
    0x00,                    # counter: db 0       <- 0x801B
])

ram = bytearray(0xC000)                 # 0x4000..0xFFFF
ram[START - 0x4000: START - 0x4000 + len(program)] = program
# Seed the boot stack: (SP)=START so the SNA loader's RETN jumps to `start`.
sp = 0x809A
ram[sp - 0x4000] = START & 0xFF
ram[sp - 0x4000 + 1] = START >> 8

header = bytearray(27)
header[0] = 0x3F            # I register
header[23] = sp & 0xFF      # SP low
header[24] = sp >> 8        # SP high
header[25] = 1              # IM 1
header[26] = 0              # border colour

out = Path(__file__).resolve().parent / "src" / "main.sna"
out.write_bytes(bytes(header) + bytes(ram))
print(f"wrote {out} ({out.stat().st_size} bytes), start=0x{START:04X}")
