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

## The option table and the SYS byte (2026-10-06)

`Variables` (`#8865`) starts with a table of 4-byte entries: a pointer to the option word (ending in `#FF`), the
current value, and the value used when the option is given. `ParseOptions` (`#8543`) copies byte 3 over byte 2
for each `/word` it finds on the `.ZX` option line; the entries' byte 2 are the labels the code reads:

| Entry | Word | Value (`l....h`) | Absent | Given | Read by |
|---|---|---|---|---|---|
| `#8865` | `turbo` | `#8867` | `#02` | `#03` | `E`, the SYS byte: bit 1 = 1 writes the turbo bit, bit 0 = turbo |
| `#8869` | `lines312` | `#886B` | `#41` | `#61` | `OUT (#BD),A` before the start |
| `#886D` | `sprinter` | `#886F` | `#0C` | `#04` | `E`: bit 2 = 1 writes CNF, bit 3 = port map 1 / 0 |
| `#8871` | `7FFD` | `#8873` | `#30` | `#00` | the first `#7FFD` value (`#30` = 48K lock + ROM 1) |
| `#8875` | `1FFD` | `#8877` | `#40` | `#00` | `E`: CNF bit 6 holds `#1FFD` cleared |
| `#8879` | `mem512` | `#887B` | `#00` | `#80` | `E`: CNF bit 7, Pentagon 512 |
| `#887D` | `int-sc` | `#887F` | `#00` | `#01` | `D` of `StartStub`: non-zero starts TR-DOS (`#3D29`) |
| `#8881` | `to-trdos` | `#8883` | `#02` | `#01` | the BIOS `FN_SYNC` mode (`C = #F2`): 2 Pentagon, 1 Scorpion |
| `#8885` | `no-run` | `#8887` | `#FF` | `#00` | `#85E6`: stop after loading |
| `#8889` | `origin` | `#888B` | `#00` | `#03` | ALL_MODE `#FA` and `FN_SYNC` mode 3 |
| `#888D` | `ret-zx` | `#888F` | `#00` | `#41` | not read |
| `#8891` | `ret-fn` | `#8893` | `#00` | `#41` | `#FFF6` of page `#41`: `ResetHook` goes to DSS when non-zero |

`E` = turbo + sprinter + 1FFD + mem512 (`#8834-#8843`). `EnterZxMode` writes `#04` and `#1C` to the SYS port
`#7C` (bit 1 = 0: the turbo bit is left alone, the preparation runs at 21 MHz); `StartStub` at `#FF00` writes `E`
to `#3C` and jumps into the ROM, so the CPU clock of the mode is set by that one write. Values for each `.ZX`
mode and the PLD side: [research-zx-mode.md](../../../../inprogress/2026-09-28-sprinter/research-zx-mode.md) §4.1.

Read from the bytes and not tried on a machine: the `int-sc` and `to-trdos` words point at each other's values.
As written, `/int-sc` makes `StartStub` start TR-DOS and `/to-trdos` asks `FN_SYNC` for the Scorpion INT. None
of the `.ZX` files seen uses either word in this spelling (`SC256.ZX` has `/sc-int`, which matches no entry).
