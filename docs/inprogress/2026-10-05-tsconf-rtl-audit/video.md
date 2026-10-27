# TS-Conf audit: video timing, modes, render and output

Scope: raster and frame timing, graphics window geometry, register latching, the graphics row counter and X offset,
the four graphics modes, border, the gfx / TSU / border priority mux, CRAM and the DAC paths. The TSU engine itself
(tiles, sprites, layer pages) is audited elsewhere. References: RTL = `zx-evo/pentevo` (`fpga/current/...`, VDAC
boards `vdac/...`), the shipped build has `XTR_FEAT` off (`fpga/current/quartus/tune.v:18`) and the VDAC builds have
it on (`fpga/current/quartus_vdac/tune.v:16`, `fpga/current/quartus_vdac2/tune.v:13`). TS-Labs Unreal = `Unreal/...`.
unreal-ng = `core/...` at `cf3adb714`. Tests are in `core/tests/emulator/machines/tsconf/`.

| # | Behavior | RTL | TS-Labs Unreal | unreal-ng | Test | Verdict |
|---|---|---|---|---|---|---|
| 1 | Line = 448 dots at 7 MHz = 224 T at 3.5 MHz | `fpga/current/video/video_sync.v:84` `HPERIOD = 9'd448`, `:123` line_start at `hcount == HPERIOD-1` | `Unreal/draw.h:9` `VID_TACTS = 224` | `core/src/emulator/platforms/tsconf/tsconfgeometry.h:10` `kLineTacts = 224`; `core/src/emulator/config.cpp:1722` `t_line = 224` | `TsConfInterrupts_Test.TIM0_FrameIs320LinesOf224T` (added 2026-10-06) | match |
| 2 | Frame = 320 lines, 71680 T, 50 Hz | `video_sync.v:93-94` `VBLNK_END_50 = 32`, `VPERIOD_50 = 320`; `:151` vcount wraps at `vperiod-1` | `Unreal/draw.h:10` `VID_LINES = 320` | `tsconfgeometry.h:11`; `config.cpp:1721` `frame = 71680` | `TsConfInterrupts_Test.TIM0_FrameIs320LinesOf224T` (added 2026-10-06) | match |
| 3 | 60 Hz raster (262 lines, window table `v60hz ? ...`) | `video_sync.v:97-107`, but `:210-217` `v60hz <= 1'b0` unless `FORCE_60HZ`/`ENABLE_60HZ`, defined in no `tune.v` | absent | absent | n/a (not built) | match |
| 4 | `PENT_312` (312-line frame) | `video_sync.v:89-92`, `video_mode.v:164-173`; commented out `quartus/tune.v:28` and in the VDAC builds | absent | absent | n/a (not built) | match |
| 5 | Blanking / visible area: HBLANK dots 1-88 (picture delayed one dot by the c3 plex register, so picture dots 0-87 are blank), VBLANK lines 0-31; visible 360x288 from (88, 32) | `video_sync.v:140-141` `tv_hblank = hcount > 0 && hcount <= 88`, `tv_vblank = vcount < vblnk_end`; `video_out.v:55-56,62-72` | no blanking: the whole 448x320 raster is drawn into `vbuf` (`Unreal/draw.h:11-12`, `Unreal/draw.cpp:636-686`), the crop is a host option | `screentsconf.h:39-44`, `core/src/emulator/video/tsconf/screentsconf.cpp:637-643` (lines < 32 and tacts < 44 skipped) | `VID1_Geometry` `screentsconf_test.cpp:142`; `GEOM1_WorkingWindowFollowsVConfig` `screentsconf_test.cpp:477` | ng-matches-RTL, Unreal differs |
| 6 | RRES 0 window 256x192: dots 140-395, lines 80-271 | `video_mode.v:154,159,175,180` | `Unreal/draw.cpp:12` `{80, 272, 70, 70+128}` (tacts x2 = 140/396) | `tsconfgeometry.h:21` `{140, 80, 256, 192}` | `GEOM1_...` `screentsconf_test.cpp:477` | match |
| 7 | RRES 1 window 320x200: dots 108-427, lines 76-275 | `video_mode.v:155,160,176,181` | `Unreal/draw.cpp:14` `{76, 276, 54, 214}` | `tsconfgeometry.h:22` | `GEOM1_...` `:477` | match |
| 8 | RRES 2 window 320x240: dots 108-427, lines 56-295 | `video_mode.v:156,161,177,182` | `Unreal/draw.cpp:15` `{56, 296, 54, 214}` | `tsconfgeometry.h:23` | `GEOM1_...` `:477` | match |
| 9 | RRES 3 window 360x288: dots 88-447, lines 32-319 (also for ZX mode) | `video_mode.v:157,162,178,183,191-194` (`rres` applies to every mode) | `Unreal/draw.cpp:16` `{32, 320, 44, 224}`, `:713` raster from `comp.ts.rres` for every mode | `tsconfgeometry.h:24,27` `WindowOf(vConfig)` | `GEOM1_...` `:477`; `VID5_ZxInFullWindowWrapsColumns` `screentsconf_test.cpp:218` | match |
| 10 | V_CONFIG latched at line start (write mid-line acts from the next line) | `video_ports.v:124` `vconf_r <= ...`, `:153-157` `vconf <= vconf_r` at `line_start_s` | `Unreal/io.cpp:1515-1516` `vconf_d = val`; `Unreal/draw.cpp:613-617` copied at `line_pos == 0` | `portdecoder_tsconf.cpp:678-679,682` (flush, store), `tsconfengine.cpp:231-232` `latVConfig = r[VConfig]` in `LineStart` | `ENG2_VConfigActsFromTheNextLine` `tsconfengine_test.cpp` (added 2026-10-06) | match |
| 11 | V_PAGE (reg 0x01) latched at line start | `video_ports.v:123,155` | `Unreal/io.cpp:1519-1520`; `Unreal/draw.cpp:620` | `tsconfengine.cpp:233` | `ENG2_LatchedRegisterActsFromTheNextLine` `tsconfengine_test.cpp:37` | match |
| 12 | #7FFD bit 3 sets V_PAGE = 5 / 7 at once (also the pre-latch copy) | `video_ports.v:122` `vpage_r <= {6'b000001, d[3], 1'b1}`, `:150-151` `vpage <= ...` bypasses the latch | `Unreal/io.cpp:759` `vpage = vpage_d = (val & 8) ? 7 : 5` | `portdecoder_tsconf.cpp:753-754,780` `SetLiveVideoPage`; `tsconfengine.cpp:300-305` | `VID7_ScreenBitActsOnTheCurrentLine` `tsconfengine_test.cpp:116`; `P7F7_ScreenBitSetsVideoPageImmediately` `portdecoder_tsconf_test.cpp:175` | match |
| 13 | G_X_OFFS latched at line start | `video_ports.v:125-126,159` | `Unreal/io.cpp:1552-1558` `g_xoffs*_d`; `Unreal/draw.cpp:619` | `tsconfengine.cpp:235-236` | `ENG2_...` `tsconfengine_test.cpp:37` | match |
| 14 | PAL_SEL latched at line start | `video_ports.v:127,160` | `Unreal/io.cpp:1548-1549`; `Unreal/draw.cpp:621` | `tsconfengine.cpp:234` | `ENG2_...` `tsconfengine_test.cpp:37` | match |
| 15 | BORDER (reg 0x0F) acts at the dot (not latched) | `video_ports.v:110` `border <= xt_wr_data` (plain reg, no line latch) | `Unreal/io.cpp:1540-1541` sets it without `update_screen()`; the beam is caught up after the instruction (`Unreal/z80_main.inl:290-291`), so the new color covers the whole instruction | `portdecoder_tsconf.cpp:678-679` `FlushVideo()` first; read per dot `screentsconf.cpp:393,524` | `ENG2_BorderChangesWithinTheLine` `tsconfengine_test.cpp:54` | ng-matches-RTL, Unreal differs |
| 16 | OUT #FE: BORDER = `{PAL_SEL[3:0], 0, D[2:0]}` with the **latched** PAL_SEL | `video_ports.v:109` `border <= {palsel[3:0], 1'b0, d[2:0]}` (`palsel` = line-latched output reg, `:160`) | `Unreal/io.cpp:623,629` `(val & 7) \| 0xF0` (fixed bank F) | `portdecoder_tsconf.cpp:604-606` uses `_ts.regs[TsConfReg::PalSel]` (the unlatched register), comment says "latched" | `BorderWriteUsesPalSel` `portdecoder_tsconf_test.cpp:338`, `VID4_Border` `screentsconf_test.cpp:178` (both write PAL_SEL and OUT #FE without a line start between, so they encode the unlatched value) | **ng differs from both (BUG)** |
| 17 | G_Y_OFFS write: the row counter reloads with the current G_Y_OFFS at the next line start (not value + elapsed lines) | `video_sync.v:195-199` `y_offs_wr_r`, `:177-178` `line_start && y_offs_wr_r -> cnt_row <= rstart`; `video_top.v:324,355` `rstart = gy_offs` (not latched) | `Unreal/io.cpp:1560-1567` `g_yoffs_updated = 1`; `Unreal/draw.cpp:650-659` `ygctr = g_yoffs` at the next pixel line start | `portdecoder_tsconf.cpp:686-688` `yOffsPending = 1`; `tsconfengine.cpp:224-226` | `ENG3_RowCounterReloadsWithTheWrittenValue` `tsconfengine_test.cpp:82` | match |
| 18 | Row counter reload at the end of line 31 (`vis_start`) | `video_sync.v:128` `vis_start = line_start && vcount == vblnk_end-1`, `:177` | `Unreal/draw.cpp:811` `ygctr = g_yoffs - 1` at frame start, `+1` at the first window line (`:650-653`) - same value | `tsconfengine.cpp:225` `previous == kFirstVisibleLine - 1` | `ENG3_...ReloadsWithTheWrittenValue` `tsconfengine_test.cpp:82` (second frame) | match |
| 19 | Row counter +1 after every line of the graphics window, judged with the geometry that line was shown with | `video_sync.v:180-181` `line_start && vpix` (`vpix` from the old latched `vconf`) | `Unreal/draw.cpp:637,648-653` per line inside `u_brd..d_brd` of the current raster | `tsconfengine.cpp:227-228` `LineInWindow(_ts.latVConfig, previous)` before re-latching | `ENG3_RowCounterWrapsAndFollowsTheGeometry` `tsconfengine_test.cpp:103` | match |
| 20 | Row counter 9 bits, wraps at 512 | `video_sync.v:60` `cnt_row [8:0]`, `:181` | `Unreal/draw.cpp:653` `ygctr &= 0x1FF` | `tsconfengine.cpp:228` `& 0x1FF` | `ENG3_RowCounterWrapsAndFollowsTheGeometry` `tsconfengine_test.cpp:103` | match |
| 21 | ZX mode uses `cnt_row[7:0]`: rows wrap at 256; rows 192-255 read the attribute area | `video_mode.v:204-205` `addr_zx_gfx = {cnt_row[7:6], cnt_row[2:0], cnt_row[5:3], ...}` | `Unreal/drawers.cpp:105-106` (`ygctr & 0xC0` etc.) | `screentsconf.cpp:309-312,421-424` `y = gy & 0xFF` | `VID9_ZxRowsWrapAt256` `screentsconf_test.cpp` (added 2026-10-06) | match |
| 22 | ZX pixel and attribute fetch, color index `{PAL_SEL[3:0], BRIGHT, ink/paper}` | `video_mode.v:204-206`; `video_render.v:41-43` `zx_pix = {palsel, zx_attr[6], dot ^ (flash & attr[7]) ? attr[2:0] : attr[5:3]}` | `Unreal/drawers.cpp:116-136` | `screentsconf.cpp:307-326,419-445` | `VID3_ZxPaletteIndex` `screentsconf_test.cpp:166` | match |
| 23 | ZX columns wrap at 32 bytes (256 px) in the 320 / 360 windows | `video_mode.v:204` `cnt_col[4:1]` (16 words = 32 bytes) | `Unreal/drawers.cpp:105` `xctr & 0x1F` | `screentsconf.cpp:316,431` `x = gx & 0xFF` | `VID5_ZxInFullWindowWrapsColumns` `screentsconf_test.cpp:218` | match |
| 24 | FLASH: 5-bit frame counter, phase = bit 4 (16 frames on, 16 off), inverts ink/paper of attr bit 7 | `video_sync.v:202-209` `flash = flash_ctr[4]`, `+1` at `frame_start_s`; `video_render.v:43` | `Unreal/drawers.cpp:132` `comp.frame_counter & 0x10`; `Unreal/mainloop.cpp:69` | `screentsconf.cpp:313,425` `(frame_counter >> 4) & 1` | `VID10_FlashSwapsEvery16Frames` `screentsconf_test.cpp` (added 2026-10-06) | match |
| 25 | 16C: 4 bpp, high nibble = left pixel, `{PAL_SEL, nibble}`, 128 KB at `V_PAGE & 0xF8`, 256-byte rows | `video_mode.v:209` `{vpage[7:3], cnt_row, cnt_col[6:0]}`; `video_render.v:51-55` `hc_dot[0] = data[7:4]` | `Unreal/drawers.cpp:430-466` | `screentsconf.cpp:327-338,447-455` | `GFX1_SixteenColors` `screentsconf_test.cpp:203` | match |
| 26 | 256C: byte = CRAM index (no PAL_SEL), 256 KB at `V_PAGE & 0xF0`, 512-byte rows, even byte left | `video_mode.v:212`; `video_render.v:58-61` | `Unreal/drawers.cpp:484-504` | `screentsconf.cpp:339-349,457-462` | `TSO2_RendererMatchesTheReference` `screentsconf_test.cpp:265` | match |
| 27 | G_X_OFFS in 16C / 256C: linear pixel scroll, wraps at 512 px (column start + fine 0-3 / 0-1 dot shift) | `video_mode.v:115` `x_offs_mode`; `video_top.v:323,354` `x_offs = x_offs_mode[1:0]`, `cstart = x_offs_mode[9:2]`; `video_sync.v:129,160-162`; 16C 1 fetch / 4 dots, 256C 1 / 2 dots (`video_mode.v:129-131`) | `Unreal/drawers.cpp:432` `(xctr + (g_xoffs >> 1)) & 0xFF` (16C), `:488` `& 0x1FF` (256C) | `screentsconf.cpp:301,416` `gx = (wx + gxOffs) & 0x1FF` | `TSO2_...` `screentsconf_test.cpp:265` (random offsets against the old ng renderer) | match |
| 28 | G_X_OFFS in ZX mode | Not a linear scroll: `cstart = gx[8:2]` counts DRAM fetches, ZX fetches 2 words (pixels, attrs) per 16 px, so `cnt_col[4:1] = gx[6:3]` -> coarse shift 16 px per 8 of G_X_OFFS; `gx[2] = 1` makes the first fetch read attributes into the pixel slot (`video_mode.v:97,206`, `video_sync.v:160-170`); fine shift `gx[1:0]` dots (`video_sync.v:129`) | ignored: `Unreal/drawers.cpp:105` uses `vid.xctr` only (cleared per line, `Unreal/draw.cpp:647`) | `screentsconf.cpp:301,316` linear 1 px per unit, wrap 256 | `TSO2_...` `:265` (reference has the same linear rule, `screentsconf_test.cpp:23,32`) | **ng differs from both (BUG, low)** |
| 29 | G_X_OFFS in TXT mode | TXT fetches 4 words (chars, attrs, font 0, font 1) per 8 dots, so one `cnt_col` = 2 dots: `cstart = gx >> 2` gives a coarse shift of `gx` **hires** pixels (2 chars per 16), with a garbled first cell unless `gx[3:2] = 0`, plus a fine `gx[1:0]`-dot shift (`video_mode.v:100-112,216-220`, `video_sync.v:129`) | ignored: `Unreal/drawers.cpp:322-338` uses `vid.xctr` only | `screentsconf.cpp:376,474-476` `px = gx * 2`: G_X_OFFS counts dots (2 hires px each), twice the RTL coarse shift | `TSO2_...` `:265` (same rule in the reference, `screentsconf_test.cpp:56`) | **ng differs from both (BUG, low)** |
| 30 | TXT: row of 256 bytes at V_PAGE (128 codes, 128 attrs), 64 rows (`cnt_row[8:3]`), font at `V_PAGE ^ 1` + `code*8 + line`, 8 hires px per cell, ink `attr[3:0]` / paper `attr[7:4]` in the PAL_SEL bank, 128-char column wrap | `video_mode.v:216-220`, `:111-112` byte select `cnt_row[0]`; `video_render.v:47` `tx_pix = {palsel, dot ? attr[3:0] : attr[7:4]}`; `video_mode.v:136-137` hires | `Unreal/drawers.cpp:320-344` | `screentsconf.cpp:350-380,464-507` | `GFX3_TextMode` `screentsconf_test.cpp:188` | match |
| 31 | TXT border = `{PAL_SEL[3:0], BORDER[3:0]}` (hires plex keeps 4 bits) | `video_render.v:82` `vplex_out = hires ? {temp, video[3:0]} : video`; `video_out.v:61` `vdata = hires ? {palsel, nibble} : plex` | `Unreal/drawers.cpp:535` `vid.clut[comp.ts.border]` (full 8 bits in every mode) | `screentsconf.cpp:391-394` `palBank \| (border & 0x0F)`, `:596-605` | `TSO2_...` `:265` (reference `screentsconf_test.cpp:97`) | ng-matches-RTL, Unreal differs |
| 32 | TXT: TSU pixels cut to the low nibble in the PAL_SEL bank, one TSU pixel per two hires pixels | `video_render.v:82`; `video_out.v:61` | `Unreal/drawers.cpp:552-553` `clut[tsline]` full 8 bits | `screentsconf.cpp:590-591,596-605` | `TSO2_...` `:265` | ng-matches-RTL, Unreal differs |
| 33 | Priority mux, GFXOVR = 0: inside the gfx window an opaque TSU pixel (`index[3:0] != 0`, NOTSU clear) wins, else graphics (NOGFX: border) | `video_render.v:77,79,81` `video1 = tsu_visible ? ts : (nogfx ? border : pix)` | `Unreal/drawers.cpp:546-555` `draw_ts` over the drawn graphics when `tsline & 0x0F` | `screentsconf.cpp:570-594` | `TSO2_...` `:265`; `MAP6_TsuColourMatchesTheRenderer` `tsconfvideomapper_test.cpp:224` | match |
| 34 | GFXOVR (V_CONFIG bit 3): a "visible" gfx dot (ZX ink after flash, 16C/256C index != 0, TXT font bit) wins over the TSU, an invisible one shows TSU else border. RTL: `XTR_FEAT` builds only | `video_render.v:70-74,78,80`; `video_top.v:466-470` `gfxovr = vconf[3]` only with `XTR_FEAT`, else `1'b0` | not rendered (bit only shown in `Unreal/debugger/dbgtsconf.cpp:65`) | `screentsconf.cpp:388-389,549-558,575-587`, always on (hardware-spec D1 "superset") | `TSO2_...` `:265` | ng-matches-RTL, Unreal differs (RTL `XTR_FEAT`; see gap note) |
| 35 | NOGFX (V_CONFIG bit 5): window shows BORDER, TSU still overlays, graphics DRAM fetch stops | `video_top.v:133`; `video_render.v:78-79`; `video_sync.v:237` `video_go ... && !nogfx` | `Unreal/draw.cpp:715` `nogfx -> M_BRD` (border drawer), `draw_ts` still runs (`:676`) | `screentsconf.cpp:399,539`; `tsconfengine.cpp:62-63` `VideoCost = 0` | `TSO2_...` `:265`; `DMA12_PacingFollowsTheVideoBandwidth` `tsconfdma_test.cpp:253` | match |
| 36 | NOTSU (V_CONFIG bit 4) hides TSU pixels at the mix of the displayed line (the TSU still renders and fetches) | `video_top.v:134`; `video_render.v:77` `tsu_visible = \|ts[3:0] && !notsu` (latched `vconf` of the shown line) | `Unreal/tsconf.cpp:699,747`: the layers are not rendered when NOTSU is set while the TSU draws the next line, so NOTSU acts one line early and also saves the TSU fetch | `screentsconf.cpp:388,570` `set.vConfig & 0x10` of the displayed line | `TSO2_...` `:265`; `GEOM2_WorkingWindowIncludesTheTsuWindow` `screentsconf_test.cpp:575` | ng-matches-RTL, Unreal differs |
| 37 | TSU window = gfx window, or 360x288 with T_CONFIG bit 0 (`ts_rres_ext`, RTL `XTR_FEAT` only); outside the gfx window but inside the TS window an opaque TSU pixel shows over the border | `video_mode.v:196-201`; `video_top.v:280-284`; `video_sync.v:229-231`; `video_render.v:81` `(hvtspix && tsu_visible) ? ts : border` | T_CONFIG bit 0 is `t0ys_en` (`Unreal/tsconf.h:329`); the TSU is drawn only inside the gfx window (`Unreal/draw.cpp:670-676`) | `tsconfengine.cpp:53-54,72`; `screentsconf.cpp:572-593`; always on (D1) | `GEOM2_...` `screentsconf_test.cpp:575`; `MAP6_...` `tsconfvideomapper_test.cpp:224` | ng-matches-RTL, Unreal differs (RTL `XTR_FEAT`; see gap note) |
| 38 | Outside both windows: BORDER | `video_render.v:81` | `Unreal/drawers.cpp:524-543` | `screentsconf.cpp:407-408,512-513,532-534` | `GEOM1_...` `:477`; `VID4_Border` `:178` | match |
| 39 | CRAM word: R `[14:10]`, G `[9:5]`, B `[4:0]`; bit 15 is only forwarded to an external VDAC (`vdac_mode`) | `video_out.v:48-51` | `Unreal/tsconf.cpp:45-49` | `screentsconf.cpp:60-61` | `CramColors` `screentsconf_test.cpp:157` | match |
| 40 | CPU CRAM write through the FM window: even byte stashed, odd byte commits `{D, stash}` to entry `A[8:1]`; visible from that dot on | `fpga/current/z80/zmaps.v:64-77`; `video_out.v:135-151` (dual-port RAM read per pixel) | `Unreal/z80_main.inl:118-126`; no beam flush before the write, catch-up after the instruction (`:290-291`), so it shows up to one instruction early | `portdecoder_tsconf.cpp:1014-1036`, `:1038-1046` `FlushVideo()` then commit | `FM1_CramThroughTheWindow` `portdecoder_tsconf_test.cpp:184` (value only; no mid-line timing test for the CPU path) | ng-matches-RTL, Unreal differs |
| 41 | DMA CRAM write lands at its dot | `fpga/current/common/dma.v:420`; `zmaps.v:69` | `Unreal/draw.cpp:688-690`: DMA runs after the span was drawn (chunk granularity), `Unreal/tsconf.cpp:453-470` | `tsconfengine.cpp:170-183` (`_videoFlush` per word) | `TIM5_DmaCramWriteLandsAtItsDot` `screentsconf_test.cpp:413` | ng-matches-RTL, Unreal differs |
| 42 | CRAM contents at power-on = `video_cram.mif` (all 256 entries); a Z80 reset keeps CRAM | `video_out.v:157` `lpm_file = "../video/mem/video_cram.mif"` (FPGA configuration only) | `Unreal/draw.cpp:820-827` reloads only 0xF0-0xFF from `spec_colors` | `portdecoder_tsconf.cpp:148-153`; `tsconfcraminit.h` equals the .mif (checked entry by entry) | `RST1_WarmResetValues` `portdecoder_tsconf_test.cpp:28`, `RST2_WarmResetKeepsTheNotResetRegisters` `:54` | ng-matches-RTL, Unreal differs |
| 43 | No-VDAC build: top 2 bits of each 5-bit channel drive a 2-bit DAC, the low 3 bits a PWM adding `i/8` of a level (none at level 3); emulated as the time average | `video_out.v:75-97` `red0 <= (!pwm[ired][...] \| &cred) ? cred : cred+1`, `:112-114`, `:125-132` (`pwm[i]` has i ones of 8) | `Unreal/tsconf.cpp:31-35,97-100` gamma-2.2 table from measured analog levels (#00-#5D-#A2-#FF), not the linear digital average | `screentsconf.cpp:19-32` `(dac*8 + boost) * 255 / 24`, `:64-65` | `CramColors` `screentsconf_test.cpp:157` | ng-matches-RTL, Unreal differs |
| 44 | 5-bit VDAC build (`IDE_VDAC`, STATUS VDAC_VER 3): VDAC board CPLD maps CRAM bit 15 = 1 to `{in, 3'b0}` (white = 248), bit 15 = 0 to the rounded linear table 0, 10, 21, ..., 117 (at 11), ..., 255 from 24 | `vdac/vdac1/cpld/top.v:28-57`; `video_out.v:100-104` (no PWM), `fpga/current/top.v:440-444` (`vdac_mode` on `ide_d[15]`) | `Unreal/tsconf.cpp:53-64` `pwm_lin[]` / `r << 3` - same as the CPLD | `screentsconf.cpp:68-77`: bit 15 = 1 -> `code * 255 / 31` (31 -> 255, 16 -> 131 instead of 128), bit 15 = 0 -> truncating `v * 255 / 24` (116 at 11, off by one at 11, 14, 17, 19, 20, 22, 23) | `VDAC_CurvesStatusAndRender` `screentsconf_test.cpp:364` and `VDAC2_CardTable` `:390` ("the 5-bit VDAC build keeps its curve") assert the divergent values | **ng differs from both (BUG)** |
| 45 | VDAC2 build (VDAC_VER 7): card CPLD, bit 15 = 1 -> `{in, 3'b0}`, else the rounded table | `vdac/vdac2/cpld/top.v:58-66` | `Unreal/tsconf.cpp:53-64` when configured `5BIT` | `screentsconf.cpp:39-56,66-67` `Vdac2Level` | `VDAC2_CardTable` `screentsconf_test.cpp:390` | match |
| 46 | 3-bit / 4-bit VDAC (VDAC_VER 1 / 2) | no build produces VDAC_VER 1 or 2 (`fpga/current/z80/zports.v:237-247`: 3, 6, 7, 0); no board CPLD in the tree | `Unreal/tsconf.cpp:67-95` bit 15 = 1: bit replication `CcccCccc` / `CccCccCc`; bit 15 = 0: `pwm_lin` ("FIX ME: clone-specific PWM") | `screentsconf.cpp:68-77` `code * 255 / (2^bits - 1)` (3-bit codes 2, 4, 6 give 72, 145, 218 vs Unreal 73, 146, 219), truncating linear curve | `VDAC_CurvesStatusAndRender` `:364` | unclear (no hardware reference) |
| 47 | Graphics DRAM fetch per line (DMA / TSU budget) | `video_sync.v:237` fetch window `[hpix_beg - go_offs - x_offs, hpix_end - go_offs - x_offs + 4)` = window + 4 dots; rates `video_mode.v:128-133` | per drawn tact: `Unreal/drawers.cpp:118,339,461-465,499` (window width only) | `tsconfengine.cpp:58-65` `w >> {3,2,1,1}` (window width only) | `DMA12_PacingFollowsTheVideoBandwidth` `tsconfdma_test.cpp:253` | unclear (RTL fetches over w + 4 dots; the exact extra slot count depends on the arbiter phase, not verified) |
| 48 | Debug video mapper: BORDER source color in TXT | renderer rule as row 31 (`{PAL_SEL, BORDER[3:0]}`) | n/a (Unreal shows `clut[border]` everywhere, row 31) | `core/src/emulator/video/tsconf/tsconfvideomapper.cpp:271-281` reports `regs[Border]` and `cram[regs[Border]]` in every mode, so in TXT it disagrees with the ng renderer | `MAP5_TextCellsAndBorder` `tsconfvideomapper_test.cpp:177` (TXT mode, asserts `colourIndex == regs[Border]`) | **ng-matches-Unreal, RTL differs (BUG, debug only)** |

Verdict counts (48 rows): match 30; ng-matches-RTL, Unreal differs 11; ng differs from both 4 (rows 16, 28, 29, 44);
ng-matches-Unreal, RTL differs 1 (row 48); unclear 2 (rows 46, 47).

## Bugs and gaps

**Row 16 - OUT #FE takes the unlatched PAL_SEL (BUG).** RTL builds the border index from the `palsel` output register,
which is the copy latched at line start (`video_ports.v:109,160`); unreal-ng reads `_ts.regs[TsConfReg::PalSel]`,
the pre-latch register (`portdecoder_tsconf.cpp:605-606`), even though its own comment and hardware-spec §3.4 say
"latched". The difference shows when a program writes PAL_SEL and OUT #FE on the same line (a raster effect switching
the bank and the border together). Fix: use `_ts.latPalSel` after `FlushVideo()` (which has caught the engine up).
Both existing tests (`BorderWriteUsesPalSel`, `VID4_Border`) write PAL_SEL then #FE with no line start between, so
they assert the buggy value and must change with the fix. Suggested test `FE1_BorderUsesTheLatchedPalSel`
(tsconfengine_test.cpp style): `NewFrame(); RunTo(T(100, 50)); Reg(PalSel, 0x0A); Out(0x00FE, 0x05)` ->
`regs[Border] == 0xF5`; `RunTo(T(101, 10)); Out(0x00FE, 0x05)` -> `regs[Border] == 0xA5`.

**Row 28 - G_X_OFFS in ZX mode is a linear scroll in unreal-ng (BUG, low).** RTL loads the fetch column counter with
`G_X_OFFS[8:2]`, a unit that is right for 16C (one word = 4 dots) but in ZX mode one column step is one word fetch,
and ZX fetches a pixel word and an attribute word per 16 pixels: G_X_OFFS = 8 starts at byte column 2 (a 16 px shift),
and any value with bit 2 set swaps the pixel / attribute fetches (`video_mode.v:97,206`, `video_sync.v:160-170`).
Unreal ignores G_X_OFFS in ZX mode; unreal-ng shifts by G_X_OFFS pixels (`screentsconf.cpp:301,316`). hardware-spec
§4.2 ("they apply in ZX ... and TXT too") is wrong for X. Decide whether to model the RTL behavior (the faithful
choice) or ignore the offset like Unreal; either way the current linear rule matches neither. Suggested test
`VID8_ZxXOffsetStepsByFetchColumns`: ZX rres 0, byte column 2 of row 0 = 0x80 (ink 2), `G_X_OFFS = 8`, one frame ->
pixel at window dot 140 line 80 is ink 2 (RTL: column 2 shows first); `G_X_OFFS = 1` -> the window starts at pixel 1.

**Row 29 - G_X_OFFS in TXT mode scrolls twice as far as RTL (BUG, low).** In TXT one fetch column = 2 dots (4 words
per 8 dots), so `cstart = G_X_OFFS >> 2` shifts the text by G_X_OFFS hires pixels in steps of 4 (clean only for
multiples of 16), plus `G_X_OFFS[1:0]` dots of fine shift (`video_mode.v:100-112,216-220`, `video_sync.v:129`).
unreal-ng treats G_X_OFFS as dots and doubles it into hires pixels (`screentsconf.cpp:376,474-476`), so G_X_OFFS = 16
shows character 4 at the left edge where RTL shows character 2; Unreal ignores the offset. Suggested test
`GFX4_TextXOffsetCountsHiresPixels`: TXT rres 0, codes 0..7 with distinct fonts, `G_X_OFFS = 16` -> the first 8 hires
pixels of line 80 are character 2's glyph.

**Row 44 - 5-bit VDAC curves do not match the VDAC board (BUG).** The VDAC1 board's CPLD (`vdac/vdac1/cpld/top.v:28-57`)
maps a channel to `{in, 3'b0}` when CRAM bit 15 is set (top 248) and to the rounded table 0, 10, 21, ..., 117, ...,
245, 255 when it is clear - exactly the VDAC2 card's table that unreal-ng already has in `Vdac2Level` and exactly
what Unreal does (`Unreal/tsconf.cpp:53-64`). unreal-ng's `vdac == 3` path instead scales 31 to 255 and uses a
truncating curve (`screentsconf.cpp:68-77`), off by one at seven levels and up to 7 at the top. hardware-spec §4.3
("the emulator scales each DAC to full 255") documents a decision the hardware contradicts. Fix: route `vdac == 3`
through `Vdac2Level`. The tests `VDAC_CurvesStatusAndRender` (expects 0x83 / 0xFF) and `VDAC2_CardTable` (line "the
5-bit VDAC build keeps its curve", expects 116) assert the divergence and must change. Suggested test
`VDAC1_BoardCpldTable`: for v in 0..31, `CramToRgba(v, 3)` blue == card table[v] and
`CramToRgba(0x8000 | v << 10, 3)` red == `v << 3`.

**Row 48 - debug mapper reports the wrong border cell in TXT (BUG, debug only).** `TsConfVideoMapper::BorderSources`
(`tsconfvideomapper.cpp:271-281`) returns `regs[Border]` and its CRAM color in every mode; in TXT the renderer shows
`cram[{PAL_SEL[3:0], BORDER[3:0]}]` (row 31), so the debugger names a CRAM cell and color that are not on screen.
`MAP5_TextCellsAndBorder` runs in TXT and asserts the current value. Fix: in TXT flatten with the latched PAL_SEL of
the line. Suggested test `MAP10_TextBorderIsFlattened`: TXT, `PAL_SEL = 0x03`, `BORDER = 0xA5` -> `colourIndex ==
0x35` and `rgb == CramToRgba(cram[0x35])`.

**Row 1 / row 2 - frame constants untested (gap). Covered 2026-10-06:** `TsConfInterrupts_Test.TIM0_FrameIs320LinesOf224T`. The 224 T / 320 line / 71680 T frame is only used as constants.
Suggested test `TIM0_FrameIs320LinesOf224T`: create a TSL machine, assert `config.frame == 71680`,
`config.t_line == 224`, and that the frame interrupt period measured over two frames is 71680 T.

**Row 10 - V_CONFIG mid-line latch untested (gap). Covered 2026-10-06:** `TsConfEngine_Test.ENG2_VConfigActsFromTheNextLine` (the line table's V_CONFIG and video cost; no pixel check). ENG2 covers V_PAGE, PAL_SEL and G_X_OFFS but not V_CONFIG, the
register whose latch decides mode and geometry. Suggested test `ENG2_VConfigActsFromTheNextLine`: `NewFrame();
RunTo(T(100, 120)); Reg(VConfig, 0x41)` -> `Line(100).vConfig == 0x00`, `Line(101).vConfig == 0x41`, and the drawn
pixel at dot 400 of line 100 is still graphics of the 256x192 window (border right of dot 395 is not yet 320 wide).

**Row 21 - ZX row wrap untested (gap). Covered 2026-10-06:** `ScreenTSConf_Test.VID9_ZxRowsWrapAt256`. Suggested test `VID9_ZxRowsWrapAt256`: ZX rres 0, `G_Y_OFFS = 200` -> line 80
shows the bytes at offset `0x1800 + ...` of the row-200 formula (row 200 reads the attribute area), and
`G_Y_OFFS = 256` -> line 80 shows row 0.

**Row 24 - FLASH untested (gap). Covered 2026-10-06:** `ScreenTSConf_Test.VID10_FlashSwapsEvery16Frames`. No test varies `frame_counter`. Suggested test `VID10_FlashSwapsEvery16Frames`:
attr 0x81 (flash, ink 1), pixel set; frame_counter 15 -> ink color, 16 -> paper color, 31 -> paper, 32 -> ink.

**Gap note, rows 34 and 37 - XTR_FEAT bits in the standard build.** GFXOVR (V_CONFIG bit 3) and the 360x288 TS window
(T_CONFIG bit 0) exist only in the `XTR_FEAT` (VDAC / VDAC2) builds; the standard build ties them to 0
(`video_top.v:280-284,466-470`). unreal-ng implements them always by decision D1, also when `[MISC] TS_VDAC = NONE`
reports the standard build (VDAC_VER 0). Software that sets these bits by accident renders differently from a
standard board. If build fidelity matters, gate both on `VdacVersion() != 0`, as DMA BLT2 already is
(`DMA5_Blit2OnlyInXtrBuilds`); suggested test `XTR1_GfxOvrAndTsWindowOnlyInXtrBuilds`: with `ts_vdac = 0`, V_CONFIG
0x08 renders like 0x00 and T_CONFIG 0x81 keeps the TSU inside the 256x192 window; with `ts_vdac = 3` both act.

**Row 46 - 3/4-bit VDAC (unclear).** No firmware build reports VDAC_VER 1 or 2 and no board CPLD is in the tree; the
options exist only in Unreal. unreal-ng and Unreal differ by one LSB at 3-bit codes 2, 4, 6 and on the bit-15-clear
curve. Low priority; aligning with Unreal (bit replication, `pwm_lin`) would remove the difference.

**Row 47 - video fetch cost (unclear).** RTL's fetch window is 4 dots longer than the graphics window
(`video_sync.v:237`), so a line likely costs 1-2 more DRAM slots than unreal-ng's `VideoCost` charges (affects DMA
pacing and the TSU budget slightly). Needs a check against `dram/arbiter.v` before changing anything.

Side notes on hardware-spec: §4.2 claims G_X_OFFS applies in ZX and TXT (rows 28-29 show it does not linearly);
§4.3 says the 5-bit VDAC scales to 255 (row 44: the board gives 248 and a rounded table); §3.4 says the latched
PAL_SEL is used, which the code does not do (row 16).
