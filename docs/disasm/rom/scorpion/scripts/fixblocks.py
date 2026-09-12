#!/usr/bin/env python3
"""Apply hand-verified code/data fixes to the monitor block maps.

The analyzer's reachability pass systematically missed:
 - code after a final jr/ret (routine tails)
 - trampolines (ld bc,imm16; jr) and rst 18h wrapper pairs
 - routines only entered via jp (hl) / jump tables
so dozens of small (and some large) pockets were typed bytedata.
Each fix below was verified by decoding the bytes and cross-checking the
sister ROM, where the same routine sits with relocated call targets.
"""
import os
import re

HERE = os.path.dirname(os.path.abspath(__file__))

# ---- operations per file -------------------------------------------------
# delete: block goes away entirely (bytes become code)
# shrink: (name, new_start, new_end) - keep only part as data
# add:    extra bytedata blocks (name, start, end)
BASE_DELETE = """
blk010 blk011 blk012 blk013 blk014 blk015 blk016 blk017 blk018 blk019
blk021 blk022 blk023 blk024 blk025 blk026 blk027 blk028 blk029 blk030
blk031 blk032 blk033 blk034 blk035 blk036 blk037 blk038 blk042 blk043
blk044 blk045 blk046 blk047 blk048 blk049 blk051 blk052 blk053 blk054
blk055 blk056 blk057 blk058 blk059 blk060 blk061 blk062 blk063 blk064
blk065 blk066 blk067 blk068 blk069 blk070 blk071 blk072 blk074 blk075
blk076 blk077 blk078 blk079 blk080 blk081 blk082 blk083 blk085 blk086
blk087 blk088 blk089 blk090 blk091 blk094 blk100 blk101 blk102 blk103
blk104 blk105 blk106 blk107 blk108 blk109 blk110 blk111 blk112 blk113
blk114 blk115 blk118 blk119 blk120
""".split()
BASE_SHRINK = {
    'blk020': (0x07BA, 0x07E3),      # keep KeyJumpTable words, code wrapper before
    'blk098': (0x2BB2, 0x2BBD),      # keep only 'Int mode'+0x80 string
}
BASE_KEEP_MSG = []                    # filled by string scan below

S295_DELETE = """
blk006 blk007 blk009 blk010 blk011 blk012 blk013 blk014 blk015 blk016
blk017 blk018 blk020 blk021 blk022 blk023 blk024 blk025 blk027 blk028
blk029 blk030 blk031 blk032 blk033 blk034 blk035 blk036 blk037 blk038
blk039 blk040 blk041 blk045 blk046 blk047 blk048 blk049 blk050 blk051
blk053 blk054 blk055 blk056 blk057 blk058 blk059 blk060 blk061 blk062
blk063 blk064 blk065 blk066 blk067 blk068 blk069 blk070 blk071 blk072
blk074 blk075 blk076 blk077 blk078 blk079 blk080 blk081 blk085 blk091
blk092 blk093 blk094 blk095 blk096 blk097 blk098 blk099 blk100 blk101
blk103 blk104 blk105 blk106 blk107 blk109 blk110 blk111
""".split()
S295_SHRINK = {
    'blk008': (0x00F1, 0x00FC),      # junk before the XOR keys stays data
    'blk019': (0x07BA, 0x07E3),      # KeyJumpTable words
    'blk089': (0x2BE0, 0x2BEB),      # 'Int mode'+0x80 string
    'blk108': (0x34C7, 0x34CC),      # ' Bad'+0x80 string
}
S295_ADD = [('xorkeys', 0x00FC, 0x0100)]

# Prof 4.01: v4.01 is heavily rewritten (rst 30h RAM hooks, inline command
# token table at $23A3 as a linked list of {u16 next; u8 token; name 00;
# handler} records, RAM cells relocated to $E0xx).  Kept: blk001-005 (RST
# vector junk, base parity), blk032 (ld de,$0DEC table), blk039 (dead blob
# $1062-$1079, no refs), blk087/097/098/100/102/103 (strings/tables),
# blk122 (XOR-encoded tail + port-name strings).
PROF_DELETE = """
blk006 blk007 blk009 blk010 blk011 blk012 blk013 blk014 blk015 blk016
blk017 blk018 blk019 blk020 blk021 blk022 blk023 blk024 blk025 blk026
blk028 blk029 blk030 blk031 blk033 blk034 blk035 blk040 blk042 blk043
blk044 blk045 blk046 blk047 blk048 blk049 blk050 blk051 blk052 blk053
blk054 blk055 blk056 blk057 blk058 blk059 blk060 blk061 blk062 blk063
blk064 blk065 blk066 blk067 blk068 blk069 blk070 blk071 blk072 blk073
blk074 blk075 blk076 blk077 blk078 blk079 blk080 blk081 blk082 blk083
blk084 blk085 blk086 blk088 blk089 blk090 blk091 blk092 blk093 blk094
blk095 blk096 blk099 blk105 blk106 blk107 blk108 blk109 blk110 blk111 blk112
blk113 blk114 blk115 blk116 blk117 blk118 blk119 blk120 blk121
""".split()
PROF_SHRINK = {
    'blk008': (0x00F1, 0x0100),      # junk + XOR keys $FC-$FE, zeros freed
    'blk036': (0x0F4E, 0x0F67),      # key table 1 incl 00 terminator
    'blk037': (0x0F67, 0x0F89),      # key table 2 (was off by one)
    'blk038': (0x0F91, 0x0FEB),      # register name lists PC,SP.. + 2nd list
    'blk041': (0x1107, 0x14F6),      # MnemonicTable starts at $1107
    'blk101': (0x33D6, 0x33E0),      # 'Analyser '+0x80 string
    'blk104': (0x3415, 0x341F),      # '\rInt mode'+0x80 string
}
PROF_ADD = [
    ('scrtab', 0x344A, 0x344F),      # 00 02 04 06 81 (ld hl,$344A; ldir)
    ('regptrs', 0x27DA, 0x27FD),     # 13 row bytes + 11 window routine ptrs
]
PROF_NULSCAN = [(0x2398, 0x27DA)]   # command token table names (\0-terminated)
PROF_TOKEN_HEAD = 0x23A3           # list head (ld hl,$23A3 at $3AC8)
PROF_STR_EXCLUDE = {0x1C7A, 0x1F00, 0x2BD8}   # operand-byte false positives
PROF_EXTRA_SCAN = [(0x22F0, 0x2309)]          # 'eval*2' terminator beyond blk076


TEXT = set("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789 ,.:;!?()'\"<>=/_-*")
LET = set("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz ")


def texty(run):
    s = run.decode('latin-1')
    if not all(c in TEXT for c in s):
        return False
    if len(set(s)) == 1 and s[0] in '<>#*.=-':
        return True                     # separator/filler bars
    return sum(c in LET for c in s) * 2 >= len(s)


def scanStrings(binData, lo, hi, covered):
    """Bit-7 terminated ASCII messages (RST 20h convention) in [lo,hi)."""
    out = []
    i = lo
    while i < hi:
        if any(s <= i < e for s, e in covered):
            i += 1
            continue
        j = i
        while j < hi and 0x20 <= binData[j] <= 0x7E:
            j += 1
        if j > i + 2 and j < hi and binData[j] >= 0x80 and texty(binData[i:j]):
            out.append((i, j + 1))
            i = j + 1
        else:
            i = j + 1 if j > i else i + 1
    return out


def scanStrings0(binData, lo, hi, covered):
    """NUL-terminated ASCII names (v4.01 command token table) in [lo,hi)."""
    out = []
    i = lo
    while i < hi:
        if any(s <= i < e for s, e in covered):
            i += 1
            continue
        j = i
        while j < hi and 0x20 <= binData[j] <= 0x7E:
            j += 1
        if j > i + 1 and j < hi and binData[j] == 0:
            out.append((i, j + 1))
            i = j + 1
        else:
            i = j + 1 if j > i else i + 1
    return out


def walkTokenRecords(binData, head, limit):
    """Walk the v4.01 command table linked list: each record is
    {u16 next; u8 token; name[] 00; handler}.  Returns (rec_start, end_of_00)
    ranges so the defb covers the whole header and disassembly resumes
    cleanly at the handler.  Empty names are legal (prefix matcher records)."""
    out = []
    rec = head
    while rec < limit:
        ptr = binData[rec] | (binData[rec + 1] << 8)
        tok = binData[rec + 2]
        e = rec + 3
        while e < limit and binData[e] != 0:
            e += 1
        name = binData[rec + 3:e]
        ok = e < limit and len(name) <= 12 and all(0x20 <= c <= 0x7E for c in name)
        if not ok or ptr <= rec or ptr > limit:
            print('   !! walk stop at %04X (ptr->%04X tok=%02X name=%r)' % (rec, ptr, tok, name))
            break
        out.append((rec, e + 1))
        rec = ptr
    return out


def main():
    import sys
    only = sys.argv[1] if len(sys.argv) > 1 else None   # run one job only
    jobs = [
        ('mon_scorpion_p2', BASE_DELETE, BASE_SHRINK, [], []),
        ('mon_scorp295_p2', S295_DELETE, S295_SHRINK, S295_ADD, []),
        ('mon_prof401_p2', PROF_DELETE, PROF_SHRINK, PROF_ADD, PROF_NULSCAN),
    ]
    jobs = [j for j in jobs if not only or j[0] == only]
    for stem, deletes, shrinks, adds, nulRanges in jobs:
        path = HERE + '/' + stem + '.blocks'
        blocks = {}
        order = []
        for line in open(path):
            m = re.match(r'(blk\d+): start 0x([0-9A-Fa-f]+) end 0x([0-9A-Fa-f]+) type bytedata', line)
            if m:
                blocks[m.group(1)] = (int(m.group(2), 16), int(m.group(3), 16))
                order.append(m.group(1))
        # keep set
        keep = {}
        freed = []
        for name in order:
            if name in deletes:
                freed.append(blocks[name])
            elif name in shrinks:
                keep[name] = shrinks[name]
                old = blocks[name]
                if old[0] < shrinks[name][0]:
                    freed.append((old[0], shrinks[name][0]))
                if shrinks[name][1] < old[1]:
                    freed.append((shrinks[name][1], old[1]))
            else:
                keep[name] = blocks[name]
        binData = open(HERE + '/' + stem + '.bin', 'rb').read()
        covered = list(keep.values()) + [(a[1], a[2]) for a in adds]
        # string scan inside freed ranges only
        strings = []
        for lo, hi in freed:
            strings += scanStrings(binData, lo, hi, covered)
        if stem == 'mon_prof401_p2':
            strings += scanStrings(binData, 0x22F0, 0x2309, covered)  # 'eval*2'
            strings = [(s, e) for s, e in strings if s not in PROF_STR_EXCLUDE]
        # v4.01: token table records come from the linked-list walk, not the
        # generic NUL scan (which both missed empty names and hit operands)
        if stem == 'mon_prof401_p2':
            tokens = walkTokenRecords(binData, PROF_TOKEN_HEAD, 0x27DA)
        else:
            tokens = []
            for lo, hi in nulRanges:
                tokens += scanStrings0(binData, lo, hi, covered)
        # name them
        n = 0
        for s, e in strings:
            n += 1
            keep['Msg%04X' % s] = (s, e)
        for s, e in tokens:
            keep['Tok%04X' % s] = (s, e)
        for name, s, e in adds:
            keep[name] = (s, e)
        with open(path, 'w') as f:
            for name in sorted(keep, key=lambda k: keep[k][0]):
                s, e = keep[name]
                f.write('%s: start 0x%04X end 0x%04X type bytedata\n' % (name, s, e))
        print('%s: kept %d blocks, freed %d ranges, recovered %d strings, %d tokens'
              % (stem, len(keep), len(freed), len(strings), len(tokens)))
        for s, e in strings:
            print('   str 0x%04X-0x%04X %r' % (s, e, binData[s:e]))
        for s, e in tokens:
            print('   tok 0x%04X-0x%04X %r' % (s, e, binData[s:e]))


if __name__ == '__main__':
    main()
