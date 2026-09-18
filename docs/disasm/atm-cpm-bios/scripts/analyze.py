#!/usr/bin/env python3
"""Reachability + data-region classifier for the ATM710 CP/M system page.

Walks code from the hardware vectors and the traced entry anchors with a
full Z80 instruction-length decoder, marks reachable bytes as code, then
classifies unreached regions as strings or padding and emits a z80dasm
block-definition file (byte-complete coverage is later verified by
checkcov.py).

Adapted from docs/disasm/rom/scorpion/scripts/analyze.py: the RST 20h
inline-message convention is Scorpion-specific and dropped here; the entry
list gains the CP/M anchors traced on the emulator during the prince.trd
investigation (see ../monitor-rom.md, ../bios-ram.md).
"""
import sys

PAGE = 16384

# ---------------------------------------------------------------------------
# Z80 instruction length decoder
# ---------------------------------------------------------------------------

# base opcodes: length including opcode byte
BASE_LEN = [1]*256
for op in (0x06,0x0E,0x16,0x1E,0x26,0x2E,0x36,0x3E):
    BASE_LEN[op] = 2
for op in (0x40,):
    # placeholder
    pass
for op in (0x76,):
    BASE_LEN[op] = 1
for op in range(0x80,0xC0):
    BASE_LEN[op] = 1
# 3-byte: jp nn, call nn, 16-bit loads/immediate, alu a,n
THREE = set([0x01,0x11,0x21,0x22,0x2A,0x31,0x32,0x3A,
             0xC2,0xC3,0xC4,0xCA,0xCD,0xD2,0xD4,0xDA,0xDC,0xE2,0xE4,0xEA,0xEC,
             0xF2,0xF4,0xFA,0xFC,
             0xC6,0xCE,0xD6,0xDE,0xE6,0xEE,0xF6,0xFE])
for op in THREE:
    BASE_LEN[op] = 3
for op in (0x18,):  # jr e
    BASE_LEN[op] = 2
for op in (0x20,0x28,0x30,0x38,0x10):
    BASE_LEN[op] = 2
# IN a,(n), OUT (n),a
BASE_LEN[0xDB] = 2
BASE_LEN[0xD3] = 2

# ED xx lengths
ED_LEN = [2]*256
for op in range(0x40,0xC0):
    ED_LEN[op] = 2
for op in list(range(0x43,0x48))+[0x4B,0x53,0x5B,0x63,0x6B,0x73,0x7B]:
    ED_LEN[op] = 4        # ld rp,nn / ld (nn),rp etc.
for op in [0x7B,0x6B,0x4B,0x5B,0x43,0x53,0x63,0x73]:
    ED_LEN[op] = 4
for op in [0x23,0x2B,0x21,0x22,0x33,0x2A,0x3A,0x3B]:  # not real ED ops (same as base)
    ED_LEN[op] = 2

# CB: 2 bytes. DD/FD DD CB nn dd: 4 bytes.
CB_LEN = 2
DDCB_LEN = 4

def insn_len(data, pos):
    """Return (length, prefix_kind, opcode) for instruction at pos. None if out of range."""
    if pos >= len(data):
        return None
    b = data[pos]
    if b == 0xDD or b == 0xFD:
        if pos+1 >= len(data): return (1,'dd',b)
        n = data[pos+1]
        if n == 0xDD or n == 0xFD:
            return (1,'dd',b)  # treat doubled prefix as prefix only
        if n == 0xCB:
            return (DDCB_LEN,'ddcb',b)
        if n == 0xED:
            return (2,'dd',b)
        return (BASE_LEN[n] if BASE_LEN[n]>1 else 1, 'dd', b)
    if b == 0xED:
        if pos+1 >= len(data): return (1,'ed',b)
        return (ED_LEN[data[pos+1]], 'ed', b)
    if b == 0xCB:
        return (CB_LEN,'cb',b)
    return (BASE_LEN[b], 'base', b)

# conditional branches: opcode -> (is_jump, is_call, is_ret)
CC_JR = {0x20,0x28,0x30,0x38,0x10}
COND_JP = (0xC2,0xD2,0xE2,0xF2,0xCA,0xDA,0xEA,0xFA)
COND_CALL = (0xC4,0xD4,0xE4,0xF4,0xDC,0xEC,0xFC,0xF4)
COND_RET = (0xC0,0xC8,0xD0,0xD8,0xE0,0xE8,0xF0,0xF8)
RST_OPS = (0xC7,0xCF,0xD7,0xDF,0xE7,0xEF,0xF7,0xFF)

# Hardware vectors of the page-zero image, plus entry anchors traced on the
# emulator: 0965 service-session LDIR copy, 2A00 BDOS record/FCB search
# utilities, 387E WBOOT/BIOS entry (runtime $F87E),
# 1400 cold-boot config shell (runs from the $D400 RAM copy at cold start,
#   see the xlat notes below),
# 1BC6 CCP cold entry: the warm-boot tail (LD DE,$D400 / LD HL,$1BC6)
# copies the CCP here; its trampolines JP $D758/$D75C stay inside the copy.
ENTRIES = [0x0000,0x0008,0x0010,0x0018,0x0020,0x0028,0x0030,0x0038,0x0066,
           0x0965, 0x2A00, 0x387E, 0x1400, 0x1BC6]


# Word-pointer tables: NONE verified.  An earlier pass guessed a 49-entry
# DW table at $2405, but those bytes are walked code (LD HL,(DF43)/EX DE,HL/
# JP (HL) indirect jumps); a whole-image scan for >=8-word runs into code
# addresses finds only opcode soup.  The BDOS function dispatch is the
# verified JP table at $31C6 (runtime $EA00), harvested by harvest_jp_tables.
DW_TABLES = []


def harvest_dw_tables(data):
    extra = set()
    for start in DW_TABLES:
        i = start
        n = len(data)
        while i + 1 < n:
            t = data[i] | data[i+1] << 8
            v = xlat(t)
            if (0xC000 <= t <= 0xFFFF) or (0x0100 <= t <= 0x3B00):
                if 0 < v < n:
                    extra.add(v)
                i += 2
            else:
                break
    return extra


# Hand-verified code frees (scorpion fixblocks stage).  Each entry was
# checked for opcode-flow density >8% and ASCII <50%; they are the BDOS
# function tails, the XVR disk-BIOS mid layer ($3049-$35DC, banners carve
# back out as strings), the CCP helper islands, the low monitor/menu
# code reached from the other ROM pages, and the small monitor/shell
# fragments that are only entered through computed dispatch or from the
# monitor page (var save/restore via LD (5Fxx),A, JR islands, the $8003
# signature check, the port-$FD video save/restore).
FREES = [
    (0x01F1,0x0235),(0x0270,0x02B3),(0x02D9,0x0347),(0x03A5,0x03D7),
    (0x064F,0x0681),(0x079A,0x0821),(0x0876,0x08B4),(0x0A48,0x0A76),
    (0x103F,0x10A5),(0x1335,0x13CD),(0x1550,0x1583),
    (0x1C58,0x1CA5),(0x1CBA,0x1CE5),(0x1DF6,0x1E78),
    (0x2467,0x24FA),(0x2577,0x263B),(0x268E,0x277F),(0x27C0,0x280E),
    (0x29A6,0x29EB),(0x2B39,0x2BD6),(0x2C01,0x2DDE),(0x2E0E,0x2F01),
    (0x2F4A,0x303A),(0x3049,0x35DD),
    (0x3B3F,0x3B8F),(0x3CD2,0x3D0A),(0x3E0B,0x3E3E),
    # the 17-entry BIOS jump table itself: unreached middle entries would
    # render as defb islands mid-table
    (0x02CA,0x02FD),
    # unreachable-but-real code islands (second curation round):
    (0x0125,0x0128),(0x01F4,0x01FC),(0x03DC,0x03FE),(0x0566,0x056F),
    (0x05A0,0x05A8),(0x078B,0x078E),(0x07C3,0x07CB),(0x08B8,0x08BC),
    (0x091A,0x0930),(0x09F2,0x0A00),(0x0A19,0x0A21),(0x0AA9,0x0AAC),
    (0x0AB6,0x0AD7),(0x0C0F,0x0C1F),(0x0CD8,0x0CF0),(0x1027,0x1030),
    (0x142D,0x143E),(0x1593,0x1596),(0x15A1,0x15A7),(0x1617,0x1622),
    (0x16D1,0x16DB),(0x179B,0x179E),(0x1819,0x1823),(0x1856,0x1866),
    (0x1A88,0x1AA2),(0x1B7E,0x1B86),(0x1ECE,0x1ED6),(0x20E5,0x20EA),
    (0x345A,0x3462),
    # monitor/BDOS fragments uncovered after dropping the bogus $2405
    # word-table seeding (CALL/DJNZ flows, LD (5Fxx),A var saves):
    (0x09E8,0x09F2),(0x0A00,0x0A19),(0x0A21,0x0A48),(0x0A76,0x0A9F),
    (0x2173,0x2178),(0x2AC7,0x2ACC),
]

# Hand-carved inline literals below the >=8B carve threshold: the 'k.\r\n'
# KiB suffix printed by the RAM-size report at $3860.
INLINE_MSGS = [(0x3860, 0x3865)]

# Carve overrides: code regions whose opcodes are pure printable bytes
# (plus a 00 operand) and would otherwise be carved back out as strings:
# $1027 LD L,A/LD H,0/ADD HL,HL x6 (x64 multiply) and $345A the buffer
# store loop LD (HL),E/INC HL/... (its 00 is an operand of LD (HL),0).
UNCARVE = [(0x1027, 0x1030), (0x345A, 0x3462)]


# Meaningful names for the data blocks (renblocks stage).  Keyed by block
# start address; unnamed blocks fall back to DataNNNN.  Content verified
# against the ROM bytes (banners, keyword tables, keyboard matrices).
RENAMES = {
    0x000B: 'ZeroPageRst10Pad', 0x001B: 'ZeroPageRst20Pad', 0x0023: 'ZeroPageRst30Pad',
    0x003B: 'PageZeroReserved',
    0x0436: 'MonitorTable0436',
    0x0609: 'MonitorTable0609', 0x06D1: 'MonitorTable06D1',
    0x0715: 'MonitorTable0715', 0x073F: 'MonitorTable073F',
    0x0908: 'MonitorTable0908', 0x0A86: 'MonitorTable0A86',
    0x0B63: 'FdcTable0B63', 0x0B94: 'FdcTable0B94',
    0x0E32: 'MenuKbdRows', 0x0E5C: 'KbdSymMap0E5C',
    0x1306: 'ConfigTable1306',
    0x15E9: 'ColdShellTab15E9',
    0x1A64: 'CcpTable1A64',
    0x1AC6: 'CcpMsgConfigError', 0x1ADC: 'CcpMsgStartupAborted',
    0x1BC9: 'CcpSerialBanner',
    0x1ED6: 'CcpCommandKeywords',
    0x1F91: 'CcpTable1F91',
    0x1FA5: 'CcpMsgReadError', 0x2118: 'CcpMsgAllYn',
    0x21CD: 'CcpMsgNoSpace', 0x2248: 'CcpMsgFileExists',
    0x2340: 'CcpMsgBadLoad',
    0x2361: 'BdosPad2361',
    0x247E: 'BdosErrMessages',
    0x34F2: 'BiosBanners',
    0x37DD: 'BiosFaultMsg',
    0x3815: 'RamSizeTable',
    0x382F: 'MsgMemory', 0x3851: 'MsgSizeIs', 0x3860: 'MsgKib',
    0x389D: 'DriverTable389D', 0x38E0: 'DriverTable38E0',
    0x3916: 'DriverTable3916',
    0x3932: 'DriverTable3932', 0x3938: 'DriverTable3938',
    0x395F: 'MsgM1Ve31',
    0x39DE: 'DriverTable39DE',
    0x39F9: 'KbdMap39F9', 0x3A3A: 'CharsetTables',
    0x3BF6: 'KbdTables3BF6',
    0x3C1F: 'KbdCtrlMap3C1F', 0x3CB3: 'KbdTables3CB3',
    0x3D66: 'ZxKeyboardRows',
    0x3F51: 'RomTailPad',
}


def harvest_jp_tables(data):
    """Collect dispatch-table targets: runs of >= 6 consecutive JP nn.
    The image carries three - the 17-entry BIOS jump table at $02CA (all
    targets in the $F8xx driver), the BDOS function dispatch at $31C6
    (targets in the $EAxx BDOS bodies) and a 7-entry driver sub-table at
    $3E08.  Their targets are code entries the recursive walk cannot
    reach (dispatch is computed at runtime)."""
    extra = set()
    i = 0
    n = len(data)
    while i < n - 2:
        if data[i] == 0xC3:
            j = i
            while j + 2 < n and data[j] == 0xC3:
                j += 3
            if (j - i) // 3 >= 6:
                for k in range(i, j, 3):
                    t = xlat(data[k+1] | data[k+2] << 8)
                    if 0 < t < n:
                        extra.add(t)
            i = j
        else:
            i += 1
    return extra

# The page is built for several bases at once, decided by TARGET range:
#   $C000-$D3FF  -> +$C000 image offset: alias window onto the service
#                   code (same ROM page banked high)
#   $D400-$EFFF  -> DUAL mapping (see xlat_candidates): this window holds
#                   the cold-boot RAM copy of the $1400-$1BC5 config shell
#                   (base +$C000) at cold start, and the WBOOT LDIR copy
#                   (LD BC,$1B1E / LD DE,$D400 / LD HL,$1BC6 / LDIR at
#                   $3866) of the CCP+BDOS image $1BC6-$36E3 (base
#                   t-$B83A) after warm boot.  The latter puts the
#                   21-entry JP dispatch at $31C6 exactly on the traced
#                   BDOS entry $EA00; $E9DD/$EA9F map into $31A3/$3265
#                   (the traced BDOS bodies).
#   $F000-$FFFF  -> +$C000 (driver/WBOOT tail: the $02CA BIOS table's
#                   $F87E = image $387E WBOOT, verified by trace)
#   $0000-$3FFF  -> 1:1 (service mapping: the sys page mapped at $0000
#                   by #7FFD bit 4 - page-zero stubs, monitor core, the
#                   WBOOT code's own local jumps like JP $38FA)
def xlat(t):
    """Primary translation (dispatch tables: their context is unambiguous)."""
    if 0xC000 <= t < 0xD400:
        return t - 0xC000
    if 0xD400 <= t < 0xF000:
        return t - 0xB83A
    if 0xF000 <= t <= 0xFFFF:
        return t - 0xC000
    return t

def xlat_candidates(t):
    """All image offsets a branch target t can denote.

    The $D400-$EFFF window holds the cold-shell copy (+$C000) at cold
    start and the WBOOT CCP/BDOS copy (t-$B83A) afterwards; a static walk
    must follow both.  Every other window has a single meaning."""
    if 0xD400 <= t < 0xF000:
        return (t - 0xB83A, t - 0xC000)
    return (xlat(t),)

def walk(data, strict):
    """One recursive-descent walk. Returns kind array ('C' or '?')."""
    n = len(data)
    kind = ['?']*n
    stack = list(harvest_jp_tables(data) | harvest_dw_tables(data))
    stack += [e for e in ENTRIES if e < n]
    seen = set()
    while stack:
        pos = stack.pop()
        while True:
            if pos < 0 or pos >= n or pos in seen:
                break
            r = insn_len(data,pos)
            if r is None: break
            ln,pfx,op = r
            seen.add(pos)
            for i in range(ln):
                if pos+i < n: kind[pos+i]='C'
            b = data[pos]
            stop = False
            if pfx=='base':
                if b == 0x18:  # jr e - unconditional
                    off = data[pos+1]
                    for c in xlat_candidates(pos+2+(off-256 if off>=128 else off)):
                        if 0<=c<n: stack.append(c)
                    stop = True
                elif b in CC_JR:
                    off = data[pos+1]
                    for c in xlat_candidates(pos+2+(off-256 if off>=128 else off)):
                        if 0<=c<n: stack.append(c)
                    if b == 0x10: stop = True  # djnz - loops back
                elif b == 0xC3:  # jp nn unconditional
                    for c in xlat_candidates(data[pos+1]|data[pos+2]<<8):
                        if c < n: stack.append(c)
                    stop = True
                elif b in COND_JP:
                    for c in xlat_candidates(data[pos+1]|data[pos+2]<<8):
                        if c < n: stack.append(c)
                elif b == 0xCD or b in COND_CALL:
                    for c in xlat_candidates(data[pos+1]|data[pos+2]<<8):
                        if c < n: stack.append(c)
                elif b in RST_OPS:
                    t = (b & 0x38)
                    if t < n: stack.append(t)
                    if strict: stop = True
                elif b == 0xC9:      # ret unconditional
                    stop = strict
                elif b in COND_RET:
                    pass             # conditional ret: fall through
                elif b == 0xE9:      # jp (hl)
                    stop = strict
                elif b == 0x76:      # halt
                    stop = strict
            elif pfx=='dd':
                nn = data[pos+1] if pos+1<n else 0
                if nn == 0xE9:      # jp (ix)
                    stop = strict
            elif pfx=='ed':
                nn = data[pos+1] if pos+1<n else 0
                if nn in (0x45,0x55,0x5D,0x65,0x6D,0x75,0x7D):  # retn/reti
                    stop = strict
            if stop:
                break
            pos += ln
    return kind

def classify_regions(kind, data):
    """Return list of (start,end,type) for unreached regions >= 3 bytes."""
    regions=[]
    i=0
    n=len(data)
    while i<n:
        if kind[i]!='C':
            j=i
            while j<n and kind[j]!='C': j+=1
            if j-i>=3:
                # classify
                seg=data[i:j]
                printable=sum(1 for c in seg if 32<=c<127 or c in (0x0D,0x20))
                if printable>=len(seg)*0.85 and len(seg)>=6:
                    t='defm'
                elif all(c==0x00 for c in seg): t='defs0'
                elif all(c==0xFF for c in seg): t='defsF'
                else: t='defb'
                regions.append((i,j,t))
            i=j
        elif kind[i]=='M':
            j=i
            while j<n and kind[j]=='M': j+=1
            regions.append((i,j,'defm'))
            i=j
        else:
            i+=1
    # merge adjacent same-type regions
    merged=[]
    for s,e,t in regions:
        if merged and merged[-1][2]==t and merged[-1][1]==s:
            merged[-1][1]=e
        else:
            merged.append([s,e,t])
    return [tuple(x) for x in merged]

def carve_strings(final, data):
    """Carve inline string literals out of code regions.
    The CP/M layer stores its messages as NUL/'$'-terminated pure-ASCII
    text inline in the code stream (right after the CALL that prints them).
    A run of >=8 bytes from {0x00,0x0A,0x0D,printable} that CONTAINS a
    terminator byte (0x00/0x0A/0x0D - every real message here ends in one)
    and is >=60% printable becomes string data.  Runs containing other
    control bytes or high-bit bytes are NOT strings here: on this page
    those are code bytes (LD (FAxx),A-style opcodes are printable/high-bit
    heavy - the $383C-$384E print loop was mis-carved by the
    Scorpion-grade rule) or attribute tables (see RamSizeTable at $3815)."""
    n=len(data)
    i=0
    while i<n:
        if final[i]=='C':
            j=i
            while j<n and (data[j] in (0,0x0A,0x0D) or 32<=data[j]<127): j+=1
            if j-i>=8:
                seg=data[i:j]
                pr=sum(1 for c in seg if 32<=c<127)
                if pr>=len(seg)*0.6 and pr>=6 and any(c in (0,0x0A,0x0D) for c in seg):
                    for k in range(i,j): final[k]='M'
            i=max(j,i+1)
        else:
            i+=1
    for s,e in UNCARVE:
        for k in range(s,min(e,n)): final[k]='C'
    return final

def final_kinds(data):
    """2-layer classification: strict code / linear-only (density split) / data,
    then the hand-curated FREES overlay, then string carving."""
    strict = walk(data, True)
    linear = walk(data, False)
    n=len(data)
    final=['D']*n
    for i in range(n):
        if strict[i]=='C': final[i]='C'
    # linear-only regions: keep as code unless ASCII-dense
    i=0
    while i<n:
        if linear[i]=='C' and strict[i]!='C':
            j=i
            while j<n and linear[j]=='C' and strict[j]!='C': j+=1
            seg=data[i:j]
            printable=sum(1 for c in seg if 32<=c<127)
            if printable < len(seg)*0.6:
                for k in range(i,j): final[k]='C'
            i=j
        else:
            i+=1
    # hand-curated frees (fixblocks stage): regions the recursive walk cannot
    # reach because they are entered through computed dispatch inside already
    # walked code, or called from the OTHER ROM pages (menu core in sos/128).
    # Verified by opcode-flow density + ASCII sparsity; embedded strings are
    # re-extracted by the carve below
    for s,e in FREES:
        for k in range(s,min(e,n)): final[k]='C'
    # hand-carved short literals
    for s,e in INLINE_MSGS:
        for k in range(s,min(e,n)):
            if final[k]=='C': final[k]='M'
    return carve_strings(final,data)

if __name__=='__main__':
    src, dst = sys.argv[1], sys.argv[2]
    data=open(src,'rb').read()
    kind=final_kinds(data)
    code=sum(1 for k in kind if k=='C')
    print(f"code bytes: {code}/{len(data)} ({100*code/len(data):.1f}%)")
    regs=classify_regions(kind,data)
    with open(dst,'w') as f:
        for idx,(s,e,t) in enumerate(regs):
            # z80dasm block syntax: meaningful name where known, exclusive end
            name = RENAMES.get(s, f'Data{s:04X}')
            f.write(f"{name}: start 0x{s:04X} end 0x{e:04X} type bytedata\n")
    for s,e,t in regs:
        print(f"  {s:04X}-{e-1:04X} {t} ({e-s}B)")
