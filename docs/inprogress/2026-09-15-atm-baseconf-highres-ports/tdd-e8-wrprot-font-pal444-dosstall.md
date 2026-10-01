# E8: write protect, font RAM, 4:4:4 palette, DOS-entry clock stall — technical design

| | |
|---|---|
| **Date** | 2026-10-01 |
| **Status** | Implemented 2026-10-01 (§8 verification, §9 what landed). Not committed |
| **Closes** | gaps **P-5**, **C-6**, **C-7** of [gap-analysis.md](gap-analysis.md), and the new gap **C-10** (this document). **A-7** (raster selection) is analysed in §7 and stays open |
| **Does not close** | **C-5** flash writes and **C-8** ULA+ (rest of plan phase E8; separate designs, [tdd-evo-control-and-avr.md](tdd-evo-control-and-avr.md) §5.2, §5.4) |
| **Hardware source** | `pentevo/fpga/base_trdemu/trunk`: `mem/atm_pager.v`, `video/video_palframe.v`, `video/video_render.v`, `z80/zports.v`, `z80/zclock.v` |
| **Cross-check** | xpeccy-plus `src/libxpeccy/hardware/pentevo.c` (`evo_wprot`, `evoMWr`, `evoOutFF`, the `#3Dxx` hold) |

## 1. Where this comes from: the claims, checked

A comparison of our `ATM3` against xpeccy-plus listed five things xpeccy-plus does and unreal-ng does not.
Each one was checked against the FPGA RTL (the ground truth) and against both code bases.

| # | Claim | Verdict | Evidence |
|---|---|---|---|
| 1 | xpeccy-plus has the 12-bit palette (`pal444`), unreal-ng has only the 2-bits-per-channel scheme | **True** | RTL: `#BF`.5 → `pal444_ena`; the low bit of each channel then comes from the port's high address byte (`zports.v:916-917`, `video_palframe.v` palette write). xpeccy-plus `evoOutFF` does it. unreal-ng `Port_ATM_Palette_Out` always substitutes the data byte for the high byte |
| 2 | xpeccy-plus has write protect `#xBF7`, unreal-ng does not | **True** | RTL `atm_pager.v:176-190` (`wrdisables`, one bit per window per map), applied at `:141`. xpeccy-plus `evo_wprot`. unreal-ng: `#xBF7` is logged and ignored, `#12BD` reads 0 |
| 3 | xpeccy-plus has font RAM, unreal-ng does not | **True** | RTL `fnt_wr = fntw_en_reg && mem_wr_fclk` (`zports.v:950`), 2 KB `video_fontrom`, `#0EBD` = `fontrom_readback`. xpeccy-plus `vid_fnt_wr`. unreal-ng: renderer reads the constant `ATM_FONT`, `#0EBD` returns `#FF` |
| 4 | xpeccy-plus holds the clock on a `#3Dxx` fetch of the TR-DOS ROM, unreal-ng does not | **True, and not tracked before** | RTL `atm_pager.v:259-282` (`zclk_stall`, 4 fclk), `zclock.v:196` (stall freezes the divider). xpeccy-plus adds the hold in `evoMRd`. unreal-ng has no such wait. New gap **C-10** |
| 5 | xpeccy-plus has an honest 320-line raster, 71 680 T against our 69 888 T | **Half true** | 71 680 T = 320 × 224 is the **Pentagon raster**, one of **four** the AVR selects (`modes_register` bits 5:4: Pentagon / 60 Hz / 48K 69 888 / 128K 70 908). xpeccy-plus implements only that one. unreal-ng runs the 48K raster on purpose (finding 4 of the [README](README.md); decision Q1 of the gap analysis). The real gap is that nothing selects between them: **A-7**, analysed in §7 |

The remaining statements of the comparison (shadow ports, NMI/Magic, virtual TR-DOS, `#xxBD` readback, INT
ack) were not re-derived here: phases E0-E4 already compared them against the RTL.

One nuance that matters for items 1 and 3 of the claim list: the FPGA does **not** drive a 4-bit DAC. The palette
RAM holds 12 bits, but `video_palframe_mk3bit` dithers each 4-bit channel over x/y/frame phase down to the 2-bit
VGA DAC. xpeccy-plus and this design show the mean level (`nibble * 0x11`); the dither itself is not emulated
(§3.3).

## 2. Decisions

| ID | Decision | Why |
|---|---|---|
| D1 | Write-protect state is **one byte, bit i = window i of map 0, bit 4+i = window i of map 1**, the order `#12BD` reads back (`rd_wrdisables`) | The hardware keeps one flop per window per map; the readback is the table's own order |
| D2 | A protected **RAM** window keeps its read pointer and gets the **trash page as write pointer** (`Memory::SetBankWriteProtected`). No new check in the write path | ROM windows already work this way; the hot write path stays untouched and costs nothing for other machines |
| D3 | Window 0 is **never** protected while it shows the NMI page, the virtual TR-DOS page or the `#EFF7`.3 RAM 0 (RTL `:126`: `wrdisable <= trdemu_wr_disable`, not `wrdisables`). The one-instruction `trdemu_wr_disable` window is already covered by the order in `BeforeMachineM1` (E4) | Exactly the RTL branch |
| D4 | With the pager off (`#xx77` A8 = 0) nothing is protected (RTL `:121`) | Exactly the RTL branch |
| D5 | Font RAM is **2048 bytes in `EmulatorState`** (`atmFontRam`), address layout **`code * 8 + row`** (RTL read address `{pixbyte, typos}`), initialised from `ATM_FONT` (stored `row * 256 + code`) at construction. The renderer always reads it, for ATM710 and ATM3 alike | One render path. ATM710 has no writer, so its picture is unchanged. The layouts differ: copying `ATM_FONT` as is would scramble every glyph |
| D6 | The font capture is a **write-only bus overlay** over the whole address space, installed only while `#BF`.2 = 1 (`HostBusOverlay`, `observesReads = false`) | Zero cost while off; RTL `mem_wr_fclk` is any memory write, ROM windows and protected windows included, and the normal write still happens |
| D7 | The font survives a Z80 reset (the FPGA's `altdpram` is not reset) and is restored by TTD in its **own blob** `EvoFontRam` (id 22) | Software loads a font once and expects it to stay; a separate blob leaves the 136-byte `AtmPaging` blob alone |
| D8 | `pal444` is not stored apart: it is `pBF`.5, already in the TTD blob. The 12-bit colors live in `atmPalette` as 4 bits per channel (`nibble * 0x11`) and `#0DBD` is derived from them | No new state; `atmPaletteRegs` stays for ATM710 and old recordings |
| D9 | The DOS-entry stall is **128 counter ticks** (`AddWaitTicks`; 256 ticks = one 3.5 MHz T): 4 fclk of the 28 MHz clock, exact at 14 MHz (2 clocks), 7 MHz (1 clock) and **3.5 MHz (half a clock)** | The RTL says "minimum 3 clocks @ 28 MHz" and gives 4 (`dos_turn_on` + 3 counter states). xpeccy-plus rounds to whole clocks; the tick counter lets us keep the RTL number. Applies only while the `contention` feature is on, like every machine wait |
| D10 | The stall is applied from the **TR-DOS session checks the Z80 instruction start already makes** (`z80.cpp`: the `CF_SETDOSROM` entry check and, while the session is on, the `CF_LEAVEDOSRAM` branch), through a new `PortDecoder::OnDosRomFetch(pc)` called only for a `#3Dxx` fetch in those states | The first design attached the **machine M1 hook** while the stall could fire. That state is the normal one (48K ROM selected, DOS ROM mapped in map 1): measured, the hook cost ATM3 about 19% of its speed (§8). The session checks run in exactly the states where the stall exists and cost nothing elsewhere |

## 3. Design

### 3.1 Write protect (P-5)

State: `EmulatorState::evoWrProt` (D1), saved in the existing `AtmPagingState` (one of its two `reserved` bytes;
the struct size stays 136, old recordings read 0 = nothing protected). Reset clears it (RTL reset `wrdisables <= 0`).

Port `#xBF7` (shadow, A8 = 1, A11:A10 = `10`, window = A15:A14): `evoWrProt[regSet + window] = value & 1`, with
`regSet` the map `#7FFD`.4 selects now (RTL `wrdisables[pent1m_ROM]`), then `UpdateZ80Banks()`.

`updateMemoryBanks` (after the window loop and the window-0 overrides): for each window that maps **RAM** and whose
protect bit is set, and which is not one of the D3/D4 exceptions, call `Memory::SetBankWriteProtected(bank)`.
`#12BD` returns `evoWrProt` as is.

`Memory::SetBankWriteProtected(bank)`: `_bank_write[bank] = trash`, `_bank_ram_page_cache[bank] = kPhysPageNone`
(the TTD must not journal the discarded writes as writes to the page), bank mode unchanged (it is still RAM: the
TR-DOS session logic reads `_bank_mode[0]`).

### 3.2 Font RAM (C-6)

`state.atmFontRam[2048]`, `code * 8 + row`. `#BF`.2 edge: install or remove `EvoFontOverlay` (`Core::AddBusOverlay`,
same install/remove pattern as `EvoTurboOverlay`, `SyncTurboWaits`); its `onWrite(addr, value)` does
`atmFontRam[addr & 0x7FF] = value`. The text renderers (`M_ATMTX`, `M_ATMTL` in `screenatm.cpp`) read
`atmFontRam[code * 8 + (screenY % 8)]`.

`#0EBD` returns `state.atmFontByte`: the glyph byte the text renderer fetched last (RTL `symbyte` is a registered
RAM output that holds between fetches). It starts at `#FF` and is only updated by the text modes. In graphic modes
the hardware reads `font[screenByte * 8 + row]`, a value nobody uses; it is not emulated.

TTD: new `PeripheralId::EvoFontRam = 22`, blob = version byte + 2048 bytes + the fetched byte; the overlay is
re-installed from `pBF` on restore (same path as the turbo overlay).

### 3.3 4:4:4 palette (C-7)

`Port_ATM_Palette_Out` for ATM3: the low bit of each channel comes from the real port high byte, inverted, when
`pBF`.5 = 1; otherwise from the data byte (today's behaviour, which is the RTL's `pal444_ena = 0` branch).
Per channel, with `d = ~data`, `a = ~(port >> 8)` and the channel's two data bits `H, L` (blue d0/d5, red d1/d6,
green d4/d7) and address bits `AH, AL` (blue A8/A13, red A9/A14, green A12/A15):

```
nibble = (H << 3) | (L << 2) | (pal444 ? (AH << 1) | AL : (H << 1) | L)
```

ATM710 keeps its own, unchanged function (it has no `#BF`).

`#0DBD` readback: the displayed cell's 12-bit color, as the pairs `{g, r, b}`: with `pal444` the **low** pair of each
channel, without it the **high** pair (RTL `palcolor`), laid out `{~g1, ~r1, ~b1, ~g0, 1, 1, ~r0, ~b0}` as today.
Software that wants all 12 bits reads twice with `#BF`.5 toggled.

The dither of `video_palframe_mk3bit` is not emulated: the frame buffer gets `nibble * 0x11`, which is the
long-run average the monitor shows. Recorded as an accepted deviation (PAL-D1).

### 3.4 DOS-entry clock stall (C-10)

RTL condition (`dos_exec_stb`, M1 with MREQ, window `w`, `A13:A8 = #3D`): the current map is 1 (`#7FFD`.4), and
window `w`'s **map-1** register is ROM with `dos7ffd` set (`pFFF7[4 + w] & 0x300 == 0x100`). It fires on **every**
such fetch, whether DOS is already on or not.

`PortDecoder_ATM3::OnDosRomFetch(pc)`: when the condition holds and the `contention` feature is on,
`Z80::AddWaitTicks(128)` (D9). The Z80 calls it (D10) from the entry check (`CF_SETDOSROM`, the first `#3Dxx` fetch,
just before `CF_TRDOS` is set) and from the in-DOS branch (`CF_LEAVEDOSRAM`, which ATM3 uses). Both run at the
start of the instruction whose opcode fetch is the stalled one.

Known limit: the entry check exists only while the emulator thinks TR-DOS is available (`trdos_present` and a DOS
ROM loaded); with no DOS ROM the real chip would still stall a `#3Dxx` fetch and this model does not.

## 4. State and TTD

| State | Where | Blob |
|---|---|---|
| `evoWrProt` | `EmulatorState`, `AtmPagingState.reserved[0]` → `evoWrProt` | `AtmPaging` (size unchanged) |
| `atmFontRam[2048]`, `atmFontByte` | `EmulatorState` | new `EvoFontRam` (22) |
| `pBF`.2, `pBF`.5 | exists | `AtmPaging` |
| 4:4:4 colors | `atmPalette` (exists) | `AtmPaging` |
| stall | none (a timing rule) | — |

Existing recordings: they hold no `EvoFontRam` blob and `evoWrProt = 0`; the fixtures need no re-record. A
recording that uses the new features restores them.

## 5. Tests (written first)

| ID | Test | Expect |
|---|---|---|
| WP-1 | `#BF7` writes in shadow, all four windows, both maps | `#12BD` reads the byte in D1 order; only the active map's bit changes |
| WP-2 | protect RAM window 2, write `#8000` | the write is dropped, a read returns the old byte; clear the bit: the write lands |
| WP-3 | protect window 0 while `#EFF7`.3 is set, and with the NMI page in | window 0 still writable (D3) |
| WP-4 | pager off | nothing protected (D4) |
| WP-5 | protect + TTD journal | a dropped write is not journaled; seek back and forth keeps the byte |
| WP-6 | `#BF7` write outside shadow | no effect |
| WP-7 | reset | all bits clear |
| FNT-1 | `#BF`.2 = 1, write `#4000 + n` | `atmFontRam[n]` changes, the RAM byte too |
| FNT-2 | `#BF`.2 = 1, write to a ROM window and to a protected window | the font byte changes (D6) |
| FNT-3 | `#BF`.2 = 0 | no change; overlay not installed |
| FNT-4 | power-on font | `atmFontRam[c * 8 + r] == ATM_FONT[r * 256 + c]` for all 2048 |
| FNT-5 | text mode, load a glyph through `#BF`.2, render a frame | the pixels show the new glyph |
| FNT-6 | `#0EBD` | `#FF` before any text frame, then the last glyph byte the renderer fetched |
| FNT-7 | TTD seek across a font write | the font and the overlay state restore |
| FNT-8 | Z80 reset | the loaded font stays |
| PAL-1 | `pal444` = 1, `OUT (C),A` with port `#xxFF` and a chosen high byte | the four bits per channel follow the formula of §3.3; the bits of the DD case are unchanged |
| PAL-2 | `pal444` = 0 | byte-identical to today's results for all 256 data bytes |
| PAL-3 | `#0DBD` with `pal444` on and off | the low / high pair of the displayed cell |
| PAL-4 | ATM710 palette | unchanged (regression) |
| STALL-1 | fetch `#3D00` with map 1, window 0 = ROM+`dos7ffd`, `#7FFD`.4 = 1, `contention` on | exactly +128 ticks over the same fetch elsewhere |
| STALL-2 | the same at 3.5, 7 and 14 MHz | +128 ticks each (half, one, two clocks) |
| STALL-3 | fetch `#3Dxx` in window 1 whose map-1 register is ROM+`dos7ffd`; a window that is RAM; `#7FFD`.4 = 0; `contention` off | stall only in the first case |
| STALL-4 | fetch `#3C00` and `#3E00` | no stall |
| STALL-5 | cost | the M1 hook is not attached by the stall: `machineM1Hook` stays null in the default booted state (§8 A/B) |
| STALL-6 | the entering fetch (`CF_SETDOSROM`, DOS off) | +128 ticks, and TR-DOS is on afterwards |

## 6. Cross-platform and performance notes

- No platform code; new files are plain C++17 (`evofontoverlay.{h,cpp}`, `ttdevofontram.{h,cpp}`), no underscores in names.
- No new check on any machine's hot path: D2 and D6 use pointers and an overlay that exist only on ATM3 in the
  states that need them. D10 adds one compare of the fetch address to the two TR-DOS session branches of the Z80
  instruction start, which run only while a session is being entered or is on; §8 records the A/B result.
- The mingw syntax check (`x86_64-w64-mingw32-g++ -fsyntax-only`) is run on the new files.

## 7. A-7: raster selection (analysed, not implemented here)

The AVR `modes_register` (type 3, bits 5:4) picks the raster: `00` Pentagon (71 680 T, 320 lines, no contention,
INT early), `01` 60 Hz, `10` 48K (69 888 T, contention), `11` 128K (70 908 T). unreal-ng fixes the 48K raster:
the frame length lives in the emulator configuration the core reads once, ATM3 forces the `M_ZX48` descriptor at
three timing sites in `ScreenZX`, and `EvoAvr` already stores bits 5:4 but nothing reads them.

What a faithful selection needs, none of it small: (1) a runtime change of frame length and INT position through
the Core (today a restart-level setting), (2) the 60 Hz line count and frame period reaching the frame pacer and
the audio ring (see the A/V pacing notes), (3) contention off for Pentagon, (4) the renderer descriptors per raster
including the AlCo/HWMC descriptors that carry Pentagon timing today, (5) TTD: the raster is chipset state, so a
seek must restore it, and the fixture corpus is re-recorded when the timing format changes.

It is a separate design (E9-sized, plan row A-7) and is **not** folded into this change, because doing it halfway
(hard-wiring 71 680 T as xpeccy-plus does) would break every 48K-raster program and decision Q1. Proposed next
step: its own TDD, default raster stays 48K.

## 8. Verification log

2026-10-01, `cmake-build-agent-release`, `-j 10`.

- **Build:** full `ninja` and `core-tests`, zero compiler warnings (the linker's "built for newer macOS" notes are not
  compiler output). The changed and new C++ passes `x86_64-w64-mingw32-g++ -std=c++20 -Wall -Wextra -Werror -fsyntax-only`.
- **Tests:** `test-parallel` over 20 shards: 5594 passed, 0 failed, 35 skipped (the skips are the suite's existing
  ones).
- **Golden run (`CoreGolden`):** only the ATM3 row moved. Cause isolated by switching each feature off in turn:
  without the stall the old row returns exactly, without the write protect it does not change. Re-recorded in
  `core_golden_test.cpp` with the reason beside the row.
- **TTD CI gate** (`testdata/ttd/bench/v1-ci-gate.txt`): three exact ATM3/idle rows moved, all by the new 2050-byte
  `EvoFontRam` blob (device blobs 1177.7 -> 2208.2 bytes per frame, file 7365.4 -> 8400.8 bytes per frame); updated
  in the file with a dated header line. A fixed-size blob is carried at every capture; a variable "only when the
  font differs" encoding would break the fixed-size rule of `TTDSerializable`, and PLAN #40's device RAM regions are
  the place for it later.
- **A/B of the stall (D10):** 150 frames of the booted ERS menu, 8 rounds each, interleaved, machine under heavy
  load (about 90), so absolute times are noise and only the ratio counts.

  | Variant | min ms | median ms |
  |---|---:|---:|
  | first design: stall through the machine M1 hook, hook attached (map-1 DOS ROM present) | 141.0 | 149.5 |
  | same state, hook not attached | 118.4 | 124.8 |
  | final design: stall in the Z80 session checks, `machineM1Hook` null | 118.8 | 127.6 |
  | same state, DOS-ROM dos7ffd bit cleared (nothing armed) | 119.6 | 127.8 |

  The hook cost about 19%; the final design is equal within noise. The hook-based version was dropped for it.
- **Not measured:** an A/B of the whole change on a quiet machine (the load made absolute numbers meaningless). The
  other hot-path additions are nil (D2, D6) or one compare inside TR-DOS session branches (D10).

## 9. What landed

| Item | Where |
|---|---|
| **P-5** write protect | `EmulatorState::evoWrProt`; `PortDecoder_ATM3::Port_BF7_Out`, `IsWindowWriteProtected`, protect pass at the end of `updateMemoryBanks`; `Memory::SetBankWriteProtected` (write pointer to the trash page, `GetRAMPageForBank*` still answer the mapped page, `DirectWriteToZ80Memory` pokes through); `#12BD`; `AtmPaging` blob field (the old reserved byte, size 136 unchanged) |
| **C-6** font RAM | `EmulatorState::atmFontRam` / `atmFontByte` (`InitAtmFont`: transposed built-in table); `EvoFontOverlay` (`evofontoverlay.h`) installed by `SyncFontOverlay` while `#BF`.2 is set (also on the legacy FPGA tree, which has the bit); `ScreenAtm` text renderers read the RAM and leave the fetched byte; `#0EBD`; video-debug `MemView` reads the RAM; `EvoFontRam` TTD blob (id 22, `ttd.ksy` and the contract test updated) |
| **C-7** 4:4:4 palette | `PortDecoder_ATM710::PaletteLowBitsFromAddress` (virtual, ATM3: `#BF`.5 and the current tree); `#0DBD` derived from the displayed color's nibbles |
| **C-10** DOS-entry stall | `PortDecoder::OnDosRomFetch` (new virtual, default empty), called from the two TR-DOS session branches of the Z80 instruction start; `PortDecoder_ATM3::OnDosRomFetch` adds 128 ticks |
| Tests | `portdecoder_atm3_test.cpp` (WP-1/2/3/6/7, PAL-1/2/3, STALL-1/2/3/4/6), `evofontoverlay_test.cpp` (FNT-1/2/3/4/6/7/8), `atm_video_modes_suite_test.cpp` (FNT-5), `ttdatmpaging_test.cpp` (the write protect byte round trip and hash) |

Deviations from the test table of §5, recorded:

- WP-4 (pager off) and WP-5 (TTD journal of a dropped write) have no test of their own. WP-5 rests on the page
  cache being cleared, as for ROM, and on WP-2's page query; a TTD seek test across a dropped write is a good
  addition when the TTD harness for ATM3 is next touched.
- FNT-7 tests the blob round trip, not a seek across a font write.
- The text renderer's `#0EBD` byte is exact per fetch, the port read does not wait for the beam: a polling loop sees
  the last fetched glyph byte.
- Open, unchanged: **A-7** raster selection (§7), **C-5** flash writes, **C-8** ULA+. The dither of the FPGA's 4:4:4 output
  (PAL-D1) is not emulated.
