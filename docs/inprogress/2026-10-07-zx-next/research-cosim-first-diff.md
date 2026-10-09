# Co-simulation: first diff of the real boot, jnext vs unreal-ng

**Date:** 2026-10-09 · part of [README.md](README.md) · tools: `tools/verification/next-cosim/` (README there)

Question: why does the personality ROM (`enNextZX.rom`, started by the real `TBBLUE.FW` from the FAT card) restart every
~19 frames instead of reaching the NextZXOS menu on our emulator?

## Setup

* Card: `~/Downloads/zx-spectrum/zx-next/cards/sn-test-card` (ours, through `HostFolderFat`) / the same tree as a 1 GB FAT32
  SD image (references). 1500 frames, `COSIM_PORT_SKIP=eb,e7`.
* References: jnext 0.99.155, ZEsarUX 13.1-SN, MAME 0.289 `specnext_ks2` (boot ROM 3.01.00), all patched to the trace format of
  `trace-format.md`. **All three run the real chain (boot ROM -> `TBBLUE.FW` -> personality ROM -> NextZXOS) and show the
  NextZXOS "Welcome" screen at frame 1500.** (The older text in [research-other-emulators.md](research-other-emulators.md)
  that ZEsarUX does not run the firmware does not hold for the current ZEsarUX: its trace has the same firmware NR writes.)
* Ours: `core-tests --gtest_filter='NextCosim_Test.*'` (`core/tests/_helpers/nextcosimtrace.h`), at the tip of `zx-next`
  (`1b868c3cc`). Final state at frame 1500: PC `0168`, IFF off, never at the menu.
* Command: `diff-traces.py <jnext-dir> <ours-dir> --ignore-regs 40,41,43,44,18,19,1A,1B,1E,1F --kinds NRW,NRR,POUT,MMU`.

The three references agree with each other on everything below (NR #8C sequence, the register read-backs); where one differs
it is said.

## Result: root cause

**NR #8C (AltROM) is not implemented. The personality's esxDOS / RST 8 trampoline depends on it, and without it the CPU runs
off into the wrong ROM code and restarts through `jp 0` - no NR #02, exactly the symptom.**

Evidence (first event that matters, ours vs jnext):

1. The ROM first writes `NR #8C <- C0` at `0E37` (AltROM enable + "writes go to the alt RAM"), copies code into the
   0000-3FFF area, then uses the trampoline at `$0072`:

   ```
   0072: ED 8A 00 7B   push $007B           ; Z80N PUSH imm16
   0076: C5            push bc
   0077: ED 4B 54 5B   ld bc,($5B54)
   007B: ED 91 8C 80   nextreg $8C,$80      ; AltROM enabled, reads come from the alt RAM
   007F: C9            ret                  ; into the alt-ROM image at 0003/0006
   ```
   The alt image has its own code at `$0006` and at `$007B` (`nextreg $8C,$00` ... ret), which switches the normal ROM back.
   References (all three): `8C <- C0 @0E37`, then `80 @007B / 00 @007B` pairs, 28+ times in 1500 frames; PC trace in jnext:
   `... 3E13 [8E=00] 3E17 007B [8C=80] 007F 0003 0006 007B [8C=00] 007F 3E13 [8E=02] ...`.
2. Ours (PC window of the frame): `... 3E13 [8E=00] 3E17 007B [8C=80] 007F 0003 0004 0005 0006 0007 0008 103B ...`:
   after `8C=80` the CPU still reads the **normal** ROM: `0003..0008` is the start of the ROM (`F3 C3 EF 00 / 45 44 09 02 / C3 3B 10`
   = `di; jp 00EF` ... `RST 8: jp 103B`), runs into the esxDOS call handler, returns a stack value of `FFFF`, executes
   `RST 38h` code from `FFFF`, falls to `0000` -> `00EF` (the ROM's init; the first writes `07<-03, 03<-B0, C0<-08, 82..85<-FF`
   are its init sequence) and the whole thing starts again ~every 19 frames. No `NR #02` is written in between: it is the
   ROM jumping to its own reset vector.
3. `grep -i altrom` in `core/src/emulator/io/z80n`, `memory/next`, `ports/models/portdecoder_next.*` finds nothing; NR #8C is a
   plain stored register. VHDL: `nr_8c_altrom` (zxnext.vhd:387-392, 2255-2265, 2981-3003); jnext `Mmu::altrom_sram_page_`.

What to implement (from jnext/MAME/VHDL, to be checked against the VHDL): NR #8C bit 7 enable, bit 6 `rw` (1: the alt RAM is
the target of WRITES to 0000-3FFF - used to fill it, 0: it is what READS see), bit 5 / bit 4 lock ROM1 / ROM0 (the 48K / 128K
selection of the alt 16K), bit 3:0 -> the 4-bit shadow `nr_8c_altrom(7:4) <= (3:0)` on a write with bit 3 ... (the
"restore-on-NMI/dot" feature). SRAM: alt ROM is a RAM region of its own (`0C0000`-`0CFFFF`, 16K per ROM: jnext
`altrom_sram_page_`, MAME `bank_update` branch `sram_altrom_en`: `0b000001100 | alt_128_n << 1 | a13`). The visible effect is
only on 0000-3FFF and only on the CPU side (the DivMMC / Multiface / config mode keep priority, MAME lists the order).

## First 20 divergences (NRW / NRR, collapsed, jnext = ref)

Palette (#40/#41/#43/#44), clip (#18-#1B) and raster (#1E/#1F) registers are listed separately (section "Secondary").

| # | where (ref idx) | ref | ours | meaning |
|--:|---|---|---|---|
| 1 | NRR 10 @0189 | `04` (jnext only; ZEsarUX/MAME/ours `00`) | `00` | core ID nibble; not significant |
| 2 | NRR 05 @A7E2, 06 @011E, 0A @6818, 08/05 @0D71 | `41` / `BD` / `01` / `9E` / `71` | `00` | **registers read back as 0**: NR #05/#06/#08/#0A have non-zero reset values (see below) |
| 3 | NRW 05 @A7E9 | `FB` | `FA` | read-modify-write on the value from 2 (bit 0 and 6 lost) |
| 4 | NRW 06 @0122 | `04` | `00` | same, NR #06 |
| 5-9 | NRW 05/08/06/06/0A @0224-0245 | `5B, DE, AF, AC, 11` | `5A, 4E, AB, A8, 10` | the personality's init: each differs by exactly the bits missing from the read-backs (05 bit0, 08 bit7+4... 06 bit 2/4/..., 0A bit0) |
| 10-11 | NRR 54 @27E8, 55 @27F2 | `0B`, `10` | `04`, `05` | MMU slots 4/5 read-back differ (`0B`/`10` vs `04`/`05`); not analysed, probably a consequence of the earlier state difference |
| 12-14 | NRR 18 x4 @0A02, then NRW 18/19/1A @0A07 | `00,FF,00,BF` then `00,FF,00,BF`... | `BF,BF,BF,BF` | **clip windows read return the same byte; the ROM saves and restores them** (index-cycling NR #18-#1B reads missing) |
| 15 | NRR 41/44 @0A31/0A3B | the palette entry, 9-bit (`41`, `44`) | `FF` / `00` | **palette read-back missing** (NR #40 index + #41 read, #44 second byte); the ROM saves and restores the whole palette |
| 16-17 | NRW 8E pairs `03 @3CFC` / `02 @0452` | present | missing 2 | different path through the esxDOS call glue; consequence of the above state (stack / MMU), not independent |
| 18 | NRW 8C `80 @007B` then `00 @007B` | both | only `80` | **the AltROM root cause** |
| 19-20 | NRW 8E `78 @104D`, 07/03/C0/82.. @00EF.. | none | 541 events | the restart: ROM init from `jp 0` |

Everything after block 18 is the restart loop.

## Secondary findings (not the stall, but in the same traces)

* **NR read-backs with reset values** (all three references agree except where noted; hard-reset defaults from the VHDL
  `nr_*` signals, which our board model returns as 0): NR #05 `41`(jnext) / `40`(MAME) / `01`(ZEsarUX), NR #06 `BD`
  (ZEsarUX `9D`), NR #08 `9E` (ZEsarUX `1E`: bit 7 = "port 7FFD not locked", read as 1), NR #0A `01` (MAME `11`).
  These values are what the firmware writes, then the personality ORs/ANDs: the test is to
  read every NR #00-#FF after the boot (`state.txt` in each dump) and diff against jnext - see below.
* **Auto-indexing reads**: NR #18-#1B clip windows (4-byte cycle on the same register), NR #41 palette value /
  #44 second byte (NR #40 index auto-increments after the 9-bit read). The ROM reads the whole palette (256 x 2 reads) before
  any file operation and writes it back.
* **NR #1E/#1F** raster line: the ROM reads `1F` (`27` / `4B` ... in the references, depends on timing; ours `00`).
* **MMU events**: ours reports 31302 slot changes vs 6599 (the loop restarts and #7FFD paging inside the personality).
* **Port traffic** (after `PIN/POUT #EB/#E7` removal) is close: jnext 535461 PIN vs ours 528378 - keyboard half-row scans
  (`#7FFE`, ...) dominate and match; the first PIN divergence is at collapsed index 458751 (the restart).
* **The `min` card cannot boot the firmware on any emulator**: the real `TBBLUE.FW` reads `machines/next/enAltZX.rom`, `48.rom`
  ... and stops with `Error reading file: c:/machines/next/enAltZX.rom` (seen on jnext; ours stalls earlier). The boot card needs
  the full `machines/next/` (the `sn-test-card` has it).

## State dump at frame 1500

jnext `state.txt`: HALT in the NextZXOS Welcome loop, IM 1, IFF on, MMU `FF FF 0A 11 04 05 00 01`. Ours: PC `0168`, IFF off (restart loop). The references show the "Welcome to NextZXOS!" page; ours has none. Dumps: `/Volumes/TB4-4Tb/Projects/emulators/cosim-out/{jnext2,zfull,mfull,ours}`.

## Next steps

1. Implement NR #8C (AltROM) in `NextMemory` / `NextBoard` (the reads of 0000-3FFF from SRAM `0C0000`-based pages when enabled; the `rw`
   bit chooses the write target), with a test that replays the `8C=C0 / fill / 8C=80` pattern. Then re-run
   `NextCosim_Test` and diff again: expect the restart loop to be gone and the next divergence to be the NR read-backs.
2. NR hard-reset values for #05/#06/#08/#0A and the auto-indexing reads of #18-#1B and #40/#41/#44 (palette read), as
   listed above. A cheap check: `diff-traces.py ... ` prints the NR registers differing in the final dump.
3. Run the other two references against ours too (`run-ref.sh zesarux|mame` already produce traces; the same NR #8C
   sequence is in both).
