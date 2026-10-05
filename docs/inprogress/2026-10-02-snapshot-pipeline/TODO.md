# TODO: one snapshot pipeline for every machine and every format

Design: [proposal.md](proposal.md). Plan: [PLAN.md](../PLAN.md) #84 (T3, owner: lower priority).
First user: the Sprinter ZX mode, phase Z5 ([tdd-zx-mode.md](../2026-09-28-sprinter/tdd-zx-mode.md) §9, Q4;
owner decision 2026-10-02: yes, via this shared pipeline, lower priority).

Status: proposal written 2026-10-02 (documents only, no code). Open questions Q1-Q8 in
[proposal.md §11](proposal.md#11-open-questions) wait for the owner.

| Step | Item | Size | Status |
|:--|:--|:--|:--|
| P0 | Golden commit digests for every SNA / Z80 / SZX fixture × creatable model on `master`; pin the suspected defects (Pentagon 1024 lock, ATM / TS-Conf after a cold reset, 128K on 48K) as current behavior | S-M | **done 2026-10-05** (branch `snapshot-pipeline`): `testdata/loaders/golden/commit-digests.txt` (884 rows: 52 fixtures × 17 machines; ram / ports / cpu / ay / misc hashes), `core/tests/loaders/snapshot/snapshotgolden_test.cpp` (one test per machine, ~0.3 s each), helper `_helpers/snapshotdigest.{h,cpp}`; defects pinned in `SnapshotDefects_Test`, see "P0 findings" |
| P1 | `SnapshotImage` + `SnapshotReport`; parsers (SNA, Z80, SZX, SPG, ZXP) fill the image; pipeline inside `LoadSnapshotStaged`; `LegacyCommit` = today's code | M | **done 2026-10-05** (branch `snapshot-pipeline`): `snapshotimage` / `snapshotreport` / `snapshotpipeline` in `core/src/loaders/snapshot/`; each loader builds its image from the staging and calls `snapshot::Pipeline::Plan` before its unchanged commit (SNA, Z80, SZX, SPG; ZXP builds one image per module, its group-level plan waits for Q7); `Emulator::LastSnapshotReport()` on every load path; SP-2 in `snapshotimage_test.cpp` (oracles: the raw bytes, libspectrum, the mhmt-verified SPG hashes); the P0 table is unchanged |
| P2 | Plan step, `ISnapshotCommitPolicy`, named-policy registry, `GetSnapshotPolicy()` on the port decoder | S | open |
| P3 | `commit` option + `inspect` on WebAPI / OpenAPI, MCP, CLI, Lua, Python; recipe `.recipe/media/load-snapshot.md` | S-M | open |
| P4 | Sprinter ZX commit (= Sprinter Z5): cell-table mapping, refusal outside ZX mode, T-ZX-11 / T-ZX-12 | M | open |
| P5 | Fit checks and machine policies after the owner's answers: 128K on 48K (Q1), 48K on 128K (Q2), Pentagon 1024 compatibility, ATM family, TS-Conf | M | open |
| P6 | Save path: capture → image → writer; 48K SNA writer stops touching live RAM | M | open |
| P7 | One model-switch orchestrator for SZX / SPG / RZX; Qt uses it (Q6) | S | open |
| P8 | TTD: a load during a recording continues the track with a full checkpoint + marker (Q5), after TTD v2 regions | S-M | open |
| P9 | Clean-up: legacy commits read the image; remove duplicated staging and SNA dead code | M | open |

## P0 findings (2026-10-05)

The table records `master` as it is. Facts it and `SnapshotDefects_Test` pin (each flips in the step named):

- **Pentagon 1024 lock (P5, SP-6): confirmed.** A 128K SNA with #7FFD bit 5 (lock) set maps page 39 (32 + 7) at #C000;
  the 128K Pentagon and the unlocked file map 7. Cause as in the proposal: the reset leaves #EFF7 = 0 (1 MB paging).
- **ATM3 and ATM710: confirmed, worse than expected.** After a 128K SNA the window at #C000 is **unmapped** (the pager is
  not in its 128K form); ATM450, TS-Conf, Profi, Scorpion and the Pentagons map bank 7. (TS-Conf was suspected: it
  is fine from the BASIC reset state; the SP-8 screen digests from the BIOS menu state are still to do in P5.)
- **128K file on a 48K (Q1): confirmed.** It loads, `ok`, leaving a 128K paging byte (#17) in a 48K machine.
- **Sprinter (P4): confirmed.** The load succeeds and bank 7 is physical page 7, a system page; nothing refuses.
- **48K SNA vs 48K Z80 (Q2): confirmed.** On the 128K and the Pentagon a 48K SNA leaves #7FFD = #10 (unlocked), a 48K
  Z80 leaves #30 (locked).
- **Refusals:** 216 of 884 rows are `refused`, all SZX on another model (the SZX model check); no SNA / Z80 row is refused
  and none throws on the valid fixtures.
- The table is deterministic (two rewrites are byte-identical) and a mutation (dropping the lock bit of the Z80
  48K commit) fails 17 machine tests and the Q2 test.

To rewrite after an approved change: `UNREAL_SNAPSHOT_GOLDEN_UPDATE=1 core-tests --gtest_filter='SnapshotGoldenRewrite*'`,
review the diff, list the changed rows in the commit message.

## P1 notes (2026-10-05)

- **The image is built beside the staging, not instead of it.** `LegacyCommit` still reads each loader's private staging,
  so the P0 golden table is bit-for-bit the same; moving the commits onto the image is P9.
- **Oracles for SP-2** are independent of the loaders: the raw bytes of the file sliced by the published layout (SNA, Z80
  with its own RLE unpacker, ZXP), libspectrum's dump next to each SZX file, and the SPG hashes verified against lvd's mhmt.
- **Found on the way:** a 128K SNA whose paged bank is 5 or 2 is 147487 bytes (six further banks, the third bank repeats one
  already stored), not 131103; the staging handles it, the oracle now states it. The Z80 v2 staging does not keep the model
  byte (`_modelCode` is set for v3 only), so the image reads it from the header.
- **Extensions are descriptors** (origin, kind, size, note); payloads stay with the stage until a commit reads the image (P9).
- **ZXP** gives four images (`LoaderZXP::BuildImage(module)`) with the group registers in an extension; no plan hook yet.
- SZX's `Outcome` and the report's differ in numbering: `LoaderSZX::AppendReport` is the one place that maps them.
- Open for P2: the plan step takes `Options` (`commit`) and returns legacy or a refusal for an unknown name; the machine
  policy and the registry come next.
