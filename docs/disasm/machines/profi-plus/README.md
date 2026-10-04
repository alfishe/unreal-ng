# Profi / Profi+ software: disassembly notes

Programs that run on the Profi (`PROFI`, `PROFI3`) and Profi+ (`PROFI-PLUS`) machines, looked at while checking
the emulation. Machine background: [`.recipe/machines/profi.md`](../../../../.recipe/machines/profi.md),
[`docs/inprogress/2026-10-04-profi-plus/`](../../../inprogress/2026-10-04-profi-plus/design.md).

| Folder | Program | Finding |
|:--|:--|:--|
| [`sp-demo/`](sp-demo/README.md) | `SP.COM` music demo (PQ-DOS HDD image, `DEMOS`) | Logo in colored dashes: the program passes a wrong attribute pointer to the console's `ESC i`; not an emulator bug |
| [`pqdos-hdd-programs/`](pqdos-hdd-programs/README.md) | all 116 `.COM` programs of the PQ-DOS HDD image | which start, and the causes of the failures: PQ-DOS lacks BDOS 98, BDOS 9 stops at NUL; JAZZY / COLUMNS open |
