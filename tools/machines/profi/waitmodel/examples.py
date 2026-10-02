"""Worked examples: steady-state T per instruction in a loop of identical instructions, per sub-phase r
(r = master-clock phase of the CPU clock against the video counter, cnt mod 4 at Phi rising), paper vs border.
'ROM' marks a cycle outside RAM (code in ROM). Waits counted in 3.5 MHz T."""
from v5waitsim import run
R, O = 1, 0
INS = {
    'NOP (4)':                [('M1', R)],
    'LD A,(HL) (7)':          [('M1', R), ('MR', R)],
    'LD (HL),A (7)':          [('M1', R), ('MW', R)],
    'INC (HL) (11)':          [('M1', R), ('MR', R), ('X', 0), ('MW', R)],
    'PUSH BC (11)':           [('M1', R), ('X', 0), ('MW', R), ('MW', R)],
    'LD A,(nn) (13)':         [('M1', R), ('MR', R), ('MR', R), ('MR', R)],
    'LDIR repeat (21)':       [('M1', R), ('M1', R), ('MR', R), ('MW', R), ('X', 0), ('X', 0)] + [('X', 0)] * 5,
    'OUT (n),A (11)':         [('M1', R), ('MR', R), ('IO', 0)],
    'ROM: LD A,(HL) (7)':     [('M1', O), ('MR', R)],
    'ROM: NOP (4)':           [('M1', O)],
}
SUB = {0: 4, 1: 5, 2: 6, 3: 7}   # Sim start value that yields this sub-phase (see lineedge.py)
print('%-20s %6s | %s | %s' % ('instruction (T)', '', 'paper r=0,1,2,3', 'border r=0..3'))
for name, cyc in INS.items():
    base = sum({'M1': 4, 'MR': 3, 'MW': 3, 'IO': 4, 'X': 1}[k] for k, _ in cyc)
    row = []
    for fld in (1, 0):
        for r in range(4):
            res = run(cyc * 60, SUB[r], fld_fn=lambda mc, f=fld: f)
            w = sum(x[6] for x in res[len(cyc) * 20:])
            n = (len(res) - len(cyc) * 20) / len(cyc)
            row.append(base + w / n)
    print('%-20s %6d | %s | %s' % (name, base, ' '.join('%5.2f' % v for v in row[:4]), ' '.join('%5.2f' % v for v in row[4:])))
