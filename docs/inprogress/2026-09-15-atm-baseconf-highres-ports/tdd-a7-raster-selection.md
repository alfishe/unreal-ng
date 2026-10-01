# A-7: AVR raster selection (Pentagon / 60 Hz / 48K / 128K) — technical design

Date: 2026-10-01
Status: design only, nothing implemented. Gap row A-7 of [gap-analysis.md](gap-analysis.md); first analysis in
[tdd-e8-wrprot-font-pal444-dosstall.md](tdd-e8-wrprot-font-pal444-dosstall.md) §7.
Machine: ZX-Evo BaseConf, model `ATM3`. Decision Q1 stays: a board with an empty NVRAM runs the 48K raster
(69 888 T, today's frame).

Paths below are relative to the repository root. `pentevo/...` is the pentevo hardware checkout
(`fpga/base_trdemu/trunk` = the released BaseConf FPGA, `avr/baseconf/trunk/src` = its AVR firmware). Confidence
marks follow [research-zxevo.md](../2026-09-29-machine-waits/research-zxevo.md): **[H]** read in the RTL (and
simulated there), **[M]** read in the RTL, or derived by arithmetic from it, not simulated, **[L]** inferred.

## 1. What the feature is, in plain words

The video chip of the BaseConf board can run four different TV rasters. A raster fixes how long a frame is, where in
the frame the Z80 interrupt (INT) comes, and whether the CPU is slowed down by the video chip (contention):

| Name | Plays like | Who picks it |
|---|---|---|
| Pentagon | Pentagon 128: 320 lines, no contention | the user, with Scroll Lock on a PS/2 keyboard, stored in the AVR's NVRAM |
| 60 Hz | a 262-line NTSC-timed frame, no contention | same |
| 48K | Sinclair 48K: 312 lines, contention on | same |
| 128K | Sinclair 128K: 311 lines of 228 T, contention on | same |

The Z80 program cannot choose the raster (§3.3); it only sees its effect. Software written for Pentagon timing
(demos with cycle-exact border or multicolor effects) needs the Pentagon raster, and 48K or 128K software needs the
Sinclair ones. Today unreal-ng always runs the 48K raster, with no contention.

Terms used below: **fclk** = the 28 MHz FPGA clock; a **slot** = one 7 MHz video cycle (2 T at 3.5 MHz);
**hcount** counts slots in a line; **vcount** counts lines in a frame; **T** = one Z80 clock at 3.5 MHz.

## 2. Ground truth: the RTL

### 2.1 Numbers per raster

`modes_raster[1:0]` is an input of `video_sync_v`, `video_sync_h`, `video_palframe` and `zclock` (§2.2). Every
number below is a `localparam` or a comparison in the cited line.

| | `00` Pentagon | `01` 60 Hz | `10` 48K | `11` 128K | Evidence |
|---|---|---|---|---|---|
| Lines per frame | 320 | 262 | 312 | 311 | `video_sync_v.v:103-106` (`vperiod <= VPERIOD - 1`, `:136-141`) |
| Slots per line (T per line) | 448 (224) | 448 (224) | 448 (224) | 456 (228) | `video_sync_h.v:114-115`, `:147` |
| **Frame, T at 3.5 MHz** | **71 680** | **58 688** | **69 888** | **70 908** | lines x T per line |
| Frame period | 20.480 ms | 16.768 ms | 19.968 ms | 20.259 ms (20 259.4 us) | / 3.5 MHz (fclk 28 MHz / 8) |
| Frame rate | 48.83 Hz | 59.64 Hz | 50.08 Hz | 49.36 Hz | |
| INT line (vcount) | 0 | 0 | 1 | 1 | `video_sync_v.v:109-111`, `:193` |
| INT slot in the line (hcount) | 2 | 2 | 126 | 130 | `video_sync_h.v:109-111`, `:234` |
| INT tact in the RTL frame (T) | about 1 | about 1 | 287 | 293 | line x T per line + slot / 2 **[M]**, +-1 T |
| INT length | 256 fclk = 32 T at 3.5 MHz, in every raster | | | | `zint.v:57-78` (`intctr[8]`), see 2.3 |
| Contention | none | none | 48K pattern | 128K pattern | `zclock.v:282` (`modes_raster[1]`) |
| Contention at 7 / 14 MHz | none | none | none | none | `zclock.v:282` (`!int_turbo`) |
| Paper lines (vpix, ZX modes) | 81..272 | 47..238 | 65..256 | 64..255 | `video_sync_v.v:82-92`, `:201-207` (begin + 4 in ZX modes) |
| Border latched every 4 T | no | no | yes | yes | `video_palframe.v:91-94`, `video_top.v:379` (`border_sync_ena = modes_raster[1]`) |
| Vertical blank, sync (lines) | blank 0..32, sync 8..11 | blank 0..22, sync 4..7 | as Pentagon | as Pentagon | `video_sync_v.v:64-70`, `:176`, `:183` |

Notes on the table:

- **INT to first paper pixel**, by arithmetic on the RTL (`vcount` changes at `hsync_start`, hcount 10; paper
  pixels start at `hpix`, hcount 140 in ZX modes, `video_sync_h.v:92`, `:243`): Pentagon 17 989 T, 60 Hz 10 373 T,
  48K 14 343 T, 128K 14 369 T **[M]**, +-1 T for the slot phase. The matching unreal-ng constants for the Sinclair
  and Pentagon machines are 14 340, 14 366 and 17 988 (`core/src/emulator/config.cpp:1100-1190`,
  `ApplyModelTimingDefaults` comment): the RTL arithmetic is 1-3 T above them, which is inside the uncertainty of the
  pixel pipeline. For the Sinclair rasters the simulated contention onset after INT is 14 335 (48K) and 14 361
  (128K), equal to the real machines (research-zxevo.md §B.3, **[H]** for the number, **[M]** for the clock phase).
  No hardware measurement exists for the 60 Hz raster.
- **ATM video modes** (non-ZX): the paper window starts 4 lines earlier and ends 4 lines later in the line gate, so
  the paper is 200 lines (`video_sync_v.v:201-207`, `mode_atm_n_pent`); hpix starts at hcount 108
  (`video_sync_h.v:96`). The frame length and INT do not depend on the video mode **[H]**.
- **60 Hz** is 262 lines at 224 T: a 16.768 ms frame, 59.64 Hz (not exactly 60). The blank and sync lines differ
  from the 50 Hz rasters (`:68-70`), which only matters for a signal-level display.
- **`video_palframe.v:98`**: ULA+ colors are used only in the 128K raster (`up_ena & modes_raster == 2'b11`); in the
  other rasters the mux picks the ZX/ATM color index. **[M]**, the consumer of `up_ena` was not traced (open
  question O-6).
- **`video_palframe.v:156-160`**: a 2-bit frame counter `phase` runs only in the 48K raster. It drives the 3-bit to
  2-bit dither of the VGA output (`video_palframe_mk3bit`), an analog detail with no effect on the emulated machine.

### 2.2 Contention, exactly

`zclock.v:267-282` (a copy with a longer analysis is in research-zxevo.md §B):

```verilog
assign contend_addr = (modes_raster[0]==1'b0) ? ( a[15:14]==2'b01 ) :                          // 48k mode
                                                ( a[15:14]==2'b01 || (a[15:14]==2'b11 && p7ffd[0]) ) ;
assign contend_wait = contend && (contend_mem || contend_io) && !int_turbo && modes_raster[1] && mode_contend_ena;
```

- Contention applies only in rasters `10` and `11`, and only at 3.5 MHz. `mode_contend_ena` is tied to 1 and
  `mode_contend_type` to 0 (`top.v:189-190`), so the +2A/+3 pattern is dead code **[H]**.
- The contended address is `#4000-#7FFF` (whatever is mapped there, even ROM, even under ATM paging); the 128K
  raster adds `#C000-#FFFF` **when bit 0 of the `#7FFD` register is 1**, not by the page the pager really maps
  **[H]** (research-zxevo.md §B.1).
- The 6,5,4,3,2,1,0,0 pattern: `contend_ctr` restarts at hcount 127 each line (`video_sync_h.v:118`, `:255-261`)
  and runs 128 T, on lines where `vpix` is set (`:266`). So contention follows the **line gate** (192 lines in ZX
  modes, 200 lines from 4 lines earlier in ATM modes), not the horizontal paper start: in an ATM mode the contended
  columns stay where they are while the paper starts earlier (hcount 108 against 127) **[M]**.
- A port whose high byte is `#40-#7F` is contended on all four T even when A0 = 0 (sim, research-zxevo.md §B.4).

### 2.3 INT length at the other CPU clocks

`zint.v:57-78`: the pulse is a counter of fclk, 256 long, frozen while /WAIT is low; it ends early on INTACK
(`:69`). 256 fclk = 32 T at 3.5 MHz, 64 T at 7 MHz, 128 T at 14 MHz. unreal-ng already scales `intstart` and
`intlen` by the clock multiplier (`Z80::RecomputeFrameTiming`, `core/src/emulator/cpu/z80.cpp:647-662`) and ends the
pulse on acknowledge on ATM3 (`core/tests/emulator/cpu/int_test.cpp:333-396`). No change needed for this, **[H]**.

### 2.4 What happens in the RTL when the raster changes while running

- `vperiod`, `vpix_beg`, `vpix_end` are plain registered functions of `modes_raster` (`video_sync_v.v:135-157`), and
  `vcount` is **not** reset: the new length applies at the next compare. `vcount` is 9 bits: switching from a
  longer to a shorter raster while `vcount` is already above the new `vperiod - 1` makes `vcount == vperiod` fail
  until the counter wraps at 512, so that frame is extra long (up to 512 lines) **[M]**, by reading, not simulated.
  Switching to 128K changes the line length from 448 to 456 slots mid-line in the same way
  (`video_sync_h.v:147`).
- The Z80 is not reset; the clock phase `hcount` keeps running; the AVR only writes SPI config register `#50`
  (`zx.c:650-656`).

unreal-ng will not copy the transient (decision RS-D6): the change applies cleanly at a frame boundary.

### 2.5 Cross-check with other emulators

| Emulator | What it models | Evidence |
|---|---|---|
| xpeccy-plus | one fixed raster for BaseConf: layout `ULA.Evo`, 448 x 320 dots, INT at line 0, dot 2 = the RTL Pentagon raster; the AVR raster bits read back as Pentagon | `res/layouts.conf:12`, `src/libxpeccy/hardware/pentevo.c:431-436` |
| ZXMAK2 `UlaPentEvo` | Pentagon only: 71 680 T, first paper line 80, first paper tact 65 (80 x 224 + 65 = 17 985), INT at 0, length 32 | `src/ZXMAK2.Hardware/Evo/UlaPentEvo.cs:20-33` |
| zx-evo-unreal (Unreal fork) | default `Frame=71680`; a `PRESET.ATM1_2_3.5MHz` of 69 888 T for the older ATM boards; no per-raster selection found | `Unreal/cfg/Unreal.ini:118,137` |
| unreal-ng today | 69 888 T (48K raster) always, INT at 1756, no contention | `core/src/emulator/config.cpp:1178-1190`, `:1246-1252` |

No emulator in the collection implements the four rasters; the xpeccy-plus Pentagon INT position (line 0, dot 2)
agrees with the RTL `INT_BEG`/`HINT_BEG` (`video_sync_v.v:109`, `video_sync_h.v:109`). So the RTL is the only source
for 48K, 60 Hz and 128K. **Not determined**: the AVR's value on a board whose NVRAM was never written (the firmware
reads cell `#FE` without a validity check, `rtc.c:203`), the exact INT-to-pixel pipeline delay (+-3 T), and the 60 Hz
INT-to-paper distance on real hardware.

## 3. How the raster is chosen on the board

### 3.1 FPGA side

`modes_raster` is `config0[5:4]` of the AVR-to-FPGA SPI register `#50` (`top.v:789`, `slave/slavespi.v:205`). It is
not a Z80 port and not in `#xxBD` readback.

### 3.2 AVR side (firmware)

- `modes_register` bits: 0 VGA, 1 tape-out, 2 Caps LED, 5:4 raster (`avr/baseconf/trunk/src/main.h:148-155`).
- Power-on: `modes_register = 0` (`main.c:213`), then restored from NVRAM cell `#FE` (`RTC_COMMON_MODE_REG`,
  `rtc.h:25`) with the Caps LED bit cleared (`rtc.c:203`), then `zx_set_config(0)` sends it (`rtc.c:206`).
- Runtime: the **Scroll Lock** key (PS/2 `0x7E`, `zx.c:395-405`) adds 1 to the 3-bit field {raster[5:4], VGA[0]}, so it
  cycles TV/VGA x Pentagon, 60 Hz, 48K, 128K (8 states); `zx_mode_switcher` (`zx.c:635-648`) flips the changed bits,
  sends the configuration and **saves `modes_register` to NVRAM `#FE` at once** (`zx.c:644`). Lines 411 and 417 call it again (they sit in the same key handler; not
  analyzed further).
- A hard reset (Ctrl+Alt+Del, F12 hold) restarts the firmware (`main.c:270-272`, `atx.c:117`) and reloads the saved
  value, so it is unchanged by a reset.
- Read-back for the Z80: Gluk extension type 3 `EXT_TYPE_RDCFG` returns `modes_register` at index 0
  (`version.c:37-40`); the Z80 cannot write it. (research-zxevo.md §C.2: the gluk emulation exposes cells only up
  to `#EF`.)

So on the real board the raster changes at **host-keyboard speed, at any instant, with the Z80 running**.

## 4. unreal-ng today (checked against the working tree at 5a076651 plus uncommitted ATM work)

| Piece | What it does | Where |
|---|---|---|
| Frame length, INT, line | one `config.frame` / `t_line` / `intstart` / `intlen`, read from the INI at start and overwritten by `ApplyModelTimingDefaults`; ATM3 = 69 888 / 224 / 1756 / 32 | `core/src/emulator/config.cpp:315-319`, `:1100-1268` (ATM3 `:1178-1190`, `:1246-1252`) |
| Frame limit in the CPU | `_frameLimit`, `_intStart`, `_intEnd` are members rebuilt by `Z80::RecomputeFrameTiming()` at every `BeginFrame` and on every hardware turbo change | `core/src/emulator/cpu/z80.cpp:647-672`, `:863-890` |
| Frame rollover | `Core::AdjustFrameCounters` subtracts `config.frame * multiplier` from `z80.t`; `MainLoop::OnFrameEnd` adds `config.frame` to `t_states` | `core/src/emulator/cpu/core.cpp:996-1030`, `core/src/emulator/mainloop.cpp:643-647` |
| Frame boundary order | `CompleteFrame`: `OnFrameEnd`, `OnFrameStart` (screen `InitFrame`), `Z80::BeginFrame`, then pending media, TTD checkpoint, input queues | `core/src/emulator/mainloop.cpp:409-500` |
| Frame pacing | `config.frame_duration_us` is read every loop (`CalculateFrameDurationUs`, rounded up to whole us) | `core/src/emulator/mainloop.cpp:212-218`, `core/src/emulator/config.cpp:319`, `:1263` |
| Audio | samples per frame come from `config.frame` with an exact carry accumulator, so they follow a frame change at the next frame with no code change; ring target 40 ms, emergency refill 15 ms | `core/src/emulator/sound/soundmanager.cpp:780-818`, `soundmanager.h:205-216` |
| Raster descriptors | static table `rasterDescriptors[M_MAX]` (pixels, 8/16 vSync lines, 15/16 vBlank lines); ATM3 AlCo modes `M_P16` / `M_PMC` use the `M_ZX48` row for timing through the single override `Screen::GetTimingDescriptor` | `core/src/emulator/video/screen.h:467-530`, `screen.cpp:1316-1323` |
| Users of that override | `SetVideoMode` (`screen.cpp:483`), `ScreenZX::CreateTstateLUT` (`screenzx.cpp:208`), `:513`, `VideoMapService` (`videomapservice.cpp:110`), device state (`devicestatevideo.cpp:161`) | |
| Direct users of the static rows | about 25 sites read `rasterDescriptors[_mode]` (`screenzx.cpp:117`, `:545`, `:762-801`, `:915`, `:997`, `:1092`, `:1196`, `:1258`, `:1295`, `:1376`; `screencapture.cpp:78`; `zxpolygroup.cpp:1227,1723,1758`) | |
| Detected video mode | `DetectModeATM3` -> `M_ZX48`, `M_P16`, `M_PMC`, `M_ATM16/HR/TX/TL`; `InitRaster` calls `SetVideoMode` only when the mode changed | `screen.cpp:395-440`, `:166-215` |
| Contention | ATM3 is never contended: `SetVideoMode` enables it for the Sinclair models only (`ferranti`), `UlaContention` rule `None` | `screen.cpp:557-574`, `core/src/emulator/video/ulacontention.h:41-48` |
| Contention memory interface | `Core::SelectMemoryInterface` picks plain or contended read/write paths; run at every `CPUFrameCycle` and at mode set | `core/src/emulator/cpu/core.cpp:629-657`, `:976-983` |
| Slot contention cache | per mapped RAM page, ROM never contended | `core/src/emulator/memory/memory.cpp:1658-1669` |
| Border update | `borderUpdateTStates` = 4 for the Sinclair models, 1 for ATM3 | `screen.cpp:557-574` |
| AVR modes register | stored nowhere: the extension read answers the constant `kModesRaster48K` (0x20) with the Caps LED and tape-out bits; Scroll Lock is not handled | `core/src/emulator/memory/atm/evoavr.h:66-67`, `evoavr.cpp:136-146`, `:217` |
| NVRAM | `[EVO] NvramFile`: 256 cells + 4 KiB EEPROM; the loader keeps cells `#0E-#EF`; saved at machine destruction | `evoavr.cpp:366-394`, `core/src/emulator/ports/models/portdecoder_atm3.cpp:74-76`, `:111-116`, `config.cpp:284-285` |
| EvoAvr users | ATM3 **and TSConf** (the TS-Conf decoder owns an `EvoAvr` too) | `portdecoder_tsconf.cpp:105`, `:175` |
| TTD | frame position in units of `config.frame x clock units`; `FrameSpan()` = `config.frame * ttd_clock_units`, constant per session; `GlobalT = frame * FrameSpan + tInFrame`; chipset struct has no raster; model state travels in peripheral blobs (ids 0-22) | `core/src/debugger/ttd/timetravelmanager.cpp:2178-2184`, `core/src/debugger/ttd/timetravelmanager.h:830-846`, `ttdserializable.h:54-68`, `ttd.ksy:520-545` (same folder) |
| TTD fixtures | the corpus holds Pentagon 128K and TS-Conf sessions only: no ATM3 `.ttd` fixture exists | `testdata/ttd/README.md` |

Two things in the existing code matter for the design: a frame-boundary slot that already applies queued
changes before the checkpoint (`MediaManager::ApplyPending`, `mainloop.cpp:455-457`), and the INT, frame limit and
clock being derived from `config` in one cold function (`RecomputeFrameTiming`).

## 5. Design

### 5.1 One raster profile per selection

A small table, one row per `modes_raster` value, in a new header (`core/src/emulator/memory/atm/evoraster.h`, named
after what it describes like its neighbors). The T numbers are in unreal-ng's frame coordinates, calibrated by the
same invariant `ApplyModelTimingDefaults` already uses for every model: INT fires at `intstart + 1`, and
`intstart + 1 = paperStartT - distance`. The distances are the sister-machine values (§2.1 note), so the Sinclair
rasters agree with the real Sinclair machines the RTL reproduces.

| Raster | `frame` | `t_line` | Lines | Descriptor rows (timing) | Distance INT to paper | `intstart` | `intlen` | Contention | Border step | `frame_duration_us` |
|---|---|---|---|---|---|---|---|---|---|---|
| `00` Pentagon | 71 680 | 224 | 320 | `M_PENTAGON128K` (16 + 16 + 288) | 17 988 | 71 635 | 32 | off | 1 T | 20 480 |
| `01` 60 Hz | 58 688 | 224 | 262 | **new**: vSync 4, vBlank 18, visible 240 (top border 25, paper 192, bottom 23) | 10 372 **[M]** | 179 | 32 | off | 1 T | 16 768 |
| `10` 48K (Q1 default) | 69 888 | 224 | 312 | `M_ZX48` (8 + 16 + 288) | 14 340 | 1811 | 32 | on at 3.5 MHz | 4 T | 19 968 |
| `11` 128K | 70 908 | 228 | 311 | `M_ZX128` (8 + 15 + 288, 456 px per line) | 14 366 | 1845 | **32** (RTL; the ZX-128K model uses 36) | on at 3.5 MHz | 4 T | 20 260 |

- 60 Hz: paper starts at line 22 + 25 = 47 (RTL `vpix` from line 47, `video_sync_v.v:85`), T = 47 x 224 + 24 = 10 552;
  `intstart = 10 552 - 10 372 - 1 = 179`. The distance is the RTL arithmetic (10 373) minus 1 T, the same offset the
  Pentagon uses (INT line 0, slot 2 like Pentagon). No reference emulator or measurement exists: **[M]**, +-3 T.
- 48K row: `intstart` 1811 replaces today's 1756 (see RS-D2).
- `frame_duration_us` is `CalculateFrameDurationUs(frame)` (`mainloop.cpp:212`, rounded up), listed for tests.
- A table row also carries the RTL `INT line/slot` and `lines` for the information API and the tests.

### 5.2 The setting: where the raster lives and how it is chosen

State owner: `EvoAvr` gets the modes register (`uint8_t _modes`), with these rules (all in the AVR firmware's
terms):

1. **Power-on value**, in this order: the INI key `[EVO] Raster = 48K | 128K | Pentagon | 60Hz` if present; else
   the NVRAM file's raster byte; else **48K** (Q1).
2. **NVRAM file format**: the file keeps its 256 + 4096 byte image, then gets an optional 16-byte trailer
   `"EVOMODE1"` + `modes_register` + 7 reserved zero bytes (RS-D4). An older file has no trailer and loads as 48K; a
   newer file read by an older build is accepted because `LoadNvram` reads only the first 4352 bytes
   (`evoavr.cpp:366-377`). Cell `#FE` of the image is not used: old files hold zero there, which would read as
   Pentagon and silently change every existing board.
3. **Scroll Lock** (`PcKey::ScrollLock`, `core/src/emulator/io/keyboard/pckey.h:49`) delivered by the existing PS/2
   sink into `EvoAvr::OnPcKey`: on a press, the 3-bit field {raster, VGA} is incremented exactly as `zx.c:395-405`
   (bit 0 is stored and read back; the emulator has no scan doubler to switch). The key never becomes a ZX key.
4. **Automation** sets the raster directly (`EvoAvr::SetRaster(value)`), the same code path as the key.
5. A change fires a handler (like `SetResetHandler`) that stores a **pending raster** in a cell the machine loop
   reads at the next frame boundary (5.3). The NVRAM trailer is rewritten at machine destruction as today.
6. `kExtModes` read (`evoavr.cpp:136-146`) returns the live register.

Z80 reset and hard reset leave the register unchanged (§3.2).

TSConf is untouched: its decoder owns an `EvoAvr` too, so the raster handler is installed by `PortDecoder_ATM3`
only, and the TS-Conf model keeps its fixed Pentagon raster (`config.cpp:1253-1258`).

### 5.3 Runtime change, at a frame boundary

The change is applied by one function, `MainLoop::ApplyPendingRaster()`, called in `CompleteFrame` **between
`OnFrameEnd()` and `OnFrameStart()`** (`mainloop.cpp:409-421`):

- Before it, the closed frame has been rendered, its audio mixed and `t_states += config.frame` done with the old
  geometry; after it, `OnFrameStart` -> `Screen::InitFrame` -> `InitRaster` and `Z80::BeginFrame` ->
  `RecomputeFrameTiming` already see the new values, and the TTD checkpoint of the new frame (taken later in
  `CompleteFrame`) holds the new raster, as the comment at `mainloop.cpp:440-447` requires.
- It does: set `config.frame`, `t_line`, `intstart`, `intlen`, `frame_duration_us` from the profile; set
  `EmulatorState::evoRaster` (2 bits; the applied raster, next to `evoFddMask`, `platform.h:1189`); rebuild the
  raster descriptors (5.5); **force** `Screen::SetVideoMode` (today `InitRaster` skips it when the mode is
  unchanged, `screen.cpp:210-215`), because the ULA contention snapshot (`ContentionRaster`,
  `screen.cpp:585-593`) and `_rasterState` are rebuilt there; re-select the memory interface (5.6); post
  `NC_VIDEO_MODE_CHANGED` when the framebuffer size changed (60 Hz), which the existing size-changing mode switches
  already handle (`screen.cpp:611-624`).
- Why a boundary, not mid-frame: `z80.t`, `Z80::_frameLimit`, `AdjustFrameCounters` and the per-t-state LUT all
  assume one frame length per frame; the real transient of §2.4 is not reproducible by software anyway (the raster
  is a keyboard action).
- The pending cell is `std::atomic<uint8_t>` (0xFF = none) so the WebAPI thread can request a change; the key path
  runs on the emulation thread and uses the same cell. Precedent: the network setting that "applies at the next
  frame boundary" (`core/automation/python/src/emulator/python_emulator.h:1849`, `Core::OnNetworkFrame`,
  `mainloop.cpp:494`).

### 5.4 INT position

No new INT code. `Z80::RecomputeFrameTiming` rebuilds `_frameLimit`, `_intStart` and `_intEnd` from `config` at each
`BeginFrame`, scaled by the clock multiplier, and handles a pulse that straddles the frame end (`_intWraps`,
`z80.cpp:657-661`). The Pentagon row has `intstart + intlen` = 71 667 < 71 680: no wrap. The first frame after a
change starts with `int_pending = false` (`core.cpp:1011-1029` clears a stale latch at the old boundary). The RTL
pulse is 32 T at 3.5 MHz in all rasters (§2.3), so `intlen` = 32 everywhere (RS-D3).

### 5.5 Screen descriptors

The Screen needs a per-raster view of the descriptor rows that today are a `const` static table, with 25 direct
readers. Decision RS-D5: make `rasterDescriptors` a per-Screen array initialised from a static base table
(`kBaseDescriptors`), with one function `Screen::ApplyRaster(kind)` that rewrites the rows of the ATM3 family:

- **Timing fields** (`pixelsPerLine`, `vSyncLines`, `vBlankLines`, `fullFrameHeight`, `screenOffsetTop`) come from
  the raster row of 5.1 for every ATM3 mode, including the ATM text/graphics modes (`M_ATM16/HR/TX/TL`, whose own
  rows keep their width, 44-line top border and 200-line paper): the frame length is the same in every video mode
  **[H]** (§2.1), the RTL gate makes the paper window 4 lines earlier in them (`video_sync_v.v:201-207`), which
  the existing 44 versus 48 top border already encodes.
- `GetTimingDescriptor` loses its `atm3AlcoTiming` special case (`screen.cpp:1316-1323`): `M_P16` / `M_PMC` take the
  selected raster like every other ATM3 mode (their static rows stay Pentagon-like for the other machines).
- 60 Hz is a new geometry: 352 x 240 framebuffer (RS-D7), line 47 paper. `ScreenZX::MAX_FRAME_TSTATES` (71 680,
  `screenzx.h:49`) already covers the longest raster (Pentagon, exactly). `CreateTstateLUT` runs again on the forced
  `SetVideoMode` (it is rebuilt per mode change already, `screenzx.cpp:195-219`).
- The Pentagon and 60 Hz rasters use the 1 T border step; 48K and 128K the 4 T step
  (`video_palframe.v:91-94`) through `_rasterState.borderUpdateTStates`
  (`screen.cpp:557-574`; today the `ferranti` test fixes ATM3 at 1).
- ULA+ visibility only in the 128K raster is **not** part of this design until O-6 is answered.

### 5.6 Contention

Today ATM3 has none. After this change it is on in the 48K and 128K rasters while the CPU runs at 3.5 MHz, because
the RTL has it (§2.2), including in the default 48K raster. This is a behavior change for default ATM3
(RS-D9); at the reset clock (7 MHz, [baseconf-hardware-reference.md](baseconf-hardware-reference.md) §A.11) nothing is contended, so the board BIOS boots unchanged
and only software that drops to 3.5 MHz (`#EFF7` bit 4 set, `top.v:401`) sees it.

Mechanism, designed so every other machine pays nothing (AGENTS.md performance rule):

1. **Gate** `contended(ATM3) = raster in {48K, 128K} && clock multiplier == 1`, evaluated on **cold** events only:
   a raster change (5.3) and a clock change (`PortDecoder_ATM3::OnMachineM1` -> `ApplyHardwareTurboNow`,
   `portdecoder_atm3.cpp:1160-1165`), each ending in `Core::SelectMemoryInterface` (it already chooses the plain or the
   contended read/write interface per frame, `core.cpp:629-657`). The mutex there is acceptable at clock-switch
   rate; if a benchmark shows a cost (BIOS switching clock often), the fallback is a multiplier compare inside the
   already-contended path (RS-D8).
2. **Rule** by raster: `Ula48` for 48K (rule chosen by line length < 228, `ulacontention.h:132`) and `Ula128` for
   128K, with the two RTL differences from the Sinclair rules as ATM3 variants:
   - slot contention by **address**, not by mapped page: slot 1 always, slot 3 only in the 128K raster and only when
     the `#7FFD` bit 0 is set (`zclock.v:276-277`); ROM in slot 1 is contended too. The slot cache
     (`memory.cpp:1658-1669`) therefore needs an ATM3 rule and a refresh on every `#7FFD` write (cold, only ATM3 in
     the 128K raster);
   - a port with a contended high byte (`#40-#7F`) and A0 = 0 is `C:1` on all four T (research-zxevo.md §B.4).
3. **Raster of the pattern**: the contention window follows the line gate of RTL, not the visible paper: the
   `ContentionRaster` pushed for ATM3 is built from the raster profile in ZX-mode geometry (192 lines; 200 lines
   starting 4 earlier in ATM modes) and `CONTEND_START` 127 (T 63.5), independent of the video mode's own paper
   column (`video_sync_h.v:118`, `:257`, `:266`). The onset after INT must come out as 14 335 (48K) and 14 361
   (128K) per research-zxevo.md §B.3; the ATM-mode window is **[M]**, covered by test RS-T12.
4. Pentagon and 60 Hz: `SetContentionEnabled(false)`, as today.
5. The existing floating-bus code in `UlaContention` keeps its Ferranti/discrete type from the raster
   (`SetFetchType`): the 48K and 128K rasters are Ferranti-like, Pentagon and 60 Hz discrete **[L]**: the RTL has no
   floating-bus ULA behavior to check (BaseConf answers `#FF` itself), so this part keeps today's ATM3 behavior in
   all rasters (RS-D10).

Cost: zero per instruction on non-ATM3 machines (no new check in any shared loop); on ATM3 at 7 or 14 MHz or in the
Pentagon or 60 Hz rasters the plain interface stays, so ATM3 pays only in the 3.5 MHz Sinclair-raster case, where
the contended interface is the same one the Sinclair models use. A benchmark A/B is required before merge:
`core-benchmarks` frame loop for ATM3 at 3.5 MHz, plain versus contended, and a Sinclair/Pentagon control run.

### 5.7 Pacing, audio and the other frame-length readers

- **Pacer**: reads `frame_duration_us` per loop (`mainloop.cpp:218`): follows with no change. The two-frame
  re-anchor rule (`:236`) means a change from 20.5 to 16.8 ms needs no special case.
- **Audio**: the carry accumulator uses `config.frame` per frame (`soundmanager.cpp:801-804`): 60 Hz gives 739.4
  samples per frame at 44.1 kHz (58 688 x 44 100 / 3 500 000); the buffers hold 8192 (`audio.h:69`). The ring
  target (40 ms) is above every frame length; the emergency refill threshold (15 ms) is below the shortest frame
  (16.8 ms): test RS-T9 checks a 60 Hz run does not trigger a refill burst and a change does not cause an audio
  hard resync.
- **Other readers of `config.frame`** that run across a boundary and must be checked in phase R2 (list, not yet
  audited line by line): beeper and Covox frame duration (`beeper.h:128`, `covox.cpp:121`), TurboSound FM
  (`soundchip_turbosoundfm.cpp:379`), MoonSound / GS budgets, `Emulator` step helpers (`emulator.cpp:2376`, `:2974`),
  `Screen` tolerance (`screen.cpp:1672`), device state telemetry (`devicestatevideo.cpp:151-217`, which reports
  `cpu_hz = frame * intfq`: `intfq` stays 50, so the figure must come from the frame length and period instead),
  the flash toggle interval (`devicestate.cpp:1500`), ZX-Poly (`zxpolygroup.cpp:1143`; not an ATM3 concern).
- **Recording**: the encoder frame rate follows the pacing clock; a recording that spans a raster change changes its
  frame period mid-file. Phase R6 states the behavior (recordings keep the rate they started at and the audio
  carries on by sample count; a raster change during a recording is refused with a message, like other settings
  that would change the geometry). **[L]**, depends on the recorder design, not read here.

### 5.8 TTD (time travel)

The raster is chipset state and a host input.

- **State blob**: new peripheral id **23 `EvoRaster`** (additive, `ttdserializable.h`, `ttd.ksy` list):
  `u8 modesRegister` (the AVR's value, including bits 0-2), `u8 appliedRaster`, `u8 pendingRaster` (0xFF = none),
  `u8 reserved` = 4 bytes. It is registered only for ATM3. A reader that does not know the id keeps the bytes
  (`ttd.ksy:520-526`). A checkpoint without the blob (any older ATM3 recording) restores to 48K, the default.
- **Restore**: `RestoreCheckpoint` restores the blob **before** `RecomputeFrameTiming`
  (`timetravelmanager.cpp:1440-1445`, `:5944`) and calls the same apply function as 5.3 (without the frame
  boundary notifications), so a seek back across a raster change gets the right frame length, descriptors and
  memory interface.
- **Input**: the Scroll Lock press travels the existing `PcKey` journal (`TTDInputKind` 11,
  `ttd.ksy:140-146`), so a replay from checkpoint N applies it at the same (frame, t) and the apply at the boundary
  follows. An automation `SetRaster` is not a key: it is journaled as a new input kind **`RasterSelect`** (value in
  the `key` byte; additive, `ttd.ksy` kind list) so a replay reproduces it; until that lands (phase R5) a
  raster change by automation during a recording is refused (like loading media wipes the history).
- **`FrameSpan()`**: `GlobalT = frame * FrameSpan + tInFrame` assumes one frame length for the whole session
  (`timetravelmanager.cpp:2178-2184`, used at `:2582`, `:4797`, `:5045`, `:5158`, `:5342`, `:5429`, `:5511` and in
  `ttdbench.cpp:297,596`). With rasters it must stay unique and ascending, which a **constant span = the longest
  raster (71 680 x clock units)** guarantees, because `tInFrame` is always below the frame's own length (RS-D11).
  For ATM3 recordings in the 48K raster this changes `GlobalT` from `frame * 69 888` to `frame * 71 680`: the
  write/port journals store `globalT`, so ATM3 `.ttd` files recorded before this change are re-recorded (none are in
  the corpus). Each of the sites above is classified in phase R5 as "key" (keep the span) or "this frame's length"
  (use the current frame length) and has a test.
- **Fixture and golden impact**: no ATM3 file in `testdata/ttd` (README table). `core/tests/emulator/cpu/core_golden_test.cpp:67`
  (ATM3 row) and `core/tests/emulator/video/contentionregression_test.cpp:137-140` (four ATM3 rows) are timing
  sensitive: the INT moves 55 T (RS-D2), and contention switches on when the program is at 3.5 MHz. They are
  re-recorded in phase R7 with a note of which of the two moved them (check by switching each off, the method the
  E8 note at `core_golden_test.cpp:65-67` used). Also `int_timing_test.cpp:388-416` (ATM3 frame and INT) and
  `contention_test.cpp:859` (ATM3 "no contention" expectation) are updated.
- **Id numbering note (2026-10-01)**: the Kempston joystick landed first and took peripheral id **23** (`KempstonJoystick`,
  [tdd-kempston-joystick.md](tdd-kempston-joystick.md)), so the raster blob id above must shift to the next free
  number (24 at the time of writing; use the first free value in `ttdserializable.h`) and every "23" / "blob 23" in this
  section, RS-D11, RS-T14 and phase R5 follows it.

### 5.9 Automation parity

Every surface exposes the same setting, name `raster`, values `pentagon | 60hz | 48k | 128k`, read and write, plus the
information in device state (profile, applied, pending, frame T, period). Follows the pattern of the network
settings (applied at the next frame boundary, reply says "pending until the next frame"):

| Surface | Change |
|---|---|
| CLI | `settings raster [value]` (`cli-processor-settings.cpp`) and a line in the ATM3 state dump |
| WebAPI + OpenAPI | `GET/PUT/POST /api/v1/emulator/{id}/settings/raster` (`settings_api.cpp`), device state node with the profile numbers; `openapi.json` updated |
| MCP | the same settings route through `emulator_manage` / `invoke_api`; tool and resource docs updated |
| Lua | `emu:raster()` / `emu:setRaster(name)` binding |
| Python | `Emulator.raster` property and `set_raster(name)` |
| Qt | menu entry under the ZX-Evo machine settings (radio group of four) and a status-bar hint; Scroll Lock reaches the AVR through the PC key sink (verify the Qt key map forwards it) |
| Docs | `.recipe/machines/atm.md`, `docs/inprogress/.../README.md`, `TODO.md`, WebAPI/CLI/MCP/Lua/Python reference pages |

(The exact names above are the proposal; the existing neighbors set the final spelling in phase R6.)

## 6. Decisions

| ID | Decision | Alternative rejected | Why |
|---|---|---|---|
| RS-D1 | The four rasters are a data table (`evoraster.h`), one row each, the single source for `config`, descriptors and tests | per-raster `if`s spread over config, screen and CPU | one place to audit against the RTL table of §2.1 |
| RS-D2 | The 48K raster uses the RTL-confirmed INT position: `intstart` 1811 (INT to paper 14 340, the Sinclair 48K value; onset 14 335 per research-zxevo.md §B.3), replacing today's 1756 (14 395, taken from the original Unreal ATM preset for the older ATM boards) | keep 1756 | the RTL, not another emulator's preset, is the ground truth for BaseConf; costs a 55 T INT shift for default ATM3: **needs the owner's confirmation (open question O-2)** |
| RS-D3 | `intlen` 32 in all four rasters (RTL: 256 fclk) | 36 for 128K as the ZX-128K model | `zint.v` has one length |
| RS-D4 | Raster stored in an optional 16-byte trailer of the NVRAM file; INI `[EVO] Raster` overrides; blank = 48K (Q1) | NVRAM cell `#FE` like the AVR | old images hold 0 there (= Pentagon) |
| RS-D5 | `rasterDescriptors` becomes a per-Screen array built from a static base table, rewritten by `ApplyRaster` for the ATM3 family | override each of the 25 readers; a second table beside it | one array keeps every reader correct; the single override `GetTimingDescriptor` drops its special case |
| RS-D6 | A raster change applies at a frame boundary, between `OnFrameEnd` and `OnFrameStart`; the RTL's one long transient frame (§2.4) is not modeled | apply mid-frame | `z80.t`, frame limit, LUT assume one length per frame; software cannot trigger the change |
| RS-D7 | 60 Hz renders a native 352 x 240 framebuffer (25 + 192 + 23 lines) | pad to 288 lines | no invented pixels; the size-changing mode-switch notification already exists |
| RS-D8 | Contention memory interface re-selected on raster and clock changes (cold) | test the multiplier inside the contended path | zero cost on the hot path; fall back only if a benchmark says the mutex is visible |
| RS-D9 | Contention on in the 48K and 128K rasters at 3.5 MHz, including the default 48K raster | contention only after the user picks a raster | the RTL has it (`zclock.v:282`); the BIOS runs at 7 MHz and is untouched |
| RS-D10 | Floating-bus / `#FF` behavior of ATM3 stays as it is in all rasters | derive a Ferranti floating bus for 48K/128K | the RTL answers `#FF` itself; no evidence either way |
| RS-D11 | TTD `FrameSpan()` for ATM3 = the longest raster, constant; new blob 23 `EvoRaster`; new input kind `RasterSelect` | variable span with a per-checkpoint base | keeps `GlobalT` unique and ordered with no format-wide change; additive per `ttd.ksy` rules |
| RS-D12 | The raster handler is installed by `PortDecoder_ATM3` only; TSConf (which shares `EvoAvr`) ignores the setting | one setting for both machines | TS-Conf's raster is fixed by its own hardware |
| RS-D13 | A Z80 reset and the hard reset keep the raster | reset to the default | the AVR restores the saved value (`rtc.c:203`) |
| RS-D14 | The 4 T border step follows the raster (4 T in 48K/128K, 1 T in Pentagon/60 Hz) | keep 1 T | `video_palframe.v:91-94` |

## 7. Tests (written first)

Files, named after the unit under test: new `core/tests/emulator/memory/atm/evoraster_test.cpp` (table, setting),
additions to the existing `evoavr_test.cpp`, `int_timing_test.cpp`, `contention_test.cpp`, `screen_test.cpp`,
`ttdmodelstatecontract_test.cpp`, `ttd/atm/ttdevoraster_test.cpp` (new, TTD blob). Each runs on a real ATM3
emulator, the way the existing ATM3 tests do. Scratch files use `TestPathHelper::GetUniqueTestScratchPath()`;
waits use `TestWait`; boot-bound tests call `EnableTurboMode()` only when they do not assert pixels.

| ID | Test | Asserts |
|---|---|---|
| RS-T1 | Profile table | the four rows equal §2.1: lines x `t_line` = `frame` (71 680, 58 688, 69 888, 70 908), `frame_duration_us` (20 480, 16 768, 19 968, 20 260), `intlen` 32 |
| RS-T2 | Frame length per raster | after selecting each raster and one frame boundary, `z80.t` wraps at that `frame`; `GetFrameTStates()` = `frame x multiplier` at 3.5 / 7 / 14 MHz |
| RS-T3 | INT tact per raster | INT asserted in `[intstart + 1, intstart + intlen]` x multiplier; `intstart + 1` distance to paper start (`ScreenZX` LUT) = 17 988 / 10 372 / 14 340 / 14 366 |
| RS-T4 | INT length vs clock | 32 / 64 / 128 T at 3.5 / 7 / 14 MHz; ends at acknowledge (existing `int_test` helper) |
| RS-T5 | Descriptors | `GetTimingDescriptor` and the per-Screen rows give `maxFrameTiming` = `frame` for every raster x every ATM3 video mode (`M_ZX48`, `M_P16`, `M_PMC`, `M_ATM16`, `M_ATMHR`, `M_ATMTX`, `M_ATMTL`); 60 Hz framebuffer 352 x 240; `M_P16` no longer forced to the 312-line row |
| RS-T6 | Contention per raster | 48K: first contended T1 at 14 335 after INT, delay pattern 6,5,4,3,2,1,0,0; 128K: 14 361; Pentagon and 60 Hz: zero wait everywhere |
| RS-T7 | Contention vs clock | 48K raster: waits at 3.5 MHz, none at 7 and 14 MHz; a clock switch mid-frame re-selects the interface; stats count nothing at 7 MHz |
| RS-T8 | 128K slot rule | `#C000` slot contended iff `#7FFD` bit 0 (also with the pager mapping another page there); slot 1 contended with ROM mapped; `#40FE` I/O is `C:1` x4 |
| RS-T9 | Pacing and audio | `frame_duration_us` follows a switch; samples per frame accumulate exactly (739.4 / 903.2 / 880.6 / 893.4 at 44.1 kHz) over 1000 frames; no emergency refill, no hard resync at 60 Hz or on a switch |
| RS-T10 | Mode switch mid-run | run N frames in 48K, request Pentagon at a random T: the current frame keeps its length, the next frame is 71 680; INT moves; no stale second INT; framebuffer size change notification only for 60 Hz; repeat through all 12 ordered pairs |
| RS-T11 | Setting path | Scroll Lock cycles {raster, VGA} in the firmware order (8 states); `kExtModes` reads the live register; Z80 reset and hard reset keep it; TSConf ignores the key |
| RS-T12 | ATM-mode contention window | contention lines in an ATM mode start 4 lines earlier and the onset column stays 127 T-slot based, not the 108-slot paper start (**[M]**: the expected numbers come from the RTL read, flagged as such) |
| RS-T13 | Persistence | NVRAM trailer round trip; an old 4352-byte file loads as 48K; INI overrides NVRAM; a file written by the new build loads in the old reader's size check |
| RS-T14 | TTD blob and restore | serialize / deserialize blob 23; a seek back across a raster change restores raster, frame limit, descriptors, interface; a recording without the blob restores to 48K; `ttdmodelstatecontract_test` lists id 23 |
| RS-T15 | TTD replay | record a Scroll Lock change, seek back before it, replay: same frame lengths and `GlobalT` at every checkpoint; `RasterSelect` journal replays the same; an automation change during a recording before R5 is refused |
| RS-T16 | `FrameSpan` sites | every site of 5.8 gives the same answer in the 48K and the Pentagon raster for a fixed program (the classification test of phase R5) |
| RS-T17 | Golden impact | re-recorded golden row and contention rows each differ from the old ones only by the stated causes (INT shift, contention at 3.5 MHz); reproduce the old row by switching each cause off |
| RS-T18 | Other machines unchanged | the goldens of every non-ATM3 model are bit-identical; `TTD_Corpus_Test` (Pentagon, TS-Conf fixtures) passes unchanged |
| RS-T19 | Automation parity | one test per surface (CLI, WebAPI, MCP, Lua, Python) sets and reads each raster and sees `pending` until the boundary |
| RS-T20 | Benchmark | `core-benchmarks` frame loop, ATM3 3.5 MHz plain versus contended; Pentagon and a Sinclair model as control, run twice with `vm.loadavg` below 12 |

## 8. Risks

| ID | Risk | Mitigation |
|---|---|---|
| RK-1 | The unreal-ng INT-to-pixel constants differ from the RTL by 1-3 T and the 60 Hz distance has no reference | phase R0 extends the Verilator bench (`docs/inprogress/2026-09-29-machine-waits/tools/zxevo-rtl-sim`) to print INT start, first paper T and frame length per raster; constants checked against it before R2 |
| RK-2 | 55 T INT shift for default ATM3 (RS-D2) breaks a demo tuned to the old 1756 | O-2 decision first; both numbers are one line in the table; the old value is easy to restore |
| RK-3 | Default ATM3 gets contention at 3.5 MHz (RS-D9), slower programs | by hardware; the board BIOS stays at 7 MHz; documented in `.recipe/machines/atm.md` and TODO |
| RK-4 | `rasterDescriptors` has 25 readers; one still reads the static row | RS-D5 keeps the same member name; RS-T5 loops every raster x mode; grep gate in the review |
| RK-5 | Frame-length change breaks something that caches `config.frame` (analyzers, recording, SIMD LUT sizing) | the §5.7 list is audited in R2 with a test per reader; `MAX_FRAME_TSTATES` already equals the longest raster |
| RK-6 | TTD: `FrameSpan` sites misclassified, a seek lands on the wrong T after a raster change | RS-T15 and RS-T16; the first TTD phase refuses automation changes during a recording |
| RK-7 | The 60 Hz framebuffer size change reaches consumers that cache dimensions (Qt viewer, recording, video wall) | the mode-change notification exists for this (`screen.cpp:611-624`); RS-T5 and a viewer smoke test |
| RK-8 | EvoAvr is shared with TSConf; a raster side effect leaks into TS-Conf | RS-D12; RS-T11 asserts TSConf ignores it |
| RK-9 | A stale NVRAM file from a build without the trailer silently gives Pentagon | RS-D4 (trailer, not cell `#FE`); RS-T13 |
| RK-10 | Contention on ATM modes uses the line-gate rule (`[M]`) | RS-T12 labelled by confidence; owner can disable ATM-mode contention with one row flag |
| RK-11 | Cold `SelectMemoryInterface` takes a mutex at every clock switch | RS-T20; the fallback in RS-D8 |

## 9. Phased plan

| Phase | Content | Size | Depends on |
|---|---|---|---|
| R0 | Extend the RTL bench: INT start T, first paper T, frame length, contention onset per raster; check the §2.1 and §5.1 numbers; record the result here | S | none |
| R1 | `evoraster.h` table; `EvoAvr` modes register, Scroll Lock, live `kExtModes`, NVRAM trailer, INI key; RS-T1, T11, T13 | S-M | R0 |
| R2 | Frame-boundary apply: config, CPU geometry, `evoRaster` state, forced `SetVideoMode`, per-Screen descriptors for Pentagon, 48K, 128K; pacing and audio checks; the §5.7 reader audit; RS-T2-T5, T9, T10 | L | R1 |
| R3 | 60 Hz raster: descriptor, 352 x 240 framebuffer, notifications; tests for it | M | R2 |
| R4 | Contention: address-based slot rule, I/O rule, clock gate, `ContentionRaster` from the line gate, 4 T border; RS-T6-T8, T12, T20 | M-L | R2 |
| R5 | TTD: blob 23, `RasterSelect` input kind, restore order, `FrameSpan` constant and the site classification; RS-T14-T16 | M | R2 |
| R6 | Automation parity and Qt (§5.9), docs (`.recipe`, README, TODO, API references); RS-T19 | M | R2, R3 |
| R7 | Re-record the ATM3 golden and contention rows, update `int_timing_test` and `contention_test` expectations, prove the old rows return when each cause is off; RS-T17, T18; final verification log | S | R4, R5 |

Order for value: R1, R2 give the three 50 Hz-class rasters with no contention (Pentagon-timed demos work); R4 makes
48K/128K faithful; R3 and R5 are independent after R2. The per-commit checks (full build with zero warnings,
`core-tests`, mingw syntax check on new files, the Linux gcc image) follow AGENTS.md; nothing is committed without
an explicit request.

## 10. Open questions

| ID | Question | Recommendation |
|---|---|---|
| O-1 | Blank-NVRAM raster on real hardware is not determinable from the sources (no validity check in `rtc.c:203`) | keep Q1 (48K) |
| O-2 | RS-D2: move the 48K INT from 1756 to 1811 (RTL-consistent) or keep 1756 (current) | 1811, with the golden re-record in R7; ask the owner before R2 |
| O-3 | Contention on by default in the 48K raster (RS-D9) | yes, by hardware |
| O-4 | 60 Hz framebuffer 352 x 240 (RS-D7) versus padded 288 | 240 |
| O-5 | Automation `SetRaster` during a TTD recording: refuse (first) or journal (RS-D11, R5) | refuse until R5 |
| O-6 | `up_ena` gating in `video_palframe.v:98`: is ULA+ really invisible outside the 128K raster | trace `up_ena` in R0, then decide; excluded here |
| O-7 | Recording across a raster change (5.7) | refuse the change while recording; verify against the recorder in R6 |
| O-8 | Flash attribute period (`_vid.flash = frame_counter & 0x10`) in 60 Hz: the RTL flash source was not traced | check in R0 |

## 11. Summary

1. The AVR (Scroll Lock, saved in NVRAM) picks one of four rasters at any time; the Z80 cannot choose or write it.
2. RTL numbers: Pentagon 320 x 224 = 71 680 T, 60 Hz 262 x 224 = 58 688 T (59.64 Hz), 48K 312 x 224 = 69 888 T, 128K 311 x 228 = 70 908 T; INT 32 T in all, at line 0 slot 2 (Pentagon, 60 Hz) and line 1 slot 126 / 130 (48K / 128K).
3. Contention exists only in 48K and 128K rasters at 3.5 MHz, by address (`#C000` follows the `#7FFD` bit), on the line gate; Pentagon and 60 Hz have none.
4. A table of four profiles (`evoraster.h`) feeds `config`, the CPU geometry and the per-Screen descriptors.
5. The raster is stored in `EvoAvr` (INI, else NVRAM trailer, else 48K per Q1), cycled by Scroll Lock, read back through the Gluk mode extension.
6. A change applies at a frame boundary between `OnFrameEnd` and `OnFrameStart`, forcing `SetVideoMode`; pacing and audio already follow `config.frame`.
7. Contention is a cold re-selection of the memory interface on raster or clock change, so other machines pay nothing; 4 T border follows the raster.
8. TTD gets blob 23 and a `RasterSelect` input kind, restores raster before recomputing the frame geometry, and `FrameSpan` becomes constant (longest raster).
9. No ATM3 `.ttd` fixture exists; the ATM3 golden and four contention rows are re-recorded in the last phase; plan R0-R7, sizes S to L.
10. Open: INT 1811 versus 1756 (O-2), blank-NVRAM default, 60 Hz distance and 240-line framebuffer, ULA+ only in 128K (O-6), recording across a change, and a 1-3 T RTL-versus-reference gap closed by R0.
