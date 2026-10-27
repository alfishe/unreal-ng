# Research: the original DivMMC (Mario Prato, 2013) against the Next's DivMMC

**Date:** 2026-10-10 · part of [README.md](README.md) · extends [audit-divmmc-gap-2026-10-10.md](audit-divmmc-gap-2026-10-10.md),
[esxdos-and-sd.md](esxdos-and-sd.md) and the [storage survey](../2026-09-28-storage-controllers-survey/divide-divmmc-esxdos.md) section 2

Until now the classic board was known only second-hand (zxsp, pico-spec, Karabas-Pro RTL, MAME). The primary sources are public and are now in
the collection: `/Volumes/TB4-4Tb/Data/zx-spectrum/hardware/sd-divmmc/mprato-DivMMC/` (upstream https://github.com/mprato/DivMMC, commit `dd92c68`,
2023-02-22; GPL-3.0): the CPLD source `divmmc/divmmc.vhd` (version 1.0, 2012-11-22), the schematic `DivMMC_2013_schematic.pdf`, Zilog's
`docs/Divide_pgm_model.txt` and the 2023 `docs/Divmmc_allram_manual.txt`. This note reads them against the Next core 3.02.03
(`device/divmmc.vhd`, `zxnext.vhd`, `serial/spi_master.vhd`) and records what that changes.

## 1. The board (schematic)

One Xilinx **XC9572XL-VQ64** CPLD (3.3 V, AMS1117-3.3 regulator) holds the whole interface: the automap, the `#E3` register, the chip selects and the
SPI byte engine. Around it: a 28C64 EEPROM (U1, write-protected by a jumper), a 128K or 512K RAM (U2; R7 / R8 fitted for 512K, R9 for 128K), two
miniSD sockets (U4, U5: shared CLK / CMD / DAT0, separate chip selects), a 74LVC1G14 inverter on the CPU clock, a power-on RC (`poweron`, R1 / C1) that is
**separate from the CPU reset**, a RESET and an NMI button, three LEDs (map active, card 1, card 2), two transistors and a jumper (J3) for the +2A / +3 ROMCS,
and a JTAG header. The card interface is **not** bit-banged: the SPI pins hang on the CPLD.

## 2. The SPI engine (`divmmc.vhd`)

States `IDLE`, `SAMPLE`, `TRANSMIT`; a 4-bit `TState`; clock = the inverted CPU clock; `spi_clock <= TState(0)` (SCK = CPU clock / 2, mode 0, MSB first).

- An access to `#EB` seen in `IDLE` (full 8-bit address compare, IORQ low, M1 high) moves to `SAMPLE`; a write loads `toSDByte` from the data bus (a read loads
  nothing), then `TRANSMIT` runs 16 clock edges (`TState` 0..15). Eight bits are sampled on the odd edges 1..13 and at edge 15.
- `toCPUByte` (what a read returns) is loaded **only at edge 15**: a read that arrives while the byte is in flight gets the **old** byte.
- An access while `TRANSMIT` is running (`TState < 15`) is ignored. At edge 15 a *write* request chains straight into the next byte (no idle clock); a read does not chain.
- A read starts a transfer too (the engine does not distinguish): this settles the survey's "unverified" point. The byte sent is whatever `toSDByte` holds; after a
  full byte the register is `b0` followed by seven 1s (it shifts seven times, and the eighth bit goes out at edge 15 without a shift), so **the first MOSI bit of a
  read byte is bit 0 of the previous write**, then 1s. Harmless in practice (every command frame ends in a CRC / stop byte whose bit 0 is 1; a card ignores MOSI
  while it answers). Read from the source, not simulated.
- `#E7` write stores `D(1:0)` into `card(1:0)` raw (active low); `reset` (the CPU reset line, not `poweron`) sets both to `1` and stops the engine.

Next's `spi_master.vhd` is the same engine with a different edge: a 4-bit counter that runs 16 clocks from `spi_begin` (read or write, only when the counter is
zero), a read clears the shift register to all 1s (no MOSI leftover), `miso_dat` is latched when the counter is zero, no chaining. The same two rules that the audit
records as D11 / D13 therefore hold on the original board: the byte is 16 **CPU** clocks, and an early read returns the old byte and starts nothing. Our `SpiRead`
before the D13 fix was wrong for both boards; the fix follows both.

## 3. Memory paging and the automap

| Point | Original CPLD (`divmmc.vhd`, `Divide_pgm_model.txt`) | Next core 3.02.03 (audit) | Consequence |
|:--|:--|:--|:--|
| Entry addresses | `#0000 #0008 #0038 #0066 #04C6 #0562` (fixed, full 16-bit compare) and `#3D00-#3DFF` | NR `#B8-#BB`: any of the 8 RST addresses, `#3Dxx`, `#056A #04D7 #0562 #04C6`, `#0066` | Next is a superset; `#056A` / `#04D7` and `#0010-#0030` exist only on the Next |
| Timing | one two-stage latch (`mapcond` then `automap`, both on the falling edge of MREQ in an M1 cycle): an entry fetch comes from the Spectrum ROM, **the next M1** from the board; `#3Dxx` instant | per entry NR `#BA`: delayed or instant; `automap_held` takes effect from the next memory cycle (operands of the entry instruction included) | the original changes only at an M1 edge, the Next at the next memory cycle; no entry instruction has operands, so no known program differs |
| `#0066` | an ordinary entry: any fetch at `#0066` maps | only with `button_nmi` and NR `#BB` bit 1 / 0 (D4) | a program that jumps to `#0066` by hand is trapped on the original, not on the Next |
| Release | **only** a fetch at `#1FF8-#1FFF` (`mapcond and map1F00`); no RETN rule, no reset rule: `mapcond` has no reset | reset, NR `#0A` bit 4 = 0, port disable, `ED 45`, `#1FF8` | RETN and reset release are Next additions; on the board a held map survives a CPU reset |
| Reset | `poweron` clears bank, MAPRAM, CONMEM; the CPU `reset` clears only the chip selects and the SPI engine | NR `#02` soft / hard reset clears `#E3` and the latches (D5) | MAPRAM, CONMEM and the bank survive a CPU reset on the original board |
| `#E3` | write-only, full 8-bit decode, `bank(5:0) <= D(5:0)`, `mapram <= D(6) or mapram`, `conmem <= D(7)` | readable, 4 bank bits (bits 5:4 read 0), MAPRAM cleared by NR `#09` bit 3 | 8K banks: 64 on the board (512K), 16 on the Next |
| CONMEM vs MAPRAM | **CONMEM wins**: `romoe` opens when `conmem`, and `ramoe` is closed at `#0000-#1FFF` when `conmem`; priority "jumper, MAPRAM, CONMEM" (Zilog) | **MAPRAM wins** since 3.02.00: `rom_en = ... and not mapram` (D3) | esxDOS-for-classic and NextZXOS see different results for CONMEM + MAPRAM; a classic-card model must keep the board's rule |
| Lower 8K in MAPRAM | bank 3 forced for A13 = 0 (`bankout(1:0)` = 11, `bankout(5:2)` = 0), read-only unless CONMEM | bank 3, read-only | same |
| Automap enable | `romcs = (automap and not eprom) or (automap and mapram) or conmem`: the trap works only with the EEPROM present (jumper) or MAPRAM set | NR `#0A` bit 4 | |
| +2A / +3 | jumper J3 / ROMCS2 | ROM 3 condition in the decode (`#B9`) | |
| 2023 ALLRAM revision | port `#0F3B`: D7 ALLRAM, D6 WRLOCK, D5 MAPDISABLE, D4 MAPRAM_PAGE (3 to 11 / 59); `OUT #E3,%11xxxxxx` with MAPRAM set resets MAPRAM and sets CONMEM (Velesoft); bit 4 of `#7FFD` honoured; the NMI button is off while CONMEM | not present | a different product line; not for the Next |

## 4. What changes

1. **The Next model stays as the audit left it.** Every Next rule in D1-D14 was read from the Next VHDL; the original board never was the reference for it. The
   differences above are properties of two boards, not defects in the Next model. D3 (CONMEM versus MAPRAM) is the one place where a classic-card model would have
   to choose the other rule.
2. **D8 and D13 hold on both boards** (D8: the original has no `RETN` rule at all, so the `ED 45` question only exists on the Next; D13: confirmed by the CPLD).
3. **The survey is settled:** a classic DivMMC read starts a new transfer (it was "unverified"); `#E3` is write-only; reset does not clear MAPRAM, CONMEM, the
   bank or the automap latch (only `poweron`); the byte is 16 clocks; the first MOSI bit of a read is the previous write's bit 0.
4. **If a classic DivMMC personality is added later** (the storage survey's plan, `DivPaging`), it takes the CPLD's rules from section 3, not the Next's: fixed
   entries, M1-edge two-stage latch, release only at `#1FF8-#1FFF`, no reset or RETN rule, CONMEM over MAPRAM, write-only `#E3` with 6 bank bits, raw `D1:D0` selects,
   an SPI engine with write chaining at edge 15.
5. **Attribution in the knowledge base was wrong:** the schematic reads "SD card interface for the ZX Spectrum 2013 By Mario Prato", and the DivIDE programming model is
   signed "Zilog". The knowledge-base articles that credit the DivMMC to "Zoxon" and the DivIDE to "Zeax" are corrected to the primary sources.

## 5. Open points

- Whether a real board's first MOSI bit matters for any card: not testable here (no hardware, no CPLD simulation run).
- The `#0F3B` ALLRAM port: nothing in the Next maps to it; out of scope.
- Four JEDEC images are kept for a future hardware-level check (`xc9572xl` bitstreams, not disassembled).
