# Status: TODO

Hardware probe programs for the ZX Spectrum Next. Written 2026-10-08; assembled; **not run anywhere**.

## Order
- [ ] 1. Smoke test in an emulator that models the Next (jnext, ZEsarUX, MAME `specnext`, or CSpect): each program must reach `D0DE`; the numbers are only plausible there (emulators are not the reference). Fix any crash before asking anybody to run them on a board.
- [ ] 2. Run on a real Next, photograph, fill [results/](results/README.md). Any core version; note it.
- [ ] 3. Compare with the predictions in [experiments.md](experiments.md); write the differences into the Next design's `research-fpga-vhdl.md` and the test expectations (grade A evidence in `verification-program.md`).
- [ ] 4. Repeat on a second board / core version when possible (the core changes the timing now and then).

## Ideas not done (add when the first results are in)
- Contention in the other machine timings (48K / 128K / +3 / Pentagon): needs the config-mode machine type change (NR `#03`), so it has to run from a personality set in `config.ini` rather than from NextZXOS.
- SPI byte spacing: the 16-clock rule (a byte begun earlier is ignored). Needs a spare SD card and a bare program; the card in use by NextZXOS must not be touched.
- Sprite per-line time budget in more detail (sprites partly off-screen, over-the-border sprites).
- Copper: the behavior at the end of the list in mode `01`.
