# GMX: technical design

**Date:** 2026-10-07 - part of [README.md](README.md) - requirements in [requirements.md](requirements.md) - evidence in
[research-reference-consensus.md](research-reference-consensus.md)

## 0. Name

GMX is the **Graphic Memory eXpander** (Scorpion Ltd / MOA, 1998); the board has no MIDI part.
The model table says "ZS Scorpion + GMX"; this folder keeps that name.

## 1. Today

| Piece | State on master |
|:--|:--|
| `MM_GMX` | `platform.h`; model row `{"ZS Scorpion + GMX", "GMX", MM_GMX, 2048, RAM_2048}` in `config.h` |
| ROM | `config.gmx_rom_path` read from `[ROM] GMX=`; `rom.cpp` has `case MM_GMX: assert("Not implemented")` (twice: file name and bank layout); the loader checks only that the image is 32 pages (512 KB). `data/rom/gmx.rom` is tracked ([roms.md](roms.md)) |
| Ports | `ports.h` still declares `_p7EFD, _p78FD, _p7AFD, _p7CFD, _gmx_config, _gmx_magic_shift`; `EmulatorState` has `scorpion.p7EFD` only (the GMX group was removed by the model-state refactor) |
| Port decoder | none: `PortDecoder::IsModelSupported(MM_GMX)` is false, the factory has no case; `emulatormanager_test.cpp` asserts exactly that |
| Config folder | none (`data/configs/gmx` missing) so `Config::IsModelCreatable` is false |
| Screen | `Screen::DetectVideoMode` calls `DetectModeGMX` (7EFD bit 3 -> `M_GMX`, `R_320_200`); `M_GMX` geometry row is a copy of the 256x192 one; `DrawGMX` is an empty stub. The `R_320_200` raster is wrong for GMX (640x200, section 5) |
| TTD | `ttd.ksy` mentions "the GMX p7xFD group" as moved out of the standard chipset blob; no GMX blob exists |
| Snapshots | `snapshotcapture.cpp` default branch: "capture_unsupported ... GMX ... cannot be created yet"; `machinestatetransfer.cpp` `FamilyOf` returns `Other` |
| Automation / Qt | the name appears in the MCP description, `.recipe/_common/machines.md` ("no factory port decoder -> HTTP 400") and `list_models` (`creatable: false`); no Qt menu entry (`supportedModels` in `menumanager.cpp`) |

## 2. Hardware facts

Confidence tags: **H** two or more independent sources agree; **M** one source; **O** open (see [TODO.md](TODO.md)).
Detail and the per-source table: [research-reference-consensus.md](research-reference-consensus.md).

### 2.1 What the board is

A plug-in card that sits in the Z80 socket of a Scorpion ZS-256 (yellow board, Turbo, or green Turbo+). It carries up to
2 MB of RAM on SIMMs, a 512 KB flash (Intel 28F400 per MAME's port comments), an Altera FLEX FPGA and the Z80. The FPGA
loads one of up to seven "file-schemes" from the flash; the schemes make the card behave as Scorpion (scheme 2, the base
one), Pentagon 128 (4), Composit (5) or run the board test (6) (H, two articles). The host board keeps the FDC, keyboard
port, Kempston port, printer port, AY and tape/beeper; its RAM and the RAM multiplexers go unused. The unreal-ng model
is the **Scorpion scheme** only. Pentagon scheme = the existing Pentagon machine; Composit and the board test are out of
scope (Q8).

### 2.2 Ports

All ports are decoded with the Scorpion rule `A5 = 1, A1 = 0, A0 = 1` plus the high byte (H, Black_Cat table and
Unreal); the high byte is matched **exactly** in the GMX register file (H, Unreal and MAME). Writes unless noted.

| Port | Dir | Effect |
|:--|:--|:--|
| `#xx00` | W | Global configuration, any high byte, **always** reachable (even when the register file is blocked). Bit 3 arms the "magic" readout (below) and, with bit 4 = 0, resets the CPU (RAM and latches stay). Bit 4 "fixrom": freezes the ROM plane (`#7EFD` bits 4-6 ignored) and suppresses that reset. Bit 5 "BLKEXT": disables `#78FD #7AFD #7CFD #7EFD #DFFD`. Bit 7 read back at `#7EFD` bit 4. Bits 0-2 are the low bits of the magic byte. Other bits: stored (H: Unreal, MAME, pico-speccy) |
| `#7FFD` | W | As Scorpion: bits 0-2 RAM page bits 0-2, bit 3 screen 5 / 7, bit 4 ROM A14, bit 5 lock. The lock is lifted when `#7EFD` bit 2 = 1 in Unreal; **MAME: bit 2 means "Magic disabled"** (Q3) |
| `#1FFD` | W | Scorpion: bit 0 RAM page 0 at `#0000`, bit 1 service ROM, bit 4 RAM page bit 3. **GMX: bit 2 "hard DOS page"**: the plane's page 3 (TR-DOS) at `#0000` with the Beta ports on, overriding bit 0 (MAME, pico-speccy; Unreal marks it as TR-DOS flag). Bits 6-7 (ZS-1024 page bits) are not used by the GMX RAM formula (H) |
| `#DFFD` | W | Bits 0-2: RAM page bits 4-6 (Profi-style, `(DFFD & 7) << 4`) (H: Unreal reads it back, MAME and pico-speccy use it in the formula) |
| `#78FD` | W | Bits 0-6: the page at `#8000-#BFFF`, stored **XOR 2**: value 0 = page 2 (H: MAME, pico-speccy; the article: "any bank can be connected to the second page") |
| `#78FD` | R | Bit 7 = `#FE` bit 1 (BRD1); bits 0-6 = the stored `#78FD` value; bit 0 is OR-ed with the magic shift bit, and every read shifts the magic byte right once (H: Unreal, MAME, pico-speccy) |
| `#7AFD` | W | Hardware scroller, low byte of the offset. MAME and pico-speccy keep `data & #F0`, Unreal keeps all 8 bits, the articles say "8 bits" (Q4) |
| `#7AFD` | R | Paging status: bits 0-2 = `#7FFD` bits 0-2, bit 3 = `#1FFD` bit 4, bits 4-6 = `#DFFD` bits 0-2, bit 7 = `#FE` bit 0 (BRD0) (H, three sources identical) |
| `#7CFD` | W | Scroller high byte, 6 bits (`data & #3F`) (H) |
| `#7EFD` | W | Bit 7 turbo (7 MHz); bits 6-4 ROM plane = flash A18-A16 (ignored when `#00` bit 4); bit 3 extended 640x200 mode; bit 2 see Q3; bit 1 Vpp and bit 0 EWR of the flash (ignored, Q9) (H except bit 2) |
| `#7EFD` | R | Bit 0 = `#7FFD` bit 5; bit 1 = `#7FFD` bit 3; bit 2 = turbo (write bit 7); bit 3 = write bit 3; bit 4 = `#00` bit 7; bit 5 = `#00` bit 5; bit 6 = `#1FFD` bit 0; bit 7 = `#FE` bit 2 (BRD2) (H, three sources identical) |
| `#FE`, `#FF`, `#FFFD`, `#BFFD`, FDC `#1F-#FF`, Kempston, mouse, SMUC, ... | | as the Scorpion decoder (host board) |
| Turbo+ strobes | R | `IN` from the `#1FFD` family clears turbo, from the `#7FFD` family sets it (Scorpion Turbo+; MAME's GMX class inherits it, Unreal and pico-speccy do not model it for GMX) (Q5) |

The "magic" readout: a write to `#00` with bit 3 stores `#88 | (value & 7)`; each read of `#78FD` returns the low bit in
bit 0 and shifts right, so eight reads return, least significant bit first: the three bits `v & 7`, then 1, then three 0,
then 1 (after eight reads the byte is 0 and reads add nothing). No source says what software does with it, Q6. The registers read back in the shadow monitor are frozen at the Magic button
(MAME `m_port_7afd_lock_data`, `m_port_7efd_lock_data`) and released by the next `IN #FF` (MAME only, Q7).

### 2.3 RAM

| Window | Page |
|:--|:--|
| `#0000-#3FFF` | ROM (2.4) or RAM page 0 (`#1FFD` bit 0 and not Magic pending) |
| `#4000-#7FFF` | RAM page 5 |
| `#8000-#BFFF` | RAM page `(#78FD & #7F) ^ 2` (H) |
| `#C000-#FFFF` | RAM page `((DFFD & 7) << 4) \| ((1FFD & #10) >> 1) \| (7FFD & 7)` (H) |

Pages are masked to the installed size: 2048 KB = 128 pages of 16 KB; MAME and pico-speccy wrap with `% pages`
(pico: out of range -> page 2 / low three bits). The article lists SIMM options 256, 512, 1024, 1280 and 2048 KB; the model
table says `RAM_2048` only. The default and only supported size stays 2048 KB (Q10). The normal screen is page 5, or 7
with `#7FFD` bit 3 (as Scorpion).

### 2.4 ROM

512 KB = 8 planes of 64 KB (H: MAME `(prof_plane << 2) | page`, pico-speccy `romInUse = (plane << 2) | bank`). Inside a
plane the four 16 KB pages are, in the Scorpion order, 0 = 128 editor, 1 = 48 BASIC, 2 = service monitor, 3 = TR-DOS.
Rule at reset and in operation (MAME `scorpion_update_memory`, H with pico-speccy):

```
if 1FFD.2:                     page 3 of the plane, Beta ports on     (GMX only; wins over bit 0)
elif 1FFD.0 and no Magic:      RAM page 0
elif 1FFD.1:                   page 2
else:                          page = (dos << 1) | 7FFD.4
```

The plane is `#7EFD` bits 6-4 and nothing else (pico-speccy hardware trace; MAME also lets reads of `#0100-#010C`
change the two low plane bits as on the Turbo+ ProfROM add-on, which pico-speccy found to crash the real GMX firmware,
Q2). The plane at power-on is 0, which is the **GMX loader**, not an editor. The tracked 512 KB image (CRC32
`00DF8568`, identical to MAME's "GMX Boot Rom 1.2 V.5.00") has, from our own page scan ([roms.md](roms.md)): plane 0 = loader,
test and FPGA scheme data; plane 1 = Pentagon scheme ROMs (128, 48 BASIC); plane 4 = ProfROM-class Scorpion ROMs;
plane 5 = shadow monitor. Which plane the real Scorpion scheme enters after the loader is firmware behavior, not a
register: the loader pokes `#7EFD` itself.

### 2.5 Video

Standard mode is the Scorpion raster unchanged. With `#7EFD` bit 3 the **extended mode** is on: 640x200, 16 colors,
one attribute byte per 8 horizontal pixels, in extended RAM (H: article, MAME, Unreal, pico-speccy):

| Item | Value | Source |
|:--|:--|:--|
| Pixel bytes | page `#39` (`#7FFD` bit 3 = 0) or `#3B` (= 1), 80 bytes per line, 200 lines = 16000 bytes | MAME, Unreal, pico-speccy |
| Attribute bytes | the same offset in page `#79` / `#7B` (page + 64) | same three |
| Attribute | bits 0-2 ink, 3-5 paper, bit 6 bright (both), bit 7 flash (inverts the pixels on the flash phase) | MAME, Unreal |
| Buffer pages | the article says `#3A / #7A`; the code of all three emulators says `#3B / #7B` | Q11 |
| Scroll | 14-bit offset from `#7CFD:#7AFD`. MAME and pico-speccy: `offset / 80` = first source line, mod 200 (a line scroller). Unreal: byte offset, wrapping at 16000 (shifts columns too). The article: "hardware line scroller" | Q4 |
| Palette | the fixed 16 ZX colors; no palette port (pico-speccy) | M |
| Geometry | the dot clock doubles: 640 pixels in the time of the 256-pixel line; the border is a few pixels wide on the sides (MAME 24 / 40) | MAME; Q12 |
| Timing | the frame, line and INT are the Scorpion's; the INT may be switched to Pentagon's from the shadow monitor (article) | M, Q13 |

The article states that a second mode of 320x200 with one color per pixel was announced and **never realized**; no emulator
has it. Not built.

### 2.6 Turbo

`#7EFD` bit 7 selects 7 MHz and is read back in bit 2 (H). The Scorpion Turbo+ wait states (`ScorpionTurboOverlay`) apply
unchanged (the board is a Scorpion Turbo+ with the card on it) (Q5). The article's "INT stretched to 132000 clocks in
turbo" is not understood and not used (Q13).

### 2.7 Devices

All of the host Scorpion: Beta128 FDC, AY (the Scorpion's, TurboSound option), Kempston joystick and mouse, covox, SMUC
(IDE, NVRAM, RTC; ProfROM and GMX firmware use it), PS/2 keyboard controller option. Not working on the real card: General
Sound (article) - the slot planner should say so (Q14).

## 3. How it fits unreal-ng

| Area | Change | File |
|:--|:--|:--|
| Factory | `case MM_GMX:` -> `new PortDecoder_Scorpion256(context)`; `IsModelSupported` adds it | `core/src/emulator/ports/portdecoder.cpp` |
| Decoder | `PortDecoder_Scorpion256` gets a GMX arm next to its `MM_PROFSCORP` arm: `IsPort_GMX(port, high)` for `#78FD #7AFD #7CFD #7EFD #DFFD`, port `#xx00`, the read-backs, the register-file block. The GMX latches live in a new `GmxState` (below). No subclass: the model shares the whole host board | `core/src/emulator/ports/models/portdecoder_scorpion256.{h,cpp}` |
| State | `platforms/gmx/gmxstate.h`: `GmxState` member `gmx` of `EmulatorState` (plain values: `p00, p78FD, p7AFD (scroll lo), p7CFD (scroll hi), pDFFD`, magic shift byte, `magicLock` and its two frozen bytes). `scorpion.p7EFD` stays where it is (GMX shares it), so `DetectModeGMX` keeps working. `pDFFD` already exists in `EmulatorState` (Profi) and is reused | `core/src/emulator/platforms/gmx/` |
| Memory | `ScorpionMemory::UpdateModelBanks` gets the GMX branch: window 2 from `78FD ^ 2`, window 3 from the 7-bit formula, plane from `#7EFD`, `1FFD.2` hard DOS page. `ScorpionRomWindow` with a 512 KB image already takes plane bits from `#7EFD`; for GMX the 0x0100 read strobe is **off** (Q2). RAM is 2048 KB (`MM_GMX`, `RAM_2048`) | `core/src/emulator/memory/scorpion/` |
| ROM | remove the two `assert("Not implemented")`; `GMX` returns `config.gmx_rom_path`; keep the 32-page check; `OnRomLoaded(32)` derives the plane mask (8 planes). Add `GMX_ROLES` in `ROM::GetROMPageRole` (per plane, Section 2.4) | `core/src/emulator/memory/rom.cpp` |
| Config | folder `gmx` (`data/configs/gmx/unreal.ini`: copy of `profscorp`, `HIMEM=GMX`, `RAMSize=2048`, `[ROM] GMX=rom\gmx.rom`, SMUC on, `ScorpionTurboLogic=SC15.1`); `Config::ApplyModelTimingDefaults` and `even_M1` get `MM_GMX` next to the Scorpion cases | `data/configs/gmx/`, `core/src/emulator/config.cpp` |
| Screen | `DetectModeGMX` returns a new raster `R_640_200` (replace `R_320_200`, which no source supports); `M_GMX` geometry row and `DrawGMX` filled (section 5); `DetectModeGMX` falls back to the Scorpion mode, not `_vid.mode` | `core/src/emulator/video/screen.{h,cpp}` |
| TTD | one peripheral blob `GmxPaging` (next free `PeripheralId`; Phoenix reserves 62 in its design, so number at implementation time), class `TTDGmxPaging` in `core/src/debugger/ttd/gmx/`. Fields: the `GmxState` bytes, version byte. `#DFFD` joins it (not in the standard chipset blob). Existing `ScorpionProfRom` blob keeps `p7EFD` and the plane | `core/src/debugger/ttd/` |
| Snapshot capture | `WindowMapCapture`: while `#78FD = 0`, `#DFFD = 0`, `1FFD.2 = 0` and no page above 15 is mapped the machine is a Scorpion 256 and the existing `scorpion256` view applies; else `capture_unsupported` "extended GMX paging in use". Remove GMX from the default-branch text | `core/src/loaders/snapshot/snapshotcapture.cpp` |
| State transfer | `FamilyOf(MM_GMX)` = `PagingFamily::Scorpion` (the paging family is the same, the extension is the `SameFamily` test): `Scorpion` source analysis gets `gmx.p78FD != 0 \|\| gmx.pDFFD != 0 \|\| pages above 15`; a Scorpion-256 source goes to a GMX target as `Mode128`. A GMX source with extended state goes only to a GMX target | `core/src/loaders/snapshot/machinestatetransfer.cpp` |
| Snapshot formats | `.sna`, `.z80`, `.szx` have no GMX id; GMX saves as plain 128K/Scorpion state when capture allows it. Unreal and no other emulator define a GMX snapshot | |
| Media and slots | Beta128 floppies and the SMUC IDE exactly as `SCORPION`; `machines.cpp` refdata row (host board bus as Scorpion); General Sound marked incompatible (Q14) | `core/src/emulator/slots/refdata/machines.cpp` |
| Automation | model lists in CLI (`cli-processor-state.cpp`, `cli-processor-instance.cpp`), WebAPI (`state_memory_api.cpp`, `ports_api.cpp`, OpenAPI files), MCP description and `unreal://memory-map`, Lua and Python headers; `state/paging` gets `gmx_*` fields | `core/automation/...` |
| Qt | add `MM_GMX` to `supportedModels`; Machine menu entry "ZS Scorpion + GMX (2048K)"; video window honors 640x200 | `unreal-qt/src/menumanager.cpp` |
| Docs | `.recipe/machines/gmx.md`, `.recipe/README.md`, `.recipe/_common/machines.md` row, `AGENTS.md` models list, `data/rom/README-ROMS.md` | |

No third-party library: the FPGA schemes are not emulated, the card's behavior is the register file above. If the Altera
loader's flash programming (`#7EFD` bits 0-1, jumpers X7 / X8) is ever wanted it becomes its own device (Q9).

## 4. The decoder

Add to `PortDecoder_Scorpion256`, guarded by `mem_model == MM_GMX` (cold path, like the PROFSCORP arm):

```text
DecodePortOut(port, value, pc):
    if (port & 0x00FF) == 0x00:             Out00(value)          // before everything, any high byte, even blocked
    elif gmx.p00 & 0x20:                    fall through to the Scorpion decode
    elif IsPortFD(port) && high byte is one of 78 7A 7C 7E DF:   // A5=1 A1=0 A0=1 + exact high byte
        78: gmx.p78FD = v & 7F, remap;  7A: gmx.scrollLo = v;  7C: gmx.scrollHi = v & 3F
        7E: Out7EFD(v);                     DF: state.pDFFD = v & 7, remap
    else Scorpion decode (7FFD lock lifted per Q3, 1FFD with bit 2)
DecodePortIn: same keys -> Read78FD / Read7AFD / Read7EFD (with the Magic freeze, Q7); everything else as Scorpion
```

- `Out7EFD`: turbo from bit 7 (`state.scorpion.turbo`, `SyncTurboWaits()`); plane from bits 6-4 when `p00` bit 4 is clear
  (`ScorpionRomWindow::OnWindowPortWrite` with the GMX mask, then remap); bit 3 -> `InitRaster` (as `DetectModeGMX`
  today, `Screen` re-detects on the next frame boundary); bit 2 per Q3.
- `Out00`: bit 3 -> magic byte and `Z80::reset` through the existing CPU-reset entry when bit 4 is 0 (RAM and ports
  stay, so it is not `Emulator::Reset`).
- `reset()`: every GMX latch 0, plane 0, `p00 = 0` (MAME `machine_start` also zeroes `p00`), turbo off.
- The decode constants are `static constexpr` masks next to each other, shared with the port-map rows
  (`GetPortMapInfo`, model name "ScorpionGMX") and the tests.
- `GetTTDModelStateIds()` adds `GmxPaging` to what the Scorpion returns.

## 5. Video

Naive first: the extended mode is a new `Screen` raster `R_640_200`, not a new renderer. `DrawGMX(n)` is called once per
2 T like the other modes and emits 4 pixels per T of the 640-wide line (or the host resamples two 320-wide halves; Q12).
Per line it reads 80 pixel bytes and 80 attribute bytes from the pages in 2.5 at `line * 80 + column` after the scroll
offset (policy of Q4). Flash phase from `frame_counter & 0x10` as the Spectrum. No contention, no floating bus (Scorpion).
The border band is the Scorpion's. `VideoModeEnum::M_GMX` keeps its name and `screenvideomodename` string. The Qt
screen widget and the screenshot/recording paths already size from `Screen`'s raster description, so they need the new
raster entry and nothing else (to be confirmed in phase 3).

## 6. TTD

TTD's standard chipset state carries `p7FFD` and `pFE`; `p1FFD`, `pDFFD` and the Scorpion `p7EFD` / plane live in model
blobs. The GMX adds the register file in one blob (`GmxPaging`, section 3). Record/replay needs no new input events
(every GMX port is a CPU access). The model fingerprint includes `RAMSize`. The magic-lock state is part of the blob
(it changes what `IN #7AFD` returns). Every phase starts with a TTD recording ([section 8](#8-verification-protocol-for-every-phase)).
`ttd.ksy`, `ttdfileinfo.cpp` ("gmx-paging") and a fixture in `core/tests/debugger/ttd/gmx/` are updated in phase 2.

## 7. Phases

Every phase ends with a full `tools/build/build.sh` (zero warnings), `tools/build/test.sh` green and, for C++ changes,
`docker/linux/build.sh --test`. Tests are written first ([tdd-plan.md](tdd-plan.md)). Parity columns: CLI / WebAPI +
OpenAPI / MCP / Lua / Python / Qt / recipe.

| Phase | Work | Parity delivered | Needs |
|:--|:--|:--|:--|
| 1 | **Creatable machine.** Factory + `IsModelSupported`; `GmxState`; `#00`, `#78FD`, `#7AFD`, `#7CFD`, `#7EFD`, `#DFFD`, read-backs; RAM formula; plane and `1FFD.2`; ROM loader and roles; `data/configs/gmx/`; timing defaults; port-map rows. Boots `gmx.rom` to the GMX loader and the shadow monitor. `emulatormanager_test` flips from "unsupported" to "creatable" | CLI `start GMX`, `list_models`; WebAPI `POST /emulator/start {"model":"GMX"}` + OpenAPI model enum and `GET /ports`; MCP `emulator_manage create` + resource text; Lua/Python `create` accept it; Qt Machine menu; recipe `.recipe/machines/gmx.md` sections 1-2 verified (create, ports, boot) | Q2, Q3 decided (recommendations in TODO.md) |
| 2 | **TTD and state.** `GmxPaging` blob, ksy, fixture, divergence hash; `FamilyOf`, `WindowMapCapture`, `MachineStateTransfer`; `state/paging` `gmx_*` fields | WebAPI/CLI/Lua/Python paging fields (`p78FD`, `pDFFD`, plane, gfx_ext, turbo, `p00`); MCP `inspect_state` and `time_travel`; TTD checkpoint/seek from every surface; recipe section 3 (paging, TTD) | 1 |
| 3 | **640x200 video.** `R_640_200`, `DrawGMX`, scroller, flash; screenshot/video recording sizes; Qt widget; test pattern from the board test scheme is not available, so a synthetic RAM pattern is the fixture | screenshot via WebAPI/MCP/CLI/Lua/Python returns 640x200; Qt shows it; recipe section 4 | 1; Q4, Q11, Q12 |
| 4 | **Turbo and details.** Turbo read-back, Turbo+ strobe decision (Q5), Magic freeze (Q7), BLKEXT, `#00` reset, INT select (Q13) | the same surfaces; recipe adds the device sections | 1; Q5-Q7, Q13 |
| 5 | **Slots and media.** Refdata row, General Sound incompatibility, SMUC with the GMX firmware's HDD menu (disk list), floppy flow | WebAPI `slots`/`media`; MCP; recipe `slots.md` row | 1; Q14 |
| 6 | **Evidence-driven corrections.** Anything a schematic, the Zonov manual, a flash dump or a real board settles (Q1-Q13); each is a one-line table change plus its test | none new | sources |

Phases 1-3 can start once the phase 1 questions have a recorded answer; the recommendations in [TODO.md](TODO.md) are
chosen so that they do not block.

## 8. Verification protocol for every phase

1. Build with `tools/build/build.sh`, tests with `tools/build/test.sh --gtest_filter='*Gmx*'` and then the full suite.
2. Create the machine through the surface under test (CLI, WebAPI, MCP, Lua, Python, Qt) with `model: GMX`.
3. **Start a TTD recording before running anything**; run the recipe steps; stop. `seek` back to the start and step
   forward: the state hash at the end equals the recorded one. A failure here is a blob bug and blocks the phase.
4. Run the phase's recipe section and tick it in `.recipe/machines/gmx.md` ("Verified: date, build").

## 9. Cost

Nothing on a shared hot path: the GMX arm is behind a model test in the cold port-decode path, `UpdateModelBanks` is
evaluated on a paging write, the screen mode switch is per frame. The Scorpion read-strobe (`ApplyScorpionReadCycle`)
is gated by a cached bool, and for GMX the bool is false, so the Scorpion fast path is not touched. No A/B benchmark; the
phase 1 diff of `memory.cpp`, `rom.cpp` and the decoder's hot functions is the check.
