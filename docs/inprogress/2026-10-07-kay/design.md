# Kay-1024: technical design

**Date:** 2026-10-07 · part of [README.md](README.md) · evidence in [research-kay-reference-consensus.md](research-kay-reference-consensus.md)

Source ids S1 to S11 are those of the research file. Anything marked (Q n) is an open question in [TODO.md](TODO.md).

## 1. Today

- `MM_KAY` exists in `core/src/emulator/platform.h`. `config.h` has the row `{ "Nemo's KAY", "KAY", MM_KAY, 256, RAM_256 | RAM_1024 }`.
- `rom.cpp` already has a `MM_KAY` case for the image path (`config.kay_rom_path`) and the role pointers (128 = page 0,
  48 = 1, DOS = 2, SYS = 3).
- `PortDecoder::GetPortDecoderForModel` has no case: the factory throws, `IsModelSupported(MM_KAY)` is false, so the
  model is not creatable. `emulatormanager_test.cpp` pins that (`MM_KAY` in the "no decoder" list).
- `screen.cpp` has no case: Kay would fall to `DetectModeLegacy`.
- `snapshotcapture.cpp` answers `capture_unsupported` for Kay ("cannot be created yet").
- No config folder `data/configs/kay`, no recipe, no TTD blob, no automation entry.

## 2. The machine (what the board is)

Kay-1024/3SL/TURBO (Nemo, St. Petersburg, 1998; passport of 2000), a Spectrum 128K clone on the NemoBus.

| Part | Fact | Source |
|:--|:--|:--|
| CPU | Z80A, clock from a 14 MHz crystal: 3.5 MHz, or 7 MHz turbo | S1, S2 |
| RAM | eight 1M x 1 DRAM = 1024 KB (64 pages of 16 KB); a 256 KB board is the same with fewer chips (Unreal: 256 / 1024) | S3, S5 |
| ROM | one 27512, 64 KB, four roles | S1, S2, [roms.md](roms.md) |
| Video | discrete logic, 256x192 standard Spectrum bitmap, RGB and PAL / SECAM outputs, no new video mode on this board | S1, S2 |
| Sound | beeper; AY-3-8910 on the board (the 2010 redraw prints YM2149F) | S1, S2 |
| Ports built in | Kempston joystick, Sinclair joysticks through the keyboard lines, Centronics through the AY ports A / B plus #1FFD / #7FFD bits, IBM keyboard connector (an XT controller card) | S1 |
| Slots | three NemoBus slots (XS6, XS7, XS8); the FDC (BETA-TURBO), IDE (NemoIDE), XT keyboard, modem, General Sound are cards | S1, S8, S9 |
| Variants not in this design's first phases | Kay 128 / 256 (1994, 48 KB ROM, turbo per machine cycle), Kay 2006 NB (CPLD: multicolor, GigaScreen, 512x192), Kay 2010 SL4 (four slots, `/IODOS` mixed with `/DOS`) | S3, S9, S11 |

## 3. Ports

All decodes need IORQ with no card claiming IORQGE (DD46 enable E0 = IORQG). Every Kay port decode is partial (S2, S4).

| Port | Decode (A15 A14 ... A1 A0) | Dir | Meaning |
|:--|:--|:--|:--|
| #FE | `x x ... x 0` | W | D0-D2 border, D3 tape out, D4 speaker; D5-D7 = 0 |
| #FE | `x x ... x 0` | R | keyboard rows by A8-A15; D5 = 0, D6 = EAR, D7 = Centronics BUSY (pulled up: 1 with no printer) (Q6) |
| #7FFD | `0 1 ... 0 1` | W | D0-D2 RAM page at #C000, D3 screen, D4 ROM 48 / 128, D5 lock, D6 Centronics INIT, D7 RAM bit |
| #1FFD | `0 0 ... 0 1` | W | D0 RAM page 0 at #0000, D1 Centronics Q8, D2 turbo (0 = TURBO, 1 = NORM), D3 ROMS, D4 RAM bit, D5 STROBE, D6 O6, D7 RAM bit |
| #BFFD | `1 0 ... 0 1` | W | AY register data |
| #FFFD | `1 1 ... 0 1` | W / R | AY register select / AY register data |
| #1F (odd ports) | `x x ... x 1` | R | Kempston joystick, D5-D7 = 0; not while the DOS ports are on, not while reading #FFFD (the passport's "blocked by C1") |
| #EFFE, #E7FE | keyboard rows | R | Sinclair joystick 1 / 2 are the keys 6-0 / 1-5; nothing extra |
| #1F, #3F, #5F, #7F, #FF (DOS on) | `0BAxxx11`, system `1xxxxx11` | R / W | BETA-TURBO card: WD1793 and the system register (S4); today's Beta 128 in unreal-ng |

- #7FFD lock (D5 = 1) stops further #7FFD writes. #1FFD has no lock: the lock gates the #7FFD latch clock only (DD49.3 in S2).
- Reset clears both latches (DD40 and DD45 clear input). With #1FFD D2 = 0 the machine is in turbo after reset if the
  front-panel switch is on (S3: TURBO needs the switch, D2 = 0, the bus line not grounded, DOS = 1, IORQ = 1).
- Not decoded on the board: no #DFFD, no #EFF7, no #FF floating-bus port. `IN #FF` is a Kempston read (A0 = 1).
- Mouse: BC #4 lists the Kempston mouse for model 7 ("standard" decode; the 1xxxxxxx variant answers on odd addresses).
  Use the standard decode of the existing mouse support (Q15).
- Centronics: a printer is wired to AY port A (data) and B (status); the strobe / init / select lines come from the
  #1FFD / #7FFD bits. No printer is emulated first: AY port A writes go nowhere, port B reads `#FF`, BUSY reads 1.

## 4. Memory map and paging

```
#0000-#3FFF  ROM (role by ROMS, TR-DOS, #7FFD.4) or RAM page 0 when #1FFD.0 = 1
#4000-#7FFF  RAM page 5
#8000-#BFFF  RAM page 2
#C000-#FFFF  RAM page N, N = (7FFD & 7) | ((1FFD & 0x10) >> 1) | ((1FFD & 0x80) >> 3) | ((7FFD & 0x80) >> 2)
```

- N is masked by the RAM size: 256 KB keeps 16 pages (mask `0x0F`), 1024 KB keeps 64 (`0x3F`); `Memory` already has the
  mask helper (config.ramsize in KB).
- The page-bit order of the two top bits follows Unreal Speccy (Q3). Software cannot see the order, only a snapshot or
  a memory view can.
- Whether the two top bits move the #4000 / #8000 windows too (the schematic shows them entering the last stage of the DRAM
  address multiplexer, DD9) is not settled. Unreal gates them to #C000 only. The first phase does the same (Q3).
- The video always reads pages 5 and 7 of the first 128 KB (the multiplexer sends ground to the top bits during the
  video phases).
- ROM role index = `((1FFD.3) xor TR-DOS) * 2 + 7FFD.4`: 0 = 128, 1 = 48, 2 = SYS (Kramis), 3 = DOS. Role to image page
  through a four-entry table (Q1): `{0, 1, 3, 2}` for `kay1024.rom`, `{2, 3, 0, 1}` for `kay1024b.rom`.
- The TR-DOS trap: with the 48 ROM paged in and a Beta card present, an M1 at `#3Dxx` pages the DOS role in; it does not
  fire from RAM (Unreal's `CF_LEAVEDOSRAM`, as Scorpion). The existing `Memory` session-flag tail does this already.
- Special all-RAM modes (Scorpion, +3) do not exist on this board.

## 5. Video and timing

- Frame 69888 T (312 lines x 224 T), INT 32 T, border updated every 4 T, no contention at 3.5 MHz
  (`ContentionRule::None`, [contention research](../2026-09-28-m1-contention/contention-by-machine.md) section 10).
- Paper start 16132 T from INT, if Unreal's preset counts as it does for its Scorpion preset (14344 = 14336 + 8) (Q4).
  `config.cpp` gets a `case MM_KAY` in both geometry switches (the programmatic one and the INI one).
- INT: the redraw shows DD53, a 4-bit shift register loaded at the frame sync and clocked by /M1 which releases /INT after
  a few M1 cycles; this agrees with S3 ("the INT length depends on the instructions"). Phase 3 uses a fixed 32 T; a
  later step may count M1 cycles (Q4).
- `screen.cpp`: `DetectModeKay` returning `{ M_ZX48, R_256_192 }` style normal mode, the Kay's own `SetVideoMode` case with the
  4T border and no contention. Odd boards (2006 NB multicolor) are out of scope.

## 6. Turbo

- Sources of the turbo state: the front-panel switch, `#1FFD.D2 = 0`, the bus `TURBO` line, and DOS inactive (S3).
- First step: flat 7 MHz when the state is on, through the existing `FrontPanelSwitch::Turbo` (as Profi) and the
  speed control. The switch is a TTD-recorded input (as Profi, `TTDInputKind::FrontPanelSwitch`).
- Waits at 7 MHz: the passport gives the ratio ROM 2.0 / RAM 1.75, the article 1.9 for RAM, and says IORQ is stretched
  (1.5 to 2 times on the Kay-256) and "ports 1.0". An overlay (`hostbusoverlay.h`) is added only after a measurement
  against these numbers; naive first (Q9).

## 7. Fitting unreal-ng

| Area | Change |
|:--|:--|
| Factory | `PortDecoder::IsModelSupported` and `GetPortDecoderForModel`: `case MM_KAY: new PortDecoderKay(context)` |
| New files | `core/src/emulator/ports/models/portdecoderkay.h` / `.cpp` (no underscore, AGENTS naming), based on the Spectrum 128K decoder; class `PortDecoderKay` |
| Memory | the decoder owns the bank computation through `UpdateModelMemoryBanks()` (as `PortDecoder_Spectrum3`); add `MM_KAY` to `decoderOwnsMemoryManager` in `Memory::UpdateZ80Banks` |
| Config | `data/configs/kay/unreal.ini` (`HIMEM=KAY`, `RAMSize=1024`, `[ROM] KAY=rom\kay1024.rom`), `Config::GetConfigFolderForModel`, `config.cpp` geometry cases; `[KAY] RomLayout=` and `[KAY] Turbo=` keys |
| Model row | keep short name `KAY`; default RAM 1024 (Q7); aliases `KAY1024` and `KAY256` (RAM 256) in the `ModelAlias` table |
| `PagingFamily` | `MachineStateTransfer`: add `PagingFamily::Kay` ("Kay"): #7FFD with the extension bit and #1FFD with ROMS, RAM-at-0 and the turbo bit. It is not `Scorpion`: Scorpion's #1FFD bits mean other things (monitor, ROM bit 2). Transfer rules: Kay to 128K keeps pages 0-7 and the ROM role; to Pentagon / Scorpion only through the RAM bank list |
| Snapshot capture | `PortDecoderKay` installs a `KayCapture` policy (an `ISnapshotCapturePolicy`): view `machineHint = "kay"`, 16 banks (256 KB) or 64 banks (1024 KB), `p1FFD` carried. Replaces the `capture_unsupported` note for Kay in `snapshotcapture.cpp`. A `.sna` / `.z80` is offered only when the state is a plain 128K subset (`SavesAs48K`-style rule: #1FFD ROMS = 0, no extension bits, RAM at 0 off); `.szx` has no Kay machine id, so no `.szx` (as Profi). The TTD checkpoint carries everything |
| TTD | `PeripheralId::KayPaging` (next free number at the time of writing: 62; take the next free one when implementing), `TTDDeviceType` mirror and its `static_assert`, `core/src/debugger/ttd/kay/ttdkaypaging.h` / `.cpp`. Blob (explicit filler, byte-wise hashed): `p1FFD`, flags (turbo switch position, turbo state), 2 reserved bytes. `GetTTDModelStateIds()` returns `{KayPaging}`; `CreateTTDSerializers()` covers it. #7FFD is in the chipset state. A version byte is not needed: the reserved bytes are the room |
| Ports / trace | `portdecoder.cpp` `GetPortMapInfo` rows for Kay (`modelName` "Kay-1024"); the port-trace decode attribution |
| Devices | built-in Beta 128 (as every model); `IDE_NEMO` (already `SchemeFits` for Kay); Kempston joystick (`HasKempstonJoystick`); the Kempston mouse; AY with `Spectrum128AyIoPort`-style port A / B stub |
| Slots | no `machines.cpp` refdata entry first; the three NemoBus slots come with the zx-bus-slots work (`CardWins`), a later phase |
| Media | floppy: built-in Beta 128 drives A-D; HDD: `[HDD] Scheme=NEMO`; tape as every model. Nothing new |
| Qt | `menumanager.cpp` `supportedModels`: "Kay-1024 (1024K)"; the TURBO switch in the Machine menu |

Not done on purpose: no `KayBoard` struct until a second Kay board is built; no pattern beyond the Profi precedent.
Odd chips: none. The 2006 NB CPLD, if ever built, goes in its own vendored library.

## 8. Phases

Every phase ends with a zero-warning build and `core-tests` green. Every phase carries its own automation parity, its
own verified recipe and a TTD recording (see below).

| Phase | Work | Automation parity (all in the same phase) | Recipe | TTD |
|:--|:--|:--|:--|:--|
| 0 | Evidence: trace in the redraw the frame / INT counters (Q4), the top RAM bits and their gating (Q3), the Kempston wiring (Q5); boot experiment for the ROM roles (Q1) | none | notes in [research-kay-reference-consensus.md](research-kay-reference-consensus.md) | none |
| 1 | `PortDecoderKay`: #7FFD / #1FFD, RAM and ROM mapping, config folder, ROM plumbing, `KAY` creatable, boots the four ROM roles; `KayPaging` blob; fixes `emulatormanager_test.cpp` | CLI: model tables in `cli-processor-state.cpp` and `cli-processor-instance.cpp`; WebAPI: `state_memory_api.cpp`, `openapi_lifecycle.inc` / `openapi_schemas.inc` model enum, `GET /api/v1/emulator/models`; MCP: `mcp-tools.cpp` model list, `mcp-resources.cpp` `unreal://machine/kay`; Lua `lua_emulator.h`, Python `python_emulator.h` model names; Qt `menumanager.cpp` | `.recipe/machines/kay.md` written and run through MCP and WebAPI | record a TTD session before the boot run in the recipe; seek back over a #1FFD write and check the page |
| 2 | Ports and devices: AY decode, Kempston at A0 = 1, #FE bits, Sinclair joysticks, DOS ports, NemoIDE, mouse, Centronics stub, port map and trace rows | the port map and port trace endpoints / tools list Kay's rows (CLI `portmap`, WebAPI port map, MCP `inspect_state`), Lua / Python port read / write helpers need nothing new | recipe gets "ports" and "IDE" sections, each command run | TTD recording while the AY and Kempston are used; replay equal |
| 3 | Video and timing: `config.cpp` geometry, `screen.cpp` Kay mode, no contention, INT | video-mode and timing fields read the Kay values on every surface (screen mode query, `get_machine_info`) | recipe: frame length and INT check | TTD frame index equals 69888 per frame |
| 4 | Turbo: #1FFD.D2, front-panel switch, 7 MHz, A/B measurement, overlay only if measured needed | CLI `switch`, WebAPI `/switches` + OpenAPI, MCP resource, Lua / Python `get_switch` / `set_switch`, Qt Machine > TURBO Switch | recipe: turbo on / off | the switch is a recorded input; seek over a switch change |
| 5 | Snapshots and transfer: `KayCapture`, `PagingFamily::Kay`, `.sna` / `.z80` of a plain 128K state, TTD fixture in `testdata/ttd/` | snapshot save / load surfaces report Kay's view and reasons (`capture_unsupported` gone) on CLI, WebAPI, MCP, Lua, Python; Qt save dialog | recipe: save and reload | the fixture is a TTD recording of a Kay boot |
| 6 | Variants (separate designs): Kay-256 v1.4 (48 KB ROM, per-machine-cycle turbo), NemoBus slots with BETA-TURBO / NemoIDE / XT keyboard cards, 2006 NB, 2010 SL4 | per variant | per variant | per variant |

Recipe rule: a recipe is verified when every command in it has been run against a live emulator, and the TTD recording is
started before the run so a failure can be replayed.
