# Finalizing and merging `profi`, `generalsound`, `moonsound`

How to bring the three long-lived feature branches into master around the TTD
v2 work. Data gathered 2026-09-25 with read-only git (merge simulations in a
throw-away clone); nothing was built on the branches. The overall sequence with
TTD phases is in [migration-trajectory.md](migration-trajectory.md).

---

## 1. Where the branches stand

**Use the remote tips.** Each local branch is a strict ancestor of
`origin/<branch>` (= `github/<branch>`), so they fast-forward:

| Branch | local | remote tip | remote-only commits |
|---|---|---|---|
| `generalsound` | `c6890611` | `c9277ab3` (09-23) | 36 |
| `moonsound` | `8f9cead9` | `009997c4` (09-23) | 18 |
| `profi` | `0a98d677` | `18d632ef` (09-24) | 23 |

| | profi | generalsound (GS) | moonsound (MS) |
|---|---|---|---|
| Ahead / behind master | 9 / 6 | 34 / 10 | 39 / 10 |
| Size vs master | 59 files, +3.7k | 94 files, +27k (15k vendored z80ex) | 189 files, +39k |
| Conflicts merging into master | **none** | 3 files, mechanical | 3 files, **one architectural** |
| Feature complete? | usable machine; parity gaps (IDE, joystick, FE bit 7, Covox aliases) | main card + lightweight card + automation done; NeoGS not started | sound core done; **no automation at all**; TTD incomplete |
| TTD | 17 B paging serializer, under the contract | GS RAM (up to 512 KB) copied into a blob **every checkpoint**; lightweight blob changes size | chip state only; **1 MiB wave SRAM not captured** |

The master commits that GS and MS still lack include the Covox self-decoding device API
(`d90421bb`), mixer T-state sample counting (`ec66d3bc`) and timed AY writes +
exact TTD restore (`6ed6d4c0`). Profi lacks only the last five sound/TTD commits.

## 2. Things to fix on master first (step 0)

These are cheap now and expensive after any merge.

1. **One `PeripheralId` table.** All three branches claim id **9** (Profi:
   `ProfiPaging`, MS: `MoonSound`, GS: `NeoGS`). The id is written into every
   device blob in `.ttd` files, so two names with the same value compile fine
   and silently misattribute state. Allocate on master before any merge:

   | Id | Device |
   |---|---|
   | 5 | GeneralSound (already reserved) |
   | 9 | ProfiPaging |
   | 10 | MoonSound |
   | 11 | GeneralSoundLightweight |
   | 12 | NeoGS (reserved; may be released before V5 if NeoGS is dropped — ids become permanent only with the versioned format) |

   Each branch adopts the table when it merges master in. Update `ttd.ksy`,
   `PERIPHERAL_ID_NAMES` in the analyzer, and the contract test's fake
   decoder (GS switched it to `NeoGS`).
2. **Audio-activity enum** in `notifications.h`: GS and MS both claim value 6.
   Reserve GS = 6, MoonFM = 7, MoonPCM = 8 on master.
3. **Fast-forward the local branches** to their remote tips.

## 3. Per-branch finalization checklists

### 3.1 `profi` — merge first

Before the merge (on the branch):

- [ ] Merge master in (picks up `45812176`..current).
- [ ] Adopt the id table (ProfiPaging = 9 is already its value).
- [ ] `data/configs/profi/unreal.ini`: set **`MoonSound=0`** (the file ships
  `MoonSound=1`; after MS lands, MS's claim on low byte `#7E` would swallow
  Profi palette writes on `#xx7E`) and an explicit **`GSType`** (`BASS` becomes
  the GS lightweight card with a deprecation warning once GS lands).
- [ ] Decide RTC determinism: `ProfiCMOS` serves host time and has no TTD
  state. Minimum for the merge: declare it as known-nondeterministic in the
  TODO; the fix is part of TTD v2 step V3 ([migration-trajectory.md](migration-trajectory.md)).
- [ ] Refresh the stale `docs/inprogress/PLAN.md` row 13a (still lists Covox and
  RTC as open).
- [ ] Build, `core-tests`, zero warnings; TTD contract, capture-cost and Profi
  serializer tests.

Remaining feature gaps (IDE, Kempston joystick, FE bit 7, Covox aliases,
BIOS menu boot check, hi-res timing evidence) do **not** block the merge; they
stay in the Profi TODO.

### 3.2 `generalsound` — merge after TTD step V1 (memory regions)

Why not before: every Pentagon config ships `GSType=Z80` with `GSRamSize=512`,
and the GS serializer copies all of that RAM into its blob at every checkpoint.
On master that would make every default Pentagon recording carry 512 KB of
device state per frame (≈ 25 MB/s before compression) and add a 512 KB copy +
compress to every frame. `TTD_Capture_Cost_Gate_Test` does not see it (it
measures only the page store).

Before the merge (on the branch):

- [ ] Merge master in. Resolve:
  - `config.cpp`: keep both new blocks (decimator quality + GS);
  - `soundmanager.cpp` frame end: keep master's removal of the duplicate
    TurboSound call, add the GS call;
  - `porttrace_test.cpp`: renumber (GS rows 4–6, Beta128 7, total 8).
- [ ] Adopt the id table (GS lightweight 10 → **11**, NeoGS 9 → **12**) and the
  notification enum.
- [ ] **Move GS RAM to a TTD memory region** (V1 API); the blob keeps only the
  95-byte fixed part. Move the lightweight card's upload store to a region as
  well, so its blob size stops changing.
- [ ] Add the z80ex fields missing from the GS blob (`noint_once`,
  `reset_PV_on_int`, `int_vector_req`; perf review G6) — the blob's reserved
  bytes hold them without a layout change.
- [ ] Review the GS clock derivation (`soundchip_gs.cpp:253`,
  `soundchip_gslw.cpp:1026`): it still uses the rounded `frame_duration_us`
  that `ec66d3bc` replaced with exact T-state counting in the mixer.
- [ ] Expose the TTD restore report (proposal §7.2 in the GS docs) — or rely on
  V2, which does it for every device.
- [ ] Existing `testdata/ttd` fixtures have no GS blob: re-record them after
  the merge ([testdata/ttd/README.md](../../../testdata/ttd/README.md)).
- [ ] Build, tests, zero warnings; GS TTD switch test; capture-cost gate
  extended to count device state and regions.

Known and accepted after the merge: the lightweight → full card switch freezes
emulation ~17 s on a 381 KB module (needs chunking); NeoGS not started.

### 3.3 `moonsound` — finish on the branch, merge last

MoonSound is the least finished of the three and carries the only
architectural conflict. Finishing it *on the branch* keeps master shippable and
lets its TTD be built directly on memory regions.

Before the merge (on the branch):

- [ ] **Strip dead weight** into separate commits or drop it: uncompiled ymfm
  OPL/PCM sources, the duplicate libopl4 copy in `tools/poc/015-opl4-synthesis`,
  unrelated z80 benchmarks, the edit to the TTD v2 PoC benchmark (it removes the
  PoC's delta API).
- [ ] **Unify port claiming.** Master now has *self-decoding devices*
  (`RegisterSelfDecodingDevice`, used by Covox); MS adds a *full-decode
  observer* (`RegisterFullDecodePort`, a tap in `Z80::in/out`, claim-override
  hooks in 8 decoders). Two parallel "a device claims a port outside the table"
  mechanisms need one precedence rule — preferably one mechanism. This is a
  design decision, not conflict editing. It also decides the cost of the extra
  check on every IN/OUT on every model.
- [ ] Re-apply the MS claim hooks in the **rewritten** Profi decoder (MS
  patched the old one).
- [ ] Resolve the ~10-file overlap with GS (TTD registration, id enum,
  notifications, soundmanager, `atm3`/`atm710` ini, 4 HUD files) — paid once,
  by whichever of GS/MS lands second.
- [ ] **Automation (P2-2)**: state report and control on WebAPI / MCP / CLI /
  Lua / Python. PLAN.md #11 requires the design first.
- [ ] **TTD Tier B**: wave SRAM as a memory region (V1 API), per its own TDD
  (F2). Tier A blob gets id 10.
- [ ] Re-verify the float mix bus + limiter against master's decimator,
  output-stage flush and TTD seek sample phase (`ec66d3bc`, `6ed6d4c0`).
- [ ] Build, tests, zero warnings; `fulldecodeclaim_test` expectations around
  Beta128 re-run.

## 4. Merge order and why

```
step 0 (master) -> profi -> [TTD V0, V1 on master] -> generalsound -> moonsound -> [TTD V2..V6]
```

| Order considered | Conflicted files per step | Verdict |
|---|---|---|
| profi → GS → MS | 0 → 4 → ~14 | **chosen**: smallest first, the GS/MS overlap is paid once at the end |
| profi → MS → GS | 0 → 5 → ~13 | MS is not ready; would block GS |
| GS → MS → profi | 3 → 13 → 2 | GS before memory regions = 512 KB/frame blobs on master |

The GS↔MS overlap (~10 files) is intrinsic and costs the same in every order.

## 5. How each merge is done

1. On the branch: `git merge master`, resolve, commit (explicit approval).
2. Build `ninja -C cmake-build-agent-release`, full `core-tests`, zero warnings,
   plus the branch's own tests and the TTD contract / capture-cost / serializer
   tests.
3. `pentagon128k/unreal.ini` is CRLF: stage merges touching it with
   `git -c core.autocrlf=false add`, and check the diff is not the whole file.
4. Merge into master with `--no-ff`, after explicit approval, then push to both
   remotes.
5. Re-record `testdata/ttd` when the merge changes what a Pentagon session
   contains (GS does), and update PLAN.md / the branch TODO markers.
