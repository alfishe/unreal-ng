#!/usr/bin/env python3
"""POC 036: the instruction starts and port cycles of a test ROM on the RTL (tb_cpu.vhd) against this emulator (NextRtlTiming_Test).

    compare.py start <rtl trace> <run address hex>       prints UNREAL_NEXT_RTL_START ("<body>:0014:<T after the INT>")
    compare.py diff <rtl trace> <our trace> <run address hex> [max lines]

Times are T of 3.5 MHz (8 clocks of 28 MHz) from the INT that starts the body (the RTL: the CPU's INT line falling; here: the frame's
intstart). The RTL logs every M1; an M1 after a CB / ED / DD / FD prefix is the same instruction's.
"""
import sys

PREFIX = {0xCB, 0xED, 0xDD, 0xFD}


def rtl_events(path):
    ints, inst, ports = [], [], []
    cont = False
    pending = None
    for line in open(path):
        p = line.split()
        if len(p) < (2 if p[:1] == ["INT"] else 6 if p[:1] in (["m"], ["O"], ["I"]) else 5):
            continue  # a line the simulation did not finish
        k = p[0]
        c = int(p[1])
        if k == "INT":
            ints.append(c)
        elif k == "M":
            pending = (c, int(p[4], 16), int(p[2]), int(p[3]))
        elif k == "m" and pending:
            op = int(p[5], 16)
            if not cont:
                # the start from the fetch's end less its 1.5 T: the +3's WAIT comes in T2, after M1 falls (the 48K / 128K hold
                # the clock before it)
                inst.append((c - 12, pending[1], pending[2], pending[3]))
            cont = (not cont) and op in PREFIX  # the M1 after a prefix is the same instruction's
            pending = None
        elif k in ("O", "I"):
            ports.append((k, c, int(p[4], 16), int(p[2]), int(p[3])))
    return ints, inst, ports


def body_origin(ints, inst, run_at):
    for i, (c, a, vc, hc) in enumerate(inst):
        if a == run_at:
            ref = max(x for x in ints if x <= c)
            return ref, i
    raise SystemExit("the body's first instruction is not in the trace")


def ours(path):
    """The instruction starts are taken from the opcode fetch less its usual offset from M1: ours puts a fetch's contention wait
    after the M1 hook, the RTL before M1 falls"""
    inst, ports, ints, fetch = [], [], [], {}
    lines = [line.split() for line in open(path)]
    pending = None
    for p in lines:
        if p[0] == "M":
            pending = len(inst)
            inst.append((int(p[1]), int(p[2], 16)))
        elif p[0] == "F" and pending is not None:
            fetch[pending] = int(p[1])
            pending = None
    if fetch:
        offsets = {}
        for i, t in fetch.items():
            offsets[t - inst[i][0]] = offsets.get(t - inst[i][0], 0) + 1
        k = max(offsets, key=offsets.get)
        inst = [(fetch[i] - k if i in fetch else t, a) for i, (t, a) in enumerate(inst)]
    for p in lines:
        if p[0] in ("O", "I"):
            ports.append((p[0], int(p[1]), int(p[2], 16)))
        elif p[0] == "INT":
            ints.append(int(p[1]))
    return inst, ports, ints


def main():
    mode, rtl, *rest = sys.argv[1:]
    ints, inst, ports = rtl_events(rtl)
    run_at = int(rest[-1] if mode == "start" else rest[1], 16)
    ref, i0 = body_origin(ints, inst, run_at)
    if mode == "start":
        # the prologue's last HALT (#0014) before the INT that starts the body, from the INT before it. Ours starts there 2 T
        # earlier: the T80 registers INT at the rising edge that starts an instruction's last T and sees it a clock later, ours checks
        # the line at the instruction's end (the four phases of the HALT's M1 against the INT only agree with 2; POC 036 analysis.md)
        halt = max(c for c, a, vc, hc in inst if a == 0x0014 and c < ref)
        prev = max(x for x in ints if x <= halt)
        print("%04X:0014:%d" % (run_at, (halt - prev) // 8 - 2))
        return
    limit = int(rest[2]) if len(rest) > 2 else 40
    our_inst, our_ports, our_ints = ours(rest[0])
    r = [((c - ref) / 8, a, vc, hc) for c, a, vc, hc in inst[i0:] if c >= ref]
    o = our_inst[next(i for i, (t, a) in enumerate(our_inst) if a == run_at):]
    print("the body starts: RTL %.2f T after its INT, ours %d" % (r[0][0], o[0][0]))
    # the RTL's M1 edge is a fixed fraction of a T into T1: take it from the first instruction
    frac = 0.0
    n = min(len(r), len(o))
    bad = 0
    first = None
    for k in range(n):
        rt, ra, vc, hc = r[k]
        ot, oa = o[k]
        if ra != oa:
            print("instruction %d: the RTL runs %04X, ours %04X (T %.2f / %d): the paths part" % (k, ra, oa, rt, ot))
            bad += 1
            break
        d = ot - (rt - frac)
        if abs(d) > 0.01:
            if first is None:
                first = k
            if bad < limit:
                print("instr %6d pc %04X  rtl T %9.2f (line %3d hc %3d)  ours %8d  ours-rtl %+6.2f" % (k, ra, rt - frac, vc, hc, ot, d))
            bad += 1
    print("%d instructions compared (RTL %d, ours %d), %d differ, first at %s; M1 edge %.3f T into T1" % (n, len(r), len(o), bad, first, frac))
    # port cycles: the times of the strobes against ours (the time ours logs for the access)
    rp = [(k, (c - ref) / 8, a) for k, c, a, vc, hc in ports if c >= inst[i0][0]]
    op = [p for p in our_ports if p[1] >= o[0][0]]
    m = min(len(rp), len(op))
    deltas = {}
    for j in range(m):
        if rp[j][0] != op[j][0] or (rp[j][2] & 0xFF) != (op[j][2] & 0xFF):
            print("port %d: the RTL %s %04X, ours %s %04X: the sequences part" % (j, rp[j][0], rp[j][2], op[j][0], op[j][2]))
            break
        d = round(op[j][1] - rp[j][1], 2)
        deltas.setdefault((rp[j][0], rp[j][2]), {}).setdefault(d, 0)
        deltas[(rp[j][0], rp[j][2])][d] += 1
    print("%d port cycles compared (RTL %d, ours %d); ours - RTL strobe, by port:" % (m, len(rp), len(op)))
    for key, ds in sorted(deltas.items()):
        print("  %s %04X: %s" % (key[0], key[1], ", ".join("%+.2f x%d" % (d, c) for d, c in sorted(ds.items()))))


if __name__ == "__main__":
    main()
