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
| P2 | Plan step, `ISnapshotCommitPolicy`, named-policy registry, `GetSnapshotPolicy()` on the port decoder | S | **done 2026-10-05** (branch `snapshot-pipeline`): `snapshotpolicy.{h,cpp}` (`Verdict` Decline / Take / Refuse, `ISnapshotCommitPolicy`, `SnapshotPolicies` registry); `Pipeline::Plan` returns a `Decision` (Legacy / Take / Refuse) in the proposal's order: the caller's name (unknown = refused with the known names; a named policy that declines = refused, never a silent fallback), the machine's policy, [fit checks: P5], the legacy commit; `PortDecoder::GetSnapshotPolicy()` (+ `SetSnapshotPolicy`), nullptr everywhere; every loader commits through the policy on Take; SP-4 with fake policies (`SnapshotPlan_Test`) |
| P3 | `commit` option + `inspect` on WebAPI / OpenAPI, MCP, CLI, Lua, Python; recipe `.recipe/media/load-snapshot.md` | S-M | **done 2026-10-05** (branch `snapshot-pipeline`): `Emulator::LoadSnapshot(path, reportedPath, options)`, `InspectSnapshot`, `SnapshotLauncher` (`commit`, `report`, `Inspect`); WebAPI `snapshot/load {commit}` + `report`, `POST snapshot/inspect`, `info.report` (+ OpenAPI); MCP `load_software {commit, inspect}`; CLI `snapshot load --commit`, `snapshot inspect`, `snapshot info`; Lua `snapshot_load(path, commit)`, `snapshot_inspect`, `snapshot_report`; Python `snapshot_load(..., commit)`, `snapshot_inspect`, `snapshot_report` (module and `Emulator`); Qt shows a refusal's reason; checked live on the WebAPI, the CLI and Lua (Python is off in this build: its translation unit was syntax-checked against pybind11) |
| P4 | Sprinter ZX commit (= Sprinter Z5): cell-table mapping, refusal outside ZX mode, T-ZX-11 / T-ZX-12 | M | **done 2026-10-05** (branch `snapshot-pipeline`): `SprinterZxSnapshot` (policy `sprinter-zx`, `Instance()` handed out by `PortDecoder_Sprinter` via `SetSnapshotPolicy`, and registered by name); `SprinterMemory::RefreshZxShadow`, `PortDecoder_Sprinter::SetPagingFromSnapshot` (the `#7FFD` latch extracted as `Latch7ffd`); the 38 SPRINTER golden rows that loaded now say `refused` (a fresh Sprinter is in no mode), the other 16 machines unchanged |
| P5 | Fit checks and machine policies after the owner's answers: 128K on 48K (Q1), 48K on 128K (Q2), Pentagon 1024 compatibility, ATM family, TS-Conf | M | open |
| P6 | Save path: capture → image → writer; 48K SNA writer stops touching live RAM | M | **done 2026-10-06** (branch `snapshot-p6`, see "P6 notes"): `snapshotcapture.{h,cpp}` (the machine's 128K view, `QuerySaveFormats`, `SaveSnapshotFile`), SNA / Z80 written from the image, `Emulator::SaveSnapshot` waits for the pause and keeps a `LastSaveResult`, `SnapshotSaveFormats`; reasons + formats on WebAPI (`GET snapshot/formats`, the save answer), CLI (`snapshot formats`), Lua, Python, Qt (items disabled with the reason, dialog filters follow the machine) |
| P7 | One model-switch orchestrator for SZX / SPG / RZX; Qt uses it (Q6) | S | **done 2026-10-06** (branch `snapshot-p7`): `SnapshotLauncher::NeedOf` (what a file needs of the running machine: SPG = TS-Conf, SZX = a machine that fits by `LoaderSZX::Suits`); `switchModel` is optional: an SPG switches by default, an SZX of another model is refused unless `switch_model=true` / CLI `--switch` or `[SNAPSHOT] SwitchModel=1` (owner decision Q6: not by default, on request or by setting); Qt's two probe blocks are one call of `NeedOf` (the window keeps switching: choosing the file is the request). RZX keeps its own launcher |
| P8 | TTD: a load during a recording | S-M | **closed 2026-10-06 by the one TTD rule (D42, owner, final):** a snapshot load ENDS the recording session like a reset (history kept, no new session unless the `ttdrestart` feature is on); the earlier "continue the track with a checkpoint + marker" (Q5, TTD D10a) is withdrawn and its code removed. Nothing left to do here |
| P10 | SPG and ZXP through the image (see P10 notes) | S | **done 2026-10-06** (branch `snapshot-p10`) |
| P9 | Clean-up: legacy commits read the image instead of private staging; drop the duplicated staging and the SNA dead code; SP-1 unchanged | M | **done 2026-10-05** for SNA, Z80 and SZX (branch `snapshot-p9`): the three commits read `snapshot::Image`; SPG and ZXP stay as they are (see "P9 notes"); the SNA dead code is gone (`SNAHeader`, `SNA128Header`, `_borderColor`); the golden table is unchanged (with AY now visible in it) |

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

## P2 / P3 notes (2026-10-05)

- **No "transform" verdict yet.** A rewritten image only helps once the legacy commits read the image (P9), so the
  interface has Decline / Take / Refuse only; the first policy that needs a transform (P5) brings it. The proposal's
  `48k-on-128k` example assumed commits that read the image: it becomes a policy that Takes, or waits for P9.
- **A policy that Takes writes the whole machine**: registers, memory, ports, border, screen redraw, the HALT detection the
  legacy commits do. A shared "finish" helper for that comes with the first real policy (P4).
- **ZX-Poly** still has no plan hook (group-level, Q7); `.zxp` loads as before.
- **Inspect does not run the format's own model check** (an SZX saved on another model is only refused at commit), so
  `would_load` can be true for it; `image.machine_hint` tells a script what the file was made on.

## P4 notes (2026-10-05)

- **The cell table, not "pages 0-7".** In BIOS 3.06's own ZX mode the cells `#F0-#F7` are `00 ED 02 EF F0 05 EE F1`; the
  proposal's "SNA / Z80 write physical pages 0-7" is right at the DSS prompt and half right in a mode (banks 0, 2, 5 land
  where the cells put them by accident). The test compares against the file's own bytes bank by bank.
- **Windows 1 and 2** (cells `#E9` / `#EA`) show banks 5 and 2; if a mode ever made them differ from `#F5` / `#F2` the
  commit writes both pages. They agree in every mode seen so far.
- **The Spectrum screen shadow** is brought in step by replaying the write intercept over the page (bank 5 in window 1,
  bank 7 in window 3 after `#7FFD` selects it); a mutation test (no replay) fails on the first screen byte.
- **48K snapshot in a 128K mode** gets `#7FFD` = `#30` here (the Z80 loader's rule). Q2 (one shared rule for every machine)
  is still open for the others.
- **Not done:** the TR-DOS paging flag of an SNA 128 (the Sprinter follows its M1 trap rule), the interrupt shadow after
  EI, and a snapshot taken on the Sprinter (saving needs TTD, out of scope in the ZX-mode design).

## P5 progress (2026-10-05)

- **Q1 answered by the owner: refuse with the reason, for now.** "If the model is 48K and the snapshot is 128K: refusal. Later
  we will analyze the file at start and offer options; for now a refusal with the reason." Built as the plan's shared fit check
  (proposal 4.5): a 128K file on a 48K machine is refused (`needs: model:128K`, the reason says the snapshot holds bank N, was
  made on X, and names a 128K machine or a Pentagon); a bank beyond the machine's RAM (a Scorpion 256K snapshot on a 128 KB
  machine) is refused (`needs: ram:256K`). **The "locked 128K file = a 48K state in disguise" exception is dropped** (the owner
  asked for a plain refusal). A machine that owns a policy decides for itself (the Sprinter). Golden: 38 rows changed to
  `refused` (33 on the 48K, 5 Scorpion rows on 128K / +2 / +2A / +3 / Pentagon 128), nothing else.
- **Q2 (48K SNA vs Z80 lock) left as it is**: both run the program; a shared rule is not needed until something breaks.
- **Idea for later (owner):** analyze the file when it is opened and offer options (switch to the machine it was made on, enter
  a Spectrum mode on the Sprinter); `image.machine_hint` and `inspect` are the inputs for it.
- **Pentagon 1024 done (2026-10-05).** `PortDecoder::EnterSpectrum128Paging(pc)` (default: nothing) is called by the SNA and
  Z80 commits right after the reset, before their `#7FFD`; the Pentagon 1024 sets `#EFF7` bit 2 (memory above 128K absent), so
  `#7FFD` bit 5 is the lock again and pages are 0-7. Before: a locked 128K file mapped page 32 + n, a 48K Z80 (`#7FFD` = `#30`)
  mapped page 32 instead of locking. SZX and the state transfer already did it. Golden: the `ports` hash of the 38 SNA / Z80
  rows of PENTAGON1024 (`#EFF7` = `#04`); RAM and CPU hashes unchanged. The hook is the generic place for the next ones: the
  ATM family's pager and TS-Conf's MemConfig also need "the plain 128K form" after a reset.
- **ATM family done (2026-10-05).** `EnterSpectrum128Paging` on the ATM710, ATM3 and ATM450. The reset of an ATM710 / ATM3 leaves
  the memory manager off (PEN = 0: every window reads the last ROM page, writes go to the trash page) and forces the TR-DOS
  signal (~CPM = 0): a snapshot's RAM was not in the address space (`#C000` read as ROM, the HALT detection read the ROM). Now the
  manager is on as the RM_DOS boot sets it (`aFF77` = `#4300`, `pFF77` = `#E3`: ZX video mode, INT gate), laid out as a
  Spectrum 128K: #7FFD.4 = 0 -> ROM pair 2 (128K BASIC / system), #7FFD.4 = 1 -> pair 0 (48K BASIC / TR-DOS), RAM 5 / 2 fixed,
  window 3 from #7FFD. The ATM3 adds the BaseConf's own: `#EFF7` bit 2 (no 1 MB paging), bit 3 clear (no RAM at #0000), no write
  protection, no NMI / virtual TR-DOS page. The ATM450: `aFE` = ROM | ZX video mode, `aFB` = 0 (not the system ROM), `pFDFD` = 0.
  Checked live on the WebAPI: `dizzyx.z80` and `z80full.sna` give a **byte-identical PNG** on the Pentagon, ATM710, ATM3 and
  ATM450; `action.sna` (an animated demo) runs on all three. Golden: 38 rows of each of the three machines (`ports` hash; `misc`
  where TR-DOS was forced on before; `cpu` in the 10 SNA files whose PC is on a HALT, which the ROM-mapped window could not see).
  Mutation (hook off) fails `SnapshotAtm_Test` x2 and `SnapshotDefects_Test.AtmFamilyMapsTheTopWindow`.
- **TS-Conf done (2026-10-05).** The reset leaves MEM_CONFIG in the normal mode (`W0NoMap` = 1): window 0 is ROM page 0, an
  "unknown ROM" (the TS-BIOS image), whatever `#7FFD` says, so a 128K snapshot called the TS-BIOS where it expected BASIC (found
  live: `Dizzy Y 2.sna`, `#7FFD` = `#00`, showed the unknown ROM instead of BASIC-128). `EnterSpectrum128Paging` writes MEM_CONFIG
  = mapped mode + LCK128 = 128K (`#7FFD` bits 7:6 are no page bits), ROM128 as `#7FFD` bit 4 has it: window 0 is then the
  {service, TR-DOS, 128, 48} group member the snapshot's own `#7FFD` picks. Golden: the `ports` hash of 34 TSL rows.
- **Found while checking it, on every machine: a 48K snapshot's ROM latch disagrees with the ROM shown.** The 48K SNA commit sets
  the 48K ROM as a bank pointer (`SetROM48k`) and never the latch; the shipped configs say `RESET=128`, so the latch says
  BASIC-128, and the first recompute of the banks (any `#7FFD` write, a TR-DOS page-in) swapped the ROM under the program - on all
  12 machines probed. The other tests reset to `RM_SOS`, where the latch already agrees, which hid it. The 48K SNA and 48K Z80
  commits now call `SetROMMode(RM_SOS)` (the latches the reset itself sets, including `#1FFD` ROM bit 2 on the +2A / +3) and the SNA
  writes the latch through the decoder (models that keep the ROM bit elsewhere, TS-Conf's MEM_CONFIG, follow). Golden: the four
  48K SNA rows of PENTAGON512 / PENTAGON1024 (their golden machines start from a 128-ROM reset, as the app does).
  `SnapshotRomLatch_Test` resets to `RM_128` first, like the shipped config, on 12 machines.
- **Corrected my ATM change of the same day:** the ATM710 hook took the ROM pairs as pages 2 / 0, true for its 64 KB ROM but not
  for the ATM3, whose ROM roles sit at pages 30 / 28 (the probe above showed the ROM jumping from page 28 to page 0 on a recompute).
  The pairs now come from the model's own 128K / 48K ROM pages. Golden: the `ports` hash of 38 ATM3 rows.
- P5 is complete for the machines the proposal listed (the fit check, Pentagon 1024, the ATM family, TS-Conf). Not done:
  ZX-Poly (Q7), Spec256 (nothing in the codebase).

## P9 notes (2026-10-05)

- **What the commits read now.** `LoaderSNA::applySnapshotFromStaging`, `LoaderZ80::commitFromStage` and `LoaderSZX::CommitImage` take
  the machine state - RAM banks, the paging latches, the CPU, AY 0, the border - from `snapshot::Image`, the one `Pipeline::Plan`
  decided on. Hence a transform of the image (or a policy that rewrites it) reaches the commit: `Commit_Test` changes the image
  between the plan and the commit for each of the three and checks the machine follows. SZX keeps the model check, the media,
  the Beta 128, the devices and the version rules on its `Stage` (the image only DESCRIBES their payloads): `Commit(stage)` is
  `CommitImage(BuildImage(stage), stage)`.
- **The image learned what the commits needed:** `Cpu::pcOnMachineStack` (a 48K SNA whose stack is in the ROM or at the top of
  memory: the image cannot read the PC, the commit pops it from the machine as it always did), `Cpu::holdIntCycles` and `q` (SZX),
  `Image::portFE`, `Image::ayAddressLatch` (a Z80 48K stores the selected AY register without AY registers), `Image::unsupported`.
- **A behavior change on the way, deliberate:** a SamRam / SAM Coupe Z80 and a Z80 with a ROM block used to throw an uncaught
  `std::logic_error` from the commit, after the machine had been reset (the side finding of the proposal, section 12). The plan now
  refuses them first (`needs: format:unsupported`, the reason names which), nothing written; `Commit_Test` pins it with synthetic
  files and a mutation (the refusal off) fails it.
- **A gap in P0 found and closed.** The golden machines were built without a TurboSound slot (the test runner leaves it empty), so
  `SnapshotDigest` saw no AY chip and every `ay` hash of the first table was 0: the AY path of the commits was not covered by it.
  The golden test now holds `SoundCardScope(TurboSound)` while it creates machines. The table was regenerated on the code BEFORE
  this step and P9's code reproduces it row for row, AY included; no other hash moved.
- **Not done, and why.** (1) SPG: its commit reads `LoaderSPG::Image`, the format's own parse record (blocks at physical
  addresses, PC, SP, page 3, clock), which the neutral image carries only partly (page 3 and the clock are a text note); typed
  fields would be needed, for a loader that is 180 lines. (2) ZXP: the plan has no hook for four-module snapshots (owner question
  Q7). (3) The loaders still parse into their staging (`_memoryPages`, `_stagingRAMPages`, the headers) and build the image from
  it: the unit tests exercise those internals, and the save path (P6) is built on the same buffers. A parser that fills the image
  directly belongs with P6.
- **Next for the plan (what P9 enables):** the `Transform` verdict of the proposal (image to image, then the legacy commit) -
  needed by the fit options the owner wants later (analyze the file, offer a machine).

## P6 notes (2026-10-06)

Decisions (owner): a save waits for a confirmed pause; a snapshot is the machine's **128K view** (Sprinter in a ZX mode: banks in
the Spectrum's order through the cells #F0-#F7, the machine named by the launcher mode: `SP.ZX` / `ORIGIN.ZX` a 128K, `P128.ZX` a
Pentagon 128, `SC256.ZX` a Scorpion; TS-Conf and the ATMs: while the live window map is a Spectrum 128K); where no view exists the
save is refused with the reason (no file conversion in the emulator: that is a separate tool); a Pentagon 512 / 1024 saves as .szx only;
a 48K SNA with the stack in the ROM is refused (use .z80 / .szx); the Qt menu items are disabled and the dialog filters follow the machine.

What changed in what is written (`testdata/loaders/golden/save-digests.txt`, first generated on the code before P6, 102 rows: 17
machines x 2 scenarios x 3 formats; the plain 128K family rows with a free #7FFD are unchanged):

- **A 48K machine's .sna / .z80** were a 128K file (131103 bytes / a 128K model) saved from an unlocked "#7FFD = 0"; now the 48K
  layout (49179 bytes, model 0, three pages).
- **A locked 128K** (#7FFD bit 5) was a 48K file whatever the bank on top: a program with bank 4 at #C000 lost it. Now the 48K
  layout needs the lock, bank 0 on top, the normal screen and no #1FFD special paging; anything else keeps all 8 banks.
- **Scorpion, ProfScorp .sna**: refused (a .sna holds banks 0-7; the old file was a 128K SNA that dropped banks 8-15). .z80 / .szx unchanged.
- **Pentagon 512 / 1024 .sna / .z80**: refused with `format:szx` (the old files kept 8 of 32 / 64 banks).
- **ATM710 / ATM3 / ATM450, TS-Conf, Profi, Profi3, Sprinter**: the old writers saved the physical pages 0-7 whatever the machine was
  doing; now the view exists only in a 128K layout (TS-Conf and the ATMs after a snapshot load, the Sprinter in a ZX mode) and
  the file is the same as a plain 128K's; Profi has no view yet (`capture_unsupported`).
- **The 48K .sna writer no longer writes the PC into the running machine's RAM** (two bytes under SP).
- **AY**: a 48K machine's file carries none (it has no AY); every 128K-family file as before.
- Machines that can be saved as SZX through a 128K view now include TS-Conf, the ATMs and the Sprinter (the file says 128K / Pentagon / Scorpion).

Removed with the old writers: `LoaderSNA::determineOutputFormat / captureStateToStaging / save48kFromStaging / save128kFromStaging /
isPageEmpty` and the Z80 equivalents (their tests with them). `DezogDebugAdapter` saves its state as a .sna: on a Pentagon 512 it now
gets the refusal's reason instead of a junk file.

Open (not in P6): Profi / Kay / Quorum views; the Sprinter 512 KB modes; ZX-Poly saves; restoring a file onto another model through the
Qt window (P7); the debugger UI of the capture view.

## P10 notes (2026-10-06): SPG and ZXP through the image

- **SPG:** the commit moved from the loader (which read `LoaderSPG::Image`, the format's own record) into the TS-Conf machine's snapshot
  policy `TsConfProgramSnapshot` (`emulator/platforms/tsconf/`, handed out by `PortDecoder_TSConf::GetSnapshotPolicy`). It reads the
  neutral image: the blocks as physical runs, the CPU, and the two TS-Conf registers the file names (the page at #C000, SYS_CONFIG[1:0])
  as the payload of the `spg:header` extension - no TS-Conf field in the shared image. The load report now says `commit: tsconf-program`
  (it said `legacy`); `commit=legacy` and `LoaderSPG::Commit` run the same code from the same image, on a machine without the policy they
  refuse as before. The policy is not in the by-name registry (shared code may not name TS-Conf paths, `TsConfIsolation_Test`).
- **ZXP:** `LoaderZXP::Apply` plans the group first (each module's image through `Pipeline::Plan` on its own machine) and commits only if
  all four proceed: one refusal stops the load before any machine is touched, the reason names the module. The module commit is
  `LoaderZXP::CommitImage` from the image (the old `ApplyModule` read the module record). This is the group-level plan the P1 note waited
  for; owner question Q7 (a plain SNA / Z80 on a ZX-Poly group) is separate and stays open.
- Tests: `tsconfprogramsnapshot_test.cpp` (the machine's policy decides, the machine follows a changed image, another machine refuses, a
  Spectrum snapshot declines, legacy), `LoaderZXPGroup_Test` (planned then committed; one refusing module touches nothing).

## P11 notes (2026-10-07): a refused load does not end the TTD session

The TTD rule (D42) ends the recording session when a snapshot is loaded. It used to end it when the load was CALLED, so a load that was
then refused (another model's SZX, an SPG on a Pentagon, a corrupt file) had already stopped the recording of a machine that never
changed. Now `snapshot::Options::beforeCommit` is called by `Pipeline::Plan` once, at the moment the plan decides to commit (a Take or a
Legacy decision), before the loader writes anything; `Emulator::LoadSnapshotStaged` ends the session there (`OnLoad(Snapshot)`).
A file that fails before the plan (unreadable, corrupt) never reaches it.

Two refusals used to come AFTER the plan, inside the legacy commit, and would have ended the session first: they moved into the plan
(`LegacyWillRefuse`): an SZX saved on another model than the running one (the loader's own `LoaderSZX::Suits` rule, `needs: model:<short>`)
and an SPG on a machine whose policy does not take it (`needs: model:TSL`). The commits keep their own checks as a second line.
Tests: `SnapshotBeforeCommit_Test` (announced once on a good load, never on a refusal, legacy judged too, inspect never),
`ARefusedSnapshotLoadLeavesTheRecordingRunning`.
Open: a media load (tape, disk) is still refused while recording, unchanged.

## P12 notes (2026-10-07): owner decisions Q7 and Q8

- **Q7: a ZX-Poly takes a .zxp and nothing else.** `snapshot::IsZXPolyModule` (the machine is a member of a ZX-Poly group): the plan
  refuses every other format with `needs: format:zxp` (the reason: `snapshot::ZXPolyRefusal`), which also covers `inspect` and the
  automation surfaces; `SnapshotLauncher::Load` never replaces a module by another model; the Qt window refuses an SZX / SPG
  before it would switch the model, and a SNA / Z80 through the plan (message box with the reason); a save from a module is refused
  for every format (`needs: zxpoly`). A .zxp load into the group itself (the module images carry `format: zxp`) is unchanged.
- **Q8: the state transfer is not merged with anything.** It stays `MachineStateTransfer`. Rule: it works wherever that is physically
  possible. See the matrix in `docs/features/automation.md`: what works today, what is expected next (ATM family among themselves,
  a 128K into a Sprinter ZX mode), what never works (TS-Conf state, ZX-Poly modules).
