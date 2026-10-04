"""Mean waits per M-cycle in DS80 (v5.06 parts: Z84C0020, 1533 logic), per ZQ3, speed, video requests on/off,
for a loop of M1 + memory read (code in RAM or in ROM, data in RAM). Waits in CPU T at that speed."""
from ds80waitsim import run
for x in (16, 20, 24):
    for t in (False, True):
        for req in (0, 1):
            for code in (1, 0):
                m1 = []; mr = []
                for ph in (0, 7, 13):
                    for cp in range(4):
                        r, _ = run([('M1', code), ('MR', 1)] * 30, xmhz=x, f0_phase=ph, cpu_phase=cp, req_on=req, turbo=t)
                        r = r[10:]
                        m1 += [w for (_, k, ram, w) in r if k == 'M1']; mr += [w for (_, k, ram, w) in r if k == 'MR']
                print(f'ZQ3 {x} MHz {"turbo " if t else "normal"} CPU {x/(2 if t else 4):4.1f} MHz  requests {"on " if req else "off"}  '
                      f'code in {"RAM" if code else "ROM"}: M1 {sum(m1)/len(m1):.2f}  read(RAM) {sum(mr)/len(mr):.2f}  '
                      f'max {max(m1+mr)}', flush=True)
