"""Sensitivity of the DS80 border (no video request) wait to the Z80 MREQ delay (dz), the WAIT setup time and the
flip-flop delay, per ZQ3 frequency. Prints the mean waits per RAM M-cycle for an LD A,(HL) loop, normal speed."""
from ds80waitsim import run, UNIT_NS
import ds80waitsim
cyc = [('M1', 1), ('MR', 1)] * 30
print('dz_ns setup_ns dff_ns |' + ''.join(f'  {x}MHz' for x in (16, 20, 24)))
for dff in (3, 4, 5):
    for dz in (5, 8, 11, 14, 17, 20):
        for setup in (0, 5, 9, 14, 17):
            row = []
            for xmhz in (16, 20, 24):
                ws = []
                for cp in range(4):
                    r, _ = run(cyc, xmhz=xmhz, f0_phase=0, cpu_phase=cp, req_on=0, dz=dz, setup=setup, dff=dff)
                    ws.append(sum(x[3] for x in r[10:]) / len(r[10:]))
                row.append(sum(ws) / len(ws))
            print(f'{dz*UNIT_NS:5.0f} {setup*UNIT_NS:5.0f} {dff*UNIT_NS:5.0f} |  ' + '  '.join(f'{v:5.2f}' for v in row))
