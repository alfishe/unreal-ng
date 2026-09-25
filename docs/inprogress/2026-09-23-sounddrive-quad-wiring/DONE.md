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
   routing to Covox exactly when TR-DOS isn't claiming the address and
   `SD=1`, matching the reference decoders (pentevo/Unreal `io.cpp`, Xpeccy
   `soundrive.c` SDRV_105_1) - Beta128 keeps precedence while TR-DOS is
   paged in.
   - **Near-regression caught in testing:** registering Covox at the raw
     `#0F/#1F/#4F/#5F` in `SoundManager::attachToPorts()` would have won
     the exact-address dispatch-map race against `WD1793::attachToPorts()`
     (`core.cpp` attaches sound before the Beta disk), permanently blocking
     disk access whenever `SD=1` - caught by
     `WD1793_Integration_Test.TRDOS_FORMAT_FullOperation` failing. Session 2
     fixed this with a `0x0100`-marked dispatch key distinct from the FDC's
     raw address; session 3 below replaced that marker-key workaround
     entirely by moving Covox off the exact-address map altogether.
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

## What landed (session 3: self-decoding architecture, found via SQTracker/Scorpion)

`PortDecoder_Pentagon128`'s fix in session 2 did not help `PortDecoder_Scorpion256`
at all - confirmed live via `SQTrackerV1.0.trd` (a genuine real-time 4-channel
Scorpion + SoundDrive player): Scorpion had **no** SoundDrive/Covox routing
whatsoever. Its catch-all OUT/IN arms dispatched the *raw, unmasked* port
through the exact-address map, so `OUT (n),A`-style writes (upper byte =
the accumulator value, e.g. `0x850F`) never matched anything; worse, `#1F`/
`#5F` are unconditionally claimed by Scorpion's own Beta128 mirror decode,
so those writes were live-traced landing in the real WD1793 controller as
bogus register writes.

Investigating why this bug class kept recurring per-model led to checking
how other multi-machine Speccy-clone emulators avoid it
(`/Volumes/TB4-4Tb/Projects/emulators/github/Xpeccy`): every machine file
there calls one shared `zx_dev_wr()` for Covox/SoundDrive/GeneralSound,
gated by a single per-machine `flgBDI` boolean - the card's own address
pattern lives in exactly one place, never duplicated per model. Adopted the
same shape here:

- `PortDevice::tryClaimOut/In` — new virtual hook (default declines) for
  devices whose address pattern can't be one exact key.
- `PortDecoder::RegisterSelfDecodingDevice`/`DispatchSelfDecodingOut/In` —
  base-class registry and dispatch, tried by every model's decode fallback.
- `Covox` is now the self-decoding device: `Fitment` (Mono `#FB` vs Quad
  mode-1+mode-2) is baked in at construction from `config.sound.sd`/
  `covoxFB` (mirrors Xpeccy's `sdrvCreate(type)`), and `tryClaimOut/In`
  hold the one authoritative mask/match check.
- `SoundManager::attachToPorts()` shrank to a single
  `RegisterSelfDecodingDevice(_covox)` call - no per-port wiring, so the
  session-2 `0x0100` marker-key workaround for the WD1793 dispatch-map
  collision is no longer needed: Covox never touches the exact-address map.
- `PortDecoder_Pentagon128` and `PortDecoder_Scorpion256` both call
  `DispatchSelfDecodingOut/In` from their existing decode fallbacks, each
  keeping its own Beta128/TR-DOS precedence logic (unchanged, model-specific
  hardware truth) - only the "who do I ask when nothing else claims this
  port" plumbing is now shared.
- `PortDiagnosticRecorder::ResolveDeviceId` mask-matches the low byte for
  the seven SoundDrive addresses it can't recognize by exact key anymore
  (self-decoded events carry the raw port), so `/profiler/porttrace` no
  longer attributes Covox writes to the generic `"Custom"` device.

`ATM710`/`ATM3`/`ProfScorpion` ship `SD=1` too and likely have the same gap
as Scorpion did, but were not touched - no test content available to verify
against (see Remainders).

## Evidence

- Full `core-tests` suite green (3298 tests, 1 pre-existing unrelated
  skip), zero compiler warnings, before and after every change across all
  three sessions.
- `WD1793_Integration_Test.TRDOS_FORMAT_FullOperation` used to bisect and
  confirm the dispatch-map collision (session 2's near-regression) before
  it ever reached a live build; still green after session 3 removed the
  marker-key workaround that fixed it.
- New regression coverage: `CovoxTest.SoundDriveDoesNotStealBeta128Addresses`,
  `PortDecoder_Pentagon128_Test.SoundriveQuadPortsReachHandlerUndisturbed`/
  `SoundriveModeOnePortsRespectTrdosPrecedence`,
  `PortDecoder_Scorpion256_Soundrive_Test.*` (new file) - all drive the real
  `DecodePortOut`/`DecodePortIn` path with `RegisterSelfDecodingDevice`, not
  a stub.
- Live-tested via MCP (port-trace + audio) and manually by the user across
  multiple real SoundDrive/Covox demos on both Pentagon and Scorpion,
  including mode-switching mid-playback and a genuine 4-channel Scorpion
  tracker (SQTracker v1.0) confirmed playing on real hardware content.

## Permanent documentation

- `docs/ports/ports.md` — Soundrive/Covox sections, both port sets, TR-DOS
  precedence
- `docs/emulator/design/audio/sound-device-registry.md` — registration rule,
  updated for the self-decoding architecture
- `.recipe/peripherals/covox-sounddrive.md` — agent recipe (semantics,
  fitment check, mode-switch click behavior)
- `core/src/emulator/sound/covox.h` — class/constant docs cite hardware
  references, the reference-emulator survey, and the `Fitment`/self-decoding
  rationale

## Remainders

- `CovoxDD` (`#DD` mono Covox) still parsed-but-inert — scope C, revisit
  if Scorpion Covox software matters.
- `PortDecoder_Spectrum128` (plain 128K models) does not decode any
  Covox/SoundDrive ports at all, even though clone configs (e.g.
  `spectrum128/unreal.ini`) ship `SD=1`. `PortDecoder_ATM710`/`ATM3`/the
  ProfROM path likely have the same gap `PortDecoder_Scorpion256` had
  before session 3 (raw-port catch-all with no self-decoding fallback
  wired in) — none of these were touched: no test content was available to
  verify a fix against, and blind-patching an if-chain decoder with no way
  to confirm it works is worse than leaving it visibly broken. Revisit with
  real SoundDrive/Covox content for any of these models.
- A genuine, separate bug surfaced during Scorpion testing on some tracks:
  the *content* the CPU computes for the DAC clusters near the silence
  midpoint (correct routing, wrong/quiet values) - not a port-decode issue,
  most likely a memory-banking or loader problem upstream of the player.
  Out of scope for this fix; needs its own investigation.
