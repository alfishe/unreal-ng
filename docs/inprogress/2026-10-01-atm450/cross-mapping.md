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
| Paging | `pFDFD` latch (RAM page ext bits 2-0, CPNET bit 3) + **address-bus latches of the low byte** `aFE` (write `#xxFE`), `aFB` (unclaimed read, A2=0) | window regs `#xxFF7` + `#FF77` | same + `x7F7` 4M manager, `xxBF`/`xxBE` |
| RAM at `#0000` | `aFE.7 = 0` (window1 = RAM page **4**) | — (windows instead) | EFF7_ROCACHE |
| ROM at `#0000` | CPSYS (`aFB.7`) / TR-DOS / 7FFD.4 | `~CPM` (aFF77.9) | DOSEN/SHADOWEN (`pBF`) |
| Video mode select | **`aFE` bits 6-5** = A6-A5 of the `#FE` write | `pFF77` bits 2-0 | same + EFF7 z-bits |
| Video modes | 0=EGA 320×200, 1=640×200 MC, 3=ZX (2 unused) | + 4/6/7 text | + EVO text, ALCO, HWMC |
| Palette | **`#7DFD` group** (`port & 0x8202 == 0`), `--grbGRB` data layout, no gate | `#xx9F/BF/DF/FF` group, `grbG--RB`, DOS + pen2 gate | `#FF`, same + 4:4:4 |
| Turbo | none in software (hardware switch; ZXMAK2 "[turbo]" = separate machine) | FF77.3 → 7 MHz | FF77.3/EFF7.4 → 14/7 MHz + DRAM waits |
| FE read bit 7 | `atm450_z` PAL flag w/ zero windows | tape/keyboard as on 128K (the ATM IDE status is a separate `#7FFD`-group read, `io.cpp:1007`) | same as 710 |
| Extra devices | — | IDE (config), Beta128 | gluk CMOS, SD, PS/2, board NMI, font RAM |
| CPM memswap | `aFE.6` → `atm_memswap` (ini-gated, off) | FF77.0 → same | — |

## 3. Behavior → reference → our code

### 3.1 unreal-speccy (`emulators/github/unreal-speccy/`) — primary reference

| Behavior | Reference | Our target |
|---|---|---|
| Model enum + names | `emul.h:58-68`, `vars.cpp:206-218` | `platform.h:318`, `config.h:60` (done) |
| `pFDFD` write decode `(port & 0x8202) == 0x8000` | `io.cpp:577-582` | `PortDecoder_ATM450::DecodePortOut` |
| Palette write on the `#7DFD` group, `(port & 0x8202) == 0` | `io.cpp:533-537`, `atm.cpp:296-307`; ATM1 bit table `draw.cpp:440-447` | `PortDecoder_ATM450` palette arm |
| Bank calculation incl. RAM-at-0 and ROM priority | `memory.cpp:134-162` (ATM450 branch of `set_banks`) | `PortDecoder_ATM450::updateMemoryBanks()` override |
| `aFE` latch = **low** address byte of `#xxFE` writes (`set_atm_aFE((unsigned char)port)`) + bit 6/7 side effects | `io.cpp:446-469`, `atm.cpp:286-292` (`set_atm_aFE`) | `DecodePortOut` FE arm + `emulatorState.aFE` |
| Bright border from A3 (ATM family) | `io.cpp:461-463` | family `atmBorderBright` path |
| `aFB` latch = **low** address byte of an A2=0 read that no earlier arm claimed (wide decode, "for MODPLAYi"; strict `#xx7B` commented out); returns `#FF` | `io.cpp:1040-1055` (GS `#B3/#BB` claimed earlier, `io.cpp:724`) | `DecodePortIn` arm |
| FE read bit 7 `atm450_z` (zero windows at 7200/7284/7326 ±40T; turbo branches dead) | `io.cpp:1001-1004`, `atm.cpp:315-324` | `DecodePortIn` FE arm |
| Memswap on an A6 change (physical permutation ini-gated OFF) | `atm.cpp:9-19`, `:290` | not ported: the board wires A6 to RG0 only (manual, D50); `atmMemSwapped` stays false |
| Video modes from aFE bits, renderers `rend_atm_1` | `dxr_atm.cpp:147-215` | `Screen::DetectModeATM1` `screen.cpp:336-357` (**done, green**) |
| ROM page order `sys=0,dos=1,128=2,sos=3` | `config.cpp:898-904` | `rom.cpp:237-241` (**done**, verified against `atm1.rom` contents) |
| `[ROM] ATM1=` ini key | `config.cpp:327`, `:976` | `config.cpp:280`, `rom.cpp:118` (**done**) |
| Reset defaults: RM_DOS → aFE=#E0, aFB=0; other modes → aFE=#80, aFB=#80 (sys ROM) | `z80.cpp:123-133` | `ApplyBootROMDefaults` override |
| Boot ROM modes (RM_128/SOS/SYS/DOS; 450 keeps 7FFD.4 in RM_DOS) | `memory.cpp:375-400` | `Memory::SetROMMode` (already generic) |
| CF_SETDOSROM `#3Dxx` step trap | `z80_main.inl:113-122` | do **not** port (our DOS-entry path covers it) |
| xx77/xFF7 group = 710/ATM3 only | `io.cpp:231` | the 450 decoder has its own arm list (no 710 arms) |
| ATM IDE status read `(port&0x8202)==(0x7FFD&0x8202)`, 710-or-IDE_ATM | `io.cpp:1007-1012` | `idecontroller.cpp:149-150` fitment (**done**, config-driven) |
| Turbo: commented out for 710 (`z80_main.inl:156-162`), none for 450 | — | non-goal R8 |

### 3.2 ZXMAK2 (`emulators/github/ZXMAK2/src/ZXMAK2.Hardware/Atm/`) — secondary

| Behavior | Reference | Notes |
|---|---|---|
| Geometry: 8 ROM pages / 32 RAM pages ("ATM450 512K") | `MemoryAtm450.cs:24-28` | our shipped `atm1.rom` uses 4 pages; RAM 32 pages = 512K matches `config.h:60` |
| FE read PAL flag phased `Tact % FrameTactCount` | `MemoryAtm450.cs:261`, `Atm450_z` `:352` | matches unreal `atm450_z` modulo phasing |
| Paging ports (`#FE` AFE, A2=0 read AFB, `#7FFD`, `#FDFD`, `#7DFD` palette) | `MemoryAtm450.cs:39-48` | Same port set as UnrealSpeccy (palette included); latches truncate to the low byte (`(byte)addr`) despite the "High address byte" attribute text |
| Mode enum Ega320x200=0, Hwm640x200=2, Std256x192=3, Txt080x025=6 | `UlaAtm450.cs:270-281` | renderer-interface numbering; the *selection* source is the CM/aFE latch — do not copy the numbers as decode values |
| Turbo as a separate machine doubling frameTactCount | `UlaAtm450.cs:283-300` (`UlaAtmTurbo`) | supports the R8 non-goal / possible later ini variant |
| FDD decode | `FddAtm450.cs`, gate `General/FddController.cs:205` | Same partial decode; gate DOSEN \|\| SYSEN (see §3.3) |
| Palette data layout `--grbGRB` | `UlaAtm450.cs` `InitStaticTables` | Identical to UnrealSpeccy `draw.cpp:440-447` |
| Reset: `AFE \|= 0x80`, `AFB \|= 0x80` | `MemoryAtm450.cs` `BusReset` | Same as UnrealSpeccy's non-DOS reset |

### 3.3 Where the references disagree (we follow UnrealSpeccy)

| Point | UnrealSpeccy | ZXMAK2 | Effect at the default config | Open item |
|---|---|---|---|---|
| `pFDFD` bit 2 | RAM page bit 5 (masked by RAM size) | ROM A16 (`romPage \|= CMR1 & 4`), RAM uses bits 1-0 | None at 512 KiB with a 64 KiB ROM | OQ-5 — **schematic: ROM A16, ZXMAK2 shipped** |
| `aFB` latch | Fall-through read arm, returns `#FF`, earlier arms (GS, AY, DOS ports) win | Side effect on every A2=0 read, other devices still answer | Differs only for device ports with A2=0 (GS `#BB`/`#B3`) | OQ-6 |
| FDC port gate | TR-DOS session (`CF_DOSPORTS`) | `DOSEN \|\| SYSEN` (sys ROM mapped opens it) | Differs while the sys ROM runs outside a TR-DOS session | OQ-7 |
| Turbo | none | separate `[turbo]` machine | none (R8 non-goal) | — |

The board manual (§3.5) settled OQ-5 (ROM A16, shipped) and identified the
A2=0 read as the printer-port read (OQ-6); OQ-7 and the PAL marker timing
stay with the UnrealSpeccy behavior.

### 3.4 Xpeccy / xpeccy-plus

Not a reference for this machine: `src/libxpeccy/hardware/` contains `atm2.c`
(710), `pentevo.c`, `tslab.c` — **no atm1**; `HW_ATM1` is an unused enum
(`hardware.h:20`, `filer.cpp:141`). Useful only for the 710 arms the subclass
inherits.

### 3.5 Hardware docs

`emulators/github/pentevo/docs/atm_proj.txt:63-76` documents the **ATM2** 1M
manager (16K windows, two maps switched by 7FFD D4) — that is 710-family, not
450.

The 4.50 board itself is documented in the MicroArt manual "Многофункциональный
компьютер ATM Turbo — Инструкция по наладке. Описание компьютера" (~1992):
[index](https://zxpress.ru/book.php?id=170),
[appendix 2 port table](https://zxpress.ru/ru/books/chapter/2356),
schematic sheets [Ver 4.50](https://zxpress.ru/chapters_images/atmturbo-6.jpg)
and [ATM-TURBO 512+](https://zxpress.ru/chapters_images/atmturbo-7.jpg).
What it confirms:

| Point | Manual | Agrees with |
|---|---|---|
| `#FE` write latches A7 (CPUS), A6 (RG0), A5 (RG1), A3 (border intensity) | appendix 2; register D50 | both emulators (low byte); A6 is RG0, not memswap |
| Modes RG0/RG1: 1/1 ZX, 0/1 640x200, 0/0 320x200x16, 1/0 forbidden | appendix 2 | `DetectModeATM1` |
| Memory maps SINC 128 / CP/M SYSTEM (ROM SYS) / CP/M USER (pages 0, 4, 2, n) | appendix 2 | both emulators |
| A2=0 read = printer port: A7 → CPSYS, D7 BUSY, D6 ULINE; CPSYS set at reset, cleared by the 48K lock (Z48) | appendix 2; flip-flop D69.1 | UnrealSpeccy arm order |
| `#FDFD`: D0/D1 RAM pages, D2 ROM A16, D3 CPNET | schematic latch D3, body text | ZXMAK2 (UnrealSpeccy differs on D2) |
| `#7DFD` palette, 6 bits | appendix 2 | both emulators |
| `#FE` read bit 7 = Z from the protected PLM | schematic D45 | timing undocumented |
| Turbo = hardware switch latched on `OUT (#FE)` | chapter "TURBO" | non-goal R8 |
| RAM maximum 512 KiB | assembly notes (565РУ7) | ZXMAK2 "ATM450 512K" |

## 4. Our prewire inventory (verified 2026-10-01)

Already present — nothing below needs writing:

| Piece | Where |
|---|---|
| `MM_ATM450` enum | `core/src/emulator/platform.h:318` |
| Model-table row (512K default, RAM_512\|RAM_1024) | `core/src/emulator/config.h:60` |
| Timing + canonical frame geometry grouped with 710/ATM3 | `core/src/emulator/config.cpp:1218-1228`, `:1286-1293` |
| `[ROM] ATM1` ini key → `atm1_rom_path` | `core/src/emulator/config.cpp:280` |
| ROM page order sys/dos/128/sos (verified against the image) | `core/src/emulator/memory/rom.cpp:237-241` |
| ROM image 4×16K | `data/rom/atm1.rom` (65536 B, shipped) |
| `aFE`, `aFB`, `pFDFD`, `atmMemSwapped` state fields | `platform.h:1120`, `:1141-1144` |
| Video-mode detection from aFE bits | `core/src/emulator/video/screen.cpp:254-255`, `:336-357` |
| aFE mode-matrix test (green) | `core/tests/emulator/video/atm_video_modes_suite_test.cpp:167-195` |
| IDE_ATM fitment for MM_ATM450 | `core/src/emulator/io/ide/idecontroller.cpp:149-150` |
| TTD paging blob covers aFE/aFB (**not pFDFD** — to add) | `core/src/debugger/ttd/atm/ttdatmpaging.cpp:30-31`, `:80-81` |
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
