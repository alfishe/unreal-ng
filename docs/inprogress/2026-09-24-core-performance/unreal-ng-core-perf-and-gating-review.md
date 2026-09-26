# unreal-ng: core review of CPU load, peripheral gating and feature gating

**Date:** 2026-09-24
**Scope:** the emulation core (`core/src`): the frame and instruction hot path, the port and memory paths, the sound devices (AY/TurboSound, TSFM, General Sound, MoonSound), `FeatureManager` gating and per-frame overheads.
**Excluded:** the Qt/SDL clients, except where they configure the core (logging, port muting).

**Revisions reviewed**

| Ref | Commit | What it covers |
|---|---|---|
| `master` | `4c49fb62` | Shared core. Line numbers below refer to this unless noted. |
| `moonsound` | `009997c4` | master plus MoonSound/OPL4, the full-decode port claim and the wide mix/limiter. `[ms]` marks lines on this branch. |
| `generalsound` | `c9277ab3` | master plus the GS card (z80ex LLE and the LW personality). `[gs]` marks lines on this branch. |

Once both branches merge, the Pentagon/Scorpion/ATM configs enable **three** sound coprocessors by default: `GSType=Z80`, `TurboSound=FM` and `MoonSound=1`. The gating findings matter most for exactly that default setup.

**Evidence tags**

| Tag | Meaning |
|---|---|
| **[V]** | Checked by reading the code at the cited lines. |
| **[M]** | Measured on a standalone replica: the device or library hot path built with g++ `-O2` outside the emulator build. Treat as an order of magnitude, not an absolute number. |
| **[D]** | Taken from the project's own docs (`docs/inprogress/...`). |
| **[?]** | Plausible but not traced end to end. Verify before acting. |

**Constraints this review respects**

- **No predecode or recompilation.** It is not proposed; in-frame decode is about 20% of CPU time, and the dominant cost is per-step overhead.
- **The "Never fast-forward HALT" non-goal** (`2026-09-15-frame-budget-triage/05-optimization-roadmap.md` §Non-Goal) is respected. §C5 explains why part of its reasoning no longer matches the code.
- **Bit-exactness.** Every proposal below is either bit-exact against current behaviour, including TTD state and hash, or explicitly marked as not bit-exact.

---

## 0. Executive summary

| # | Finding | Impact | Effort | Section |
|---|---|---|---|---|
| 1 | **Merge `a921dc1b` (2026-08-19) silently reverted most of the 2026-08-04 performance round.** That round took the TTD-gaming frame from 913 µs to 481 µs. The reverted items are the SIMD FIR, the stereo decimator, audio batching, the AY micro-optimisations, the `ScreenZX::DrawPeriod` override and the SIMD 8-pixel `Draw`. Only the TTD page cache survived. | Up to about 400 µs per frame in that benchmark | Low–medium (re-land and re-bench) | §A |
| 2 | **Idle sound coprocessors run at full cost forever.**<br>• MoonSound: about 1.1–2.5 ms per frame idle **[M]**<br>• GS LLE: about 0.36 ms per frame idle **[M]**<br>• TSFM FM cores: advanced every instruction, not measured<br>• TurboSound: the second AY and its decimators are always ticked<br>That is roughly 7–15% of a 20.48 ms frame per instance doing nothing. | High (default configs) | Medium | §B |
| 3 | **Exact quiescence fast-forward is the right gating mechanism for GS and MoonSound.** It is cheaper and safer than "skip until first use". It covers both "never used" and "stopped using", is bit-exact, and needs no TTD format change. | High | Medium | §B.1–B.3 |
| 4 | **Correctness bugs found on the way:**<br>• GS loses time in turbo-without-audio<br>• MoonSound Authentic-mode stream grows without bound (about 282 MB per 10 minutes **[M]**)<br>• MoonSound `#7F` reads are not gated on NEW2, so every TR-DOS data read drives an OPL4 sync<br>• Turning debugmode or TTD off mid-recording corrupts TTD history<br>• Port breakpoints fire even with the breakpoint features off | Correctness | Low each | §B, §F |
| 5 | **Per-instruction peripheral stepping (`OnCPUStep`) is the largest structural cost.** It is 6–9 out-of-line or virtual calls per instruction. Most devices are already catch-up capable, so sync-on-access plus frame-end sync is **bit-exact** if the sync target is the *instruction-start* T-state. | High (large share of the per-step overhead) | High (staged) | §C.2 |
| 6 | **Memory accesses cost two non-inlinable calls per byte.** `Z80::rd/wd` sit in another TU from the opcodes, then call a pointer-to-member `MemIf`. There is no LTO. | Medium–high | Low–medium | §C.1 |
| 7 | **Port I/O path overheads:**<br>• Every non-muted port builds a `std::string` log line that is then discarded (the Qt app runs at `LogInfo`)<br>• 2–3 `std::map` lookups per IN/OUT<br>• Ungated debug hooks | Medium (disk and loader-heavy code) | Low | §D |
| 8 | **Per-frame overheads:**<br>• ~~TurboSound frame-end runs twice~~ **[DONE 2026-09-24]**<br>• Shared memory does `msync(MS_SYNC)` twice per frame<br>• Heap payloads and string copies per frame<br>• Float peak meters run on silent buffers | Low–medium | Low | §E |

**Suggested order:** see §I.

---

## 1. Where the time goes today

### 1.1 Numbers already on record [D]

**Interactive `unreal-qt`, average per frame** (`2026-09-15-frame-budget-triage/01-evidence-and-deductions.md`):

| Stage | µs per frame |
|---|---|
| Z80 | ~1,210 |
| Screen stepping | 1,354 |
| I/O stepping | 670 |
| Sound stepping | 1,747 |

- **Peripheral stepping is about 3× the Z80 itself.**
- Steps per frame range from 12.2k to 17.9k, with HALT-heavy frames at the top.

**Other figures:**

| Source | Figure |
|---|---|
| Your benchmark (project notes) | About 53 ns per instruction in-frame vs about 10 ns isolated. Decode is only about 20% of the work; the rest is per-step overhead. |
| Headless `BM_CovoxDemoFrame_CoreRate` | 2,971 µs per frame (`04-benchmark-evidence.md` §2) |

### 1.2 What runs per instruction (`Z80::Z80FrameCycle`, `z80.cpp:547-571`) [V]

| # | Call | Where | Kind |
|---|---|---|---|
| 1 | `Emulator::IsPaused()` | `z80.cpp:555` → `emulator.cpp:3373` | Out-of-line call, `volatile bool` |
| 2 | `ProcessInterrupts()` | `z80.cpp:561`, body `850-934` | Out-of-line; touches `_context->pScreen->_vid` every step |
| 3 | `Z80Step()` | `z80.cpp:171-425` | See 1.3 |
| 4 | `Z80::OnCPUStep()` → `MainLoop::OnCPUStep()` | `z80.cpp:1037-1043` → `mainloop.cpp:450-473` | 2 out-of-line calls plus 3 null checks |
| 5 | `Screen::UpdateScreen()` → `GetCurrentTstate()` → `DrawPeriod()` → `Draw()` per T-state | `mainloop.cpp:467`, `screenzx.cpp:700-710`, `screen.cpp:1205-1297` | Virtual, then virtual per T-state |
| 6 | `WD1793::handleStep()` | `mainloop.cpp:470`, `wd1793.cpp:3179-3208` | Out-of-line; returns early when asleep |
| 7 | `Tape::handleStep()` | `mainloop.cpp:471`, `tape.cpp:681-702` | Out-of-line; returns early when stopped |
| 8 | `SoundManager::handleStep()` → `_turboSound->handleStep()` | `mainloop.cpp:472`, `soundmanager.cpp[ms]:542-554` | 2 calls; the device renders the span `[last, now]` |

### 1.3 Inside `Z80Step` and the bus [V]

**`Z80Step` (`z80.cpp:171-425`) does, every instruction:**
- a TR-DOS flag test;
- `isDebugMode`;
- `_context->pDebugManager->GetAnalyzerManager()->hasCPUStepSubscribers()`, where `GetAnalyzerManager` is out-of-line (`debugmanager.h:52`);
- 3 PC-trap compares;
- the calltrace and profiler bools;
- the Q update;
- `cycles_to_capture`.

**`m1_cycle` (`z80.cpp:578-643`) checks:**
- `m1TraceHook` (a `std::function`);
- `ttdCoverageActive`;
- `ttdProbe.IsArmed()`.

**`rd/wd` (`z80.cpp:649-700`) do, per byte:**
- a contention pointer check;
- the cycle increment;
- a pointer-to-member call into `Memory::MemoryReadFast`/`MemoryWriteFast` (`memory.cpp:176`, `287`), in another TU;
- a `busTraceHook` (`std::function`) check.

**Why none of this inlines:**
- The opcode TUs (`op_*.cpp`) call `cpu->rd()`/`cpu->wd()`, which are defined out of line in `z80.cpp` (declared at `z80.h:375-376`).
- There is no IPO/LTO in any CMake file.
- So every memory byte is at least **2 non-inlinable calls**.

---

## A. Optimizations lost in merge `a921dc1b` (P0, verify first)

### A.1 Evidence [V]

Commit `6febbd4f` (2026-08-04, "Performance: 47% frame time reduction (913µs → 481µs)") is an ancestor of `master`, but most of its changes are gone. The merge `a921dc1b` ("merge: integrate master into time-travel", 2026-08-19 14:12) took the side without them. The table checks for each change's marker in merge parent 1 (`52f28a21`), in the merge result, and on master:

| Change from 6febbd4f | Marker checked | Parent 1 | Merge | master |
|---|---|---|---|---|
| Stereo interleaved decimator (2 filters instead of 4) | `filter_decimator_stereo.h` exists | yes | **no** | **no** |
| SIMD FIR, NEON/SSE2, 8-wide with 4 accumulators | `__ARM_NEON` in `filter_decimator.h` | yes | **no** | **no** |
| Branchless AY mixer and inlined generators | `INV_3` in `soundchip_ay8910.cpp` | yes | **no** | **no** |
| Audio batching at 80 T-states | `setBatchInterval` in `soundmanager.h` | yes | **no** | **no** |
| `ScreenZX::DrawPeriod` override (non-virtual `Draw`) | the override in `screenzx.h` | yes | **no** | **no** |
| SIMD 8-pixel screen-area writes in `Draw` | `vst1q_u32` / `_mm_storeu_si128` in `screenzx.cpp` | yes | **no** | **no** |
| `IncrementCPUCyclesCounter` inlined in the header | inline body in `z80.h` | yes | reverted to a `.cpp` body | `.cpp` body (`z80.cpp:1050`) |
| TTD previous-page cache (`InternXorCached`) | present | yes | yes | yes |

The 2026-08-04 doc also lists "Inlined DrawPeriod" as −11.6% (578 → 511 µs), and SIMD FIR as −28%, as done. The code no longer agrees with it.

### A.2 Why this probably happened, and how to re-land

**Likely cause.** On 2026-08-18, `674bd5ab` ("universal core sample rate support … runtime filter design") rewrote `FilterDecimator`, and `2f9b1888` (2026-08-09) added border 4T latching and attribute latching to `ScreenZX::Draw`. When the merge conflicted, it resolved to the newer master side.

**What that means per item:**
- Some losses were inevitable: the SIMD FIR has to be rebuilt around the runtime-designed coefficients.
- Others were collateral:
  - The `DrawPeriod` override and the AY micro-optimisations don't depend on either rewrite.
  - Audio batching may have been dropped on purpose, if the multirate/DRC path relies on per-step rendering. It is worth confirming.

**Recommendation, one item at a time, each with its own benchmark delta** (`core/benchmarks`, TTD-gaming plus the CoreRate covox demo):

1. **`ScreenZX::DrawPeriod` override.**
   - Loop over `[from, to]` calling `ScreenZX::Draw` non-virtually, or make `Draw` `final` if the test subclass `ScreenZXCUT` (`screenzx.h:230`) doesn't override it.
   - Hoist the per-T-state `frameLimit`/mode test (`screenzx.cpp:719-724`) out of the loop.
   - It must keep the new latching semantics: `_lastLatchSymbolX` and friends are per-T-state state (`screenzx.cpp:764-775`).
   - Bit-exact.
2. **SIMD FIR on the runtime-designed decimator.**
   - `FilterDecimator` now takes run-time tap counts: Reference is 96 and HighFidelity is 192, both multiples of 8. The 8-wide, 4-accumulator kernel with a double (mirrored) history buffer fits.
   - Floating-point summation order changes, so it isn't bit-exact. It's still an audio-only difference, within the float rounding of the scalar path, and doesn't touch TTD state (the decimators aren't in the CPU-visible state).
3. **Stereo interleaved decimator.** It needs porting to the new `configure(rate, quality, …)` API.
4. **AY branchless mixer and `INV_3`.** Re-apply to `soundchip_ay8910.cpp`; this is bit-exact if you keep the integer math.
5. **Audio batching (80T).** Re-evaluate only after §C.2. With catch-up sync, batching comes for free, since the AY renders only on sync points.
6. **SIMD 8-pixel screen writes.** Re-derive them on top of attribute latching. With 4T latching the 8-pixel cell is exactly one latch period, so it stays valid, but it needs a multicolor regression run.

**Process.** Run `git diff 52f28a21 a921dc1b -- core/` to audit what else that merge dropped. I only checked the perf items above.

---

## B. Peripheral gating: skipping emulation of unused sound hardware

### B.0 General design: a device activity contract

Today every device invents its own flags:
- `setSynthesisSuppressed` and `setCoreSynthesisSkipped` (TSFM);
- GS runs always;
- MoonSound runs always;
- WD1793 has `_sleeping`.

**Proposed shared contract (in `SoundDevice` or a small `IActivityGated` mixin):**

| State | Meaning | Cost per frame |
|---|---|---|
| `Absent` | Not configured (already handled by `nullptr`) | 0 |
| `Quiescent` | Chip state is a closed-form function of time: no audible output, no pending CPU-observable event other than the analytically computable ones | O(1) |
| `Active` | Full emulation | Full |

**Rules:**
1. **Quiescence is derived, never saved.** A device re-derives it after `TTDLoadState` and after reset. The TTD blob and hash stay identical to a full run, so `kTtdLayoutVersion` is unchanged.
2. **Wake triggers are the device's own port accesses.** They already sync first (`flush()` / `SyncTo()`). Only writes that change audible state wake it. Reads fast-forward and stay quiescent.
3. **The manager uses the state.**
   - No peak scan or mix for a device whose buffer is known to be all zero (`soundmanager.cpp[ms]:748-826`).
   - No HUD `AudioActivityPayload` unless the state changed.
4. **A device can re-enter `Quiescent` after use.** This is the "stopped using it" case, which "skip until first use" does not cover.

**Why not "skip until first port access" (true dormancy).**
- **GS:** the POST must still have happened by the time the Spectrum program first looks, and programs probe the GS during boot.
  - For example, BUG-6 in the GS docs describes a probe during POST.
  - Stray `OUT (#F3BB)` aliases also show up.
  - A dormant card would need a cached post-POST snapshot keyed on ROM, RAM size and prior RAM contents (§B.1.4), plus materialize-on-save for TTD, where `TTDSaveState`/`TTDHashState` are `const`.
- **MoonSound:** "never written" equals the reset state, so dormancy is easy. But the same fast path also covers "went silent", so a separate dormant mode adds nothing.
- **Conclusion:** implement **exact quiescence**. "Never used" is simply its first instance.

### B.1 General Sound (`generalsound` branch)

#### B.1.1 Current cost and structure [V][M]

**No per-instruction work.** `SoundManager::handleStep` never touches `_gs`.

**Per frame:**
- **`handleFrameStart`** (`soundchip_gs.cpp[gs]:386-396`) only snapshots the time bases.
- **`handleFrameEnd`** (`:398-438`):
  - `runTo(frameEnd)` catches the GS Z80 up by a full frame;
  - then `blip_end_frame`/`blip_read_samples` ×2.
- **Port accesses** call `flush()` → `runTo(now)` (`:279-288`).

**GS cycles per frame:** 245,760 on Pentagon (12 MHz × 71,680 / 3.5 MHz), which is 768 INT quanta of 320 cycles (`generalsoundcard.h:61-63`).

**The idle loop.** After the POST, the firmware sits in `COMINT_` at ROM `0x026E` (gs104 and gs105a) with **DI**:
- `IN A,(FLAGS)` / `RRCA` / `JR C` / `LD A,(#4084)` / `OR A` / `JR Z`;
- 51 T per iteration, 6 M1 cycles, no writes, no DAC reads.

**Measured idle cost [M]** (standalone replica of `runTo`/`readMem`/`gsIn` linked against the vendored z80ex, with the real `gs105a.rom`):

| Per frame | Value |
|---|---|
| `z80ex_step` calls | ~28.9k |
| Memory-read callbacks | ~57.8k |
| FLAGS polls | ~4.8k |
| `z80ex_int` calls | ~28.9k, all refused |
| Time | **0.35–0.37 ms** |

- **The level-held INT with IFF1=0 is the reason for those refused calls** (`soundchip_gs.cpp[gs]:318-328`). `_intPending` stays true all the time and `z80ex_int(_cpu)` is called and refused before every instruction.
- **Missing ROM case.** The ROM is zero-filled and the CPU executes NOPs over all 64 KB, which fetch DAC bytes and call `emitSample`. That measured about **0.53 ms per frame [M]**, worse than a real idle card.
- **POST length.** The replica took about 0.536 s (26 Pentagon frames, 1.13M instructions) to reach `COMINT_`. The GS doc says about 0.27 s. Please re-check on the real build.

#### B.1.2 Recommendations

**G1 (P0): exact idle-loop fast-forward in `SoundChip_GeneralSound::runTo`** (`soundchip_gs.cpp[gs]:290-353`).

**At `loadROM`:** signature-scan for the `COMINT_` bytes `DB 04 0F 38 22 3A 84 40 B7 28 F5`. Store `_idleLoopPC`, or disable the fast path if the signature isn't found (e.g. `bootGS.rom` doesn't have it).

**Skip condition, checked at the loop head:**
- `PC == _idleLoopPC` and `prefix == 0`;
- `iff1 == 0` and `!_nmiPending`;
- `(_mb.status & 1) == 0`, i.e. no command pending;
- RAM[`#4084`] (PROCESS) `== 0`, read through the current bank mapping;
- `!_portTrace.isCapturing()`.

**Advance, for `n = floor((target - now) / 51) - 1` whole iterations:**
- total cycles += 51·n;
- `r` += 6·n (7-bit R semantics as z80ex keeps them; the TTD blob stores 16 bits);
- `cpuSteps` += 6·n;
- normalise the INT quantum by looping `while (_intQuantum >= 320)`:
  - `_gsCyclesAbs += 320`;
  - `interruptPeriods++`;
  - `interruptsCoalesced++`, because `_intPending` is already set;
- then run the tail normally, so `_intQuantum` and the loop phase end exactly where the full run would.

**Why this is exact:**
- The loop's only input is status bit0 plus PROCESS.
- Bit0 changes only on a host `OUT #BB`/`#33`.
- Every such path calls `flush()` first (`[gs]:535-560`, `:634-650`), and `#33` bit7 resets the PC.
- A, F, SP, MEMPTR, RAM, banking and the latches are invariant inside the loop.

**Effect:**
- Idle drops from about 0.36 ms per frame to O(1).
- The card also goes back to sleep on its own whenever the firmware returns to `COMINT_` with DI.
- **To verify:** that the firmware returns to DI with PROCESS=0 after playback stops. `QTFAULT` returns without `EI` (`INTTST.a80:160-166`), which suggests it does.

**G2: inline IFF guard before `z80ex_int`** (`[gs]:318`).

```cpp
if (_intPending && _cpu->iff1 && !_cpu->noint_once && !_cpu->prefix) { int t = z80ex_int(_cpu); ... }
```
- These fields are visible through the z80ex typedefs that `soundchip_gs.cpp` already includes.
- Measured −14% of idle cost **[M]**. It also helps DI stretches during playback.
- Bit-exact, since `z80ex_int` refuses under exactly these conditions. Double-check `int_vector_req`.

**G3: no-ROM guard.** When `!_romLoaded`, `runTo` only advances time and the INT bookkeeping and doesn't execute NOPs. This isn't a behaviour change: a card without a ROM is undefined anyway, and the warning is already logged.

**G4 (bug): time lost in turbo-without-audio.**
- `SoundManager::handleFrameStart` always calls `_gs->handleFrameStart()` (`soundmanager.cpp[gs]:523-527`, before `if (suppressed) return;` at `:529`). That rebases `_frameStartGsCycles = totalGsCycles()`.
- `MainLoop::OnFrameEnd` skips `SoundManager::handleFrameEnd` entirely in turbo without audio (`mainloop.cpp:558-574`), so `runTo(frameEnd)` never happens.
- Result: the GS only advances as far as the in-frame port accesses reach, and loses the rest of each frame.
- Consequences:
  - The POST stretches (which reopens the BUG-6 probe-during-POST race).
  - CPU-visible GS behaviour depends on the turbo setting.
  - This goes against your own "program-visible state must not depend on audio settings" comment (`[gs]:520-522`).
- **Fix options:**
  - Add a `GeneralSoundCard::advanceToFrameEnd()` (`runTo` without the blip drain) and call it from `SoundManager` when the frame-end is skipped, the same way MoonSound's D3 does in `handleFrameStart`; or
  - call `SoundManager::handleFrameEnd`'s device-advance part unconditionally.
- With G1 this is O(1) while idle.
- **Test:** GS status/data/RAM hash after N frames must be identical with turbo on and off.

**G5: silent-frame shortcut.** When the frame had no deltas and the blip tail has settled, skip `blip_end_frame`/`blip_read_samples`, zero the buffer (or flag it silent) and let `SoundManager` skip the mix. The audio is exactly zero either way.

**G6: TTD capture cost while recording.**
- Every checkpoint serialises the full GS RAM (128–512 KB): the vector is allocated, copied, then compressed (`soundchip_gs.cpp[gs]:1060-1064`, via `ttdperipheralregistry.cpp:90-98`).
- Add a `_ramDirty` flag set in `writeMem`, and let the registry reuse the previous blob or page-store slot when it's clean. Idle GS RAM is never written.

**G7: cleanups.**
- `traceEvent` does two acquire loads per GS port access or DAC fetch (`gsporttrace.h:151-154`). Replace them with a plain bool refreshed at frame start.
- The comment at `gsporttrace.h:23` says trace is gated by FeatureManager `porttrace_gs`, but no such feature is registered in `featuremanager.cpp`.
- Serialisation gap: the z80ex `noint_once`, `reset_PV_on_int` and `int_vector_req` are not in the TTD blob. That only matters if a frame boundary lands right after `EI`. The reserved bytes `[59..94]` (`[gs]:1052`, `:1104`) can hold them without a layout change.

**GS lightweight personality.** `soundchip_gslw.cpp[gs]:1060-1098` is already cheap when idle: `serviceQuantum` returns early when `!_player.isPlaying()`. Only G5 applies to it.

#### B.1.3 Validation for G1–G4
- **A/B determinism.** Run the same machine with the fast path forced off and on, for N frames idle, plus command sequences injected at arbitrary ZX tact offsets. Require an identical `TTDHashState` every frame, and identical RAM, status and data.
- **Existing tests:** `soundchip_gs_bootdiag_test.cpp` (POST, round trip, DAC paths), `soundchip_gs_intrate_test.cpp`, `soundchip_gs_porttrace_test.cpp` (`RealRomProducesDacActivity`), `soundchip_gs_test.cpp` (`TTDSerialize_RoundTrip`, `HostReset_*`, `Port33_*`) and `ttdgeneralsoundswitch_test.cpp`.
- **Missing:** a turbo on/off equivalence test and a GS frame benchmark. `core/benchmarks/emulator/sound/` has none.

#### B.1.4 If you still want real dormancy later
- Store a dormant epoch in the reserved blob bytes, and on wake run the real POST from reset to the wake time using G1's fast path. That is about 26 frames of real execution the first time, then idle skip.
- Don't restore a cached post-POST snapshot until you've verified that post-POST RAM doesn't depend on pre-reset RAM. `reset()`/`hostReset()` don't clear `_ram` (`[gs]:93-115`), and the POST RAM walk saves and restores bytes. Test it by booting twice with different RAM pre-fills and comparing blobs.

### B.2 MoonSound (`moonsound` branch)

#### B.2.1 Current cost and structure [V][M]

**No per-instruction hook.**

**Per frame:**
- **`handleFrameStart`** (`soundchip_moonsound.cpp[ms]:208-225`) runs `_opl4.Run(...)` on every frame, including turbo and sound-off (D3).
- **`handleFrameEnd`** (`:227-280`):
  - `Run` to the frame end;
  - `RenderSplit` (FM and PCM Kaiser polyphase resamplers);
  - an int16 trim per sample (`TrimToI16`, `:36`, `:268-269`);
  - an activity scan and a HUD post.
- **Port reads** (`:352-383`) call `ReadStatus`/`ReadFm`/`ReadWave`, and each does `SyncTo(now)` first.

**The chip loop** (`opl4.cpp[ms]:290-315`) steps through every FM tick (684 master clocks, about 49.5 kHz) and every output step (768 clocks, 44.1 kHz). That is about 1,014 FM ticks and 903 output steps per Pentagon frame.

- **FM tick** (`AdvanceFmToOutput`, `opl4.cpp:184-221`):
  - two virtual calls through `FmBus` (`fmbus.cpp:91`, `:94`);
  - `Opl4Fm::Advance` (`fmsynthopl4.cpp:582-711`), which walks all 44 operator slots and computes all 18 channels even when every envelope is off;
  - an 18-channel peak loop (`opl4.cpp:193-201`).
- **Output step** (`AdvanceOutputStep`, `opl4.cpp:223-288`):
  - PCM `Advance` (`pcmsynthopl4.cpp:350-496`) skips off slots, but still runs 3 loops over all 24 slots, including the TL interpolation with 64-bit `%9` and `/9%3` (`:402-419`);
  - a 24-slot peak loop (`opl4.cpp:242-259`).
- **Resampler** (`opl4render.cpp[ms]:158-180`):
  - double precision, 96 or 192 taps × 512 phases;
  - every input sample rebuilds an ordered history with two `%` per tap (`:168-169`);
  - every output sample convolves L and R separately with blended kernel rows;
  - **no silence shortcut.**

**Measured idle cost [M]** (2000 frames, reset state, split streams on, standalone `-O2`):

| Config (mode / quality / rate) | Chip `Run` per frame | `RenderSplit` per frame |
|---|---|---|
| **HiFi / Reference / 48000 (probable default)** | **652 µs** | **940 µs** |
| HiFi / HighFidelity / 48000 | 641 µs | 1,861 µs |
| HiFi / Reference / 44100 | 646 µs | 492 µs |

Against the design target in `2026-09-13-0217-opl4-core-tdd.md:603` ("under 3% of one modern core"), idle alone is about 5.5–12%.

**Gating today:**
- config only (`soundmanager.cpp[ms]:112-125`);
- `enableWideMix(true)` is set permanently as soon as the device exists (`:124`);
- `Opl4Pcm::AnyActive()` (`pcmsynthopl4.cpp:37`) and `Opl4::RamDirtyBitmap()` (`opl4.cpp:631`) exist but have no callers.

#### B.2.2 Recommendations (bit-exact unless stated)

**M1 (P0): zero-run shortcut in `PolyphaseResampler::Process`** (`opl4render.cpp[ms]:158`).
- Keep a counter of consecutive zero inputs.
- Once it reaches `taps`, every output is exactly +0.0, so write zeros but still advance `_phase` and `_histPos` (and write the zero into history).
- Render cost went from 492–1,861 µs to **10–21 µs** per frame **[M]**.
- It also helps whenever only one group plays, e.g. PCM-only music.

**M2: early-outs in the synth.**

| Where | Change | FM tick / PCM step cost [M] |
|---|---|---|
| FM operator (`fmsynthopl4.cpp:593-598`) | If `egState==Off && keyReq==keyOn`: only the phase step, and nothing at all when `fnum==0 && !vib`, since the step is exactly 0 | FM tick 439 → 88 ns |
| FM channel, 2-op, non-rhythm | Both operators off → `out=0` and shift a 0 into the feedback history | (part of the above) |
| PCM | All slots off, `tl==tlDest`, no active LFO → only `_egCnt++` | PCM step 96 → 34 ns |

- Keep "any PCM active" as a counter updated on state transitions rather than a 24-slot scan.
- The 4-op and rhythm branches need the same treatment.
- Combined with M1, idle came to about **0.2 ms per frame** instead of about 1.6 ms **[M]**.

**M3: quiescence fast path in `Opl4::SyncTo`** (`opl4.cpp[ms]:290`).

**Quiescence Q holds when:**
- every FM operator is `Off` with `keyReq==keyOn`, all `_fbHist` and `op.out` are 0;
- every PCM slot is `Off` with `tl==tlDest`;
- `windowSumL/R` and `heldL/R` are 0.

**While Q holds, `SyncTo` only does closed-form arithmetic:**
- `masterPos`, `fmTicks += nF`, `outSteps += nO`, `windowTicks`;
- FM `_egCnt`, `_lfoPm` (mod 8192), `_lfoAm` (mod 13440);
- the PCM `_egCnt`, and for LFO slots `lfoCnt += nO·kLfoSteps`;
- FM phase `+= nF·step` (mod 2^19) for operators with vibrato off, and a loop only over vibrato operators;
- `_noise`: either keep a tight `n`-iteration LFSR loop (about 1k iterations per frame, about 1 µs) or use a GF(2) jump matrix;
- timers: the next overflow at `k = max(1, P − t)` and then `t = (n − k) mod P`, with `P = (256 − load)·4` (T1) or `·16` (T2); set the status flag unless masked. No IRQ is wired to the Z80, so the timers only affect status (`fmbus.cpp:129-151`).
- BUSY and LD are deadlines compared against `masterPos` (`opl4.cpp:355-369`), so they are exact after the arithmetic.

**Streams and waking:**
- Bulk-append zeros to the active streams, so a mid-frame wake stays aligned. With M1, rendering them is nearly free and bit-exact.
- `WriteFm`/`WriteWave` leave Q. Reads fast-forward only.

**Effect:** tens of µs per frame idle, O(1) in turbo, with identical saved state, so no `kStateVersion` bump.
- *Not bit-exact alternative:* freeze the phases of off FM operators. They can't be heard, because key-on resets them (`fmsynthopl4.cpp:380`), but it changes the TTD hash, so it would need a version bump. Not recommended.

**M4 (bug): Authentic-mode memory leak.**
- `AdvanceOutputStep` always pushes to `chipStream` (`opl4.cpp[ms]:276-277`).
- The device only calls `RenderSplit` (`opl4.cpp:468-488`), which drains only the FM and PCM streams.
- Growth measured at about 282 MB of RSS per 10 emulated minutes with `RenderMode=authentic` **[M]**.
- Fix: don't push `chipStream` when `splitStreams` is on.

**M5 (bug): `#7F` read not NEW2-gated.**
- `portDeviceInMethod` → `ReadWave` (`soundchip_moonsound.cpp[ms]:378-381`) → `SyncTo` plus a possible BUSY extension and an MA auto-increment (`opl4.cpp:371-384`).
- Writes *are* gated (`:423-434`), and the comment says the chip ignores wave access while NEW2 is clear.
- `NotifyFullDecodeIn` reaches the card through the low-byte table for every `#xx7F` (`portdecoder.cpp[ms]:1481-1488`), so **every Beta-128 data-register read (`#7F`) during a TR-DOS transfer synthesises OPL4 up to now** and may mutate `memAdr`.
- Fix: `if (!_opl4.New2Mode()) return 0xFF;` for `PORT_WAVE_DATA` (and the handled/claim semantics should be consistent with it).

**M6: active-path speedups** (they matter while music plays).
- Resampler:
  - a mirrored ring (history stored twice), so there's no ordered copy and no `%` (TDD §11 already asks for "no modulo in the inner loop");
  - float instead of double;
  - fused L/R;
  - SIMD.
  - Expect 2–4×. Nearest-phase instead of row blending is an accuracy trade-off; measure it separately.
- PCM sample fetch goes through `IWaveMemory*` virtually (`pcmsynthopl4.cpp:125-138`), up to about 100 calls per output step. Hold a `WaveMemory*` (it's `final` with an inline `Read`) or a raw pointer plus mask.
- Cache `FmBus::Caps()` instead of calling it every tick (`fmbus.cpp:94`).
- Replace the `std::vector::erase` from the front of the streams (`opl4.cpp:481-486`) with read/write indices.

**M7: TTD correctness (not perf).**
- Wave SRAM (up to 1 MiB) is not captured at all; Tier B isn't implemented yet (`soundchip_moonsound.h:156`).
- So restoring an earlier checkpoint keeps the latest sample RAM, which is wrong for programs that stream samples.
- When Tier B lands, drive it from the existing `RamDirtyBitmap()`.

**M8: mixer.**
- The wide mix and master limiter are on for the whole session once MoonSound exists (`soundmanager.cpp[ms]:124`). That's cheap (a few µs), but it means the beeper and AY go through the master DC blocker even when MoonSound is never used.
- R6 ("legacy output unchanged") therefore only holds for `MoonSound=0`.
- Switching the wide mix on lazily would put a step into the DC blocker and click, so keep it. Just note it in the design doc.

**M9: dead code.** `3rdparty/ymfm/ymfm_opl.*` and `ymfm_pcm.*` were added (about 4k lines) but are excluded from the core build (`core/src/CMakeLists.txt:89-102`). Remove them or move them to `tools/poc`.

**M10: tests not in core-tests.**
- `3rdparty/opl4/tests/*` sit in an `EXCLUDE_FROM_ALL` target that is only CTest-registered in a standalone build (`3rdparty/opl4/CMakeLists.txt:31-47`).
- `TestDeterminism` (interleaved vs batched output bit-identical) is exactly the oracle for M1–M3. Wire it into `test-parallel`.

**Validation for M1–M3:**
- **Twin-chip A/B test** with the fast path forced off vs on. Compare the `SaveState` blob every frame and the rendered audio byte for byte. Cover:
  - idle, then key-on at a mid-frame T-state;
  - timers enabled, with status polling;
  - BUSY/LD reads while idle;
  - keyed-off operators with vibrato and fnum ≠ 0;
  - rhythm mode (noise LFSR);
  - an off PCM slot with an active LFO;
  - 44.1 and 48 kHz, HiFi and Authentic.
- Also: time from release to entering Q; restoring an active blob into a quiescent device; and a `#7F` read storm with NEW2 clear leaving the hash untouched.
- **Existing tests:**
  - `soundchip_moonsound_test.cpp`: `SuppressedFrame_RendersSilenceCoreKeepsRunning` (:789), `TTD_SaveNeutralAndRestoreExact_ReplaysIdenticalAudio` (:831), `TTD_RestoreHiFi_ConvergesAfterFilterWindow` (:895), `TTD_RoundTrip_*` (:1020), `Canary_*`.
  - `opl4tests.cpp`: `TestBusTiming` (:281), `TestDeterminism` (:363), `TestSaveRestore` (:414).

### B.3 TurboSound FM (TSFM, master)

- **The core advances every instruction, in every mode** (`soundchip_turbosoundfm.cpp:255-260`, `syncTo(nowT())`). It is fully clocked unless `_coreSynthesisSkipped` is set, and that only happens when output is suppressed **and** no TTD session is active (`soundmanager.cpp` handleFrameStart; `soundchip_turbosoundfm.cpp:86-109`). [V]
- **The skip path is a ready-made precedent.** It keeps timers, busy and the FM clock counter exact and skips only the operator clocking.
- **Recommendation T1:**
  - Extend the skip condition to `(suppressed || fmIdle) && !ttdActive`.
  - `fmIdle` means no key-on in either YM2203, and every FM operator at maximum attenuation in release (the ymfm envelope state), for at least one sample.
  - Leave it on any FM register write; `portDeviceOutMethod` already syncs first.
  - It is inaudible, because YM2203 has no LFO and key-on resets the phase.
  - It is not TTD-hash-exact, which is why `!ttdActive` stays in the condition, exactly as today.
- **Measure first.** There's no TSFM idle-cost benchmark. Add `BM_TSFM_IdleFrame` next to the TurboSound benchmarks. `TurboSound=FM` is the default on every shipped config on all three branches, including `spectrum48` (`data/configs/spectrum48/unreal.ini:358` on master), so this is a default-path cost.
- **Per-step sync goes away under §C.2.** The FM core would sync only on its port accesses and at frame end.

### B.4 Legacy TurboSound (2×AY)

- **`updateState()` ticks both chips at the generator rate** (`soundchip_turbosound.h:204-208`), and HQ feeds 4 FIR decimators (`soundchip_turbosound.cpp:95-130`). That happens even though most software never selects chip 1 (`OUT #FFFD,#FE`). [V]
- **T2: keep chip 1 dormant until the first chip-select.**
  - Its state is the reset state, so its output is a constant. Its decimator input is constant too, so its output converges to a constant (0 after DC).
  - Start ticking on the first select, and re-dormant on `reset()`.
  - A chip with all volumes at 0 and no envelope mode is also quiescent (a silence fast path per chip: the mix is 0 regardless of the tone/noise state, and the generator counters can be advanced arithmetically if you need hash exactness).
- **T3: model defaults.**
  - `spectrum48` ships `TurboSound=FM` on every branch, and `MoonSound=1` on master and `generalsound` (`spectrum48/unreal.ini:358,365`). The `moonsound` branch already sets `MoonSound=0` for 48/128/+3 with the comment "real Sinclair … never had this card". Apply the same reasoning to the AY/TSFM slot on the 48K.
  - `ay_scheme`/`ay_chip` are parsed but never used.
  - A 48K has no AY. Make the AY/TS device model-aware: absent on 48K unless an "AY interface" option is set.

### B.5 Mixer and manager (all sources)

- **Peak meters** do a float abs and a divide per sample, for every device, every frame, even on silent buffers (`soundmanager.cpp[ms]:748-790`).
  - Compute the integer max-abs and convert once.
  - Skip devices flagged silent (B.0 rule 3).
- **`AudioActivityPayload` posts** go out every active frame per device: TurboSound (`soundchip_turbosound.cpp:246`), TSFM (`:479`, `:488`), GS (`[gs]:436`) and MoonSound.
  - Each is a heap allocation, a mutex-guarded queue and topic resolution.
  - They ignore the `hud` feature, while the Memory and Screen HUD posts do check it (`memory.cpp:1937,1948`; `screen.cpp:1929,1938`).
  - Gate them on a cached HUD flag, and post on state change only, plus a low-rate keep-alive.

---

## C. Core hot path: per instruction

### C.1 Inline fast memory path (bit-exact)

**Problem** [V]:
- Opcode TUs → `Z80::rd/wd` (`z80.cpp:649-700`, out-of-line) → `(_memory->*MemIf->MemoryRead)` (`memory.cpp:176`, another TU).
- `busTraceHook` is a `std::function` checked on every access (`z80.cpp:669`, `698`).
- The M1 fetch goes through the same path (`m1_cycle` → `rd(pc, true)`, `z80.cpp:632`).

**Recommendation:**
1. **Try IPO/LTO first**, since it's a zero-code change.
   - `include(CheckIPOSupported)`, then `set_property(TARGET core PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)`.
   - Measure `BM_CpuSpeedProbe_LCG10M` and the CoreRate covox frame.
   - Check the zero-warnings policy on MSVC and MinGW: ThinLTO and `/GL` behave differently.
2. **Header-inline fast path**, which doesn't depend on LTO.
   - In `z80.h`, `rd`, `wd` and `m1` become inline and test a single cached `_fastBus` bool.
   - `_fastBus = !isDebugMode && !busTraceHook && !m1TraceHook && !ttdCoverageActive && !ttdProbe.IsArmed()`, refreshed from `UseFastMemoryInterface`/`UseDebugMemoryInterface` (`core.cpp:574-583`), hook setters and TTD state changes.
   - When the flag is set, read and write `_memory->_bank_read[addr >> 14][addr & 0x3FFF]` directly: the bank-pointer arrays live at stable addresses, so cache a pointer to them in `Z80`.
   - Otherwise take the current out-of-line path.
   - Keep the contention check inline: `IsAddressContended` is already inline (`ulacontention.h:100-107`). Cache `pUlaContention` in a `Z80` member.
3. **Put `IncrementCPUCyclesCounter` back in the header** (it was lost in `a921dc1b`, §A). `rate` has been constant at 256 since `ApplyQueuedFrequencyMultiplier` (`z80.cpp:453`); keep the multiply for generality, it costs nothing once inlined.
4. **Measure with your existing `z80_overhead_attribution.cpp`** (moonsound branch). `PointerToMember` vs `DirectRead` and `FullComparison` are exactly the delta this removes.

### C.2 Replace per-instruction peripheral stepping with catch-up sync (the big one)

**Observation** [V]. Every per-step device already renders or advances over an arbitrary span `[last, now]`:

| Device | Per-step work today | State the CPU can observe | Already catch-up? | Sync points needed |
|---|---|---|---|---|
| Screen (`ScreenZX` / ATM / TSConf) | `UpdateScreen` → `DrawPeriod(_prevTstate, now)` (`screenzx.cpp:700-710`) | None. Floating bus and contention are computed from `t` by `UlaContention`, not from the framebuffer | **Yes** | A write to a displayed VRAM page; any OUT (border, mode, palette, paging); frame end; pause, debug break or screenshot |
| AY / TurboSound | Renders `[_lastTStates, now]` (`soundchip_turbosound.cpp:56-130`) | AY register reads, which are time-independent | **Yes** | OUT to `#FFFD`/`#BFFD` (and TS select); frame end |
| TSFM | `syncTo(nowT())` (`soundchip_turbosoundfm.cpp:255-260`) | Status (busy, timers) | **Yes** (`syncTo` walks timer expiries in order, `:81-109`) | IN/OUT on its ports; frame end. Verify that no YM2203 IRQ line is wired to the Z80 **[?]** |
| Tape | Computes `getTapeStreamBit(t)` and pushes a DAC edge on change (`tape.cpp:681-702`) | `#FE` bit 6 (verify it is computed at IN time **[?]**) | **Yes**, since the bit is a pure function of time: emit all edges in `(last, now]` with their exact T-states | IN `#FE`; OUT `#FE` (beeper DAC ordering); frame end |
| WD1793 | `process()` per step while active (`wd1793.cpp:3179-3208`); asleep otherwise | Only via its ports, which already call `process()` | **Partly.** Needs `process()` to loop until caught up across several byte periods (112T each) **[?]** | Its ports; frame end; or a next-deadline scheduler |
| GS, MoonSound | None per step | Their ports | **Yes** (`runTo`, `SyncTo`) | Already done |
| Beeper, Covox | Event-driven on OUT | — | **Yes** | Already done |

**Bit-exactness rule: sync to the instruction-start T-state, not the access T-state.**
- Today `OnCPUStep` runs *after* each instruction. So a VRAM write or OUT that happens mid-instruction is already visible to the draw or render for the whole span up to the end of that instruction, while everything before the instruction's start was drawn with the old data.
- A sync hook that brings the device to `_stepStartT` (the value of `t` before `Z80Step`) before any mutation reproduces that **exactly**.
- Syncing to the access T-state instead is more hardware-accurate (sub-instruction raster precision) but not bit-identical. Make it a separate, deliberate follow-up.

**Mechanics:**
1. **A small `ICatchUpDevice { void SyncTo(uint32_t frameT); }` registry** in `MainLoop`/`Core`, with `SyncAll()` called:
   - at frame end, before the current `OnFrameEnd` work;
   - on `Emulator::Pause`/breakpoint/`WaitWhilePaused` entry;
   - on every automation state query that reads peripheral state (screen capture, audio buffers, TTD capture — capture is at frame end, so it's covered).
2. **`PortDecoder`:** call `SyncAll(_stepStartT)` at the top of `Z80::in`/`Z80::out` (`z80.cpp:702`, `766`).
   - IN/OUT are a small fraction of instructions, and syncing *all* devices on I/O is the simplest correct rule.
   - Later it can be narrowed per port if profiling asks for it.
3. **VRAM writes:** a per-bank `_bankIsDisplayed[4]` bitmap maintained by `Memory::UpdateZ80Banks` and the screen-page selection (7FFD bit 3, ATM/TSConf modes).
   - On the fast path: `if (_bankIsDisplayed[bank]) screen->SyncTo(_stepStartT)`, which is one byte test per write (fits C.1's inline path).
   - This also replaces the dead `video_memory_changed` flag (`memory.cpp:363-366`, written and never read).
4. **`OnCPUStep`** then keeps only the WD1793 while it's awake. That call already returns immediately when asleep (`wd1793.cpp:3182-3185`), and it can move to a next-deadline check (`if (t >= _fdcNextEventT)`) as a phase 2.
5. **Frame clip semantics.** TurboSound clips the last instruction's overshoot at the frame boundary (`soundchip_turbosound.cpp:77-89`) and TSFM does the same (`:275-280`). Keep them as they are; the frame-end sync supplies the same `frameTStates` limit.

**Staging** (each stage A/B-tested bit-exact against the current build):
1. **Screen.** It is the largest share (1,354 µs of stepping in-app) and needs the VRAM hook.
2. **AY/TurboSound and TSFM.**
3. **Tape.**
4. **WD1793** deadline scheduling (optional).

**Test harness:** for every stage, run twin emulators (per-step vs catch-up) over the existing demo, loader and multicolor corpora. Compare the framebuffer hash every frame, the audio buffer hash every frame, and the TTD hash. Existing assets:
- the multicolor/border timing tests (`98421786`, Pentagon border timing);
- `BM_CovoxDemoFrame_CoreRate`;
- the boot tests in `core/tests/emulator/machines/*`.

### C.3 Consolidate the per-instruction instrumentation checks

- **Today** each instruction evaluates, as separate loads and branches:
  - `isDebugMode`;
  - the `pDebugManager` → `GetAnalyzerManager()` (out-of-line, `debugmanager.h:52`) → `hasCPUStepSubscribers()` chain (`z80.cpp:297-304`);
  - `_feature_calltrace_enabled`, `_feature_opcodeprofiler_enabled`, `cycles_to_capture`;
  - in `m1_cycle`: `m1TraceHook`, `ttdCoverageActive`, `ttdProbe.IsArmed()`;
  - in `rd/wd`: `busTraceHook`.
- **Recommendation:** one `uint32_t _instrumentationMask` on `Z80`, recomputed whenever any of these change. The analyzer manager, TTD start/stop and hook setters notify it.
  - The hot path does `if (_instrumentationMask) [[unlikely]] SlowPathHooks();`.
  - Bit-exact. It removes about 8 dependent loads per instruction plus one out-of-line call.
- The three PC-trap compares (`z80.cpp:313`, `324`, `332`) are cheap immediate compares; leave them.

### C.4 Pause check per instruction

`z80.cpp:555` calls `Emulator::IsPaused()` (`emulator.cpp:3373`, out-of-line, `volatile bool`). Two options:
- make it an inline `std::atomic<bool>` with `memory_order_relaxed` loads; or
- better, fold "pause requested" into the same `_instrumentationMask` or a single `std::atomic<uint32_t> _stepFlags` that the pause code sets. That's one relaxed load per instruction for all of it.

`volatile` isn't a synchronisation primitive anyway; the atomic is also the correct type for it.

### C.5 HALT: no change proposed, but a correction to the rationale

- `op_76` (`op_noprefix.cpp:711-718`) re-executes the HALT through the full `Z80Step`/M1 path every 4T; `vm1` is never set (`z80.h:299`, only read at `z80.cpp:341`). That is where the 17.9k steps in HALT-heavy frames come from.
- **Most of the non-goal's reasons no longer hold for the current code.** `06-overrun-root-cause-analysis.md` lists several things that would break if HALT were fast-forwarded:
  - "starves the GS Z80 coprocessor" (line ~125);
  - "desynchronizes … OPL4 hardware timers" (line ~116);
  - YM2203 timers.

  GS (`runTo`) and MoonSound (`SyncTo`) are **catch-up devices that don't take part in per-step stepping**, and TSFM's `syncTo` is catch-up too. Of the peripherals it names, only the FDC (and the per-step screen/tape/AY stepping that §C.2 removes) truly relies on per-step calls today.
- **Proposal: none now.** Once C.2 lands, a HALT step costs just the M1 re-fetch plus the INT check, which is cheap. Keep the non-goal until then, then revisit with data. HALT-to-next-event would need the FDC deadline from C.2 stage 4 and exact R/`halt_cycle`/`tstates_halted_current` arithmetic.

### C.6 Screen specifics

- **HQ off still pays per instruction.** `mainloop.cpp:465-468` calls `UpdateScreen()` (virtual → virtual `GetCurrentTstate` → `DrawPeriod`, which returns at `screen.cpp:1239-1242`) even with ScreenHQ off.
  - Gate it on `_renderThisFrame && screen->IsScreenHQEnabled()`, cached as one bool at frame start.
  - Call `ResetPrevTstate()` when HQ toggles.
  - `SetBorderColor`'s own `UpdateScreen` is unaffected.
- **Per-T-state virtual `Draw`** (`screen.cpp:1294-1297`, about 70k calls per frame with HQ on): see §A.2 item 1. The per-T-state `frameLimit`/mode test (`screenzx.cpp:719-724`) can be computed once per `DrawPeriod`.

---

## D. Port I/O path

**D1 (P1): log-line construction on every non-muted port** [V]
- `portdecoder_pentagon128.cpp:157-169` (IN) and `:229-237` (OUT), with the same pattern in `portdecoder_scorpion256.cpp`, `portdecoder_spectrum128.cpp` and `portdecoder_spectrum3.cpp`:
  - `if (_logger->GetLevel() <= LogInfo)` → `std::set` lookup → `GetPCAddressLocator(pc)` (`std::string`) → `MLOGINFO(...)`.
- **The Qt app runs at `LogInfo`** (`unreal-qt/src/mainwindow.cpp:1206-1207`, `1221`).
  - It turns IO module logging off (`:1237`).
  - It mutes only `#FE`, `#7FFD`, `#FFFD` and `#BFFD` (`:1250-1254`).
- So every access to the FDC ports (`#1F/#3F/#5F/#7F/#FF`), Kempston, GS (`#B3/#BB`), MoonSound (`#C4-#C7/#7E/#7F`), covox and so on allocates and formats a string that `ModuleLogger` then drops.
  - TR-DOS status-polling and data loops are the worst case.
  - Headless defaults to `LogTrace` (`modulelogger.h:247`), where it's the same or worse.
- **Fix:**
  - (a) Make `MLOGINFO`/`MLOGWARNING`/`MLOGERROR` check `IsLoggingEnabledForLogLevel(_MODULE, _SUBMODULE, level)` like `MLOGDEBUG` already does (`modulelogger.h:20-44`).
  - (b) Guard the decoder blocks with the same module check before building `currentMemoryPage`.
  - (c) The same applies to `wd1793.cpp:3253-3266`, where `dumpStatusRegister` builds and cleans a string on every `#1F` status read outside any guard, and to `wd1793.cpp:3319` (`memBankName()` evaluated as a log argument).

**D2: map lookups per access.**
- `_portDevices` and `_fullDecodeDevices` are `std::map<uint16_t, PortDevice*>` (`portdecoder.h:372`, `383`).
- Every IN/OUT does `_fullDecodeDevices.find(port)` (`portdecoder.cpp:1474` and the OUT twin).
- Dispatch does `key_exists` then `at` on `_portDevices`, which is two tree walks (`portdecoder.cpp:1187-1240` region and `:436`).
- **Fix:**
  - an `if (!_fullDecodeDevices.empty())` guard (it's always empty today, since MoonSound registers through the low-byte array);
  - a flat `std::array<uint8_t, 65536>` index into a small device table (64 KB, cache-friendly for hot ports), or a 256-entry low-byte table plus a per-entry high-byte mask.

**D3: debug hooks per I/O, not feature-gated.**
- `OnPortInComplete`/`OnPortOutComplete` (the model-decoder path) and the legacy `DecodePortIn/Out` always call `BreakpointManager::HandlePortIn/Out` (out-of-line; `portdecoder.cpp:221`, `319`, `156`, `264`) and `MemoryAccessTracker::TrackPortRead/Write` (the pointer is never null; `:243`, `:341`, `:191`, `:294`).
- Gate both on one cached `_ioDebugHooksActive` bool: breakpoints feature AND `hasPortIn/Out`, OR memorytracking.
- This also fixes the gating bug F1.

**D4:** MoonSound `#7F` read (M5).

---

## E. Per-frame overheads

| # | Where | Problem | Fix |
|---|---|---|---|
| E1 | `soundmanager.cpp:495` **and** `:652` (master); `[ms]:564` and `:727` | `_turboSound->handleFrameEnd()` is called twice per frame. The second call only re-posts `NC_AUDIO_ACTIVITY` (TS `soundchip_turbosound.cpp:232-248`; TSFM `:464-490`, 2 posts). The flags aren't reset between the calls. | **[DONE 2026-09-24, verified]** Deleted the second call (master `:652`); kept the first (drains TSFM's word queues before the mix, as required). Verified: added `core/tests/emulator/sound/audioactivityindicators_test.cpp` (parameterized AY/FM), which posts `NC_AUDIO_ACTIVITY` from a driven active frame and asserts exactly one delivery per frame; confirmed the test fails with the exact duplicate-post symptom ("got 2", then "got 4") when the second call is temporarily reintroduced, and passes with it removed. Full suite green (3299/3299) with the fix in place. Not yet committed - working tree only. |
| E2 | `core.cpp:789` (unconditional, in `CPUFrameCycle`) **and** `mainloop.cpp:601-611` | `Memory::SyncToDisk()` runs twice per frame when shared memory is on. Each call does `__sync_synchronize()` plus `msync(MS_SYNC \| MS_INVALIDATE)` over the whole block (`memory.cpp:657-681`), or `FlushViewOfFile` on Windows. For `shm_open`/anonymous shared mappings the barrier is enough; `msync(MS_SYNC)` forces writeback of file-backed pages. | Keep one call site (mainloop). Drop `msync` for `shm_open` memory and keep it only for file-backed mappings, rate-limited or on demand. See also `2026-08-27-shared-memory-coherency`. |
| E3 | `mainloop.cpp:621-635` | Per rendered frame: a `std::string` copy of the emulator id, then `new EmulatorFramePayload` (which parses a UUID from it), a topic-string conversion and two mutex-guarded queue operations. `_context->emulatorId` is already a parsed UUID. | Use `_context->emulatorId`, resolve topic ids once at init, pool the payloads. Same for `NotifyCPUFrequencyChanged` (`z80.cpp:462-470`). |
| E4 | TurboSound, TSFM, GS and MoonSound activity posts | Heap payload per active frame per source, not gated by the HUD feature (§B.5) | Cached HUD flag; post on change plus a keep-alive. |
| E5 | `soundmanager.cpp[ms]:748-826` | Float peak and mix over silent buffers | §B.5 |
| E6 | `mainloop.cpp:89-135` | `getenv` once per `Run()` is fine, but the "TEMP DIAG" block is marked for removal | Remove with the audio-underrun diagnostics test, as planned. |
| E7 | `z80.cpp:92` | `OpcodeProfiler` (160 KB trace buffer) is allocated per emulator even with the feature off. While capturing: a `fetch_add` and a `%` per instruction (`opcode_profiler.cpp:89`, `106`). | Allocate lazily on enable; power-of-2 mask; plain counter owned by the emulation thread. |

---

## F. Feature-gating defects

Each item lists the gating bug and its fix.

**F1. `breakpoints` feature applied inconsistently** [V]
- Memory R/W breakpoints need `debugmode && breakpoints` (`memory.cpp:250`, `369`).
- PC breakpoints need `isDebugMode` only (`z80.cpp:232`).
- Port breakpoints aren't gated at all: the model-decoder path `OnPortInComplete`/`OnPortOutComplete` (`portdecoder.cpp:216-221`, `314-319`) and the legacy `DecodePortIn`/`DecodePortOut` (`:150-156`, `:264`) check only `pDebugManager != nullptr`.
- So turning `breakpoints` off leaves PC and port breakpoints live, and port breakpoints fire even with debugmode off.
- **Fix:** one cached `_feature_breakpoints_enabled` (debugmode AND breakpoints), used on all three paths.

**F2. TTD history can be silently corrupted** [V]
- Nothing stops `setFeature(debugmode|timetravel, false)` while recording (`featuremanager.cpp:117-244`).
- The CPU then switches to the fast memory interface (`featuremanager.cpp:561-565`, `core.cpp:770-781`) and `MarkDirty` stops (`memory.cpp:333`, `1783`), while `OnFrameBoundary` keeps capturing.
- Every page written after that point is treated as clean and shares the previous slot.
- **Fix:** reject turning those features off while `IsRecording()` (the same pattern as the shortcut block at `:127-139`), or stop the recording first.

**F3. Enabling the `timetravel` feature (without recording) disables fasttape, fastdisk and turbotape** [V]
- `setFeature(kTimeTravel, true)` sets `_ttdShortcutOverrideActive` (`featuremanager.cpp:145-154`), and `isTtdRecordingActive()` returns true for it (`:71-76`), so `isEnabled` reports the shortcuts off (`:297-302`).
- It also forces debugmode (the slow memory interface) and the per-write `MarkDirty` / `RecordMemoryWrite` calls.
- **Fix:** set the override only in `onTtdRecordingStarted` (`:88-101`). Gate the write hook on a "TTD recording" context bool, not the feature flag.

**F4. Calltrace logged twice; operands decoded as instructions** [V]
- With memorytracking and calltrace both on, control flow is logged twice: `TrackMemoryExecute` (`memoryaccesstracker.cpp:651-655`) and `Z80Step` (`z80.cpp:378-386`).
- `TrackMemoryExecute` also runs for operand fetches, which pass `isExecution=true` (e.g. `op_noprefix.cpp:18-19`, 72 sites). So data such as `LD BC,#C9xx` can log a phantom `RET`.
- **Fix:** remove the calltrace block from `TrackMemoryExecute`; `Z80Step` stays the single source.

**F5. The access-tracker read path is gated only on a pointer that is never null** [V]
- `memory.cpp:206-217`, while the write path checks `_feature_memorytracking_enabled` (`:323`).
- **Fix:** add `_feature_memorytracking_enabled &&`.

**F6. Buffers freed while the emulation thread may be using them** [?]
- `MemoryAccessTracker::UpdateFeatureCache` frees the counters and `_callTraceBuffer` from the UI/WebAPI thread (`memoryaccesstracker.cpp:136-157`).
- The emulation thread can be between `IsCalltraceCapturing()` and `GetCallTraceBuffer()->…`, with no null check (`z80.cpp:381-383`).
- **Fix:** defer the free to the frame boundary on the emulation thread, or pause first; add a null check.

**F7. Analyzer PC breakpoints force debugmode** [V]
- Registering the first analyzer PC breakpoint turns the `breakpoints` feature on (`analyzermanager.cpp:50-56`), which cascades to debugmode and the slow memory interface for every access.
- PC breakpoints don't need the debug memory interface (`HandlePCChange` is in `Z80Step`).
- **Fix:** choose the debug memory interface only when memory R/W breakpoints exist, or tracking or TTD recording is active.

**F8. The slow `isEnabled` runs on trap paths** [V]
- `FeatureManager::isEnabled` (`featuremanager.cpp:290-305`: `recursive_mutex`, `std::string` construction, map lookups) is called on trap hits: `diskfastload.cpp:36` (every `$3FEC` INI loop pass) and `tapefastload.cpp:39`.
- **Fix:** cached bools refreshed by `onFeatureChanged`.

**F9. The shared-memory migration path skips the tracker refresh** [?]
- It returns early before `_memoryAccessTracker->UpdateFeatureCache()` (`memory.cpp:1797-1800`).
- Z80's `_feature_calltrace_enabled` isn't ANDed with debugmode (`z80.cpp:1182`), unlike the tracker's flag.
- **Fix:** refresh before returning; make the two flags consistent.

**F10. The GS port-trace comment claims a feature that doesn't exist** [V]
- `gsporttrace.h:23` says trace is gated by `porttrace_gs`, which isn't registered.
- **Fix:** register the feature or fix the comment.

**F11. The `sound` feature off doesn't stop the coprocessors' synthesis**
- MoonSound (D3) and GS by design, and TSFM unless TTD is off.
- With §B's quiescence this becomes cheap. Document it as "sound off = output off; CPU-visible device state stays exact".

**Cross-thread writes of cached bools.**
- `onFeatureChanged` writes plain bools, `isDebugMode` and `MemIf` from the API thread while the emulation thread reads them (`featuremanager.cpp:542-632`).
- For the bools this is benign in practice, but it's formally a data race.
- For `MemIf` swaps and buffer frees (F6) it isn't benign. Consider applying feature changes as a queued command at the next instruction or frame boundary on the emulation thread. That also fixes F2 and F6 structurally.

---

## G. Other observations (not performance)

- **No contention on opcode or operand fetches.**
  - `rd()` skips contention when `isExecution` (`z80.cpp:654`).
  - The M1 fetch (`z80.cpp:632`) and all operand fetches (`rd(pc++, true)`) pass `true`.
  - On 48K/128K, code executing from `#4000-#7FFF` (or contended upper banks) therefore gets no contention delays. Verify whether that is intentional.
- **Debugger step paths ignore `ProcessInterrupts`' return value** (`emulator.cpp:2147-2149`, and the sibling step paths).
  - When an INT is accepted, `Z80Step` still runs in the same iteration, unlike `Z80FrameCycle` (`z80.cpp:561-567`).
  - So one debugger single-step executes the INT entry plus the handler's first instruction.
- **The 2026-01 performance percentages in `feature-management.md` are stale.** They predate the audio and screen rewrites; re-measure before quoting them in docs.
- **`2026-08-04-performance-profiling` is marked DONE**, but see §A: the code it describes isn't in master.

---

## H. Benchmarks and tests to add

| Benchmark or test | Purpose |
|---|---|
| `BM_GS_IdleFrame`, `BM_GS_PlaybackFrame` | GS cost (none exist today) |
| `BM_Moonsound_IdleFrame` (HiFi/Ref/48k, HighFidelity), `BM_Moonsound_ActiveFrame` | MoonSound cost; gate for the < 3% target |
| `BM_TSFM_IdleFrame` | Decides T1 |
| `BM_FullMachine_Pentagon_IdleDevices` | Default Pentagon with GS + TSFM + MoonSound on and no sound software. This is the number users actually pay. |
| `z80_overhead_attribution` results recorded in `docs/` | Before and after C.1 and C.3 |
| Twin-emulator A/B harness (framebuffer, audio and TTD hash per frame) | Bit-exactness oracle for B.1 G1, B.2 M1–M3, C.1, C.2 and C.3 |
| GS turbo on/off equivalence | G4 regression |
| MoonSound `#7F` read storm with NEW2 clear | M5 regression |
| Feature-toggle-during-recording test | F2 |
| `opl4tests` wired into `test-parallel` | M10 |

---

## I. Suggested order of work

1. **Quick, safe fixes (bit-exact, low effort):**
   - ~~E1 (double TS frame end)~~ **[DONE 2026-09-24]**;
   - D1 (log macros and decoder guards);
   - M4 (Authentic leak);
   - M5 (`#7F` NEW2 gate);
   - G4 (GS turbo time loss);
   - F1, F3 and F8;
   - G2 and G3 (GS INT guard, no-ROM);
   - E2 (single `SyncToDisk`, no `msync` for shm).
2. **§A: re-land the lost Aug-04 optimizations** one at a time, with benchmarks.
3. **Sound device quiescence:**
   - M1 (resampler zero-run);
   - then M2 (synth early-outs);
   - then G1 (GS idle fast-forward) and M3 (OPL4 `SyncTo` quiescence);
   - then T1/T2 (TSFM/AY);
   - then the B.0 contract so `SoundManager` skips silent sources and HUD posts.
4. **C.1 and C.3** (inline bus fast path, instrumentation mask, pause flag).
5. **C.2 staged catch-up sync:** screen, then AY/TSFM, then tape, then optionally the FDC deadline.
6. **Revisit the HALT non-goal with data** (C.5).
7. **Structural gating hygiene:**
   - F2/F6 via feature changes applied on the emulation thread;
   - M7 (MoonSound Tier B wave RAM in TTD);
   - G6 (GS RAM dirty flag for TTD).

---

## Appendix: measurement notes

- **[M] numbers** for GS and MoonSound come from standalone builds of the device or library hot path: g++ `-O2` on a 2.1 GHz Xeon VM, 2,000 frames, starting from the post-POST state (GS) or reset state (OPL4).
  - They leave out parts of the emulator: blip, the context and the mixer for GS; the device wrapper for OPL4.
  - Absolute values in the real build will differ. The ratios and the "idle costs as much as active" conclusion should hold.
  - Re-measure with the benchmarks in §H before and after each change.
- **[D] numbers** are from `docs/inprogress/2026-09-15-frame-budget-triage/` (Windows, 3.7 GHz) and `2026-08-04-performance-profiling/` (macOS ARM).
