# PQ-DOS on the PROFI-PLUS

Two disks the `ProfiPlusBoot_Test` boot tests use (PQ-DOS by Vadim / Star Software, DOS Navigator 2.0.16 by Starsoft; run
with ROM BIOS Plus 0.41h1, `PROFI-PLUS`). Design and status: `docs/inprogress/2026-10-04-profi-plus/`.

| File | Size | CRC32 | md5 | What it is |
|:--|--:|:--|:--|:--|
| `pqdos1.fdi` | 748495 | `4522C21A` | `63e7575daef142bf2c48c0b2511c8fcf` | the PQ-DOS boot floppy (FAT12, 80 x 2 x 9 x 512, label "UNTITLED"; `QDOS.SYS` 2023-09, `TRDBOOT.BIN` asks for BIOS 0.40): Karabas Pro `software/profi/pq-dos/pqdos1.fdi`. Boots to the "PQ-DOS Startup Menu", then (the 30 s timeout picks entry 2) to DOS Navigator on `A:\` |
| `pqdos-hdd-small.img` | 3050496 | `46EC82F8` | `01f86c7c5084dd6a771ec4874cfa24bc` | the 2 GB PQ-DOS 2023-09 hard disk image of Karabas Pro cut to 2.9 MB by [tools/machines/profi/pqdosimage](../../../../tools/machines/profi/pqdosimage/README.md): MBR + one FAT16 partition with the root files, `DN` and `DOS`. On an IDE master (`ide0.master`) the BIOS boots PQ-DOS from it straight into DOS Navigator on `C:\` |
