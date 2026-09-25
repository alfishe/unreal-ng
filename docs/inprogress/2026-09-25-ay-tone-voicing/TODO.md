# TODO — AY tone voicing

**Status:** design reviewed three times against `af072f83` (TDD §13), ready for phase 1 implementation and testing; nothing implemented yet.

Adds a switchable voicing stage for AY/SSG output (`flat` = hardware, `classic` = the `4c49fb6`
bass balance), because the 5 Hz coupling high-pass from `45812176` made the bass much heavier.
The original proposal (translated, with commit references) is [proposal.md](proposal.md); the reviewed design and phased plan are [ay-tone-voicing-tdd.md](ay-tone-voicing-tdd.md) (§10).

Remaining:

- [ ] Phase 1 — `FilterVoicing` + `SoundManager` wiring (two-frame pre-roll + crossfade), `[SOUND] AYVoicing`, tests incl. full audit list, benchmark, DSD-is-flat note, A/B listening vs `d90421bb`
- [ ] Phase 2 — WebAPI/CLI/Lua/Python `ay_voicing`, Qt `EmulatorOrigin` + combo + QSettings, optional DSD-tap voicing (videowall: no changes, uses the active emulator's config) (+ optional punch/room persistence)
- [ ] Phase 3 — `tv` curve by listening
- [ ] Phase 4 — permanent doc, recipe line, flip to `DONE.md`
