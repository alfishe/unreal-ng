# E2E record — ProfROM instantiation + 1024 KB RAM ladder

> Transferred from `scratch/e2e-prof/results.md` (2026-09-09). Raw artifacts
> (`boottrace.json`, ids, logs) remain in `scratch/e2e-prof/`.
>
> **Status update (post-transfer):** the two instantiation fixes described below
> are committed as `535b8238` ("fix(emulator): PROFSCORP instances failed to
> create - unknown model 10"). The Task 7 boundary analyzed at the end of this
> record has since been crossed — the ProfROM read-strobe state machine is
> implemented and live-verified; see [e2e-profrom-quadrants.md](e2e-profrom-quadrants.md).

Date: 2026-09-09. Binary: `cmake-build-release/bin/unreal-qt.app` @ merge `26b99b3a`
plus two working-tree fixes (decoder factory case for `MM_PROFSCORP`,
`data/configs/profscorp/unreal.ini`). Model: `PROFSCORP`, ROM
`data/rom/scorp_prof401.rom` (512 KB, 8 quadrants), RAM 1024 KB (twin at 256 KB).

## Fixes needed to instantiate the model (now commit 535b8238)

1. `PortDecoder::GetPortDecoderForModel` had no `MM_PROFSCORP` case → instance
   creation threw "unknown model 10". Added: both Scorpion models share
   `PortDecoder_Scorpion256` (it branches on `mem_model == MM_PROFSCORP` for the
   `#7EFD` arm). Also added the diagnostics modelName mapping.
2. `Config::GetConfigFolderForModel(MM_PROFSCORP)` → `profscorp` — no such
   config dir existed and `LoadConfig` fails hard. Created
   `data/configs/profscorp/unreal.ini` (copy of `scorpion/` with `HIMEM=PROFSCORP`).

## Results

| Check | Result |
|---|---|
| `POST /emulator/start {"model":"PROFSCORP","ram_size":1024}` | PASS — 'ZS Scorpion + PROF ROM', 1024 KB |
| 512 KB ROM load, quadrant validation (8 banks) | PASS — no size errors |
| Boot | PARTIAL — see below |
| E2E-7: banks 16-63 @ 1024 KB | PASS — tags at banks 37 (55) and 61 (61); controls at banks 5/13 read 0; interpreter healthy (`0 OK, 0:1`) |
| E2E-7: 256 KB clamp | PASS — 37→5 (PEEK 55), 61→13 (61 via both `0x18` and `0xD0` selectors); verified by a self-contained readback routine storing results at `#9000` (bank 2) |
| `#7EFD` window latch decode | PASS — `OUT 0x7EFD val 0x11 pc 0x1E7D decodedPort 0x7EFD` (wins over the `#7FFD` arm, live interpreter code) |

## Boot behavior and the Task 7 boundary (historical, pre-Task-7 analysis)

Quadrant 0 boots and the machine runs (interrupts OK, ends in 48K BASIC,
pc `0x10AC`, `#0000` = ROM1 signature `F3 AF 11 FF FF`), but the **boot menu
never appears**. Port trace (`scratch/e2e-prof/boottrace.json`, 13.3k events):

- `OUT #1FFD=0x12 @ pc 0x001C` (monitor latched in immediately), RAM probe
  `7FFD=0x17..0x10 @ 0x0695` — same shape as the base ROM boot.
- The RAM-resident monitor part then arms TR-DOS (`7FFD=0x10`, `1FFD=0x10` @
  `0xE4xx`) with **zero keyboard polls** (all 4000 `IN #FE` are post-boot,
  pc `0x0296` in ROM1) and **zero `#7EFD`-pattern accesses** — no menu wait.
- ProfROM Q0 differs from `scorpion.rom` in all four pages (Q0 page1 = the
  v2.95 48K build); the monitors diverge at the patched bytes near `0x268`
  (base `JP #259F` → ProfROM `CF 8C 00`).

Per hardware-reference §5.2, quadrant switching works by **reads of #0000-#0003
while the service ROM is paged** (the `set_scorp_profrom()` transition table) —
the read-strobe state machine (Task 7) was not implemented at the time, `profrom_bank` stayed
0, and the ProfROM monitor falls through to the no-menu path. Task 7
(`ScorpionRomWindow` + `OnRomRead` strobe hook + `p7EFD` consumption +
`profrom_mask` by image size) is the prerequisite for the real ProfROM boot
menu and ROM-disk — since implemented; see [e2e-profrom-quadrants.md](e2e-profrom-quadrants.md).

## Incidents

- `32253 = 0x7DFD`, not `0x7EFD` (= 32509) — first latch probe wrote a `#7FFD`
  mirror (harmless, `p7FFD=0x11`).
- 256K twin crashed once mid-test and re-booted; the monitor's RAM probe
  zeroed the bank-5 tag before it could be read (page scan proved it; re-run
  stable).
- BasicEncoder TR-DOS-mode injection intermittently 400s with "Invalid E_LINE
  address: 0" — retry after ~1.4 s always succeeds.
- **Stale `build.ninja`**: a 23:31 regeneration snapshot missed the committed
  Scorpion test files (glob ran in a bad window) — today's first suite run
  showed 2185 tests. After `cmake -S . -B cmake-build-release -G Ninja` +
  rebuild: **2257/2257 PASSED, 223 suites** (64 Scorpion tests restored + 8 new
  from master).

Artifacts: `scratch/e2e-prof/` (`boottrace.json`, ids, logs). RAM bank routine:
`#8000` (bank-2 fixed window), tags via `#1FFD` bits 4/6/7 only — no `#7FFD`
writes needed, safe under the 48K interpreter.
