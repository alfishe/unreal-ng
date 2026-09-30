# TS-Labs Unreal (ZX-Evo TSConf) debugger — capability survey

**Source:** https://github.com/tslabs/zx-evo-unreal (TS-Labs fork of Unreal Speccy, the reference emulator for the ZX-Evolution TS-Configuration) · local checkout commit `86fd99b` (2025-08-26) · C++ (MSVC 2017 project `Unreal/Unreal2017.vcxproj`), Win32 GDI; debugger sources in `Unreal/debugger/`
**Surveyed:** 2026-09-28 (source reading)
**Scope note:** The classic Unreal Speccy text-mode monitor (see `unreal-speccy-debugger.md`) moved into a **separate Win32 window** with a menu bar, widened from 80 to 157 text columns on TSConf to host a read-only **TSConf register panel**, plus a **PC history** pane, **bank editing**, memory-access breakpoint operands, a live **beam marker / per-line DRAM-bandwidth overlay** after every step, and a separate **"visuals" window** (TSU tiles/sprites/palette viewer). No scripting, no remote protocol, no rewind.

Paths below are relative to the checkout root: `zx-evo-unreal/Unreal/...`. Where behavior is identical to classic Unreal Speccy 0.39.0 it is summarized and cross-referenced; the classic document has the full detail (expression grammar, label formats, hotkey semantics).

## 1. Capability registry

| Area | Feature | What it does / values it shows | Where |
|---|---|---|---|
| UI conveniences | Separate debugger window | `DEBUG_WND` "UnrealSpeccy debugger" shown on break, hidden on run; emulator window stays visible; 157x30 cells on TSConf (88 otherwise), optional 2x scale | `zx-evo-unreal/Unreal/debugger/debug.cpp:414-465`, `zx-evo-unreal/Unreal/debugger/debug.h:87-97` |
| UI conveniences | Menu bar | Monitor (load/save/fill block, ripper, scale 2x), Debug (continue, step into, step over, till return, run to cursor), Breakpoints (toggle, manager) | `zx-evo-unreal/Unreal/Unreal.rc:17-43`, `zx-evo-unreal/Unreal/debugger/debug.cpp:385-409` |
| CPU & registers | Registers, flags, T counter | Same as classic (changed values bright, `DiHALT`) | `zx-evo-unreal/Unreal/debugger/dbgreg.cpp:40-96` |
| CPU & registers | Multi-CPU | Main Z80 + GS Z80, `Ctrl+~` | `zx-evo-unreal/Unreal/vars.cpp:195-201`, `zx-evo-unreal/Unreal/debugger/dbgcmd.cpp:280-297` |
| Disassembly | CPU window, branch preview, assembler, labels | Same as classic, minus HALT target resolution | `zx-evo-unreal/Unreal/debugger/dbgtrace.cpp:18-228`, `:276-340` |
| Memory views | Dump / disk editor / CMOS / NVRAM | Same as classic; the "comp palette" editor mode is gone | `zx-evo-unreal/Unreal/debugger/dbgmem.cpp:24-200`, `zx-evo-unreal/Unreal/debugger/debug.h:108` |
| Memory views | Screen-memory inset | Watch pane shows the ZX screen from page 5 or 7 with TSConf border color; `Alt+Shift+S` flips main/alt screen | `zx-evo-unreal/Unreal/debugger/dbgpaint.cpp:19-58`, `zx-evo-unreal/Unreal/debugger/dbgcmd.cpp:137-143`, `:277-278` |
| Memory views | RAM dump to file | `Alt+F9` (emulation) writes all RAM to `ram.bin` | `zx-evo-unreal/Unreal/emulkeys.cpp:356-360`, `zx-evo-unreal/Unreal/config.cpp:124-128` |
| Memory views | Pages pane with edit | Pages pane is a 4th focusable window (`TAB`); type hex to set `comp.ts.page[n]` (TSConf only) | `zx-evo-unreal/Unreal/debugger/dbgoth.cpp:178-231`, `zx-evo-unreal/Unreal/keydefs.cpp:250-259` |
| Tracing & logging | PC history | Last 28 of a 32-entry ring of `page:addr` of every opcode fetch (M1) | `zx-evo-unreal/Unreal/vars.cpp:66-71`, `zx-evo-unreal/Unreal/defs.h:39-45`, `zx-evo-unreal/Unreal/debugger/dbgoth.cpp:233-249` |
| Tracing & logging | Ripper, last branch | Same as classic | `zx-evo-unreal/Unreal/debugger/dbgcmd.cpp:222-266`, `zx-evo-unreal/Unreal/debugger/dbgtrace.cpp:224-226` |
| Breakpoints | Execution / memory R/W / conditional | Same `membits` scheme, `bpx.ini`, `Alt+C` manager | `zx-evo-unreal/Unreal/debugger/dbgbpx.cpp:479-631`, `zx-evo-unreal/Unreal/debugger/debug.cpp:268-318` |
| Breakpoints | Memory-access conditions | New operands `RD`, `WR` (address of last read/write), `MDT` (byte read/written); conditions also evaluated **inside every memory access** | `zx-evo-unreal/Unreal/debugger/dbgbpx.cpp:81-83`, `zx-evo-unreal/Unreal/z80_main.inl:59-86` |
| Breakpoints | Bank operands | New `PG0`..`PG3` = TSConf `Page0..3` registers | `zx-evo-unreal/Unreal/debugger/dbgbpx.cpp:85-88` |
| Breakpoints | Port operands | `IN`, `OUT`, `VAL`, `FD`, `DOS` as classic | `zx-evo-unreal/Unreal/debugger/dbgbpx.cpp:77-84`, `zx-evo-unreal/Unreal/io.cpp:49-50`, `:959`, `:1438` |
| Import / export & persistence | Command-line bpx and label files | `-b file` (breakpoints, default `bpx.ini`), `-l file` (labels, default `user.l`), `-i file` (ini) | `zx-evo-unreal/Unreal/init.cpp:74-109`, `zx-evo-unreal/Unreal/debugger/dbgbpx.cpp:678-718`, `zx-evo-unreal/Unreal/debugger/dbglabls.cpp:449-458` |
| Symbols & labels | Label store, XAS/ALASM import, go-to-label | Same as classic; label display now forces a reload of the user file when switched on | `zx-evo-unreal/Unreal/debugger/dbglabls.cpp`, `zx-evo-unreal/Unreal/debugger/dbgtrace.cpp:472-477` |
| Sound & device views | AY, Beta 128, ports, GS dialog | Same as classic (GS dialog in the older list-box form) | `zx-evo-unreal/Unreal/debugger/dbgoth.cpp:119-139`, `:251-405` |
| Video, raster & beam | TSConf register panel | 16 register groups decoded bit by bit: VConfig, TSConfig, SysConfig, CacheConfig, MemConfig, Bitmap, Tiles0/1, PalSel, Misc, FMAddr, MemPages, Sprites, DMA, Interrupt, IntMask | `zx-evo-unreal/Unreal/debugger/dbgtsconf.cpp:54-362` |
| Video, raster & beam | DMA state | Programmed vs current source/destination (24-bit), NUM/LEN programmed vs remaining, CTRL bits, device pair name, ACTIVE LED | `zx-evo-unreal/Unreal/debugger/dbgtsconf.cpp:264-301` |
| Video, raster & beam | INT line tools | HSINT (x in pixels = value*2), VSINT line and increment, IntMask DMA/LINE/FRAME enables | `zx-evo-unreal/Unreal/debugger/dbgtsconf.cpp:303-336` |
| Video, raster & beam | Beam marker | Inverts the frame-buffer pixel at the current beam position on break and after every step | `zx-evo-unreal/Unreal/draw.cpp:701-706`, `zx-evo-unreal/Unreal/debugger/debug.cpp:165`, `zx-evo-unreal/Unreal/debugger/dbgtrace.cpp:599-602` |
| Video, raster & beam | Live screen on step | Each `F7` runs `update_screen()` (video, TSU, DMA) up to the new T-state and repaints the emulator window | `zx-evo-unreal/Unreal/debugger/dbgtrace.cpp:518-605`, `zx-evo-unreal/Unreal/debugger/debug.cpp:320-332` |
| Profiling, heat maps, coverage | DRAM bandwidth overlay | Per scan line, bars of DRAM cycles used by CPU, video, TSU tiles, TSU sprites, DMA, with overflow in red; drawn when `Border=5` (448x320) | `zx-evo-unreal/Unreal/leds.cpp:524-587`, `zx-evo-unreal/Unreal/mainloop.cpp:44-45`, `zx-evo-unreal/Unreal/draw.cpp:585-593` |
| Video, raster & beam | Visuals window (`F4`) | Cycles: off -> sprites (85 sprites) -> sprite graphics page -> tile0 graphics -> tile1 graphics; 256-color CLUT grid always shown; updated every frame | `zx-evo-unreal/Unreal/visuals.cpp:118-287`, `zx-evo-unreal/Unreal/mainloop.cpp:110` |
| Video, raster & beam | TSU layer toggle (`Alt+Ctrl+~`) | Modeless dialog to mask graphics / tile0 / tile1 / sprite layers in rendering | `zx-evo-unreal/Unreal/tsconf.cpp:1030-1033`, `:792-796`, `zx-evo-unreal/Unreal/Unreal.rc:615-624` |
| Profiling, heat maps, coverage | Memory band LED | Same as classic | `zx-evo-unreal/Unreal/debugger/debug.cpp:342-345` |
| Scripting, automation & remote debug | None | *(inferred: no such code in the tree)* | — |
| History / rewind / time travel | Only PC history ring | No state history | `zx-evo-unreal/Unreal/defs.h:219-220` |

## 2. Architecture changes vs classic

- **Window.** `init_debug()` registers `DEBUG_WND` with its own 8-bit DIB (`debug_gdibuf`), menu `IDR_DEBUGMENU`, and paints via `StretchDIBits` with `windowScale` 1 or 2 (`zx-evo-unreal/Unreal/debugger/debug.cpp:365-465`). `debug()` shows it on entry and hides it on exit instead of switching the main renderer to `RF_MONITOR` (`zx-evo-unreal/Unreal/debugger/debug.cpp:157-266`). Closing the window = continue (`zx-evo-unreal/Unreal/debugger/debug.cpp:369-373`). Width is `157*8` px on `MM_TSL`, `88*8` px otherwise (`zx-evo-unreal/Unreal/debugger/debug.cpp:420`).
- **Event loop** gains a 4th window `wndbanks` with action table `ac_banks` (`mem.up`, `mem.down`, `reg.edit` + all `mon.*`) and a typing handler `dispatch_banks()` (`zx-evo-unreal/Unreal/debugger/debug.cpp:215-246`, `zx-evo-unreal/Unreal/keydefs.cpp:250-259`). `TAB` order is regs -> trace -> mem -> banks (`zx-evo-unreal/Unreal/debugger/dbgcmd.cpp:192-202`). The loop also exits when `dbgbreak` is cleared from the menu (`zx-evo-unreal/Unreal/debugger/debug.cpp:211-212`).
- **Two cores** remain (`cpu.dbgchk` picks the debug core per frame, `zx-evo-unreal/Unreal/mainloop.cpp:25`; `isbrk()` unchanged, `zx-evo-unreal/Unreal/debugger/debug.cpp:336-360`); TSConf has its own loop `z80loop_TSL()` that calls `debug_events()` through `step1()` (`zx-evo-unreal/Unreal/z80_main.inl:262-275`).
- **Condition checks moved into memory access.** `debug_cond_check()` is factored out and, in the debug core, called from `rm()` (after setting `brk_mem_rd`, `brk_mem_val`) and from `wm()` (after `brk_mem_wr`, `brk_mem_val`), both marked `/* this is sloooow */`, in addition to the per-instruction call (`zx-evo-unreal/Unreal/z80_main.inl:59-68`, `:77-86`, `zx-evo-unreal/Unreal/debugger/debug.cpp:268-318`). `debug_events()` resets `brk_mem_rd/wr` to `0xFFFFFFFF` together with the port variables (`zx-evo-unreal/Unreal/debugger/debug.cpp:313-314`).
- **Step is frame-accurate for the video side.** `mon_step()` executes one instruction, emulates TSConf frame/line/DMA interrupt generation (`ts_frame_int`, `ts_line_int`, `ts_dma_int`) or the generic INT window, Baseconf/Scorpion NMI rules, then `update_screen()` (renders video, TSU and DMA up to the new T-state), `update_raypos()` and `flip_from_debug()` so the emulator window shows the partially drawn frame with the beam marker (`zx-evo-unreal/Unreal/debugger/dbgtrace.cpp:518-605`). It also resets `vid.memcyc_lcmd` to accumulate the DRAM cycles of this one instruction (`zx-evo-unreal/Unreal/debugger/dbgtrace.cpp:526`).

## 3. TSConf register panel

Built with a small widget layer (`dbg_canvas` -> `dbg_column` -> `dbg_control`, each control paints rows like `bits: NAME = value`) (`zx-evo-unreal/Unreal/debugger/dbgwidgets.h:22-146`, `zx-evo-unreal/Unreal/debugger/dbgwidgets.cpp:15-130`). Three 23-column columns at x=88 (`zx-evo-unreal/Unreal/debugger/dbgtsconf.cpp:338-362`, `zx-evo-unreal/Unreal/debugger/debug.h:87-88`):

| Column | Group | Shown fields |
|---|---|---|
| 0 | VConfig (raw byte in frame title) | RRES (256x192/320x200/320x240/360x288), NOGFX, NOTSU, GFXOVR, FT_EN, VMODE (ZX/16c/256c/text) |
| 0 | TSConfig | S_EN, T1_EN, T0_EN, T1Z_EN, T0Z_EN, TS_EXT |
| 0 | SysConfig | CACHE_EN, ZCLK (3.5M/7M/14M) |
| 0 | CacheConfig | EN_0000/4000/8000/C000 |
| 0 | MemConfig | LCK128, W0_RAM, W0_MAP, W0_WE, ROM128 |
| 1 | Bitmap | EN LED, VPage, GXOffs H/L/value, GYOffs H/L/value |
| 1 | Tiles0 / Tiles1 | Z_EN and EN LEDs, T0GPage/T1GPage, X and Y offsets |
| 1 | PalSel | T1PAL, T0PAL, GPAL |
| 1 | Misc | FDD virtual drive LEDs A-D, TMPage, Border, FDDVirt |
| 1 | FMAddr | FM_EN, FM_MAPS address (`fm_addr << 12`) |
| 1 | MemPages | Page0..Page3 |
| 2 | Sprites | SGPage |
| 2 | DMA | ACTIVE, SRC / CURR SRC, DST / CURR DST (24-bit), NUM/LEN and remaining, CTRL raw, OPT, S_ALIGN, D_ALIGN, A_SZ, DDEV (RAM-RAM, BLT-RAM, SPI-RAM, RAM-SPI, IDE-RAM, RAM-IDE, FIL-RAM, RAM-CRM, RAM-SFL) |
| 2 | Interrupt | HSINT (+ pixel x), VSINTH/VSINTL (+ line, increment) |
| 2 | IntMask | DMA, LINE, FRAME |

(`zx-evo-unreal/Unreal/debugger/dbgtsconf.cpp:54-336`.) Clicking a group only highlights it (`is_active`); nothing is editable from the panel (`zx-evo-unreal/Unreal/debugger/dbgwidgets.cpp:306-319`, `zx-evo-unreal/Unreal/debugger/dbgtsconf.cpp:369-384`). The panel is painted on every monitor repaint regardless of model, but the window is only wide enough on `MM_TSL` (`zx-evo-unreal/Unreal/debugger/debug.cpp:64`, `:420`).

## 4. Breakpoints and expression language

Grammar, operator table, manager dialog, `bpx.ini` format, watches: identical to classic (`zx-evo-unreal/Unreal/debugger/dbgbpx.cpp:24-66`, `:126-266`, `:479-758`). Additions to the operand table `DECL_REGS` (`zx-evo-unreal/Unreal/debugger/dbgbpx.cpp:74-124`):

| Token | Size | Meaning | Set in |
|---|---|---|---|
| `RD` | 32 | address of the current/last memory read, `0xFFFFFFFF` after the per-instruction check | `zx-evo-unreal/Unreal/z80_main.inl:63` |
| `WR` | 32 | address of the current/last memory write | `zx-evo-unreal/Unreal/z80_main.inl:83` |
| `MDT` | 8 | data byte of that read or write (for TSConf RAM, the byte delivered by the emulated CPU cache) | `zx-evo-unreal/Unreal/z80_main.inl:22`, `:48`, `:59`, `:84` |
| `PG0`..`PG3` | 8 | TSConf `Page0`..`Page3` (bank in each 16K window) | `zx-evo-unreal/Unreal/debugger/dbgbpx.cpp:85-88` |

Examples *(constructed from the grammar)*: `WR==5B00 && MDT==0` (zero written to #5B00), `(RD & 0C000)==0C000 && PG3==20` (read from page #20 via window 3), `PC==8000 && PG2==5` (bank-qualified execution breakpoint). Because `debug_cond_check()` runs inside `rm()`/`wm()`, an `RD`/`WR` condition stops **at the access**, not at the end of the instruction (the debugger is still entered at the next instruction boundary by `debug_events()`) *(inferred from the call order)*. The tokenizer now accepts 4-character tokens and no longer excludes words starting with `M` (needed for `MDT`) (`zx-evo-unreal/Unreal/debugger/dbgbpx.cpp:186-224`).

Bank awareness of plain BPX/BPR/BPW is unchanged: 64K logical `membits` per CPU (`zx-evo-unreal/Unreal/debugger/dbgbpx.cpp:539-565`).

## 5. PC history

`TMainZ80::m1_cycle()` pushes `{page = comp.ts.page[pc>>14], addr = pc}` into a 32-entry ring on every opcode fetch, in both cores (`zx-evo-unreal/Unreal/vars.cpp:66-71`, `zx-evo-unreal/Unreal/defs.h:39-45`, `:219-220`). The "PC hist" pane at x=80 prints the newest 28 as `PP:AAAA` (`zx-evo-unreal/Unreal/debugger/dbgoth.cpp:233-249`, `zx-evo-unreal/Unreal/debugger/debug.h:64-67`). Page is the TSConf page register, so on non-TSConf models it is meaningless *(inferred)*. Prefixed opcodes record one entry per M1 (DD/FD/CB/ED prefixes included) *(inferred: every M1 goes through `m1_cycle`)*.

## 6. Banks pane editing

Pages pane rows are selectable with the mouse or `UP`/`DOWN` when focused; typing a hex digit (TSConf only) opens a 2-digit field and writes `comp.ts.page[selbank]`, then `set_banks()` (`zx-evo-unreal/Unreal/debugger/dbgoth.cpp:178-231`, `zx-evo-unreal/Unreal/debugger/debug.cpp:117-121`). Editing `#7FFD` (`Alt+B`) now also resets `comp.ts.vpage` to 5/7 (`zx-evo-unreal/Unreal/debugger/dbgcmd.cpp:158-167`).

## 7. Video, raster and DRAM bandwidth tools

- **Beam marker.** `update_raypos()` XOR-inverts `vbuf[vid.buf][vid.vptr]`, the pixel the renderer would write next, on debugger entry and after each step (`zx-evo-unreal/Unreal/draw.cpp:701-706`).
- **DRAM cycle accounting per line.** The renderer counts DRAM cycles per scan line into `vid.memcpucyc`, `memvidcyc`, `memtstcyc`, `memtsscyc`, `memdmacyc`; CPU cycles are counted on TSConf cache misses (`zx-evo-unreal/Unreal/z80_main.inl:34-51`, `zx-evo-unreal/Unreal/drawers.cpp:109-142`, `zx-evo-unreal/Unreal/tsconf.cpp:773`). `show_memcycles()` draws, in the left border of each line, one segment per client (CPU green, video yellow, tiles pink, sprites purple, DMA white) scaled 1 px per 4 cycles, overflow beyond 112 px in red (`zx-evo-unreal/Unreal/leds.cpp:540-587`). Enabled by `[VIDEO] Border=5` (448x320), both per frame and up to the beam line when stopped (`zx-evo-unreal/Unreal/mainloop.cpp:44-45`, `zx-evo-unreal/Unreal/draw.cpp:704-705`, `zx-evo-unreal/Unreal/config.cpp:467-469`). This is a per-line bus-contention visualizer for the TSConf DRAM arbiter.
- **Visuals window** (`main.visuals=F4`, emulation mode). 840x716 32-bit window, redrawn every frame (`zx-evo-unreal/Unreal/mainloop.cpp:110`): 16x16 CLUT swatches (`vid.clut`) at x=599; mode 1 = all 85 sprites from SFILE, each drawn from `SGPage` with its own palette and size (flips not applied, `// !!! add flips`); modes 2-4 = full 64x64 tile graphics page (sprite page, tile0 page with T0PAL, tile1 page with T1PAL) in a 9 px grid (`zx-evo-unreal/Unreal/visuals.cpp:156-281`). The header lists planned features (CRAM values, hints, editing, per-tile palette from TileMap) (`zx-evo-unreal/Unreal/visuals.cpp:8-40`).
- **TSU layer toggle** (`main.tsutoggle=ALT CONTROL TIL`): modeless checkboxes that AND the S/T0/T1 enables and force border-only for GFX, guarded by a critical section (`zx-evo-unreal/Unreal/tsconf.cpp:37-40`, `:792-796`, `:978-1033`).
- **Screen inset.** `Alt+S` toggles watches vs a ZX-screen render of page 5/7 (`scrshot_page_mask` via `Alt+Shift+S` or `F9`/`Shift+F9`) with TSConf border color; the classic "ray-painted" inset and `Alt+F9` full-screen views were removed ("ray-painted is already available in main emu window") (`zx-evo-unreal/Unreal/debugger/dbgoth.cpp:76-81`, `zx-evo-unreal/Unreal/debugger/dbgcmd.cpp:118-147`, `:277-278`).

## 8. Hotkeys (default `zx-evo-unreal/Unreal/cfg/Unreal.ini` `[SYSTEM.KEYS]`, lines 711-922)

Same chord mechanism and action names as classic. Differences:

| Action | Classic default | TS-Labs default |
|---|---|---|
| `main.monitor` (break in) | `ESC` | `TIL` (`~`) |
| `mon.emul` (run) | `ESC` | `TIL` |
| `main.visuals` | — | `F4` |
| `main.selectfilter` | `F4` | `CONTROL F4` |
| `main.tsutoggle` | — | `ALT CONTROL TIL` |
| `main.saveram` | — | `ALT F9` (writes `ram.bin`) |
| `main.shotclipbrd` / `main.status` / `main.flictoggle` | — | `SHIFT ALT F8` / `ALT GRDIV` / `CONTROL SHIFT F4` |
| `main.lockmouse` | `MMB` | `SHIFT ESC` |
| `mon.scrshot` | `ALT S` (3-way) | `ALT S` (watches <-> screen memory) |
| `mon.scrshot_alt` | — | `ALT SHIFT S` (main/alt screen in inset) |
| `mon.screen` / `mon.altscreen` | `F9` / `SHIFT F9` full-screen views | both map to `mon_scr0` = show/flip the inset |
| `mon.rayscreen` | `ALT F9` | still bound, but `mon_scr()` body is commented out (no-op) |
| `mon.switchdump` | cycles incl. comppal | cycles mem/phys/log/cmos/nvram |
| `mon.maxspeed` | — | action exists, no ini binding |
| banks window | — | `UP`/`DOWN` select, `ENTER` or hex digit edit page |

Everything else (`F7` step, `F8` step over, `F4` run to cursor in CPU window, `F11` exit sub, `SPACE` BPX, `Alt+C` manager, `Ctrl+J` labels, `Ctrl+L` show labels, `Alt+O` watches, `Alt+T` ripper, `Ctrl+~` CPU, register/memory keys) matches classic (`zx-evo-unreal/Unreal/cfg/Unreal.ini:772-922`, `zx-evo-unreal/Unreal/keydefs.cpp:88-259`). Note `F4` means "visuals" in emulation and "run to cursor" in the monitor.

## 9. Notable and unique ideas

1. **Per-scanline DRAM bandwidth bars by bus client** (CPU/video/tiles/sprites/DMA) drawn in the border, live and up to the beam when stopped — a contention/arbitration visualizer; the TSConf analogue of a ULA contention view and directly useful for unreal-ng's raster/contention tooling.
2. **Step updates video/TSU/DMA to the exact T-state and marks the beam pixel** — stepping through multicolor/raster code shows the frame being built.
3. **Decoded peripheral register panel** (bit ranges + symbolic values + raw byte) for a whole chipset, including **programmed vs in-flight DMA** addresses/counters and INT line/column — a template for per-device state panels.
4. **Memory-access condition operands `RD`/`WR`/`MDT`** evaluated at the access, giving data-value watchpoints ("break when #5B00 gets 0") and range watchpoints in one expression; `PG0..3` make any breakpoint bank-qualified.
5. **Bank-tagged PC history** (`page:addr`) always recorded, even without breakpoints — cheap "how did I get here".
6. **Graphics-asset viewer** for tiles/sprites/palette plus **runtime layer masking** — standard in modern retro debuggers, rare in Spectrum ones.
7. **Debugger in its own window** with menus and 2x scaling while the emulated screen stays visible.
8. **Command-line `-b` / `-l` breakpoint and label files** for build-and-debug loops.

## 10. Gaps and caveats

- TSConf panel is read-only; no editing of TS registers, CRAM or SFILE from the debugger, and the memory editor lost its palette mode (`zx-evo-unreal/Unreal/debugger/debug.h:108`). No TSConf-specific breakpoint kinds (DMA start/end, line INT, TSU events) — only expressions over `PGn`.
- MemConfig "LCK128" shows `comp.ts.s_en` (sprite enable) instead of `comp.ts.lck128`; the `d_lock` name table is unused (`zx-evo-unreal/Unreal/debugger/dbgtsconf.cpp:12`, `:124`, `zx-evo-unreal/Unreal/tsconf.h:438`).
- Condition checks inside every `rm()`/`wm()` multiply the per-access cost by the number of conditional breakpoints; the code itself flags it as slow (`zx-evo-unreal/Unreal/z80_main.inl:64`, `:85`).
- Compiled scripts store pointers in `unsigned` (`cbp[MAX_CBP][128]` of `unsigned`, `*dst++ = unsigned(regs[r].ptr)`), so conditions only work in 32-bit builds *(inferred: pointer truncation on x64)* (`zx-evo-unreal/Unreal/defs.h:254`, `zx-evo-unreal/Unreal/debugger/dbgbpx.cpp:210`).
- `save_pos[8] = { UINT_MAX }` initializes only slot 1; restoring an unset slot 2-8 jumps to `0000` instead of being ignored (classic initializes all eight) (`zx-evo-unreal/Unreal/debugger/dbgtrace.cpp:236-237`, `:484-491`).
- HALT branch resolution (`TWF_HALTCMD`) from classic 0.39.0 is absent; step-over on HALT is a plain step (`zx-evo-unreal/Unreal/debugger/dbgtrace.cpp:47-51`, `:52-147`, `:607-632`).
- `Alt+F9` "rayscreen" and `mon.altscreen` semantics changed or became no-ops while the ini comments still describe the classic behavior (`zx-evo-unreal/Unreal/cfg/Unreal.ini:802-804`, `zx-evo-unreal/Unreal/debugger/dbgcmd.cpp:118-147`); `mon.maxspeed` has no default binding, so startup reports "keydef for mon.maxspeed not found" (`zx-evo-unreal/Unreal/keydefs.cpp:135`, `zx-evo-unreal/Unreal/config.cpp:1143`).
- The watch dialog's ROM checkbox is initialized from `trace_ram` (bug inherited from classic) (`zx-evo-unreal/Unreal/debugger/dbgbpx.cpp:643-644`).
- PC history covers only the main CPU and stores the TSConf page even on other models; 32 entries, not configurable.
- Visuals: sprite flips not applied, per-tile palettes (`vis.pal_map`) never filled (writes are commented out), window cannot be closed via its close box (`WM_CLOSE` swallowed) (`zx-evo-unreal/Unreal/visuals.cpp:60-63`, `:241`, `zx-evo-unreal/Unreal/tsconf.cpp:631`, `:669`).
- The upstream todo list still has "Add all models resources to Debugger" (`zx-evo-unreal/Unreal/doc/todo.txt`).
