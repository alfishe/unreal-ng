# Scorpion ZS-256 CP/M disk

Collected 2026-10-03 for the Scorpion ZS-256 (`SCORPION`, `PROFSCORP`) machines.

## `moa-cpm-disk01.trd` - Scorpion CP/M, disk 01 (system + Turbo Pascal, WordStar)

- **Source:** zxart.ee entry "CP/M" (system software for the Scorpion ZS 256, published by Scorpion,
  1992): <https://zxart.ee/eng/software/system-software/operacionnye-sistemy/moa-cpm/>, release file
  <https://zxart.ee/releasefile/id:551401/cp_m_01.zip> (350 454 bytes, holds `cp_m_01.trd` dated
  2000-02-01 and `cp_m.txt`). Fetched 2026-10-03. Original name `cp_m_01.trd`, unchanged bytes.
- **CRC32:** `DB48E037`, **SHA-256:** `d2e63b8aac825049f0122742e03a11fe3041cdfc7fa5bedece687bdae4036a76`
- **Format:** TRD, 655 360 bytes, 80 cylinders x 2 sides x 16 sectors x 256 bytes (standard TR-DOS
  geometry). The TR-DOS catalog has one BASIC file, `boot`, the disk label is `CP/M 48k` and the disk
  reports 0 free sectors: the CP/M file system fills the rest of the disk.
- **Sign-on:** the `boot` loader prints "(c) 1992 SCORPION 256 SYSTEM LOADING"; the system carries
  "ZX Spectrum + CP/M (c) 1991 MOA" (the MOA CP/M) and "CP/M 48k".
- **How to boot:** drive A, start TR-DOS from the Scorpion's ProfROM / boot menu and run `boot`
  (`RUN` on an empty line in TR-DOS, or the menu's disk boot). Not yet booted on the emulator.
- **Contents (CP/M side):** Turbo Pascal 3.02 (`TURBO.COM`, `.MSG`, `.OVR`), WordStar (`WS.COM` and
  overlays, `MAILMRGE.OVR`), the Digital Research tools (`ASM`, `MAC`, `LOAD`, `DUMP`, `PIP`, `STAT`,
  `SUBMIT`, `XSUB`, `SID`, `ZSID`, macro libraries), `L80`, `POWER 3.03`, `DU`, `NSWEEP`, `FORMAT`
  (CP/M disk format utility 2.1, MPTI), `SDC`, `XDIR`.
- **Series:** `cp_m.txt` lists 16 disks (01 Turbo Pascal + WordStar, 02 dBASE II, 03 SuperCalc +
  Multiplan, 04 C, ... 16 games). Only disk 01 is published on zxart.ee; it is also the one with the
  system.
