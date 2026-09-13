# TSFM sound issues register

Everything found while verifying the TurboSound FM implementation against real hardware, Furnace and Xpeccy on 2026-09-13. One entry per issue: what was observed, the cause, the status, where the evidence and the fix live. Fixed entries name the commit or the uncommitted files; open entries say what a fix would take.

| # | Issue | Area | Status |
|---|---|---|---|
| 1 | FM 7.4 dB too quiet against SSG | mix levels | fixed (config), code baseline open |
| 2 | Clicks: device and mixer disagree on the frame's sample count | mixer | fixed, uncommitted |
| 3 | CSM does not retrigger on Timer A overflow | ymfm | fixed, `279bbe2c` |
| 4 | HQ character chains ran with HQ off | mixer / CPU | fixed, `7840d135` |
| 5 | Sub-audio FM content reaches the output (no coupling high-pass) | FM output stage | open, by design so far |
| 6 | int16 mix has no headroom for loud FM | mixer | open, tracked separately |
| 7 | LQ → HQ switch replays stale decimator history | devices | open, minor |
| 8 | SSG opposite-side leakage symmetric at −19 dB, hardware asymmetric | SSG pan | open, cosmetic |
| 9 | Furnace's frequency-latch semantics differ from upstream ymfm | ymfm | reviewed, not adopted |
| 10 | Chip-to-chip FM level difference on the real board | hardware | not modelled |
| 11 | Replay test reads an ignored scratch file | tests | mitigated (skip) |
| 12 | Emulator app exits when its binary is rebuilt underneath it | tooling | noted |
| 13 | Moe-bius tune leaves an FM channel keyed after it ends | tune content | not a bug; documented |
| 14 | Device state (AY, FM, FDC) not reachable from the automation interfaces | automation | fixed, uncommitted |
| 15 | Fresh floppy drives reported a random head track | FDD | fixed, uncommitted |

---

## 1. FM level 7.4 dB too quiet

**Observed.** On the real board one FM carrier at TL 0 sits 0.4 to 1.2 dB *above* one SSG channel at volume 15 on the same output. In unreal-ng it sat 6.6 dB *below*. Measured with the volume test program (`materials/volume/`, hardware MP3 vs emulator capture, per-source RMS over 24-frame steps).

**Cause.** `kFmBaseGain = 0.30` was derived from MiSTer's "FM full scale = 2.0 × one SSG channel" ratio. The board measures about 4.7 ×.

**Fix.** `TSFM_FmTrimDb=7.4` in all nine shipped inis (`e7e63ec1`); the trim is read at start and applied at device construction. Re-measured in the app: FM − SSG A = +0.8 dB, the hardware average. `TsfmGain_Test` pins the new one-carrier level (5744) and asserts the ini carries 7.4.

**Open part.** Moving the code baseline `kFmBaseGain` to 0.70 waits for issue 6.

**Docs.** `materials/volume/README.md`, `verification/volume-balance-report.md`.

## 2. Clicks: device and mixer disagree on the sample count

**Observed.** "Clicks and noise at the end" of two recordings of the Moe-bius tune. The emulator's end-state capture had one exactly-zero sample every 6 frames (5419 samples at 44.1 kHz), sometimes 12, with the HQ chain on and off. Removing those samples left a −70 dBFS residual: the rest was clean. Under music the same dropouts are an 8 Hz click train heard as buzz/"HF modulation". Xpeccy Plus does not have it.

**Cause.** `SoundManager` picks the frame's sample count with an exact integer accumulator; the TurboSound devices filled their buffers from a free-running `double` PLL that also counted the last instruction's overshoot past the frame boundary twice. Frames where the device rendered 903 and the mixer consumed 904 read the frame-start memset zero as the 904th sample; the opposite case dropped a sample. `FrameSampleCount_Test`: 458 of 1500 frames disagreed at 44.1 kHz, 183 at 48 kHz, 345 at 96 kHz.

**Fix (uncommitted).** Both devices use the mixer's integer accumulator (`_samplePhase`), clip the counted T-states at `config.frame × speedMultiplier`, and restart the phase where the manager restarts its own (`reset()`, `setCoreRate()`). Files: `soundchip_turbosound.{h,cpp}`, `soundchip_turbosoundfm.{h,cpp}`; test `core/tests/emulator/sound/frame_sample_count_test.cpp`. Verified in the rebuilt app: 0 dropouts (17 before), residual −70 dBFS (−57 before). Bit-identity, multirate and adaptivity suites unchanged.

**Docs.** `verification/mixer-sample-count-clicks.md`.

## 3. CSM does not retrigger

**Observed.** User report "CSM is also broken, but much closer to the original". Standalone probe: with CSM on and Timer A every 40 samples, upstream ymfm's output is byte-identical to a manual key-on, a sustained tone.

**Cause.** Upstream ymfm keys channel 3's operators on at the overflow and never off again until the next `prepare()`, normally 4096 samples later; later overflows find the key already on and do not retrigger. Hardware (Nuked-OPN2): a one-sample key-on pulse per overflow, phase reset, attack restart, release. Timer A, not Timer B.

**Fix.** Furnace's three-line change in `fm_operator::prepare()` ported to the vendored ymfm (`279bbe2c`), canonical diff `verification/ymfm-furnace-csm.patch`, `PATCHES.md` §4. Test `TsfmTimer_Test.CsmRetriggersEveryTimerATick`; fails on pristine upstream, passes with the patch. No new state, TTD payload unchanged.

**Docs.** `verification/csm-and-furnace-ymfm-patches.md`.

## 4. HQ character chains ran with HQ off

**Observed.** With `soundhq` off (or the turbo override on) the five punch/room chains still processed every frame: an int16→float→int16 round trip plus per-sample DSP on AY 1/2, FM 1/2 and beeper.

**Fix.** Chains gated on the effective HQ flag; their delay lines and envelopes reset on the first HQ frame after a bypass (`7840d135`). Tests `SoundHQChainBypass_Test.*`.

## 5. Sub-audio FM content reaches the output

**Observed.** The Moe-bius tune ends with one FM carrier keyed at about 0.2–0.5 Hz at full carrier level. The emulator reproduces it as a full-level slow wave; a recording that stops mid-swing clicks at the cut, and any DC step in FM material is passed as is.

**Cause.** Faithful chip behaviour. The real board's output coupling capacitors (`C16/C17`, hardware-reference §5) remove it; the emulator's FM path has no high-pass by design ("no DC blocker on FM: the ymfm output is symmetric around zero", design §7.2).

**Status.** Open. A first-order high-pass on the FM buffers matching the board's coupling would hide it. The capacitor values are not in the hardware reference; a cutoff of about 5–10 Hz is the usual figure for such a stage. Not part of any fix so far.

## 6. int16 mix has no headroom for loud FM

**Observed.** After issue 1, one carrier reaches 0.18 per channel. A YM2203 channel can exceed one carrier (up to four in algorithm 7), each chip has three channels, and there are two chips plus two SSGs: full-level material exceeds ±1.0 before the int16 clamp and clips.

**Status.** Open. Already listed in the implementation plan as "wide mix + soft limiter (TSFM makes int16 saturation likely)"; issue 1 makes it a prerequisite for moving the code baseline. Until then the trim is the safe knob and can be backed off from the Audio Settings dialog.

## 7. LQ → HQ switch replays stale decimator history

**Observed.** On a low-quality to high-quality switch both TurboSound devices output about 0.3 ms of pre-switch history: the FIR history ring is not fed in LQ mode.

**Status.** Open, minor (a tiny click on a manual quality toggle). `SoundHQChainBypass_Test.ChainsResetWhenHQReturns` measures around it deliberately. A fix is to clear the decimator rings on the switch.

## 8. SSG leakage symmetry

**Observed.** Hardware: SSG A leaks −50 dB into the right, SSG C leaks −21 dB into the left. Emulator: −19 dB both ways (pan 0.9/0.1). SSG B is 0.7 dB hotter relative to A than on the hardware.

**Status.** Open, cosmetic. The asymmetry points at the recording chain or cable path, not the mixer. Optional tweaks (B pan 0.46, opposite-side pan 0.09) are listed in the volume walkthrough and not recommended.

## 9. Furnace's frequency-latch semantics

**Observed.** Furnace's ymfm copy arms the F-number latch on a high-byte write and commits on the low-byte write only when armed, then clears the latch. Upstream ymfm and Nuked-OPN2 keep a persistent latch and commit on every low-byte write.

**Status.** Reviewed, not adopted: it came with Furnace's initial ymfm import in 2021 with no hardware rationale, and it would change pitch for software that rewrites only the low byte. Recorded with the other non-ported Furnace changes in `PATCHES.md`. Revisit only with hardware evidence.

## 10. Chip-to-chip level difference

**Observed.** On the real board chip 1's FM is 0.8 dB quieter than chip 0's; SSG levels are equal.

**Status.** Not modelled; ordinary part variation.

## 11. Replay test reads an ignored scratch file

**Observed.** `TsfmVolumeReplay_Test` reads `scratch/tvtest/tfmd_p10.bin`, which is gitignored, so a clean checkout would fail the test.

**Status.** Mitigated: the missing file now skips the test instead of failing (`f342c233`). A cleaner version would read the stream from the committed `materials/volume/tsfm_volume_test.spg` at offset `0x600`.

## 12. App exits when its binary is rebuilt underneath it

**Observed.** Rebuilding `cmake-build-release` while the emulator app was running replaced its executable and the process exited (it had to be relaunched twice during this work). Instances and their state are lost.

**Status.** Noted for anyone measuring in the app while rebuilding: stop the app first, or build into a different directory.

## 13. Moe-bius tune leaves an FM channel keyed after it ends

**Observed.** The FM2 meter keeps moving after the demo ends. State report (`state audio fm 1`): chip 1, channel 2, register 0x28 last wrote slot S4 keyed and never a key-off; S4 sits in `decay` at attenuation 0 with `decay_rate` 0 (the decay never advances), TL 0, MUL 0, block 0, fnum 0x16 — a full-level carrier at 0.25 Hz. Chip 0's channel 1 is also still keyed at ~44 Hz with quiet carriers. The player's stop routine mutes the SSG (R7 = 0x3F, volumes 0) and never touches the FM side.

**Cause.** The tune. A real YM2203 holds the note the same way; the board's output coupling hides the 0.25 Hz swing (issue 5).

**Status.** Documented. The FM state report (issue 14) is how this was established and is the tool to check any similar report.

## 14. Device state not reachable from the automation interfaces

**Observed.** Analysing issue 13 needed a throwaway test to read the FM registers and envelope states; the WebAPI had AY state only, Lua had none, MCP `inspect_state` had no audio or disk aspects, and nothing exposed the WD1793.

**Fix (uncommitted).** Ground rule adopted: every device state useful for analysis is built once in the core (`core/src/emulator/state/devicestate.h`: `Ay`, `AyChip`, `Fm`, `FmChip`, `Fdc`) and rendered by every interface through one generic converter each. WebAPI `GET /state/audio/fm[/{chip}]`, `GET /state/fdc` (AY endpoints now serve the same core report); Lua `audio_ay_state`, `audio_fm_state`, `fdc_state`; Python the same names; CLI `state audio fm [N]`, `state fdc`; MCP `inspect_state` aspects `audio_ay`, `audio_fm`, `fdc`. Tests: `DeviceState_Test.*` (content), `McpTools_Test.InspectState_Device*` (aspect fan-out and summaries). Docs: command-interface.md §3.3 and the per-interface documents.

## 15. Fresh floppy drives reported a random head track

**Observed.** The first `state fdc` report on a paused demo showed head positions 53, 70, 80 and 32 on four drives that never had a disk.

**Cause.** `FDD::FDD` seeded `_track` from `std::random_device` ("unknown position like a real drive"), so every untouched drive carried garbage into state reports and TTD snapshots; the TTD serializer tests had to force known tracks to be deterministic.

**Fix (uncommitted).** The constructor sets track 0; a fresh drive reports track 0, bottom side, motor off, no disk, not write-protected. `DeviceState_Test.FdcReportListsControllerAndDrives` pins that for all four drives and the controller (drive 0, side 0, track 0, sector 1, DRQ/INTRQ low). 267 disk/TTD tests unchanged.
