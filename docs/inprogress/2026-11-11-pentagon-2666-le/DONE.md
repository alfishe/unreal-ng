# DONE — Pentagon 2.666 LE

Closed on 2026-11-19. Everything below is on master; the phase checklist with the details is in [TODO.md](TODO.md),
the board in [hardware-reference.md](hardware-reference.md), the fit in [design.md](design.md).

- [x] P0-P4: model `MM_P2666`, ROM set, decoder with the Gluk mapping, clocks checked against the board's VHDL
  (POC 051), video, CMOS / SD / IDE / mouse / sound, the RAM-disk Beta Disk and the UART (`d24dc0dc0`)
- [x] P5: TTD, snapshots, automation planes, recipe (`9f5d9774a`); Linux gcc 16 and Windows MSVC / MinGW checks
  (2026-11-16)
- [x] Disk controller report: `state fdc` on every plane is the FPGA's WD1793 imitation on the RAM disk, the board
  having no floppy controller (`a8a0f6aa4`, `f1f36f26d`, `dd90d7d5a`)
- [x] Machine menus: the Pentagon family of `MachineCatalog` (Qt, handheld, automation lists; `ec69b43b0`), the
  Stream Deck machine page key (`e117763e6`); landed `8a6481e3e`
- [x] gcc 16.2 (CI's build-gcc-latest configuration) over the last changes: 0 warnings (2026-11-19)
- [x] Real software: [testdata/machines/p2666/software/README.md](../../../testdata/machines/p2666/software/README.md)

## As built, not bugs (owner, 2026-11-11)

The board's own bugs are copied (even Nemo IDE registers read `#FE`, the 8-bit sound sum wraps, D-1...D-12 of the
hardware reference); the fixes exist only as commented `P2666-FIX` recipes.
