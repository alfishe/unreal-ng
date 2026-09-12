# Verification record — turbo ⇄ status-bar CPU frequency chain

Date: 2026-09-11. Binary: `cmake-build-release/bin/unreal-qt.app`.
Models: `SCORPION` (`data/rom/scorpion.rom` v2.92) and `PROFSCORP` (`data/rom/scorp_prof401.rom` v4.01).

Reported symptom: *changing CPU frequency in the service monitor (NMI -> V keypress or
cursor keys + Enter) updates the on-screen menu to "normal (3.5Mhz)", but the status bar
still displays 7.0 MHz.*

## 1. Chain audit (port strobe → Z80 core → MessageCenter → status bar)

| # | Link | Layer | Verdict | Implementation / Evidence |
|---|------|-------|---------|---------------------------|
| 1 | Guest `IN` strobe → decoder flip-flop | Guest ROM & Port Decoder | OK | `Z80::in()` routes every port read to `PortDecoder_Scorpion256::DecodePortIn`; `(port & 0xC023) == 0x4021` (`#7FFD` family) sets `scorpion_turbo` / `hw_turbo_shift = 1`, `== 0x0021` (`#1FFD` family) clears them to `0`. `OUT` never clocks the flip-flop (hardware-reference 13). Tests: `ScriptedInSevenFFDSetsTurbo`, `ScorpionPorts_Test`. |
| 2 | Decoder → Z80 core application | CPU Core | OK | `DecodePortIn` calls `Z80::ApplyHardwareTurboNow()` immediately: composes `next_z80_frequency_multiplier << hw_turbo_shift`, rescales in-frame raster instant `cpu.t`, `eipos`, `haltpos`, updates `current_z80_frequency_multiplier` and `current_z80_frequency`, and calls `RecomputeFrameTiming()`. Tests: `TurboStrobeAppliesMidFrame`, `ProfRomBootDetectsSevenMhz`. |
| 3 | Z80 core → MessageCenter | Event Bus | OK (fixed in `bf269540`) | `Z80::NotifyCPUFrequencyChanged()` posts `NC_CPU_FREQ_CHANGED` (`"CPU_FREQ_CHANGED"`) with payload `CPUFreqPayload(emulatorId, frequencyHz, multiplier)`. Shared by both `ApplyQueuedFrequencyMultiplier()` and `ApplyHardwareTurboNow()`. Regression test: `ScorpionMachine_Test.TurboStrobePostsCpuFreqChanged`. |
| 4 | MessageCenter → Qt status bar | UI | OK | `StatusBarManager` observes `NC_CPU_FREQ_CHANGED` in `handleCPUFreqChanged()`, filters by emulator ID, and marshals to the Qt main thread via `QMetaObject::invokeMethod(..., Qt::QueuedConnection)`. Formats string (`"3.5 MHz"`) and resets orange style. Also backed by 200 ms polling in `refresh()`. |

## 2. Why the status bar stays 7.0 MHz *inside* the monitor — ROM behavior, not an emulator defect

On real Scorpion ZS-256 hardware, the Service Monitor UI itself **is hardwired to run at 7.0 MHz**
so all monitor utilities (screen redraws, memory viewers, disassembler, disk routines) operate at maximum speed.
The "Computer speed" menu option is **strictly a configuration staging setting** for the user program.

### Comparative disassembly: Base Scorpion (`scorpion.rom`) vs ProfROM (`scorp_prof401.rom`)

| Stage | Base Scorpion (`scorpion.rom` v2.92) | ProfROM (`scorp_prof401.rom` v4.01) | Physical Clock Effect |
|---|---|---|---|
| **Monitor Entry (NMI)** | `#040E` falls into `#0411`:<br>`LD B,#7F; JR +4; LD C,#FD; IN A,(C); IN A,(C); RET`<br>(unconditional strobe) | `#0AF2` calls `#04D5`:<br>`LD B,#7F; JR +2; LD C,#FD; IN A,(C); IN A,(C); RET`<br>(unconditional strobe) | **7.0 MHz always** (hardware flip-flop forced ON for the monitor UI) |
| **Menu Toggle (`V` / Cursor + Enter)** | `#0244` (SET) / `#024A` (RES):<br>`CALL #0250` (tests bit 7 of `#DFF8`)<br>`SET/RES 6, (#DFF8)`<br>`RET` | `#02D1` (SET) / `#02D7` (RES):<br>`CALL #02DD` (tests bit 7 of `#E02D`)<br>`SET/RES 6, (#E02D)`<br>`RET` | **None — 0 port operations.** Only modifies a flag in RAM. The UI re-draws the menu label to `"normal (3.5Mhz)"`. |
| **Monitor Exit (`R` / Run)** | `#047B` / `#0FD9` calls `#048C`:<br>`LD BC,#1FFD; LD A,(#DFF8); RLCA; RLCA; JR NC,+4; LD B,#7F; IN A,(C); RET` | `#053F` / `#1096` calls `#04CE`:<br>`LD A,(#E02D); BIT 6,A; JR Z,+4; LD B,#7F; ... IN A,(C) ×2; RET` | **Physical switch occurs here:** If bit 6 was cleared, executes `IN A,(#1FFD)` (7.0 → 3.5 MHz). |
| **Cold Reset** | Reset clears flip-flop; stub re-detects | `#0676` strobes ON; boot stub re-detects | 7.0 MHz again once detection completes |

### Detailed Disassembly Listings

#### ProfROM v4.01 (`data/rom/scorp_prof401.rom`, Page 2)
```z80
; Menu action handlers (Page 2)
02D1  CD DD 02     CALL #02DD          ; Enable turbo action
02D4  CB F6        SET  6,(HL)         ; Set bit 6 of (#E02D)
02D6  C9           RET
02D7  CD DD 02     CALL #02DD          ; Disable turbo action
02DA  CB B6        RES  6,(HL)         ; Clear bit 6 of (#E02D)
02DC  C9           RET
02DD  AF           XOR  A
02DE  21 2D E0     LD   HL,#E02D       ; Config byte (IY+$19)
02E1  CB 7E        BIT  7,(HL)         ; HW turbo present?
02E3  20 02        JR   NZ,#02E7
02E5  3C           INC  A              ; Refuse if absent
02E6  C1           POP  BC
02E7  32 7F DD     LD   (#DD7F),A      ; Status (0=OK)
02EA  C9           RET

; Exit apply routine (Page 2 #04CE)
04CE  3A 2D E0     LD   A,(#E02D)      ; Load staged config
04D1  CB 77        BIT  6,A            ; Turbo enabled?
04D3  28 04        JR   Z,#04D9
04D5  06 7F        LD   B,#7F          ; BC = #7FFD (7.0 MHz)
04D7  18 02        JR   #04DB
04D9  06 1F        LD   B,#1F          ; BC = #1FFD (3.5 MHz)
04DB  0E FD        LD   C,#FD
04DD  ED 78        IN   A,(C)          ; STROBE 1: hardware turbo clock switch!
04DF  ED 78        IN   A,(C)          ; STROBE 2
04E1  C9           RET
```

#### Base Scorpion v2.92 (`data/rom/scorpion.rom`, Page 2)
```z80
; Menu action handlers (Page 2)
0244  CD 50 02     CALL #0250          ; Enable turbo action
0247  CB F6        SET  6,(HL)         ; Set bit 6 of (#DFF8)
0249  C9           RET
024A  CD 50 02     CALL #0250          ; Disable turbo action
024D  CB B6        RES  6,(HL)         ; Clear bit 6 of (#DFF8)
024F  C9           RET
0250  AF           XOR  A
0251  21 F8 DF     LD   HL,#DFF8       ; Base Scorpion config byte
0254  CB 7E        BIT  7,(HL)         ; HW turbo present?
0256  20 02        JR   NZ,#025A
0258  3C           INC  A              ; Refuse if absent
0259  C1           POP  BC
025A  32 7F DD     LD   (#DD7F),A      ; Status (0=OK)
025D  C9           RET

; Exit apply routine (Page 2 #048C)
048C  01 FD 1F     LD   BC,#1FFD       ; Default BC = #1FFD (3.5 MHz)
048F  3A F8 DF     LD   A,(#DFF8)      ; Load staged config
0492  07           RLCA
0493  07           RLCA                ; Shift bit 6 into Carry
0494  30 02        JR   NC,#0498       ; If bit 6 == 0 -> leave BC = #1FFD
0496  06 7F        LD   B,#7F          ; If bit 6 == 1 -> BC = #7FFD (7.0 MHz)
0498  ED 78        IN   A,(C)          ; STROBE: hardware turbo clock switch!
049A  C9           RET
```

**Conclusion on UX:**
- Inside the menu, the virtual ZX screen changes to `"normal (3.5Mhz)"` because the menu code reads the updated RAM variable (`#E02D` or `#DFF8`).
- However, the Z80 CPU has not touched any I/O port, so the physical clock remains at 7.0 MHz.
- The Unreal status bar correctly reports the **actual running CPU frequency** (`7.0 MHz`).
- As soon as the user exits the monitor (e.g. key `R` for Run), `#04CE` / `#048C` executes `IN A,(#1FFD)`, and the status bar flips immediately to `3.5 MHz`.

## 3. End-to-end execution trace (when the strobe occurs)

```
[Guest Z80 Exit Routine]
   │  Executes IN A,(C) with BC = #1FFD (or #7FFD)
   ▼
[PortDecoder_Scorpion256::DecodePortIn]
   │  Masks (port & 0xC023):
   │    0x0021 (#1FFD) -> scorpion_turbo = 0, hw_turbo_shift = 0
   │    0x4021 (#7FFD) -> scorpion_turbo = 1, hw_turbo_shift = 1
   │  Invokes Z80::ApplyHardwareTurboNow()
   ▼
[Z80::ApplyHardwareTurboNow]
   │  desiredMultiplier = next_multiplier << hw_turbo_shift (e.g. 1 << 0 = 1)
   │  Rescales in-frame raster instant: cpu.t = cpu.t * desired / old
   │  Rescales eipos and haltpos
   │  Sets current_z80_frequency_multiplier = 1, current_z80_frequency = 3'500'000 Hz
   │  Calls RecomputeFrameTiming() (_frameLimit: 139'776 -> 69'888 T)
   │  Calls NotifyCPUFrequencyChanged()
   ▼
[Z80::NotifyCPUFrequencyChanged]
   │  Constructs CPUFreqPayload(emulatorId, 3'500'000, 1)
   │  Posts NC_CPU_FREQ_CHANGED via MessageCenter::DefaultMessageCenter()
   ▼
[StatusBarManager::handleCPUFreqChanged] (Core execution thread)
   │  Filters by emulator instance ID
   │  Extracts frequency (3,500,000 Hz)
   │  Marshals to Qt GUI thread via QMetaObject::invokeMethod(..., Qt::QueuedConnection)
   ▼
[Qt Main Thread Event Loop]
   │  Executes lambda in GUI thread context:
   │    _cpuFreq->setText("3.5 MHz");
   │    _cpuFreq->setStyleSheet("padding-top: 1px;"); // Clears orange highlight
   ▼
[User Interface]
      Status bar displays "3.5 MHz" in standard text color
```

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
