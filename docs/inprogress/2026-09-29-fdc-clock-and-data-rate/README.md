# FDC clock and data rate

Research and outcome for the WD1793 (КР1818ВГ93) controller clock and the disk data rate: what
"turbo VG" on Spectrum clones really does, why a 3.5 MHz Spectrum cannot read HD floppies, and what
the Sprinter needs for 1.44 MB disks.

| File | What it is |
|---|---|
| [research.md](research.md) | the research: datasheet facts, per-machine table, emulator and RTL survey, open questions (§6), sources |
| [DONE.md](DONE.md) | status marker: the implementation landed; follow-ups and where they are tracked |

Permanent documentation (the model as implemented):
- [WD1793_Clock_And_Data_Rate.md](../../WD1793/WD1793_Clock_And_Data_Rate.md): every case with worked
  examples, the clock policy per machine, `[Beta128] TurboVG=`, how to add a machine.
  (The file name follows the existing `WD1793_*` naming of `docs/WD1793/`, not kebab-case.)
- [WD1793_Timeouts.md](../../WD1793/WD1793_Timeouts.md#controller-clock-and-data-rate-in-unreal-ng):
  timer values per clock.

Users of the mechanism:
- Sprinter (PLAN #59, phase S3a): wires the `Latched` policy to its `#BD` density latch,
  [tdd-storage.md](../2026-09-28-sprinter/tdd-storage.md) §2.3.
- Media manager: the track density rule for floppy images,
  [integration-floppy.md](../2026-09-28-storage-manager/integration-floppy.md) §2.
