# Verification record — turbo ⇄ status-bar CPU frequency chain

Date: 2026-09-11. Binary: `cmake-build-release/bin/unreal-qt.app` (working tree,
not yet committed). Model: `PROFSCORP`, image `data/rom/scorp_prof401.rom`.

Reported symptom: *changing CPU frequency in the ProfROM service monitor does not
change the status bar from 7.0 to 3.5 MHz and back — it always shows 7.0 MHz.*

## 1. Chain audit (port strobe → Z80 core → MessageCenter → status bar)

| # | Link | Verdict | Evidence |
|---|------|---------|----------|
| 1 | Guest `IN` strobe → decoder flip-flop | OK | `Z80::in()` routes every read to `PortDecoder_Scorpion256::DecodePortIn`; `(port & 0xC023) == 0x4021` (`#7FFD` family) sets `scorpion_turbo`/`hw_turbo_shift`, `== 0x0021` (`#1FFD` family) clears them. `OUT` never clocks the flip-flop (hardware-reference 13). Tests `ScriptedInSevenFFDSetsTurbo`, `ScorpionPorts_Test` mirror table. |
| 2 | Decoder → Z80 core application | OK | `DecodePortIn` calls `Z80::ApplyHardwareTurboNow()` immediately: composes `next_z80_frequency_multiplier << hw_turbo_shift`, rescales in-frame `t`/`eipos`/`haltpos`, refreshes frame geometry. Test `TurboStrobeAppliesMidFrame` (2↔1 mid-frame, raster instant preserved). |
| 3 | Z80 core → MessageCenter | **Gap, fixed** | `ApplyQueuedFrequencyMultiplier()` posted `NC_CPU_FREQ_CHANGED`, but `ApplyHardwareTurboNow()` did not — and a mid-frame strobe never passes a frame boundary, so the queued path sees *no change* next frame and posts nothing. Fix: shared `Z80::NotifyCPUFrequencyChanged()` called from both apply paths. |
| 4 | MessageCenter → status bar | OK | `StatusBarManager` observes `NC_CPU_FREQ_CHANGED` (payload-filtered by emulator id) **and** polls `emulatorState.current_z80_frequency` every 200 ms in `refresh()` — the poll is why the label was accurate *when the state actually changed*; link 3 only affected event-driven consumers. |

## 2. Why the status bar stays 7.0 *inside* the monitor — ROM behavior, not an emulator defect

Cross-reference: `profrom-service-monitor-turbo.md` §3.1, §3.7, §4 (facts below are
established there; not re-derived here).

| Action in the service monitor | What the ROM actually does | Clock effect |
|---|---|---|
| Enter the interactive monitor (NMI → `#0AF2`) | `CALL #04D5` → `IN A,(#7FFD)` ×2 — unconditional | **7 MHz always**, by design |
| Toggle "Computer speed" (`#02D1`/`#02D7`, codes `#87`/`#88`) | `SET/RES 6,(#E02D)` — a RAM flag store, gated by bit 7 | **none** — no port access at all |
| Exit to user program (`#053F` / `#1096` → `#04CE`) | `IN A,(#7FFD)` or `IN A,(#1FFD)` ×2 **per `#E02D` bit 6** | switch happens **here** (7 → 3.5 with speed OFF) |
| Cold reset | `#0676` strobes ON; boot stub re-detects | 7 MHz again once detection completes |

Practical consequence: on the hardware the monitor UI itself runs at 7 MHz — the
"Computer speed" item changes a stored setting, and the new clock takes effect at
monitor exit (and after save/load, at next boot via the page-6 config staging). A
live 7.0 ⇄ 3.5 flip *while sitting in the menu* is not a behavior the ROM produces,
so the status bar holding 7.0 there is hardware-matching.

## 3. The defect that was real: missing notification on the mid-frame apply path

`ApplyHardwareTurboNow()` is the only apply path for guest strobes (boot detection
flip, monitor entry `#0AF2`, exit `#04CE`). Until this fix it updated
`current_z80_frequency`/`_multiplier` silently. Impact matrix:

| Consumer | Before | After |
|---|---|---|
| Qt status bar label | stale up to 200 ms (poll) | immediate on message (poll unchanged as fallback) |
| Any `NC_CPU_FREQ_CHANGED` observer (WebAPI/MCP event listeners, future UI) | never informed of hardware turbo switches | informed at the strobe, same payload shape as host speed changes |

Fix (working tree): `Z80::NotifyCPUFrequencyChanged()` in `z80.h`/`z80.cpp`, called
from both `ApplyQueuedFrequencyMultiplier()` and `ApplyHardwareTurboNow()`. A no-op
strobe (multiplier unchanged) stays silent.

## 4. Live E2E (WebAPI, real app)

Sequence on a fresh PROFSCORP instance; multiplier read from
`GET /api/v1/emulator/{id}/video/beam` → `frame_timing.frequency_multiplier`:

| Step | Guest action | Multiplier | Matches |
|---|---|---|---|
| Boot 60 frames | boot stub detection (`#025E`) completes | **2** (7 MHz) | status bar "7.0 MHz" at boot |
| Inject at `#8000`: `LD BC,#1FFD / IN A,(C) ×2 / JR $` (the `#04CE` OFF sequence), pc=#8000, run 2 frames | exit-path speed-off strobe | **1** (3.5 MHz) | status bar flips after monitor exit |
| Overwrite `BC=#7FFD` variant, pc=#8000, run 2 frames | exit-path speed-on strobe | **2** (7 MHz) | round trip |

Note for reproduction: `PUT /registers/PC` takes a **numeric** `value`
(`{"value": 32768}`); a string value silently writes 0. Park loops, not `RET`.

### 4.1 ROM-native apply (2026-09-11, follow-up session)

Stronger variant — no injected code, the ROM's own apply routine executes the
switch. Fresh instance, boot → timeout to BASIC → NMI into the monitor, then the
speed byte staged to `normal` (`#E02D := #E02D & #BF`, byte-identical to the
`#02D7` action) and `PC := #04CE` (the exit-path apply helper, called at `#053D`):

- port trace: `IN #1FFD` ×2 at pc `#04DD`/`#04DF` — exactly the two `IN A,(C)`
  instructions of `#04CE`;
- `frequency_multiplier` 2 → 1 **in the same frame** the strobe executes;
- the switch **sticks** (still 1× after 15 frames, no re-force).

Boot strobes observed live the same way (multiplier 1→2 at frame 8, 2→1 at 38,
1→2 at 41 on one boot; 2→1 at 0, 1→2 at 11 on another) — every ROM-issued
switch passes through the same decoder → apply → state path.

Correction to an earlier suspicion: the "re-forced ON ~12 frames after the
monitor exit" seen in an earlier synthetic run was an artifact of entering
`#04E2` with a synthetic stack — the exit fell into the `#015C` boot-exit
continuation (`JP #000B`), which re-derives the config and re-strobes ON. The
real apply (`#04CE`) leaves the machine at the staged speed.

### 4.2 Pitfall — WebAPI keyboard injection requires the running loop

`POST /keyboard/tap` queues a sequence in `DebugKeyboardManager`; the queue is
pumped by `KeyboardManager::OnFrame()` from **MainLoop's** frame-end. The
`POST /run_frames` stepping path (`Emulator::RunNFrames`) drives the Z80 directly
and **bypasses that pump** — taps queued while stepped silently never deliver.
Several earlier "key X does nothing in the monitor" observations (including a
false `V`-does-not-work) were caused by this; re-tested with the instance
running (`POST /resume`), `V` toggles `#E02D` bit 6 immediately (no strobe — the
hotkey only stages the flag, matching the `#02D7` analysis). Any scripted
keyboard automation must `resume` before tapping and `pause` to inspect.

## 5. Test coverage added

`ScorpionMachine_Test.TurboStrobePostsCpuFreqChanged`
(`core/tests/emulator/ports/models/scorpionmachine_test.cpp`):
ON strobe → payload multiplier 2 / `base×2` Hz; OFF strobe → 1 / base Hz;
repeat OFF (no change) → no additional message.

Full suite status (2026-09-11): everything green except 4 tests in the TTD
family (`TTDChipsetStateTest.Capture*_PortLatches`, `TTD_Divergence_Corpus_Test.*`),
which are broken by a separate, in-progress TTD peripheral-registry refactor
already sitting in the working tree before this verification — unrelated to the
turbo chain (no TTD file is touched by this fix). Zero build warnings.

## 6. Side-track closed — SMUC board presence (2026-09-11)

An intermediate hypothesis held the "always 7 MHz" symptom came from the ProfROM
re-deriving its config to FAST defaults because the SMUC board (whose NVRAM
stores the setup) is absent. A `smucboard` runtime feature was prototyped
(board present by default) and verified to change the boot/monitor flow — then
**reverted in full**: the task was re-scoped to the notification chain only
(the ROM-issued switch demonstrably reaches the status bar), and shipping with
the SMUC board absent remains the standing decision
(`profrom-smuc-not-found-and-driver-disassembly.md` §8.2). The persistence
question stays open as a ROM-behavior topic, not an emulator defect.

## 7. Expected UX summary

- Status bar **inside** the service monitor: 7.0 MHz regardless of the setting —
  hardware-matching (monitor UI forces turbo on at `#0AF2`).
- Status bar **after exiting** the monitor (speed OFF): 3.5 MHz within one poll
  interval / immediately on the notification.
- Status bar after cold reset: 7.0 MHz once boot detection completes (~frame 45).
