# Peters Plus SPECTRUM.EXE (Sprinter Spectrum launcher, 2002)

The DSS program that switches a Sprinter Sp2000 into the ZX (Spectrum-compatible) mode on any BIOS, by Ivan Mak
(Peters Plus). No source was published; the community rewrite (v2.03, with source) is a different program.
Context: [research-zx-mode.md](../../../../inprogress/2026-09-28-sprinter/research-zx-mode.md) §4.

| File | Size | CRC32 | MD5 | Found in |
|---|---|---|---|---|
| `SPECTRUM.EXE` | 2 696 bytes | `98FDD183` | `f641aa98b779f04b67a356ef3e05919f` | `ZX\` of the DSS 1.62 floppy and of the ZXMAK2 `sp_disk1.vhd` |

[spectrum-exe-pp.asm](spectrum-exe-pp.asm): z80dasm 1.2.0 output (`-a -t -l -g 0x8100`, data blocks
`#81EC-#849E` and `#8865-#8987`) of the code after the 512-byte DSS EXE header (load and start `#8100`, SP `#BFFE`),
with the main routines named and described by hand. Every line keeps its address and bytes. Symbols for the
emulator's label loader: [data/symbols/sprinter/spectrum-exe-pp.map](../../../../../data/symbols/sprinter/spectrum-exe-pp.map).

What it shows:

- the ROM images go into fixed RAM pages `#42-#47`; Spectrum page n is physical page n (`InitSpectrumPages`);
- cells it cannot reach by a port are set by patching the port-table entry of port `#0000` and doing `OUT (#0000)`
  (`SetCellViaPortTable`);
- a TRD on the command line is read into a BIOS RAM disk and attached to TR-DOS drive A (`LoadTrdToRamDisk`:
  BIOS `#93`, `#92`, `#C7`, `#CB`), which only the Sprinter TR-DOS 7.0x reads;
- ALL_MODE (`#204E`) = `#FA` with `/origin` ("original waits"), `#FE` otherwise (`EnterZxMode`);
- the return to DSS uses the BIOS reset intercept in page `#41` (`InstallResetHook`, `ResetHook`).
