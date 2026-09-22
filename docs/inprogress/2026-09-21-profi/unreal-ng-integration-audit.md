# ZX Profi 1024 (MM_PROFI) - integration audit

Date: 2026-09-21. Scope: what exists in the repo today, and what must change to make Profi a real,
creatable, TTD-safe, tested machine. Read-only audit; no source was modified. Line numbers are as of
`master` at 09f55bf1.

Glossary (jargon used below):
- **Latch**: a hardware register written by an OUT to a port (e.g. `#7FFD` selects RAM bank).
- **Decoder**: `PortDecoder_*` class that turns a port address into an action.
- **TTD**: time-travel debugging; records machine state per frame and restores it on seek.
- **Creatable**: the `/emulator/models` flag; true only if a decoder exists AND `configs/<folder>/unreal.ini` resolves.

## 0. Bottom line

Profi is a **stub**: a decoder that only does 128K-style `#7FFD` bank-3 paging (3 bits, 8 pages),
ignores `#DFFD`, has no config folder (so it is not "creatable"), maps ROM pages in a probably-wrong
order, has no renderer for its 512x240 mode, and has no TTD serializer for `pDFFD`. Everything else
(1024K RAM, 64K ROM, four ROM roles, Beta Disk, Pentagon-style TR-DOS closing) the core already
supports. The bulk of work is: decoder + memory-map translation, config folder, video renderer, TTD
serializer, tests.

## 1. Current Profi footprint (file:line evidence)

| Area | Evidence | State |
|---|---|---|
| Enum / model table | `core/src/emulator/platform.h:315` `MM_PROFI`; `config.h:55` `{"Profi","PROFI",MM_PROFI,1024,RAM_1024}`; `platform.h:296` `IDE_PROFI`, `:184` `SUBMODULE_VIDEO_PROFI`, `:535` `covoxProfi_vol` | Defined |
| Decoder | `ports/models/portdecoder_profi.{h,cpp}` (46 + 273 lines), derived from `PortDecoder` directly | Stub, see below |
| Factory | `ports/portdecoder.cpp:64` (`IsModelSupported`) and `:109-110` (`new PortDecoder_Profi`) | Registered, so decoder is "supported" |
| Why NOT creatable | `config.cpp:493-513` `GetConfigFolderForModel` has no `MM_PROFI` case, so falls to lowercase ShortName -> `configs/profi`; `config.cpp:526-550` `IsModelCreatable` requires `configs/profi/unreal.ini`. `data/configs/` holds only `atm3 atm710 pentagon128k pentagon512k profscorp scorpion spectrum128 spectrum3 spectrum48 ts-conf zx-diagnostics`. **No `profi` folder.** | Blocker #1 |
| ROM file | `data/rom/profi.rom` exists, 65536 bytes (also copied under `testclient/build/bin/rom/` and a poc). `config.cpp:209` reads `[ROM] PROFI=`; `data/configs/scorpion/unreal.ini:529` has `PROFI=rom\profi.rom` and `:588` a `[ROM.profi]` section (a good template) | ROM present, no profi ini |
| ROM role mapping | `memory/rom.cpp:86-88` (path) and `:172-177`: `base_sys_rom=page0, base_dos_rom=page1, base_128_rom=page2, base_sos_rom=page3` | **Suspect**, see 1.1 |
| Memory | `memory/memory.cpp:854`: `dosflags = CF_LEAVEDOSADR` for Pentagon/Profi (DOS closes when PC >= #4000). `memory.cpp:764`: `state.pDFFD &= ~0x10` in `SetROMMode` (DFFD bit 4 = "RAM at #0000" is cleared when a ROM is selected, as in the original UnrealSpeccy). No `MM_PROFI` branch in `UpdateZ80Banks()` (`memory.cpp:806+`) - generic 128K path only | Partial |
| Video | `video/screen.cpp:253` dispatch `DetectModeProfi`; `:422-428` returns `{M_PROFI, R_512_240}` when `pDFFD & 0x80`; `screen.h:56` `M_PROFI`, `:73` `R_512_240`, `:434` geometry row `{352,288,256,192,...}` (the **256x192 row - no 512-wide geometry**), `:504` `&Screen::DrawProfi`; `screen.cpp:1797` `DrawProfi` is `(void)n;` (no-op); `:1096` screen-digest falls back to classic surface; `:1160` mode name "PROFI" | Detection only |
| Contention/timing | `video/ulacontention.cpp:138` groups Profi with Pentagon/Scorpion as `ULA_DISCRETE_LOGIC`. No Profi frame/raster spec beyond the 48K-style row | Assumed |
| Port tracing | `portdecoder.cpp:523-530` `getPortMapEntries` rows: `#7FFD mask 0x8006 match 0x0004`, `#DFFD mask 0x2002 match 0x0000` (tags Memory/Screen, latch `PDFFD`); `portdecoder.cpp:705/800` `PagingLatch::PDFFD` read/name. Profi has **no `getPortTraceDecodeRules()`** (Pentagon128 has one at `portdecoder_pentagon128.cpp:417`), decoder uses `PortTraceRule::kNoTable` (`portdecoder_profi.cpp:73,124`) | If-chain, no table |
| Automation | `webapi/src/api/state_memory_api.cpp:350` and `cli/src/commands/cli-processor-state.cpp:643`: MM_PROFI in the "4 ROM pages" list (correct for a 64K ROM). `mcp/src/mcp-tools.cpp:116` lists PROFI in the create help. `core/recording/src/recordingmanager.cpp:162` "640p ... ProFi/ATM HD" preset. Model lists are otherwise table-driven (`config.h:55`) | OK once creatable |
| Qt UI | No hard-coded model list (`grep` of `unreal-qt/src` finds none); the menu comes from the model table/API | No change expected |

### 1.1 Decoder gaps (portdecoder_profi.cpp)

- `Port_7FFD` (`:229-254`): only `bankRAM = value & 7` into `SetRAMPageToBank3`; **no extended bits**; ROM polarity
  `isROM0 ? RM_128 : RM_SOS` means D4=1 -> 128K ROM, the opposite of the real 128K (D4=1 -> 48K).
  `modelsregression_test.cpp:382` documents "+3 and Profi ... opposite polarity" and locks it in as goldens
  (`kGoldenRows_Profi`, `:523-550`, e.g. 7FFD=0x10 -> "R128"). Needs a hardware check before the goldens are trusted.
  Also does not write `state.p7FFD` (the base/other decoders do), so `p7FFD` stays 0 and TTD/`/state/paging` see nothing.
- `Port_DFFD` (`:257-273`): empty; comment explicitly says "WHEN YOU IMPLEMENT THIS declare it from
  `GetTTDModelStateIds()`". `pDFFD` is never written, so `Screen::DetectModeProfi` can never see bit 7.
- `IsPort_DFFD` (`:196-210`, mask `0x2002`, match 0): A13=0 and A1=0 only. It **overlaps `IsPort_7FFD`** (mask `0x8006`):
  ports like `#5FFD` (A15=0, A13=0, A2=1, A1=0) satisfy both, and `DecodePortOut` fires both handlers. Real decode
  needs schematic verification (ports `#DFFD` vs `#7FFD` on Profi are distinguished by A15/A13, plus A2 for 7FFD).
- `IsPort_7FFD` comment cites SOUNDRIVE `F1/F9` conflict; Soundrive/Covox-Profi (`config covoxProfi_vol`) is not wired.
- `reset()` (`:17-49`) forces `SetROMMode(RM_SOS)`, sets bank1=5, bank2=2, bank3=0 but never calls `UpdateZ80Banks`
  with a Profi model; no `ApplyBootROMDefaults` override (base hook `portdecoder.h:376`).
- `DecodePortIn` has no Beta128 gating (`IsBeta128Port && !CF_TRDOS`) unlike Pentagon128 (`portdecoder_pentagon128.cpp:97-107`);
  no `#7FFD` readback; no mouse/AY rules beyond mirrors. Kempston mouse handled via the shared `Default_IsPort_KempstonMouse`
  (which already honours `CF_DOSPORTS`: "only Beta Disk answers in TR-DOS", `portdecoder.cpp:1030-1040`).
- No `GetTTDModelStateIds`/`CreateTTDSerializers` override (section 4).
- No `UpdateModelMemoryBanks` (the hook ATM uses; `portdecoder.h:381`).

### 1.2 ROM layout - measured, contradicts rom.cpp

`data/rom/profi.rom` (64 KB) page contents by `xxd`/`strings`:

| Page | First bytes | Strings | Role |
|---|---|---|---|
| 0 | `ED 56 C3 34 03 ...` | "TR-DOS 48K", "TR-DOS 128K", "Sinclair 48", "Sinclair128", "ROM Bios" | **128K ROM with Profi menu** |
| 1 | `F3 11 FF FF 3E 07 ...` | "* TR-DOS Ver 6.08 *", "1998 Profi Code Computers Club", "PROFI+" | **TR-DOS** |
| 2 | `F3 22 03 40 21 00 00 39 ...` | "TR - DOS", "STS IN", "(C) 1997 BY POWER OF SOUND GROUP" | **Service / STS shadow monitor** |
| 3 | `F3 AF 11 FF FF ...` | "1982 Sinclair Research" | **48K BASIC (SOS)** |

`rom.cpp:172-177` assigns `sys=0, dos=1, 128=2, sos=3`. By the measurement the correct assignment is
`128=0, dos=1, sys=2, sos=3` (pages 0 and 2 swapped). This is the same class of bug the Scorpion work fixed
(`rom.cpp:178-186` comment "The previous Service-first assignment scrambled all four roles"). Must be verified against a
boot test (does the 128K menu appear at reset?). Real-hardware page order may differ from the upstream UnrealSpeccy
convention; confirm against the Profi ROM decode (CPU sees page N at #0000 via `#7FFD` D4 + `#DFFD` bits + DOS/SYS latches).

## 2. Memory subsystem capacity

- Constants (`platform.h:249-252`): `MAX_RAM_PAGES = 256` (4 MB), `MAX_ROM_PAGES = 128` (2 MB), one trash page.
  Profi needs 64 RAM pages (1024K) and 4 ROM pages: **fits, no memory-layout change**.
- Per-model RAM sizes already in use: Scorpion 256K (`RAM_256`), ATM710 up to 1024K, ATM3 4096K, Pentagon1024 1024K
  (port `#EFF7`, `portdecoder_pentagon1024.cpp`). `Memory::GetRamMask()` (`memory.cpp:~878`) derives the mask from
  `config.ramsize/16` (1024K -> 0x3F). `TimeTravelManager::ResolveModelRamPages` (`timetravelmanager.cpp:~963`) returns
  `ramsize/16` = 64 for Profi, so TTD RAM capture already covers 1024K.
- Bank API in `memory.h`: `SetROMMode(RM_NOCHANGE|RM_SOS|RM_DOS|RM_SYS|RM_128|RM_CACHE)` (`:31-36`, `:296`),
  `SetROMPage`, `SetROMPageToBank(bank,page)` (`:322`), `SetRAMPageToBank0..3` (`:323-326`; bank0/bank3 take `updatePorts`),
  `SetROM48k/128k/DOS/System` (`:406-409`). Bank 0 (#0000-#3FFF) ROM/RAM switching therefore already exists
  (`SetRAMPageToBank0` for CP/M-style RAM at #0000).
- Derived class hook: `virtual bool UpdateModelBanks()` (`memory.h:305`) is how Scorpion owns its full latch-to-bank
  translation (`ScorpionMemory`, created in `cpu/core.cpp:95-101`). ATM instead delegates to
  `PortDecoder::UpdateModelMemoryBanks()` (`memory.cpp:823-830`). Either pattern works for Profi; the ATM pattern
  (no new Memory subclass, translation in the decoder) is cheaper.
- The generic `UpdateZ80Banks` (`memory.cpp:806-871`) has **no Profi RAM translation**: bank 3 is only set by
  `Port_7FFD`. Profi needs: bank3 page = `(7FFD & 7) | ((DFFD & 7) << 3)` (masked with `GetRamMask()`), bank0 = RAM page
  when DFFD bit 4 (CP/M mode) is set (the original clears that bit on `SetROMMode`, already mirrored at `memory.cpp:764`),
  ROM role selection from `7FFD` D4, `CF_TRDOS`, and shadow/service state. Exact DFFD bit meanings
  (D0-D2 high RAM bits, D4 RAM-at-0000, D5 ROM/shadow control, D6 ?, D7 512x240) are from the UnrealSpeccy
  convention and must be re-checked against a Profi schematic/reference before coding.
- Cost: S/M. No allocation, no `MAX_*` change.

## 3. Reference machines (pattern to copy)

### Port decode tables
- Table-driven decoders (`PortDecoder_Pentagon128`, `getPortTraceDecodeRules()` `:417`, shared table at `:345`) give
  `decodeRuleIndex` attribution. Profi/128K/+3/Scorpion still use if-chains with `PortTraceRule::kNoTable`
  (`spectrum128.cpp:82,145`, `spectrum3.cpp:76,143`, `scorpion256.cpp:177,325`). Either is acceptable; a table is the cleaner target
  for Profi because `#7FFD`/`#DFFD` overlap resolution is then explicit.
- Scorpion: `PortDecoder_Scorpion256` (904 lines); `IsPort_7FFD` mask `0xD027`; `ScorpionMemory` owns bank translation
  (`scorpionmemory.cpp:70-214`): bank3 = 7FFD[2:0] | 1FFD bits, RAM-at-0 priority chain, TR-DOS trigger.
- ATM710/ATM3: `portdecoder_atm710.cpp` (760), `portdecoder_atm3.cpp` (510): FFF7[8] memory map, `UpdateModelMemoryBanks`,
  ATM video via `FF77`; TTD via `PeripheralId::AtmPaging` (`ttd/atm/ttdatmpaging.{h,cpp}`).
- Pentagon1024: `#EFF7` bit 2 extension, `Port_7FFD_Out` override (`portdecoder_pentagon1024.cpp:60-130`), `InitRaster()` call on
  video-mode-bit change (`:88-100`) - **this is the pattern Profi's DFFD bit 7 write must follow** (`Screen::InitRaster`
  re-runs `DetectMode*`).

### Video
- `Screen::DetectMode*` per model (`screen.cpp:266-441`); render dispatch table `screen.h:~490-505`; renderers are
  per-`n`-pixel-clock functions writing `vbuf[video.buf][vptr...]` and advancing `video.vptr` (`DrawATMHiRes`
  `screen.cpp:1544+`, `DrawATM16` `:1453`, `DrawATM2Text` `:1630`).
- Geometry tables carry `{fullFrameWidth, fullFrameHeight, screenW, screenH, ...}` (`screen.h:415-440`); ATM HR uses a
  704-wide storage row while the beam stays 448 px/line (comment at `screen.h:~424`). Profi 512x240 will need an analogous row
  (e.g. 512 or 704 wide x 288) plus screen-digest surface definition (`screen.cpp:~1096` comment says Profi renderers
  are stubbed).
- Note: `DrawP16`/`DrawPMC`/`DrawP384`/`DrawPHR`/`DrawGMX` are also no-op stubs (`screen.cpp:1413-1430`, `:1802`), so
  "Pentagon 16-color" is detection-only; Profi should not assume a finished pixel-renderer precedent other than ATM.

### Beta Disk / TR-DOS per model
- Memory rule (memory note): only Beta Disk answers while TR-DOS is selected. Implemented as: `CF_DOSPORTS` raised by
  `UpdateZ80Banks` (`memory.cpp:854-871`) and consumed by the shared mouse gate (`portdecoder.cpp:1037`) and port
  attribution (`portdecoder.cpp:585, 920`); Pentagon128 additionally gates `IsBeta128Port` on `CF_TRDOS`
  (`portdecoder_pentagon128.cpp:97-107`). Config switch: `[BETA128] Beta128=` -> `config.trdos_present` (`config.cpp:251`).
- Profi uses `CF_LEAVEDOSADR` (`memory.cpp:854`), i.e. the Pentagon rule (DOS ROM closes at PC >= #4000). Profi decoder does not
  copy Pentagon's FDC gating - to do.

### Existing tests per machine (structure)
- `core/tests/emulator/ports/models/`: `portdecoder_models_test.cpp` (consolidated 48/128/+3/Profi/Scorpion `IsPort_*` equation
  tests via `ExpectDecodeMatchesEquation`; Profi at `:128-144`), `portdecoder_atm710_test.{h,cpp}`, `portdecoder_atm3_test.{h,cpp}`,
  `scorpion*_test.cpp` + `scorpionfixture.h` (hermetic fixture: synthetic ROM with self-identifying tag bytes, RAM page n
  -> `0x40|n`, ROM page k -> `0xC0|k`; `Core::Init()` builds the context, the fixture loads the ROM, `RebuildWithModel()`),
  `kempston_mouse_decode_test.cpp:174` (`Profi` bare decoder test; comment at `:139` "PROFI (no shipped config) cannot boot").
- Boot-level: `atm710_trdos_boot_test.cpp`, `atm710_cpm_boot_test.cpp`, `zxevo_boot_test.cpp` (use
  `EmulatorManager::CreateEmulatorWithModelAndRAM(id, "ATM710", 1024, LogError)`, `EnableTurboMode()`, `EmulatorTestHelper::RunUntil`).
- Video: `video/atm_video_modes_suite_test.cpp`, `atm_videomode_test.cpp`, `pentagon_16col_mode_test.cpp` (detection-level),
  `videomode_change_test.cpp:19` (mentions "Profi via DFFD"), `screenvideomodename_test.cpp:20`, `scorpionraster_test.cpp:195`.
- Profi-specific today: `memory/modelsregression_test.cpp` (`SequenceProfi` `:163`, goldens `:523-550`, test `:632`),
  `ports/portdecoder_portmap_test.cpp:126`, `portdecoder_porttag_test.cpp:80,147,291,460` (DFFD `0x83` -> ext bank 3 + 512x240),
  `emulatormanager_test.cpp:480` (`IsModelSupported(MM_PROFI)`).

## 4. TTD integration

Facts:
- `TTDChipsetState` (`ttd/ttdcheckpoint.h:~149-190`) carries: `p7FFD, pFE, pEFF7, pBFFD, pFFFD, pFF77, border_attr, flags,
  wd_shadow[4], comp_pal[16], ulaplus_*, hw_turbo_shift*, freq multipliers, t_states, frame_counter`. It has **no `pDFFD`**
  (nor `p1FFD`). `EmulatorState::pDFFD` lives at `platform.h:905`.
- Framework contract (`timetravelmanager.cpp:1046-1097` `RegisterModelPeripherals`): core devices are registered
  (TurboSound/TSFM, Covox, Tape, KempstonMouse, BetaDisk); then `decoder->CreateTTDSerializers()` are registered and every id in
  `decoder->GetTTDModelStateIds()` must be registered or recording is **refused with an error naming the missing serializer**.
  Overrides: `portdecoder.h:494` (`GetTTDModelStateIds`) and `:498` (`CreateTTDSerializers`). Scorpion example:
  `portdecoder_scorpion256.cpp:586-605`.
- `PeripheralId` enum (`ttdserializable.h:42-55`): `TurboSound=0 BetaDisk=1 Tape=2 Covox=3 TSFM=4 GeneralSound=5
  ScorpionProfROM=6 KempstonMouse=7 AtmPaging=8`, then `Count`.
- Serializer template: `ttd/atm/ttdatmpaging.{h,cpp}` (packed POD struct with `static_assert` on size and no padding,
  `TTDSaveState/TTDLoadState/TTDHashState/TTDStateSize/TTDDeviceName/TTDPeripheralId`, `Snapshot()` shared by save and hash).
- Note: `docs/inprogress/2026-09-21-roadmap/01-roadmap-and-machine-state.md:31` claims "Profi decoder serializers" are present. **They are not**:
  `Port_DFFD` says so itself.
- The `.ksy` (`ttd/ttd.ksy:368`) mentions model-specific latches migrating out of `TTDChipsetState`; the dump format keys peripherals by id,
  so a new id needs no change to the chipset block but should be documented in `ttd.ksy`/`ttddumpformat.h`.

Profi must declare:
1. New `PeripheralId::ProfiPaging = 9` (before `Count`).
2. New `core/src/debugger/ttd/profi/ttdprofipaging.{h,cpp}` class `TTDProfiPaging` modelled on `TTDAtmPaging`. Minimal payload:
   `pDFFD` (1 byte) and any further Profi latches that are not derivable from `p7FFD` (shadow/service/DOS-trigger latches, `#FE`-side
   colour/mono bits, CMOS/RTC address if fitted). Restore must re-run `Memory::UpdateZ80Banks()` and `Screen::InitRaster()` so the
   512x240 mode and bank map are rebuilt (check how `RestoreCheckpoint` and `TTDAtmPaging::TTDLoadState` do it).
3. In `PortDecoder_Profi`: `GetTTDModelStateIds() -> {ProfiPaging}` and `CreateTTDSerializers() -> {make_unique<TTDProfiPaging>(_context)}`.
4. `p7FFD` must actually be written by the Profi decoder (today it is not) or TTD restores a stale value.
5. Update `TimeTravelManager::ResolveModelRamPages` TODO (`timetravelmanager.cpp:973`) only if a non-contiguous page set is needed (not for Profi).

Tests to mirror: `core/tests/debugger/ttd/atm/ttdatmpaging_test.cpp` (round-trip, hash sensitivity), `scorpion/ttdscorpionprofrom_test.cpp`,
`ttdmodelstatecontract_test.cpp` (add `"PROFI"` to the model list at `:49`; it already tolerates a not-provisionable model via `continue`, so this
test silently skips Profi until it is creatable - make Profi mandatory there once the config exists), `ttdperipheralregistry_test.cpp`,
`ttdfullrestore_test.cpp`/`ttdseek_test.cpp` (add a Profi run that pages RAM through DFFD, records, seeks back, compares `MachineStateHash`),
`machinestatehash_test.cpp:126` (already sets `pDFFD`; check whether the hash covers it or relies on the serializer).

## 5. Test infrastructure

- Layout: `core/tests/{emulator,debugger/ttd,automation,z80,...}`, tests globbed recursively (`core/tests/CMakeLists.txt:95`), so new `*_test.cpp`
  files need no CMake edit. Helpers in `core/tests/_helpers/`: `emulatortesthelper.h` (`CreateStandardEmulator(model, level)`,
  `CreateDebugEmulator`, `RunFramesFast`, `RunUntil`, `RunUntilBASICReady`, `ReadSysVar`), `testwaithelper.h` (`TestWait::For/ForAtLeast/ForExactly`;
  never `sleep_for`), `testpathhelper.h` (`GetUniqueTestScratchPath()`), `trdostesthelper.*`, `ttddivergenceharness.*`.
- Machine creation patterns: (a) bare `EmulatorContext` + `new PortDecoder_X(context)` (`portdecoder_portmap_test.cpp:126`, `kempston_mouse_decode_test.cpp:174`);
  (b) `Core::Init()` fixture with synthetic ROM (`scorpionfixture.h`) - best fit for Profi paging tests, no ROM file needed;
  (c) `EmulatorManager::CreateEmulatorWithModelAndRAM("PROFI", 1024, LogError)` (needs `configs/profi`, deployed to the test output by
  `core/tests/CMakeLists.txt:266-272` from `data/configs`).
- Reading state: VRAM straight from `Memory::RAMPageAddress(page)`; boot text via `DecodeTextRows(context, ...)` (ATM boot tests); framebuffer via
  `Screen`/`screencapture`. `EnableTurboMode()` on boot-bound tests, never on pixel-asserting tests (AGENTS.md).
- ROMs: `data/rom/profi.rom` (64K, real Profi ROM: TR-DOS 6.08 / STS / 128 menu / 48K BASIC) is present in `data/rom`, also `testdata/`? (no Profi ROM in
  `core/tests/testdata`; the only copies are under `data/rom`, `testclient/build/bin/rom`, `tools/poc/011-ttd-v2-capture-analysis/bin/rom`).
  `cmake-build-release/bin/rom` exists (build staging). ROM-missing skip idiom in this repo: the TTD contract test does `if (emulator == nullptr) continue;`;
  boot tests generally `ASSERT_NE(emulator, nullptr)`. Profi boot tests should `GTEST_SKIP()` when `FileHelper::FileExists(profi.rom)` is false.
- Known caveat: no Profi machine can be created today, so no existing test boots it; `kempston_mouse_decode_test.cpp:139` records this.

## 6. Automation surface

- Model list / `creatable` flag: table-driven from `config.h:55` + `Config::IsModelCreatable`; nothing to edit besides adding `configs/profi` (and
  `GetConfigFolderForModel` if the folder name differs; ShortName `PROFI` -> `profi` already resolves).
- `/state/paging`: paging latch decode via `portdecoder.cpp:705` (`PDFFD` read) and `:800`; design in
  `docs/inprogress/2026-09-14-automation-triage-gaps/port-tags-paging-design.md:371` (`PDFFD` -> `extended_ram_bank` bits 0-2, `video_512x240` bit 7).
  Real content only appears once `pDFFD` is written by the decoder. Confirm `/state/paging` handles a 64-page bank3 and bank0 RAM.
- `/state/screen/mode`: comes from `Screen` mode enum via `GetVideoModeName` (`screen.cpp:1137`, "PROFI"); currently unreachable because DFFD is never written.
  Doc gap: automation-triage README `:134-137` says ATM/Profi/TSConf mode machinery returns literal "standard".
- Port trace: `getPortMapEntries` (`portdecoder.cpp:523`) already lists both rows; add `getPortTraceDecodeRules()` if the decoder becomes table-driven so
  `decodeRuleIndex` is meaningful. Porttag tests at `portdecoder_porttag_test.cpp:80,147,205,267,291,460`.
- ROM info endpoints (`state_memory_api.cpp:350`, `cli-processor-state.cpp:643`): already report 4 pages/64 KB; page descriptions per model in the CLI
  (`cli-processor-state.cpp:660+`) need a Profi entry (128K/TR-DOS/Service/48K names) - verify.
- MCP: `emulator_manage list_models` reads the same source; `mcp-tools.cpp:116` help text already lists PROFI. Update AGENTS.md "Creatable on master" list
  when done (currently excludes PROFI).

## 7. Ordered gap list

Size: S = under half a day, M = about a day, L = multi-day.

| # | Change | Files | Size | Risk |
|---|---|---|---|---|
| 1 | Hardware spec: write down Profi port map (`#7FFD`, `#DFFD` bits, shadow/service/DOS latches, 512x240 memory layout, AY/Soundrive/RTC, frame timing) from schematic/UnrealSpeccy; resolve ROM polarity and page order | new doc under `docs/inprogress/2026-09-21-profi/` | M | Everything below depends on it; ROM D4 polarity and DFFD bits are currently unverified |
| 2 | Add `data/configs/profi/unreal.ini` (copy `data/configs/atm710` or the scorpion `[ROM.profi]` block: `HIMEM=PROFI`, `RAMSize=1024`, `[BETA128] Beta128=1`, `PROFI=rom/profi.rom`, `ROMSET=ROM.profi` with `sos/dos/128/sys` mapped to pages 3/1/0/2 or `profi.rom:N`) | `data/configs/profi/unreal.ini` | S | Makes Profi "creatable"; ini rom key style `rom\...` vs `rom/...` |
| 3 | Fix ROM role mapping (`128=0, dos=1, sys=2, sos=3`) after boot verification | `core/src/emulator/memory/rom.cpp:172-177` | S | Wrong mapping boots wrong ROM; verify with real ROM boot test |
| 4 | Implement `Port_7FFD` (write `state.p7FFD`, correct ROM polarity, lock rules) and `Port_DFFD` (write `state.pDFFD`, re-run `UpdateZ80Banks`, `InitRaster` on bit 7 change, model on `Port_EFF7_Out` `portdecoder_pentagon1024.cpp:60-100`) | `portdecoder_profi.{h,cpp}` | M | Overlap of `IsPort_7FFD`/`IsPort_DFFD` masks; existing goldens encode current (suspect) polarity |
| 5 | Profi latch-to-bank translation: bank3 = `(7FFD&7)|((DFFD&7)<<3)` masked by `GetRamMask()`, bank0 RAM/ROM (CP/M bit), ROM role + `CF_TRDOS/CF_LEAVEDOSADR/CF_DOSPORTS`. Use `UpdateModelMemoryBanks` (ATM pattern, `memory.cpp:823-830`) or a `ProfiMemory : Memory` (Scorpion pattern, `cpu/core.cpp:95-101`) | `memory.cpp`, `portdecoder_profi.cpp` | M | Regressing 128K/Pentagon paths (base body must stay byte-identical); `memory.cpp:764` DFFD bit4 clear |
| 6 | Beta Disk gating in `DecodePortIn/Out` (Pentagon128 pattern) + `#7FFD` readback decision + AY/`FE` rules review | `portdecoder_profi.cpp` | S | Rule "only Beta answers in TR-DOS" must hold; mouse gating already in base |
| 7 | Optionally convert to a mask/match table + `getPortTraceDecodeRules()` for trace attribution | `portdecoder_profi.{h,cpp}` | S-M | Keep `getPortMapEntries` rows (`portdecoder.cpp:523`) and portmap tests (`portdecoder_portmap_test.cpp:126`) consistent |
| 8 | Video: geometry row for R_512_240 (`screen.h:434`), `DrawProfi` renderer (`screen.cpp:1797`, per-clock like `DrawATMHiRes`), screen-digest surface for M_PROFI (`screen.cpp:~1096`), colour/mono (`ProfiMonochrome`), `Screen::InitRaster` mode refresh, HUD/recording geometry (`recordingmanager.cpp:162`) | `screen.h`, `screen.cpp` | L | Largest item; 512-wide storage vs beam timing; consumers of frame size (Qt, recorder, screencapture); contention/timing spec for Profi |
| 9 | Timing: confirm frame/line T-states and INT position for Profi; add to `ulacontention` / raster spec if not 224x312 | `ulacontention.cpp:138`, `screen.h` timing | S-M | Unknown vs 48K-style row; may need MiSTer/schematic reference |
| 10 | TTD: `PeripheralId::ProfiPaging=9`, `ttd/profi/ttdprofipaging.{h,cpp}`, decoder `GetTTDModelStateIds/CreateTTDSerializers`, update `ttd.ksy`/`ttddumpformat.h` docs, review `machinestatehash` inclusion of `pDFFD` | `ttdserializable.h`, new files, `portdecoder_profi.cpp` | M | Recording is refused if declared-without-serializer (by design); restore must rebuild banks and video mode |
| 11 | Automation: CLI ROM page descriptions for Profi, `/state/paging` and `/state/screen/mode` verification, OpenAPI/MCP docs, AGENTS.md creatable list | `cli-processor-state.cpp`, `state_memory_api.cpp`, `openapi_ports.inc`, `AGENTS.md` | S | Docs drift; `/state/paging` assumptions about <=8 RAM banks |
| 12 | Tests (new): `PortDecoder_Profi` paging (fixture with synthetic tag ROM, mirror `scorpionfixture.h`), DFFD bit matrix, ROM role/polarity, TR-DOS session (CF_LEAVEDOSADR), video mode detect + renderer pixels (no turbo), boot test with `profi.rom` (skip if absent), porttrace/porttag rows, TTD `ttdprofipaging_test.cpp` + add PROFI to `ttdmodelstatecontract_test.cpp:49` + seek/restore round trip, `emulatormanager_test` creatable flag, update `modelsregression_test.cpp` goldens, `kempston_mouse_decode_test.cpp:139` comment | `core/tests/emulator/ports/models/portdecoder_profi_test.*`, `core/tests/emulator/profi_boot_test.cpp`, `core/tests/emulator/video/profi_video_test.cpp`, `core/tests/debugger/ttd/profi/` | L | Each < 50 ms, use `TestWait`, scratch names unique; boot test uses real ROM |
| 13 | Docs: `docs/inprogress/2026-09-21-profi/` design + DONE.md following Scorpion/ATM docs; correct the roadmap claim that Profi serializers exist | docs | S | - |

### Key risks
1. **Hardware truth unverified**: ROM D4 polarity, DFFD bit assignment, 512x240 memory layout, and `#7FFD`/`#DFFD` decode all come from the current stub or memory of
   UnrealSpeccy; the existing goldens would enshrine any mistake.
2. **ROM role order** in `rom.cpp` contradicts the shipped `profi.rom` contents (pages 0/2 look swapped); boot verification needed first.
3. **Decode overlap** (`#5FFD`-style ports fire both 7FFD and DFFD).
4. **Video** is the only large item; consumers of frame size (Qt viewer, recorder, screencapture) must handle a 512-wide mode.
5. **TTD**: `pDFFD` outside `TTDChipsetState` means restoring without the new serializer silently loses paging; the guard only helps if the model declares its id.
6. Existing suites that reference Profi (`modelsregression_test`, `portdecoder_porttag_test`, `portdecoder_portmap_test`) must be updated in the same change as the decoder.
