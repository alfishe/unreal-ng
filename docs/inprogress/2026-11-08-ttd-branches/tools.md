# TTD branches: maintenance tools

Part of [TTD branches](README.md). Status: **design** (2026-11-08), not built (P7). Format: [storage.md](storage.md).

Python tools to inspect, extract and clean branched sessions and recording folders without an emulator. They build
on the existing reader `tools/verification/ttd-analyzer/src/ttdcontainer.py` (headers, parts, CRCs, every piece
version decoded, part dependencies), which moves to a shared package so the analyzer and the new tools use one
reader.

## 1. Layout

```text
tools/ttd/
  README.md
  requirements.txt            # zstandard (as the analyzer)
  ttdlib/                     # the shared reader (moved from ttd-analyzer/src/ttdcontainer.py) + branch table,
                              #   folder index, writer of containers (part copy, index, trailer)
  ttdtool.py                  # the command line
  tests/                      # pytest over C++-written fixtures (testdata/ttd/v2/, branched ones added)
```

`tools/verification/ttd-analyzer` keeps its commands and imports `ttdlib`.

## 2. Commands

| Command | Does | Writes |
|---|---|---|
| `info <file|folder>` | sizes per stream and per branch, segment list, memory / disk tiers as recorded | - |
| `tree <file|folder>` | the branch tree: GUID, name, parent, fork, length, created, by, reason, tags, pinned | - |
| `validate <file|folder>` | everything the analyzer validates, plus the branch table (no cycle, parents exist, forks inside the parent, unique GUIDs), every branch part's dependencies, the folder index against the files | - |
| `extract <in> --branch <guid|name> [--lineage] -o <out>` | a file holding the lineage of the branch (or the branch and its descendants with `--subtree`): parts copied verbatim (storage.md S-6), branch table and index rewritten | new file |
| `prune <file|folder> <criteria> [--apply]` | criteria: `--older-than`, `--created-by`, `--run`, `--tag`, `--untagged`, `--unpinned`, `--shorter-than`, `--longer-than`, `--not-lineage-of <branch>`; dry run unless `--apply`; never the trunk, never pinned without `--force` | folder: tombstones + files deleted; file: a new file |
| `join <folder> -o <file>` | what Save does (all branches), for a crashed or unsaved folder | new file |
| `reindex <folder>` | rebuilds `branches.tdx` from the segment trailers | the index |
| `recover <folder>` | what a crashed recording keeps, per branch | - (or `-o` a joined file) |
| `gc <ttd root> --older-than <days> [--apply]` | crashed folders older than N days: listed by explicit names with their ages from `stat`, deleted only with `--apply` | deletes folders |
| `stats <file|folder> [--json]` | per branch: frames, bytes on disk, unique bytes (what deleting it frees), shared bytes | - |

Rules for every writing command:

- dry run by default; the plan printed (files, bytes) before anything changes;
- never in place: a file command writes a new file; a folder command appends tombstones before deleting files,
  so a crash midway leaves a consistent folder (storage.md §4.2);
- refuses a folder whose `owner.pid` names a live emulator process;
- every output is checked with `validate` before the command reports success.

Flattening a lineage into a linear file (moving branch records to trunk stream ids, which needs re-encoding of
references) stays in the C++ engine (`ttd save lineage=`); `ttdtool.py flatten` calls it over the WebAPI when an
emulator runs, and says so otherwise.

## 3. Tests

- pytest over fixtures written by the C++ writer (a `DISABLED_WriteBranchFixtures` test, as the analyzer's
  fixtures are made): a two-branch session, a deep lineage, a mid-frame fork, many short branches, a folder with
  `branches.tdx`, a crashed folder, damaged copies.
- Cross-check without C++ at test time: `expected.json` holds, per branch and checkpoint, the content CRCs the C++
  reader computed; the tools' outputs (extract, prune, join) are decoded by `ttdlib` and must give the same CRCs
  for every position they keep.
- C++ side: core-tests load every tool output kept as a fixture (extract, prune, join of the fixtures) and restore
  every position to the original's hash (test-strategy.md T-F1 oracle).
- Fuzz F-4 (test-strategy.md): damaged files never raise, same verdict as the C++ reader.
