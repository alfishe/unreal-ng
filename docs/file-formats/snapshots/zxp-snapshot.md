# ZXP (ZX-Poly) Snapshot Format

`.zxp` is the snapshot format of the ZX-Poly Java emulator ([raydac/zxpoly](https://github.com/raydac/zxpoly)). The ZX-Poly is a
quad-Z80 Spectrum: four CPU modules run the same program in step, and their 1-bit screen planes are combined into 4-bit color.
A `.zxp` holds the full state of **all four modules**. It is written by the ZX-Poly Sprite Corrector when an adaptation is
exported, and read by the emulator.

The layout is **big-endian**, which sets it apart from the other snapshot formats here.

## Layout

| Offset | Size | Field |
|-------:|-----:|-------|
| 0 | 4 | Magic `0xC0BA0100` |
| 4 | 4 | Flags |
| 8 | 1 | #3D00: the platform control register. Bit 0 nWAIT (the slaves run), bit 1 local reset, bits 2-4 video mode, bits 5-6 the mapped CPU, bit 7 lock |
| 9 | 1 | #FE (the border and ULA latch) |
| 10 | 4 x 5 | Per module: #7FFD, then the platform registers R0, R1, R2, R3 |
| 30 | 11 x 4 x 2 | Per register, four modules: AF, AF', BC, BC', DE, DE', HL, HL', IX, IY, IR |
| 118 | 4 + 4 + 4 | Interrupt mode (4 bytes), IFF1 (4 booleans), IFF2 (4 booleans) |
| 130 | 4 x 2 | PC of each module |
| 138 | 4 x 2 | SP of each module |
| 146 | per module | One byte N, then N times: a page index (1 byte), 16384 bytes of RAM |

The page index is the 128K bank number 0-7; a module stores only the pages it uses. The header is 146 bytes; nothing may follow
the last module's pages.

In the corpus the registers are identical across the four modules at the snapshot moment (the program is in lockstep) while R0
differs: the master has `0x00`, and the slaves `0x12`, `0x14`, `0x16` (their I/O writes are disabled and their heap windows start
at 128 KB, 256 KB and 384 KB).

## In Unreal-NG

Snapshot overview and how loading works: [README](README.md).

- A `.zxp` is read by the **ZX-Poly machine**, not by a single instance: module *i* goes into instance *i* of the group. The
  group is created from the file (for example `emulator_manage` `create` with a ZX-Poly configuration and the path of a `.zxp`).
  Every module must be a model with 128K #7FFD paging.
- The file is parsed and validated completely before any module is touched. A wrong magic, a truncated file or trailing bytes are
  refused with the reason.
- The group is **planned, then committed**: each module's image (banks, CPU, #7FFD, the border) goes through the snapshot plan on its own machine (the shared fit check, the machine's policy, a caller's `commit`), and one refusal stops the whole load before any module is touched (the reason names the module, `module 2: ...`). Each module is then committed from its image.
- After the modules the group latches #3D00 and the module registers.
- A plain SNA or Z80 given to a ZX-Poly group goes to one instance only (a normal snapshot is not replicated into the four modules).
- Loading only; there is no ZXP writer.

Test material: `testdata/machines/zxpoly/zxp/` with the measurements in its README.

## References

- [raydac/zxpoly](https://github.com/raydac/zxpoly), the emulator and the Sprite Corrector
- [The format's grammar (JBBP)](https://github.com/raydac/zxpoly/blob/master/zxpoly-emul/src/jbbp/snapshots/zxp/com.igormaznitsa.zxpoly.formats.ZXPParser.jbbp)
