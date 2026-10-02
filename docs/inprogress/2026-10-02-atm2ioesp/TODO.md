# TODO: ATM2IOESP

**Plan:** network adapters · [README.md](README.md)

| Step | Status |
|:--|:--|
| Research | done 2026-10-02: [reference-atm2ioesp.md](reference-atm2ioesp.md) |
| ATM 7.10 #FE decode A2..A0 = 110 (only the v7.10 board; ATM 4.50 keeps A0 and no longer gets the v7.10 keyboard controller it had by inheritance) | done 2026-10-02 |
| INTERNAL I/O bus (#FB latch, #FA strobes, reset, TTD blob 28) | done 2026-10-02 |
| The card (16550 + peer, TTD blob 29, guest 4 on the virtual network) | done 2026-10-02 |
| Config, runtime keys, state report on every surface, Qt Network window | done 2026-10-02 |
| Tests | done 2026-10-02: `Atm2IoEsp_Test` (10) |
| NedoOS end to end (20 runs of `wget example.com/`) | done 2026-10-02: 20 of 20 complete (the whole page), 0 overruns, 7 MHz turbo with the RAM waits, `Atm2IoEsp=ESPNET` at 115200. The ATM2 COM in the same series: 16 of 20 (each failure one lost byte) |
| Open questions (reference): an unselected #FA read, the full #FA / #FB decode, #FB read without a printer, other ATM boards | open |
