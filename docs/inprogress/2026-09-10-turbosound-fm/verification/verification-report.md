# TSFM Design — Verification Report (revision 1 → revision 2)

**Date:** 2026-09-12
**Checked:** revision 1 of `tsfm-tdd.md` (2026-09-10), claim by claim, against three things:
- **Code:** unreal-ng `master` @ `6c72e4dd`
- **ymfm:** upstream @ `81aec25ccbb98f4873a255f7551ac4dadac59b4a`, including small test programs
- **Hardware:** the NedoPC board logic source, schematic, manuals and player code ([../hardware-reference.md](../hardware-reference.md))

Verdicts: **OK** = confirmed; **WRONG** = replaced in revision 2; **PARTIAL** = right in part, refined.

## Files in this folder

| File | What it is | How to run |
|---|---|---|
| `ymfm-ttd.patch` | The local ymfm patch (§9.5 of the design) | `patch -p1` inside a copy of `ymfm/src` |
| `stress.cpp` | TTD stress harness: randomized YM2203 traffic, save/restore at every step | `c++ -std=c++17 -O2 -I<ymfm/src> stress.cpp <ymfm/src>/ymfm_{opn,ssg,adpcm}.cpp -o stress && ./stress` — prints `PASS`/`FAIL`, ~25 s |
| `bench.cpp` | `clock_fm` cost | same build line; prints ns per call |

---

## A. Hardware claims

| # | Revision 1 said | Verdict | Truth (evidence) |
|---|---|---|---|
| A1 | YM2203 clock is a fixed 3.5 MHz | **WRONG** as a rule | 2 × host AY clock; the board has no oscillator (CPLD `CLK2OUT = … xor CLK1`, schematic). In emulated time that is 1 master clock = 1 T-state on every model (`audio.h:17-18`). |
| A2 | FM period = 4.5 AY ticks | OK | Holds on every host because both derive from one clock. |
| A3 | Control word bits 0/1/2 = chip / status select / FM enable | OK | CPLD `CUR_CHIP.d = DA[0]`, `GET_STAT.d = DA[1]`, `FM_DIS.d = DA[2]` |
| A4 | Chip 0 = bit0 = 1 (`0xFF`); reset selects it | **WRONG** | Reset clears `CUR_CHIP` to 0 → the `0xFE` chip (CPLD `CUR_CHIP.clrn = _RES`; manual "chip D1"). TFM Compiler: `statuschip0 = %11111000`. Unreal CHRV: `active_ay = val & 1`, reset 0. MiSTer RTL has it inverted. |
| A5 | In AY mode, addresses ≥ `0x10` are not latched; data is dropped | **WRONG** | The CPLD passes every address `< 0xF8` and all data to the YM2203 in both modes; `FM_DIS` only gates DAC data (`FM1_OUT = FM1_IN and not FM_DIS.q`). Copied from MiSTer's `ym_acc`. |
| A6 | A control word clears the address-accepted flag | **WRONG** | `_WR` is held inactive for control words; the chip's latch is untouched. ALCO §5.1: "the current register does not change". |
| A7 | Register read with FM address returns `0x00` | **PARTIAL** | Only ymfm returns 0; MiSTer, Unreal and Xpeccy return `0xFF`. Silicon is unknown (H2). Revision 2 uses `0xFF`. |
| A8 | "TFM players write pairs every ~7 µs; real chips accept faster writes" | **WRONG** | Players poll busy before address and before data (ALCO §5.1; `TFMCOM12.$H` `WaitStatus`). The busy flag must set and clear on the right T-state. "Never drop writes while busy" is kept. |
| A9 | Prescaler writes are rare; warn on each | **WRONG** | Every player writes `0x2F` then `0x2D` at init (ALCO §5.3; `TFMCOM12.$H`). |
| A10 | SSG clock at prescaler /3, /2 is ×2 / ×3 (/2 → 5.25 MHz) | **WRONG** | ×2 / ×4 (SSG dividers 4/2/1, `ymfm_opn.h:274-285`); /2 → 7.0 MHz, measured |
| A11 | FM audio gated by `fm_ena`: "unverified on the board" | **PARTIAL → confirmed** | CPLD forces DAC serial data low; rev. C manual "FM blocked by default" |
| A12 | Mix `2A + B`, FM mono, both chips summed | OK (shape) | Schematic: A/C 24 kΩ, B 47 kΩ both sides, FM1/FM2 24 kΩ both sides. MiSTer's 8-bit saturation is not on the board. |
| A13 | FM full scale ≈ one SSG channel peak-to-peak | **PARTIAL** | Only a MiSTer assumption. Schematic weights are equal, so the ratio is set by voltage swing: estimate FM FS ≈ 2–2.5 × SSG AC amplitude, consistent with MiSTer's 2.0. Unreal calibrated by ear. Unmeasured (H1). |
| A14 | YM2203 SSG = YM2149 volume curve | OK | ALCO, ymfm/MAME, ZXMAK2 agree; not measured on silicon |
| A15 | Timer A 10-bit, IRQ not connected | OK | ymfm `0x24/0x25`; schematic |
| A16 | "Emulated in Unreal Speccy since 2005" | **WRONG** | Unreal 0.36b, 28 Feb 2007 (`history.txt:748`); the board design dates from 2005 |
| A17 | TSFM Pro: `0xF7`/`0xFF` toggle SAA | OK | zx-multisound `saa_clk_en <= ~zxd[3]` |

## B. ymfm claims

| # | Revision 1 said | Verdict | Truth (evidence) |
|---|---|---|---|
| B1 | `clock_fm()`, `m_last_fm`, `m_fm` reachable from a subclass | OK | protected (`ymfm_opn.h:468-478`); the design's classes compile as written |
| B2 | FM word is a "14-bit sum" | **WRONG** | One operator is 14-bit; channels sum in int32; `roundtrip_fp` clamps to int16 and truncates to the DAC step. One carrier at TL=0 peaks ±8168 (measured). |
| B3 | Engine usable after construction | **WRONG** (omission) | The constructor does not call `reset()` and FM registers are uninitialised; `reset()` must be called |
| B4 | `fm.reset()` is harmless to the AY | **WRONG** | `ym2203::reset` → `m_ssg.reset` → `ssg_reset()` on the override → would reset `SoundChip_AY8910`. Revision 2's adapter makes it a no-op. |
| B5 | ymfm reset restores prescaler /6 | **WRONG** (omission) | `reset()` leaves `m_clock_prescale` unchanged. Revision 2 writes address `0x2D` on reset. |
| B6 | Busy = 32 · prescale clocks after data write; writes never dropped | OK | `ymfm_opn.cpp:827`; no busy check in `write_*` |
| B7 | SSG data writes bypassing ymfm behave the same | **PARTIAL** | Bypassing skips `ymfm_set_busy_end`. Revision 2 sets busy explicitly. |
| B8 | Timer A `(1024 − N) · 72` | **PARTIAL** | True only for the 10-bit value `TA = reg24<<2 \| reg25&3`. Timer B is at `0x26` (not `0x25`): `(256 − TB) · 1152`, first load minus `m_total_clocks & 15` (`ymfm_fm.ipp:1480-1587`). |
| B9 | Timer advance in 16-clock steps | **WRONG** | Loses the overshoot (8 clocks per timer-A period). Revision 2 steps exactly to the next expiry. |
| B10 | "ymfm handles the internal key-on delay" | **WRONG** | Key-on applies at the next `clock()`; no modelled delay |
| B11 | Prescaler change on address write, `ssg_prescale_changed` callback | OK / PARTIAL | Correct, but the callback fires on no-op writes and on every `save_restore` |
| B12 | Build: compile `opn`, `ssg`, `misc`, `adpcm` | **PARTIAL** | `adpcm` is required (2608/2610 classes in `opn.cpp`); `misc` is not. About 16 `-Wunused-parameter` warnings would fail the `-Werror` build. |
| B13 | ymfm save state is fixed-size and allocation-free with a reserved vector | OK | 482 bytes upstream / 494 patched; `resize(0)` then `push_back` |
| B14 | TTD roundtrip via `save_restore` is exact | **WRONG** | See §D. Restore reproduces the saving chip, but the save itself perturbs later output, so a run with checkpoints differs from one without and a replay diverges from its recording once it crosses a checkpoint. |
| B15 | Bypassing `generate()` is safe | OK | `generate()` only schedules `clock_fm` and resamples the SSG; timers and busy go through the interface |

## C. unreal-ng code claims

| # | Revision 1 said | Verdict | Truth (evidence) |
|---|---|---|---|
| C1 | Device switch "at sound-stack rebuild / reset" | **WRONG** | No rebuild exists. `SoundManager` is built once in `Core::Init`; `reset()` never re-reads config. Revision 2: choose in the constructor (Covox pattern). |
| C2 | Timers/busy advance in the render loop | **WRONG** (design flaw) | The render loop is skipped in turbo (`_synthesisSuppressed`) and with sound off (`soundmanager.cpp:397-414`). `SoundManager::handleFrameStart` returns before the device in turbo (`:382`); `MainLoop` skips `SoundManager::handleFrameEnd` in turbo (`mainloop.cpp:464-480`). The program-visible busy flag would freeze and TTD replay would depend on audio settings. Revision 2: chip core / output stage split. |
| C3 | Config `[SOUND] TurboSound = AY \| FM` | OK, with caveat | The key is new. Shipped inis already carry unparsed `[AY] Chip=YM2203`, `Scheme=AYX32`; reusing them would enable TSFM everywhere. |
| C4 | `SoundChip_TurboSound` bases: `PortDevice`, `TTDSerializable` | **PARTIAL** | Also `PortDecoder` (`soundchip_turbosound.h:14`); `reset()` overrides its pure virtual |
| C5 | Add `peripheralId()` to the interface | **WRONG** | `TTDSerializable::TTDPeripheralId()` exists; the registry is keyed by the id passed at `timetravelmanager.cpp:1042` |
| C6 | Legacy combined `_ayBuffer` / `getAudioBuffer` matters for bit-identity | **WRONG** | No external users |
| C7 | SSG pan 1.0, one channel ±1/6 | **WRONG** | Max pan is 0.9 (ABC A = 0.9/0.1) → ±0.15 (`soundchip_ay8910.cpp:570-606`) |
| C8 | `FilterDecimator` constant `DEFAULT_INPUT_RATE`, 6 AY instances | **PARTIAL** | The name is `INPUT_RATE`; there are 4 AY instances |
| C9 | Tests in `core/tests/sound/` | **WRONG** | `core/tests/emulator/sound/`, `core/tests/debugger/ttd/` |
| C10 | Speed multiplier handling copied from legacy | **flag** | Legacy AY scales T-states by the host multiplier although `z80->t` already runs `frame × multiplier` — suspected host² over-render. Not copied into the TSFM core; tracked separately. |
| C11 | TTD ids not persisted | **WRONG** (header comment) | Ids are written to session files (`ttd.ksy:421`); id 4 is frozen |
| C12 | Legacy reset chip | **flag** | Legacy resets to the `0xFF` chip (AYX32 numbering). The NedoPC TurboSound resets to `0xFE`. Not changed here. |
| C13 | Cross-kind TTD session load | **gap** | Would silently leave the other device stale (`missingBlobs`). Revision 2 refuses it. |
| C14 | `SoundManager` calls device `handleFrameEnd` | **WRONG** | Never called; revision 2 adds the call (output stage only) |
| C15 | `AYLogRecord` has room for flags | OK | 13 bytes padded to 16. Separately, `tacts` is u16 but frames are 69 888 / 71 680 T: a truncation bug, tracked separately. |
| C16 | FM1/FM2 only need an enum entry | **PARTIAL** | Also `deviceBuffer()` and mixer switch arms, recording names, multitrack dialog, audio settings widget |
| C17 | Chip model forced to YM2149 | **gap** | The Qt combo can override it at runtime and TTD does not restore it. Revision 2 locks the combo for TSFM. |
| C18 | Performance budget ≤ +60 µs/frame total | **WRONG** | `clock_fm` alone: 29–63 ns × 1 992 calls = 58–126 µs/frame (M1 Ultra), and in revision 2 it runs always |
| C19 | 3rd-party vendoring "like blip_buf" | **PARTIAL** | Every `.cpp` under `core/src` is globbed with PCH and `-Werror`; ymfm needs `SKIP_PRECOMPILE_HEADERS` and warning suppression |

---

## D. The ymfm save/restore problem (drives R4)

**Symptom.** Upstream `fm_engine_base::save_restore` ends with `invalidate_caches()`, and `ym2203::save_restore` calls `update_prescale()`, on save as well as restore. `invalidate_caches()` forces `prepare()` on the next `clock()`.

**Why that matters.** `prepare()` is not a pure refresh:
- it calls `clock_keystate()`, which moves the operator key state forward;
- it clears the CSM key-on bit (`ymfm_fm.ipp:427-438`).

Its normal timing is set by `m_modified_channels` and `m_prepare_count`, a periodic sweep every 4 096 samples. Neither is saved.

**Measured with upstream** (`stress.cpp`; 6 seeds, 4 M steps each, ~19 k random writes per seed):

| Property | Result |
|---|---|
| P1: output + status stream of a chip that saves vs one that never saves | differs on 6/6 seeds |
| P2: a fresh chip restored at each checkpoint vs the never-saved chip, per segment | 54 – 100 374 mismatching segments |
| Control: restored chip vs the chip that produced the save (variant of `stress.cpp` comparing against run B) | 0 mismatches on 6/6 seeds |

**Reading.** Upstream restore is faithful to the moment of the save, but the save changes the chip.

**Why that breaks TTD:**
- a recording saves at every frame boundary, and a seek replay does not re-save;
- so the replay diverges from the recording once it crosses a boundary;
- and a recorded session plays differently from the same session run without TTD.

**Why a save-only fix is not enough:** skipping `invalidate_caches()` on save alone would leave restore forcing `prepare()`, so restore would then diverge. The patch has to change both.

**Patch** (`ymfm-ttd.patch`, 38 lines):
1. Save and restore `m_active_channels`, `m_modified_channels`, `m_prepare_count`.
2. Remove `invalidate_caches()`. On restore, rebuild each operator cache with `m_regs.cache_operator_data(...)`, without `prepare()`. Every register write marks all channels modified (`ymfm_fm.ipp:1412, 1563`), so a cache with no pending modification equals a fresh computation.
3. Call `update_prescale()` on restore only.

**Measured with the patch:**

```
seed 1 every 1 events 18891: save side-effect none | restore-continue mismatches 0 | state 494 bytes fixed
seed 2 every 839 events 19020: save side-effect none | restore-continue mismatches 0 | state 494 bytes fixed
seed 3 every 3758 events 18856: save side-effect none | restore-continue mismatches 0 | state 494 bytes fixed
seed 4 every 1677 events 18801: save side-effect none | restore-continue mismatches 0 | state 494 bytes fixed
seed 5 every 4596 events 18963: save side-effect none | restore-continue mismatches 0 | state 494 bytes fixed
seed 6 every 2515 events 18910: save side-effect none | restore-continue mismatches 0 | state 494 bytes fixed
PASS
```

Seed 1 saves and restores at **every** 8-clock step, which is "any point in time" at finer than instruction resolution.

**Coverage limits of the harness:**
- It drives ymfm through a timer/busy interface equivalent to the design's `Ym2203Interface`, with no SSG override attached.
- Register traffic is random, not musical. It covers:
  - key-on/off, all operator registers, 3-slot and CSM modes;
  - both timers and all three prescalers;
  - SSG-EG bits, through random values in `0x90–0x9F`.
- Phase 3 of the plan ports it into the gtest suite and adds a full-emulator TTD test on a real player.

## E. Measurements

| What | Value | Where |
|---|---|---|
| `clock_fm`, silent chip | 29.1 ns | `bench.cpp`, M1 Ultra, clang `-O2` |
| `clock_fm`, 3 ch × 4 op playing | 63.1 ns | same |
| 2 chips per Pentagon frame (1 992 calls) | 58 – 126 µs | derived |
| ymfm ym2203 state | 482 B upstream / 494 B patched | `stress.cpp` |
| TSFM TTD payload | 1 142 B | design §8.2 |
| One carrier TL=0 FM word peak | ±8168 (+8160 / −8176 after DAC quantisation) | ymfm test program |
| Timer A `0x24=0x80` / timer B `0x26=0x80` | 36 864 / 147 456 clocks | ymfm test program |
