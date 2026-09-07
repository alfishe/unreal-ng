#!/usr/bin/env python3
"""Analyze an unreal-ng porttrace JSON for TurboSound dispatch faults.

Replays FFFD/BFFD traffic through candidate TurboSound dispatch semantics and
reports which chip ends up with the music data. Companion to
porttrace_convert.py; reads the same "unreal-ng-porttrace-v1" JSON format.

Usage: analyze_wildplayer_ts.py <trace.json>
"""
import json
import sys
from collections import Counter

PATH = sys.argv[1] if len(sys.argv) > 1 else \
    "/Volumes/TB4-4Tb/Projects/Test/unreal-ng/scratch/porttrace-wildplayer.json"

with open(PATH) as f:
    data = json.load(f)

ev = data["events"]
print(f"== session: model={data['session']['model']} events={len(ev)} "
      f"captured={data['session']['total_captured']} filtered={data['session']['total_filtered']}")

F_OUT = 1

def is_out(e): return e["flags"] & F_OUT != 0

# ---- 1. global stats -------------------------------------------------------
outs = [e for e in ev if is_out(e)]
ins = [e for e in ev if not is_out(e)]
print(f"\n== OUT={len(outs)}  IN={len(ins)}")
print(f"   frames {ev[0]['frame']}..{ev[-1]['frame']}  ts {ev[0]['ts']}..{ev[-1]['ts']}")

dec_raw = Counter((e["dec"], e["raw"]) for e in ev)
print("\n== (dec,raw) histogram:")
for (dec, raw), n in dec_raw.most_common():
    print(f"   dec={dec:04X} raw={raw:04X}  x{n}")

pc_hist = Counter(e["pc"] for e in ev)
print(f"\n== distinct PCs: {len(pc_hist)}; top 30:")
for pc, n in pc_hist.most_common(30):
    sel_fe = sum(1 for e in ev if e["pc"] == pc and is_out(e) and e["dec"] == 0xFFFD and e["val"] > 0x0F)
    print(f"   pc={pc:04X}  x{n:6d}   would-be-selects(FFFD,val>0F)={sel_fe}")

# ---- 2. chip-select audit --------------------------------------------------
print("\n== all OUT FFFD values > 0x0F (chip-select candidates) grouped by (pc,val):")
sel_hist = Counter((e["pc"], e["val"]) for e in outs
                   if e["dec"] == 0xFFFD and e["val"] > 0x0F)
for (pc, val), n in sel_hist.most_common(40):
    print(f"   pc={pc:04X} val={val:02X}  x{n}")

# ---- 3. detection window ---------------------------------------------------
print("\n== detection window events (pc 0x6080..0x60B0):")
for e in ev:
    if 0x6080 <= e["pc"] <= 0x60B0:
        d = "OUT" if is_out(e) else "IN "
        print(f"   f{e['frame']:6d} ts{e['ts']:12d} {d} raw={e['raw']:04X} dec={e['dec']:04X} "
              f"val={e['val']:02X} pc={e['pc']:04X}")

# ---- 4. replay under candidate semantics -----------------------------------
def replay(select_fn, read_chip_fn, label):
    chips = [{"R": [0] * 16, "writes": 0, "reg_sel": 0} for _ in range(2)]
    cur = 0
    sel_counts = Counter()
    for e in ev:
        if not is_out(e):
            read_chip_fn(cur, e)
            continue
        if e["dec"] == 0xFFFD:
            new = select_fn(cur, e["val"])
            if new != cur:
                sel_counts[e["val"]] += 1
            cur = new
            chips[cur]["reg_sel"] = e["val"] & 0x0F
        elif e["dec"] == 0xBFFD:
            chips[cur]["R"][chips[cur]["reg_sel"]] = e["val"]
            chips[cur]["writes"] += 1
    print(f"\n== replay [{label}]")
    print(f"   select changes by value: {dict(sel_counts)}")
    for i, c in enumerate(chips):
        print(f"   chip{i}: BFFD data writes={c['writes']}")
        print(f"      R7(mixer)={c['R'][7]:02X} R8/9/10(vol)={c['R'][8]:02X}/{c['R'][9]:02X}/{c['R'][10]:02X} "
              f"R11/12(envp)={c['R'][11]:02X}/{c['R'][12]:02X} R13(envsh)={c['R'][13]:02X} "
              f"R0/1(tpA)={c['R'][0]:02X}{c['R'][1]:02X} R2/3(tpB)={c['R'][2]:02X}{c['R'][3]:02X}")
    for i, c in enumerate(chips):
        r7, en = c["R"][7], []
        for ch, m in ((0, 0x01), (1, 0x08), (2, 0x40)):
            vol = c["R"][8 + ch]
            if not (r7 & m) or (vol & 0x10):
                en.append(f"ch{'ABC'[ch]}={'on' if not (r7 & m) else 'env'}")
        print(f"      sounding channels chip{i}: {en}")
    return chips

cur_sem = lambda cur, val: (1 if val & 0x01 else 0) if (val & 0xF8) == 0xF8 else cur
replay(cur_sem, lambda cur, e: cur, "current: %11111<fm><stat><sel> select (FE->D1/chip0, FF->D2/chip1), IN=current or YM2203 status while stat=0")

# stat=0 latched: every IN answers YM2203 idle status (0x00) regardless of chip
stat0_sem = lambda cur, val: (1 if val & 0x01 else 0) if (val & 0xF8) == 0xF8 else cur
def status_aware_read(cur, e):
    return cur  # marker only; stat handling would need write context - see replay above
replay(stat0_sem, status_aware_read, "current (reads noted): stat=0 windows answer 0x00 - detection probes")

# ---- 5. per-frame engine pattern -------------------------------------------
print("\n== per-frame pattern sample (frames with most BFFD writes):")
frame_writes = Counter(e["frame"] for e in outs if e["dec"] == 0xBFFD)
busy = sorted(f for f, _ in frame_writes.most_common(5))
for fr in busy:
    fe = [e for e in ev if e["frame"] == fr]
    sel_seq = [(e["pc"], e["val"]) for e in fe if is_out(e) and e["dec"] == 0xFFFD and e["val"] > 0x0F]
    pcs = Counter(e["pc"] for e in fe if is_out(e) and e["dec"] == 0xBFFD)
    print(f"   frame {fr}: {len(fe)} events; select-seq(pc,val)={sel_seq[:12]}")
    print(f"      BFFD-writer pcs: {[(f'{p:04X}', n) for p, n in pcs.most_common(8)]}")

# ---- 6. select-writer pc -> which chip got the following data --------------
print("\n== select-writer pc -> chip receiving the next BFFD data (current semantics):")
pairs = Counter()
pend_sel = None
pend_pc = None
for e in ev:
    if not is_out(e):
        continue
    if e["dec"] == 0xFFFD and e["val"] > 0x0F:
        pend_sel = e["val"]; pend_pc = e["pc"]
    elif e["dec"] == 0xBFFD and pend_sel is not None:
        selchip = pend_sel & 0x01  # %11111<fm><stat><sel>: sel bit -> D2=chip1
        pairs[(pend_pc, selchip)] += 1
        pend_sel = None
for (pc, chip), n in pairs.most_common(20):
    print(f"   select-writer pc={pc:04X} -> data to chip{chip}: x{n}")
