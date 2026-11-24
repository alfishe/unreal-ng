# TS-Conf + VDAC4

How unreal-ng emulates the VDAC4 board for ZX Evolution / TS-Conf, so that the software written for it runs:
DOOM-VDAC4, R-Type VDAC4, HMM2-VDAC4, the ESP App SPG demos and the VDAC2 programs that the board's FT812
emulation also runs.

Date: 2026-11-15. Status 2026-11-23: phases 1-5 on master (DOOM and R-Type run, the ESP code through a JIT by
default); phase 6 without TTD. Open items: [TODO.md](TODO.md); the status per phase: §11.

Research notes, with a source reference for every fact (cited as R1 §n, R2 §n, R3 §n):

- [research-z80-esp-interface.md](research-z80-esp-interface.md) (R1): what the Z80 sees of the ESP. Covers the SPI
  slave, the link layer, the frames, the module loader and the per-game command sets.
- [research-app-abi.md](research-app-abi.md) (R2): the ESP application ABI (`zm_core_t` / `zm_app_t`), how the firmware
  implements it, the ELF loader, the exported symbols and what each game uses.
- [research-tang-video-ft-core.md](research-tang-video-ft-core.md) (R3): the Tang Nano 20K side. Covers the FT812
  emulation core against eve-emu, the BL616 coprocessor, the LINK registers and video switching.

## 1. What the board is

VDAC4 ([andrewinsidelazarev/VDAC4](https://github.com/andrewinsidelazarev/VDAC4), MIT) sits on the Evo's IDE
connector. It has **no FT812 chip**. It carries:

| Part | Role | Seen by the Evo as |
|---|---|---|
| Tang Nano 20K (Gowin GW2AR-18) | Multiboot FPGA, 1 MB slots in its own flash. Slot 0: TS-Conf video capture to HDMI 720p50. Slot 1: an FT812 emulation (RTL port of eve-emu). Slot 2: ROM font image. Slot 3: MJPEG video core (R3 §0) | The FT812 on the Evo SPI (`#77` D2), the same chip select as VDAC2. Present only after the switch to slot 1 (§5.3) |
| BL616 (RISC-V MCU on the Tang module) | The FT812 coprocessor (RAM_CMD consumer), written in C after eve-emu, plus VDAC4 3D commands and image decoders (R3 §1.4) | Through the FT812 registers only |
| ESP32-S3 Zero N8R8 (Xtensa LX7 × 2, 240 MHz, 8 MB flash, 8 MB PSRAM) | SPI Slave HD of the Evo (`#77` D4). Wi-Fi, file servers, sound, and **a loader for ELF applications** that the Z80 sends it. Second host of the FT812 emulation over the LINK QSPI bus (R1, R2) | A ZiFi binary-protocol device on the SPI bus |
| PCM5102 DAC | I2S output of the ESP | (audio only) |

The games run **on the ESP**. DOOM, R-Type and HMM2 are Xtensa ELF shared objects. A Z80 launcher (SPG) uploads
them to the ESP, starts them, serves their file reads from the Evo SD card and forwards the input. The ELF draws
through the Tang's FT812 emulation and plays sound through the DAC (R1 §7, R2 §6).

The Evo runs the TS-Labs **VDAC2 firmware build** (`quartus_vdac2`, `ESP32_SPI`). STATUS `VDAC_VER` is 6 in that build
(hardware-spec §0.1, vdac2-tdd §2.4).

## 2. Scope and goals

In scope:

- **G1.** Machine `TSL-VDAC4` (alias `TSCONF-VDAC4`) on every surface, like `TSL-VDAC2`.
- **G2.** The Tang device: TS-Conf picture by default; the FT812 personality on demand; M_SEL switching; fallback;
  ESP host mode; the LINK register file the ESP uses.
- **G3.** A VDAC4 profile of eve-emu: the documented behaviours where the Tang/BL616 FT812 differs from a real FT812
  (§5.2).
- **G4.** The ESP as an SPI slave. Covers the shared buffer, the link layer and the ZiFi frames (reusing the emulator's
  ZiFi code). Adds the module loader, its cache and the application commands.
- **G5.** An Xtensa LX7 interpreter (own library, `core/src/3rdparty/unreal-xtensa`) with the options the ESP32-S3
  apps use.
- **G6.** HLE of the ESP firmware core: the `zm_core_t` table, the libc/libm/libgcc symbol table, a FreeRTOS-like
  scheduler on two virtual cores, PCM into the mixer, `ft_*` and `link_*` into the Tang device, `module_open`.
- **G7.** TTD, automation planes, tests, performance.

Acceptance, in order:

1. VDAC2 software through the Tang FT812: Weather 2 screensaver, Zuma Deluxe VDAC2, R-Type Arcade VDAC2.
2. `HSD.ELF` (2 imports) and `BOING.ELF` via the ESP App SPG.
3. **DOOM-VDAC4 1.1 with DOOM1.WAD (shareware)**: boots from `_DOOM.SPG`, plays, saves.
4. R-Type VDAC4 1.0, HMM2-VDAC4 (with OGG.ELF music). HMM2's data file `HMM2.DAT` is not shipped; the owner supplies it.
5. DEMO3D (BL616 3D commands).

Out of scope, first round:

- Wi-Fi services of the ESP on this board (FTP/SMB/OTA/updates). The UART ZiFi module keeps them.
- Tang overlays (ESP menu) and the MJPEG video core (slot 3). FTVIEW4 needs the video core.
- Tang microSD (`sd_*`, `sd2_*`). No target app uses it (R2 §6.3).
- HDMI cosmetics: 1366×768, ASPECT, dithering, frame drops.

## 3. Decisions

| # | Decision | Why |
|---|---|---|
| D1 | `TSL-VDAC4` = TSL model + `ts_vdac = 7` (the stock VDAC2 build with `ESP32_SPI`: `#77` D4 selects the ESP), no IDE. A separate flag `ts_vdac4` selects the Tang and the ESP in the FT812's place | The board runs on the stock TS-Labs VDAC2 Evo firmware (VDAC4.md "Что нужно со стороны Evo"); STATUS reports 7 as with VDAC2. The first draft said `ts_vdac = 6`: wrong, corrected 2026-11-15 |
| D2 | The FT812 personality is eve-emu with a **VDAC4 profile** selected at runtime (`EveConfig` model / flags). No compile-time macro | The third-party patch uses `#ifdef EVE_VDAC4_PALETTED8`. A runtime profile keeps the FT812 of `TSL-VDAC2` untouched in the same binary (R3 §1.5) |
| D3 | The ESP is HLE: **its firmware is not run**. The applications are run on an Xtensa interpreter; the firmware core is native C++ | The app ABI is a clean function table (`zm_core_t`), versioned, with no ESP-IDF symbols by name. The apps import only ~30 libc/libgcc names (R2 §1, §6.2). Running ESP-IDF would need the whole ESP32-S3 SoC |
| D4 | Own Xtensa interpreter, vendored as a library like `unreal-z80` | Needs: deterministic, embeddable, compatible license, TTD-serialisable. QEMU (GPL, whole SoC) does not fit; Unicorn has no Xtensa |
| D5 | ESP time is derived from emulated time (240 MHz, one cycle per instruction, §7.4). CCOUNT, `millis()` and the FreeRTOS tick come from it | Determinism (TTD, replay, tests). The board's real speed (PSRAM cache misses) is not modelled |
| D6 | The ESP runs **one quantum behind** the Z80 on a worker thread (§7.5). Its effects on the SPI buffer, the FT812 and the mixer are applied at quantum boundaries in emulated-time order | Speed (DOOM is CPU-bound on the ESP) without giving up determinism |
| D7 | Math (`sinf`, `powf`, `log10f`, `sin`, `pow`, …) is a port of the newlib/fdlibm C sources, built with `-ffp-contract=off`. Host libm is never called | Project rule: bit-exact on all platforms. newlib is what the board runs, so results also match the board |
| D8 | The ESP's flash module cache is a host directory per instance, not TTD state. TTD records the cache key (name, size, FAT stamp) | The cache is only an accelerator. A missing entry makes the launcher send the ELF again (R1 §5) |
| D9 | Input needs nothing new: the Z80 launchers read Kempston, mouse and the PS/2 FIFO themselves and forward them | R1 §8 |
| D10 | The 3D commands use the same deterministic math as D7 (port of `ft_3d.c` with own `sinf`/`cosf`/`sqrtf`) | R3 §1.4.1 bit-exactness hazard |

## 4. Machine and configuration

- `machinevariants.cpp`: `{"TSL-VDAC4", "TS-Conf + VDAC4", "TS-Conf with the VDAC4 board: Tang Nano 20K (TS-Conf
  HDMI + FT812 emulation), ESP32-S3 application processor", "TSL", 4096, ApplyTsConfVdac4, IsTsConfVdac4,
  TsConfVdac4Supported}`. Alias `TSCONF-VDAC4`.
- `CONFIG`: `ts_vdac = 7` and `ts_vdac4 = true`. ini `[MISC] TS_VDAC4=1` sets both. `IsTsConfVdac2` requires
  `!ts_vdac4`, so the two variants never match the same config.
- ini, section `[VDAC4]`:
  - `RomImage`: ROM fonts, shared with `[VDAC2]` by default;
  - `RamG`: `2M` (default), `3M`. This is the power-on value; the ESP and apps switch it through `FT_CFG`;
  - `EspCache`: the ESP's LittleFS (module cache, `path.cfg`). Default: in the desktop app `~/.unreal-ng/vdac4/zm`,
    which outlives the app like the board's flash; headless instances and tests get an empty folder of their own per
    board, removed with it, so every run of a program starts from the same flash;
  - `EspMhz`: 240;
  - `EspThread`: 1 = the ESP app's jobs on a worker thread, 0 = on the emulation thread (§7.5);
  - `FtThread`: 1 = the Tang / FT812 work of those jobs on a thread of its own, 0 (default) = on the job's thread;
  - `FtDrawThread`: 1 (default) = the FT812's drawing on the eve-emu renderer thread, 0 = where the FT812 runs
    ([eve-async-drawing.md](eve-async-drawing.md));
  - `EspLatencyMs`: 1-16, default 4: emulated ms the app's outputs reach the firmware after its job (§7.5);
  - `EspBootMs`: ESP boot time after power-on, when it arms the Tang (§5.3), default 400;
  - `EspLog`: app log file, empty = off;
  - `LoadDelayMs`: Tang slot switch time, default 174 (R3 §2.2).
- `ENABLE_VDAC4` CMake option (ON). It needs `ENABLE_VDAC2` (eve-emu) and builds `unreal-xtensa`. With it off,
  `TSL-VDAC4` is not offered and a config asking for it fails with a reason, as VDAC2 does.
- Isolation rule from VDAC2 kept: shared files (`zcontrollerspi`, `screen`, automation modules) never name VDAC4; they
  see SPI devices, a picture source, a device-state report.

## 5. The Tang device (`Vdac4Tang`)

`core/src/emulator/platforms/tsconf/vdac4/vdac4tang.{h,cpp}`. It owns the FT812 personality (an `EveChip` with the
VDAC4 profile) and the LINK register file. It is the SPI device in slot 1 of the Z-Controller hub (mask `#04`, active
high), exactly where `Vdac2Card` sits.

### 5.1 Reuse of `Vdac2Card`

`Vdac2Card` already solves time conversion, event stepping, INT_N edges, picture latching at the FT812 rate, capture
and TTD for an `EveChip` on the Evo SPI (vdac2-integration-design §5-§9). These parts are factored out into
`EveHostCard`, which takes the chip, its ROM and its clock.

- `Vdac2Card` becomes "an `EveHostCard` that is always present".
- `Vdac4Tang` becomes "an `EveHostCard` plus the slot state machine and LINK". The chip object always exists (fixed
  TTD blob sizes); entering FT powers it on and runs the core's own start-up (CLKEXT, CLKSEL 0x48, ACTIVE), entering
  TS powers it on again, so the FT core's memory is gone.

The refactor lands first and on its own, with the VDAC2 tests unchanged (§11 phase 1a).

### 5.2 VDAC4 profile of eve-emu

Hooks H1-H10 of R3 §1.5. What is implemented, by visibility:

| Behaviour | Implement | Ref |
|---|---|---|
| `EVE_MODEL_VDAC4` row in the chip table; `EveConfig.flags` size-guarded | yes | R3 H1 |
| RAM_G 2 MB, runtime 3 MB (`FT_CFG` bit 0 / STAT bit 27); global `kRamGSize` becomes `table.ramGSize` + a runtime limit | yes | R3 F11, H2 |
| ROM fonts at `0x200000-0x2FFFFF`, read-only, only in 2 MB mode. ROM_FONTROOT is written after the "font copy" (§5.3) | yes | R3 F12, H3 |
| Core reset keeps RAM_DL / RAM_CMD; host traffic ignored for 128 clocks | yes | R3 F2, F3, H4 |
| Starts ACTIVE at 64 MHz after the slot switch; REG_ID is the constant `0x7C` | yes | R3 F1, F9, H5 |
| PALETTED8: one ARGB8888 entry (memory B, G, R, A), low 2 bits of PALETTE_SOURCE ignored; palette cache keyed by profile | yes. The patch's two places plus the alignment rule | R3 §1.3, H6 |
| Coprocessor table: + PROGRESS, B0 LOADIMAGE_BOX, B1 IMAGEINFO, A0-AF 3D. SETFONT2, ROMFONT and PLAYVIDEO fault. 04 and 07 fault | yes | R3 §1.4, H7 |
| CMD_TEXT waits for ROM_FONTROOT (up to 6 s), then draws without fonts | yes | R3 §1.4 |
| Images: OPT_FULLSCREEN = 1024×768 RGB565 canvas; error words; progressive JPEG, PNG 1/2/4/16-bit + Adam7, WebP | phase 5. Baseline formats work from phase 1 | R3 §1.4.2, H8 |
| INT_FLAGS read keeps a swap that lands after the read | yes | R3 F15 |
| Renderer omissions (sloped lines, EDGE_STRIP, MACRO, TEXT8X8/VGA not drawn) | **no**. eve-emu draws them. Revisit only if a program depends on the gap | R3 H9 |
| HDMI 1366×768, dithering, frame drops, RAM_G-write cancels | no (cosmetic) | R3 §3.6 |

The picture goes to the Screen as the external picture, as with VDAC2: 1024×768 at the FT812 rate.

### 5.3 Personalities and switching

State: `personality` ∈ {TS (1), Loading, FT (2)}, `armed`, `espHost`, `ramG3M`, `fontsReady`, the idle timer. Every
transition is timed in emulated time (raster tacts, as Vdac2Card's clock).

| From | Event | To | Visible effect | Ref |
|---|---|---|---|---|
| TS | Evo FT812 ACTIVE or memory access ≥ 16 SCK, while armed. ESP `ft_open` arms and boots | Loading | MISO 0 | R3 §3.2 |
| Loading | `LoadDelayMs` elapsed | FT | Chip powered ACTIVE at 64 MHz; RAM_G zero; CHIPID written; `fontsReady = false` | R3 F1, §2.2 |
| FT | Font copy done: ESP firmware copies VD4FONT1, emulated as a delay (font image size / LINK rate) | FT, `fontsReady` | ROM_FONTROOT valid | R3 F12 |
| FT | M_SEL = 0 and no Evo FT traffic for 500 ms, and not `espHost` | TS | Chip destroyed (RAM_G, DL and registers gone) | R3 §3.3 |
| FT | ESP `core_back` | TS | same | R2 §2.7 |
| any | Evo reset | TS picture at once; FT falls back by the idle rule | | R3 §3.4 |

- Arming. The slot-0 core resets with `boot_armed = 0` (`ts_fb_core.sv:568-580`). The ESP firmware writes "ARM1" right
  after its own boot and after every Tang reload, when slot 1 holds a bitstream (`tang_link.cpp:419-432, 578, 622`).
  It disarms only while the Tang SD or flash is busy (WC VFS, updates). The model therefore arms at the ESP's boot
  time, a constant after power-on (`EspBootMs`, default 400). VDAC2 software that pokes the FT812 earlier sees MISO 0
  and retries, as on the board.
- `espHost` (`FT_CFG` bit 2): Evo FT reads return 0, writes are dropped, INT_N is inactive, the idle timer is frozen
  (R3 §3.5).
- INT_N reaches the TS-Conf interrupt controller only while M_SEL = 1, as with VDAC2 (R3 F16).
- Picture: with M_SEL = 0 the Screen shows TS-Conf. With M_SEL = 1 it shows the FT picture in personality FT and black
  otherwise.

### 5.4 LINK register file (ESP side)

The ESP HLE (§8) calls `Vdac4Tang::LinkRead/LinkWrite(addr, words, n)` and `LinkBurst`. Registers modelled (R3 §2):

| Addr | Register | Model |
|---|---|---|
| `0x00` | ID | `0x56444334` "VDC4" |
| `0x04` | VERSION | personality in [31:24], protocol 01, core version `0x012D` (≥ `0x011D`, so the burst path) |
| `0x08` | STATUS | bit 12 = M_SEL |
| `0x28 / 0x2C` | FT_RSTAT / FT_RTIME | frame serial; render time = eve-emu's line-budget metric of the last frame |
| `0x30` | CP_INFO | `C0290201` (BL616 firmware 41) |
| `0x38` | FT_FREE | always 512 free, Gray-coded |
| `0x50` | FT_SEEN | Evo FT traffic marker |
| `0x54 / 0x58` | FT_ADDR / FT_DATA | ESP window into the FT address space: bus access with the chip brought to the access time |
| `0x5C` | FT_STAT / FT_CFG | STAT bits 31, 30, 29, 28, 27, 13, 12, 11:0. CFG bit 0 = 3 MB, 1 = no dither (stored), 2 = ESP host, 3 = ROM write |
| `0xE8` | FT_HOST | host commands from the ESP (ACTIVE, CLKEXT, CLKSEL, CORERST) |
| `0xF0` | BOOT | ARM1 / ARM0 / BOOT, own and next slot |
| `0x40000-0x7FFFF` | FT_BURST | stream writes to FT space |
| others | | read 0, writes ignored. Overlays are logged once as unimplemented |

## 6. The ESP on the SPI bus (`Vdac4Esp`)

`core/src/emulator/platforms/tsconf/vdac4/vdac4esp.{h,cpp}`: a `SpiDevice` in slot 2 of the hub, mask `#10`, active
high. `#13` selects it, `#03` deselects it (R1 §1).

### 6.1 Layers

| Layer | Class | Source |
|---|---|---|
| SPI Slave HD: per CS window decode `01 WRBUF`, `02 RDBUF`, `03 WRDMA`, `04 RDDMA`, `07`, `08`. Address byte, one dummy byte, 64-byte shared buffer. Other commands ignored | `EspSlaveHd` | R1 §2 |
| Link: request/response mailbox at 0..59, status word 60..63 (`5A/A5`, `D7`, version 3, boot nonce), rings 16 KB in / 4 KB out. Bulk 512, put, media up to 8192, replayed tid, NEW_SESSION | `EspSpiLink`, a port of the firmware's `SpiLinkCore` (portable C++, MIT, has PC tests) | R1 §3 |
| Frames `5A CMD LEN DATA XOR`, odd frames padded with `00`, `EE` errors, `FE` ACK | the emulator's ZiFi frame code, moved out of `ZiFiNativeModule` into `ZiFiFrameLink` behind a byte-link interface. Serves both the UART module and this one | R1 §4, §9 |
| Core commands: ECHO, PING, GET_STEP, SYS_INFO, SYS_RESET, SYS_CLOCK, PATH_GET/SET, LINK_PROBE. Network commands answer "no network" (§2) | `Vdac4EspCore`, sharing handlers with `ZiFiNativeModule` | R1 §4 |
| Module loader: MOD_QUERY 69, MOD_PUT 6A, MOD_COMMIT 6B, APP_START 6C, APP_STOP 6D. Cache of 24 entries keyed by name + size + FAT stamp; 2 MiB per module; ELF validation as `elfImageProblem` | `EspModuleLoader` | R1 §5 |
| App command routing (`command_first..last`, media range), events to the Z80 (≤ 63 bytes, 8-deep queue) | `EspAppHost` (§8) | R1 §5, R2 §3 |

**Time.** The SPI side runs on the emulation thread at the Z80's access time. A WRBUF that completes a request is
queued for the ESP. The ESP answers it at its next quantum boundary (§7.5): the response head (`3C` marker) becomes
visible after the body, as the firmware writes it (R1 §3.1). The Z80 driver polls up to 2048 times per turn, so a
latency of one quantum (≤ 1 ms of emulated time) is within what the drivers expect.

**DMA.** The TS-Conf DMA SPI modes already exchange per byte through the hub (R1 §2). The 03/04 phases need no new
DMA code.

**Shared FT812.** The launchers of HMM2 and R-Type write RAM_G themselves over the Evo SPI (`#77 = #07`) while the
ESP app draws over LINK (event E6, R1 §7.4). Both hosts reach the same `EveChip` through `Vdac4Tang`. Accesses are
applied in emulated-time order (§7.5).

## 7. Xtensa interpreter (`unreal-xtensa`)

`core/src/3rdparty/unreal-xtensa/`, namespace `xtensa`. It has no emulator dependencies, so it can be tested alone.

### 7.1 ISA

ESP32-S3 configuration of the LX7 core, as GCC uses it for these apps:

- core ISA;
- code density (16-bit narrow forms);
- windowed registers: 64 physical AR, ENTRY / RETW / CALL4/8/12 / CALLX* / MOVSP / ROTW / S32E / L32E, WindowBase and
  WindowStart;
- MUL16, MUL32, MUL32_HIGH, DIV32;
- zero-overhead loops (LOOP, LOOPNEZ, LOOPGTZ, LBEG / LEND / LCOUNT);
- booleans (b0-b15);
- single-precision FPU, including MADD.S / MSUB.S as fused operations (`std::fma` gives the exact single rounding);
- NSA / NSAU, MIN / MAX / MINU / MAXU, SEXT, CLAMPS;
- S32C1I with SCOMPARE1;
- MEMW / EXTW / ISYNC / DSYNC as no-ops;
- RSR / WSR / XSR for SAR, LBEG / LEND / LCOUNT, SCOMPARE1, CCOUNT (read only), PS fields the apps touch;
- RUR / WUR for THREADPTR, FCR / FSR.

Not implemented; the interpreter stops the task with a diagnostic naming the PC:

- privileged instructions;
- the PIE / SIMD `EE.*` extension;
- MAC16;
- the approximation instructions RECIP0.S / RSQRT0.S / DIV0.S / SQRT0.S. GCC calls `__divsf3` and `sqrtf` instead; the
  apps import them (R2 §6.2).

App sources contain inline asm only for `rsr.ccount` and `rur.threadptr`, and no SIMD (checked in DOOM, R-Type, HMM2).

**Census (done 2026-11-15)**, `xtensa-esp-elf` 16.1.0 objdump, control-flow descent from the function entries (a
linear sweep counts literal pools as code and reports fake `EE.*` / MAC16):

- DOOM, OGG, HSD: core, density, windowed calls, MUL16/32, MULSH, DIV32, LOOP, booleans, single FPU, S32C1I,
  L32AI / S32RI. OGG writes LBEG / LEND and reads LCOUNT, and has one privileged `wsr.eps2` (unreached at run time);
- R-Type and HMM2 add WSR.SCOMPARE1, SUBX2, MOVF.S, BALL / BNALL, OLE.S / OEQ.S, SSA8B, CALL0, CALL12, RSR.CCOUNT,
  RUR.THREADPTR and JX. HMM2 has 12 `jx` (switch tables), so descent leaves part of it unvisited;
- no `EE.*` and no MAC16 reached. Three single hits (`ee.vld.128.ip`, `ee.vmulas.u8.accx.ld.ip.qup` in R-Type,
  `ee.ldf.64.xp` in HMM2) look like data decoded after a `jx`; the interpreter stops on them with the PC, which
  settles it at run time.

### 7.2 Execution engine

- **Fetch.** The ESP32-S3 sees PSRAM twice: the D-bus at `0x3C000000` and the I-bus at `0x42000000`, an offset of
  `0x06000000` (R2 §4). Both map to one backing store; internal RAM and the trap page are separate regions.
- **Decode.** Predecoded basic blocks, keyed by PC, cached and invalidated on writes to code pages. JMP_SLOT literals
  live in `.text` (R2 §6.1).
- **Window spills.** The interpreter holds 64 physical registers. On window overflow or underflow it performs the
  spill or fill the standard handlers would do, natively, to the base save area below the callee's SP. This keeps the
  stack bytes the same as on the board, so `setjmp` / `longjmp` and stack walking keep working.
- **Traps.** Every imported symbol and every `zm_core_t` function pointer points into a trap page. Executing a trap
  address calls the native function with a0-a7 / a2-a7 of the windowed CALLx ABI and returns through the RETW rules.
- **Budget.** Run N instructions, or run until a blocking HLE call (`delay`, `signal_wait`, `mutex_lock`, full
  `pcm_write`, `event` with wait) yields the task.
- **State.** Plain structs: registers, special registers, block cache excluded. Serialisable for TTD.

### 7.3 Speed target

DOOM at 20-30 fps on the board means the ESP's core 1 is busy. The interpreter must reach at least 240 M
instructions/s for one core on the reference Mac (M-class) to keep emulated time at 100 %. The plan is a predecoded
threaded interpreter. A JIT is not planned. If the target is missed, D5's "1 cycle per instruction" stays and the
emulation runs below 100 % while the ESP is busy (measured in phase 4).

### 7.4 Time

- ESP cycle = instruction (D5).
- A core with no runnable task idles (WAITI semantics) and burns no host time.
- `CCOUNT` = cycles of that core.
- `millis()` = emulated time.
- The FreeRTOS tick is 1 kHz (R2 §2.1).

### 7.5 Quantum model (implemented: jobs)

The plan was quanta of `Q` = 1 ms with the ESP one quantum late. What runs is that idea cut to what DOOM needs:
the Z80 side never waits on the ESP, the ESP's work runs off the emulation thread, and the result does not depend on
host timing or on which thread did the work.

**Jobs.** While the app owns the board's FT812 (the Tang runs the FT core with the ESP as its host), each
`EspXtensaRuntime::Poll` (once per emulated ms, from the firmware) posts a *job*: "advance the ESP to `t`".

- A job's board accesses (LINK reads and writes) happen at the job's *stamp*: the Tang's raster position when it
  was posted (`EveHostCard::PinNow`). At its end the FT812 is drawn up to that stamp (`Synchronize`).
- What the app hands the firmware during a job (events, PCM, log lines, `ft_open`, `link_media`) is kept with the
  job and replayed on the emulation thread `EspLatencyMs` Polls after the post, in job order.
- What the app asks is answered from the job's own inputs: `core->event` takes as many events as the firmware's
  queue had room for at the post, less what the jobs not yet replayed took; `core->reply` and `core->app_done`
  answer as outside a command (a job never runs inside one).
- A command, an app start or stop, and the frame end flush: wait for every job, replay everything. A machine frame
  ends with the ESP settled, so TTD checkpoints, snapshots and the published state never see a job in flight.
- Before the emulation thread touches the Tang (Z80 port access, the FT detach guard, reset, frame end) it waits for
  the jobs (`Vdac4Tang::SetEspWait`); `TakeIntEdges` answers nothing while a job is out (INT_N is held inactive in
  this mode anyway).
- Once the newest replayed job says the app no longer owns the FT812, the runtime flushes and runs the ESP directly
  again, outputs at once.

**Threads.** `EspThread=1` runs the jobs on the runtime's worker; `EspThread=0` runs the same jobs at once on the
emulation thread. The replay rule is the same, so the two give the same machine bit for bit (unit test
`Vdac4EspJobs_Test.ThreadedEqualsInline`; DOOM's screen hash after 6300 frames is the same for both).
`FtThread=1` moves the Tang's part of a job (`Vdac4FtWorker`) to a third thread: the job queues its LINK writes with
their stamps and an end-of-job marker, the FT thread applies them in order; a LINK read waits until the queue is
applied and reads on the job's thread. Also bit for bit the same.
`FtDrawThread=1` moves only the FT812's line drawing and the frame conversion to the eve-emu renderer thread
(`EveSetAsyncDrawing`, [eve-async-drawing.md](eve-async-drawing.md)): the chip stays where it was, its drawing
inputs are queued; the picture, line costs, metrics and `REG_TAG` are the same bit for bit.

`EspLatencyMs` is part of the emulated behaviour (a run with 4 is not the run with 1, each is deterministic): it is
how far the worker may run behind the machine. 1 serializes the threads (the emulation thread waits for each job
it posted a ms earlier); 4 lets them overlap.

**Measured** (DOOM-VDAC4 in game, the shooting scene; Apple M-class build host, 300 frames from frame 6000 as fast as
possible, ms per frame, A/B rounds interleaved):

| Mode | ms / frame |
|---|---|
| before the jobs (all on the emulation thread) | 6.96 - 7.12 |
| jobs, `EspThread=0` | 6.90 - 7.05 |
| `EspThread=1`, latency 1 | 6.47 |
| `EspThread=1`, latency 4 | 5.96 - 5.99 |
| `EspThread=1 FtThread=1`, latency 4 | 6.15 - 6.21 |

Per frame, threaded (latency 4): the worker runs about 4.3 ms of jobs (about 20 jobs: the Xtensa interpreter and the
FT812 work), the emulation thread does about 2.5 ms of its own and waits about 3.5 ms. The ESP is the critical path;
at 50 fps (20 ms a frame) the waits cost nothing as long as the worker's share fits the frame. The FT thread does
not help: the app reads the FT812 about once per job (≈ 19 reads a frame) and each read waits for the queue, while
the FT812 work itself is only ≈ 0.9 ms a frame.

The counters are in the state route's `threads` block (§9).

FT812 accesses from both hosts are not mixed in this mode: with the ESP as host the Evo's SPI to the FT812 is cut off
(R3 §3.5).

## 8. ESP firmware HLE (`EspAppHost`)

### 8.1 ELF loader

Follows R2 §4:

- only `.text`, `.data`, `.rodata`, `.data.rel.ro`, `.bss` are loaded, section by section: HMM2 puts `.rodata` after
  `.bss`;
- `.text` goes into one PSRAM block, data into another;
- relocations: RELATIVE with the one-past-end rule, GLOB_DAT / JMP_SLOT to the symbol table, RTLD skipped;
- the loader's quirks are kept (`vdac4/esp/espelfloader.cpp`, a port of the firmware's path): the relocation type
  is compared against `STT_*` values when names are looked up, so GLOB_DAT and R_XTENSA_32 resolve through the
  firmware tables only; sections are searched in the order text, bss, data, rodata, data.rel.ro, and `.text` is
  rounded up to 4. In HSD.ELF the rounded `.text` covers the first 3 bytes of `.rodata`, so a pointer to the first
  string becomes an **I-bus** address. Both PSRAM views must therefore be byte-readable in the interpreter;
- weak undefined symbols resolve to 0;
- entry `main(3, {name, core, &app})` must return 1;
- `abi_major` 1, `size >= offsetof(ui_model)`, `start` present.

Modules (`module_open`, e.g. `OGG.ELF`) use the same loader with `main(3, {name, arg1, arg2})`.

### 8.2 `zm_core_t`

Filled as firmware ABI 1.16 (`size`, `abi_minor = 16`, `firmware = "unreal-ng"`):

| Group | HLE |
|---|---|
| alloc / realloc / free | PSRAM heap (8 MB minus loaded images), 16-byte tracked header as the firmware; `ZM_MEM_INTERNAL` from a 300 KB internal pool; freed on app stop. Raw `malloc` / `free` (libc table) untracked, ≤ 4 KB from internal, as the firmware |
| log | to the instance log and `EspLog`, 136 chars |
| millis / delay | emulated time; `delay` blocks the task for max(ms, 1) ticks |
| task_start, mutex, signal | §8.3; 8 tasks, 16 sync objects |
| reply / event | `reply` only inside `command`; `event` into the 8-deep queue, ZM_BUSY when full after `wait_ms` |
| pcm_* | int16 stereo ring of 256 KB at the open rate; `pcm_write` takes float, clamps, converts, blocks up to 500 ms. The ring feeds a mixer source (`ICardAudio` row "VDAC4 DAC") resampled by the mixer. `pcm_queued_ms` = ring + DMA depth (3840 or 1440 frames); volume 0..255 |
| module_open / close, resource / release | §8.1 loader; resources from the module cache |
| ft_open / write / read / host_cmd / close | `Vdac4Tang`: open = arm, boot to FT, wait for the load, `espHost` on, 1024×768@59 boot sequence (as `ft_host.cpp` `boot1024x768`); writes paced as one LINK burst |
| core_load / core_back, link_read / write / burst, link_media | `Vdac4Tang` LINK (§5.4); `link_media` sets the media command range |
| app_done, temperature, ui_available, wifi_*, net_*, sock_*, sd_*, sd2_*, handoff | app_done as firmware; temperature 450 (45.0 °C); the rest return ZM_NOT_FOUND / 0 |

**Symbol table.** Union of the three firmware tables (R2 §5), served by an HLE ROM (§8.4).

### 8.4 HLE ROM

Instead of reimplementing libc in C++, the ESP's C library is the real one: the esp32s3 newlib, libm and libgcc from
the Espressif toolchain, linked at `0x40800000` with a small C shim and embedded as generated data
(`esprom.cpp`). The interpreter runs it as guest code, so `snprintf`, string, math and soft-float results are the
board's bit for bit.

- `tools/vdac4/esp-rom/build.sh <toolchain>` builds it: `romshim.c` (`-O2 -mlongcalls -ffp-contract=off`), then
  `xtensa-esp32s3-elf-ld --gc-sections -Ttext=0x40800000` with every firmware export forced in (`exports.txt`) and
  `emit.py` writing `esprom.cpp` (image, export table, trap table, `_impure_data`).
- What the firmware does natively is a trap (`traps.txt`): one address per name at `0x40000000 + 4 * i`. The newlib
  system calls (`_write`, `_sbrk`, `_gettimeofday`, ...), `__getreent` (returns `_impure_data`), the whole
  malloc / heap_caps family (the firmware's heaps), `usleep` / `sleep`, the pthread entry points (fail: no app uses
  them through the table), and the shim's `zm_host_*` calls.
- The shim also holds the guest halves of `zm_core_t`: `log` (`vsnprintf` into 136 bytes, then one trap) and
  `ft_open` (the `ft_host.cpp` sequence: LINK ID, BOOT register switch to the FT core with 100 ms polls, `setHost`,
  `boot1024x768`), so its waits are ordinary blocking delays of the calling task.
- `zm_core_t` slot `i` is the trap `0x40000800 + 4 * i`; `log` and `ft_open` point into the ROM instead.
- The ELF resolver searches the export table, then the traps, and never resolves `zm_*` (shim internals).
- `setjmp` uses `SYSCALL` with `a2 = 0` (spill all windows), `longjmp` `WINDOWBASE` / `WINDOWSTART` / `RSYNC`: the
  interpreter implements both (§7.1).

### 8.3 Scheduler

- Two virtual cores. Each has FreeRTOS semantics: fixed priorities 0..24, preemptive, round-robin within a priority
  on the 1 kHz tick.
- Tasks are pinned to core 0, core 1 or any. "Any" is assigned to core 0 at creation; it never migrates, for
  determinism.
- Each task has its own stack, internal or PSRAM per `ZM_TASK_STACK_PSRAM`, and its own non-zero THREADPTR. HMM2's
  pthread emulation needs THREADPTR (R2 §6.6).
- The firmware's own contexts are virtual tasks with their real priorities:
  - **core 1:** the transport loop at priority 1. It calls `app->command` and serves frames. HMM2's session task
    shares priority 1 with it on purpose;
  - **core 0:** `zifi-net` at priority 2, calling `app->poll` every 2 ms;
  - the audio player at priority 4 (R2 §3).
- `stop`:
  1. call `app->stop()`;
  2. join the tasks for 500 ms;
  3. kill the rest;
  4. release everything in the firmware's order (R2 §3 lifecycle).
- Stop triggers: APP_STOP, `app_done`, 5 s without commands when the cache needs room, and the Tang falling back to TS
  (R2 §3).
- The 5 s task watchdog of core 0 is reported as a log warning, not a panic.

**Implemented so far (`espapphost.cpp`).** A simplification of the above: in each 1 ms slice the two
highest-priority ready tasks run (no pinning); host tasks are `loop` (priority 1: main, start, command) and `net`
(priority 2: poll every 2 ms while `loop` is idle). Stop kills the app tasks without calling `app->stop()` and
releases in the firmware's order. Per-task THREADPTR and pinning come with HMM2.

### 8.5 Wiring in the machine (implemented)

- `Vdac4Board` (`vdac4/vdac4board.{h,cpp}`) is what `PortDecoder_TSConf` holds next to the Tang when `[MISC]
  TS_VDAC4=1`: the `Vdac4Esp` device on SPI slot 2 (#77 D4, active high), the `EspXtensaRuntime` (the
  `IVdac4AppRuntime` over `EspAppHost` and the interpreter) and the ESP's LINK to the Tang.
- Time: the machine's emulated microseconds x 1000 plus an offset, so the ESP's clock never steps back. A machine
  reset (`Core::Reset`, which every SPG load does) zeroes the engine's t-states while the ESP keeps running, as the
  board's ESP does across an Evo reset: the first read after it is behind the last one (184 ms after an SPG load was
  seen), and the offset grows by the difference (as `Ds12887::RebaseEmulatedTime` does for the RTC). Without it the
  firmware's frame parser timed a frame out (an unsigned `now - last` wraps) and dropped the Z80's MOD_PUT. The
  state route reports `esp.clock_restarts` and `esp.clock_offset_us`. The board keeps its own time base, not
  `t_states + Z80::t`: the frames the CPU closed (`frame_counter`, which `Core` counts where it rebases `Z80::t`)
  plus the CPU's position in its frame. The sum is not monotonic: the main loop adds a frame to `t_states` at its
  frame end, while `Core` rolls the CPU over only once `Z80::t` reached the frame (a frame may close a few T-states
  early and finish in the next one), and the engine's rollover catches its DMA up (SPI to the ESP included) after
  the rebase. It made a back step every few frames (1.2 s of offset in 3 s of DOOM, every few dozen frames in
  R-Type) and the ESP's sound ran ahead of the mixer.
- Advance: once per machine frame from the engine's frame-end listener (the engine is up to date there; a LINK access
  from anywhere else catches the engine up first), plus every SPI select / exchange. The bus-access hook catches the
  engine up only when the engine is not already advancing (`TsConfEngine::Advancing`): a DMA RAM -> SPI word reaches
  the ESP from inside the engine, and a nested catch-up ran the DMA's next words before the current one finished. The
  ESP then received the bytes of a 2 KB block reordered (2 of 911 R-Type #31 replies, ~20 s into the attract loop),
  and the game stopped with "HQ cell unpack error". Regression test: `TsConfDma_Test.DMA14b_SpiDeviceSeesTheEngineAdvancing`.
- eve-emu image limits per model: CMD_LOADIMAGE on VDAC4 takes each side below 2048 with any pixel count (BL616
  `ft_image.c` IMG_WMAX), the FT812 keeps 1024 x 768 and the 524288 / 483328 pixel caps. The DOOM launcher's 1024 x 768
  JPEG background needs it.
- ROM fonts: the board's ESP copies the FT812 ROM font image into the Tang's SDRAM after each FT core load; the
  emulator loads the same image as VDAC2 (`[VDAC2] RomImage`, default `rom/ft81x.rom`, looked up next to the
  executable and in the app's resources) and marks the fonts ready at the core load. Without the image ROM_FONTROOT
  is valid but the metrics read zero: the DOOM launcher waits ~3 s for fonts and its screen has no text.
- Audio: the ESP's I2S player (`EspAppHost::PcmPlay`, in ESP time at the rate the app opened) feeds `Vdac4Audio`
  (`vdac4/vdac4audio.{h,cpp}`), an `ICardAudio` attached to `SoundManager` as the mixer row "VDAC4 ESP" (mixer key
  `vdac4_esp`). It resamples linearly to the mixer rate into a FIFO the frame end drains; an underrun holds the last
  level (no click), a FIFO beyond 3 frames drops its oldest samples (bounded latency). The state route's `audio`
  block: input / output rate, queued frames, underruns, dropped frames, sound in the last frame.
- `[VDAC4] EspMhz` sets the interpreter's clock (`EspAppHost` cycles per microsecond; default 240).
- State for any thread: the Tang, ESP, app and link fields are captured at each frame end on the emulation thread
  (`Vdac4Control::CaptureLive`) and published under a lock; the log and packet rings are copied under the same lock;
  the audio counters are atomics. A WebAPI read never parks the emulation thread.
- Not yet: saved state of the app runtime.
- Diagnostics: `GET /api/v1/emulator/{id}/vdac4/state[?log=N]` (`Vdac4Control`): Tang personality / FT_CFG, ESP phase
  and time, the app (module, name, version, command range, last runtime error), link counters (pending bytes, send
  blocked, bad checksums, resyncs, dropped frames), the Z80's last 64 packets, the ESP log ring (256 lines) and the
  `threads` block of §7.5 (modes, latency, job / FT counters and host time).

**First DOOM run (2026-11-16).** `TSL-VDAC4`, SD = host folder with `DOOM/_DOOM.SPG`, `DOOM.ELF`, `DOOM1.WAD`, the
SPG loaded through the snapshot loader: the launcher's FT812 boot screen, path.cfg, ELF upload (366 MOD_PUT) and
start, DOOM.ELF's own loading screen through the FT window, W_Init / R_Init over the Z80's WAD reads (#71, 1 KB each,
~36 KB/s), then the demo loop in 1024x768. DOOM's own counter: 22-30 fps of ESP time, render 4-6 ms per view. The
machine runs at about real time headless.

**R-Type and HMM2 (2026-11-16).** R-Type VDAC4 1.0 (`_rtype_vdac4.spg`, RTYPE.ELF, RTYPE.DAT): title, the attract
demos with HQ cells read from RTYPE.DAT through E4 / #31 (1264 chunks checked byte for byte against the file, 0 bad),
sound on the `vdac4_esp` row, back to the title. HMM2-VDAC4 without the game's `HMM2.DAT` (not shipped, built from the
original data): the launcher stops with its own "FILE NOT FOUND: HMM2.DAT" (`BootMessage` checked in Z80 memory). Its
FT error screen shows only the first glyph row of each letter. That is the launcher, not the emulator: its error
display list sets `BITMAP_LAYOUT` / `BITMAP_SIZE` of handle 0 without the `_H` words, and the splash before it left
the high stride / width bits there. The Tang's handles live across display lists and RST_PULSE does not clear them
(`ft_rd_walk.sv`, `ft_dl_snapshot`), as in eve-emu (`EveVdac4_Test.TransformMagnifiesAnL1GlyphOverItsCell` checks the
magnified glyph itself).

## 9. Media, files, tests assets

- The games read the Evo SD through the launcher, so the existing SD-ZC card image is all that is needed.
- `testdata/machines/tsconf/vdac4/`: a small FAT32 image builder script. It takes the release zips (DOOM-VDAC4 1.1,
  R-Type VDAC4 1.0, HMM2-VDAC4 `_SD`) and the user's WAD/DAT, and writes `scratch/` images. The images are not
  committed.
  - DOOM1.WAD shareware is committed with the test (§12).
  - The game binaries are GPL/MIT and can be fetched by the script from GitHub releases.
- Launch path:
  - tests use the SPG loader (`loaderspg`) on the game's SPG with the SD image mounted;
  - Wild Commander runs as on the board.

## 10. TTD, automation, performance

### 10.1 TTD

Device keys ≥ 66 (memory note: PeripheralId frozen at 65). Separate histories per device (owner rule):

| Device | State | Memory region |
|---|---|---|
| `vdac4-tang` | personality, timers, LINK file, profile switches, plus the `EveHostCard` blobs | eve-emu regions, as VDAC2 |
| `vdac4-esp-link` | slave HD buffer, link state, rings, frame parser, loader progress, cache key list | — |
| `vdac4-esp-cpu` | two cores' registers, scheduler, tasks, heaps' metadata, PCM ring, event queue, quantum outputs in flight | PSRAM 8 MB and internal RAM as changed pages |

The quantum model makes a checkpoint at a quantum boundary complete: the outputs of quantum `k` are part of the
state. Recording costs nothing when the board is not fitted (owner rule "histories: zero cost off", A/B benchmark in
phase 6).

### 10.2 Automation planes

Every capability on CLI, Python, Lua, MCP, REST and WebSocket (guideline). New module `vdac4`.

**Implemented: the board state.** Every plane reads `Vdac4Control::GetState` / `HasBoard`
(`core/src/emulator/platforms/tsconf/vdac4/vdac4control.h`): the Tang, ESP and app fields are the board's copy from
the last frame end, the log, packets and audio are read under their locks, so a read from any thread never parks the
emulation thread. The answer is the same JSON tree on every plane:

| Block | Keys |
|---|---|
| `tang` | `personality` (`ts`, `loading`, `ft`), `armed`, `esp_host`, `ram_g_3mb`, `ft_cfg` |
| `esp` | `phase` (`off`, `booting`, `running`, `flushing`, `restart_wait`), `ms`, `boots`, `boot_nonce`, `last_step`, `last_error`, `resources`, `clock_restarts`, `clock_offset_us` |
| `app` | `running`, `module`, `name`, `version`, `command_first`, `command_last`, `error` |
| `link` | `pending_from_z80`, `pending_to_z80`, `send_blocked`, `bad_checksums`, `resyncs`, `dropped_frames` |
| `packets` | `count`, `recent` (the last 64: `command`, `length`, `ms`) |
| `audio` | `input_rate`, `output_rate`, `queued`, `underruns`, `dropped`, `sound` |
| `log` | the last N ESP log lines, oldest first |

| Plane | Call |
|---|---|
| CLI | `vdac4 state [log N]` (alias `vdac4 status`; text, the last N log lines, default 10), `vdac4 log [N]` (the log only, default 50) |
| Python | `emu.vdac4_state(log=50)` -> dict; `RuntimeError` without the board |
| Lua | `vdac4_state([log])` -> table, `{error = "..."}` without the board |
| MCP | tool `vdac4_state` (`target`, `log`): the text has the personality, ESP phase, app name / version / state, the errors, the link, the audio and the last 10 log lines; `structuredContent` the JSON |
| REST | `GET /api/v1/emulator/{id}/vdac4/state[?log=N]` (default 50, at most 256); 409 `No VDAC4 Board` with the reason |
| WebSocket | `{"op":"call","method":"GET","path":"/api/v1/emulator/{id}/vdac4/state?log=20"}`; no topic (a polled read that never loads the emulation thread; the app-state and log topics below are planned) |

A machine without the board (not TS-Conf, `[MISC] TS_VDAC4=0`, a build without `ENABLE_VDAC2`) answers on every plane
with the reason. The live MCP check: `tools/verification/mcp/test_vdac4.py`.

**Implemented (2026-11-16): the app's memory.** `app.sections` in the state lists the running app's ELF sections
(`elf_address`, `size`, `loaded_at`). `vdac4_esp_memory` reads up to 4096 bytes: by the app's ELF address (`app=1`,
mapped through those sections) or by the ESP's D-bus address. It reads at the caller's moment from the board's
memory, not the emulation thread's frame copy, so it is a debugging read, not a frame-exact one.

| Plane | Call |
|---|---|
| CLI | `vdac4 esp-memory <address> [size] [app]` (default 64 bytes; hex dump) |
| Python | `emu.vdac4_esp_memory(address, size=16, app=False)` -> bytes |
| Lua | `vdac4_esp_memory(address[, size[, app]])` -> `{address, size, app, bytes, hex}` |
| MCP | tool `vdac4_esp_memory` (`address`, `size` 1..4096, `app`): a hex dump, 16 bytes a line |
| REST | `GET /api/v1/emulator/{id}/vdac4/esp/memory?address=&size=&app=1` -> `{address, size, app, hex}`; 400 / 409 |
| WebSocket | the REST route as a `call` |

Game profiles use it through the haptic probes' `cpu: esp` (frame-end reads on the emulation thread,
[haptics.md §5](../2026-10-17-controller-manager/haptics.md)). The DOOM profile `data/software/tsconf/doom-vdac4.yaml`
binds a pad (twin stick, the PC keys Esc / Tab / Backspace through `pc.*`) and plays shots, hits, pickups and death
from players[0]; the addresses and how they were found are in its header.

**Planned:**

- `vdac4 status`: personality, M_SEL, `espHost`, RAM_G mode, fonts, app name / version / state, tasks with core,
  priority, state and cycles, PCM queue, event queue.
- `vdac4 log [n]`: the app log ring.
- `vdac4 app stop`, `vdac4 cache list|clear`.
- `vdac4 tang read|write <addr>`: LINK registers.
- ESP memory peek by guest address (`0x3C……`, `0x42……`); the app's image is readable now (below).

FT812 inspection reuses the VDAC2 commands on the Tang's chip (screenshot of the FT picture, DL dump, metrics). The
WebSocket topics are app state changes and log lines, coalesced (owner rule "debug infra: no load on emulation").

### 10.3 Performance

- Nothing is added to the Z80 hot path when the board is absent.
- With the board fitted and no app running, the cost is the SPI slave and link code per access.
- The ESP worker's speed is measured with an interpreter micro-benchmark (Google Benchmark) and DOOM's timedemo (fps
  reported by the app status F3 frame counter).

## 11. Work order

Each phase ends with a target build, filtered tests and a report. Landing on master per the branch rules once the
owner says so.

| Phase | Content | Done when | Status 2026-11-23 |
|---|---|---|---|
| 1a | `EveHostCard` refactor out of `Vdac2Card` | all VDAC2 tests pass unchanged | done |
| 1b | eve-emu VDAC4 profile (§5.2 minus images), `Vdac4Tang`, `TSL-VDAC4`, LINK file, switching | Weather 2, Zuma VDAC2, R-Type Arcade VDAC2 run on `TSL-VDAC4`; profile unit tests (PALETTED8 1-pass and 4-pass, RAM_G 3 MB, reset keeps DL, fault codes, ROM window) | the profile, the Tang and switching done (3bc2c72d0; `EveVdac4_Test`, `Vdac4Tang_Test`); the three VDAC2 programs on `TSL-VDAC4` not re-checked since |
| 2 | `unreal-xtensa`: ISA of §7.1, block cache, windowed spills, traps; conformance tests from assembled vectors and QEMU-ESP traces (§12 Q1) | conformance suite green; ≥ 240 MIPS micro-benchmark | the interpreter and its tests done (`XtensaCpu_Test`; no QEMU traces, §12 Q1 got no answer); the MIPS target was overtaken by a block JIT on top (POC 053, `esp_jit` feature on by default, d93910137, `XtensaJit_Test`; the interpreter stays the fallback) |
| 3 | `Vdac4Esp`: slave HD, `EspSpiLink` port, `ZiFiFrameLink` extraction, core commands, module loader and cache | ESP App SPG uploads and starts an ELF; HSD.ELF answers 70-75 and logs its reads | done (`Vdac4EspLink_Test`, `Vdac4EspFirmware_Test`, `Vdac4EspElfLoader_Test`) |
| 4 | `EspAppHost`: loader, `zm_core_t`, symbols, newlib math, scheduler, PCM, ft/link, quantum worker | **DOOM1.WAD plays from `_DOOM.SPG`**: title, E1M1, sound, save / load, quit | done: DOOM runs with sound (3bc2c72d0), the ESP app as jobs on a worker thread (a920e4dbb) |
| 5 | R-Type (media path, RTMEM helper, `link_media`, low-latency PCM), HMM2 (3 MB, E6 path, OGG module, `core_back`), images extensions, 3D A0-AF | R-Type demo loop 3 levels; HMM2 to the adventure map with music; DEMO3D | R-Type runs (its pictures are the A/B reference of [eve-async-drawing.md](eve-async-drawing.md) §6). The image decoders and the 3D commands are in eve-emu (`eve-decoders.cpp`, `eve-vdac4-3d.cpp`). **Not checked**: HMM2-VDAC4 (`HMM2.DAT` is not shipped) and DEMO3D (not in the collection) |
| 6 | TTD devices, automation planes, docs chain (command-interface.md → module doc → planes), Linux / Windows builds | TTD seek inside DOOM gameplay reproduces frames; `run_all.py` MCP block; zero warnings gcc / clang / MSVC / MinGW | planes, docs chain and the MCP block (`tools/verification/mcp/test_vdac4.py`) done; builds on every CI platform (no JIT on Windows arm64, b11fee65e). **TTD not done**: no device of §10.1 exists ([TODO.md](TODO.md)) |

## 12. Open questions for the owner

| # | Question | Default if no answer |
|---|---|---|
| Q1 | May the work download and run Espressif's prebuilt tools inside `scratch/`: the `xtensa-esp-elf` toolchain (objdump, as) for the instruction census and test vectors, and Espressif's QEMU for ESP32-S3 to record reference instruction traces? Offline tools only, nothing linked into the emulator. (A research agent was stopped from fetching them without the owner's approval) | without them, phase 2 builds its own decoder census and hand-assembled test words from the ISA reference manual |

Settled during the design: the slot-0 arming (§5.3, from the firmware sources); DOOM1.WAD shareware is committed under
`testdata/machines/tsconf/vdac4/` for the integration test (licence questions are not raised, owner rule).
