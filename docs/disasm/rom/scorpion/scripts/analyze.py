#!/usr/bin/env python3
"""Reachability + data-region classifier for Scorpion service monitor ROM pages.

Walks code from the RST/NMI entry points with a full Z80 instruction-length
decoder, marks reachable bytes as code, then classifies unreached regions as
strings (defm), padding (defs), word tables (defw) or byte data (defb), and
emits a z80dasm block-definition file.
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
for op in range(0xC0,0xC0+0x20):   # ret cc .. 
    pass
for op in (0x40,):  # placeholder
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

def walk(data, strict):
    """One recursive-descent walk. Returns (kind dict-of-ranges? raw list, msg_ranges)."""
    n = len(data)
    kind = ['?']*n
    msgs = []          # (start,end) inline RST 20h message ranges
    entry = [0x0000,0x0008,0x0010,0x0018,0x0020,0x0028,0x0030,0x0038,0x0066]
    stack = [e for e in entry if e < n]
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
            if pfx=='base' and b == 0xE7:
                # RST 20h: inline bit-7-terminated message follows
                if pos+1 < n:
                    m = pos+1
                    while m < n and data[m] < 0x80: m += 1
                    if m < n: m += 1  # include terminator
                    if m-(pos+1) >= 1:
                        msgs.append((pos+1, m))
                        pos = m
                        continue
            if pfx=='base':
                if b == 0x18:  # jr e - unconditional
                    off = data[pos+1]
                    t = pos+2+(off-256 if off>=128 else off)
                    if 0<=t<n: stack.append(t)
                    stop = True
                elif b in CC_JR:
                    off = data[pos+1]
                    t = pos+2+(off-256 if off>=128 else off)
                    if 0<=t<n: stack.append(t)
                    if b == 0x10: stop = True  # djnz - loops back
                elif b == 0xC3:  # jp nn unconditional
                    t = data[pos+1]|data[pos+2]<<8
                    if t < n: stack.append(t)
                    stop = True
                elif b in COND_JP:
                    t = data[pos+1]|data[pos+2]<<8
                    if t < n: stack.append(t)
                    if not strict: stop = False
                elif b == 0xCD or b in COND_CALL:
                    t = data[pos+1]|data[pos+2]<<8
                    if t < n: stack.append(t)
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
    return kind, msgs

def analyze(data, strict=True):
    return walk(data, strict)[0]

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
    """Carve message-table string runs out of code regions.
    Monitor string tables mix printable text with 0x00/0x0D terminators and
    high-bit marker/attribute bytes. A run of >=10 bytes consisting only of
    {0x00,0x0D,printable,0x80-0xFF} with >=55% printable becomes string data.
    Real code fragments on C3/CD/21/C5/... opcodes (<0x20) break such runs."""
    n=len(data)
    i=0
    while i<n:
        if final[i]=='C':
            j=i
            while j<n and (data[j]==0 or data[j]==0x0D or 32<=data[j]<127 or data[j]>=0x80): j+=1
            if j-i>=10:
                seg=data[i:j]
                pr=sum(1 for c in seg if 32<=c<127)
                if pr>=len(seg)*0.55 and pr>=8:
                    for k in range(i,j): final[k]='M'
            i=max(j,i+1)
        else:
            i+=1
    return final

def final_kinds(data):
    """3-layer classification: strict code / linear-only (density split) / data.
    Inline RST 20h messages are carved as strings in both passes."""
    strict, msgs_s = walk(data, True)
    linear, msgs_l = walk(data, False)
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
    # inline messages: unconditionally string data
    for s,e in msgs_s + msgs_l:
        for k in range(s,e):
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
            # z80dasm block syntax: unique name, exclusive end, bytedata type
            f.write(f"blk{idx:03d}: start 0x{s:04X} end 0x{e:04X} type bytedata\n")
    for s,e,t in regs:
        print(f"  {s:04X}-{e-1:04X} {t} ({e-s}B)")
