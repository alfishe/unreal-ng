# TS-Conf audit: interrupts, wait states, CPU clock

Three-way check of the unreal-ng TS-Conf machine against the ZX-Evo RTL (`fpga/current/`, default build
`quartus/tune.v`: `COPPER`, `PENT_312`, `AUTO_INT`, `IDE_VDAC2` all off; the VDAC2 build `quartus_vdac2/tune.v` turns
on `IDE_VDAC2`) and the TS-Labs Unreal Speccy fork (`Unreal/`). unreal-ng paths are relative to the repository root
(`core/src/...`, `core/tests/...`), checked at `cf3adb714`. Every row was read in all three sources;
"not found" means the place searched is named. Timing units: fclk = 28 MHz FPGA clock, "tact" = 3.5 MHz raster
T-state (224 per line, 71680 per frame), "clock" = one CPU clock at the current speed.

## Table

| # | Behavior | RTL | TS-Labs Unreal | unreal-ng | Test | Verdict |
|:--|:--|:--|:--|:--|:--|:--|
| 1 | Frame INT position | `int_start_s = (hcount == {hint_beg,1'b0}) && (vcount == vint_beg) && c0` -> tact `VSINT*224 + HSINT` (`fpga/current/video/video_sync.v:132`); registers `fpga/current/z80/zports.v:207-209,542-544`, `video/video_ports.v:83-93,120` | `frame_t = vsint * t_line + hsint` (`Unreal/io.cpp:1467-1480`), compared with `cpu.t` (`Unreal/tsconf.cpp:930`) | `frameEvent = vs * kLineTacts + hs` (`core/src/emulator/platforms/tsconf/tsconfinterrupts.cpp:51-62`) | `INT2_FramePosition` (`core/tests/emulator/machines/tsconf/tsconfinterrupts_test.cpp:53`) | match |
| 2 | Out-of-range HSINT / VSINT | 8-bit `hint_beg` x2 can reach hcount 510 > 447, `vint_beg` 9-bit > 319: compare never true, no INT (`video_sync.v:132`, `HPERIOD=448`, `VPERIOD_50=320` at `:84,94`) | `frame_t = -1` when `hsint > t_line-1` or `vsint > 319` (`Unreal/io.cpp:1469`) | no event when `hs >= 224` or `vs >= 320` (`tsconfinterrupts.cpp:54`) | `INT2_FramePosition` (`tsconfinterrupts_test.cpp:53`) | match |
| 3 | HSINT reset value | `hint_beg <= 8'd1` -> tact 1 (`video/video_ports.v:104`); `vint_beg <= 0` (`:80`) | `comp.ts.hsint = 2` (`Unreal/tsconf.cpp:906`) | `r[TsConfReg::HsInt] = 0x01` (`core/src/emulator/ports/models/portdecoder_tsconf.cpp:248`) | `INT1_ResetFramePulse` (`tsconfinterrupts_test.cpp:36`, first INT at tact 1) | ng-matches-RTL, Unreal differs |
| 4 | VSINTH bits 7:4 (auto-increment of the frame INT line) | stored as `vint_inc` (`video_ports.v:86-89`) but `int_start_s` is tied to `1'b0` unless `AUTO_INT` (`video/video_top.v:253-257`), which no `tune.v` defines: no effect | only bit 0 stored (`vsinth:1`, `Unreal/tsconf.h:253`) | only bit 0 used (`tsconfinterrupts.cpp:53`) | none | match |
| 5 | Frame INT pulse length | 6-bit `intctr`, `intctr_fin = intctr[5]` = 32 counts of `zpos` (CPU clock edges) (`fpga/current/z80/zint.v:187-196`); `int_frm` cleared on `intctr_fin` (`zint.v:140-141`) | `frame_len = (conf.intlen * cpu.rate) >> 8` = `intlen` clocks at the current speed (`Unreal/z80.cpp:199`), `intlen=32` from `cfg/Unreal.ini:124` | `kFramePulseClocks = 32` CPU clocks (`tsconfinterrupts.h:55`, `tsconfinterrupts.cpp:104-108`) | `INT1_ResetFramePulse` (`:36`), `INT7_PulseIsCpuClocks` (`:115`) | match |
| 6 | Pulse frozen by /WAIT and clock stalls | counts only on `zpos && !intctr_fin && !wait_r && !vdos` (`zint.v:194`); a `zclock` stall suppresses `zpos` (`fpga/current/z80/zclock.v:120-121`) | counts raw `cpu.t` (`Unreal/tsconf.cpp:941-944`); no freeze | `OnWait` shifts the pulse start by the stall (`tsconfinterrupts.cpp:146-178`), fed by every `AddWaitStates`/`AddWaitTicks` (`core/src/emulator/cpu/z80.h:879-891`) | `INT9a`..`INT9e`, `INT10`, `INT10b` (`tsconfinterrupts_test.cpp:229-306`) | ng-matches-RTL, Unreal differs |
| 7 | Pulse frozen while vdos | same counter condition `!vdos` (`zint.v:194`), `vdos` = `pre_vdos` (`fpga/current/top.v:1106`) | `else if (vdos) { /* No Operation */ }` keeps `frame_pend` and stops counting (`Unreal/tsconf.cpp:940`) | `FramePulseActive` counts on through vdos and `IsIntAsserted` drops the latch after 32 clocks (`tsconfinterrupts.cpp:104-116`); vdos only gates the output (`:119`) | `INT8_VdosGatesWithoutLosing` (`:150`) covers 1 tact only | ng differs from both (BUG) |
| 8 | vdos INT gate starts at the trapping I/O cycle | gate is `pre_vdos`, set by `vdos_on = iordwr_s && ... && virt_vg` during the I/O cycle (`fpga/current/z80/zports.v:650`, `fpga/current/z80/zmem.v:107-114`, `top.v:1106`); the INT sampled at the end of that instruction is blocked | gate is `vdos OR vdos_m1`; `vdos_m1 = 1` at the trapped access (`Unreal/io.cpp:1236`, `Unreal/z80_main.inl:278,286`) | gate is `_ts.vdos` only (`tsconfinterrupts.cpp:119`); the trapped access sets `preVdos` (`portdecoder_tsconf.cpp:818`), `vdos = 1` only at the next M1 (`:914-918`) | none (`VDOS1_VirtualDriveSwap`, `core/tests/emulator/machines/tsconf/tsconfstorage_test.cpp:122`, checks mapping only) | ng differs from both (BUG) |
| 9 | vdos INT gate ends at once | `vdos_off` clears `pre_vdos` in the same cycle (`zmem.v:108-111`, `zports.v:651`) | `comp.ts.vdos = 0` at the VG access (`Unreal/io.cpp:1228-1232`) | `_ts.vdos = 0` at the VG93 register access (`portdecoder_tsconf.cpp:821-825`) | `VDOS1_VirtualDriveSwap` (`tsconfstorage_test.cpp:122`, state only) | match |
| 10 | Frame / line / DMA events during vdos | latches set regardless of vdos; only `int_all ... && !vdos` is gated (`zint.v:89-93,135-157`); the `zint.v:29-31` comment ("lost") is stale | frame deferred (`tsconf.cpp:940`), line lost (`if (!vdos) line_pend = ...`, `tsconf.cpp:964-965`), DMA latched (`tsconf.cpp:969-976`) | all latched, output gated (`tsconfinterrupts.cpp:43-71,119`); frame see row 7 | `INT8_VdosGatesWithoutLosing` (`:150`, frame only) | ng-matches-RTL, Unreal differs (line); frame part: see row 7 |
| 11 | Line INT event | `int_start_lin = line_start_s` = `hcount == 447 && c3` on every line (`video_sync.v:123-125`, `top.v:1095`); `int_lin` rises at fclk 1792 of the line = tact 224 of the line = tact 0 of the next | `line_t` starts at 0, `+= VID_TACTS` (224) when `cpu.t >= line_t` (`Unreal/tsconf.cpp:953-955`, `Unreal/z80_main.inl:274`, `Unreal/vars.cpp:130`): tacts 0, 224, ... 71456 | `(raster + 1) % 224 == 0`: tacts 223, 447, ... 71679 (`tsconfinterrupts.cpp:64-68`) | `INT3_LineInterrupts` (`:72`, asserts first at 223), `INT4` (`:91`) | ng differs from both (BUG, 1 tact early) |
| 12 | Line INT count | 320 per frame (`VPERIOD_50 = 320`, `video_sync.v:94`) | 320 (`line_t` 0..71456) | 320 (`tsconfinterrupts_test.cpp:85`) | `INT3_LineInterrupts` (`:72`) | match |
| 13 | Line INT from the FT812 (VDAC2 build) | `int_start_lin(vdac2_msel ? int_start_ft : line_start_s)`, `int_start_ft` = falling edge of FT812 INT_N (`top.v:468-471,1093`) | at every line start, `pre_pend = vdac2::is_interrupt()` when `ft_en` (level sampled, all lines) (`Unreal/tsconf.cpp:958-962`) | edges via `ITsConfLineSource`, msel per line (`tsconfinterrupts.cpp:73-102`) | `Vdac2Card_Test.LineInterruptFromLineStartsWithoutMsel`, `Ft812SwapInterruptIsTheLineInterruptWithMsel`, `MselIsLatchedAtTheLineStart` (`core/tests/emulator/machines/tsconf/vdac2card_test.cpp:143,155,183`) | ng-matches-RTL, Unreal differs |
| 14 | Line / DMA latches have no timeout | `int_lin`, `int_dma` cleared only by ack, mask or reset (`zint.v:143-157`) | `line_pend` / `dma_pend` cleared only in `handle_int` or by `INTMASK` (`Unreal/op_system.h:26-33`, `Unreal/io.cpp:1464`) | cleared only by ack / mask (`tsconfinterrupts.cpp:15-18,122-144`) | `INT5_MaskClearsPending` (`:103`, line still pending 77 tacts later) | match |
| 15 | DMA INT event | `int_start = !dma_act && dma_act_r` (falling edge of busy) (`fpga/current/common/dma.v:406-410`) | `new_dma = true` at the last block (`Unreal/tsconf.cpp:170`), latched in `ts_dma_int` (`:969-976`) | `TsConfDma::Finish` -> `RaiseDma` (`core/src/emulator/platforms/tsconf/tsconfdma.cpp:279-284`, `tsconfinterrupts.cpp:20-24`) | `DMA8_StatusAndInterrupt`, `DMA9_RelaunchWhileBusy` (`core/tests/emulator/machines/tsconf/tsconfdma_test.cpp:190,206`) | match |
| 16 | Wait-port INT (vector F9, INTMASK bit 3) | `int_start_wtp` = AVR `config0` bit 5 (`top.v:182-192`), latch `zint.v:169-179`, `dis_int_wtp = !intmask[3]` (`zint.v:98`) | not implemented: `intmask = val & 0x07`, vectors only FF/FD/FB (`Unreal/io.cpp:1463`, `Unreal/tsconf.cpp:891-893`) | `RaiseWaitPort` from the AVR ZiFi strobe (`tsconfinterrupts.cpp:26-30`, `portdecoder_tsconf.cpp:1161`) | `ZiFi_Test.TheWaitPortInterrupt` (`core/tests/emulator/io/network/zifi_test.cpp:210`) | ng-matches-RTL, Unreal differs |
| 17 | Copper INT (F7, bit 4) | only under `COPPER` (`zint.v:42-44,100,160-166`), not defined in any `tune.v` | none | none | n/a | match (not built) |
| 18 | Vectors and priority | FF frame > FD line > FB DMA > F9 wait-port (`zint.v:80-83,117-132`); driven on D during `intack` (`top.v:435`, `fpga/current/z80/zsignals.v:73`) | FF > FD > FB (`Unreal/vars.cpp:97-112`) | same table (`tsconfinterrupts.cpp:124-143`) | `INT4_PriorityAndSelectiveClear` (`:91`), `INT6_InterruptModesThroughTheCpu` (`:173`) | match |
| 19 | Ack clears only the served source | `int_frm` cleared on any `intack_s`; `int_lin` only if `!int_frm`; `int_dma` only if neither (`zint.v:140,148,156`) | clears frame, else line, else DMA (`Unreal/op_system.h:26-33`) | clears the highest pending bit (`tsconfinterrupts.cpp:135-142`) | `INT4_PriorityAndSelectiveClear` (`:91`), `INT1_AcknowledgeGivesVectorFFAndClears` (`:45`) | match |
| 20 | Source chosen at the acknowledge moment | `int_sel` latched at `intack_s` (IORQ in the INTA cycle, about 3 clocks after sampling), from the latches at that moment; frame may already have ended (`intctr_fin`); with nothing pending `int_sel` keeps its previous value (no else branch, `zint.v:117-132`) | re-evaluates the frame pulse 3 tacts later (`tt += rate * 3; ts_frame_int(...)`, `Unreal/vars.cpp:95-100`), returns `0xFF` when nothing is pending (`:111-112`) | picks the source at the sampling instant: `AcknowledgeInterrupt` ignores `t`, does no `CatchUp` and no pulse-end check (`tsconfinterrupts.cpp:122-144`); fallback `0xFF` | none | ng differs from both (edge case, low) |
| 21 | INTMASK write and reset | 8-bit register, reset `8'b1` (`zports.v:562,616-617`); a cleared bit holds its latch at 0 every clock (`zint.v:95-98,136-137`) | `intmask = val & 0x07; pend &= val & 0x07` (`Unreal/io.cpp:1462-1465`), reset 1 (`Unreal/tsconf.cpp:894`) | `OnMaskWrite: intPending &= mask` (`tsconfinterrupts.cpp:15-18`); events latch only while the bit is set (`:54,67`); reset `0x01` (`portdecoder_tsconf.cpp:252`) | `INT5_MaskClearsPending` (`:103`), `RST1_WarmResetValues` (`core/tests/emulator/machines/tsconf/portdecoder_tsconf_test.cpp:28`) | match |
| 22 | IM0 / IM1 / IM2 through the CPU | real Z80; vector byte on D for every mode (`top.v:435`) | `handle_int`: IM<2 -> #38, IM2 reads `(I<<8)+vector` (`Unreal/op_system.h:8-13`) | `HandleINT` (`core/src/emulator/cpu/z80.cpp:1745-1848`) | `INT6_InterruptModesThroughTheCpu` (`:173`) | match |
| 23 | INT while HALT | real Z80: HALT runs NOP M1 cycles, INT sampled each, return address past HALT | `if (DirectRm(pc) == 0x76) pc++` (`Unreal/op_system.h:15-16`), halted step (`Unreal/z80_main.inl:222-238`) | `HaltedM1` idle fetches (`z80.cpp:399-419`), `if (cpu.halted) cpu.pc++` (`z80.cpp:1767-1768`) | none in the TS-Conf suite (generic Z80 tests only) | match |
| 24 | Frame pulse across the frame end | free-running counter, no frame boundary in `zint.v` | `f2 && new_frame` carries the pulse (`Unreal/tsconf.cpp:931-934`), `last_cput -= conf.frame` (`Unreal/vars.cpp:131`) | `intFrameRaster -= kFrameTacts` (`tsconfinterrupts.cpp:180-211`) | `FramePulseCrossesTheFrameEnd` (`:125`), `RolloverLatchesTheLastLineEvent` (`:140`) | match |
| 25 | Frame pulse across a clock switch | counts `zpos` edges: clocks already counted stay counted, the rest run at the new speed (`zint.v:194`) | `frame_len` recomputed for the new rate, `frame_cnt` kept in 3.5 MHz tacts (`Unreal/z80.cpp:199`, `Unreal/tsconf.cpp:941`): elapsed time, not elapsed clocks | `t` rescaled (`z80.cpp:938-960`), pulse measured as `t - intFrameRaster * Multiplier()` (`tsconfinterrupts.cpp:104-108`): elapsed raster time x new multiplier | none | ng-matches-Unreal, RTL differs (low) |
| 26 | CPU clock select | `turbo = sysconf[1:0]`: 00 3.5, 01 7, 1x 14 MHz (`top.v:228`, `zclock.v:50-52,107-108`); reset `sysconf <= 0` (`zports.v:564`) | `zclk` 0/1/2/3 -> turbo 1/2/4/4 (`Unreal/z80.cpp:190-198`), reset 0 (`Unreal/tsconf.cpp:901`) | `kRatio[4] = {1, 2, 4, 4}` (`portdecoder_tsconf.cpp:1099-1124`), reset 0 (`:246`) | `SysConfigClock` (`portdecoder_tsconf_test.cpp:346`), `RST1_WarmResetValues` (`:28`) | match |
| 27 | When the clock switch takes effect | combinational from the register: at once after the write; the "only at RFSH" rule in the `zclock.v:22` comment is not implemented (`zclock.v:69,107`) | `set_clk()` inside the port write (`Unreal/io.cpp:1448-1452`) | `ApplyClock` -> `ApplyHardwareTurboNow` inside the write (`portdecoder_tsconf.cpp:707-712,1123`) | `CLK2_ClockSwitchKeepsRasterEvents` (`tsconfinterrupts_test.cpp:213`) | match |
| 28 | Raster events independent of the CPU clock | `video_sync` runs on fclk phases only | `cpu.t` is in 3.5 MHz tacts at every rate (`turbo(a)`, `Unreal/defs.h:383`) | `RasterAt(t) = t / Multiplier()` (`tsconfinterrupts.cpp:38-41`) | `CLK2_ClockSwitchKeepsRasterEvents` (`:213`), `INT7_PulseIsCpuClocks` (`:115`) | match |
| 29 | SYSCONFIG bit 2 copies into CACHECONFIG | `cacheconf <= {4{xt_wr_data[2]}}` on every SYSCONF write (`zports.v:593-597`) | `cacheconf = comp.ts.cache ? 0x0F : 0x00` (`Unreal/io.cpp:1450`) | `(value & 0x04) ? 0x0F : 0x00` (`portdecoder_tsconf.cpp:709`) | `CCH2_SysConfigCopiesTheCacheBit` (`core/tests/emulator/machines/tsconf/tsconfmemory_test.cpp:103`) | match |
| 30 | Cache hit avoids the DRAM access (all speeds) and its 14 MHz wait | `ramreq = !rom_n_ram && ((memrd && !cache_hit_en) ...)`, `cache_hit_en = cache_hit && cache_en[win]` (`zmem.v:121,213-214`) | `cache_miss` gates the 14 MHz alignment (`Unreal/z80_main.inl:37`, `Unreal/vars.cpp:58,78`) | `CacheRead` hit -> no `dram` -> no `DramWait` (`core/src/emulator/memory/tsconf/tsconfmemory.cpp:102-128,189-201`) | `CCH1_HitReturnsTheCachedWord` (`tsconfmemory_test.cpp:75`), `TIM1_NoWaitsOffDram` (`:156`) | match |
| 31 | Cache filled while every window has it off | every CPU DRAM read writes the entry (`.wren (cpu_strobe)`, `zmem.v:222-229,265`), regardless of `cache_en` | fills on every miss, also when disabled (`miss = !(cacheconf & bit) OR tag mismatch`, `Unreal/z80_main.inl:37-46`) | fills only while some window is enabled; `CacheClear` when all go off (`portdecoder_tsconf.cpp:1063-1081`, documented in `tsconfmemory.cpp:207-212`) | none (`DMA13_DmaLeavesTheCacheStale`, `tsconfdma_test.cpp:273`, covers the enabled case) | ng differs from both (documented, low) |
| 32 | ROM accesses never wait / never cached | `ramreq` and cache fill only for `!rom_n_ram` (`zmem.v:81,121`) | `cache_miss = false` outside RAM (`Unreal/z80_main.inl:56-57`) | `BANK_RAM` check (`tsconfmemory.cpp:106-107`, `:189-201` via `CountDramRead`) | `TIM1_NoWaitsOffDram` (`tsconfmemory_test.cpp:156`) | match |
| 33 | 14 MHz DRAM wait amounts (M1, data read, write) | wait table comment: M1 +3..+6, read +2..+5, write 0 (`zmem.v:153-176`); code releases M1 at c1 and read at c2 of the cycle after the grant (`zmem.v:202-207`) | M1 miss: `tt = (tt + 0x40*7) & ~0x7F` (+2/+3 clocks); read miss: +5/+6 instead of +3 (+2/+3 clocks); writes none (`Unreal/vars.cpp:52-86`) | `TsConfArbiter::CpuAccess`: M1 +3..+6 fclk, read +4..+7 fclk (code reading, deliberately not the comment), write 0 while granted (`core/src/emulator/platforms/tsconf/tsconfarbiter.cpp:83-127`, `tsconfmemory.cpp:170-179`) | `TIM1_UncachedRamWaitsAt14MHz` (`tsconfmemory_test.cpp:130`) | **settled 2026-10-05 by RTL simulation: unreal-ng matches** (TODO.md, rtl-sim) |
| 34 | 14 MHz: video refusing the CPU (`cpu_next = 0`) | read waits for the grant; any non-read cycle freezes the clock while `cpu_next = 0` (`stall14_cyc = memrd ? stall14_cycrd : !cpu_next`, `zmem.v:146`; arbiter `fpga/current/dram/arbiter.v:171-189`) | none (only `memcpucyc` statistics, `Unreal/z80_main.inl:44,149`) | modeled in `TsConfArbiter` (`tsconfarbiter.cpp:84-262`; the refused cycles fclk by fclk since 2026-10-05) | `ARB3_NopsAreNotDelayedByVideo`, `ARB4_WritesIn256C`, `ARB5_RefusedReadWaitsForTheGrant`, `ARB6_CpuWaitsMatchTheRtl` (`core/tests/emulator/machines/tsconf/tsconfarbiter_test.cpp:100,113,132,279`) | ng-matches-RTL, Unreal differs |
| 35 | 3.5 / 7 MHz DRAM stall | `stall357 = cpureq_357 && !cpu_next` exists (`zmem.v:144,151`) | none | none: arbiter only at 14 MHz (`portdecoder_tsconf.cpp:1104-1108`) | `TIM1_NoWaitsOffDram` (`tsconfmemory_test.cpp:156`, asserts no waits at 7 MHz) | **settled 2026-10-05 by RTL simulation: unreal-ng matches** (TODO.md, rtl-sim) |
| 36 | 14 MHz external I/O stall (AY `#xxFD` with A15=1, VG93 `#1F/#3F/#5F/#7F` while DOS or VG_OPEN) | `io_stall = iorq_s && external_port && turbo_int[1]`, `stall_count <= 0` -> counts to 8 (`zclock.v:76-90`, `zports.v:344-345`): "8 tacts 28MHz"; by cycle count 9 fclk incl. the start cycle | none | `AddWaitStates(4)` = 8 fclk at 14 MHz (`portdecoder_tsconf.cpp:1329-1336`) | `TIM2_ExternalIoStallAt14MHz` (`portdecoder_tsconf_test.cpp:411`) | ng-matches-RTL, Unreal differs (8 vs 9 fclk unresolved) |
| 37 | DOS-entry / vdos-exit stall | `dos_stall = dos_on OR vdos_off` -> `stall_count <= 4`, about 4-5 fclk clock stop at every speed (`zclock.v:75,84-85`) | none | none (`BeforeMachineM1` / `FdcAccess` add no wait, `portdecoder_tsconf.cpp:799-829,908-937`; the ATM3 decoder has the equivalent, `portdecoder_atm3.cpp:874-882`) | none | ng-matches-Unreal, RTL differs (gap) |
| 38 | IDE port stall | `ide_stall` from `ide_req` until `ide_ready` (`zports.v:777-781`, `fpga/current/common/ide.v:72-81`: 5-state cycle, longer while DMA owns the bus), all speeds (`zclock.v:96`) | none | +1/+2/+3 T at 3.5/7/14 MHz only with `[HDD] IdeStall=1`, default 0 (`portdecoder_tsconf.cpp:1338-1345`, `core/src/emulator/platform.h:610`, `core/src/emulator/config.cpp:671`) | `IDE4_Stall` (`tsconfstorage_test.cpp:179`) | ng-matches-Unreal, RTL differs (default off) |
| 39 | /WAIT on the Gluk clock data port `#BFF7` | `wait_start_gluclock = gluclock_on && !a[14] && (portf7_rd OR portf7_wr)` holds /WAIT until the AVR's `wait_end` (`zports.v:763`, `fpga/current/z80/zwait.v:31-43`) | none (`cmos_read()` returns at once, `Unreal/io.cpp:1413-1421`) | none (`DecodeF7In/Out` call `_evoAvr` directly, `portdecoder_tsconf.cpp:841-865`) | none | ng-matches-Unreal, RTL differs (gap) |
| 40 | /WAIT on the COM / ZiFi port `#xxEF` | every `#xxEF` read or write waits for the AVR (`zports.v:735-736,764`, `zwait.v:45-49`) | none (`zf232.read`, `Unreal/io.cpp:1424-1425`) | AVR access cycles as `AddWaitStates` when a serial device is attached (`core/src/emulator/io/serial/comport.cpp:69-82,279,292`); without one the arm reads `#FF` with no wait (`portdecoder_tsconf.cpp:547-550`) | `ComPort_Test.EveryEvoAccessWaitsForTheAvr` (`core/tests/emulator/io/serial/comport_test.cpp:126`) | ng-matches-RTL, Unreal differs (no wait when no COM device) |
| 41 | NMI | none: `assign nmi_n = 1'bZ`, `znmi` ports commented out (`top.v:208,422,1111-1123`) | the NMI key injects one (`main_nmi -> m_nmi`, `Unreal/emulkeys.cpp:304-309`) | `RequestNMI` / the button path pulses /NMI; TS-Conf does not override `RequestBoardNmi` (`core/src/emulator/emulator.cpp:1047-1061,1156`) | none | ng-matches-Unreal, RTL differs (low, host action) |

Verdict counts (41 rows): match 21; ng-matches-RTL, Unreal differs 8 (rows 3, 6, 10, 13, 16, 34, 36, 40);
ng-matches-Unreal, RTL differs 5 (rows 25, 37, 38, 39, 41); ng differs from both 5 (rows 7, 8, 11, 20, 31); unclear 2
(rows 33, 35).

## Bugs and gaps

**Row 7 - frame INT lost during vdos (BUG).** The RTL freezes the 32-clock counter while `pre_vdos` is set
(`zint.v:194`), so a frame INT whose event falls inside a virtual-drive session (or that is running when one starts) is
delivered after the session ends; TS-Labs Unreal does the same (`tsconf.cpp:940`) and the project's own spec (§5,
"frozen while vdos or WAIT is active") says so. unreal-ng only gates the output: `FramePulseActive` keeps counting raster
time and `IsIntAsserted` drops the latch once 32 clocks have passed, so any vdos stretch longer than 32 clocks over the
event loses the frame INT. Virtual-drive sector transfers run for thousands of clocks, so TR-DOS programs on a virtual
drive lose frame INTs. Fix direction: treat vdos like a wait in the pulse accounting (shift `intFrameRaster` by the vdos
time, as `OnWait` does). Suggested test `INT8b_VdosFreezesTheFramePulse`: event at tact 1, set `vdos = 1` at tact 5,
query at tact 5000 (gated, latch still pending), clear vdos at tact 5000, assert /INT for exactly the 28 remaining
clocks (tacts 5000..5027) and not at 5028.

**Row 8 - vdos gate starts one M1 late (BUG).** In the RTL the INT gate is `pre_vdos`, which rises during the
trapped VG93 I/O cycle (`zports.v:650`, `zmem.v:113-114`), so an INT sampled at the end of that `IN`/`OUT` is
refused; Unreal gates on `vdos_m1` too. unreal-ng gates only on `_ts.vdos`, which becomes 1 at the next M1
(`portdecoder_tsconf.cpp:914-918`). If /INT is active at the end of the trapping instruction (EI'd TR-DOS polling the
status register through a virtual drive), unreal-ng accepts the INT, and the ISR's first fetch then switches vdos on:
an IM1 handler at `#0038` is fetched from RAM page `#FF` (the virtual-drive code) instead of the TR-DOS ROM. Fix
direction: gate on `vdos OR preVdos` (and freeze the pulse on both, row 7). Suggested test
`INT8c_TrappedAccessGatesTheIntAtOnce`: DOS on, FDD_VIRT drive 0, frame INT pending inside its pulse, `In(0x001F)`;
assert `IsIntAsserted` is false before any M1, and true again after a VG93 register access ends vdos (pulse permitting).

**Row 11 - line INT one tact early (BUG, sub-tact class).** `line_start_s` is the `c3` fclk of dot 447
(`video_sync.v:123-125`), so `int_lin` rises at fclk 1792 of the line, i.e. tact 224 = tact 0 of the next line; the
frame INT on the same convention rises 1 fclk into tact `HSINT`. TS-Labs Unreal fires at tacts 0, 224, ... which is the
same set as the RTL (the last line's event wraps to tact 0). unreal-ng fires at `224 n - 1` (223, 447, ..., 71679),
one tact (two pixels; four clocks at 14 MHz) early relative to both, and its own spec §5 says "≈ tact 0 of the next
line". Raster effects driven by the line INT are shifted. Fix: event at `224 n` (n = 1..320, the last wrapping to tact 0
of the next frame, which the rollover already handles). Suggested test change: `INT3_LineInterrupts` expects the first
line INT at tact 224 (or 0 of the next frame for line 319) and `INT4` places the coincident frame/line pair at tact 224
with `HS_INT = 0` of line 1.

**Row 20 - acknowledge picks the source at the sampling instant (edge, low).** The RTL latches `int_sel` at
`intack_s`, about three clocks after the CPU sampled /INT; a frame pulse that ends in that window is gone (`intctr_fin`)
and a lower source (or, with nothing pending, the previous `int_sel`) supplies the vector. Unreal re-evaluates the
frame pulse 3 tacts later in `IntVec`. unreal-ng serves the frame vector `#FF` and leaves the line/DMA latch for the
next acknowledge. Only observable when /INT is sampled in the last ~3 clocks of the frame pulse. Suggested test
`INT11_PulseEndingBeforeTheAcknowledge`: line pending, frame pulse sampled at its 31st clock, acknowledge at t + 3;
assert vector `#FD` and the frame latch cleared.

**Row 25 - frame pulse across a clock switch (low).** RTL counts CPU clock edges, so 10 clocks at 3.5 MHz leave 22
clocks at 14 MHz. unreal-ng (and Unreal) measure elapsed raster time times the new multiplier: after 10 clocks at
3.5 MHz the switch to 14 MHz makes it 40 elapsed clocks and the pulse ends at once. Rare (a SYS_CONFIG write inside
the first 32 clocks after the frame event). Suggested test `CLK3_SwitchInsideThePulseKeepsTheClockCount`: event at
tact 1, switch to 14 MHz at clock 11, assert /INT until 22 more 14 MHz clocks, not beyond.

**Row 31 - cache filled while disabled (documented divergence, low).** The RTL fills an entry on every CPU DRAM read
even with `CACHE_CONFIG = 0`; enabling the cache later can return pre-DMA data. unreal-ng neither fills nor keeps
entries while every window is off (documented in `tsconfmemory.cpp`). Suggested test
`CCH3_EntriesFilledWhileOffSurviveADma`: cache off, read `#C000`, DMA-write that word, enable the cache, read: expect
the old byte (RTL) - or keep the divergence and record it in hardware-spec §12.

**Row 33 - 14 MHz read wait (unclear).** unreal-ng uses +4..+7 fclk for a data read, against the `zmem.v` comment
table (+2..+5), from its reading of `stall14_fin` (released at `c2` for reads, `c1` for M1). Note `opfetch` implies
`memrd`, so the `(opfetch && c1) || (memrd && c2)` release also lets an M1 go at `c2` - worth re-checking. Suggested
check: run `fpga/current/tb` with a 14 MHz `LD A,(HL)` loop and compare the stall count with
`TIM1_UncachedRamWaitsAt14MHz` (`32 * 12` clocks).

**Row 35 - 3.5/7 MHz DRAM stall (unclear).** `stall357` exists in RTL; unreal-ng never applies arbiter waits below
14 MHz. By analysis no mode makes `cpu_next = 0` at a 7 MHz request, but this is unproven. Suggested test
`ARB6_SevenMegahertzNeverStalls`: run `TsConfArbiter::CpuAccess` at 7 MHz request spacing (one per 12 fclk minimum)
in all four modes inside the fetch window and assert zero wait; if it can be non-zero, apply the arbiter at 7 MHz too.

**Row 37 - DOS-entry / vdos-exit stall missing (gap).** `zclock.v` stops the clock about 4-5 fclk on `dos_on` (the
`#3Dxx` fetch that maps TR-DOS) and on `vdos_off`, at every speed. Neither emulator has it; the ATM3 decoder in
unreal-ng already models the same stall (`kDosEntryStallTicks`). Suggested test `TIM4_DosEntryStall`: fetch from
`#3D2F` with ROM128 and mapping on, assert the instruction costs 4 fclk (2 clocks at 14 MHz, 128 counter ticks at
3.5 MHz) more than the same fetch with the trap off.

**Row 38 - IDE stall off by default (gap).** The RTL always stalls the CPU for the IDE bus cycle; unreal-ng models it
only with `[HDD] IdeStall=1`. Covered by `IDE4_Stall` when enabled; consider defaulting it on for TS-Conf, or record
the choice in hardware-spec §12.

**Row 39 - no /WAIT on `#BFF7` (gap).** The RTL holds /WAIT on every Gluk data-port access until the AVR answers
(`wait_start_gluclock`, `zwait.v`); this also freezes the frame pulse. unreal-ng and Unreal answer at once. Suggested
test `CMOS3_DataPortWaitsForTheAvr`: with EFF7 bit 7 set, `In(0xBFF7)` costs the AVR's service time in clocks (as
`ComPort::AddAccessWait` computes for `#xxEF`) and a frame pulse running across it keeps its remaining clocks.

**Row 40 - `#xxEF` waits only with a COM device attached (minor).** On hardware the AVR always answers `#xxEF` with a
/WAIT; unreal-ng adds the wait only through `ComPort`. Without a serial device the access is free. Suggested test
`COM2_NoDeviceStillWaits` if the owner wants it; otherwise record it.

**Row 41 - NMI on TS-Conf (low).** The TS-Conf FPGA never drives /NMI (`nmi_n = 1'bZ`); unreal-ng and Unreal deliver
the NMI button anyway. Either refuse it for TS-Conf (`RequestBoardNmi` returning "handled, nothing") or document it as
a host convenience. Suggested test `NMI1_TsConfHasNoNmi`: `RequestNMI()` on a TS-Conf emulator leaves PC unchanged.

**Rows with Test = none but verdict match (coverage gaps).** Row 4 (VSINTH bits 7:4 ignored): suggested
`INT2b_VsIntHHighBitsIgnored` - write `VSINTH = 0xF0`, assert one frame INT per frame at line 0. Row 23 (INT during
HALT on TS-Conf, incl. 14 MHz idle fetches from DRAM): suggested `INT12_HaltWakesAndReturnsPastHalt` - HALT in RAM with
EI, frame INT, assert return address = HALT + 1 and the wake-up within one 4-clock M1 of the event.
