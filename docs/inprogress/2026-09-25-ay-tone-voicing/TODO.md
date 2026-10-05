# TODO — AY tone voicing

**Status:** implemented 2026-09-27 (phases 1, 2, 4); phase 3 and the listening checks remain. What landed and how it was verified: TDD §14. Permanent doc: [docs/emulator/design/audio/ay-tone-voicing.md](../../emulator/design/audio/ay-tone-voicing.md).

Adds a switchable voicing stage for AY/SSG output (`flat` = hardware, `classic` = the `4c49fb6`
bass balance), because the 5 Hz coupling high-pass from `45812176` made the bass much heavier.
The original proposal (translated, with commit references) is [proposal.md](proposal.md); the reviewed design and phased plan are [ay-tone-voicing-tdd.md](ay-tone-voicing-tdd.md) (§10).

Remaining:

- [x] Phase 1 — `FilterVoicing` + `SoundManager` wiring (two-frame pre-roll + crossfade), `[SOUND] AYVoicing`, tests incl. full audit list, benchmark, DSD-is-flat note
- [x] Phase 2 — WebAPI/CLI/Lua/Python `ay_voicing`, Qt `EmulatorOrigin` + combo + QSettings, optional DSD-tap voicing (videowall: no changes, uses the active emulator's config) (+ optional punch/room persistence)
- [ ] Phase 3 — `tv` curve by listening. Started 2026-09-27: `tv` is visible with the design starting point (HPF2 130 Hz + LPF2 6 kHz, Q 0.7071, magnitude-matched low-pass so the curve is the same at every core rate 44.1–192 kHz; `FilterVoicing_Test.TvMatchesDesignCurveAtEveryCoreRate`). Remaining: the listening pass, then adjust the row in `filtervoicing.h`
- [ ] Headphones (`headphones`: Classic bass + critically damped 10 kHz low-pass) and Small speaker (`small_speaker`: HPF2 250 Hz + 1.5 kHz +3 dB peak + LPF2 4.5 kHz) added 2026-09-27 at the user's request, visible, same curve at every core rate (`FilterVoicing_Test.EveryPresetMatchesDesignCurveAtEveryCoreRate`). Remaining: the listening pass for both, together with `tv`
- [ ] Warm (`warm`: HPF2 90 Hz Q 0.6 + critically damped LPF2 8 kHz, between Headphones and TV speaker) added 2026-09-27; Headphones was the built-in default 2026-09-27 to 2026-10-05; **Classic is the default again** since 2026-10-05 (`FilterVoicing::DEFAULT_PRESET`, owner decision). Profiles listed in UI order: flat, classic, headphones, warm, tv, small_speaker. Remaining: listening pass
- [x] Defaults since 2026-09-27: `ay_room` = `9db` (`SoundManager::DEFAULT_AY_ROOM`, was off); the Qt label is "EQ profile" (was "Bass voicing")
- [x] Phase 4 — permanent doc, recipe line (the folder flips to `DONE.md` once phase 3 lands)
- [ ] A/B listening vs a `d90421bb` build (bass-heavy tune, volume-envelope "barrels", TSFM); decide R1 (punch with `flat`)
