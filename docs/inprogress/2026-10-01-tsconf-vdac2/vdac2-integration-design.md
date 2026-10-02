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
| TTD v2 memory regions (PLAN #40 phase 1) | the chip's 7 memory regions (arch §4.5) as changed pages | planned, not built. Until then the regions are a whole blob in every checkpoint (§9.1) |
| ROM image file | ROM fonts (spec §1) | extracted by a tool (§10) |

Isolation rule (tdd §7): shared files (`zcontrollerspi`, `screen`, the automation
modules) never name VDAC2 or the FT812; they see an SPI device, a second picture source,
a device-state report. The isolation test keeps passing.

## 2. Build

- `eve-emu` is developed in its own repository and **vendored** at
  `core/src/3rdparty/eve-emu` (decided 2026-10-02: the library has no published
  repository to take as a submodule). `VENDORED.md` there names the source commit, the
  files copied (the library and the two decoder sources it compiles, not its tests,
  benchmarks and tools) and how to update. `core/src/CMakeLists.txt` keeps the copy out of
  the core source glob; the library builds with its own CMake.
- Root `CMakeLists.txt`: `ENABLE_VDAC2` (**ON** by default) and `EVE_EMU_DIR` (default the
  vendored copy; a developer points it at an eve-emu checkout while working on both);
  built-in decoders (A3); the library's tests, benchmarks and tools off.
- `core/src/CMakeLists.txt`: with `ENABLE_VDAC2`, `target_link_libraries(core PRIVATE
  eve::emu)` and `target_compile_definitions(core PUBLIC ENABLE_VDAC2)`. The gate is
  PUBLIC like `UNREALNG_HAVE_OPL4`: `core-tests` compiles core sources itself and links
  `eve::emu` too. Without the gate the card's methods are empty stubs (the card is never
  fitted then).
- `ENABLE_VDAC2=OFF`: the `TSL-VDAC2` machine is not offered and a configuration asking
  for `TS_VDAC2=1` fails with "this build has no VDAC2 support" on every surface; nothing
  silently falls back.
- **The machine on every surface:** `TSL-VDAC2` (alias `TSCONF-VDAC2`) is a machine
  variant (`core/src/emulator/machinevariants.{h,cpp}`): the TSL model with the VDAC2
  board applied as a config override (`ts_vdac = 7`, no IDE). EmulatorManager creates it
  by name (create and model switch), the WebAPI / CLI model lists carry it, an instance's
  identity reports `variant`, and unreal-qt's Machine menu has it as **TS-Conf + VDAC2
  (FT812)**.

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
- **Lifetime.** The decoder fits the card at a reset when the config says VDAC2
  (`ts_vdac == 7`) and removes it otherwise, so it follows the configured firmware build.
  A board power-on (`PowerCycle`) resets the FT812 like power applied (`EveReset`). The
  Evo's reset button does not: the card's FT812 has its own power-on reset, and the
  software's `ft_init` starts with `PWRDOWN` / `ACTIVE` / `RST_PULSE` anyway (TO VERIFY on
  the card's schematic: where PD_N goes).

### 5.2 Time

- **Clock base.** TS-Conf time is the raster tact (3.5 MHz, 71 680 per frame,
  independent of the CPU turbo: the CPU runs 1, 2 or 4 clocks per tact,
  `tsconfinterrupts.h`). The card converts raster tacts to FT812 system clocks:
  `ftClocks = floor(rasterTacts × f_sys / 3 500 000)`, carried incrementally with an
  exact remainder, so nothing drifts over a session.
- `f_sys` changes only while the FT812 clock is stopped (`CLKSEL` is accepted in SLEEP
  only, spec §2.2). The card advances the chip to "now" before every bus access, so each
  interval is converted at the frequency that was in force during it.
- The card keeps an absolute raster-tact position (a frame base plus the tact inside the
  frame) and the remainder in its own state (§9). The tact inside the frame is the
  TS-Conf engine's accounted position (`TsConfState::budgetRaster`): at a port access the
  decoder has caught the engine up to the CPU, so it is the CPU's tact; while the engine
  accounts a `DMA_RAM_SPI` transfer it is the DMA's position, so DMA bytes reach the chip
  at their own time. The engine calls the card when it closes a frame (after the old
  frame's DMA is accounted, before positions restart at 0), and the card moves its frame
  base on. A Z80 reset or a power-on that zeroes the engine position cannot move the
  card's time backwards: a position below the card's is ignored until the engine is back.
- While the chip's clock is stopped (`EveSystemClockHz` = 0) time passes without clocks and
  the remainder is dropped.
- **When it advances the chip:**
  - before every `select` / `exchange` (port access and DMA alike);
  - at the machine frame end (the engine's frame-end call);
  - at the next chip event while it matters: the card implements `IMachineStepHook` and
    compares the current raster tact with the precomputed tact of the next INT_N change
    or FT812 frame end (`EveClocksToNextEvent` converted back). One integer comparison per
    instruction, and the hook exists only on machines with the card.

## 6. Interrupt path

- The FT812's INT_N reaches the Evo only while msel = 1: the card's CPLD routes it onto
  the video cable [C `top.v:36`]. With msel = 1 the TS-Conf **line** interrupt is
  triggered by INT_N's falling edge instead of the raster line start
  (`int_start_lin(vdac2_msel ? int_start_ft : line_start_s)`, [V `top.v:1092-1093`]).
- `TsConfInterrupts` latches the line event at raster tact `224 n − 1` on each of the 320
  lines and evaluates events lazily up to the tact the CPU has reached (`CatchUp`). It
  gets an optional **external line source**, implemented by the decoder (it knows the
  per-line msel) on top of the card (it knows the edges):

  ```cpp
  struct ITsConfLineSource
  {
      virtual bool DrivesLine(uint32_t line) const = 0;      // msel latched for that line
      virtual size_t TakeLineEdges(uint32_t raster, uint32_t* out, size_t max) = 0;
  };
  ```

  In `CatchUp(from, raster)` with a source: the card's falling INT_N edges up to `raster`
  are taken (always, so a masked edge is gone, not deferred); each latches the line INT
  if its line drives from the card. The usual `224 n − 1` events latch only on lines that
  do not. msel of a line is V_CONFIG bit 2 as latched at the line start ([V]
  `video_ports.v:153-157`): the engine's per-line copy for a line that has started, the
  register for one that has not (a V_CONFIG write brings the engine up to the write
  first, so nothing can fall in between).
- **Event-time stepping.** INT_N can fall between two bus accesses (a swap at the
  FT812's VSYNC, the coprocessor finishing). The card steps the chip from event to event
  (`EveClocksToNextEvent`) whenever it advances, records each falling edge with its raster
  tact, and keeps the tact of the chip's next event. The interrupt controller asks after
  every instruction; until that tact it costs one comparison. A bus access that changes
  INT_N (an `INT_EN` write with flags pending, the `REG_INT_FLAGS` read) is sampled at the
  access. The engine's frame-end call to the card comes last in the rollover, after the
  interrupt controller has taken the old frame's edges.
- The FPGA synchronizes INT_N to fclk in two stages [V `top.v:468-471`]: the edge is seen
  two fclk later, i.e. within the same raster tact. The design takes the edge's raster
  tact rounded up.
- A new edge needs `REG_INT_FLAGS` cleared by a read first (spec §5.4); the chip models
  that.

## 7. Video output and presentation

### 7.1 Two picture sources

- `Screen` gets a generic **external picture source** (it never names the card):
  `SetExternalPicture(buffer, width, height, framePeriodUs)`, `ClearExternalPicture()`,
  `LatchExternalFrame()`. While one is active, `GetFramebufferDescriptor` /
  `GetFramebufferData` describe it (videoMode `M_NUL`: no machine raster descriptor, so
  every consumer takes the whole picture), the machine frame end does not latch the native
  framebuffer, and the present queue is sized for the external picture. The native
  renderer keeps running into `GetNativeFramebufferDescriptor()` (video mappers, the
  ZX-only render path read that one). Switching on or off, and a new size, post
  `NC_VIDEO_MODE_CHANGED`, the path guest mode switches already use; the Qt window
  re-attaches on it.
- The card owns two buffers: the chip draws ARGB8888 into one (`EveSetOutput`), and each
  finished FT812 frame is converted into the other, RGBA8888 like the framebuffer, which
  is the external picture. Size: `HSIZE × VSIZE` from `EveGetTiming`; a new mode resizes
  both at the next FT812 frame end (that frame was drawn for the old size and is not
  presented).
- **Which one the monitor shows** follows msel (tdd §2.1, the card switches the whole
  signal): at each machine frame end the decoder passes msel as the last line latched it
  to the card (`SetShowing`), which switches the Screen's external picture on or off.
- **Drawing on or off.** The chip draws only while it is shown, and not while turbo
  decimation skips the machine frame (`Screen::IsTurboRenderSkip`). All timing runs
  either way, and nothing the guest can observe depends on drawing.

### 7.2 Presenting at the FT812 rate

- **Native picture (msel = 0):** unchanged, latched at the machine frame end
  (`MainLoop::OnFrameEnd` → `Screen::LatchFramebuffer`).
- **FT812 picture (msel = 1):** latched by the card at each FT812 frame end (the chip's
  frame-end event, found by the event stepping of §6), possibly in the middle of a
  machine frame, on the emulation thread (`LatchExternalFrame`).
- **Present delay.** The A/V delay is a number of frames in the present queue. While the
  external picture is active, the native delay is converted into external frames of the
  same duration, rounded to the nearest (`Screen::ExternalDelayFrames`): at 59 Hz against
  48.8 Hz the 2-frame delay stays 2 frames (34 ms against 41 ms), so the A/V offset moves
  by at most half an FT812 frame. Exact time stamps per frame are a later refinement if
  that is ever audible.
- **The host window** shows the newest presentable frame at each host refresh, as for any
  machine picture; the window scales by the framebuffer the descriptor reports, so the
  descriptor carries the FT812 picture's exact size (`HSIZE × VSIZE`).

### 7.3 Consumers of the picture

| Consumer | Change |
|---|---|
| Qt main window | re-attaches on `NC_VIDEO_MODE_CHANGED` (exists) |
| Recording (`RecordingManager::CaptureFrame`, `EncodeVideoFrame(framebuffer, timestamp)`) | today: called at the machine frame end with the shown descriptor, so a recording with msel = 1 holds the FT812 picture at the machine rate. One picture size per file: frames of another size (a source switch, a new FT812 mode) are not encoded into the running file (logged once). Later: frames from the latch path at the FT812 rate, and a new file at a switch (C1) |
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

Built 2026-10-02 (branch `vdac2-line-metrics`; design of the change:
[line-budget-metrics.md](line-budget-metrics.md) §3.4). Until then a TTD recording on a
VDAC2 machine ran and silently left the FT812 out.

### 9.1 What is recorded

Two device blobs per checkpoint, registered by `PortDecoder_TSConf::GetTTDModelStateIds` /
`CreateTTDSerializers` when the card exists and its chip was created (the serializers:
`core/src/debugger/ttd/tsconf/ttdvdac2.h`, the data: `Vdac2Card::Ttd*`):

| Id | Name | Content | Size |
|:--|:--|:--|:--|
| 42 | `Vdac2Memory` | every memory region of the chip in region order: `RAM_G` (1 MB), `DL0`, `DL1` (8 KB each), `REG`, `CMD`, `SPECIAL` (4 KB each), `INFLIGHT`; zero runs of 64 bytes or more dropped (format below) | variable: 3.3 KB per checkpoint for an empty chip, 645 KB with test6's image in `RAM_G` (after compression); 2.2 MB worst case |
| 43 | `Vdac2` | the card: blob version (1), what the monitor shows (msel latched), the INT_N level, the INT edges not yet taken (count and up to 16 raster tacts), the card's time (frame base, position, conversion remainder, next event); then the chip's control state (`EveSaveState`: registers, scan, coprocessor, audio, bitmap handles, graphics context, the line budget metrics block of the last finished frame) | 168 bytes + the control state (`EveStateSize`; the metrics block alone is 8 KB) |

- **Whole regions until TTD v2.** The TTD in master stores the machine's RAM as changed
  pages (a full snapshot every 50 frames) but device memory as whole blobs in every
  checkpoint (the classic General Sound card's 512 KB is the precedent). The FT812's memory
  follows that rule: correct, but up to 2.2 MB per frame before compression. When TTD v2
  memory regions exist (PLAN #40 phase 1), `Vdac2Memory` becomes regions with changed
  4 KB pages only (eve-emu already tracks them, `EveRegion::dirty`).
- **Zero runs dropped.** The memory blob is `u32` magic `VZR1`, `u32` byte count of what
  follows, then per region a sequence of `u32` tokens: bit 31 set = that many zero bytes
  (the low 31 bits), clear = that many bytes follow as they are. No token crosses a region
  boundary; a region's tokens add up to exactly its size, and a blob that does not (or has
  another magic) is refused. The blob is variable-size (`ITTDSerializable::TTDVariableSize`):
  a checkpoint stores only the bytes written, never the 2.2 MB worst case.
  - *Threshold 64 bytes.* A zero run inside data costs a run token and a new data token,
    8 bytes, so runs of 9+ bytes already save space before compression; but zstd shrinks
    short zero stretches itself, and every token is a branch on save and restore. 64 keeps
    the token count to a few hundred for real content (graphics data has many short zero
    stretches) while every long one - unused `RAM_G`, the empty display list tail, idle
    `INFLIGHT` - goes.
  - *What it buys* (measured on the M1 Ultra, `TSL-VDAC2`, mean of 10 captures): an empty
    chip's capture 0.94 ms → 0.59 ms per frame (plain `TSL`: 0.18 ms), because 2.2 MB of
    zeros are no longer allocated, cleared, copied and compressed every frame. The stored
    size barely changes (3 388 → 3 343 bytes per checkpoint): zstd already squeezed the
    zeros. With real content (test6: 645 KB per checkpoint, 2.4 ms capture) nothing changes;
    that is TTD v2's job (changed pages only).
  - *Test:* `Vdac2Card_Test.TtdMemoryBlobRestoresEveryByteWhereverTheZerosAre` - all zero,
    no zero at all, runs at a region's start and end and across a region boundary, 63 / 64 /
    65 bytes, a single non-zero byte at a region's end, random data with random zero
    stretches; every byte compared after a restore over a different live state.
- **Not recorded:** the FT812 frame buffer and the presented picture (drawn again from the
  restored memory, §9.3), the ROM image (not state), the bus capture (a debug tool).

### 9.2 Restore

Blobs are restored in id order, so the memory comes first:

1. `Vdac2Memory`: each region decoded back (zero runs filled), then `EveMemoryRestored` (the library marks its
   drawing stale).
2. `Vdac2`: `EveLoadState` with the control state, then the card's time, edges and monitor
   source; the output is configured for the restored mode. A restore never paints: if what
   the monitor shows changed, the Screen's external picture is switched, its content comes
   from the TTD replay.

After a restore eve-emu draws the FT812 frame in flight again from line 0 with the
restored memory (arch §7.4).

### 9.3 What a TTD position shows

The card is a TTD display participant (`ttd::ITTDDisplayParticipant`,
`EmulatorContext::pTtdDisplayParticipant`), so `TimeTravelManager::ComposeDisplay`, which
replays the machine to the position in a sandbox and copies what the Screen shows, applies
the same rule as for the ZX screen:

- **By frame number:** the picture the monitor showed at the end of that machine frame: the
  FT812 frame that finished last. The FT812 frame (about 16.9 ms) does not line up with the
  machine frame (about 20 ms), so the replay starts one machine frame earlier
  (`TTDLeadInFrames() = 1`): the FT812 frame that finished last is then drawn entirely by the
  replay, even if it started before the target frame's checkpoint.
- **At a T-state / time point inside a frame:** the FT812 frame in flight, drawn up to that
  moment, over its previous frame. The library draws lines lazily (at a frame end or before a
  memory write); `TTDPrepareComposedPicture` brings the chip to the position and makes it draw
  every line due, then presents its frame buffer.
- **While the replay runs the chip draws every frame,** shown or not, so the result does not
  depend on whether the host was looking at the picture (`Vdac2Card::Drawing`).

### 9.4 Checked by

`core/tests/debugger/ttd/ttdvdac2_test.cpp`, on a `TSL-VDAC2` machine running the TS-Labs
SDK program `test6.spg` (a 1940 x 768 image in `RAM_G`, scrolled every frame):

| Test | What it proves |
|:--|:--|
| `ChipBlobsAreRecorded` | both blobs are registered with their sizes |
| `SeekByFrameMatchesTheLiveRun` | seeking by frame number, back and forth: the picture equals the live picture at the end of that frame; at the live run's exact position the card state, the chip state, every memory region and the metrics block equal the live run's byte for byte |
| `SeekInsideFrameShowsTheFrameDrawnSoFar` | at three T-states inside frames: the picture equals the live machine's frame drawn so far (and differs from the last finished frame), state and memory equal |
| `SavedSessionReplaysTheSame` | the session written to a stream and read back seeks to the same pictures and state |
| `HistoryLimitKeepsTheChipRight` | with a 6-frame history limit (the oldest checkpoints and their blobs released while recording), saved and loaded: every kept frame seeks to the live picture, card state and chip memory |

Notes from building it:

- The card and the TS-Conf engine (whose DMA feeds the card) run lazily: the same moment can
  sit at different catch-up points in a live run and after a restore. Comparing state needs
  both brought to the CPU's position first (`CatchUpEngine`, `Synchronize`); after that the
  bytes are equal.
- A seek to an exact T-state lands on the first instruction boundary at or after it; at a
  few frame starts (14 MHz) that is a later boundary than the live run stopped at (2 T-states
  in the test). That is how TTD positions, not a card effect; the test compares state only
  where the position matched exactly.
- The one-frame lead-in is not proven necessary by these captures: the library redraws the
  frame in flight from line 0 after a restore, so the picture is right without it unless the
  memory changed during that FT812 frame before the checkpoint. It stays, correct by
  construction, for one extra replayed frame per seek.

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
| **Bus capture (built)** | WebAPI `POST …/vdac2/capture/start {path}`, `POST …/vdac2/capture/stop`, `GET …/vdac2/capture/status`; CLI `vdac2 capture start <path>\|stop\|status`; MCP `capture_media` actions `vdac2_capture_start` (filename) / `_stop` / `_status`; Lua `vdac2_capture_start/stop/status`; Python `emu.vdac2_capture_*`. All through `Vdac2Control` (`vdac2control.h`): the FT812 bus to an .evr replay stream (vdac2-test-corpus.md §4), started at any moment (the stream then begins with the chip's state) | `api/vdac2_api.cpp`, `openapi/openapi_vdac2.inc`, `cli-processor-vdac2.cpp`, `mcp-media.cpp`, `lua_vdac2.h`, `python_vdac2.h`; recipe `.recipe/machines/tsconf-vdac2.md` |
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
