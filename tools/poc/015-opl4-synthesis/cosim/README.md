# cosim/ — co-simulation harnesses

Co-simulation for libopl4 per the core TDD test plan (§12.2 golden vectors,
§12.3 differential, §12.6 determinism): drive libopl4 and a reference engine
from the **same wave-memory image and the same register scripts**, then
compare what comes out. The unit tests in `../tests/` pin tables and pure
functions; these harnesses pin *behaviour* — and they earned their keep
during bring-up by catching three real engine bugs (see "Track record").

## Layout

| File | Purpose |
|---|---|
| `cosim-ymfm.cpp` | Differential harness: libopl4 vs ymfm's `ymf278b` |
| `cosim-oracle.cpp` | Golden self-oracle: libopl4's own bit stream vs committed digests |
| `cosimdrv.h` | Shared driver (chip + SRAM image + register scripts) |
| `fetch-refs.sh` | Fetch the pinned ymfm revision into `refs/` (gitignored) |
| `golden/oracle.txt` | Committed golden digests (regenerate via `golden/generate.py`) |
| `refs/`, `bin/`, `build/` | Gitignored: fetched sources, binaries, CMake dir |

## Running

```sh
./fetch-refs.sh                      # once; needs network, pins ymfm @ 81aec25
cmake -S .. -B ../build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCOSIM=ON
ninja -C ../build cosim-ymfm cosim-oracle
./bin/cosim-ymfm                     # differential: 6/6 scenarios
./bin/cosim-oracle                   # self-oracle: 15/15 cases
./bin/cosim-ymfm --dump ../scratch/x # optional: write raw .pcm streams
```

The oracle alone builds without `refs/` (no ymfm needed); only `cosim-ymfm`
requires `fetch-refs.sh` first.

## Tiers

| Tier | Reference | Status | What it proves |
|---|---|---|---|
| 1 | **ymfm** `ymf278b` @ `81aec25` | **Automated** (6 scenarios) | PCM position/wrap/decode, envelope shape, FM pitch, block-mix semantics, in-process determinism |
| 2 | **Golden self-oracle** | **Automated** (15 cases) | libopl4's exact bit stream — regression fence + cross-machine determinism (§12.6; the golden digests must reproduce on every platform) |
| 3 | openMSX / Nuked-OPL3 / HDL (mangOPL4) | **Not automated** in this PoC — see below | Full-system and die-level confirmation |

### Why openMSX and friends are tier 3 (documented, not automated)

- **openMSX** (`YMF278.cc`, post-2016 ValleyBell fixes) is the most accurate
  published **PCM** model and much of its hardware-verified behaviour
  (separately-clipped TL/envelope, TL interpolation, loop-overrun glitch) is
  already *adopted into* libopl4 via the TDD record. But it is a full-system
  MSX emulator: a chip-level A/B needs a booted machine plus register pokes
  through emulated I/O — a harness of its own. It also shares lineage with
  the behaviours we deliberately **reject** (D5: the early 0.75 linear
  power-table guess, +0.51 dB/stage), so it is a source in the record rather
  than a clean automated oracle. Manual spot-checks against openMSX WAV
  dumps remain the recommended procedure for anything the ymfm harness
  cannot see (reverb/DAMP envelope corners, bus timing under MA=1).
- **Nuked-OPL3** is die-shot-derived YMF262 — the right FM oracle *after*
  compensating the 49716 vs 49516.4 Hz grid (§12.2), but again a separate
  harness; ymfm's OPL3 core already covers the FM ground at the same
  fidelity class for this PoC's statistical comparisons.
- **HDL co-simulation** (Verilator + an open OPL4/OPL3 core, §12.4) is the
  right tool for the reducer (HoldDrop, D2) and bus-level timing; budgeted
  as future work, not skipped silently.

## Differential scenarios (cosim-ymfm)

Both engines see identical SRAM contents and PCM register bytes; FM uses
per-engine addressing (libopl4's linear operator map vs the classic OPL3
map, `cosimdrv.h: KeyOnFm`).

| Scenario | Verdict criterion |
|---|---|
| `pcm-position` | Loop-overrun / one-shot position traces decoded from the output streams agree **exactly**, frame for frame; the E=0 degenerate corner is a documented model difference — each engine must follow its own model exactly (libopl4: openMSX linear one-shot; ymfm: wrap-every-step) |
| `pcm-widths` | 8- and 12-bit sample decode traces agree exactly |
| `pcm-envelope` | 6 dB decay time ratio within **[1.6, 2.4]** of the documented 2× mid-rate model difference; sustain plateau within 3 dB |
| `fm-tone` | Pure-carrier zero-crossing rate (pitch) ratio 1.000 ± 0.02; per-engine TL ladder ≈ 0.2512 (−12 dB→−24 dB step); stereo balance |
| `mix` | Block-mix attenuation ratios for codes 0/3/7 match within 0.03; code 7 = mute in both |
| `determinism` | libopl4 stream reproduced bit-exactly across two chips in-process |

## Documented model differences (expected, not bugs)

- **Envelope clocking** — libopl4 gates rows every `2^(12−rate/4)` samples
  (Valley Bell lineage, per the TDD); ymfm's 5.11 counter nets
  `2^(11−rate/4)` — measured exactly 2× at mid rates (rate 32: 2048 vs
  1024 samples per 6 dB). Both agree at rate 0/15 and on sustain plateaus.
- **Power-table mantissa** — libopl4 unity = triple `(x*2047)>>11` chain
  (envelope, TL, pan each round through the 2047-mantissa table ≈ 0.9985);
  ymfm PCM = `(x*8168)>>15` (≈ 0.2485). Position traces use a per-engine
  empirical scale, which removes both.
- **FM headroom** — libopl4 emits the 16-bit chip stream (D10 adder);
  ymfm's OPL3 core is ~13-bit internally. Levels are compared per engine
  (TL ladder), never across.
- **FM grid → output** — libopl4 models the HoldDrop reducer on the chip
  boundary (D2) then resamples; ymfm decimates 171/192 without
  interpolation. Only pitch is compared exactly; waveform-level FM A/B is
  meaningless across different decimators.
- **PCM interpolation** — ymfm's `fetch_sample` ignores the fractional
  position bits; traces therefore use integer steps. The libopl4
  interpolator is covered by `VecInterpGolden` in `../tests/`.
- **PCM end S == 0 corner** — the header's stored complement 0 is a full
  64 KiB sample. libopl4 follows openMSX's chip comparator (`pos + S >=
  0x10000` never trips, 5426b4b1): a linear one-shot. ymfm decodes S == 0
  to end == 0 and wraps every step (`pos += increment + loop`). The
  `end0-degenerate` trace checks each engine against its own model.

## Track record

The differential harness found three real engine bugs during bring-up —
exactly the class of bug unit tests cannot see:

1. **FM pitch ×9.5 too high** — `PhaseStep` contradicted its own A440
   comment (F-number 582 / block 4 = A440, D1 §4.2). Rewritten onto the
   datasheet formula in a 12-bit F-number fraction domain.
2. **Operator scale 2× over** — 13-bit sine table shifted `<<4` instead of
   `<<3`, pushing unity past the 16-bit rail (TL-0 clipped, ladder shifted
   −6 dB). Now `wave << 3` = unity at the rail, matching PCM unity in the
   D10 single adder.
3. **PM/AM LFO models wrong** — PM ran at 3.02 Hz (should be 6.04 Hz,
   period 8192) as an absolute-frequency nudge instead of
   F-number-proportional cents with depth bits; AM ran at 6.04 Hz (should
   be 3.69 Hz, period 13440) without the 4.8/1.2 dB depth select.

It also exposed a shared register-map bug in the test framework
(`KeyOnFmCh0` wrote B0 = 0x30 = F-number 3 instead of 0x33 = 0x303), and the
oracle's `save-restore-replay` case forced an explicit statement of the
restore contract: a snapshot carries **absolute chip time** — the restoring
host must keep feeding timestamps from that point.

## Baseline policy

Golden digests are a fence, not a goal. If `cosim-oracle` fails after an
intentional change:

```sh
python3 golden/generate.py   # rebuild + regenerate + checklist
```

Re-run `cosim-ymfm` (must stay 6/6), review the digest diff, then commit the
new baseline together with the change that justified it. A digest change
with no explaining diff is a regression, full stop.
