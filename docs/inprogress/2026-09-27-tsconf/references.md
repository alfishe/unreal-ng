# Reference Survey — TSConf Sources

Survey of local reference repositories (cloned into the sibling
`emulators/github/` directory next to the project), performed 2026-09-27
(**updated same day after a premise correction**, again with full external
links, and re-verified in review round 1). Deep links point at the upstream default branch; local clones are
`--depth 1` (`zx-evo-tsconf` is a blob-filtered sparse checkout of
`pentevo/fpga/current` + `pentevo/specs`).

## Premise correction

Two lookalikes had to be ruled out before the real references were pinned down:

- [`alfishe/pentevo`](https://github.com/alfishe/pentevo) (mirror of the NedoPC
  SVN `svn://svn.nedopc.com/pentevo`; newer mirror:
  [`aaydev/zxevo.pentevo`](https://github.com/aaydev/zxevo.pentevo); product
  page: <http://nedopc.com/zxevo/zxevo_eng.php>) — its `fpga/baseconf` is the
  ZX-Evo **Base Configuration** (ATM Turbo 2 lineage; effectively what
  unreal-ng's existing `ATM3` machine implements). **Not TS-Conf.**
- `pentevo/tools/unreal_fix/0.39.0/` in the same tree — Unreal 0.39.0 patched
  for baseconf-era ZX-Evo (Z-controller IDE `zc.cpp`, SD loader protocol
  `sdcard.cpp`/`TSdCard`, GS `gsz80.cpp`). **Not TS-Conf** (verified: no
  `0xNNAF` register model, no VDU modes, no DMA — in `original/`, `nedopc/`,
  `Unreal_NS/SRC/` alike; no TSConf branch exists locally, on the GitHub
  mirrors, or in the SVN itself).

TS-Conf is TS-Labs' FPGA configuration for the same board, maintained in the
[`tslabs/zx-evo`](https://github.com/tslabs/zx-evo) project (its `pentevo/`
tree is the TS-Conf fork of the same SVN layout — this is the "unreal in
pentevo" lineage: `pentevo/unreal/Unreal`). There is no `tslabs/vga-tsconf`
repository; the org is [`tslabs`](https://github.com/tslabs).

## Active references

| Repo (local clone dir) | Upstream | What it is | Value for this design |
|:--|:--|:--|:--|
| **zx-evo-unreal** | <https://github.com/tslabs/zx-evo-unreal> ([Unreal/](https://github.com/tslabs/zx-evo-unreal/tree/main/Unreal)) | TS-Labs Unreal fork with full TS-Conf support — the **direct ancestor of unreal-ng's partially-ported code**. Sources are **cp1251**: search with `LC_ALL=C grep -a` | **Primary porting blueprint** (structure): ports decoder (`io.cpp` `ts_ext_port_wr`), TSU/DMA task machines (`tsconf.cpp`), `update_screen` budget loop (`draw.cpp`), SD (`zc.cpp`, `sdcard.cpp`), SPG + depackers (`snapshot.cpp`, `depack.cpp`), debugger panel. Diverges from [V] in ~12 behaviors — hardware-spec §12 |
| **zx-evo-tsconf** | <https://github.com/tslabs/zx-evo> ([pentevo/fpga/current](https://github.com/tslabs/zx-evo/tree/master/pentevo/fpga/current), [pentevo/specs](https://github.com/tslabs/zx-evo/tree/master/pentevo/specs)) | TS-Conf FPGA Verilog (`top.v`, `common/`, `video/`, `z80/`, `dram/`, `vg93/`, `sound/`, `texts/`) | **Authoritative hardware spec** — register semantics, arbitration, timings. Three shipped builds with their own `tune.v`: `quartus` (IDE, standard), `quartus_vdac`, `quartus_vdac2` (hardware-spec §0.1) |
| **zx-evo-docs** | <https://github.com/tslabs/zx-evo-docs> ([TSconf/](https://github.com/tslabs/zx-evo-docs/tree/main/TSconf)) | TS-Conf documentation set | Register/reset tables. **TSconf.xls is the only complete one** (read with Python `xlrd`); `tsconf_en.md` leaves SPI/sound/RTC/VDOS empty, `tsconf_ru.md` is a stub — [tsconf_en.md](https://github.com/tslabs/zx-evo-docs/blob/main/TSconf/tsconf_en.md), [tsconf_ru.md](https://github.com/tslabs/zx-evo-docs/blob/main/TSconf/tsconf_ru.md), **[TSconf.xls](https://github.com/tslabs/zx-evo-docs/blob/main/TSconf/TSconf.xls)** (authoritative), [memory.txt](https://github.com/tslabs/zx-evo-docs/blob/main/TSconf/memory.txt), [GluExt extension docs](https://github.com/tslabs/zx-evo-docs/tree/main/GluExt) |
| **SPG format** | [zx-evo-docs/Formats](https://github.com/tslabs/zx-evo-docs/tree/main/Formats) | SPG v1.0/v1.1 specs | block descriptor + compression codes (0 raw, 1 MegaLZ, 2 Hrust) |
| **xpeccy-plus** | <https://github.com/dotkoval/xpeccy-plus> ([src/libxpeccy](https://github.com/dotkoval/xpeccy-plus/tree/main/src/libxpeccy); upstream [samstyle/Xpeccy](https://github.com/samstyle/Xpeccy)) | Full C emulator: [hardware/tslab.c](https://github.com/dotkoval/xpeccy-plus/blob/main/src/libxpeccy/hardware/tslab.c), [video/tsconf.c](https://github.com/dotkoval/xpeccy-plus/blob/main/src/libxpeccy/video/tsconf.c), [sdcard.c](https://github.com/dotkoval/xpeccy-plus/blob/main/src/libxpeccy/sdcard.c), [vfat.c](https://github.com/dotkoval/xpeccy-plus/blob/main/src/libxpeccy/vfat.c), [xstate.c](https://github.com/dotkoval/xpeccy-plus/blob/main/src/libxpeccy/xstate.c) | Independent implementation: windows, video modes, SD/vFAT, state; known-defect list |
| **mame** | <https://github.com/mamedev/mame> ([src/mame/sinclair/evo](https://github.com/mamedev/mame/tree/master/src/mame/sinclair/evo)) | Full C++ driver: [tsconf.cpp](https://github.com/mamedev/mame/blob/master/src/mame/sinclair/evo/tsconf.cpp), [tsconf_m.cpp](https://github.com/mamedev/mame/blob/master/src/mame/sinclair/evo/tsconf_m.cpp), [tsconf_dma.cpp](https://github.com/mamedev/mame/blob/master/src/mame/sinclair/evo/tsconf_dma.cpp), [tsconf_beta.cpp](https://github.com/mamedev/mame/blob/master/src/mame/sinclair/evo/tsconf_beta.cpp), [tsconf_rs232.cpp](https://github.com/mamedev/mame/blob/master/src/mame/sinclair/evo/tsconf_rs232.cpp) | Clean device-oriented reference; only open implementation of DMA transparent-blit, cache wait-states, virtual TR-DOS |
| **unreal-ng (self)** | this repository | Partially-ported ancestor code: `core/src/emulator/platforms/tsconf/tsconf.h`, `core/src/emulator/video/tsconf/screentsconf.*`, `data/configs/ts-conf/unreal.ini`, `MM_TSL` hooks in z80.cpp/rom.cpp/config.cpp | The starting point — technical-design.md §3.1. Also: `data/rom/zxevo.rom` (512 KB; TS-Conf ROM set in pages 0-3), `data/rom/ts-bios*.rom` (64 KB variants); the shared `SdCardSpi` on the `neogs` branch (`scratch/wt-neogs`) |

Key file-level entry points inside the Verilog clone (all under
[`pentevo/fpga/current`](https://github.com/tslabs/zx-evo/tree/master/pentevo/fpga/current)):
`z80/zports.v` (port decode + register reset), `z80/zmem.v` (windows, ROM
mapping, cache), `z80/zmaps.v` (FMAPS), `z80/zint.v` (INT controller),
`z80/zclock.v` (clock + stalls), `z80/zkbdmus.v` (mouse), `common/dma.v`
(DMA engine), `common/spi.v`, `sound/sound.v` (beeper/Covox DAC), `video_top.v` / `video_mode.v` / `video_ts.v` /
`video_ts_render.v` / `video_sync.v` / `video_ports.v` (VDU + TSU),
`dram/arbiter.v` (priority), `texts/` (canonical developer notes:
`video_info.txt`, `lock128.txt`, `dram_access.txt`, `video_modes.txt`).
The power-on palette is `video/mem/video_cram.mif`. **Where a Verilog comment
contradicts the code, the code wins** (known cases: clock switch "at RFSH" in
`zclock.v:22`, the vdos INT comment in `zint.v:29-31`, "32 of 3.5MHz" in
`arbiter.v:41`).

## MAME software list

[`hash/tsconf.xml`](https://github.com/mamedev/mame/blob/master/hash/tsconf.xml) —
31 software entries: 25 snapshot entries holding **27 `.spg`** files (one entry
has three parts) — 3,584 B … 3,172,352 B, candidate TTD/test fixtures — and 6
floppy entries holding **8 `.trd`** images (80-track DS, 655,360 B). No HDD/IDE
images; MAME implements no IDE for TSConf. MAME's `ts-bios` ROM CRCs differ
from the images in unreal-ng's `data/rom/`.

## Discrepancy notes

- MAME raster: 448×320 total, 7 MHz dot clock, ≈48.8 Hz, visible ≤ 360×288 —
  hardware-correct (text mode doubles horizontally to 720×288)
- MAME implements TS-Conf 2 "copper" only on the separate `tsconf2` (EvoMAX3)
  machine — out of scope for `TSCONF` v1
- Xpeccy [`tsconf-text-artifacts` branch](https://github.com/dotkoval/xpeccy-plus/tree/tsconf-text-artifacts)
  (text-mode fixes) is **already merged into `main`** (verified with
  `git merge-base --is-ancestor`)
- Nemo IDE **is** built in the standard `quartus` firmware (and TS-BIOS offers
  IDE boot); Xpeccy implements it too. Deferred in unreal-ng until the shared IDE
  core exists (technical-design D2; resolved 2026-09-29: scheme `NEMO-DIVIDE`)
- MAME's Beta map (`.mirror(0xff00)`, `0x9F`) and 7FFD decode (`port & 0x8002`)
  are loose decodes, not hardware; MAME's zclk 3 = 28 MHz and 10-bit DMA_NUM
  are bugs (hardware-spec §12)
- All resolved divergences (Verilog vs ancestor vs MAME vs Xpeccy) are recorded
  with decisions in [hardware-spec.md](hardware-spec.md) §12
