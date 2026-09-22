# Status: DESIGN REVIEW ROUND 2 APPLIED

## Overview

Sound card implementations for ZX Spectrum emulation:
- **General Sound (GS)** — P0, Z80 coprocessor with 4-channel DAC
- **NeoGS** — P2, Physical Z80 + FPGA glue, 8 channels, SD, MP3

## Documents

| Document | Priority | Status |
|:---------|:---------|:-------|
| [`gs-tdd.md`](gs-tdd.md) | P0 | Round 2 review fixes applied |
| [`gs-card-interface.md`](gs-card-interface.md) | — | Phase 0 design snapshot for the card-personalities work (interface contract, LW interpreter design); superseded by the as-built TDD |
| [`gs-card-personalities-tdd.md`](gs-card-personalities-tdd.md) | P0 | As-built record: `GeneralSoundCard` interface, `SoundChip_GSLightweight` card + `gsmodplayer`, runtime switching, replay algorithm (§7.3), TTD contracts (§5.5); phases 0-4 gated, phase 5 tests 18/18 |
| [`neogs-tdd.md`](neogs-tdd.md) | P2 | Round 2 review fixes applied |
| [`verification-findings-and-bugs.md`](verification-findings-and-bugs.md) | — | Playback chain verified against firmware sources; 6 bugs found, BUG-4 (DRC windup), BUG-5 (37.5 kHz interrupt pulse-loss — the steady-tone pitch float) and BUG-6 (NeoGS RAM default bled into the classic card — scorpion-family boot race, stock 128 KB card restored) fixed + tested (2026-09-20); GS enabled on atm3/atm710 — decoder arms + configs + regression tests (§5.1); BUG-10..16 (real-content playback correctness) + render-quality pass (2026-09-22) |
| [`diagnostics-gaps-proposal.md`](diagnostics-gaps-proposal.md) | — | WebAPI/MCP/CLI/Lua/Python triage gaps found while debugging BUG-10..16 (§1-§5: PROPOSAL, not implemented), a concrete wiring plan for those gaps (§6), and the TTD-specific angle (§7): BUG-17 (TTD use-after-free on GS personality switch, §7.1) IMPLEMENTED + tested 2026-09-22; the TTD restore-report visibility gap (§7.2) is still PROPOSAL |
| [`materials/README.md`](materials/README.md) | — | Materials index |

## Design Review Round 1 (2026-09-19)

### Critical Issues — Fixed

- [x] **C1:** LLE Z80 only, HLE/BASS out of scope
- [x] **C2:** Lazy flush on port access, not bulk frame-end
- [x] **C3:** Use z80ex from [github.com/alfishe/z80ex](https://github.com/alfishe/z80ex)
- [x] **C4:** TTDSerializable interface (ttdserializable.h signatures)
- [x] **C5:** Use existing `[SOUND] GSType/GSReset/gs_vol` keys

### Major Issues — Fixed

- [x] **M1-M10:** All fixed in Round 1

## Design Review Round 2 (2026-09-19)

### Critical Issues — Fixed

- [x] **CR-1:** TTD API corrected to real ttdserializable.h signatures
- [x] **CR-2:** Automation uses `_context->pSoundManager->getGeneralSound()`
- [x] **CR-3:** z80ex from [github.com/alfishe/z80ex](https://github.com/alfishe/z80ex)
- [x] **CR-4:** MPAG ROM/RAM rule — V==0→ROM, V≥1→RAM pair (firmware/Xpeccy)
- [x] **CR-5:** NeoGS port numbers corrected from ports.v (0x11-0x15)

### Major Issues — Fixed

- [x] **MR-1:** Interrupt semantics — documented pulse→level simplification + why safe
- [x] **MR-2:** Mixing model — emit samples per-write via SoundDevice.update(tact)
- [x] **MR-3:** Stereo mapping — resolved: 1,2→L, 3,4→R (emulators/FPGA, not guide)
- [x] **MR-4:** High-byte alias — design decision documented (low-byte-match or aliases)
- [x] **MR-5:** RAM spec — corrected to 128-512KB for original GS
- [x] **MR-6:** NeoGS clksel — bits 4-5 documented with frequency table note
- [x] **MR-7:** NeoGS pieces — 8ch DAC layout, DMA provenance (CPLD/Unreal, not fpgaD)

### Minor Issues — Fixed

- [x] bootGS.rom label — 512KB NeoGS flash, not "minimal init ROM"
- [x] Volume divide — /256 not /vol_div
- [x] temp.sndblock → SoundManager::isSynthesisSuppressed()
- [x] "accurate mode" references removed (LLE-only scope)
- [x] _context->config references fixed
- [x] ROM loading config key added (gs_rom=)
- [x] NeoGS port numbers corrected from ports.v

## Design Review Round 3 (2026-09-19)

### Blockers — Fixed

- [x] **B2:** 8ch DAC decode — `(addr>>8)&7`, 0x100-byte windows (not 0x200)
- [x] **B3:** CLKSEL table — transcribed from ports.inc (24/12/20/10 MHz, reset=10MHz)

### Half-Propagated — Fixed

- [x] §4.4 + SoundDevice inheritance (float update(tact,l,r))
- [x] §6.2 volume formula — centering `(data-0x80)` + DC offset `+gs_vfx[33]`
- [x] §3.1/§3.4 vol_div → /256
- [x] §5.3 old TTD registration → RegisterModelPeripherals pattern
- [x] §4.5 "matches AY limitation" → see §2.2 decision
- [x] neogs-tdd §3.3 readback 0x3F → 0x7F
- [x] NEOGS-DIFFERENCES §3.3 GSCFG0 bits 4-5/7 added
- [x] NEOGS-DIFFERENCES §6.1 "ignore ports ≥ 0x0F" for P0

### Fabricated API — Fixed

- [x] §2.4 `pMachineState->frame_tacts` → config.frame / frame_duration_us + turbo
- [x] §10.1 httplib → drogon pattern (EmulatorAPI::getStateAudioGS)
- [x] §7.2 undefined `path` variable → `filename`

### Minor — Fixed

- [x] §2.2 stale cross-ref "§2.4" → §2.3
- [x] §2.4 "Real GS FPGA" → "NeoGS FPGA"
- [x] neogs-tdd duplicate §1.2 → renumbered to §1.6
- [x] neogs-tdd §3.1 `_gscfg0=0` → 0x30 (reset value)

## Implementation Status

### GS (P0)

- [x] Create `SoundChip_GeneralSound` class (LLE, `soundchip_gs.h/.cpp`) +
      `SoundChip_GSLightweight` (LW, `soundchip_gslw.h/.cpp` + `gsmodplayer.h/.cpp`)
      behind the shared `GeneralSoundCard` interface (§7.3 personalities work)
- [x] Embed z80ex lightweight Z80 core (LLE coprocessor)
- [x] Implement host port handlers (#B3, #BB, #33)
- [x] Implement internal port handlers (0x00–0x0B)
- [x] MPAG page encoding (rotated, masked)
- [x] DAC sample fetch on memory read
- [x] Volume calculation with gs_vfx curve
- [x] Interrupt generation (37.5 kHz)
- [x] Lazy sync on port access
- [x] Config integration (`[SOUND] GSType=Z80|LW`)
- [x] ROM loading from `data/rom/`
- [x] SoundManager integration, incl. runtime personality switching
      (`switchGeneralSoundCard`)
- [x] TTD serialization (implement TTDSerializable) - LLE and LW register
      under separate peripheral ids (`GeneralSound`/`GeneralSoundLightweight`)
- [x] TTD registration in RegisterModelPeripherals()
- [x] Unit tests (GS suites across `core/tests/emulator/sound/chips/`,
      `core/tests/debugger/ttd/`)

### Automation (GS)

- [x] WebAPI: implement `getStateAudioGS` handler
- [x] WebAPI: implement `postControlAudioGS` handler (9 actions incl.
      `switch_personality`/`dump_module`)
- [x] WebAPI: fix openapi.json GS stub description
- [x] MCP: add `audio_gs` aspect to inspect_state
- [x] MCP: add GS actions to emulator_manage
- [x] CLI: add `state audio gs` command + `gs` control command (9 actions)
- [x] Lua: add gs_* functions to lua_emulator.h
- [x] Python: add gs_* functions to python_emulator.h
- [ ] Docs: update command-interface.md - still marks `state audio gs` as
      "🔮 Planned" (line ~2103); stale, not yet corrected

### NeoGS (P2)

- [ ] Extend SoundChip_GeneralSound
- [ ] GSCFG0 register
- [ ] MPAGEX paging
- [ ] 8-channel mixing
- [ ] DMA controller
- [ ] SD card SPI emulation
- [ ] VS1001 emulation with **minimp3**
- [ ] Config `[NGS]` section
- [ ] Compatibility tests

### Documentation Updates (2026-09-19)

- [x] Corrected NeoGS architecture: **physical Z80 + FPGA glue** (not soft-core)
- [x] Added VS1001K hardware details from `fpgaD/main.v`
- [x] Selected **minimp3** for MP3 decoding

## Reference Materials

See [`materials/README.md`](materials/README.md) for complete index.

### Key Files

| File | Path |
|:-----|:-----|
| GS Programming Manual | `materials/gs/gs_prog.pdf` |
| English Guide | `materials/gs/gs-programming-guide.md` |
| GS Firmware Source | `materials/gs/gs-firmware/` |
| GS Schematic | `materials/gs/gs-firmware/sch/GS_schematic.pdf` |
| NeoGS Differences | `materials/neogs/NEOGS-DIFFERENCES.md` |
| NeoGS FPGA Source | `materials/neogs/fpgaD/` |

## LW real-content pass (2026-09-22)

- [x] BUG-16: song wrap resets speed/TICKLEN and honours byte 951 (firmware
      EFXSKP7); cc_wizard's F20 ending no longer loops 4x slow
- [x] BUG-10..15 (signed samples, volume 0x40, 9xx offset, vibrato/tremolo
      gating, 5xx portamento, 64 KB position) - see
      `verification-findings-and-bugs.md`; cross-checked against openmpt123 on
      the demo's own module (`dump_module` action): waveform corr 0.986
- [x] LW<->LLE bidirectional module handoff; LW->LLE replay paced per
      INT period (381 KB: ~6 min -> ~17 s)
- [x] LW render quality: firmware-style interpolation (SGEN1 midpoints ->
      linear on the 16.16 fraction) and GENZERO tail easing; >6 kHz share
      1.37% -> 0.36% offline, live A/B pending re-listen
- [ ] LW->LLE replay still freezes emulation for the replay duration
      (~17 s on a 381 KB module) - consider chunking across frames

## TTD + GS personality switching (2026-09-22)

- [x] BUG-17: TTD peripheral registry held a dangling `GeneralSoundCard*`
      across a personality switch during an active recording (reproduced as
      a real SIGSEGV) - fixed with `TimeTravelManager::UpdatePeripheral`,
      3 regression tests added (`ttdgeneralsoundswitch_test.cpp`)
- [ ] TTD restore-report (`sizeMismatches`/`missingBlobs`/`unclaimedBlobs`)
      not surfaced on any automation surface - seeking across a
      personality/module-size boundary silently leaves GS state stale; see
      `diagnostics-gaps-proposal.md` §7.2
