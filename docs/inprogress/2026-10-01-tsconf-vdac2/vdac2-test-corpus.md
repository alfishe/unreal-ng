# VDAC2 test corpus and oracles

Design phase D-D of [vdac2-tdd.md](vdac2-tdd.md) §8: which programs, files and references
test the FT812 library ([eve-emu-architecture.md](eve-emu-architecture.md), "arch") and the
VDAC2 integration ([vdac2-integration-design.md](vdac2-integration-design.md), "D-C"), what
each test proves, and how the expected results are produced. Behavior rules are in
[ft812-behavior-spec.md](ft812-behavior-spec.md) ("spec").

Date: 2026-10-01. Status: in use: the SDK programs are in testdata, the golden cases and
replays in `eve-emu`.

## 1. Layers

| Layer | Lives in | Input | Expected result from |
|---|---|---|---|
| L-U unit | `eve-emu` repository, `tests/` | byte scripts on the SPI API | the spec's rules, written by hand |
| L-G golden image | `eve-emu`, `tests/` + `testdata/golden/` | one small display list or coprocessor stream per feature | the BT8XX oracle (§3.1) |
| L-R restore | `eve-emu`, `tests/` | every in-flight operation (arch §7.3) | the uninterrupted run (self-referenced) |
| L-P replay | `eve-emu`, `tests/` + `testdata/replays/` | SPI streams captured from real VDAC2 programs (§4) | the oracle's memory and frames for the same stream |
| L-I integration | unreal-ng, `core/tests/` | Z80 code driving the card through `#77` / `#57` and DMA | D-C rules (bus, IDE off, INT path, LUT, presentation) |
| L-M whole machine | unreal-ng, `core/tests/emulator/machines/tsconf/` | the programs of §2 booted from an SD image | frame hashes recorded once against the oracle (§3.2) and reviewed |

The library's layers run in its own repository; unreal-ng runs L-I and L-M.

## 2. Programs

All are public (§6). "Size" is the `.spg` file.

### 2.1 TS-Labs SDK programs (`tslabs/zx-evo` `pentevo/sdk/ft812sdk`)

Small, deterministic, one feature each; the first targets.

| Program | Size | Mode | Exercises (from its source) | Proves |
|---|---|---|---|---|
| `test1` | 4.5 KB | 7 (1024×768 59 Hz) | `POINTS` + `POINT_SIZE`, `LINES` + `LINE_WIDTH`, `BLEND_FUNC`, `CELL` text from ROM font, `ft_ccmd` stream | boot sequence (spec §2.2), primitives, ROM font bitmaps |
| `test2` | 3.5 KB | 3 (800×600 60 Hz) | points in many sizes and colors, `COLOR_A` | antialiased points, alpha |
| `test3` | 4.5 KB | 3 | `COLOR_MASK`, `CLEAR_COLOR_A`, two-pass blending | destination alpha, masks |
| `test4` | 942 KB | 3 | "DXT" images: L1/L2 masks + RGB565 layers, `BITMAP_TRANSFORM_A/E`, DMA upload | exact blending (spec §6.6), transforms, DMA `RAM_SPI` (spec §9 item 5) |
| `test5` | 40 KB | 3 | ZX screens through `PALETTE_SOURCE`, `VERTEX_TRANSLATE` | paletted bitmaps, translate |
| `test6` | 733 KB | 7 | animated "DXT" with `VERTEX_TRANSLATE_X` per frame, `DLSWAP` per frame | per-frame swap, INT_SWAP pacing |
| `test9` | 3.5 KB | 7 | `CMD_ROMFONT`, `CMD_SETFONT2`, `CMD_SETBASE`, `CMD_GRADIENT`, `CMD_TEXT`, `CMD_NUMBER` | coprocessor text (needs the ROM image) |
| `test_sd` | 11 KB | - | SD card access with the FT812 bus present | hub: SD and FT812 on one bus (D-C §4) |

### 2.2 TS-Labs demos (`pentevo/demos/examples`)

| Program | Size | Exercises |
|---|---|---|
| `ft_pong` | 2.5 KB | asm macros (`includes/ft81x.asm`, `ft_func.asm`), simple game loop |
| `tunnel` | 3.3 MB | large streamed data, sustained DMA uploads |

### 2.3 `ftview` (Wild Commander plugin, `pentevo/sdk/ft812sdk/ftview`)

Runs inside Wild Commander from an SD image. Test files, made for the purpose (§5.3):
a baseline JPEG and a grayscale JPEG under the pixel limit, a progressive JPEG and a CMYK
JPEG (must fail as on the card, tdd §2.7), PNG gray / truecolor / indexed / indexed with
transparency / truecolor with alpha, an oversized PNG, a `.dxp` from `dxt_conv`, a short
M-JPEG AVI with and without audio.

### 2.4 Games (local, untracked: `testdata/machines/tsconf/vdac2/`)

R-Type 1.01, Zuma Deluxe 1.1, Heroes of Might and Magic II v022. Large, long boots; used for
L-M boot-to-title checks and for capturing replay streams (§4), not for unit-level proof.

## 3. Oracles

### 3.1 BT8XX harness (library golden images and register traces)

A small C program, `tools/oracle/` in the `eve-emu` repository, built for Windows
(MinGW or MSVC), that loads `bt8xxemu.dll` (arch §2a):

- **input:** a test case file: a list of SPI transactions (select, bytes, deselect) with
  waits in milliseconds between them, or a display list plus the bitmaps it uses;
- **output:** the frame (`Graphics` callback, ARGB8888) as PNG; dumps of `RAM_DL`, chosen
  register values and `RAM_G` ranges after each step, as JSON;
- runs headless (`Graphics` callback set: no window).

Where it runs (O1, **settled 2026-10-01**): a Windows environment on the development
machine, with the harness built by the host's MinGW cross compiler (the DLL is loaded with
`LoadLibrary`; no import library is needed). A feasibility probe loaded `bt8xxemu.dll`
5.1.26 ("Author: Jan Boon", the Kaetemi emulator), read `REG_ID` = 0x7C, programmed
640×480, drew a display list (red clear, a green point) and captured the frame through the
`Graphics` callback with the expected pixels. The DLL needs the MSVC 2015+ runtime.

Known gaps of the oracle (AN_281): no INT pin, no power host commands, no coprocessor
reset, no snapshot. Tests of those rules use the spec as their reference (L-U), not the
oracle.

### 3.2 TS-Labs Unreal (whole-program reference)

TS-Labs Unreal (`tslabs/zx-evo-unreal`) runs the same programs on TS-Conf with the same
DLL. With two small logging patches it becomes the reference for whole programs:

- in `zc.cpp`, every `#77` write and every `#57` byte to the FT812 with the T-state and
  the byte returned (the input and the expected answers of a replay, §4);
- in `ft812.cpp`, every frame from the `Graphics` callback saved with its time.

Its own deviations are known (fake INT, separate window, no msel switch, D-C §1), so its
frames are a reference for the **FT812 picture**, not for the machine's presentation
timing.

### 3.3 The real card (when available)

Nothing in this plan requires one. If someone with a VDAC2 can run test programs, the
items of spec §12 marked "card" are settled first: V6 (coprocessor costs via
`REG_CLOCK`), V8 (line look-ahead), V7 (the look of a broken line), V13 (`CSPREAD`). A test
program for each is part of the corpus (§5.4) so the run is one SD card.

### 3.4 Formulas from the sources

The TS-Labs `dxt_conv` preview gives an independent blend formula (spec §6.6); R-Type's
`ft812_line_cost.py` gives line costs for real display lists (spec §5.2). Both are compared
with the library's results in L-G.

## 4. Replay streams

A replay stream is everything that crossed the FT812's bus, with the chip's clock: replayed
into the library alone it reproduces the chip's whole run, with no machine around it. It is
the reference workload for the library's tests (L-P) and for its performance work.

### 4.1 Format `.evr` v1 (little-endian)

Header, 64 bytes:

| Offset | Size | Field |
|---|---|---|
| 0 | 4 | magic `EVR1` |
| 4 | 4 | header size (64) |
| 8 | 4 | chip model (812) |
| 12 | 4 | external clock in Hz (VDAC2: 8 000 000) |
| 16 | 20 | SHA-1 of the ROM image the chip ran with (zeros: no ROM) |
| 36 | 4 | ROM image size in bytes |
| 40 | 4 | flags: bit 0 = the stream starts at the chip's power-on |
| 44 | 8 | the clock the stream starts at (0 from power-on) |
| 52 | 12 | reserved, zero |

Then records, each `kind (u8)`, `clocks since the previous record (unsigned LEB128)`,
payload. The clock is the chip's system clock count since its creation
(`EveTotalClocks`):

| Kind | Payload | Replay |
|---|---|---|
| 1 select | u8 level (1 = selected) | advance to the clock, `EveSelect` |
| 2 byte | u8 mosi, u8 miso | advance, `EveExchange(mosi)`; the result must equal miso |
| 3 frame | u64 completed frames, u8 drawn, u16 width, u16 height, u64 FNV-1a 64 of the ARGB8888 picture (bytes of each pixel, low byte first) | advance; `EveCompletedFrames` must equal the count; when drawn and the replay draws at the same size, the hashes must be equal |
| 4 power-on | - | advance, `EveReset` |
| 5 end | - | advance to the clock: the end of the capture |
| 6 state | u32 size, state blob, u32 regions, per region: u8 name length, name, u32 size, bytes | the first record of a capture started on a running chip: `EveLoadState`, the regions written back, `EveMemoryRestored` |

A replay draws every frame (`EveSetOutput` with drawing on, a buffer of the mode's
`HSIZE × VSIZE`, resized after a frame when the mode changed: the capture does the same),
so its frame hashes are comparable. A difference in miso or in a frame hash is a behavior
change; the clock of the record says where.

### 4.2 Capture

- **unreal-ng:** start and stop at any moment on every automation surface (WebAPI
  `/vdac2/capture/start|stop|status`, CLI `vdac2 capture`, MCP `capture_media`
  `vdac2_capture_*`, Lua / Python `vdac2_capture_*`; recipe
  `.recipe/machines/tsconf-vdac2.md`), or from the card's creation with
  `[VDAC2] CaptureFile=<path>`. Started on a running chip, the stream begins with a
  **state record** (kind 6: u32 size + the `EveSaveState` blob, u32 region count, then per
  region u8 name length, name, u32 size, bytes); the header's flags bit 0 is then clear and
  its bytes 44-51 hold the clock the stream starts at. Replay: `EveLoadState`, write the
  regions back, `EveMemoryRestored`; the chip's clock then equals the record's. While
  capturing, the chip draws every frame, also while the Evo is shown, so every frame record
  carries a hash (`vdac2capture.{h,cpp}`).
- **TS-Labs Unreal (§3.2):** the same format from its logging patches, converting its
  T-states to FT812 clocks with the mode's clock.

### 4.3 Uses

- **L-P replay tests** in the library: every miso byte-exact, every frame count and drawn
  hash exact; pixel rules still TO VERIFY are named in the test.
- **Performance:** the library's benchmark replays a stream (R-Type gameplay first) as fast
  as it can and reports clocks per second against the real-time rate; optimizations keep
  every hash.

## 5. Files the corpus needs

### 5.1 Committed to the `eve-emu` repository

Golden cases (display lists, streams, expected PNG and JSON), replay streams, the oracle
harness source. All generated by the tools in that repository; small.

### 5.2 Committed to unreal-ng

L-I tests (Z80 code built into the test as bytes, as the other TS-Conf tests do); the frame
hashes of L-M scenarios.

### 5.3 Built by scripts, never stored by hand

- SD card images for L-M: `tools/neogs/make_sd_image.py`-style FAT32 image builder
  (or the test helper `core/tests/_helpers/fatimagebuilder.h`) with the program and its
  files, built at test time.
- `ftview` test images: a script generating them with Pillow (baseline / progressive /
  CMYK JPEG, PNG variants, oversized), plus `dxt_conv` for the `.dxp`, plus ffmpeg for the
  M-JPEG AVI. Deterministic (fixed content, fixed encoder options).

### 5.4 Test programs to write

For the card-only items (§3.3) and for the integration, small Z80 programs in the style
of the project's emulated test tools (they print through the ROM, run as a user would,
come with a README):

| Program | Measures |
|---|---|
| `ftclock` | `REG_CLOCK` around each coprocessor command with growing sizes (V6) |
| `ftbeam` | `RAM_G` writes at known beam positions under a full-screen bitmap (V8) |
| `ftbudget` | display lists of growing cost per line (V7) |
| `ftint` | INT_N edges → TS-Conf line interrupt with msel = 1, counted per frame (D-C §6) |

### 5.5 Local only, never committed

The game binaries (§2.4), the ROM image (`rom/ft81x.rom`, D-C §10), `bt8xxemu.dll`.

## 6. What each acceptance item rests on

Spec §9 checklist → tests:

| Spec §9 item | Test |
|---|---|
| 1 boot sequence | L-U (`ft_init` byte script), L-P (`test1`) |
| 2 frame rates | L-U (all 15 modes) |
| 3 first swap + INT_SWAP | L-U; L-P (`test1`) |
| 4 CMDB_SPACE, fault, recovery | L-U; L-G (invalid inflate stream on the oracle) |
| 5 bulk write via DMA | L-I (DMA `RAM_SPI`); L-P (`test4`) |
| 6 INT_FLAGS clear on read | L-U |
| 7 REG_FRAMES rate | L-U; L-M (count over N machine frames) |
| 8 bitmaps, transforms, blending | L-G (one case per format / filter / blend pair); L-P (`test4`, `test5`, `test6`) |
| 9 CALL / JUMP / MACRO, persistent handles | L-G; L-P (R-Type stream) |
| 10 coprocessor commands | L-G (DL dumps from the oracle); L-P (`test9`, Zuma stream) |
| 11 drawing from live memory | L-G with timed writes; `ftbeam` on a card (§5.4) |

## 7. Open items

| # | Question | Proposal |
|---|---|---|
| O1 | ~~Where the BT8XX harness runs~~ | **settled:** a Windows environment on the development machine, MinGW-built harness (§3.1) |
| O2 | Commit the TS-Labs SDK `.spg` files (4 KB to 942 KB, 1.7 MB in total) to unreal-ng testdata so L-M runs in CI, or keep them local like the games | commit them: small, public, and the only programs that test one feature each; the games stay local |
| O3 | The patched TS-Labs Unreal for capture (§3.2): a separate local build, never committed | yes; the patches are described in the `eve-emu` repository's oracle README |
