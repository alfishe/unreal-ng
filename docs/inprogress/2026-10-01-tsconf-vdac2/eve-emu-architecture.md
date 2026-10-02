# eve-emu — library architecture and implementation brief

Design phase D-B of [vdac2-tdd.md](vdac2-tdd.md) §8. This document is written so that a
separate agent can implement the library without other context: what to build, where,
with which interface, how it is built and linked, how state and time work, and how it is
accepted. **What the chip does** is defined in
[ft812-behavior-spec.md](ft812-behavior-spec.md) (cited as "spec §n"); this document
decides how that behavior is organized in code.

Date: 2026-10-01. Status: draft for review; no code exists.

Decisions by the user, 2026-10-01:

- not in `core/src/3rdparty`, but a **separate static library** linked into the emulator;
- **a build option per decoder**: the built-in, vendored decoder, or one passed in by the
  host through a function-pointer interface;
- **standardized decoder interfaces** in the public includes;
- **runtime state**: a latched stable snapshot for TTD, with the operation in flight
  described by extra fields that let a restore restart it (§7).

## 1. Scope

- **FT812 only.** It is the VDAC2 chip; every rule comes from the spec. The rest of the
  EVE family (FT80x, FT810/811/813, BT81x) may be added later (§12), but nothing beyond
  the FT812 is implemented or tested now. The library's README states this.
- **No knowledge of the host machine.** No ZX-Evo, no TS-Conf, no unreal-ng types. The
  library is a chip with an SPI port, a clock input, an interrupt pin and a picture
  output.
- **Out of scope:** widgets (`CMD_BUTTON` …), `CMD_SNAPSHOT*`, `CMD_SKETCH`, spinner,
  screensaver, logo, touch, NOR flash, audio samples (audio timing is in scope),
  `REG_RENDERMODE` 1. They fault or do nothing as the spec says (spec §7.5, §10).

## 2. Deliverables and acceptance

The implementing agent delivers:

1. The library in the layout of §3, building as a static library with zero warnings on
   clang, gcc, MinGW and MSVC (C++17, the project's warning flags).
2. The public headers of §4 and §5, unchanged in shape unless a review agrees otherwise.
3. Unit tests, golden-image tests, restore tests and benchmarks of §10, all passing
   (golden tests of TO VERIFY items report, they do not fail; §10).
4. `eve-tunables.h` (§9) with every TO VERIFY constant and its spec reference.
5. A `README.md` in the library root: scope (FT812 only), build options, how to run the
   tests, the library version.

Acceptance: the checklist of spec §9 passes as tests; the restore tests of §10.3 pass for
every operation in §7.3; the library builds stand-alone (`cmake -S . -B build` in its
repository) and inside unreal-ng through `add_subdirectory`; `nm` of the static library shows only `Eve*` symbols, nothing from
the vendored decoders (§6.3).

Rules for the implementer:

- Follow the spec. Where the spec says TO VERIFY, implement the stated design choice,
  put the number or switch into `eve-tunables.h`, and do not invent beyond it.
- Naming per the project: PascalCase functions and types, camelCase variables, no
  underscores in file names or C++ names; public C API names carry the `Eve` prefix;
  internals live in namespace `EveLib`.
- No OS calls, no threads, no exceptions across the API, no global mutable state (every
  bit of state is inside the chip context), no allocation after `EveCreate` except
  inside decoders.
- American English in comments and docs.

## 2a. References and oracles

There is **no open-source FT81x emulator** (checked 2026-10-01: GitHub repositories and
code, MAME, ZEsarUX, Xpeccy, ZXMAK2, the TS-Labs repositories). What exists:

| What | Kind | Use for the implementer |
|---|---|---|
| **Bridgetek EVE Emulator** (`bt8xxemu.dll`), https://github.com/Bridgetek/EVE_Emulator | **closed source**, Windows binaries + public header `bt8xxemu.h`; current release 5.1.26 (2026-09-25) | **The reference oracle.** Behavior-level model of FT80x / FT81x / BT81x; it runs the **real coprocessor ROM**, so the display list words it generates for `CMD_TEXT`, matrix commands, `CMD_LOADIMAGE` etc. are the chip's. Since 5.1.26 it states "hardware-matching" rendering of lines, rectangles and points. Drive it through `BT8XXEMU_defaults` / `BT8XXEMU_run` with mode `BT8XXEMU_EmulatorFT812`, `BT8XXEMU_chipSelect`, `BT8XXEMU_transfer` (one SPI byte each way), frames through the `Graphics` callback (ARGB8888). Known gaps per AN_281: no INT pin model, no power-mode host commands, no coprocessor reset, no `CMD_SNAPSHOT`, no multi-touch. Use it to produce the golden images (§10.2), to dump `RAM_DL` after coprocessor commands (spec V5) and to read registers (spec V9-V11, V14-V17). It needs a Windows environment; the harness is a small C program built with MinGW ([vdac2-test-corpus.md §3.1](vdac2-test-corpus.md)) |
| AN_281 "FT8xx Emulator Library User Guide", https://brtchip.com/wp-content/uploads/Support/Documentation/Programming_Guides/ICs/EVE/AN_281_FT800_Emulator_Library_User_Guide.pdf | document | the emulator's API, parameters (`RomFilePath`, `CoprocessorRomFilePath`, `Graphics` callback) and limitations |
| TS-Labs Unreal, `Unreal/ft812.cpp` and `Unreal/zc.cpp`, https://github.com/tslabs/zx-evo-unreal | open source wrapper around the closed DLL | a working example of driving `bt8xxemu.dll` byte by byte from an emulated SPI port; it also ships `bt8xxemu.dll` / `bt8xxemu_x64.dll` in `Unreal/cfg/` |
| The FT81x ROM image | extracted from `bt8xxemu.dll` | fonts 16-34 and the font table at `ROM_FONTROOT` (spec §1, design §5.5). The 1 MB at 0x200000-0x2FFFFF is identical in the Bridgetek and the TS-Labs DLL. The library never contains the image; tests load it from a local file and skip when it is absent |
| Bridgetek EveApps, https://github.com/Bridgetek/EveApps ; RudolphRiedel FT800-FT813, https://github.com/RudolphRiedel/FT800-FT813 ; James Bowman gd2-lib, https://github.com/jamesbowman/gd2-lib | open-source host libraries | constants, legal host traffic, init sequences; none of them emulates the chip |
| TS-Labs FT812 SDK, demos, ESP32 firmware, https://github.com/tslabs/zx-evo (`pentevo/sdk/ft812sdk`, `pentevo/demos/examples`, `pentevo/esp32`) | open source, VDAC2 host side | the authoritative host behavior; the SDK test programs (`test1`-`test9`) are the first replay targets (§10.4) |
| VDAC2 games: R-Type https://github.com/andrewinsidelazarev/R-Type-Arcade-VDAC2-FT812 , Zuma https://github.com/andrewinsidelazarev/Zuna-Deluxe-VDAC2-FT812 , HMM2 https://github.com/andrewinsidelazarev/Heroes-of-Might-and-Magic-II-for-VDAC2 | open source (MIT) | real workloads; R-Type's `Source/Tools/ft812_line_cost.py` and `CLAUDE.md` hold the line-cost model and the pitfalls verified on the card |
| FT81X Series Programmers Guide v1.2 and the FT81x datasheet (URLs in the spec) | documents | the primary specification behind the behavior spec |

The closed emulator is a **test oracle only**: nothing from it is linked into the library,
and its output is used to check rules, never copied as code or tables.

## 3. Layout and build

### 3.1 Where

The library is **its own repository, `eve-emu`**, developed next to the other emulator
sources on the development machine (where `unreal-z80` also lives; user decision
2026-10-01). It is a self-contained CMake project:

```
eve-emu/
  CMakeLists.txt            stand-alone project, also used through add_subdirectory
  README.md
  LICENSE                   MIT
  include/eve/
    eve.h                   chip API (§4)
    decoders.h              decoder interfaces (§5)
    version.h               EVE_VERSION_MAJOR / MINOR / PATCH
  src/
    eve-internal.h          chip context, control state, constants (§8)
    eve-tunables.h          TO VERIFY constants (§9)
    eve-chip.cpp            create / destroy / reset, chip parameter table (§12)
    eve-spi.cpp             SPI front end, host commands (spec §2)
    eve-memory.cpp          regions, the single write path, dirty marking (spec §1)
    eve-registers.cpp       register table and side effects (spec §3)
    eve-timing.cpp          clocks, scan, frame events, swap, INT_N (spec §4, §5)
    eve-dl.cpp              line executor, context, flow control, line cost (spec §5.2, §6.1-6.3)
    eve-raster.cpp          points, lines, strips, rectangles (spec §6.4)
    eve-bitmap.cpp          formats, sampling, filters, wrap, text formats (spec §6.5)
    eve-pixel.cpp           scissor, alpha, stencil, blend, mask, tag (spec §6.6)
    eve-output.cpp          rotate, swizzle, CSPREAD shift, host frame buffer (spec §6.8)
    eve-copro.cpp           ring reader, dispatch, cost model, faults (spec §7.1-7.4)
    eve-copro-mem.cpp       memory commands, INFLATE, APPEND, MEMCRC (spec §7.5)
    eve-copro-matrix.cpp    matrix commands (spec §7.5)
    eve-copro-text.cpp      TEXT, NUMBER, fonts, SETBITMAP, GRADIENT (spec §7.5)
    eve-copro-media.cpp     media FIFO, LOADIMAGE, PLAYVIDEO (spec §7.5, §8)
    eve-audio.cpp           audio engine timing (spec §3.3)
    eve-state.cpp           stable snapshot, in-flight operation records (§7)
    eve-debug.cpp           inspection (§4.6)
    eve-decoders.cpp        selection of built-in / external decoders (§6)
  vendor/                   built-in decoders, compiled only when selected (§6.3)
    tinfl/                  zlib inflate (from miniz)
    stb/                    stb_image (PNG + baseline JPEG)
  tests/                    gtest, <source>_test.cpp per source file (§10)
  testdata/                 golden images and display lists, SPI replays
  benchmarks/               Google Benchmark
```

A source file is split further only past about 1500 lines; one file per spec area keeps
the spec and the code diffable.

### 3.2 CMake

```cmake
# eve-emu/CMakeLists.txt (sketch)
project(eve-emu VERSION 0.1.0 LANGUAGES CXX C)

set(EVE_DECODER_INFLATE "BUILTIN" CACHE STRING "zlib inflate: BUILTIN or EXTERNAL")
set(EVE_DECODER_PNG     "BUILTIN" CACHE STRING "PNG: BUILTIN or EXTERNAL")
set(EVE_DECODER_JPEG    "BUILTIN" CACHE STRING "baseline JPEG: BUILTIN or EXTERNAL")
set_property(CACHE EVE_DECODER_INFLATE EVE_DECODER_PNG EVE_DECODER_JPEG
             PROPERTY STRINGS BUILTIN EXTERNAL)
option(EVE_BUILD_TESTS "Build eve-emu tests" OFF)
option(EVE_BUILD_BENCHMARKS "Build eve-emu benchmarks" OFF)

add_library(eve-emu STATIC ${EVE_SOURCES} ${EVE_VENDOR_SOURCES_FOR_SELECTED_DECODERS})
add_library(eve::emu ALIAS eve-emu)
target_include_directories(eve-emu PUBLIC include PRIVATE src vendor)
target_compile_features(eve-emu PUBLIC cxx_std_17)
target_compile_definitions(eve-emu PUBLIC
    EVE_HAS_BUILTIN_INFLATE=$<STREQUAL:${EVE_DECODER_INFLATE},BUILTIN>
    EVE_HAS_BUILTIN_PNG=$<STREQUAL:${EVE_DECODER_PNG},BUILTIN>
    EVE_HAS_BUILTIN_JPEG=$<STREQUAL:${EVE_DECODER_JPEG},BUILTIN>)
```

- The `EVE_HAS_BUILTIN_*` definitions are **public**, so a host can tell at compile time
  what it must supply. `EveGetBuildInfo()` (§4.1) reports the same at run time.
- Tests and benchmarks are built in the library's own repository (googletest and
  benchmark found or fetched there). A host that includes the library does not build them
  unless it sets `EVE_BUILD_TESTS`.
- **In unreal-ng** (details in D-C): the library is a git submodule at `lib/eve-emu`, like
  `lib/googletest` and `lib/benchmark`. The root `CMakeLists.txt` adds
  `add_subdirectory(${EVE_EMU_DIR})` before `core/src`, with the cache variable
  `EVE_EMU_DIR` defaulting to `lib/eve-emu`, so a developer can point it at a local
  checkout of the library while working on both. `core/src/CMakeLists.txt` links
  `target_link_libraries(core PRIVATE eve::emu)`. unreal-ng's choice of decoders is part
  of D-C; the default is `BUILTIN` for all three.

## 4. Chip API — `include/eve/eve.h`

Plain C, so the library can be used from any language and its ABI is simple. All
functions take the chip first; none of them call back into the host except the decoder
interfaces of §5.

### 4.1 Lifetime and configuration

```c
typedef struct EveChip EveChip;

typedef enum EveModel { EVE_MODEL_FT812 = 0 } EveModel;     // the only model (§1, §12)

typedef struct EveConfig
{
    uint32_t structSize;              // sizeof(EveConfig), for later extension
    EveModel model;
    uint32_t externalClockHz;         // VDAC2: 8 000 000 (spec §2.2)
    const uint8_t* romImage;          // FT81x ROM 0x1E0000..0x2FFFFF (1152 KB), or NULL (spec §1)
    size_t romImageSize;              // the library keeps the pointer; the host keeps the memory
    const EveDecoders* decoders;      // NULL = built-ins only (§5, §6)
} EveConfig;

typedef struct EveBuildInfo
{
    uint32_t version;                 // (major << 16) | (minor << 8) | patch
    uint8_t builtinInflate, builtinPng, builtinJpeg;
} EveBuildInfo;

void EveGetBuildInfo(EveBuildInfo* out);
EveChip* EveCreate(const EveConfig* config);   // NULL if a required decoder is missing (§6.2)
void EveDestroy(EveChip* chip);
void EveReset(EveChip* chip);                  // power-on (spec §2.2)
const char* EveLastError(void);                // reason of the last failed EveCreate / EveLoadState
```

### 4.2 Host bus

```c
void EveSelect(EveChip* chip, int selected);    // CS_N low (1) / high (0) - spec §2.1
uint8_t EveExchange(EveChip* chip, uint8_t mosi);
```

### 4.3 Time

```c
void EveAdvance(EveChip* chip, uint64_t systemClocks);
uint64_t EveClocksToNextEvent(const EveChip* chip);  // next INT_N change, frame end,
                                                     // coprocessor completion, timer
uint32_t EveSystemClockHz(const EveChip* chip);      // 0 while stopped
uint64_t EveTotalClocks(const EveChip* chip);        // since creation; not REG_CLOCK
```

- **The host owns time.** It calls `EveAdvance` up to the moment of a bus access before
  calling `EveSelect` / `EveExchange`, so every access sees the right scan position and
  coprocessor progress (spec §5.1).
- Between accesses the host advances lazily: up to `EveClocksToNextEvent` when it watches
  INT_N, and at its own frame end.

### 4.4 Outputs

```c
int EveIntAsserted(const EveChip* chip);              // INT_N low (spec §5.4)

typedef struct EveTiming
{
    uint16_t hcycle, hoffset, hsize, hsync0, hsync1;
    uint16_t vcycle, voffset, vsize, vsync0, vsync1;
    uint16_t pclkDivider;  uint32_t pixelClockHz;  uint64_t framePeriodClocks;
} EveTiming;
void EveGetTiming(const EveChip* chip, EveTiming* out);

void EveSetOutput(EveChip* chip, uint32_t* framebuffer, uint32_t stridePixels,
                  uint32_t widthCapacity, uint32_t heightCapacity, int drawing);
uint64_t EveCompletedFrames(const EveChip* chip);     // frames fully scanned out
```

- The host owns the frame buffer (ARGB8888). Lines are written into it as they are
  scanned out (§8.4). A mode larger than the capacity is clipped and reported through
  `EveGetTiming`, so the host can re-allocate.
- `drawing = 0` keeps all timing and draws nothing (VDAC2 with msel = 0).
- `EveCompletedFrames` changing means a whole frame is in the buffer: the host latches it
  (VDAC2: at the FT812 VSYNC, design §6.4).

### 4.5 State

```c
size_t EveStateSize(const EveChip* chip);                 // fixed for the chip's lifetime (§7.4)
void EveSaveState(const EveChip* chip, void* out);        // control state + in-flight header (§7)
int EveLoadState(EveChip* chip, const void* in, size_t size);   // 0 = ok

typedef struct EveRegion
{
    const char* name;            // "RAM_G", "DL0", "DL1", "REG", "CMD", "SPECIAL", "INFLIGHT"
    uint8_t* base; size_t size;
    const uint64_t* dirty;       // one bit per 4 KB page
    size_t pageCount;
} EveRegion;
size_t EveRegionCount(const EveChip* chip);
void EveGetRegion(const EveChip* chip, size_t index, EveRegion* out);
void EveClearDirty(EveChip* chip);
void EveMemoryRestored(EveChip* chip);    // the host wrote region contents back (TTD / snapshot)
```

- Saving = `EveSaveState` (small, **fixed size**, as unreal-ng's `TTDSerializable`
  requires) plus the dirty pages of the regions. Loading = write
  the region contents back, call `EveMemoryRestored`, then `EveLoadState`. §7 describes
  what the state contains and how a restore restarts an operation in flight.

### 4.6 Inspection (never changes state)

```c
uint8_t EvePeek(const EveChip* chip, uint32_t address);   // no clear-on-read
size_t EveGetDisplayList(const EveChip* chip, int active, uint32_t* words, size_t max);
int EveDisassemble(uint32_t word, char* text, size_t size);
void EveGetCoprocessor(const EveChip* chip, EveCoproView* out);   // pointers, phase,
                                                                   // command, cost left, fault,
                                                                   // matrix, font table
void EveGetLineCost(const EveChip* chip, uint32_t line, EveLineCost* out);  // spec §5.2
int EveProbePixel(const EveChip* chip, uint32_t x, uint32_t y, EvePixelSource* out);
```

`EveProbePixel` re-runs that line's executor in probe mode (a template parameter), so
normal drawing pays nothing for it.

## 5. Decoder interfaces — `include/eve/decoders.h`

Standard, minimal, and the same whether the decoder is built in or supplied by the host.
The library validates everything the chip validates (sizes, formats, limits) itself;
decoders only decode.

```c
typedef enum EveDecodeStatus
{
    EVE_DECODE_OK = 0,           // finished
    EVE_DECODE_NEED_INPUT = 1,   // streaming: give more input
    EVE_DECODE_OUTPUT_FULL = 2,  // streaming: give more output space
    EVE_DECODE_ERROR = 3         // invalid data: the chip faults (spec §7.4)
} EveDecodeStatus;

// --- zlib inflate, streaming (CMD_INFLATE data arrives in ring-sized pieces) ----
typedef struct EveInflateDecoder
{
    size_t stateSize;                                  // bytes of decoder state the library allocates
    int stateIsPlain;                                  // 1: the state (incl. its dictionary) is a plain
                                                       // blob without pointers; it is saved and reloaded
                                                       // for TTD (§7.3). 0: the library keeps the input
    void (*Begin)(void* state, void* user);            // start a zlib stream (2-byte header, spec V16)
    EveDecodeStatus (*Run)(void* state, void* user,
                           const uint8_t* in, size_t inSize, size_t* inUsed,
                           uint8_t* out, size_t outSize, size_t* outWritten);
    void* user;
} EveInflateDecoder;

// --- still images: the whole file at once ----------------------------------------
typedef enum EveImageLayout
{
    EVE_IMAGE_GRAY8,             // PNG gray, JPEG gray
    EVE_IMAGE_RGB888,            // PNG truecolor, JPEG color
    EVE_IMAGE_RGBA8888,          // PNG truecolor + alpha
    EVE_IMAGE_INDEXED8           // PNG indexed; palette as RGBA8888, up to 256 entries
} EveImageLayout;

typedef struct EveImageInfo
{
    uint32_t width, height;
    EveImageLayout layout;
    uint8_t interlaced, progressive, cmyk, bitDepth, paletteHasAlpha;
} EveImageInfo;

typedef struct EveImageDecoder
{
    // Read the header only: the library applies the chip's rules before decoding
    // (baseline JPEG, PNG bit depth 8, no Adam-7, no CMYK, pixel limits - spec §7.5).
    EveDecodeStatus (*Probe)(void* user, const uint8_t* data, size_t size, EveImageInfo* info);
    // Decode into the canonical layout reported by Probe; palette only for INDEXED8.
    EveDecodeStatus (*Decode)(void* user, const uint8_t* data, size_t size,
                              uint8_t* pixels, size_t pixelsSize,
                              uint32_t* palette, size_t paletteEntries);
    void* user;
} EveImageDecoder;

typedef struct EveDecoders
{
    uint32_t structSize;
    const EveInflateDecoder* inflate;   // NULL = built-in (if compiled in)
    const EveImageDecoder* png;         // NULL = built-in
    const EveImageDecoder* jpeg;        // NULL = built-in; also used for M-JPEG video frames
} EveDecoders;

// The built-ins, so a host can wrap or test them; NULL when not compiled in.
const EveInflateDecoder* EveBuiltinInflate(void);
const EveImageDecoder* EveBuiltinPng(void);
const EveImageDecoder* EveBuiltinJpeg(void);
```

- **Canonical output, chip conversion inside.** Decoders return gray, RGB, RGBA or
  indexed pixels. The conversion to the chip's bitmap formats (L8, RGB565, ARGB4,
  PALETTED565 / PALETTED4444, the palette in `RAM_G`) is done by the library, so it is the
  same whatever decoder is used (spec §7.5 `CMD_LOADIMAGE`).
- **Inflate state belongs to the library.** The decoder declares its state size; the
  library allocates it inside the chip context. A decoder whose state is a plain blob
  (`stateIsPlain`, the built-in `tinfl` is) lets a restore continue exactly where it was
  by reloading that blob; otherwise the library keeps the consumed input (§7.3).
- **M-JPEG** (AVI video) frames are decoded with the JPEG decoder, frame by frame; the AVI
  container is parsed by the library.

## 6. Decoder selection

### 6.1 Build options

`EVE_DECODER_INFLATE`, `EVE_DECODER_PNG`, `EVE_DECODER_JPEG`, each `BUILTIN` or
`EXTERNAL` (§3.2).

- `BUILTIN`: the vendored decoder is compiled into the library. The host may still pass
  its own decoder in `EveConfig.decoders`; it then takes precedence.
- `EXTERNAL`: nothing is compiled in. The host must pass the decoder.

### 6.2 Run-time rules

- At `EveCreate`, for each of the three kinds: the host's decoder if given, else the
  built-in if compiled in, else none.
- **No decoder of a kind = `EveCreate` fails** with a clear reason
  (`EveLastError()` text), because the chip would otherwise fault on valid software.
  A host that knowingly runs without a decoder (a test) passes a stub that returns
  `EVE_DECODE_ERROR`.

### 6.3 Built-in decoders

- **inflate:** `tinfl` from miniz (single-file, streaming, no allocation, state is one
  struct).
- **PNG and JPEG:** `stb_image`, PNG and JPEG only (`STBI_ONLY_PNG`, `STBI_ONLY_JPEG`).
  The library's own `Probe` rejects what the chip rejects before stb sees it.
- **No symbol leaks.** The host (unreal-ng) links its own miniz and lodepng. The
  built-ins must not export a single symbol: `stb_image` with `STB_IMAGE_STATIC` in one
  translation unit; `tinfl` compiled in a translation unit that gives it internal
  linkage (wrapped in an anonymous namespace or with its functions renamed by macro).
  Acceptance checks this with `nm` (§2).
- Versions and licenses of the vendored code are recorded in `vendor/README.md`.

## 7. Runtime state: stable snapshot and operations in flight

### 7.1 The rule

The chip state seen by TTD is a **stable snapshot**: registers, pointers and memory as
they are, with every completed operation applied. An operation that is still running
(an inflate, an image decode, a video frame, a memory command still in progress) is not
part of that snapshot. When it starts, the library writes an **in-flight record** with
everything needed to run it again. `EveSaveState` returns the stable snapshot plus that
record. `EveLoadState` restores the snapshot and **restarts the operation from its
restart point** so that it continues exactly where it was. Nothing is lost, and every
point in time can be restored.

### 7.2 What is stable

- `REG_CMD_READ` moves as the coprocessor consumes ring words, also inside a command
  with data (spec §7.2). So the ring does **not** hold the consumed part of a command in
  flight: the host may already have overwritten it. Whatever a restart needs from that
  part is in the in-flight record.
- Memory holds whatever the operation has written so far. That matches the chip (the
  host could read partial output), and the dirty-page capture records it.
- The control state (§8.1) holds no decoder internals and no pointers.

### 7.3 Restart rules per operation

| Operation | Restart point | In-flight record holds | Restore does |
|---|---|---|---|
| `CMD_INFLATE` | the exact point reached | destination, output bytes written, clocks spent, and **the decoder state** (a plain blob including its 32 KB dictionary, about 43 KB with `tinfl`, independent of the stream length) | loads the decoder state and continues live from the ring |
| `CMD_INFLATE` with a decoder that cannot save its state (§5) | command start | as above without the state, plus **the compressed bytes consumed so far** (copied when read) | re-runs the decoder over the recorded input silently, then continues live |
| `CMD_LOADIMAGE` (ring or media FIFO) | command start | pointer, options, **the image bytes collected so far** | collects again from the record; decoding happens when the image is complete, as live |
| `CMD_LOADIMAGE` after decoding, while output is being applied | command start | the same record (complete input) and output bytes applied | decodes again from the record, applies the remaining output |
| `CMD_PLAYVIDEO`, `CMD_VIDEOFRAME` | start of the current frame | AVI stream parameters (frame size, frame count, frame period, audio format), frame index, media FIFO read pointer **at that frame's start**, the frame's bytes collected so far, audio position | restarts decoding of the current frame (M-JPEG frames are independent) |
| `CMD_MEMCPY`, `CMD_MEMSET`, `CMD_MEMZERO`, `CMD_APPEND` | the offset reached | parameters, bytes done | continues from the offset. Not from the start: an overlapping `MEMCPY` would read bytes it has already changed |
| `CMD_MEMCRC` | the offset reached | parameters, bytes done, the running CRC | continues |
| `CMD_MEMWRITE` | the offset reached | pointer, count, bytes done | continues with the remaining data from the ring; the consumed part is already in memory |
| DL-producing commands (`TEXT`, `NUMBER`, `SETMATRIX`, `SETBITMAP`, `ROMFONT`, `SETFONT2`, `GRADIENT`) | command start | **the parameters and string, copied into the record** (consumed from the ring), DL words emitted so far | re-emits silently up to the recorded count, continues |
| `CMD_DLSTART` waiting for a swap | the wait | nothing beyond the phase | waits again |
| `CMD_INTERRUPT` timer | the timer | deadline in clocks | re-arms |
| audio playback, sound effect | current position | start, length, read pointer, clocks into the current sample | continues |
| SPI transaction in progress | exact | phase, address, byte count (part of the control state) | continues |

**Silent re-run** means: the operation is executed from its restart point up to its
recorded progress without consuming emulated time, without raising events, and writing
the same bytes into memory that is already holding them. The result after restore is
bit-identical to the uninterrupted run; §10.3 tests that at every point.

### 7.4 Cost and bounds

- **Fixed-size state, variable data in a region.** unreal-ng's `TTDSerializable` takes
  a fixed-size blob, saved whole at every checkpoint. So the in-flight record is split:
  - a small **header** (operation, parameters, progress counters, the copied parameters
    and string of DL-producing commands, up to a fixed bound) lives in the control state;
  - its **variable data** (the inflate decoder state with its 32 KB dictionary, about
    43 KB; the image bytes collected so far; the input copy of the fallback path) lives in
    the region `INFLIGHT`, a fixed-capacity buffer allocated at `EveCreate` and captured
    like the other regions, by dirty 4 KB pages. Its capacity is `kInflightCapacity`
    (1 MB + 64 KB, enough for the largest image file the chip accepts plus the decoder
    state; TO VERIFY against real files).
- So a checkpoint costs nothing extra while no operation runs, and during an operation
  only the pages it changed. An input larger than `INFLIGHT` faults the command, as an
  image the chip cannot hold would.
- The input-copy fallback for an inflate decoder without saveable state is bounded by the
  same capacity; the built-in decoder never needs it. The record is cleared when the
  command completes.
- **Why not hide the operation's output until it completes.** Two reasons. The chip writes
  output into `RAM_G` progressively: the display list may already show a picture that is
  still being decoded, and the host may read partial output. And it would not remove the
  need for the record, because `REG_CMD_READ` moves during the command and the consumed
  input is gone from the ring either way.
- `EveStateSize` includes the record, so the host sees the real size at every checkpoint.
  TTD stores it with the other device state; a capture during a large `LOADIMAGE` is
  larger than usual and that is expected.
- Derived state is never saved: line buffers, the cost table of the last frame, decoder
  internals. After `EveLoadState` the current frame is redrawn by the next catch-up
  (§8.4) from the restored memory and scan position.

## 8. Internal architecture

### 8.1 Chip context

One allocation in `EveCreate`; layout in `eve-internal.h`:

- `ControlState` — trivially copyable, versioned, saved whole: SPI front end, power and
  clock, the registers whose value is not plain memory, timing (total clocks, line,
  pixel position, exact pixel-clock remainder), the active display list index,
  coprocessor phase and pointers, coprocessor state (matrix, colors, scratch handle,
  number base, font pointers per handle, media FIFO), the graphics engine's per-handle
  bitmap parameters (32 records, spec §6.2), audio timing, interrupt flags.
- **Regions** — `RAM_G` 1 MB, `DL0` / `DL1` 8 KB, `REG` 4 KB (the register file the host
  reads), `CMD` 4 KB, `SPECIAL` (0x309000…), `INFLIGHT` (§7.4), each with a dirty bitmap
  per 4 KB page.
- **In-flight record** (§7) — a header in the control state, variable data in the
  `INFLIGHT` region (§7.4).
- **Derived** — two line buffers (color, alpha, stencil, tag), the line cost table,
  decoder state, decoded-image work buffer.

### 8.2 The single write path

Every byte written to the chip (SPI, coprocessor, decoder output) goes through
`Write(address, byte)`:

1. If drawing reads that address (`RAM_G`, the active display list, a register that
   drawing uses): catch up the picture first (§8.4).
2. Store the byte; mark its page dirty.
3. Run the register side effect (`eve-registers.cpp` table: reset value, write mask,
   read-only flag, side effect, read hook).

Reads go through `Read(address)`, which applies read hooks (`REG_INT_FLAGS` clear on
read, `REG_CLOCK` computed, `REG_CMDB_SPACE` computed). Nothing touches memory any other
way, so dirty tracking and beam ordering cannot be bypassed.

### 8.3 Time and the coprocessor

- `EveAdvance` moves the system clock in steps that stop at every event: line boundary,
  frame end (swap, `REG_FRAMES`, `INT_SWAP`), coprocessor completion, audio event, timer,
  `REG_ID` delay. Nothing runs per clock between events.
- The pixel clock is `system / REG_PCLK`, kept as an exact remainder so no drift
  accumulates.
- The coprocessor is a state machine (idle, executing with cost left, waiting for data,
  waiting for a swap, faulted, in reset). A command's effect is applied when its cost has
  elapsed; long commands apply in chunks as their cost elapses. Costs are the
  `eve-tunables.h` constants (spec §7.2, V6). With all costs zero the engine runs
  commands instantly: a test mode only.

### 8.4 Drawing in step with the beam

- With drawing on, a **catch-up** draws every line from the last drawn one up to the line
  the beam is preparing (line N during line N − 1, `kLineLookahead`), runs the output
  stage and writes the row into the host frame buffer.
- Catch-up runs before any write that drawing reads (§8.2), at every frame end, and when
  the host asks for a frame.
- **One line:** reset the context (per `kContextReset`, spec V1), clear the line buffers,
  run the active list from index 0: state commands update the context and the handle
  table; vertices produce the primitive's span on this line and its pixels go through the
  pipeline (spec §6.6); flow commands use a 4-level stack; every command and every span
  adds to the line cost (spec §5.2). Over budget: count and report (spec V7).
- **Spans, not shapes.** Each primitive is asked only for its span on the current line,
  with per-pixel coverage on antialiased edges. Bitmaps compute the sample position at
  the span start and step it per pixel.
- **Fast paths** come after the naive version is correct and benchmarked: identity
  matrix, NEAREST, PALETTED4444 / ARGB4 / RGB565, simple blending. Hot loops are tagged
  `SIMD-CANDIDATE`.

## 9. TO VERIFY constants — `src/eve-tunables.h`

Every open item of spec §12 that can be a number or a switch is one line, with its spec
reference and the current choice:

```cpp
// spec §5.1, V8: how many lines ahead of the scan a line is drawn
constexpr uint32_t kLineLookahead = 1;                              // TO VERIFY
// spec §6.2, V1: when the graphics context is reset
enum class ContextReset { PerLine, PerFrame, Never };
constexpr ContextReset kContextReset = ContextReset::PerLine;       // TO VERIFY
// spec §5.2, V7: clocks of the line period not available to drawing
constexpr uint32_t kLineBudgetOverhead = 0;                         // TO VERIFY (R-Type: ~44 at HCYCLE 1344)
// spec §7.2, V6: coprocessor costs in system clocks
constexpr uint32_t kCostCommand = 0;                                // TO VERIFY
constexpr uint32_t kCostInflatePerOutputByte = 0;                   // TO VERIFY
...
```

When an item is settled, its constant gets the measured value and a comment with the
evidence, and the spec's V-row is closed. No guess is hidden inside an expression.

## 10. Tests and benchmarks

All under `tests/` (gtest) and `benchmarks/` of the library repository, run there.
unreal-ng runs only its own integration tests against the linked library (D-C, D-D).

### 10.1 Unit tests

One file per source file, against the spec's rules: SPI framing, auto-increment, the two
write exceptions; host commands and clock; register reset values, masks, clear-on-read;
frame periods for all 15 TS-Labs modes (spec §4); swap and INT timing; every display list
command's state effect; every coprocessor command's memory effect; faults and recovery;
decoder selection (built-in, external, missing); dirty marking on every write path.

### 10.2 Golden images

Small display lists, one feature each (a format, a transform, a blend mode, a primitive,
a text string); expected images from the BT8XX emulator, stored as PNG with the list that
produced them under `eve-emu/testdata/golden/`. Exact comparison. A golden test of a rule
that is still TO VERIFY reports the difference instead of failing, and says which V-item
it belongs to.

### 10.3 Restore tests

For each operation of §7.3: run it uninterrupted and record memory, registers and frames;
then, for a set of points spread over the operation (every coprocessor event, and random
points in between), save, load into a fresh chip, run to the end, and compare
byte for byte with the uninterrupted run. A failure names the operation and the point.

### 10.4 Replays

SPI byte streams captured from the TS-Labs SDK programs and the VDAC2 games (D-D) replayed
into the library, compared with stored memory dumps and frames.

### 10.5 Benchmarks

A typical R-Type frame at 1024×768 with drawing; the same with drawing off; `CMD_INFLATE`
throughput (built-in); one SPI byte; `EveAdvance` across an idle frame; `EveSaveState`
during a large `LOADIMAGE`.

## 11. Integration contract with unreal-ng (input for D-C)

- unreal-ng creates one `EveChip` per VDAC2 card, passes the 8 MHz clock and the ROM
  image (extracted by the tool of design §5.5), and decoders per D-C.
- It converts Z80 time to FT812 system clocks and calls `EveAdvance` before each SPI
  access, at its frame end and at `EveClocksToNextEvent` while it watches INT_N.
- It registers the seven regions as TTD v2 device regions and stores `EveSaveState`
  (fixed size) in the device blob at every checkpoint.
- It gives the library its FT812 frame buffer and latches a frame into the presentation
  queue when `EveCompletedFrames` changes.

## 12. The family later

Nothing beyond the FT812 is built, but these choices keep it possible:

- One chip table in `eve-chip.cpp`: memory map (FT80x: `RAM_DL` 0x100000, registers
  0x102400, `RAM_CMD` 0x108000, 256 KB `RAM_G`), register offsets (different on FT80x),
  RGB width, `RAM_G` size, ROM layout, touch type, available commands. One row today.
- Register offsets go through the table, not hard-coded in the units.
- Display list opcodes and coprocessor codes are shared across the family; the few that
  differ would be table flags.
- `EveModel` has one value; adding a chip means a table row, its spec and its tests.

## 13. Open for review

| # | Question | Proposal |
|---|---|---|
| A1 | ~~Where the library lives~~ | **decided 2026-10-01:** its own repository next to the other emulator sources; unreal-ng takes it as the submodule `lib/eve-emu` (§3.2) |
| A2 | ~~Built-in image decoder~~ | **decided 2026-10-01:** `stb_image` for PNG and JPEG (can change later) |
| A3 | ~~unreal-ng's decoder choice~~ | **decided 2026-10-01:** `BUILTIN` for all three (can change later) |
