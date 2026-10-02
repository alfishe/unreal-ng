# VDAC2 integration into unreal-ng

Design phase D-C of [vdac2-tdd.md](vdac2-tdd.md) §8: how the emulator hosts the VDAC2
card. The chip itself is the `eve-emu` library
([eve-emu-architecture.md](eve-emu-architecture.md), cited "arch §n"); its behavior is
[ft812-behavior-spec.md](ft812-behavior-spec.md) ("spec §n"). Hardware facts are in the
design's §2 ("tdd §n").

Date: 2026-10-01. Status: draft for review; no code exists.

## 1. Scope and dependencies

In scope: building and linking the library, the machine configuration, the SPI bus with
several devices, the card device, time conversion, the interrupt path, the video output
and presentation, the Evo palette through the card, TTD, the media manager, the ROM
image, the automation surfaces, integration tests.

Dependencies:

| Needed | Why | Status |
|---|---|---|
| `eve-emu` | the chip | designed (arch), implemented by a separate agent |
| TTD v2 memory regions (PLAN #40 V1) | the chip's 7 memory regions (arch §4.5) | planned, not built. Until it exists, a VDAC2 machine refuses to record (§9.3) |
| ROM image file | ROM fonts (spec §1) | extracted by a tool (§10) |

Isolation rule (tdd §7): shared files (`zcontrollerspi`, `screen`, the automation
modules) never name VDAC2 or the FT812; they see an SPI device, a second picture source,
a device-state report. The isolation test keeps passing.

## 2. Build

- `eve-emu` is a git submodule at `lib/eve-emu`, like `lib/googletest` and
  `lib/benchmark` (arch §3.2).
- Root `CMakeLists.txt`:

  ```cmake
  option(ENABLE_VDAC2 "TS-Conf VDAC2 card (FT812, eve-emu library)" ON)
  set(EVE_EMU_DIR "${CMAKE_CURRENT_SOURCE_DIR}/lib/eve-emu" CACHE PATH "eve-emu checkout")
  if (ENABLE_VDAC2)
      if (NOT EXISTS "${EVE_EMU_DIR}/CMakeLists.txt")
          message(FATAL_ERROR "VDAC2: eve-emu not found in ${EVE_EMU_DIR}. "
                  "Run 'git submodule update --init lib/eve-emu' or set ENABLE_VDAC2=OFF")
      endif()
      set(EVE_DECODER_INFLATE BUILTIN CACHE STRING "" FORCE)   # A3: built-ins
      set(EVE_DECODER_PNG     BUILTIN CACHE STRING "" FORCE)
      set(EVE_DECODER_JPEG    BUILTIN CACHE STRING "" FORCE)
      add_subdirectory(${EVE_EMU_DIR} ${CMAKE_BINARY_DIR}/eve-emu)
  endif()
  ```

  before `add_subdirectory(core/src)`. `EVE_EMU_DIR` lets a developer point at a local
  checkout of the library while working on both.
- `core/src/CMakeLists.txt`: with `ENABLE_VDAC2`, `target_link_libraries(core PRIVATE
  eve::emu)` and `target_compile_definitions(core PRIVATE ENABLE_VDAC2)`. The card's
  sources compile only then.
- `ENABLE_VDAC2=OFF`: a configuration asking for `TS_VDAC2=1` fails machine creation
  with "this build has no VDAC2 support" on every surface; nothing silently falls back.
- New worktrees and CI checkouts need `git submodule update --init lib/eve-emu` (the same
  rule as the other submodules; memory note on worktrees).

## 3. Configuration and the IDE slot

- The key exists: `[MISC] TS_VDAC2=1` → `config.ts_vdac = 7` (`config.cpp:744-761`),
  which already sets `STATUS[2:0]` = 7 and enables BLT2 in the DMA.
- **IDE off.** After the `[HDD]` section is parsed, `TS_VDAC2=1` on the `TSL` model
  forces `config.ide_scheme = IDE_NONE` (`config.cpp:396`). A configured `[HDD] Scheme`
  logs one warning: "ignored: the VDAC2 card occupies the IDE connector". Port decoding
  needs no change: `TryIdePortIn` / `TryIdePortOut` find no IDE, as in a firmware build
  without `IDE_HDD`.
- **Media manager.** The IDE slots (`ide0.master` / `ide0.slave`) do not exist on such a
  machine; an insert request on any surface fails with the reason "VDAC2 occupies the IDE
  connector".
- **New keys** in a `[VDAC2]` section:

  | Key | Default | Meaning |
  |---|---|---|
  | `RomImage` | `rom/ft81x.rom` | FT81x ROM image (§10), resolved like the machine ROM paths (executable folder, then resources folder, `config.cpp:73`); missing file = ROM reads return 0, one log line |

  No other knobs: everything else follows the hardware.
- The choice is fixed at machine creation, as the card is swapped with the power off and
  the FPGA reflashed (tdd §6.1).

## 4. The SPI bus with several devices

### 4.1 `ZControllerSpi` becomes a hub

Today it has one device and one chip select: `WriteConfig` takes D1 as /CS
(`zcontrollerspi.cpp:19-27`). It is shared by ATM3 (BaseConf) and TSL.

- **Slots.** Up to four devices, each with a config-bit mask and polarity:

  ```cpp
  void SetDevice(SpiDevice* device);                                  // unchanged: slot 0, D1 active low
  void AttachDevice(uint8_t slot, SpiDevice* device, uint8_t configBit, bool activeHigh);
  ```

  TS-Conf with VDAC2 attaches the SD card as today and the card as slot 1 on D2, active
  high (`ftcs_n = ~D2`, tdd §2.2, [V `zports.v:296-311`]). D3 (SD2) and D4 (ESP32) stay
  free (VDAC3 is out of scope).
- **Selection.** `WriteConfig` recomputes each slot's select line and calls `select()` on
  the devices whose line changed (as today for the one device).
- **Exchange.** Each selected device receives the byte. MISO: if the FT812 is selected,
  its byte; otherwise the SD card's; otherwise #FF. This is the RTL's mux
  (`sdi = !ftcs_n ? ftdi : sddi`, [V `top.v:1173-1178`]). A deselected device sees
  nothing.
- **Read-back.** The read-latch rule (a read returns the previous exchange) is unchanged.
- **Cost.** With one device attached (every machine except TSL with VDAC2) the hub keeps
  today's path: one pointer test. The multi-device path is taken only when a second slot
  is attached.

### 4.2 State and TTD

- `State` grows from `{csN, rxLatch}` (2 bytes, `static_assert` in
  `zcontrollerspi.h:36`) to `{config, rxLatch}`: the last config byte written, from which
  every slot's select line is derived.
- The `EvoSdCard` blob (`TTDEvoSdCard`) carries `ZControllerSpi::State`. Its size changes,
  so the TTD fixture corpus is re-recorded (the existing rule after any device-state
  format change), and older recordings of TSL / ATM3 are not loadable after the change.
  TTD has no blob versioning yet (TTD v2 V5); the change is listed in the commit and the
  TTD TODO.

### 4.3 DMA

The TS-Conf DMA reaches SPI through `_zc` (`portdecoder_tsconf.cpp:35-38`), so
`DMA_RAM_SPI` reaches the FT812 when it is selected, with no change. The TS-Labs SDK
streams 512-byte DMA blocks with CS held (spec §9 item 5).

## 5. The card: `Vdac2Card`

`core/src/emulator/platforms/tsconf/vdac2card.{h,cpp}`, owned by `PortDecoder_TSConf`
when `ts_vdac == 7`.

### 5.1 Responsibilities

- Creates the `EveChip` (`EVE_MODEL_FT812`, 8 MHz, ROM image, built-in decoders).
- Implements `SpiDevice` for hub slot 1: `select` → `EveSelect`, `exchange` →
  `EveExchange`, each after advancing the chip to "now" (§5.2).
- Drives the chip's time, watches INT_N (§6), owns the FT812 picture (§7).
- Serializes for TTD (§9), reports device state for automation (§11).

### 5.2 Time

- **Clock base.** TS-Conf time is the raster tact (3.5 MHz, 71 680 per frame,
  independent of the CPU turbo: the CPU runs 1, 2 or 4 clocks per tact,
  `tsconfinterrupts.h`). The card converts raster tacts to FT812 system clocks:
  `ftClocks = floor(rasterTacts × f_sys / 3 500 000)`, carried incrementally with an
  exact remainder, so nothing drifts over a session.
- `f_sys` changes only while the FT812 clock is stopped (`CLKSEL` is accepted in SLEEP
  only, spec §2.2). The card advances the chip to "now" before every bus access, so each
  interval is converted at the frequency that was in force during it.
- The card keeps a 64-bit raster-tact counter since its creation and the remainder in its
  own state (§9).
- **When it advances the chip:**
  - before every `select` / `exchange` (port access and DMA alike);
  - at the machine frame end;
  - at the next chip event while it matters: the card implements `IMachineStepHook` and
    compares the current raster tact with the precomputed tact of the next INT_N change
    or FT812 frame end (`EveClocksToNextEvent` converted back). One integer comparison per
    instruction, and the hook exists only on machines with the card.

## 6. Interrupt path

- The FT812's INT_N reaches the Evo only while msel = 1: the card's CPLD routes it onto
  the video cable [C `top.v:36`]. With msel = 1 the TS-Conf **line** interrupt is
  triggered by INT_N's falling edge instead of the raster line start
  (`int_start_lin(vdac2_msel ? int_start_ft : line_start_s)`, [V `top.v:1092-1093`]).
- `TsConfInterrupts` today latches the line event at raster tact `224 n − 1` on each of the
  320 lines and evaluates events lazily up to the tact the CPU has reached (`CatchUp`).
  It gets an optional **external line source**:

  ```cpp
  struct ITsConfLineSource                   // implemented by Vdac2Card
  {
      // Falling INT_N edges in raster tacts (from, to], in order
      virtual size_t LineEdges(uint32_t fromRaster, uint32_t toRaster, uint32_t* out, size_t max) = 0;
  };
  ```

  In `CatchUp`, for each raster interval: if msel is 1 at that line (the per-line latched
  `vConfig` bit 2 from `TsConfEngine`), the line events are the card's edges in that
  interval; otherwise the usual `224 n − 1` events. The card advances the chip to
  `toRaster` to answer.
- The FPGA synchronizes INT_N to fclk in two stages [V `top.v:468-471`]: the edge is seen
  two fclk later, i.e. within the same raster tact. The design takes the edge's raster
  tact rounded up.
- A new edge needs `REG_INT_FLAGS` cleared by a read first (spec §5.4); the chip models
  that.

## 7. Video output and presentation

### 7.1 Two picture sources

- `ScreenTSConf` keeps its native 720×288 framebuffer and gets a **second framebuffer for
  the FT812**, sized from `EveGetTiming` (`HSIZE × VSIZE`, up to 2048×2048; 1024×768 in
  the games). The card passes it to the chip with `EveSetOutput`.
- **Which one the monitor shows** follows msel (tdd §2.1, the card switches the whole
  signal). The `FramebufferDescriptor` the rest of the emulator sees is the native one
  while msel = 0 and the FT812 one while msel = 1. A switch, and an FT812 mode change,
  re-describe the framebuffer and post `NC_VIDEO_MODE_CHANGED`, the path the guest mode
  switches of AlCo, Profi and ATM already use (`screen.cpp:614-629`); the Qt window
  re-attaches on it (`mainwindow.cpp:1789`).
- **Drawing on or off.** The chip draws lines only while msel = 1 and the frame will be
  presented; under turbo decimation (frames not shown) the card passes `drawing = 0`. All
  timing runs either way, and nothing the guest can observe depends on drawing.

### 7.2 Presenting at the FT812 rate

- **Native picture (msel = 0):** unchanged, latched at the machine frame end
  (`MainLoop::OnFrameEnd` → `Screen::LatchFramebuffer`).
- **FT812 picture (msel = 1):** the native latch at the machine frame end is skipped. The
  card latches the FT812 framebuffer each time `EveCompletedFrames` changes, which happens
  at the FT812's own VSYNC, possibly in the middle of a machine frame, on the emulation
  thread. `Screen` gets an entry point for that:

  ```cpp
  void LatchFrameFrom(const FramebufferDescriptor& source, uint64_t emulatedTimeUs);
  ```

  It does what `LatchFramebuffer` does (copy into the present slot ring under
  `_presentMutex`), with the frame's emulated time stamp.
- **Present delay in time, not frames.** The A/V delay (`AVSyncDelayFrames`, the present
  queue of 4 slots) is counted in machine frames today. With frames arriving at 59 Hz
  instead of 48.8 Hz, a frame count would change the audio / video offset. The present
  queue selects the frame by its time stamp against the delay in microseconds
  (`GetPresentDelayUs`), which keeps the A/V offset the same for both sources.
  `docs/emulator/design/audio/drc-rate-control.md` gets a note.
- **The host window** shows the newest presentable frame at each host refresh, as for any
  machine picture.

### 7.3 Consumers of the picture

| Consumer | Change |
|---|---|
| Qt main window | re-attaches on `NC_VIDEO_MODE_CHANGED` (exists) |
| Recording (`RecordingManager::CaptureFrame`, `EncodeVideoFrame(framebuffer, timestamp)`) | called from the latch path with the emulated time stamp, so a recording with msel = 1 holds the FT812 frames at the FT812 rate; a source switch mid-recording changes resolution and rate: each backend is checked for that, and where it cannot change resolution the recording is split into a new file at the switch |
| Screenshots, `capture_media` | read the current descriptor: work unchanged |
| Video wall, screen viewer | re-attach on the notification; checked in I2 |
| ZX DLSS, temporal filters | apply to the native ZX picture only; off while msel = 1 |
| Video debug mapper (PLAN #42) | an `IVideoMapper` for the FT812 picture backed by `EveProbePixel` (layer `ft812`) |

## 8. The Evo palette through the card (D6)

`ScreenTSConf::CramToRgba` for `ts_vdac == 7` uses the card's LUT exactly
[C `pentevo/vdac/vdac2/cpld/top.v` `lut`]:

- CRAM bit 15 = 1 (PAL_SEL = 1): `level << 3`, so 0…248;
- bit 15 = 0: the table `0, 10, 21, 31, 42, 53, 63, 74, 85, 95, 106, 117, 127, 138, 149,
  159, 170, 181, 191, 202, 213, 223, 234, 245, 255` for levels 0…24, 255 above
  (`round(v × 255 / 24)`).

The table sits in the code with a comment naming the CPLD source and explaining why the
VDAC2 build differs from the PWM and VDAC1 builds. Other `ts_vdac` values are unchanged.
The palette cache (CRAM version counter) needs no change: `ts_vdac` is fixed per machine.

## 9. TTD

### 9.1 What is recorded

- **Device blob** `PeripheralId::Vdac2` = 25 (the next free id; `ttdserializable.h`),
  appended to `PortDecoder_TSConf::GetTTDModelStateIds` / `CreateTTDSerializers` when
  the card exists. Payload, fixed size: the card's fields (raster-tact counter,
  conversion remainder, last INT_N level, the msel source state) + `EveSaveState`
  (fixed size, arch §4.5).
- **Memory regions:** the chip's seven regions (arch §4.5: `RAM_G`, `DL0`, `DL1`, `REG`,
  `CMD`, `SPECIAL`, `INFLIGHT`) registered as TTD v2 device regions; only dirty 4 KB pages
  are stored per checkpoint.
- The FT812 framebuffer is not recorded: after a seek it is redrawn from the restored
  memory (arch §7.4).

### 9.2 Restore

`TTDLoadState`: the regions are written back by the region mechanism, the card calls
`EveMemoryRestored`, then `EveLoadState` with the chip part of the blob. An operation in
flight restarts per arch §7.3. The present queue is refilled by the next FT812 frame.

### 9.3 Before TTD v2 regions exist

The card reports itself not recordable: starting a TTD recording on a VDAC2 machine
fails on every surface with "VDAC2 needs TTD memory regions (TTD v2), not available in
this build". Nothing records a state that could not be restored. RZX playback and the
rest of the machine are unaffected.

## 10. The ROM image

- `tools/machines/tsconf/vdac2/extract-ft81x-rom.py`: reads a `bt8xxemu.dll` (Bridgetek's or the one in
  `tslabs/zx-evo-unreal` `Unreal/cfg/`), finds the FT81x font table by its known first
  metric block, checks all 19 blocks against the spec (formats L1 / L4, sizes, pointers
  inside the image), cuts 0x1E0000…0x2FFFFF (1152 KB) and writes it with a SHA-1. Run by
  the user; the image is never committed.
- The user places the output as `rom/ft81x.rom` (the default of `[VDAC2] RomImage`,
  §3) next to the other ROMs. It is not committed: the build and the tests work without
  it (ROM reads return 0; tests that need glyphs skip).
- The card loads it at creation and passes the pointer to `EveCreate`; without it, ROM
  reads return 0 and the log says so once.

## 11. Automation surfaces

Parity on every surface, with docs and OpenAPI (tdd §6.5). Each surface reads one shared
device-state report (`DeviceState` aspect `vdac2`, as `tsconfdevicestate.cpp` does for
TS-Conf), so all of them return the same data.

| Surface | Additions | Where |
|---|---|---|
| WebAPI | `GET …/state/vdac2` (card present, msel, FT812 mode and rate, `REG_ID`, `REG_FRAMES`, INT flags / mask / enable, coprocessor pointers, phase and fault, line-cost summary of the last frame with overflow count); `GET …/state/vdac2/displaylist?active=1` (decoded words, `EveDisassemble`); `GET …/vdac2/memory?address=&length=` (`EvePeek`); `GET …/vdac2/frame` (PNG of the FT812 picture regardless of msel); `GET …/vdac2/linecost?line=`; `/video/pixel` layer `ft812` | `core/automation/webapi/src/api/state_device_api.cpp`, `openapi/openapi_state.inc` |
| CLI | `state vdac2`, `vdac2 dl [active\|pending]`, `vdac2 mem <addr> <len>`, `vdac2 frame <file>`, `vdac2 linecost <line>` | CLI command table |
| MCP | `inspect_state` aspect `vdac2`; `capture_media` source `vdac2` | `core/automation/mcp/src/mcp-tools.cpp` |
| Lua / Python | `vdac2_state()`, `vdac2_display_list()`, `vdac2_read(addr, len)`, `vdac2_frame()`, `vdac2_line_cost(line)` | `lua_emulator.h`, `python_emulator.h` |
| Qt | later (debug UI deferred, as for TS-Conf); the surfaces above are complete enough that a dock is integration only | - |
| Recipe | `.recipe/machines/tsconf-vdac2.md`: enabling the card, the ROM image, putting a game on the SD card, starting it, taking a picture, which images `ftview` accepts (tdd §2.7) | - |

## 12. Tests in unreal-ng

The library's own tests live in its repository (arch §10). unreal-ng tests the
integration:

| Test | Proves |
|---|---|
| `zcontrollerspi_test.cpp` additions | hub selection per config bit and polarity, MISO priority, single-device path unchanged, state round trip |
| `vdac2card_test.cpp` | `STATUS & 7 == 7`; FT812 register read / write from Z80 code through `#77` / `#57`; DMA `RAM_SPI` into `RAM_G`; time conversion exact over a long run (no drift against `REG_CLOCK`); INT_N edge → line interrupt with msel = 1, none with msel = 0 |
| `portdecoder_tsconf_test.cpp` additions | Nemo IDE ports silent with `TS_VDAC2=1`; IDE slots refused |
| `screentsconf_test.cpp` additions | the exact VDAC2 LUT (both modes, all levels); descriptor switch on msel and on FT812 mode change |
| presentation | FT812 frames latched at the FT812 rate (count over N machine frames matches the mode's rate); A/V delay in time equal for both sources |
| TTD (after V1) | record / seek / replay bit-exact over a `CMD_INFLATE` and a `CMD_LOADIMAGE` with checkpoints inside them |
| whole machine (local, untracked binaries; skipped when absent) | TS-Labs `test1`-`test6`, `test9`; R-Type, Zuma, HMM2 boot to the title (screenshot fixtures); `ftview` shows a JPEG |
| isolation | shared files free of VDAC2 / FT812 names (existing test) |

## 13. Performance

- Machines without the card: no new work. The hub keeps its one-device path; the step
  hook and the line source exist only with the card.
- With the card and msel = 0: the step hook's comparison per instruction and the chip's
  timing at events.
- With msel = 1: the chip draws 1024×768 at 59 Hz (arch §8.4). Benchmarks: whole machine
  with VDAC2 against without (`BM_TsConfFrame_Vdac2`), the chip's frame benchmark.
  Target: a machine frame with the FT812 picture costs no more than twice a plain TS-Conf
  frame (tdd §7).

## 14. Work order

| Phase | Content | Needs |
|---|---|---|
| I1 | build and submodule; config (IDE off, `[VDAC2]`); SPI hub + state change + fixture re-record; `Vdac2Card` with time and SPI; STATUS | `eve-emu` host side (arch L0-L1) |
| I2 | interrupt path; second framebuffer, msel switch, FT812-rate latch, present delay in time; recording and other consumers; exact LUT | `eve-emu` drawing |
| I3 | automation surfaces, OpenAPI, recipe; ROM extraction tool | |
| I4 | TTD blob and regions; restore tests | TTD v2 V1 (PLAN #40) |
| I5 | benchmarks and speed target | |

## 15. Open for review

| # | Question | Proposal |
|---|---|---|
| C1 | Recording across an msel switch: split files, or variable resolution where the backend allows | split at the switch for every backend; simpler and the same everywhere |
| C2 | Whether to keep the native picture available while msel = 1 (a debugger view of what the Evo outputs underneath) | yes, as a second presentation source for the debugger only; not shown in the main window |
