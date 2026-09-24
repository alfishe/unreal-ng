# DONE — SoundDrive quad wiring fix

Landed 2026-09-23. Commit pending (one-time instruction required).

## What landed (session 1: port registration)

- Full SoundDrive mode-2 quad dispatch (`#F1/#F3/#F9/#FB`) when `SD=1`;
  mono Covox (`#FB`) when only `CovoxFB=1`; `SD=` key now parsed.
- `/ports` row mode-gated to match dispatch; mono row exact-decoded.
- Two new routing tests + three port-map/tag tests updated.

This session's tests exercised `PeripheralPortOut()` directly through a
`PortDecoder` stub, bypassing `PortDecoder_Pentagon128::decodePortEx()` -
the real path a Z80 `OUT` instruction takes. That gap hid the bug below.

## What landed (session 2: real-demo bugs, found via balldreams2.sna)

Live-testing `testdata/sound/soundrive/balldreams2.sna` (SoundDrive v1.02
digi player) via MCP/port-trace surfaced three further bugs the session 1
tests never exercised, all now fixed and covered by regression tests:

1. **Decode collapse.** `PortDecoder_Pentagon128`'s mask/match table
   resolved `#F1/#F3/#F9/#FB` all onto the single literal `0x00FB` before
   dispatch (`bit1`/`bit3` - the very bits telling the four ports apart -
   were don't-care under the mask). Every mode-2 write landed on the
   RightB channel regardless of which physical port the CPU used, so
   quad SoundDrive degenerated to mono-on-RightB. Fixed with four exact
   per-port table entries (`portdecoder_pentagon128.cpp`).
2. **Mode 1 unimplemented, silently dropped.** SoundDrive's older "mode 1"
   primary port set (`#0F/#1F/#4F/#5F`) - what `balldreams2.sna` actually
   uses - aliases into the Beta128 FDC's wide mirror decode and was always
   gated (dropped) when TR-DOS wasn't paged in, with no fallback. Fixed by
   routing to Covox (via marked dispatch keys, not the raw addresses - see
   below) exactly when TR-DOS isn't claiming the address and `SD=1`,
   matching the reference decoders (pentevo/Unreal `io.cpp`, Xpeccy
   `soundrive.c` SDRV_105_1) - Beta128 keeps precedence while TR-DOS is
   paged in.
   - **Near-regression caught in testing:** registering Covox at the raw
     `#0F/#1F/#4F/#5F` in `SoundManager::attachToPorts()` would have won
     the exact-address dispatch-map race against `WD1793::attachToPorts()`
     (`core.cpp` attaches sound before the Beta disk), permanently blocking
     disk access whenever `SD=1` - caught by
     `WD1793_Integration_Test.TRDOS_FORMAT_FullOperation` failing. Fixed by
     giving Covox's mode-1 ports a `0x0100`-marked dispatch key distinct
     from the FDC's raw address (`Covox::PORT_*_MODE1`).
3. **Mono-compat hardcoded to `#FB` only.** `computeStereoAmplitudes()`
   only auto-centered mono output when RightB (`#FB`) was the sole active
   channel. A mono digi player driving mode-1 LeftB (`#1F`) instead played
   panned hard left. Generalized to center whichever single channel is
   active, regardless of which of the four it is.
4. **Stale-channel click on mode switch.** Mode 1 and mode 2 are two bus
   schemes for the *same* 4 physical latches. A demo switching its
   internal player selection (e.g. SoundDrive → Covox → SoundDrive) leaves
   the previously-used channel frozen at its last value forever (nothing
   rewrites it), which both clicks at the switch and permanently defeats
   the mono-centering above once two channels are simultaneously
   non-midpoint. Added a stale-channel decay: a channel untouched for
   `Covox::STALE_CHANNEL_FRAMES` (3) whole frames decays back to the 0x80
   midpoint through the normal delta/blip path.

## Evidence

- Full `core-tests` suite green (3296 tests, 1 pre-existing unrelated
  skip), zero compiler warnings, before and after each change.
- `WD1793_Integration_Test.TRDOS_FORMAT_FullOperation` used to bisect and
  confirm the dispatch-map collision (bug 2's near-regression) before it
  ever reached a live build.
- Live-tested via MCP (port-trace + audio) and manually by the user across
  multiple real SoundDrive/Covox demos, including switching between a
  demo's device-selection modes mid-playback.

## Permanent documentation

- `docs/ports/ports.md` — Soundrive/Covox sections, both port sets, TR-DOS
  precedence
- `docs/emulator/design/audio/sound-device-registry.md` — registration rule
  for both port schemes
- `.recipe/peripherals/covox-sounddrive.md` — agent recipe (semantics,
  fitment check, mode-switch click behavior)
- `core/src/emulator/sound/covox.h` — class/constant docs cite hardware
  references and the dispatch-map marker-key rationale

## Remainders

- `CovoxDD` (`#DD` mono Covox) still parsed-but-inert — scope C, revisit
  if Scorpion Covox software matters.
- `PortDecoder_Spectrum128` (plain 128K models) does not decode any
  Covox/SoundDrive ports at all, even though clone configs (e.g.
  `spectrum128/unreal.ini`) ship `SD=1` — out of scope here (this fix only
  touched the Pentagon/Scorpion-family decoder, which is what every tested
  demo ran under). Revisit if 128K-model SoundDrive/Covox software matters.
