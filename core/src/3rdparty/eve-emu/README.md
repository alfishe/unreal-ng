# eve-emu

An emulator of the **FT812** EVE graphics controller (Bridgetek / FTDI), as a static
C++17 library with a plain C API. It was written for the TS-Labs VDAC2 card of the ZX-Evo
(TS-Conf) and is used by the unreal-ng emulator, but it knows nothing about the host
machine: the chip has an SPI port, a clock input, an interrupt pin and a picture output.

Version: 0.1.0 (`include/eve/version.h`). Status: L0-L3b implemented; behavior that the
documents do not settle is measured on the BT8XX reference (golden cases) or follows the
stated design choices of `src/eve-tunables.h` (see "Open" below).

## What is implemented

- **Host bus:** SPI framing (one dummy byte, two with `REG_SPI_WIDTH` bit 2),
  auto-increment, `RAM_CMD` write wrap, the `REG_CMDB_WRITE` FIFO port, host commands and
  power states, clock selection.
- **Registers and time:** the FT812 register table (reset values, widths, read-only,
  clear-on-read, computed registers), scan timing in system clocks, frame events, frame
  and line swaps, `INT_N`, `EveClocksToNextEvent`, audio playback / sound effect timing.
- **Graphics engine:** the display list runs line by line from live memory, in step with
  the beam: every FT81x opcode, call stack, macros, context stack, all primitives with
  antialiased edges, all FT812 bitmap formats, NEAREST / BILINEAR, wrap, cells, palettes,
  the per-pixel pipeline, line cost and budget, `REG_TAG`, output stage (all eight
  `REG_ROTATE` orientations, `REG_SWIZZLE`, `REG_CSPREAD`). Fast paths are bit-exact with
  the general path.
- **Coprocessor:** the ring with costs (all 0 until measured: instant), faults and recovery,
  memory commands, `INFLATE`, matrices, `TEXT` / `NUMBER` / fonts / `SETBITMAP`,
  `GRADIENT`, the undocumented command codes, the media FIFO, `LOADIMAGE`, `PLAYVIDEO`,
  `VIDEOSTART`, `VIDEOFRAME`.
- **State:** a fixed-size control state plus seven memory regions with dirty pages; an
  operation in flight is described by the state and the `INFLIGHT` region and continues
  exactly after a restore (tested at points spread over every operation).

## Open

- Widgets, `CMD_SNAPSHOT*`, `CMD_SKETCH`, spinner, screensaver and logo fault by design
  (spec §7.5).
- Antialiasing: points, rectangles and lines match the BT8XX reference (a table measured
  on it, `tools/oracle/make-aa-table.py`), except a few slanted-line pixels whose
  perpendicular foot lies next to a whole 1/16 unit; edge strips match except one
  pixel at the top vertex of an `EDGE_STRIP_L` (spec V2).
- `REG_TAP_CRC` is not computed.
- Timing (coprocessor durations, line budget, beam look-ahead, frame origin, video pacing)
  needs measurements on the card (spec V6...V9, V12, V18).
- SPI replays of the SDK programs and games need captures.

## Scope

- **FT812 only.** The rest of the EVE family (FT80x, FT810/811/813, BT81x) is not
  implemented or tested; the chip table in `src/eve-chip.cpp` / `src/eve-registers.cpp`
  keeps the door open.
- Not emulated: widgets (`CMD_BUTTON` ...), `CMD_SNAPSHOT*`, `CMD_SKETCH`, spinner,
  screensaver, logo, touch, NOR flash, audio samples (audio *timing* is emulated),
  `REG_RENDERMODE` 1.
- Behavior that the documentation does not settle is a named constant in
  `src/eve-tunables.h`, marked `TO VERIFY` with its reference in the behavior
  specification.

## Build

```sh
git submodule update --init
cmake -S . -B build -G Ninja
ninja -C build -j <half the cores>
./build/eve-tests
```

Inside a host project:

```cmake
add_subdirectory(path/to/eve-emu)
target_link_libraries(my-target PRIVATE eve::emu)
```

### Options

| Option | Default | Meaning |
|---|---|---|
| `EVE_DECODER_INFLATE` | `BUILTIN` | zlib inflate for `CMD_INFLATE`: `BUILTIN` (vendored miniz `tinfl`) or `EXTERNAL` (the host passes one in `EveConfig.decoders`) |
| `EVE_DECODER_PNG` | `BUILTIN` | PNG for `CMD_LOADIMAGE`: `BUILTIN` (vendored `stb_image`) or `EXTERNAL` |
| `EVE_DECODER_JPEG` | `BUILTIN` | baseline JPEG for `CMD_LOADIMAGE` and M-JPEG video: `BUILTIN` (`stb_image`) or `EXTERNAL` |
| `EVE_BUILD_TESTS` | `ON` stand-alone, `OFF` as a subproject | googletest suite (`eve-tests`) |
| `EVE_BUILD_BENCHMARKS` | `OFF` | Google Benchmark suite (`eve-benchmarks`) |
| `EVE_WARNINGS_AS_ERRORS` | `ON` | `-Werror` / `/WX` for the library's own sources |
| `EVE_BUILD_TOOLS` | `ON` stand-alone, `OFF` as a subproject | `eve-replay` (below) |
| `EVE_SIMD` | `ON` | NEON / SSE2 kernels (`src/eve-simd.h`); `OFF` builds the plain C++ fallback, with the same results byte for byte |

The public compile definitions `EVE_HAS_BUILTIN_INFLATE`, `EVE_HAS_BUILTIN_PNG` and
`EVE_HAS_BUILTIN_JPEG` (0 / 1) tell a host at compile time which decoders it must supply;
`EveGetBuildInfo()` reports the same at run time. `EveCreate` fails when a decoder of a
kind is neither built in nor passed.

The target `eve-check-symbols` checks with `nm` that the archive defines only `Eve*` /
`EveLib::` symbols: nothing from the vendored decoders leaks into the host's link.

Warnings: the library builds with zero warnings under `-Wall -Wextra -Wshadow
-Wconversion` (clang, gcc, MinGW) and `/W4` (MSVC). A MinGW cross toolchain file is in
`cmake/toolchains/`.

## The ROM image

The FT81x ROM (0x1E0000-0x2FFFFF, 1152 KB: fonts 16-34 and the font table) is not part of
the library. The host passes it in `EveConfig.romImage`; without it, ROM reads return 0.

`tools/extract-ft81x-rom.py` cuts the whole image out of a `bt8xxemu.dll` you have (the
Bridgetek EVE Emulator or the copy in TS-Labs Unreal) and checks its font table and
glyphs. Tests that need glyphs look for it in `$EVE_ROM_PATH` or `testdata/rom/ft81x.rom`
and skip when it is absent. The image is never committed.

## Tests

```sh
./build/eve-tests                     # all
./build/eve-tests --gtest_filter='EveSpi*'
```

One `tests/<source>_test.cpp` per library source file, plus `eve-restore_test.cpp`
(restore of every operation in flight, arch §10.3) and `eve-acceptance_test.cpp` (the
checklist of spec §9). Benchmarks (`-DEVE_BUILD_BENCHMARKS=ON`, `./build/eve-benchmarks`):
an R-Type-like 1024x768 frame takes about 5.5 ms with drawing and 3.4 us without on an
Apple M1 Ultra.

## Replaying a bus capture: `eve-replay`

A host can capture everything on the chip's bus into an `.evr` stream: every chip select
change and every byte with the chip's answer, stamped with system clocks, plus a record
at each frame end with a hash of the picture (format: unreal-ng
`docs/inprogress/2026-10-01-tsconf-vdac2/vdac2-test-corpus.md` §4; unreal-ng writes it
with `vdac2 capture start <path>` and the other automation surfaces). `eve-replay` drives
the library with it alone and checks every answer byte, every frame count and every
drawn picture, then reports the speed against the chip's real time:

```sh
./build/eve-replay game.evr --rom testdata/rom/ft81x.rom             # check + time
./build/eve-replay game.evr --rom testdata/rom/ft81x.rom --no-hash   # time the drawing only
./build/eve-replay game.evr --no-draw                                # timing model only
```

The `cpu:` line is the figure to compare on a busy machine. Captures are local files (a
game's capture is ~0.5 MB per second of play); results and method:
[docs/performance.md](docs/performance.md).

## Reference cases

`testdata/golden/<case>/` holds scripts run on Bridgetek's BT8XX emulator (`bt8xxemu.dll`,
which runs the real coprocessor ROM) with its answers: frames and memory dumps.
`eve-golden_test.cpp` runs every script on eve-emu: a case marked `verify -` must match
exactly, a case of a TO VERIFY item (`verify Vn`) reports its differences. eve-emu's own
outputs land in `<build>/golden-out/`.

`tools/oracle/` makes the cases: `harness.c` (a Windows program that drives the DLL from a
script), `oracle.py`, `make-golden.py` and the `probe-*.py` measurements;
`make-aa-table.py` turns the `aa-*` cases into `src/eve-aa-table.cpp`. Running the harness is
site-specific and described by `EVE_ORACLE_DIR` / `EVE_ORACLE_RUN` (see `oracle.py`).

### Known limits of the reference

What the reference answers here is not evidence of the chip:

- `REG_CLOCK` right after boot: depends on the reference's own start-up timing.
- `REG_DATESTAMP`: holds the reference's build string.
- Audio: the reference does not play sound in this setup (`REG_PLAYBACK_READPTR` stays 0,
  `REG_PLAYBACK_PLAY` is not cleared).
- `REG_TAP_CRC`: not computed by the reference (reads 0).
- `ROM_FONT` 0x1E0000…0x1FFFFF: host reads and `CMD_MEMCRC` see zeros there, while the
  reference holds the full 1152 KB image and draws font 34 (from 0x1E1B5C) completely. The
  datasheet maps `ROM_FONT` there; eve-emu follows the datasheet. No golden cases for font 34
  are made from the reference.

## License

MIT, see `LICENSE`. Vendored code: see `vendor/README.md`.
