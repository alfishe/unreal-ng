# 034 — media-signatures

The gate P-12 of the Steam Deck client ([poc-plan.md](../../../docs/inprogress/2026-10-08-steamdeck-client/poc-plan.md),
signatures in [input-and-profiles.md §4](../../../docs/inprogress/2026-10-08-steamdeck-client/input-and-profiles.md#4-signatures))
and the controller manager's signature hook (C6, D7, D16, haptics H-3,
[mapping-and-profiles.md §6.1](../../../docs/inprogress/2026-10-17-controller-manager/mapping-and-profiles.md)).
The library under test is `core/swsignature` (namespace `unrealng::swsignature`); this POC runs it over a real
collection and measures it. Status: see the index in [tools/poc/README.md](../README.md).

## Goals

1. Do the signatures recognise one title after a re-pack (TAP ↔ TZX; TRD ↔ SCL ↔ FDI ↔ UDI) and a rename (the file,
   the TR-DOS disk label)?
2. Do they recognise a disk the game wrote to (a high-score file added, a data file rewritten) by the fuzzy rule
   (Jaccard ≥ 0.8 over the TR-DOS file set and no code file of the reference missing)?
3. How often do two different titles match (exact content or fuzzy)? Is the "no code file missing" condition needed,
   or is a stricter variant better?
4. How fast is it per file?
5. Best effort: does the memory signature (≥ 90 % equal 256-byte RAM pages) tell two snapshots of one game from
   snapshots of other games?

## Requirements

- The corpus is real: the vtrd.in mirror (`/Volumes/TB4-4Tb/Data/zx-spectrum/vtrd/files`: `gamez`, `full_ver`,
  `translat`, `demo_ver`, `demoz`, `sbor`, `press`, `system`; ZIPs of mostly SCL / TRD / TAP) read **in place**: the
  ZIP members are inflated in memory, nothing is extracted or copied. Snapshots: the `*.z80 / *.sna / *.szx` under
  `/Volumes/TB4-4Tb/Data/zx-spectrum` outside vtrd, and the five game pairs in `testdata/loaders/rzx/oracle`.
- The signatures are computed by the library itself (`Compute`, `Compare`, `CompareFileSets`), the variants of the
  disk with the core's own writers (`FloppyFormats::Save`, `TrdosFileSystem::Write / Delete`, `TzxArchiveWriter`,
  `TapArchiveWriter`), so the POC measures what the emulator will run.
- Headless, macOS arm64 (the emulator's own code; the hashing is portable C++).

## Constraints

- Read-only corpus: never write into `/Volumes/TB4-4Tb/Data` or `../zx-software-catalog`, never copy the corpus
  into the repository. Nothing from the corpus is executed (`pcutilz`, `emulz` are skipped).
- Raw output (per-file TSV, logs, the temporary images of the re-pack) in `scratch/2026-10-23-poc-034/`.
- No online lookups (P-13).
- The ground truth of "the same title" is the vtrd release (one ZIP); two ZIPs are different titles unless their
  names say otherwise. A match between two ZIPs is a *candidate* false match, classified by hand on a sample (a
  re-release of the same game by another group is the same title, not a false match).

## Acceptance criteria (from P-12)

| Measure | Pass |
|---|---|
| Recall, re-pack and rename (content signature equal) | ≥ 95 % |
| Recall, saved-to disks (fuzzy rule) | ≥ 80 % |
| False matches across the corpus | < 0.1 % |
| Speed | ≥ 2000 files/s |

Expected: re-pack recall near 100 % (the TR-DOS file set ignores the layout and label; the tape content ignores the
container); saved-to recall high for disks of ≥ 5 files and low for 1-3 files (one file added to a 2-file disk gives
Jaccard 0.67); exact-content collisions only between releases that hold the same files; speed bound by SHA-256
(hundreds of MB/s), so well above 2000 files/s for 80-640 KB images.

## Files

- [`cpp/sigpoc.cpp`](cpp/sigpoc.cpp) — the tool: walks the corpus, reads ZIPs in memory, computes, re-packs, saves to,
  compares; prints `summary.md` and writes per-file TSV into `--out`
- [measurements-2026-10-23-corpus.md](measurements-2026-10-23-corpus.md) — the corpus run: formats, re-pack, saved to,
  matches between releases, the sector path, snapshots, speed
- [analysis.md](analysis.md) — what the numbers mean, the rules compared
- [conclusions.md](conclusions.md) — the criteria met / not met
- [proposal.md](proposal.md) — what the library adopted and what is left

## Build and run

```bash
cmake -DBUILD_SIGNATURE_POC=ON cmake-build-agent-release     # once
tools/build/build.sh signature-poc
V=/Volumes/TB4-4Tb/Data/zx-spectrum/vtrd/files
cmake-build-agent-release/bin/signature-poc --out scratch/2026-10-23-poc-034/final --repack-every 10 \
    --snapshots testdata/loaders/rzx/oracle --snapshots /Volumes/TB4-4Tb/Data/zx-spectrum \
    $V/gamez $V/full_ver $V/translat $V/demo_ver $V/demoz $V/sbor $V/press $V/system
```

Options: `--repack-every N` (variants for every N-th title), `--limit N` (stop after N ZIPs), `--snapshots DIR`
(repeatable), `--cross-check` (every disk also through the DiskImage loaders, the differences listed), `--speed N`
(N media in memory: SHA-256 alone, `Compute` on one thread and on every core; writes `speed.md`). One file instead of
directories prints both paths' signatures for it.

## Outcome

Gate P-12 passed with changes (2026-10-23): re-pack / rename 99.97 % (100 % after two core loader fixes); saved to 83.9 % with the catalog rule (the
first-draft Jaccard rule 14.8 %); 0.391 % of disks match another release, nearly all the same title (different
programs ~0.01 %); 10 000+ files/s on all cores, 800-880 on one under load. The library adopted the catalog rule, the
sector path for TRD / SCL and uniform-page skipping for memory. Details in [conclusions.md](conclusions.md).
