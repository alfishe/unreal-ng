# Compare alternative wait rules (waits at phase A, phase B) against the Tact Meter RAM figure 88208 / 71680
import importlib.util, sys
spec = importlib.util.spec_from_file_location('tm', sys.argv[1]); tm = importlib.util.module_from_spec(spec); spec.loader.exec_module(tm)
def run(seq, wa, wb, ph, reps=4000):
    t = ph
    for _ in range(reps):
        for k, n in seq:
            t += n + ((wa if t % 2 == 0 else wb) if k == 'M' else 0)
    return (t - ph) / reps
for wa, wb in ((2, 3), (2, 1), (2, 2), (3, 3), (1, 2)):
    row = []
    for name in ('NOP', 'INC DE; JP', 'INC DE; JR', 'LD A,(HL)'):
        seq = tm.loops[name]; nom = sum(n for _, n in seq)
        r = min(run(seq, wa, wb, 0), run(seq, wa, wb, 1))
        row.append(f'{name}: {r:5.2f} T -> {2*71680*nom/r:7.0f}')
    print(f'waits A={wa} B={wb}: ' + ' | '.join(row))
