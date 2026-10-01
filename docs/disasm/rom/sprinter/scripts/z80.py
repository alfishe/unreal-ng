"""Small Z80 helper: instruction length, control flow and operand masking.

Used by analyze.py (code reachability) and transfer.py (byte-pattern
matching of reference-source builds against the ROM, where absolute
16-bit operands are masked so relocated code still matches).
"""

# Unprefixed opcodes with a one-byte operand.
_LEN2 = {0x06, 0x0E, 0x16, 0x1E, 0x26, 0x2E, 0x36, 0x3E,
         0x10, 0x18, 0x20, 0x28, 0x30, 0x38,
         0xC6, 0xCE, 0xD6, 0xDE, 0xE6, 0xEE, 0xF6, 0xFE,
         0xD3, 0xDB, 0xCB}
# Unprefixed opcodes with a two-byte (address or immediate) operand.
_LEN3 = {0x01, 0x11, 0x21, 0x31, 0x22, 0x2A, 0x32, 0x3A,
         0xC2, 0xC3, 0xC4, 0xCA, 0xCC, 0xCD, 0xD2, 0xD4, 0xDA, 0xDC,
         0xE2, 0xE4, 0xEA, 0xEC, 0xF2, 0xF4, 0xFA, 0xFC}
# Opcodes that address (HL); with a DD/FD prefix they take a displacement.
_HLMEM = {0x34, 0x35, 0x36, 0x46, 0x4E, 0x56, 0x5E, 0x66, 0x6E, 0x7E,
          0x70, 0x71, 0x72, 0x73, 0x74, 0x75, 0x77,
          0x86, 0x8E, 0x96, 0x9E, 0xA6, 0xAE, 0xB6, 0xBE}
_ED4 = {0x43, 0x4B, 0x53, 0x5B, 0x63, 0x6B, 0x73, 0x7B}

JR_COND = {0x20, 0x28, 0x30, 0x38}
JP_COND = {0xC2, 0xCA, 0xD2, 0xDA, 0xE2, 0xEA, 0xF2, 0xFA}
CALL_ANY = {0xCD, 0xC4, 0xCC, 0xD4, 0xDC, 0xE4, 0xEC, 0xF4, 0xFC}
RET_COND = {0xC0, 0xC8, 0xD0, 0xD8, 0xE0, 0xE8, 0xF0, 0xF8}
RST = {0xC7, 0xCF, 0xD7, 0xDF, 0xE7, 0xEF, 0xF7, 0xFF}


def length(data, pos):
    """Length in bytes of the instruction at pos (a lone prefix counts as 1)."""
    b = data[pos]
    if b in (0xDD, 0xFD):
        if pos + 1 >= len(data):
            return 1
        n = data[pos + 1]
        if n in (0xDD, 0xFD, 0xED):
            return 1
        if n == 0xCB:
            return 4
        if n == 0x36:
            return 4
        base = 3 if n in _LEN3 else 2 if n in _LEN2 else 1
        return 1 + base + (1 if n in _HLMEM else 0)
    if b == 0xED:
        if pos + 1 >= len(data):
            return 1
        return 4 if data[pos + 1] in _ED4 else 2
    if b in _LEN3:
        return 3
    if b in _LEN2:
        return 2
    return 1


def flow(data, pos, org=0):
    """Control flow of the instruction at pos.

    Returns (targets, falls_through): targets are absolute CPU addresses of
    jump/call destinations (RST targets included); falls_through is False after
    an unconditional jump, RET, RETI/RETN, JP (HL)/(IX)/(IY) and HALT.
    """
    b = data[pos]
    here = org + pos
    if b in (0x18, 0x10) or b in JR_COND:
        off = data[pos + 1]
        t = here + 2 + (off - 256 if off >= 128 else off)
        return [t & 0xFFFF], b != 0x18
    if b == 0xC3 or b in JP_COND or b in CALL_ANY:
        t = data[pos + 1] | (data[pos + 2] << 8)
        return [t], b != 0xC3
    if b in RST:
        return [b & 0x38], True
    if b in (0xC9, 0xE9, 0x76):
        return [], False
    if b in (0xDD, 0xFD) and pos + 1 < len(data) and data[pos + 1] == 0xE9:
        return [], False
    if b == 0xED and pos + 1 < len(data) and data[pos + 1] in (0x45, 0x4D, 0x55, 0x5D, 0x65, 0x6D, 0x75, 0x7D):
        return [], False
    return [], True


def masked(data, pos, n):
    """Bytes of the instruction at pos with absolute 16-bit address operands
    of jumps, calls and memory loads replaced by zero (immediates kept)."""
    seg = bytearray(data[pos:pos + n])
    b = seg[0]
    if n == 3 and (b in (0xC3, 0x22, 0x2A, 0x32, 0x3A, 0x21, 0x11, 0x01, 0x31)
                   or b in JP_COND or b in CALL_ANY):
        seg[1] = seg[2] = 0
    elif n == 4 and b == 0xED and seg[1] in _ED4:
        seg[2] = seg[3] = 0
    elif n == 4 and b in (0xDD, 0xFD) and seg[1] in (0x21, 0x22, 0x2A):
        seg[2] = seg[3] = 0
    elif n == 2 and (b in (0x18, 0x10) or b in JR_COND):
        seg[1] = 0
    return bytes(seg)
