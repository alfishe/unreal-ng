# SoundDrive Quad Wiring Fix — Hardware Truth and Dispatch Repair

Date: 2026-09-23 · Status: landed, verified (commit pending) · Scope: `core/src` sound wiring + tests + docs

## Summary

The `Covox` class (`core/src/emulator/sound/covox.h`) has always modeled
SoundDrive 1.05 correctly as a quad 8-bit DAC, but `SoundManager` registered
only port `#FB` into the port decoder's exact-address dispatch map — the
other three hardware ports (`#F1`, `#F3`, `#F9`) were silently dropped, and
the `SD=` config key was never parsed at all. Shipped SoundDrive software
therefore played through a mono Covox at best. This fix wires the full quad
when `SD=1`, parses the key, and makes the advertised `/ports` row match what
dispatch actually installs.

## Hardware truth (explicit sources)

Two independent references agree on the SoundDrive 1.05 "mode 2" port map:

1. **VELESOFT DAC-for-ZX database** —
   <https://velesoft.speccy.cz/da_for_zx-cz.htm>, section
   "SOUNDRIVE 1.05 PORTS - mode 2":

   | Port | Channel |
   |:--|:--|
   | `#F1` | left channel A |
   | `#F3` | left channel B |
   | `#F9` | right channel C |
   | `#FB` | right channel D ("covox - port #FB") |

   VELESOFT explicitly notes `#FB` doubles as the plain Covox port — mono
   Covox software works unchanged on a fitted SoundDrive.

2. **BC Info Guide #4, ZX Spectrum Ports Guide** (Black_Cat, 2008,
   www.zx.clan.su) — SOUNDRIVE v1.05 (SOUNDRIVE/COVOX) row:
   `#F1 #F3 #F9 #FB` ↔ address pattern `xxxxxxxx1111B0A1`, write
   `LA, LB, RA, RB`. Local transcription:
   [zx-ports-full-table.md](../../ports/zx-ports-full-table.md).
   The guide also lists the older v1.02 layout (`#0F/#1F/#4F/#5F` primary
   set + v1.05 mirrors) — the primary set is not implemented here.

The emulator's decode — `PORT_MASK 0xF5` / `PORT_MATCH 0xF1`, i.e. bits
`1111B0A1` — is exactly the Black_Cat pattern, and
`Covox::portToChannel()` maps the four ports to LeftA/LeftB/RightA/RightB.
The class was right; only the wiring was missing.

## What was broken (evidence)

1. **Quad ports never dispatched.**
   `SoundManager::attachToPorts()` registered only
   `Covox::PORT_RIGHT_B` (`#FB`); the comment claimed "all 4 ports decode
   to same handler", but `PortDecoder::_portDevices` is an exact-address
   map (`key_exists(_portDevices, port)` in `PeripheralPortOut`) — writes
   to `#F1/#F3/#F9` fell through to "no peripheral device to handle".
2. **`SD=` config key inert.** `config.sound.sd` (struct member since the
   UnrealSP config import) had zero readers; `Load()` parsed only
   `CovoxFB`/`CovoxDD`. All clone configs ship `SD=1` — ignored.
3. **`/ports` advertised the quad unconditionally.** The 128K-class model
   case pushed a static row
   `"Covox / SoundDrive (#F1,#F3,#F9,#FB)"` (mask `0x00F5`) regardless of
   fitment and regardless of the mono-only dispatch — observability
   claimed what runtime did not deliver.
4. **`CovoxDD=` parsed but never consumed** (Scorpion-style `#DD` Covox
   does not exist). Out of scope for this fix; documented as inert.

## The fix (scope chosen: wiring + SD key)

- `core/src/emulator/config.cpp` — parse `SD` from `[SOUND]`.
- `core/src/emulator/sound/soundmanager.cpp` —
  device created when `covoxFB || sd`; `attachToPorts()` registers all
  four ports when `sd`, `#FB` only otherwise; `detachFromPorts()`
  unregisters all four unconditionally (no-op for absent keys).
- `core/src/emulator/ports/portdecoder.cpp` — the advertised row is now
  fitment- and mode-gated: quad row (both `sound_covox` +
  `sound_sounddrive` tags) when `SD=1`, `Covox (mono #FB)` exact row when
  only `CovoxFB=1`, no row when neither.
- `core/src/emulator/sound/covox.h` — class doc now cites both hardware
  references.

## Verification

- Unit: `CovoxTest.SoundDriveWiresAllFourQuadPorts` and
  `CovoxTest.CovoxFBAloneWiresOnlyMonoPort`
  (`core/tests/emulator/sound/covox_test.cpp`) drive a concrete decoder
  stub and assert, per port, that `PeripheralPortOut` reaches the DAC
  latch (observed through `TTDSaveState`).
- Regression updates: three port-map/port-tag tests that had pinned the
  old unconditional row
  (`portdecoder_portmap_test.cpp`,
  `portdecoder_porttag_test.cpp`) now pin the mode-dependent truth.
- Full suite: all shards pass (~1,313 tests); build clean, zero compiler
  warnings.
- Live end-to-end: `POST /emulator/start {"model":"PENTAGON"}` then
  `GET /emulator/{id}/ports` returns
  `"SoundDrive quad DAC (#F1 L-A, #F3 L-B, #F9 R-A, #FB R-B; #FB doubles as mono Covox)"`
  with tags `["sound_covox", "sound_sounddrive"]` — advert matches
  dispatch.

## Documentation updated with sources

- [docs/ports/ports.md](../../ports/ports.md) — Soundrive/Covox sections
  (both copies + recap) now cite BC Info Guide #4 and VELESOFT mode 2,
  note the `#FB`-doubles-as-Covox fact, and state that only the mirror
  set is decoded.
- [docs/emulator/design/audio/sound-device-registry.md](../../emulator/design/audio/sound-device-registry.md)
  — registration rule updated to `covoxFB || sd` with the mode split.
- [.recipe/peripherals/covox-sounddrive.md](../../../.recipe/peripherals/covox-sounddrive.md)
  — toggle semantics, `/ports` fitment check, `CovoxDD`-is-inert pitfall,
  ground-truth links.
- [docs/ports/zx-ports-full-table.md](../../ports/zx-ports-full-table.md)
  — untouched on purpose: verbatim Black_Cat transcription, already
  attributed.

## Not done (deliberate)

- **`CovoxDD` device** (scope C declined for now): a mono Covox at `#DD`
  needs a small class extension (parametrized mono port); config knob
  stays parsed-but-inert, documented as such.

## Superseded — see DONE.md

The line below was wrong: `testdata/sound/soundrive/balldreams2.sna`
(SoundDrive v1.02) turned out to rely on exactly the primary port set, and
testing it live surfaced three further bugs (a decode-table collapse that
predates this fix, the missing mode-1 decode, and two mixing bugs) on top
of what this document originally covered. All four are fixed — see
[DONE.md](DONE.md) "session 2" for the full account and evidence.

~~**v1.05 primary port set** (`#0F/#1F/#4F/#5F`): no known software relies
on it over the mirror set; not decoded.~~
