#!/usr/bin/env python3
"""Print the ROM 48K signature catalog (core/src/debugger/analyzers/analysis/signatures/rom48.cpp entries) from a
48K ROM image: the first bytes of each routine, with the absolute operands of JP / CALL / LD (nn) masked out, so a
relocated copy of a routine matches as well. Needs SkoolKit (its disassembler finds the instruction boundaries).

  make_rom48_signatures.py data/rom/48.rom
"""
import sys
import types

from skoolkit.disassembler import Disassembler

# (label, address, bytes in the pattern, title)
ROUTINES = [
    ('START', 0x0000, 8, 'ROM: reset'),
    ('ERROR_1', 0x0008, 8, 'ROM: report an error (RST $08)'),
    ('PRINT_A_1', 0x0010, 3, 'ROM: print a character (RST $10)'),
    ('GET_CHAR', 0x0018, 8, 'ROM: collect a character (RST $18)'),
    ('NEXT_CHAR', 0x0020, 8, 'ROM: collect the next character (RST $20)'),
    ('FP_CALC', 0x0028, 3, 'ROM: the calculator (RST $28)'),
    ('MASK_INT', 0x0038, 12, 'ROM: the maskable interrupt (RST $38)'),
    ('RESET_NMI', 0x0066, 8, 'ROM: the NMI'),
    ('KEY_SCAN', 0x028E, 12, 'ROM: scan the keyboard'),
    ('KEYBOARD', 0x02BF, 12, 'ROM: the keyboard interrupt routine'),
    ('BEEPER', 0x03B5, 12, 'ROM: make a sound'),
    ('BEEP', 0x03F8, 12, 'ROM: BEEP'),
    ('SA_BYTES', 0x04C2, 12, 'ROM: save bytes'),
    ('LD_BYTES', 0x0556, 12, 'ROM: load bytes'),
    ('PRINT_OUT', 0x09F4, 12, 'ROM: the print routine of the main channels'),
    ('PO_MSG', 0x0C0A, 10, 'ROM: print a message'),
    ('CLS', 0x0D6B, 12, 'ROM: clear the screen'),
    ('CL_ALL', 0x0DAF, 12, 'ROM: clear the whole display'),
    ('CL_LINE', 0x0E44, 12, 'ROM: clear lines'),
    ('CL_ADDR', 0x0E9B, 12, 'ROM: the display address of a line'),
    ('PRINT_A_2', 0x15F2, 8, 'ROM: print a character'),
    ('CHAN_OPEN', 0x1601, 12, 'ROM: open a channel'),
    ('PR_STRING', 0x203C, 8, 'ROM: print a string'),
    ('STACK_A', 0x2D28, 6, 'ROM: stack A'),
    ('STACK_BC', 0x2D2B, 12, 'ROM: stack BC'),
    ('PRINT_FP', 0x2DE3, 12, 'ROM: print a floating-point number'),
]
# Opcodes whose 2-byte operand is an absolute address (masked out of the pattern)
ADDRESS_OPS = {0xC3, 0xCD, 0x22, 0x2A, 0x32, 0x3A} | {0xC2 + 8 * i for i in range(8)} | {0xC4 + 8 * i for i in range(8)}
ADDRESS_ED = {0x43, 0x4B, 0x53, 0x5B, 0x73, 0x7B}


class Instruction:
    def __init__(self, address, operation, data):
        self.address, self.operation, self.bytes = address, operation, list(data)


def main():
    rom = list(open(sys.argv[1], 'rb').read()) + [0] * 0x10000
    rom = rom[:0x10000]
    config = types.SimpleNamespace(defb_size=8, defm_size=66, defw_size=1, handle_rst=False, wrap=False,
                                   imaker=Instruction, opcodes='', asm_hex=True, asm_lower=False)
    disassembler = Disassembler(rom, config)
    for label, address, size, title in ROUTINES:
        pattern = []
        for ins in disassembler.disassemble(address, address + size + 4, 'h'):
            b = ins.bytes
            masked = list('%02X' % x for x in b)
            op, ed = b[0], len(b) > 1 and b[0] == 0xED and b[1] in ADDRESS_ED
            if (op in ADDRESS_OPS and len(b) == 3) or (ed and len(b) == 4):
                masked[-2:] = ['??', '??']
            pattern += masked
            if len(pattern) >= size:
                break
        print('    {"rom48.%s", "rom48", "%s", "%s", 0x%04X, 0, RegionKind::Code, SignatureCheck::AtAddress, "%s"},'
              % (label.lower().replace('_', '-'), label, ' '.join(pattern), address, title))
    glyphs = ' '.join('%02X' % x for x in rom[0x3D00:0x3D10])
    print('    {"rom48.charset", "rom48", "CHARSET", "%s", 0x3D00, 768, RegionKind::Font, SignatureCheck::AtAddress, '
          '"ROM: the character set (96 x 8 x 8)"},' % glyphs)


if __name__ == '__main__':
    main()
