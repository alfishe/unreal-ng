# TurboSound FM (2×YM2203)

**Status (2026-09-12):** design **verified and revised (rev. 2)**, ready for implementation. No production code yet.

TurboSound FM is the NedoPC board: two Yamaha YM2203 chips in the AY socket. Each chip is an AY-compatible square-wave generator plus a three-voice FM synthesizer. In unreal-ng it is an **optional in-place replacement** for TurboSound, selected per machine config, available on every model, with full time-travel (TTD) support. ymfm is the FM chip model.

## Documents

| File | What |
|---|---|
| [hardware-reference.md](hardware-reference.md) | What the board does, from its own logic source, schematic and player code. Normative. |
| [tsfm-tdd.md](tsfm-tdd.md) | Technical design, rev. 2 |
| [implementation-plan.md](implementation-plan.md) | Phases P0–P8 with files, tests and gates |
| [verification/verification-report.md](verification/verification-report.md) | Every rev. 1 claim checked against code, ymfm and hardware; measurements |
| [verification/ymfm-ttd.patch](verification/ymfm-ttd.patch), [stress.cpp](verification/stress.cpp), [bench.cpp](verification/bench.cpp) | ymfm TTD patch plus the programs that prove it and measure cost |
| [materials/](materials/) | NedoPC board logic source (2006, 2022) and rev. C schematic |

## Requirements

1. **Either TurboSound or TSFM**, never both, in one emulator instance.
2. **Chosen in the machine config** (`[SOUND] TurboSound = AY | FM`, default `AY`). Never switched at runtime.
3. **Any model** that has TurboSound today.
4. **Full TTD:** chip state saves and restores at any point, and replay reproduces the original run exactly.

## What verification changed

Revision 1 had three problems that would each have broken a requirement or the guest-visible behaviour:

1. **Time-travel could not have worked with upstream ymfm.**
   - Saving ymfm's state changes the chip's later output. A recording saves a checkpoint at every frame and a replay doesn't re-save, so the replay drifts from the recording, and recording itself changes the sound.
   - Cause: save and restore force a cache refresh that also advances key-on state, and the refresh schedule isn't saved.
   - Fix: a 38-line local patch. Measured: 0 differences over 24 M steps, including save and restore at every step. Upstream differs on every seed.
2. **The chip's busy flag and timers stopped whenever audio wasn't being produced** (turbo mode, sound off).
   - Real TFM players wait on the busy flag, so a player would hang in turbo, and replay would depend on audio settings.
   - Fix: split the device into a **chip core** that always runs on the CPU T-state clock, and an **output stage** that can be skipped.
3. **The port protocol was copied from the MiSTer FPGA core, which differs from the real board.** The board's logic source shows:
   - reset selects the `0xFE` chip, not `0xFF`;
   - register addresses are never blocked in AY mode;
   - control words never touch the chip's register latch.

   TFM Compiler's player code agrees.

Smaller corrections (details in the verification report):
- **Clock:** the board clock is 2 × the host AY clock, and in emulated time that is exactly 1 T-state everywhere.
- **Prescaler:** every player writes the prescaler registers at init; the SSG multipliers are ×2/×4.
- **Timer B:** it lives at register `0x26`.
- **FM level:** one FM carrier is a quarter of full scale.
- **Gain:** the default comes from the schematic's resistor values, not from a guess.
- **Config key:** the obvious existing key `[AY] Chip=YM2203` is already in every shipped ini and must not be used.
- **Performance:** the cost budget went from ≤ 60 µs to 58–126 µs per frame, measured.

## Open questions

| # | Question | Default taken |
|---|---|---|
| H1 | Real FM-to-SSG loudness of the rev. C board | Estimated from schematic and MiSTer (FM full scale = 2 × one SSG channel), adjustable with `TSFM_FmTrimDb` |
| H2 | Register read while an FM address is latched | `0xFF` (MiSTer, Unreal, Xpeccy) |
| H3 | Does a real YM2203 lose writes made while busy? | Never drop |
| Q1 | ~~Add third-party TFM music to `testdata/`~~ | Approved and done: `testdata/sound/tsfm/` (see `SOURCES.md`) |
