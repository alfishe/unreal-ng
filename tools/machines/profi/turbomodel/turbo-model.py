"""Profi v3.2 turbo wait model (derived from the main-board schematic, see the research doc).
Turbo T = one F2T period = 2 master clocks (14 MHz). One DRAM slot = one F2 period = 2 turbo T.
A memory cycle (M1, read, write) to RAM: T1 at an even turbo T (slot edge, phase A) -> 2 waits;
T1 at an odd turbo T (phase B) -> 3 waits (the request misses the F2 edge inside T1).
ROM, I/O, interrupt-ack and refresh: no waits.  Internal T-states: no waits.
Machine cycles: ('M',n) memory cycle of n T in RAM, ('R',n) memory cycle in ROM, ('I',n) internal/IO."""
import itertools, sys

def run(seq, start_phase=0, reps=2000):
    t = start_phase
    nominal = 0
    for _ in range(reps):
        for kind, n in seq:
            if kind == 'M':
                t += n + (2 if t % 2 == 0 else 3)
            else:
                t += n
            nominal += n
    return t - start_phase, nominal

def per_iter(seq):
    out = {}
    for ph in (0, 1):
        tt, nom = run(seq, ph)
        out[ph] = tt / 2000
    return out

M1 = [('M', 4)]
def M1x(extra): return [('M', 4), ('I', extra)] if extra else [('M', 4)]
loops = {
    'NOP':            M1,
    'LD A,(HL)':      M1 + [('M', 3)],
    'LD (HL),A':      M1 + [('M', 3)],
    'OUT (n),A':      M1 + [('M', 3), ('I', 4)],
    'IN A,(n)':       M1 + [('M', 3), ('I', 4)],
    'INC DE':         M1x(2),
    'JR e':           M1 + [('M', 3), ('I', 5)],
    'JP nn':          M1 + [('M', 3), ('M', 3)],
    'DJNZ (taken)':   M1x(1) + [('M', 3), ('I', 5)],
    'INC DE; JR':     M1x(2) + M1 + [('M', 3), ('I', 5)],
    'INC DE; JP':     M1x(2) + M1 + [('M', 3), ('M', 3)],
    'INC BC;LD A,B;OR C;JR NZ': M1x(2) + M1 + M1 + M1 + [('M', 3), ('I', 5)],
    'INC HL; JR':     M1x(2) + M1 + [('M', 3), ('I', 5)],
    'INC A; JR NZ':   M1 + M1 + [('M', 3), ('I', 5)],
    'INC E; JR NZ':   M1 + M1 + [('M', 3), ('I', 5)],
    'INC (HL); JR':   M1 + [('M', 3), ('I', 1), ('M', 3)] + M1 + [('M', 3), ('I', 5)],
}
if __name__ == '__main__':
    print(f"{'loop':28s} {'nomT':>5s} {'turboT A':>9s} {'turboT B':>9s} {'speed vs 3.5MHz':>16s}")
    for name, seq in loops.items():
        nom = sum(n for _, n in seq)
        r = per_iter(seq)
        print(f"{name:28s} {nom:5d} {r[0]:9.2f} {r[1]:9.2f} {2*nom/r[0]:8.4f} {2*nom/r[1]:8.4f}")
    print()
    for frame in (69888, 71680):
        print(f'frame {frame}: ROM (no waits) -> {2*frame} nominal T per frame;')
        for name in ('NOP', 'INC DE; JR', 'INC DE; JP', 'INC A; JR NZ', 'DJNZ (taken)', 'JR e', 'INC BC;LD A,B;OR C;JR NZ'):
            seq = loops[name]; nom = sum(n for _, n in seq); r = per_iter(seq)
            print(f'   RAM {name:26s}: {2*frame*nom/r[0]:9.0f} (phase A start) {2*frame*nom/r[1]:9.0f} (phase B start)')
