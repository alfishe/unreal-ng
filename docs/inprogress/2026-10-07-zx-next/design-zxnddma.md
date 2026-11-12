# zxnDDMA: the blitter extension of the Next DMA (plan, 2026-11-11)

Source: the core's guide `docs/zxnDDMA/zxnddma.gde` (SpecNext Ltd, version 1.0, 2026/11/05, "Core 3.03.00, issues 2, 4 and 5")
and its 11 example programs, [tbblue/docs/zxnDDMA](https://gitlab.com/thesmog358/tbblue/-/tree/master/docs/zxnDDMA). The
guide documents the RTL's behaviour (`cores/zxnext/src/device/new_dma.vhd`, `zxnext.vhd`); it was checked on boards by the
authors (issue 2 and 5, 2026-10-20/21). Our RTL collection (`zx-next/fpga`) does not hold `new_dma.vhd` yet: **step 0 is to
fetch it**, because the project rule is "where the RTL and a document disagree, the RTL wins" (audit-dma-timing-2026-10-09.md).

## 1. What it is

A superset of the zxnDMA on port `$6B` ("DMA+", "the Next's blitter"). Port `$0B` (Z80-DMA mode) is unchanged and ignores
the new commands. Everything that worked keeps working. The new commands are the range `$D7..$FB` of WR6-like bytes, with
follow bytes; **a core without them reads the follow bytes as ordinary register writes**, so software probes first.

| Command | Follow bytes | Meaning |
|:--|:--|:--|
| `$D7` / `$DB` | nn | Port A / Port B address bits 23:16 (effective `{nn[5:0], addr16}`; bits 7:6 dropped) |
| `$DF` | mm [Xlo Xhi Y] | mode flags: b0 physical (22-bit SRAM, no MMU), b1 2D, b2 transparent, b3 fill, b4 Layer 2 X,Y (3 bytes follow; needs b0), b5 hidden screen (`$13` instead of `$12`; with b4) |
| `$E3` / `$E7` | ll hh | 2D row length (bytes) / number of rows |
| `$EB` / `$EF` | ll hh | Port A / Port B stride (start of one row to the start of the next) |
| `$F3` | nn | transparent value: a source byte equal to it is not written (addresses still advance) |
| `$F7` | nn | fill value (no source read) |
| `$FB` | - | obsolete (copy hidden to shown screen), ignored |

Layer 2 X,Y destination (`$DF` b4): base = `$40000 + bank*$4000` (bank from NR `$12`, or `$13` with b5); by NR `$70` bits 5:4:
`00` 256x192 `base+Y*256+X`; `01`/`11` 320x256 `base+X*256+Y`; `10` 640x256 `base+X*256+Y` (X is the byte column). Port B stride
becomes 256 and stays until `$EF` or `$C3`. In mode `00` only X_lo is added.

Reset (`$C3`) also clears: physical/2D/transparent/fill/hidden flags, `$D7`/`$DB` bytes, row length, rows, strides, the
transparent colour and the fill value, plus what it cleared before; it keeps the WR0/WR4 addresses, direction, port types,
address modes, transfer mode, block length and read mask. `$CF` (load) copies WR0/WR4 addresses **and** the `$D7`/`$DB`
bytes to the working addresses. In physical mode an address carries/borrows into the extended byte at `$FFFF`/`$0000`;
outside it, it wraps in 64K. 2D ignores the WR0 block length.

Other facts the model must reproduce: continuous vs burst unchanged; timing bytes 4/3/2/4 cycles (`$C3` -> 3+3, a byte takes
6; 2+2 takes 4); fill is twice as fast as a fixed source address; prescaler rate "875 kHz / N at every CPU speed";
`$B3` (force ready) a NOP; WR4 with bit 4 and bits 3:2 = 00 hangs the decoder until reset (we already model `Stuck`);
**hardware IM2 + an NR `$CC-$CE` source pending + DI = a started transfer never runs** (we already model `DmaDelay`,
check the DI case); physical range by board: issue 5 `$000000-$3FFFFF`, issue 4 `$000000-$1FFFFF` (bit 21 dropped, wraps),
issue 2 1 MB (2 MB with the expansion); banks 5 and 7 are block RAM, a physical write to their SRAM addresses does not reach
the ULA screen; Layer 2's NextZXOS default is bank 9, the hardware default 8.

## 2. What we have

`core/src/emulator/io/z80n/nextdma.{h,cpp}` (359 lines) follows `dma.vhd` core 3.02.03: a byte sequencer (WR0-WR6), the
read sequence, auto-restart, prescaler, per-byte history, `Stuck`, `DmaDelay`, port-cycle timing and contention through
`Bus{readMemory, writeMemory, readIo, writeIo, advance, contend}` with **16-bit addresses only**
(`portdecoder_next.cpp:1172`, `DirectRead/WriteToZ80Memory`). Tests: `core/tests/emulator/io/z80n/nextdma_test.cpp`.
The state codec (`nextstatecodec.cpp`), the device histories (`nexthistory`) and the TTD device keys know the current DMA state.

Gaps for the extension: no physical (22-bit) memory access path from the DMA; no 2D/stride/transparent/fill/Layer 2 engine;
no core-version switch (we emulate 3.02.03; the guide says "every core > 3.02.16 has it", the floor is stated inconsistently:
3.02.16, 3.02.21 and 3.03.00 all appear); the status byte layout of `$BF` is documented only by our `StatusByte()` (RTL-derived).

## 3. Decisions to take

1. **Core version.** Add a core-version setting (or a machine option `zxnddma`) for the Next: off = 3.02.03 behaviour (the new
   commands are dropped and their follow bytes become register writes, exactly as the guide says), on = zxnDDMA. Default on for
   new machines once the RTL is read; old snapshots and recordings keep their value (state codec version bump).
2. **Fetch and read `new_dma.vhd`** (and the diff of `zxnext.vhd`) before coding the engine; fix this document where the
   RTL differs. Items the guide leaves open: status byte layout, WR5 auto-restart with 2D/Layer 2 state, whether any line mode
   exists, exact version floor, behaviour when `$DF` b4 is set without b0.
3. **Timing model.** The first version reuses the current per-byte cycle model (read + write cycles of the port timing bytes,
   contention, `DmaDelay`): a 2D/fill/transparent byte costs the same as a copied byte, fill reads nothing (one cycle set less,
   "twice as fast"). Acceptance numbers are the guide's measurements (28 MHz, issue 5): 32 16x16 balls, zxnDDMA opaque and
   transparent 2.1 ms, zxnDMA per column 8.2 ms, CPU LDIR 7.2 ms (see section 5).

## 4. Phases

| # | Phase | Content | Done when |
|:--|:--|:--|:--|
| 0 | RTL (checked 2026-11-11: the official repo `SpectrumNext/ZX_Spectrum_Next_FPGA`, branches `master`, `30204`, `30205` hold no `new_dma.vhd`; their `dma.vhd` is byte-identical to our 3.02.03 copy, so the RTL of core 3.03.00 is not public and the guide is the only source) | fetch `new_dma.vhd`, `zxnext.vhd` of the core 3.03.00 into the RTL collection (`zx-next/fpga`, checksums), read, list differences from this document | this document corrected; open questions closed or marked |
| 1 | Physical memory path | `Bus` gains `readPhysical(uint32_t)` / `writePhysical`; Next memory exposes the 4 MB SRAM (issue-dependent wrap: bit 21 on issue 4, 20 on issue 2 without the expansion), banks 5/7 as BRAM (writes do not reach the ULA screen) | unit tests: bank addresses `$40000+bank*$4000`, wrap per issue, bank 5/7 |
| 2 | Command decode | `$D7 $DB $DF $E3 $E7 $EB $EF $F3 $F7 $FB` + follow bytes in the sequencer; port `$0B` ignores them (follow bytes then fall through as registers); `$C3`/`$CF` extended; state in the codec (version bump), the history entry and the status/report (`portdecoder_next_reports.cpp`) | tests: every command's follow bytes on `$6B` and the drop on `$0B` and on 3.02.03 mode; reset/load tables of the guide 2.6 |
| 3 | Engine | physical addressing with carry; 2D rows with strides; transparent; fill; Layer 2 X,Y destination from NR `$12/$13/$70` for the three modes; hidden screen; fixed-address and burst/prescaler interplay; WR5 restart | unit tests from the guide's formulas (all three Layer 2 modes, clipping edge cases, row length/rows 0 = one byte) |
| 4 | Timing | per-byte cost with fill discount, 2+2 cycle bytes, contention as in the audit; IM2 hold under DI | pinned cases: the five numbers of zxnddma_compare within the existing timing tolerance |
| 5 | Examples as tests | build the 11 examples with sjasmplus into `testdata`/the verification folder, run headless, compare with a CPU model (LDIR/loops) frame by frame for the deterministic ones (examples, sprites, clip, lemmings, modes, scroll, windows, scroller); `zxnddma_compare` run in both modes (on = detects, off = takes the plain path with `FORCE_PLAIN`) | tools/verification/next-ddma + a gtest that runs the compare program and checks the detection byte |
| 6 | Automation and docs | WebAPI/MCP/CLI/Python/Lua status of the new DMA state per the automation-planes rule; DMA history shows flags and rectangle; docs chain (`command-interface.md` -> Next module doc -> plane docs), `.recipe` for Next DMA | automation-gaps list updated |
| 7 | All systems | gcc, MSVC, MinGW (batched, memory notes) | green |

## 5. Test material

- The guide's tables (sections 2.2-2.9) are the unit-test oracle for phase 2-3; the Layer 2 formulas for X,Y in all three modes.
- Examples 3.1-3.11 ship in the repo folder `docs/zxnDDMA/examples` (CC BY-NC-SA: keep them out of `testdata` or note the licence;
  the project has no licence-check rule for test data, only attribution in a README next to them).
- `zxnddma_compare` doubles as the **detection test**: it sends `$83 $C3 $D7 04 $DB 0B $DF 01 ...` and then copies a byte;
  a plain DMA writes `$5A` at the target, a zxnDDMA does not.
- `lem_walk.png` (lemmings sprite source) and the `font10.fnt` of the scroller are not in the folder: the two examples
  that use them need the data re-created or skipped.
- Real boards: none here; the guide's numbers from an issue 5 are the only hardware reference, so timing acceptance is
  "arithmetic until a board or the RTL (GHDL, POC 036 method) says otherwise".

## 6. Risks

- The guide is dated 2026-11-05 and the RTL may still move (the version floor is stated three ways): isolate the extension
  behind the core-version option and keep the commands' table data-driven.
- Memory model: 22-bit physical access must respect ROM protection? The guide says physical mode bypasses the MMU; the
  existing audit says ordinary DMA writes honour ROM protection/Layer 2 mapping because the DMA drives the CPU bus - physical
  mode must not (check in the RTL).
- TTD/snapshot compatibility: the DMA state grows; the codec and the device key (`PeripheralId`, frozen at 65) must carry it
  without a new device key.
- Contention: examples draw while the ULA fetches; the per-byte contention path must stay identical to the current one.
- Parallel work: the Next performance session edits the same module area (scratch/wt-next-perf); coordinate before phase 1
  (the memory accessors) and keep the engine in its own file (`nextddma.{h,cpp}`) called from `NextDma`.

## 7. Suggested order and size

Phase 0 (a day of reading), 1-3 together (the core of the work, one branch, a few hundred lines plus tests), 4 and 5 next,
6 with the automation planes, 7 batched. The 2D/Layer 2 engine is data-parallel and free of timing: it can be written and
tested before the timing phase.

## 8. Timing status (2026-11-11, first version in the branch `zxnddma`)

`zxnddma_compare` at 28 MHz, ours against the guide (issue 5), 32 balls of 16x16, measured in video lines of 64 us:

| Way | Ours | Guide | Note |
|:--|:--|:--|:--|
| 1 CPU, LDIR | 8.3 ms | 7.2 ms | CPU part, +15% |
| 2 CPU, skip `$E3` | 18.9 ms | 15.6 ms | CPU part, +21% |
| 3 zxnDMA, per column | 9.3 ms | 8.2 ms | mostly CPU code (OTIR set-up), +1.1 ms like way 1 (9.7 with the DMA SRAM wait on) |
| 4/5 zxnDDMA, opaque / transparent | 2.2 ms | 2.1 ms | the new engine, +5% (2.5 ms with the DMA SRAM wait on) |

- The new engine's own timing is inside the 64 us measuring step. The CPU ways are slower than the board: our LDIR costs 24.0 clocks
  a byte at 28 MHz (21 + one SRAM read wait for each of its three reads: two opcode bytes and `(HL)`; measured on 2026-11-11), while the
  guide's 7.2 ms, after the program's own loop overhead (about 2.6 clocks a byte), says the board's LDIR costs 21-22. The board seems not
  to pay the three waits.
- The rule we have is `zxnext.vhd` (core 3.02.03) 3171-3181: `sram_wait_n` is low for every read cycle that reaches the SRAM at 28 MHz
  (`cpu_rd_n` is the DMA's own read while it holds the bus, `dma_wait_n` includes `sram_wait_n`, lines 1830 and 1844). The commented-out
  line above it limits the wait to M1 and DMA reads. Core 3.03.00 may have changed it: its RTL is not public (phase 0). Pinned by
  `NextSkeleton_Test.At28MHzSramReadsWaitOneClockAndBank7DoesNot` and the RTL timing cases, so it stays as it is.
- **Switched OFF (2026-11-11, owner decision):** the DMA's SRAM read wait at 28 MHz (memory and physical accesses, not the bank 7 BRAM;
  `zxnext.vhd` 3.02.03) was implemented and measured, then disabled: `kDmaSramWait = false` in `BindDma()` (`portdecoder_next.cpp`),
  one constant to set back. With it way 4/5 took 2.5 ms (+0.3 ms: one clock for each of the 8192 bytes), way 3 9.7 ms, against the
  guide's 2.1 / 8.2 ms; without it 2.2 / 9.3 ms (way 1/2 are CPU and do not change: 8.3 / 18.9 ms). The guide's numbers fit a core where
  the DMA does not pay that clock. Open until the 3.03.00 RTL or a board measurement says which is right. Physical-mode accesses skip
  `contend` (the video logic decodes contention from the CPU address, which a physical access does not carry).
- Fill mode costs one write cycle set a byte (the guide: twice as fast as a fixed source); not measured against a number.
- Not checked: the IM2 + DI hold, 2D with auto restart, the burst/prescaler interplay with 2D.
