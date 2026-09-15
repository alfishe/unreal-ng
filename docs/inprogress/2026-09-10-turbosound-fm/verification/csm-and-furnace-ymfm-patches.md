# YM2203 CSM mode: what was wrong, how ymfm, Furnace, Nuked and hardware differ, and what unreal-ng ported

Date: 2026-09-13. Companion to `core/src/3rdparty/ymfm/PATCHES.md` (§4 and the "reviewed and NOT ported" table) and to `ymfm-furnace-csm.patch` in this folder.

## 1. Channel 3 modes on the OPN family (YM2203 / YM2608 / YM2610 / YM2612)

Register `0x27`, bits 7–6, select how channel 3 (the third FM channel, index 2) is driven:

| Bits 7–6 | Mode | Channel 3 pitch | Key-on |
|---|---|---|---|
| `00` | Normal | One F-number/block for all four operators, operator multipliers apply | Register `0x28` only |
| `01` | Extended ("3-slot", "multi-frequency") | Each operator has its own F-number/block: `0xA2/0xA6` for slot 4, `0xA8–0xAA` / `0xAC–0xAE` for slots 1–3 | Register `0x28` only |
| `10` | Extended + CSM (Composite Sine Mode) | As extended | Register `0x28` **and** every **Timer A** overflow keys all four operators |
| `11` | Illegal | | |

CSM was intended for formant speech synthesis: the four operators are tuned to formant frequencies, Timer A is set to the pitch period, and every overflow restarts the operators, so the output is a pitched pulse train shaped by the formants. This is what the *Tech Support from Moe-bius* tune uses.

**It is Timer A, not Timer B.** The YM2608 application manual, MAME's `fm.c` (`CSMKeyControll` inside `TimerAOver`), Nuked-OPN2 (`OPN2_DoTimerA` sets `mode_kon_csm`) and ymfm (`engine_timer_expired`, `tnum == 0`) all agree. Timer B's overflow only sets its status flag. Both timers are clocked correctly by our device on the exact T-state; `TsfmTimer_Test.APeriod` and `BPeriod` pin that.

The YMF288 (OPN3) dropped CSM; ymfm's YMF288 write path masks bit 7 of register `0x27` for that chip only.

## 2. What the hardware does on a Timer A overflow in CSM mode

Per Nuked-OPN2 (die-level model of the YM3438, same OPN core):

- `mode_kon_csm` is asserted when Timer A overflows while CSM is on, and lasts **one sample** (24 internal cycles).
- For channel 3's four operators the effective key-on is `manual key ∨ mode_kon_csm`.
- A 0→1 edge on that effective key resets the operator's phase and starts the envelope attack; the following 1→0 edge starts the release.

So every overflow is a *pulse*: phase reset, attack, release. With AR = 31 the attack completes instantly, so each pulse rings at full level and decays at the release rate until the next overflow. A channel also keyed manually stays keyed: the pulse rides on a key that is already 1, no edge, no retrigger.

## 3. What upstream ymfm does, and why it sounds wrong

Upstream ymfm (`aaronsgiles/ymfm` `81aec25c`, 2026-07, still current) models the key-on half and forgets the key-off half:

1. `engine_timer_expired(0)` with CSM on calls `keyonoff(0xf, KEYON_CSM)` on channel 3, which sets bit `KEYON_CSM` of each operator's `m_keyon_live`, and marks the channel modified.
2. On the next `clock()`, `prepare()` runs for the modified channel. `fm_operator::prepare()` does `clock_keystate(m_keyon_live != 0)`: the key state becomes 1, `start_attack()` resets the phase and starts the attack. Then it clears the `KEYON_CSM` bit.
3. Nothing keys the operator **off** afterwards. `prepare()` only runs when a channel is modified or every 4096 samples (`m_prepare_count`). `m_key_state` stays 1, so the envelope proceeds through decay and sustain like a held note.
4. On the next overflow, step 1 repeats, but `clock_keystate(1)` with `m_key_state` already 1 sees no edge: no phase reset, no re-attack. Later overflows do nothing.
5. About 4096 samples later the periodic `prepare()` runs, `m_keyon_live` is 0 again, so the key finally drops and the operator releases; the next overflow then re-attacks. The audible result is a sustained note with a hiccup roughly every 84 ms at 48.6 kHz instead of a pulse train at the timer rate.

Measured on our vendored copy with a standalone probe (one carrier, AR 31, RR 15, Timer A every 40 FM samples, no manual key-on): the CSM output was **byte-identical to a manual key-on**, a full-level sustained tone. The same probe with the fix: a restart from phase 0 every 40 samples, envelope decaying between pulses; manual key-on unchanged.

Why "already much closer to the original" after our earlier work: the timer side was right (exact T-state expiry, the key-on landing on the right FM sample, `TsfmTimer_Test.CsmKeyOnSampleAligned`), so the first pulse and the pitch were correct; only the retrigger was missing.

## 4. What Furnace does

Furnace's YM2203 platform offers three cores: ymfm (default), Nuked-OPN2, and a cycle-level LLE core. The last two retrigger CSM natively. For ymfm, Furnace carries a local fix in its bundled copy, commit `26739bba` (tildearrow, 2023-02-05, "YM2612: fix CSM on ymfm"):

```cpp
// fm_operator::prepare()
clock_keystate(uint32_t(m_keyon_live != 0));
if (m_keyon_live & (1 << KEYON_CSM))
    if (!(m_keyon_live & (1 << KEYON_NORMAL)))
        clock_keystate(0);
m_keyon_live &= ~(1 << KEYON_CSM);
```

The CSM key becomes a pulse of zero length inside one `prepare()`: attack with phase reset, then immediate release, unless the operator is also keyed normally. Against the hardware's one-sample pulse the only difference is that an attack rate below 31 never gets a sample to progress in, which is also what happens on hardware because the envelope clock runs every three samples. A tune authored in Furnace relies on this behaviour, which is why it sounded wrong here.

Furnace's tracker UI also exposes CSM as an extra "CSM" channel in extended mode: the note sets Timer A's period, so the "pitch" of the CSM channel is the retrigger rate.

## 5. What unreal-ng ported

- `core/src/3rdparty/ymfm/ymfm_fm.ipp`, `fm_operator::prepare()`: Furnace's three lines, with a comment block naming the origin. Canonical diff: `ymfm-furnace-csm.patch` (this folder), applied with `patch -p1` from the ymfm directory, the same way as the TTD patch.
- No new state: `m_keyon_live` and `m_key_state` were already serialized, so the TTD payload stays 494 bytes and the `YmfmTtdPatch` stress runs unchanged.
- Test: `TsfmTimer_Test.CsmRetriggersEveryTimerATick`. Timer A every 400 FM samples, no manual key-on, RR 15: one burst per overflow, each starting on the overflow sample, quiet between. Negative control done: with the patch reversed the test fails (one sustained burst); with it, it passes.
- Documentation: `PATCHES.md` §4, `THIRD_PARTY_NOTICES.md`, and the CSM line in `hardware-reference.md`.

Commit: `279bbe2c` on `tsfm`.

## 6. Every other Furnace change to ymfm, and why it was not ported

Furnace's bundled ymfm (`5be1d6b`, 2026-09-13) differs from upstream in nine OPN-related files. The CSM fix is the only fidelity change for OPN.

| Change | Files | Decision |
|---|---|---|
| Frequency latch "armed" semantics: a high-byte write (`0xA4–0xA6`, `0xAC–0xAE`) arms the latch; a low-byte write commits only when armed, then clears the latch | `ymfm_opn.cpp` | **Not ported.** Introduced with Furnace's first ymfm import (`2879b5e4`, 2021-12-15, "arcade: add ymfm-based core"), no hardware rationale given. Nuked-OPN2 keeps `reg_a4` as a persistent latch and every `0xA0` write commits with it, which is what upstream ymfm does. Porting would change pitch for software that rewrites only the low byte. |
| TL ramp (`tl_ramp`, `m_actual_level`, `m_ramp_counter`) | `ymfm_fm.h/.ipp` | Not ported. OPP/OPZ feature; behaviour-neutral for OPN but adds per-operator state TTD would have to reason about. |
| `compute_volume` early-out also requires `eg_shift == 0` | `ymfm_fm.ipp` | Not ported. `eg_shift` is only non-zero on OPZ. |
| `m_timer_running[4]`, `assert` removed in `engine_timer_expired` | `ymfm_fm.h/.ipp` | Not ported. Four-timer chips (OPX/OPQ) only. |
| Per-channel/per-operator output capture and debug accessors (`m_output`, `m_special1/2`, `debug_fm_engine`, SSG/ADPCM `get_last_out`) | `ymfm_fm.*`, `ymfm_opn.h`, `ymfm_ssg.*`, `ymfm_adpcm.*` | Not ported. Tracker UI plumbing, no emulation effect. Worth borrowing if per-channel FM capture is ever wanted. |
| `array_size` helper, `std::array` → C array, virtual destructors removed, `snprintf` rewrites, `roundtrip_fp` rewrite | `ymfm.h`, `ymfm_fm.*`, `ymfm_opn.cpp` | Not ported. Style and portability; `roundtrip_fp` is arithmetically identical. |
| `if (index >= REGISTERS) return false;` after the assert in `write` | `ymfm_opn.cpp` | Not ported. Our callers mask the address to 8 bits first. |

## 7. Other CSM-related facts worth keeping

- xpeccy-plus's `tsfm-ymfm` branch (2026-09) also runs the YM2203 on stock ymfm `81aec25` with no CSM change, so it has the sustained-note behaviour described in §3. Its older C implementation (`ym-2203.c`) is an original core that keys channel 3 on from Timer A in "special mode" with a phase reset.
- ymfm's `prepare()` is not idempotent (it clocks key state and, now, releases the CSM key). That is exactly why our TTD patch persists the prepare scheduling instead of forcing `invalidate_caches()` on restore; the CSM patch does not change that reasoning.
- The CSM key-on lands on the FM sample at or after the Timer A expiry T-state (§5.2 ordering rule of the design); Timer A periods are whole multiples of the FM sample period, so the pulse is sample-aligned.
