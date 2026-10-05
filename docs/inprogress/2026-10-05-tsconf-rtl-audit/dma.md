# TS-Conf audit: DMA and the DRAM arbiter

Audit of the unreal-ng TS-Conf DMA engine and DRAM-cycle sharing (video, TSU, CPU, DMA) against two
references. Every row was checked against both.

- **[V]** RTL (authoritative): zx-evo repo, paths relative to `pentevo/fpga/current/`. The emulated
  build is `quartus` (`IDE_HDD`, no `XTR_FEAT`, no `FDR`, no `COPPER`); the VDAC builds
  (`quartus_vdac`, `quartus_vdac2`) add `XTR_FEAT` and drop `IDE_HDD`.
- **[U]** TS-Labs Unreal Speccy: zx-evo-unreal repo, paths relative to `Unreal/`.
- **[N]** unreal-ng: paths relative to the repo root, master `cf3adb714`. Short forms:
  `dma.cpp` = `core/src/emulator/platforms/tsconf/tsconfdma.cpp`, `engine.cpp` = `.../tsconf/tsconfengine.cpp`,
  `arbiter.cpp` = `.../tsconf/tsconfarbiter.cpp`, `ints.cpp` = `.../tsconf/tsconfinterrupts.cpp`,
  `pd.cpp` = `core/src/emulator/ports/models/portdecoder_tsconf.cpp`,
  `tsmem.cpp` = `core/src/emulator/memory/tsconf/tsconfmemory.cpp`.
- Tests are in `core/tests/emulator/machines/tsconf/` (`tsconfdma_test.cpp` unless another file is named).

Units: one DRAM cycle = 4 fclk (28 MHz) = one 7 MHz dot; 448 per 224-tact line.

Verdicts: `match`, `ng-matches-RTL, Unreal differs`, `ng-matches-Unreal, RTL differs` (unreal-ng bug),
`ng differs from both` (bug), `unclear`.

## Table

| # | Behavior | RTL [V] | TS-Labs Unreal [U] | unreal-ng [N] | Test | Verdict |
|:--|:--|:--|:--|:--|:--|:--|
| 1 | Register map: DMAS_AL/AH/AX = 0x1A-0x1C, DMAD_AL/AH/AX = 0x1D-0x1F, DMA_LEN 0x26, DMA_CTRL 0x27 (write), DMA_NUM 0x28, DMA_STATUS 0x27 (read) | `z80/zports.v:198-213,220,501-509` | `tsconf.h:29-43,60` | `core/src/emulator/platforms/tsconf/tsconfstate.h:35-49` | DMA1, DMA8 | match |
| 2 | Address = 21-bit word address: AL `zdata[7:1]` -> bits 6:0 (A0 ignored), AH `zdata[5:0]` -> bits 12:7, AX `zdata[7:0]` -> bits 20:13; byte addr = `AX<<14 \| (AH&0x3F)<<8 \| AL&0xFE` | `common/dma.v:357-370,389-402` | `io.cpp:1603-1631` (`val&0xFE`, `val&0x3F`), `tsconf.h:456-476` | `dma.cpp:79-98` | DMA1 | match |
| 3 | Alignment reload value `x_addr_r = {AH[0], AL[7:1]}`, set only by register writes | `common/dma.v:342,360,366,375,392,398` | keeps the register base and adds `asize` (`tsconf.cpp:144-158`) - equivalent while writes are blocked | `dma.cpp:88,92` (`dmaSrcLow`/`dmaDstLow`) | DMA3_SourceAlignment | match |
| 4 | DMA_CTRL bits: device = `{d7, d2:0}`, ASZ d3, D_ALGN d4, S_ALGN d5, OPT d6 | `common/dma.v:220-227` | `tsconf.h:483-492`, `tsconf.cpp:124` | `dma.cpp:59-71` | DMA4, DMA5 | match |
| 5 | Start: the DMA_CTRL write launches at once: `b_ctr<=b_len`, `n_ctr<={0,b_num}`, phase reset; addresses untouched | `common/dma.v:220-231,309-313` | sets `DMA_ST_INIT` (`io.cpp:1643-1648`), `dma_init()` runs at the next `dma()` call from `update_screen` (`tsconf.cpp:106-140,525-535`, `draw.cpp:690`); status already reads busy | `dma.cpp:59-75`, `pd.cpp:680-681,734-735` (engine caught up first) | DMA2, DMA8 | match |
| 6 | Length: `DMA_LEN+1` words per block, `DMA_NUM+1` blocks, DMA_NUM 8 bits (10 only in FDR) | `common/dma.v:281-297,315-319` | `tsconf.cpp:111-112,160-163` | `dma.cpp:72-73,265-276` | DMA2 | match |
| 7 | DMA_LEN written mid-transfer applies at the next block reload (`b_ctr_next = b_len`); DMA_NUM only at launch | `common/dma.v:286,311-312,324-335` | writes dropped while busy (`io.cpp:28-41,1633-1641`) | `dma.cpp:268` (live `regs[DmaLen]`), `dma.cpp:73` | DMA10 (LEN); NUM: none | ng-matches-RTL, Unreal differs |
| 8 | Address writes mid-transfer change the live counters (a same-clock increment wins) | `common/dma.v:351-371,384-403` | dropped while busy (`io.cpp:1603-1631`) | `dma.cpp:79-98`; `pd.cpp:680-681` runs the DMA up to the write | DMA10 | ng-matches-RTL, Unreal differs |
| 9 | DMA_CTRL write while busy relaunches; `dma_act` never falls, so no INT for the aborted run | `common/dma.v:220-231,309-313,405-410` | dropped while busy (`io.cpp:1643-1648`) | `dma.cpp:56-75` | DMA9 | ng-matches-RTL, Unreal differs |
| 10 | Linear step: full 21-bit +1, crosses 16K pages, wraps at 4 MB (`s_addr_next_h` is 14 bits) | `common/dma.v:344-349,377-382` | `tsconf.cpp:175-176` (`+2 & 0x3FFFFF`) | `dma.cpp:170-171` (`kWordMask`) | DMA2 (no page crossing, no 4 MB wrap) | match |
| 11 | Aligned step inside a block: ASZ=0 low 7 word bits wrap (256 B), ASZ=1 low 8 (512 B), no carry upward | `common/dma.v:345-349` (`add_h = 0` while `!next_burst`) | `tsconf.cpp:117-118,175-176` (m1/m2 masks) | `dma.cpp:173-181` | DMA3_AlignedBlockWraps (ASZ 0, source only) | match |
| 12 | Aligned block end (`next_burst`, `b_ctr == 0` of the current word): high part +1 block (256/512 B), low part reloaded from `x_addr_r` | `common/dma.v:284-286,345-349` | `tsconf.cpp:142-158` | `dma.cpp:169,175-176,179-180` | DMA3_SourceAlignment (S_ALGN only) | match |
| 13 | Unaligned block end: the address just continues | `common/dma.v:345` | `tsconf.cpp:150,158` | `dma.cpp:170-171` | DMA2 | match |
| 14 | Counters persist after a transfer; the next launch continues from the end address | `common/dma.v:351-403` (launch does not load `s_addr`/`d_addr`) | registers follow the live address at every block end (`tsconf.cpp:146-158`) | `dma.cpp:59-75` leaves `dmaSrc`/`dmaDst` | none | match |
| 15 | Source counter steps on device reads too (SPI/IDE -> RAM): `(dram_next \|\| dev_stb) && state_rd` | `common/dma.v:351-353` | `dma_spi_r`/`dma_ide_r` never touch `ss`; unaligned block end copies the unchanged value back (`tsconf.cpp:330-352,378-398,150`) | `dma.cpp:261-262` | none | ng-matches-RTL, Unreal differs |
| 16 | Destination counter steps on device writes (RAM -> SPI/IDE/CRAM/SFILE) | `common/dma.v:384-386` | CRAM/SFILE step (`tsconf.cpp:473,498`); SPI/IDE writes do not (`tsconf.cpp:354-376,400-420`) | `dma.cpp:263` | DMA7 (one word, no step visible) | ng-matches-RTL, Unreal differs |
| 17 | Device codes: 0x1 RAM, 0x2/0xA SPI, 0x3/0xB IDE, 0x4 FILL, 0x6 BLT2, 0x9 BLT1, 0xC CRAM, 0xD SFILE (0x5 FDD, 0x7 WTP, 0xE CLIST) | `common/dma.v:90-105,122-145` | `tsconf.h:80-97` | `core/src/emulator/platforms/tsconf/tsconfdma.h:93-105` | DMA2, DMA4-7, DMA14, DMA15 | match |
| 18 | Undefined / unbuilt codes (0x0, 0x5, 0x8, 0xE, 0xF; 0x6 without XTR_FEAT): `dev_req` or the write phase waits for a strobe that never comes: busy forever, no INT, until DMA_CTRL or reset | `common/dma.v:147-151,203-204,297` | `default: DMA_ST_NOP` - not busy, no INT (`tsconf.cpp:138`) | `dma.cpp:100-120`, `WordCost() = 0` (`dma.cpp:122-125`) | DMA11, DMA5_Blit2OnlyInXtrBuilds | ng-matches-RTL, Unreal differs |
| 19 | 0x7 wait-port DMA (DMAWPD/DMAWPA, served by the AVR through `slavespi`) | `common/dma.v:102,142,437-439`, `top.v:1063-1065`, `z80/zports.v:599-600,758` | NOP, not busy (`tsconf.cpp:138`) | hangs (`dma.cpp:117-118`) | DMA11 (asserts the hang) | ng differs from both (documented out of v1 scope) |
| 20 | RAM -> RAM: read src, write dst, back to back = 2 DRAM cycles per word | `common/dma.v:201-218,244-248,415-418`; `dram/arbiter.v:168-192` | 2 units (`tsconf.cpp:178-209`) | `dma.cpp:202-205`, cost `dma.cpp:144-145` | DMA2, TIM3 | match |
| 21 | BLT1: read src, read dst, write; keep dst where the source byte (ASZ=1) / nibble (ASZ=0) is 0; 3 cycles | `common/dma.v:160-170,207-218,248` | `tsconf.cpp:211-268` | `dma.cpp:12-26,206-209`, cost `dma.cpp:128-130` | DMA4, TIM3 | match |
| 22 | BLT2 (add, OPT saturates) exists only in XTR_FEAT (VDAC) builds | `common/dma.v:92-95,127-129,172-198`; `quartus/tune.v:18` vs `quartus_vdac/tune.v:16`, `quartus_vdac2/tune.v:13` | always available (`tsconf.cpp:132,270-328`) | `dma.cpp:28-41,112-113,210-213`; `pd.cpp:306-307` | DMA5_Blit2OnlyInXtrBuilds, DMA5_Blit2InTheVdacBuilds | ng-matches-RTL, Unreal differs |
| 23 | FILL: the source word is read once (`fil_hook` freezes the write phase), then one write per word; source steps once | `common/dma.v:134,211,235-236,352` | `tsconf.cpp:422-451` | `dma.cpp:214-222`, cost `dma.cpp:131-132` | DMA6 | match |
| 24 | RAM -> CRAM / SFILE: entry = `d_addr[7:0]` (word), destination high bits ignored | `common/dma.v:412,420-421`; `z80/zmaps.v:49-77` | `(dd >> 1) & 0xFF` (`tsconf.cpp:453-502`) | `dma.cpp:223-231` | DMA7 | match |
| 25 | CRAM/SFILE cycle cost: the 1-fclk device write falls in c3, when the arbiter decides the next cycle with `dma_req = 0`, so the DMA gets every second DRAM cycle (1 used + 1 left to others) | `common/dma.v:203-204,208,417,420-421`; `dram/arbiter.v:191-192,231` | 1 unit per word (`tsconf.cpp:458-474`) | 2 (`dma.cpp:144-145`) | none (TIM3 does not assert CRAM) | ng-matches-RTL, Unreal differs |
| 26 | DMA CRAM write is visible from the next dot (dual-port CRAM read by the pixel pipeline) | `video/video_out.v:135-141`; `z80/zmaps.v:69` | `update_clut()` at the DMA step after the drawn chunk (`tsconf.cpp:469-470`, `draw.cpp:688-690`) | per-word placement in the accounted span (`engine.cpp:173-183`) | screentsconf_test.cpp TIM5_DmaCramWriteLandsAtItsDot | match (all approximate the dot) |
| 27 | A DMA CRAM/SFILE write wins the shared write port: a CPU FM-window write in the same fclk is lost | `z80/zmaps.v:49-51,69-70,77` | not modeled | not modeled | none | ng-matches-Unreal, RTL differs (1-fclk collision, negligible) |
| 28 | SPI -> RAM data: each start latches the byte of the previous exchange and sends #FF; low byte first (`bsel` 0 -> `data[7:0]`) | `common/dma.v:253-258,433-435`; `common/spi.v:31-48`; `top.v:1056-1062,1183-1188` | `Zc.Rd()` returns the previous byte (`zc.cpp:77-100`), low first (`tsconf.cpp:343-346`) | `dma.cpp:194,232-240`; `pd.cpp:43-48` | DMA14, tsconfstorage_test.cpp SpiDmaReadsTheSector | match |
| 29 | RAM -> SPI: low byte first | `common/dma.v:434` | `tsconf.cpp:367-370` | `dma.cpp:241-244` | DMA14, vdac2card_test.cpp DmaRamToSpiReachesTheChip | match |
| 30 | SPI DMA timing: 17 fclk per byte (start + 16 shift clocks, `counter[4]`), 34 fclk per word; one DRAM cycle per word, overlapped with the second byte's shift; the device phase uses no DRAM | `common/spi.v:26-52`; `common/dma.v:147-151,203,239-240,433-435` | 1 memcycle per word (`tsconf.cpp:334-349`) | 10 DRAM cycles (40 fclk) per word, charged against the free DRAM budget (`dma.cpp:133-138`, `engine.cpp:151-186`) | TIM3 (asserts 10) | ng differs from both - **BUG** |
| 31 | IDE DMA timing: bus cycle `go` -> `rdy_stb = st[4]` = 5-6 fclk, plus one DRAM cycle; ~10-14 fclk per word at idle; the bus phase uses no DRAM | `common/ide.v:62-81`; `common/dma.v:250-251,442-445` | 1 unit per word (`tsconf.cpp:378-420`) | 3 DRAM cycles charged against the budget (`dma.cpp:139-143,246-253`) | tsconfstorage_test.cpp DMA15_IdeSectorsByDma (data only) | ng differs from both (idle rate fits, loaded rate does not; same root cause as #30) |
| 32 | IDE DMA in VDAC builds: `IDE_HDD` is not defined, the IDE strobe is unconnected, so 0x3/0xB hang | `quartus_vdac/tune.v:12-13`, `quartus_vdac2/tune.v:16`; `top.v:1046-1052,1193-1216` | runs whenever `hdd` exists | hangs only with `[HDD] Scheme` = none (`dma.cpp:114-116`); TS_VDAC + Nemo IDE runs (spec §0.1 decision D1) | DMA11 (no board) | unclear - documented superset (D1), not an RTL configuration |
| 33 | DMA_STATUS (read 0x27) = `{dma_act, 7'b0}`, live | `z80/zports.v:425-426` | `io.cpp:1030-1031` | `pd.cpp:664-667` (engine caught up first) | DMA8, portdecoder_tsconf_test.cpp REG1 | match |
| 34 | DMAS/DMAD/LEN/NUM are write-only: reads return #FF (no address readback during or after a transfer) | `z80/zports.v:441-442` (`default: dout = 8'hFF`) | no read case (`io.cpp:1012-1032`) | `pd.cpp:668-669` | portdecoder_tsconf_test.cpp REG1 (its list skips 0x1A-0x26, 0x28) | match (test partial) |
| 35 | End of transfer: `int_start = !dma_act && dma_act_r` (falling edge), latched in `int_dma` until acknowledged; vector #FB; priority frame > line > DMA > WTP | `common/dma.v:405-410`; `z80/zint.v:73,119-125,151-157` | `tsconf.cpp:165-171,969-976`, `vars.cpp:102-109`, `op_system.h:28-33` | `dma.cpp:279-284`; `ints.cpp:20-24,128-140` | DMA8, DMA9 | match |
| 36 | DMA INT masked (INTMASK[2] = 0) at completion is lost; clearing the mask drops a pending one | `z80/zint.v:97,151-153` | `tsconf.cpp:974`, `io.cpp:1462-1464` | `ints.cpp:15-24` | tsconfinterrupts_test.cpp INT5 (line source only); DMA: none | match |
| 37 | DMA INT in VDOS: output gated, latch kept, serviced after VDOS | `z80/zint.v:29-31,89-93` | latch kept, `handle_int` gated by `!vdos` (`z80_main.inl:282-289`) | `ints.cpp:118-119` | tsconfinterrupts_test.cpp INT8 (frame source); DMA: none | match |
| 38 | Reset stops the DMA without an INT (`dma_act_r <= dma_act && rst_n`) | `common/dma.v:300-306,406-408` | `tsconf.cpp:903` | `dma.cpp:44-49`, `pd.cpp:268` | none | match |
| 39 | Priority: urgent video > CPU > pending video > TM (tilemap) > TS (sprites/tiles) > DMA > refresh; `dev_over_cpu = 0` (the "Z80 low priority" comment column is dead code) | `dram/arbiter.v:40-50,127,168-189` | CPU + video + TSU counted before `dma()` (`draw.cpp:583-597,688-690`) | `engine.cpp:151-168`; `core/src/emulator/platforms/tsconf/tsconfarbiter.h:16-24` | tsconfengine_test.cpp ENG1, DMA12 | match |
| 40 | The CPU never waits for the DMA (DMA only takes cycles nobody else requested) | `dram/arbiter.v:127,175-188` (`cpu_next` has no DMA term) | no DMA term in CPU timing (`vars.cpp:52-86`) | `arbiter.cpp:35-57` (no DMA term) | none | match |
| 41 | Line budget: 448 DRAM cycles of 4 fclk; no reserved refresh (refresh only when a cycle is free) | `dram/arbiter.v:8-10,182,188`; `dram/dram.v:52-53` | `MEM_CYCLES = VID_TACTS*2` (`draw.h:14`), `draw.cpp:608` | `core/src/emulator/platforms/tsconf/tsconfengine.h:59` | ENG1 | match |
| 42 | Video DRAM cycles per graphics line: `video_go` lasts `w + 4` dots, one block per `len` while it is high, so `ceil((w+4)/len) * need` (ZX 33, 16C 81, 256C 162, TXT 164 for w = 256/320/320/320) | `video/video_sync.v:237`; `video/video_mode.v:84-89,118-133`; `dram/arbiter.v:143-165` | per drawn pixel: ZX `w/8`, 16C `w/4`, 256C/TXT `w/2` (`drawers.cpp:109-142,330-340,448-500`) | `w >> {3,2,1,1}` (`engine.cpp:58-65`) | ENG1 (asserts `w >> shift`) | ng-matches-Unreal, RTL differs - **BUG** (minor) |
| 43 | Position of the free cycles inside a line: per cycle (full rate in the border, reduced in the fetch window) | `dram/arbiter.v:171-189` | per drawn chunk (`draw.cpp:583-597,688-693`, `memcyc_lcmd`) | the line's video + TSU cost spread evenly over all 224 tacts (`engine.cpp:151-165`) | none | ng differs from both - **BUG** (minor) |
| 44 | TSU (TM, TS) cycles come before the DMA | `dram/arbiter.v:168-169` | `render_ts()` before `dma()` (`draw.cpp:689-690`; `tsconf.cpp:607,733,773`) | `engine.cpp:93-95,158` (`tsuCost`) | TSU8 | match |
| 45 | CPU DRAM reads (RAM only, cache misses) take a cycle from the DMA; ROM reads do not | `z80/zmem.v:121,141-151` | `z80_main.inl:28-53` (`memcpucyc++` on RAM reads / misses) | `tsmem.cpp:102-126,156-161`; `engine.cpp:138-139,168` | ENG1 (injects `cpuAccesses`) | match |
| 46 | CPU DRAM writes take a cycle (`memwr && ramwr_en`) | `z80/zmem.v:121` | `memcpucyc++` on every write (`z80_main.inl:143-150`) | not counted (`core/src/emulator/platforms/tsconf/tsconfengine.h:49-51`, "its writes are not counted - a v1 approximation") | none | ng differs from both - **BUG** |
| 47 | Cache hit = no DRAM request, at every CPU speed | `z80/zmem.v:121,213-214` | counted only on a miss (`z80_main.inl:37-46`) | `tsmem.cpp:113-117` | tsconfmemory_test.cpp CCH1 (functional; budget not asserted) | match |
| 48 | DMA writes do not invalidate the CPU cache (only CPU writes, `cache_inv`) | `z80/zmem.v:215` | DMA writes RAM directly (`tsconf.cpp:178-502`) | `dma.cpp:155-163` | DMA13 | match |
| 49 | 3.5 / 7 MHz: the CPU stalls only when `cpu_next = 0` (`stall357`); with at most 4 of 8 video cycles and CPU requests at least 3 DRAM cycles apart no refusal occurs | `z80/zmem.v:142-151`; `dram/arbiter.v:143-146,175,187` | no video wait (`vars.cpp:52-64`) | no wait model below 14 MHz (`pd.cpp:1106-1108`) | none | match (derived) |
| 50 | 14 MHz: cache-miss M1 +3..+6, data read +4..+7 fclk by DRAM phase, writes free while granted; refused cycles freeze the clock | `z80/zmem.v:141-207`; `dram/arbiter.v:143-189` | read +2/+3 T, M1 aligned to the 7 MHz cycle, no video refusal (`vars.cpp:52-86`) | `arbiter.cpp:83-127`; `tsmem.cpp:170-186` | tsconfarbiter_test.cpp ARB1-ARB5; tsconfmemory_test.cpp TIM1_* | ng-matches-RTL, Unreal differs |

Counts: match 33, ng-matches-RTL / Unreal differs 9, ng-matches-Unreal / RTL differs 2,
ng differs from both 5, unclear 1 (50 rows).

## Bugs and gaps

### B1 (row 30, 31) - SPI / IDE DMA time is charged to the DRAM budget, SPI rate is wrong

RTL: an SPI DMA word takes two SPI exchanges of 17 fclk each (`spi.v`: `start` when `counter[4]`, then
16 counting clocks), 34 fclk per word, and needs **one** DRAM cycle, which overlaps the second
byte's shift. The DRAM stays free for CPU, video, TSU during the device phase, so the SPI DMA rate
does not depend on the video mode until the DRAM write cannot be granted within 17 fclk.

unreal-ng charges 10 DRAM cycles (40 fclk) per word against the line's free budget
(`WordCost()` `dma.cpp:133-138`, `AccountBudget` `engine.cpp:151-186`):
- idle (no video): 448 / 10 = 44.8 words per line instead of 448 / 8.5 = 52.7 (15% slow);
- 256C 320x200 window: (448 - 160 - TSU - CPU) / 10 = at most 28.8 words per line instead of 52.7
  (about 1.8x slow), so SD sector loads and raster effects that stream from SD drift with the video mode.

IDE (`ide.v`) has the same structure: 5-6 fclk bus cycle plus one DRAM cycle. ng's 3 cycles fit the
idle rate but all 3 come off the budget, so under video load the IDE DMA slows down more than the RTL.

Unreal is wrong the other way (1 unit per word for SPI and IDE).

Fix direction: model the device phase as time (34 fclk SPI, ~6 fclk IDE) and charge only the one DRAM
write/read cycle to the budget.

Suggested tests:
- `TsConfDma_Test.TIM3_SpiWordIs34Fclk`: SPI -> RAM, 256 words, NOGFX, CPU idle; assert the transfer
  ends after 256 x 34 fclk = 8704 fclk (2176 tacts at 3.5 MHz, within one word).
- `TsConfDma_Test.DMA12_SpiPacingIgnoresVideo`: the same transfer under NOGFX and 256C 360x288;
  assert equal line counts (RTL), where today 256C is slower.
- `TsConfDma_Test.TIM3_IdeLeavesDramToVideo`: IDE -> RAM under 256C vs NOGFX; assert the per-word time
  differs by less than one DRAM cycle.

### B2 (row 46) - CPU writes are not subtracted from the DMA budget

RTL `zmem.v:121`: `ramreq = !rom_n_ram && ((memrd && !cache_hit_en) || (memwr && ramwr_en))` - a RAM
write takes a DRAM cycle with priority over the DMA. Unreal counts writes (`z80_main.inl:149`). ng
counts only reads (`tsmem.cpp:120,160`), so a CPU running a write-heavy loop (screen clear, `LDIR`,
`PUSH` fill) leaves the DMA up to one cycle per write too many. `LDIR` alone is one read + one write per
21 T, i.e. 1 extra DMA cycle per 42 DRAM cycles.

Suggested test: `TsConfEngine_Test.ENG1_CpuWritesTakeCycles`: run a RAM copy across one line while the
CPU executes 40 `LD (HL),A` to RAM; assert the DMA moved `(448 - video - 40 reads(M1) - 40 writes) / 2`
words. Also assert writes to a write-protected window or ROM do not count.

### B3 (row 42) - video cycles per line undercounted by 1-4

`video_go` is high from `hpix_beg - go_offs - x_offs` to `hpix_end - go_offs - x_offs + 4`
(`video_sync.v:237`), i.e. `w + 4` dots, and the arbiter starts a block at every `len`-th cycle while
it is high. Per line: ZX 33 (ng 32), 16C 320: 81 (80), 256C 320: 162 (160), TXT 320: 164 (160),
256C 360: 182 (180). ng's `VideoCost` (`engine.cpp:58-65`) uses `w >> shift`; Unreal counts per drawn
pixel and agrees with ng. The arbiter model already has the right window (`FetchOf`, `h1 = ... + 4`,
`arbiter.cpp:20-21`); `VideoCost` could reuse it.

Suggested test: change `TsConfEngine_Test.ENG1_LineBudget` expectations to
`ceil((w + 4) / length) * need` (33, 81, 162, 164, 0) and assert `videoCost` equals the number of
blocks `TsConfArbiter::FetchOf` produces for the line.

### B4 (row 43) - free cycles spread evenly over the line

`AccountBudget` prorates the line's `videoCost + tsuCost` over the whole 224 tacts
(`share = cost*b/kLineTacts - cost*a/kLineTacts`, `engine.cpp:162`). In the RTL the fetch window takes
its cycles only between `h0` and `h1`; the border part of a line gives the DMA every cycle. A transfer
that starts at the left border therefore advances slower in ng until the window and faster inside it.
Totals per line are right; the error is the position of each word inside the line, which matters for
the DMA CRAM writes TIM-5 places by credit (`engine.cpp:173-183`) and for the busy flag / INT moment.

Suggested test: `TsConfDma_Test.DMA12_BorderGivesFullRate`: 256C 320x200, launch a RAM copy at tact 0 of
line 100, catch up to tact 50 (dots 0-99, all left of `h0` = 103); assert 50 words moved (100 cycles / 2).

### B5 (row 19) - wait-port DMA (0x7) hangs

RTL serves 0x7 through the AVR wait port (`dma.v:437-439`, `top.v:1063-1065`). ng hangs it on purpose
(out of v1 scope); Unreal treats it as a no-op. Keep DMA11's assertion only while the wait port is
unimplemented; when it is implemented add `TsConfDma_Test.DMA16_WaitPortToRam` (AVR-supplied bytes land
low first, one INT at the end).

### Untested rows (implementation matches; tests missing)

| Row | Suggested test | Assertion |
|:--|:--|:--|
| 7 | `DMA10_NumIsLatchedAtLaunch` | DMA_NUM written mid-transfer does not change the block count |
| 10 | `DMA2_LinearCrossesPagesAndWraps` | copy from 0x3FFFFE (page #FF end) wraps to 0x000000; copy across 0x7FFE/0x8000 is linear |
| 11-12 | `DMA3_DestinationAlignmentAsz1` | D_ALGN with ASZ 1: 0x1FC start wraps to 0x000 inside the block, next block starts at base + 0x200 |
| 14 | `DMA2_NextLaunchContinues` | two launches without address writes write consecutive memory |
| 15 | `DMA14_SpiReadStepsTheSource` | after a 4-word SPI -> RAM, a RAM -> RAM launch without address writes reads from source + 8 bytes |
| 16 | `DMA7_CramStepsTheDestination` | 4-word RAM -> CRAM from dst 0 fills CRAM 0..3; RAM -> SPI then RAM -> RAM continues at dst + 2 words |
| 25 | `TIM3_CramCostsTwoCycles` | `WordCost()` = 2 for 0x8C and 0x8D |
| 27 | none needed unless the 1-fclk collision is modeled | - |
| 34 | `REG1_DmaRegistersReadFF` | `In(0x1AAF..0x1FAF, 0x26AF, 0x28AF)` = #FF while busy and after |
| 36 | `DMA8_MaskedCompletionIsLost` | INTMASK[2] = 0 at completion: no pending DMA INT; setting the mask afterwards does not raise it |
| 37 | `DMA8_VdosDefersTheDmaInt` | completion inside vdos: not asserted while vdos, asserted (vector #FB) after |
| 38 | `DMA8_ResetStopsWithoutInt` | reset during a transfer: busy 0, no DMA INT |
| 40 | `ARB6_DmaDoesNotSlowTheCpu` | 14 MHz NOP/LD loop in the window takes the same fclks with a running RAM copy |
| 47 | `CCH3_HitTakesNoDmaCycle` | 40 cached reads on a line leave the DMA its full budget |
| 49 | `ARB7_NoVideoWaitBelow14MHz` | at 3.5 and 7 MHz a TXT-window loop runs at border speed |

### Notes

- The arbiter comment (`arbiter.v:40-50`) describes a "Z80 low" priority raised at INT; `dev_over_cpu`
  is the constant 0 (`arbiter.v:127`), so it is dead in every build. All three agree on CPU > DMA.
- Row 32: the spec's decision D1 leaves IDE to `[HDD] Scheme` even with TS_VDAC set; on real VDAC
  firmware IDE DMA would hang. Not a bug by the project's decision, listed for completeness.
