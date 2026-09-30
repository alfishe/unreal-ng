# RZX on the per-step work gate: how to apply it

> **Applied (2026-09-29).** `kStepWorkRzx = 1u << 3`; the RZX block sits in
> `Z80::StepInstructionWithWork` after the TTD input, with one exit through the
> machine engine and `OnCPUStep` (`Z80::RzxFrameEnd` takes the forced
> interrupt); `RzxSession` sets and clears the bit and refuses a machine with
> `kStepWorkInterruptSource`. The `IN` hook stays next to the TTD port journal.
> See [design.md](design.md) "As built".

Handoff note for the RZX playback work ([design.md](design.md) §5, "Hook 2:
fetch counting and the interrupt schedule"). The gate that section plans
("the gate becomes a small bit set") exists on master since 2026-09-29, built
and measured for TSConf / Sprinter hooks (PLAN #60(a)). RZX adds one bit and
one block; nothing on the classic step changes.

## What is on master

| Piece | Where | What it does |
|:--|:--|:--|
| `EmulatorContext::stepWork` (`std::atomic<uint32_t>`) + `StepWorkBits` | `core/src/emulator/emulatorcontext.h` | one bit per rare per-step job; replaces TTD's `ttdInputWork` bool |
| `SetStepWork(bits, on)` / `HasStepWork(bits)` | same | atomic `fetch_or` / `fetch_and`: a job sets or clears only its own bit, from any thread |
| `Z80::StepInstruction` | `core/src/emulator/cpu/z80.cpp` | loads `stepWork` once; zero → the classic step, unchanged; non-zero → `StepInstructionWithWork(work, …)` |
| `Z80::StepInstructionWithWork` | same | out of line: TTD input → interrupt decision → the instruction → the machine engine → `OnCPUStep` |
| `Z80::ProcessInterruptsImpl<bool UseSource>` | same | the interrupt decision; `<false>` is the classic one, `<true>` for a machine interrupt source |

Current bits: `kStepWorkTtdInput` (1 << 0), `kStepWorkInterruptSource`
(1 << 1), `kStepWorkMachineStep` (1 << 2). **`1u << 3` is reserved for RZX.**

The measurement behind it: two separate pointer tests per instruction cost
the classic machines 0 to about 1 % of frame time depending on the run;
behind the gate they are within noise of the code without them ([performance-guidelines.md](../../guidelines/performance-guidelines.md) §5).

## Steps for RZX

1. **Add the bit** in `EmulatorContext::StepWorkBits`:
   `kStepWorkRzx = 1u << 3, ///< RZX playback active (RzxPlayer)`, and move
   the "next free" comment to `1u << 4`.
2. **Own the bit in the player.** `RzxPlayer::Start` →
   `context->SetStepWork(EmulatorContext::kStepWorkRzx, true)`; `Stop`, end of
   file, desync abort, emulator teardown → `false`. Keep `frameIntMasked`
   handling as design §5 says.
3. **Add the block to `StepInstructionWithWork`**, after the TTD input and
   before the interrupt decision, as design §5's pseudo-code:

   ```cpp
   if (work & EmulatorContext::kStepWorkRzx) [[unlikely]]
   {
       RzxPlayer* player = _context->pRzxPlayer;
       if (player->FrameDue() && player->StartNextFrame() && iff1 && player->IntAllowed())
       {
           HandleINT(0xFF);            // the forced frame interrupt is this step
           result.intAccepted = true;
           ... the machine engine / OnCPUStep tail as below ...
           return result;
       }
       const uint8_t r0 = r_low;       // fetch counting from R (design §5)
       ... the normal interrupt decision and Z80Step below ...
       player->CountFetches(r0, r_low, result);
   }
   ```

   Keep one exit path for the tail (machine engine, then `OnCPUStep`), so a
   forced interrupt still steps a machine engine and the peripherals.
4. **Refuse machines with their own interrupt source** (design §5):
   `RzxPlayer::Start` fails when
   `context->HasStepWork(EmulatorContext::kStepWorkInterruptSource)`.
5. **The `IN` hook** (design §4) is not part of this gate: `IN` is per port
   access, not per step; it stays next to the TTD port-journal test.
6. **Update design.md**: §2's per-step loop row and §5's first paragraph name
   `ttdInputWork`; it is now the `kStepWorkTtdInput` bit of `stepWork`.

## Tests to add

- The gate: after `Start` only `kStepWorkRzx` is added, after `Stop` it is
  gone and the other bits are untouched — the pattern of
  `MachineStepHook_Test.TheGateIsZeroOnAClassicMachineAndEachSetterOwnsItsBit`
  (`core/tests/emulator/cpu/z80_test.cpp`).
- Without the bit the player is never called (the pattern of
  `MachineStepHook_Test.WithoutItsBitTheHookIsNotCalled`).
- Requirements RZ-T* as planned.

## Performance check (requirements RZ-N2)

Follow [performance-guidelines.md](../../guidelines/performance-guidelines.md)
§4: A = master before RZX, B = the RZX change.
- **Playback off:** `BM_HostFrame_{48K,Pentagon,Scorpion}_{Fast,Debug}` must
  stay within noise (by construction: the classic step is untouched).
- **Playback on:** RZ-N2 allows ≤ 5 %; measure a frame with a player active
  (a new `BM_HostFrame_Rzx_*` next to the existing ones in
  `core/benchmarks/emulator/memory/hostbusoverlay_benchmark.cpp`).
