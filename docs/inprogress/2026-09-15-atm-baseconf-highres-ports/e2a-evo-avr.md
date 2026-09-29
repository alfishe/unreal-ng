# Phase E2a — the ZX-Evo AVR behind the clock ports (`EvoAvr`)

| | |
|---|---|
| **Date** | 2026-09-28 |
| **Status** | Done. Plan: [implementation-plan.md](implementation-plan.md) phase E2a |
| **Gaps closed** | A-1 (ERS version indication), A-3 (registers A-D, modes), A-4 (NVRAM persistence), A-5 (EEPROM window) of [gap-analysis.md](gap-analysis.md) |
| **Deferred** | A-2, the PS/2 keyboard log → phase E2b. How to build it: [tdd-evo-control-and-avr.md](tdd-evo-control-and-avr.md) §6.1 |

## 1. The user-visible result

The EVO Reset Service header now reads what a stock board shows:

```
Baseconf: ZXEvo 4M 07.01.2026
AVR Boot: ZXEvoAVRBoot 25.05.2019 beta
```

Before, both lines said `NONE`: the clock cells were plain RAM, so writing the extension type to
cell `#F0` and reading it back returned the same byte, which is the ERS's own test for "no Evo AVR"
(`rom/mainmenu/src/call_cmos.a80` `GET_VERS_EVO`).

## 2. What changed

| Piece | Change | Evidence |
|---|---|---|
| `EvoAvr` (`core/src/emulator/memory/atm/evoavr.{h,cpp}`) | derives from `CMOS` (clock registers, fixed-time mode, address latch inherited; `ReadCMOS`/`WriteCMOS` made virtual). Register A = EEPROM page; B keeps only bit 2; C = `{EEPROM mode, 0, 0, UF, SD present, SD WP, Caps LED, tape-out}` with UF cleared on read, write bit 1 Caps LED / bit 7 EEPROM mode; D = `#80 \| modifiers`, read-only; `0E-EF` NVRAM; `F0-FF` EEPROM window at `A << 4` or the extension window | `avr/baseconf/trunk/src/rtc.c:338-535`, `version.c:13-50` |
| Extension window | any write to `F0-FF` selects the type and is never stored; type 0 = firmware tag, 1 = bootloader tag, 2 = PS/2 log (reads `0`, empty, until E2b), 3 = modes register at `F0` (48K raster), other types `#FF` | `rtc.c:516-528`, `version.c` |
| Version tags | exact 16-byte tags of the released images: `zxevo_fw.bin` offset `0xC670` = `5A 58 45 76 6F 20 34 4D 00 00 00 00 27 B4 41 47`; `zxevo_bl.hex` address `0x1FFF0` = `5A 58 45 76 6F 41 56 52 42 6F 6F 74 B9 26 C4 2B` | extracted from the pentevo binaries |
| NVRAM file | `[EVO] NvramFile=`: the 256 cells + the 4 KiB EEPROM (4352 bytes), read once at power-on (first reset), written when the machine is destroyed; empty = session only; a Z80 reset never touches the AVR | design D4 |
| `PortDecoder_ATM3` | owns an `EvoAvr` (`GetEvoAvr()`); `GetCMOS()` still returns the `CMOS` base | — |
| TTD | `AtmPagingState` 128 → 132 bytes: extension type, EEPROM page, flags (EEPROM mode, Caps LED, tape-out). NVRAM and EEPROM stay out, like the CMOS contents | — |

## 3. Tests

| Test | Pins |
|---|---|
| `EvoAvr_Test.VersionWindowServesTheReleasedTags` | both 16-byte tags, any `F0-FF` cell selects |
| `EvoAvr_Test.VersionDatesDecodeLikeTheErs` | `07.01.2026` and `25.05.2019 beta` |
| `EvoAvr_Test.ExtensionSelectNeverEchoes` | the ERS "no Evo AVR" check fails; PS/2 empty; modes register; unknown types `#FF` |
| `EvoAvr_Test.EepromWindowPagedByRegisterA` | EEPROM window, paging, erased `#FF`, back to the extension window |
| `EvoAvr_Test.RegistersBCDFollowTheAvrFirmware` | B, C (SD bits, Caps, EEPROM mode), D (modifiers, read-only) |
| `EvoAvr_Test.NvramCellsAreRam`, `.NvramFileRoundTrip` | NVRAM cells; file round trip; truncated / missing file keeps power-on contents |
| `EvoAvr_Test.MachineSavesNvramOnShutdown` | on the machine: ports reach the AVR, the file is written when the machine is destroyed |
| `ZXEvoErs_Test.ReadsBaseConfAndBootloaderVersionsFromTheAvr` | real ROM: the ERS reads `ZXEvo 4M` and `ZXEvoAVRBoot` through `#BEF7` while booting (port trace) |
| `TtdAtmPaging_*` (updated) | blob layout 132, AVR state round trip |

## 4. Not done here (tracked)

| Item | Where |
|---|---|
| PS/2 keyboard log, register D fed by real modifiers | E2b, design §6.1 |
| GUI default `NvramFile` (`<AppDataLocation>/zxevo-nvram.bin`) | E10 |
| `evo` automation endpoint (AVR state, NVRAM load/save) | E10 |
| UF driven by emulated time instead of host time | later refinement (design §6) |
