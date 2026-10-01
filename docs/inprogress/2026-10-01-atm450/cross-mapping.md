# ATM450 — cross-mapping and state of the references

Survey performed 2026-10-01 over the local reference tree under `emulators/github/`
(next to the project). Line numbers were verified on that date and will drift —
the file/symbol names are the stable anchors.

## 1. Which ATM models exist in which emulator

The ATM family across all surveyed emulators is exactly four machines plus
TSConf; **we already ship three of them**:

| Model | unreal-speccy | zx-evo-unreal | ZXMAK2 / kozynax | Xpeccy(-plus) | unreal-ng |
|---|---|---|---|---|---|
| ATM Turbo 1 (v1-2.x) | — | — | — | dead enum `HW_ATM1` (`hardware.h:20`, referenced only from `filer.cpp:141`; no `atm1.c` exists) | — (never) |
| **ATM Turbo 2 v4.50** | `MM_ATM450` (`emul.h:58-68`, names `vars.cpp:206-218`) | same | `MemoryAtm450.cs` | — | `MM_ATM450`, **not creatable — this program** |
| ATM Turbo 2+ v7.10 | `MM_ATM710` | same | `MemoryAtm710.cs` | `HW_ATM2` ("ATM Turbo 2+ (v7.10)", `hardware.c:102`) | creatable |
| ATM3 / ZX-Evo / PentEvo | `MM_ATM3` | "ZX-Evo" | `MemoryPentEvo.cs` ("PentEvo") | `HW_PENTEVO` ("Evo Baseconf") | creatable (`ATM3`) |
| TSConf (same board, other FPGA conf) | `MM_TSL` | `MM_TSL` | — | `HW_TSLAB` | creatable (`TSL`) |

Notes from the survey:

- **"PentEvo" is not a separate machine**: the emulators' PentEvo = ZX-Evo
  running baseconf = our `ATM3` (`emulators/github/pentevo/fpga/baseconf` is
  the baseconf firmware itself). Nothing to implement; an optional `PENTEVO`
  create-alias for cross-emulator compatibility is a one-line factory case if
  ever wanted.
- **UnrealSpeccyP has no ATM support at all** (Pentagon 128K only; the
  `-DUSE_ATM` in its rpi makefile is dead). **Zero-Emulator, Spectral,
  ZXSpeculator, zxsp** are not ATM emulators.
- **kozynax** is a ZXMAK2-engine port; identical `Atm/` + `Evo/` modules.
- **Nobody implements ATM Turbo 1** and the reference tree contains no ATM1
  hardware documentation — that variant is unreachable by porting.

## 2. Variant comparison (why 450 is not a 710 subset)

| Aspect | ATM450 (v4.50) | ATM710 (ours) | ATM3 (ours) |
|---|---|---|---|
| RAM / ROM | 512K (opt 1M) / 4×16K in `atm1.rom` | up to 1M / 4×16K `atm2.rom` | 4M / 512K `zxevo-fe.rom` |
| Paging | `pFDFD` latch (RAM page ext bits 2-0, TR-DOS bit 3) + **address-bus latches** `aFE` (write `#xxFE`), `aFB` (read A2=0 group) | window regs `#xxFF7` + `#FF77` | same + `x7F7` 4M manager, `xxBF`/`xxBE` |
| RAM at `#0000` | `aFE.7 = 0` (window1 = RAM page **4**) | — (windows instead) | EFF7_ROCACHE |
| ROM at `#0000` | CPSYS (`aFB.7`) / TR-DOS / 7FFD.4 | `~CPM` (aFF77.9) | DOSEN/SHADOWEN (`pBF`) |
| Video mode select | **`aFE` bits 6-5** (address bus) | `pFF77` bits 2-0 | same + EFF7 z-bits |
| Video modes | 0=EGA 320×200, 1=640×200 MC, 3=ZX (2 unused) | + 4/6/7 text | + EVO text, ALCO, HWMC |
| Palette `#FF`-group | **none** (gate is 710/ATM3 only, `io.cpp:231`) | 16-cell palette RAM | same + 4:4:4 |
| Turbo | none in software (hardware switch; ZXMAK2 "[turbo]" = separate machine) | FF77.3 → 7 MHz | FF77.3/EFF7.4 → 14/7 MHz + DRAM waits |
| FE read bit 7 | `atm450_z` PAL flag w/ zero windows | IDE status (`io.cpp:1007`) | same as 710 |
| Extra devices | — | IDE (config), Beta128 | gluk CMOS, SD, PS/2, board NMI, font RAM |
| CPM memswap | `aFE.6` → `atm_memswap` (ini-gated, off) | FF77.0 → same | — |

## 3. Behavior → reference → our code

### 3.1 unreal-speccy (`emulators/github/unreal-speccy/`) — primary reference

| Behavior | Reference | Our target |
|---|---|---|
| Model enum + names | `emul.h:58-68`, `vars.cpp:206-218` | `platform.h:318`, `config.h:60` (done) |
| `pFDFD` write decode `(port & 0x8202) == 0x8000` | `io.cpp:577-582` | `PortDecoder_ATM450::DecodePortOut` |
| Bank calculation incl. RAM-at-0 and ROM priority | `memory.cpp:134-162` (ATM450 branch of `set_banks`) | `PortDecoder_ATM450::updateMemoryBanks()` override |
| `aFE` latch on `#xxFE` writes + bit 6/7 side effects | `io.cpp:446-469`, `atm.cpp:286-292` (`set_atm_aFE`) | `DecodePortOut` FE arm + `emulatorState.aFE` |
| Bright border from A3 (ATM family) | `io.cpp:461-463` | family `atmBorderBright` path |
| `aFB` latch on A2=0 reads (wide decode, "for MODPLAYi"; strict `#xx7B` commented out) | `io.cpp:1040-1055` | `DecodePortIn` arm |
| FE read bit 7 `atm450_z` (zero windows at 7200/7284/7326 ±40T; turbo branches dead) | `io.cpp:1001-1004`, `atm.cpp:315-324` | `DecodePortIn` FE arm |
| Memswap bookkeeping (physical permutation ini-gated OFF) | `atm.cpp:9-19` | `atmMemSwapped` vestigial flag (`platform.h:1144`), same as 710 |
| Video modes from aFE bits, renderers `rend_atm_1` | `dxr_atm.cpp:147-215` | `Screen::DetectModeATM1` `screen.cpp:336-357` (**done, green**) |
| ROM page order `sys=0,dos=1,128=2,sos=3` | `config.cpp:898-904` | `rom.cpp:242-248` (**done**) |
| `[ROM] ATM1=` ini key | `config.cpp:327`, `:976` | `config.cpp:280`, `rom.cpp:118` (**done**) |
| Boot ROM modes (RM_128/SOS/SYS/DOS; 450 keeps 7FFD.4 in RM_DOS) | `memory.cpp:375-400` | `ApplyBootROMDefaults` override |
| CF_SETDOSROM `#3Dxx` step trap | `z80_main.inl:113-122` | do **not** port (our DOS-entry path covers it) |
| xx77/xFF7/palette group gate = 710/ATM3 only | `io.cpp:231` | subclass must disable the inherited 710 arms |
| ATM IDE status read `(port&0x8202)==(0x7FFD&0x8202)`, 710-or-IDE_ATM | `io.cpp:1007-1012` | `idecontroller.cpp:149-150` fitment (**done**, config-driven) |
| Turbo: commented out for 710 (`z80_main.inl:156-162`), none for 450 | — | non-goal R8 |

### 3.2 ZXMAK2 (`emulators/github/ZXMAK2/src/ZXMAK2.Hardware/Atm/`) — secondary

| Behavior | Reference | Notes |
|---|---|---|
| Geometry: 8 ROM pages / 32 RAM pages ("ATM450 512K") | `MemoryAtm450.cs:24-28` | our shipped `atm1.rom` uses 4 pages; RAM 32 pages = 512K matches `config.h:60` |
| FE read PAL flag phased `Tact % FrameTactCount` | `MemoryAtm450.cs:261`, `Atm450_z` `:352` | matches unreal `atm450_z` modulo phasing |
| Paging ports (`#FE` AFE/AFB, `#xx7B` CPSYS, `#FDFD`, `#7DFD` palette claim) | `MemoryAtm450.cs:39-48` | **OQ-2**: the `#7DFD` palette contradicts unreal's 710/ATM3-only gate — resolve before implementing palette (default: no palette) |
| Mode enum Ega320x200=0, Hwm640x200=2, Std256x192=3, Txt080x025=6 | `UlaAtm450.cs:270-281` | renderer-interface numbering; the *selection* source is the CM/aFE latch — do not copy the numbers as decode values |
| Turbo as a separate machine doubling frameTactCount | `UlaAtm450.cs:283-300` (`UlaAtmTurbo`) | supports the R8 non-goal / possible later ini variant |
| FDD decode | `FddAtm450.cs` | **OQ-4**: diff vs the 710 FDD gate |

### 3.3 Xpeccy / xpeccy-plus

Not a reference for this machine: `src/libxpeccy/hardware/` contains `atm2.c`
(710), `pentevo.c`, `tslab.c` — **no atm1**; `HW_ATM1` is an unused enum
(`hardware.h:20`, `filer.cpp:141`). Useful only for the 710 arms the subclass
inherits.

### 3.4 Hardware docs

`emulators/github/pentevo/docs/atm_proj.txt:63-76` documents the **ATM2** 1M
manager (16K windows, two maps switched by 7FFD D4) — that is 710-family, not
450. No repo in the tree documents the 4.50 board beyond the emulator code
itself; the emulator behavior table above is the specification.

## 4. Our prewire inventory (verified 2026-10-01)

Already present — nothing below needs writing:

| Piece | Where |
|---|---|
| `MM_ATM450` enum | `core/src/emulator/platform.h:318` |
| Model-table row (512K default, RAM_512\|RAM_1024) | `core/src/emulator/config.h:60` |
| Timing + canonical frame geometry grouped with 710/ATM3 | `core/src/emulator/config.cpp:1218-1228`, `:1286-1293` |
| `[ROM] ATM1` ini key → `atm1_rom_path` | `core/src/emulator/config.cpp:280` |
| ROM page order sys/dos/128/sos | `core/src/emulator/memory/rom.cpp:242-248` |
| ROM image 4×16K | `data/rom/atm1.rom` (65536 B, shipped) |
| `aFE`, `aFB`, `pFDFD`, `atmMemSwapped` state fields | `platform.h:1120`, `:1141-1144` |
| Video-mode detection from aFE bits | `core/src/emulator/video/screen.cpp:254-255`, `:336-357` |
| aFE mode-matrix test (green) | `core/tests/emulator/video/atm_video_modes_suite_test.cpp:167-195` |
| IDE_ATM fitment for MM_ATM450 | `core/src/emulator/io/ide/idecontroller.cpp:149-150` |
| TTD paging blob covers aFE/aFB | `core/src/debugger/ttd/atm/ttdatmpaging.cpp:30-31`, `:80-81` |
| DeviceState / WebAPI ATM branches include MM_ATM450 | `core/src/emulator/state/devicestate.cpp:1477`, `core/automation/webapi/src/api/state_memory_api.cpp:370` |
| "No factory case" guard (to flip) | `core/tests/emulator/emulatormanager_test.cpp:574-576` |

To write (the whole gap):

1. `core/src/emulator/ports/models/portdecoder_atm450.{h,cpp}` — inherits
   `PortDecoder_ATM710` (pattern: `portdecoder_atm3.h`); CMake globs pick it up.
2. Factory + `IsModelSupported` + port-trace model name + `getPortMapEntries`:
   `core/src/emulator/ports/portdecoder.cpp` (`:111`, `:166`, `:571` area).
3. `data/configs/atm450/unreal.ini`.
4. Tests per [tdd-plan.md](tdd-plan.md); UI entry
   `unreal-qt/src/menumanager.cpp` (`supportedModels` set, ~`:649`);
   docs `.recipe/_common/machines.md`, `AGENTS.md`.
