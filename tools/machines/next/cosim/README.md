# ZX Spectrum Next co-simulation

Run the same Next boot on reference emulators and on ours, write what the machine does as one common text trace,
and diff the traces. First use: why the personality ROM (`enNextZX.rom`, loaded by the real firmware `TBBLUE.FW`
from a FAT card) does not reach NextZXOS on our emulator - findings in
`docs/inprogress/2026-10-07-zx-next/research-cosim-first-diff.md`.

| piece | file |
|---|---|
| trace format, run control variables, dump | [`trace-format.md`](trace-format.md) |
| build the reference emulators (headless, patched) | `build-ref.sh <jnext\|zesarux\|mame\|all>` |
| patches (never applied inside the clones) | `patches/jnext/`, `patches/zesarux/`, `patches/mame/` |
| card folder -> partitioned FAT SD image | `mk-sd-image.sh <card> <out.img> [MB] [fat16\|fat32]` |
| run a reference on a card | `run-ref.sh <jnext\|zesarux\|mame> <card> <frames> <out-dir>` |
| our side | `core/tests/_helpers/nextcosimtrace.h` + `core/tests/emulator/machines/next/nextcosim_test.cpp` |
| diff two traces | `diff-traces.py <ref> <ours>` |

Nothing from the Next firmware / OS is in the repository (licence): the cards live outside, under
`/Volumes/TB4-4Tb/Projects/emulators/cosim-cards/`, and tools take their path from the environment.

## Cards

| card | content | use |
|---|---|---|
| `cosim-cards/min` | `TBBLUE.FW`, `machines/next/{config.ini (timing=0), menu.def, enNextZX.rom, enNxtmmc.rom, enNextMf.rom, keymap.bin}` - the layout our firmware test uses (`scratch/nextcard`) | our boot tests |
| `cosim-cards/full` | the same plus the rest of `machines/next/` (48.rom, 128.rom, enAltZX.rom, ...), `nextzxos/`, `sys/`, `dot/`, `home/` from the `tbblue` distribution (`~/Downloads/zx-spectrum/zx-next/os`, `.../firmware`) | reaching the NextZXOS screens on a reference. 3 MB |

`min` is **not enough for the real firmware**: it stops with `Error reading file: c:/machines/next/enAltZX.rom`
(the firmware reads `enAltZX.rom`, `48.rom`, ... from the card). Use `full` (or add those files to `min`) whenever
the aim is to see what the firmware does after the personality ROM starts. The folders are provisioned by hand
(see "Provision the cards"); the SD images (`<card>.img`, 1 GB sparse FAT32, MBR partition type `0C`) are built on
demand by `run-ref.sh` from the folder when the folder is newer.

### Provision the cards

```bash
tools/machines/next/cosim/provision-cards.sh /Volumes/TB4-4Tb/Projects/emulators/tbblue-dist \
    /Volumes/TB4-4Tb/Projects/Test/unreal-ng/scratch/wt-next-p2/scratch/nextcard
```

`tbblue-dist` is a checkout of `gitlab.com/thesmog358/tbblue` (here a symlink to the clone under
`emulators/gitlab/tbblue`); the second argument is the card folder our firmware test uses (`min`). `config.ini`
(`timing=0`) is not in the distribution; the script writes it.

## Reference emulators: versions, dependencies, build

Host used: macOS 15.8 (Darwin 24.6), arm64, Apple clang 17, CMake 4.0.3, Ninja 1.11, Homebrew.

| emulator | version | commit | what is built | deps (brew) |
|---|---|---|---|---|
| jnext | 0.99.155 | `810cfdff22ac` (jorgegv/jnext, 2026-08-13) | the SDL front-end `jnext` (no Qt, no debugger), Release -O2 | `cmake ninja sdl2-compat` (SDL2 API) `libpng spdlog` (zlib, curl, openssl from the SDK / brew) |
| ZEsarUX | 13.1-SN | `2d8dba1` (chernandezba/zesarux, 2026-10-08) | `zesarux` with only the `null` video / audio drivers (no Cocoa / X11 / SDL / curses) | a C compiler, `make`; nothing else |
| MAME | 0.289 (clone `f43983b62edf`, 2026-09-23) | | `specnext` - MAME reduced to `src/mame/sinclair/next/specnext.cpp` (4 machines: `tbblue`, `specnext_ks1/2/3`) | `sdl3 python3`; first build ~20 min on 10 cores |

```bash
tools/machines/next/cosim/build-ref.sh all        # or: jnext | zesarux | mame   (--clean as 2nd arg starts over)
```

What the script does: copies the clone (`/Volumes/TB4-4Tb/Projects/emulators/github/<name>`) to
`/Volumes/TB4-4Tb/Projects/emulators/build/<name>/src` with `rsync` (never touching the clone), applies the patches of
`patches/<name>/`, configures and builds there. Builds go to `build/jnext/bld`, `build/zesarux/src/zesarux`,
`build/mame/src/specnext`. Variables: `NEXT_COSIM_BUILD`, `NEXT_COSIM_CLONES`, `NEXT_COSIM_CARDS`, `JOBS`.

Notes per emulator:

* **jnext**: the clone's `third_party/spdlog` submodule is empty (no network needed this way), so `0001` points
  CMake at the Homebrew spdlog (`find_package(spdlog)`); the other options (`-DENABLE_TESTS=OFF
  -DENABLE_DEBUGGER=OFF -DENABLE_QT_UI=OFF -DJNEXT_ENABLE_LTO=OFF`) skip Qt and the unit tests. It boots the *real*
  chain: its own copy of the FPGA boot ROM -> `TBBLUE.FW` from the SD image -> NextZXOS. It extracts the ROMs it needs
  from the image at start (`/MACHINES/NEXT/48.rom`, `enAltZX.rom`, `enNxtmmc.rom`, `enNextMf.rom`): a card without
  them boots with warnings. The SD image needs a partition of type `0C` (FAT32 LBA) - FAT16 is refused, hence the
  FAT32 images. `--headless --silent --sdcard <img> --sdcard-readonly --rtc "2026-01-01 12:00:00"`
  (deterministic clock), `--delayed-screenshot` for the picture.
* **ZEsarUX**: `./configure --disable-cocoa --disable-xwindows --disable-xext --disable-sdl --disable-curses
  --disable-cursesw --disable-aa --disable-caca --disable-coreaudio --disable-sndfile --disable-fbdev`; run with
  `--vo null --ao null --machine tbblue --enable-mmc --mmc-file <img> --enable-divmmc-ports`. Its tbblue machine
  also runs the real `TBBLUE.FW` from the image (its own `tbblue_loader.rom` stands in for the FPGA boot ROM).
  The patched binary counts video frames and exits by itself at `COSIM_FRAMES`, saving `screen.bmp`. 1500 frames take
  ~100 s (paced by the host timer; `--emulatorspeed` does not lift the pacing).
* **MAME**: `specnext specnext_ks2 -bios v30100 -rompath <cards>/mame-roms -hard1 <img>`. The boot ROM set
  `tbblue/boot-30100.bin` is jnext's `roms/nextboot.rom` (CRC `ccbd55ba`, the FPGA 3.01.00 boot ROM; `build-ref.sh`
  copies it). The SD image goes in as a raw `-hard1`. The patched driver saves the screen with MAME's own snapshot
  code and exits at `COSIM_FRAMES`; `run-ref.sh` collects the PNG. About 10 s for 1500 frames (`-nothrottle`).

All three reach the NextZXOS "Welcome" screen on the `full` card within ~1500 frames.

## Patches

`patches/<emulator>/*.patch` are plain `diff -u` patches (`patch -p1` in the copied tree), minimal and
env-driven (`COSIM_*`, see `trace-format.md`); with `COSIM_TRACE` unset a hook is one mask test.

| patch | touches |
|---|---|
| `jnext/0001-cmake-use-system-spdlog.patch` | `CMakeLists.txt`: system spdlog instead of the submodule |
| `jnext/0002-cosim-trace.patch` | new `src/debug/cosim_trace.h`; `PortDispatch::read/write` wrapped (POUT/PIN, #243B/#253B excluded); the CPU-side NextREG paths in `emulator.cpp` (port #253B pair and the `NEXTREG` instruction); the per-instruction point (PC, PCS, MMU poll); `Z80Cpu::execute` (IRQ); `run_frame` (FRM, the dump) |
| `zesarux/0001-cosim-trace.patch` | new `cosim_trace.h`; the implementation at the end of `machines/tbblue.c`; the NR set/get paths (`tbblue_set_value_port`, `tbblue_get_value_port`, the two `NEXTREG` opcodes in `cpus/z80_codpred.c`); wrappers of `out_port_spectrum_no_time` / `lee_puerto_spectrum_no_time` in `operaciones.c`; `core_spectrum.c` (instruction start, frame end, interrupt) |
| `mame/0001-cosim-trace.patch` | new `cosim_trace.h`, `specnext.cpp`: the #253B pair and the Z80N NextREG callbacks, I/O taps on the CPU's I/O space, an M1 tap for PCS, `bank_update` for MMU, the IRQ acknowledge callback, `on_vblank` (frames, dump + snapshot + exit) |

Writing new patches: copy the tree as `build-ref.sh` does, edit the copy, and `diff -u --label a/<f> --label b/<f>
<pristine> <edited>` per file.

## Running

```bash
R=tools/machines/next/cosim
$R/run-ref.sh jnext   full 1500 /tmp/cosim/jnext      # trace.txt state.txt screen.png run.log
$R/run-ref.sh zesarux full 1500 /tmp/cosim/zesarux
$R/run-ref.sh mame    full 1500 /tmp/cosim/mame
COSIM_PORT_SKIP=eb,e7 COSIM_PCWIN=300:2 $R/run-ref.sh jnext full 400 /tmp/cosim/jnext-pc   # per-instruction PCS in frames 300-301
```

Ours (a `core-tests` run; build with `tools/build/build.sh core-tests`, run the binary directly or via
`tools/build/test.sh --gtest_filter='NextCosim*'`):

```bash
UNREAL_NEXT_FIRMWARE=/Volumes/TB4-4Tb/Projects/emulators/cosim-cards/full \
UNREAL_NEXT_COSIM=/tmp/cosim/ours UNREAL_NEXT_COSIM_FRAMES=1500 \
  tools/build/test.sh --gtest_filter='NextCosim_Test.*'
```

(`UNREAL_NEXT_FIRMWARE` accepts the same card folders as `nextfirmware_test.cpp`; the card is presented through
`HostFolderFat`, no image needed. The `COSIM_*` variables of `trace-format.md` apply to this run too.)

Diff:

```bash
$R/diff-traces.py /tmp/cosim/jnext /tmp/cosim/ours --skip-ports eb,e7 --ref-name jnext --ours-name ours --limit 20
```

## Known limits

* NextREG traffic of Copper and DMA is not reported by the references (CPU-originated only); our sink reports what
  the CPU did through `NextBoard::Write`, which has no other writers yet.
* `IRQ` on our side is inferred (IFF1 fell, no DI, a new instruction at the vector); the references report the
  acknowledge. The diff tool does not compare IRQ.
* Timing is not compared anywhere: only order per kind, collapsed runs, and the end-of-run dump.
* ZEsarUX's tbblue machine has no real FPGA boot ROM and ZEsarUX / MAME / jnext all model the firmware's SPI /
  SD protocol to the extent the real `TBBLUE.FW` needs; a behaviour present in only one of them is a hint, not a
  proof about the hardware (the official VHDL in `~/Downloads/zx-spectrum/zx-next/fpga` is the authority).
