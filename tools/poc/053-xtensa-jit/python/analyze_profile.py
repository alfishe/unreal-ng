#!/usr/bin/env python3
"""POC 053: the ESP's profile between two emulated seconds (UNREAL_XTENSA_PROFILE dumps).

usage: analyze_profile.py <prefix> <from-second> <to-second> [--top N]

<prefix>.<host>.<second>.host and .blocks are cumulative: the window is to - from. Prints the host-time split,
the cores' load, the top blocks with their share of the executed instructions, the concentration, the block
lengths, the opcode histogram and the block-ending instructions.
"""
import collections
import glob
import sys


def read_host(path):
    out = {"traps": {}}
    for line in open(path):
        f = line.split()
        if f[0] == "trap":
            out["traps"][f[1]] = (int(f[2]), int(f[3]))
        elif f[0] == "us":
            out["us"], out["instructions"] = int(f[1]), int(f[3])
        elif f[0] == "budget":
            out["budget"] = (int(f[1]), int(f[2]))
            out["used"] = (int(f[4]), int(f[5]))
        elif f[0] == "runs":
            for k, v in zip(f[0::2], f[1::2]):
                out[k] = int(v)
    return out


def read_blocks(path):
    out = {}
    for line in open(path):
        f = line.split()
        out[f[0]] = (int(f[1]), int(f[2]), int(f[3]), f[4:])
    return out


def find(prefix, second, kind):
    files = sorted(glob.glob("%s.*.%05d.%s" % (prefix, second, kind)))
    if not files:
        sys.exit("no %s dump for second %d" % (kind, second))
    return files[-1]


def main():
    prefix, s0, s1 = sys.argv[1], int(sys.argv[2]), int(sys.argv[3])
    top = int(sys.argv[sys.argv.index("--top") + 1]) if "--top" in sys.argv else 30
    h0, h1 = read_host(find(prefix, s0, "host")), read_host(find(prefix, s1, "host"))
    b0, b1 = read_blocks(find(prefix, s0, "blocks")), read_blocks(find(prefix, s1, "blocks"))

    ms = (h1["us"] - h0["us"]) / 1000
    insns = h1["instructions"] - h0["instructions"]
    print("window: %.0f emulated ms, %d instructions (%.2f M per ms)" % (ms, insns, insns / ms / 1e6))
    for core in (0, 1):
        budget = h1["budget"][core] - h0["budget"][core]
        used = h1["used"][core] - h0["used"][core]
        print("core %d: used %.1f %% of its cycles" % (core, 100.0 * used / budget if budget else 0))
    adv = h1["advance_ns"] - h0["advance_ns"]
    print("host: Advance %.1f ms (%.3f ms per emulated ms)" % (adv / 1e6, adv / 1e6 / ms))
    for key in ("run_ns", "trap_ns", "switch_ns", "pcm_ns"):
        d = h1[key] - h0[key]
        print("  %-9s %8.1f ms  %5.1f %%" % (key, d / 1e6, 100.0 * d / adv if adv else 0))
    runs = h1["runs"] - h0["runs"]
    run_ns = h1["run_ns"] - h0["run_ns"]
    print("  Cpu::Run calls %d (%.0f instructions each), %.2f ns per instruction inside Run, %.0f M instr/s" % (
        runs, insns / runs if runs else 0, run_ns / insns if insns else 0, insns / (run_ns / 1e9) / 1e6 if run_ns else 0))
    traps = []
    for pc, (n, ns) in h1["traps"].items():
        n0, ns0 = h0["traps"].get(pc, (0, 0))
        if n > n0:
            traps.append((ns - ns0, n - n0, pc))
    traps.sort(reverse=True)
    print("top traps (host ms, count, pc):")
    for ns, n, pc in traps[:12]:
        print("  %8.2f ms %8d  %s  (%.0f ns each)" % (ns / 1e6, n, pc, ns / n))

    rows = []
    ops = collections.Counter()
    ends = collections.Counter()
    lengths = collections.Counter()
    for pc, (length, entries, ins, names) in b1.items():
        e0, i0 = (b0[pc][1], b0[pc][2]) if pc in b0 else (0, 0)
        de, di = entries - e0, ins - i0
        if de <= 0:
            continue
        rows.append((di, de, pc, length, names))
        # instructions by opcode: the full blocks' ops weighted by entries, scaled to what ran
        if names and length:
            scale = di / (de * length)
            for n in names:
                ops[n] += de * scale
            ends[names[-1]] += de
        lengths[length] += di
    rows.sort(reverse=True)
    total = sum(r[0] for r in rows) or 1
    print("blocks run: %d; executed in blocks %d (%.1f %% of the instructions)" % (len(rows), total, 100.0 * total / insns))
    acc = 0
    marks = {10: None, 100: None, 1000: None}
    for i, r in enumerate(rows, 1):
        acc += r[0]
        if i in marks:
            marks[i] = acc
    print("concentration: top 10 %.1f %%, top 100 %.1f %%, top 1000 %.1f %%" % tuple(
        100.0 * (marks[k] if marks[k] is not None else acc) / total for k in (10, 100, 1000)))
    avg = sum(r[0] for r in rows) / max(1, sum(r[1] for r in rows))
    print("instructions per block entry: %.1f" % avg)
    print("top %d blocks (share, entries, pc, len, ops):" % top)
    for di, de, pc, length, names in rows[:top]:
        print("  %5.2f %% %9d  %s %3d  %s" % (100.0 * di / total, de, pc, length, " ".join(names[:14]) + (" ..." if len(names) > 14 else "")))
    print("opcodes (share of instructions):")
    osum = sum(ops.values()) or 1
    for name, n in ops.most_common(30):
        print("  %-10s %5.2f %%" % (name, 100.0 * n / osum))
    print("block ends (share of block entries):")
    esum = sum(ends.values()) or 1
    for name, n in ends.most_common(15):
        print("  %-10s %5.2f %%" % (name, 100.0 * n / esum))
    buckets = collections.Counter()
    for l, v in lengths.items():
        buckets["1-3" if l <= 3 else "4-7" if l <= 7 else "8-15" if l <= 15 else "16-31" if l <= 31 else "32+"] += v
    print("block length (share of instructions): " + ", ".join(
        "%s: %.1f %%" % (k, 100.0 * buckets[k] / total) for k in ("1-3", "4-7", "8-15", "16-31", "32+")))


if __name__ == "__main__":
    main()
