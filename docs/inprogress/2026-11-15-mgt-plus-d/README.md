# MGT +D disk interface

Created: 2026-11-15 · Status: **landed** (P0-P5, P7 DISCiPLE; P0.6 closed without a PAL dump) · PLAN #123

One more machine configuration: the emulator read MGT images (`unrealdisk::LoaderMGT`) but had no +D, the interface
they were made for. This folder designs the +D (Miles Gordon
Technology, 1987): an edge-connector card with an 8 KB G+DOS ROM and 8 KB RAM paged over `0x0000-0x3FFF`, a WD1772
floppy controller with two drives, a printer port and a snapshot button, and the two configurations
`48K + Plus D` / `128K + Plus D` that use it.

| Document | What it holds |
|---|---|
| [hardware-reference.md](hardware-reference.md) | Paging, ports, control latch, WD1772 vs WD1793, disk format, G+DOS boot read from the ROM, snapshot formats, open items H-1...H-8 |
| [design.md](design.md) | Card `plusd` in the slot manager, ROM / RAM overlay and M1 traps, WD1772 as a `WD1793` variant, two controllers in one machine, media slots `fdd.plusd1/2`, snapshots / TTD, planes, Qt, phases P0-P7, docs to update, risks |
| [autostart.md](autostart.md) | Why most MGT disks have no DOS, the emulator's system disk, the autostart plans, what `load_software` returns |
| [test-plan.md](test-plan.md) | FDC, card, snapshot / TTD unit tests, boot-bound integration tests, automation, oracles, A/B benchmark |
| [TODO.md](TODO.md) | Phase checklist, open items, owner decisions |

## Findings worth knowing before coding

- Most +D disks do not carry the DOS (`+SYS 2a`): they expect G+DOS already in the +D RAM. The autostart therefore boots an emulator-supplied system
  disk first, then swaps and starts the work.
- The G+DOS v1a ROM is already in the tree (`data/rom/gdos-pd.rom`, equal to MAME's and Fuse's), unused.
- MAME and Fuse disagree on the `0x028E` trap and the reset state; the ROM itself has a `KEY-SCAN` hook at `0x028E`
  and an initialisation entry at `0x0000`, so Fuse's behaviour is followed (H-1, H-2).
- No card can page memory today, no machine has two floppy controllers, and the `WD1793` has no chip variants: these
  three are most of the work. The +D logic itself is small.

## Not in scope

A real printer emulation (the port is honest, bytes go to an optional file); a +D fast-disk path;
DISCiPLE is phase P7 (owner decision D-4), done 2026-11-20: [hardware-reference.md §9](hardware-reference.md#9-disciple-p7).
