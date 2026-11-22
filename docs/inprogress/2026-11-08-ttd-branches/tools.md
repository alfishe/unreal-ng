# TTD branches: maintenance tools

Part of [TTD branches](README.md). Status: **built** (P7, 2026-11-15) in
[tools/python/ttd/](../../../tools/python/ttd/README.md). Format: [storage.md](storage.md).

Python tools to inspect, extract and clean branched sessions and recording folders without an emulator. They build
on the reader that was `tools/verification/ttd-analyzer/src/ttdcontainer.py` (headers, parts, CRCs, every piece
version decoded, part dependencies), moved to the shared package `tools/python/ttd/ttdlib/` so the analyzer and the
new tools use one reader.

As built, against this design:

- the path is `tools/python/ttd/` (owner's choice), not `tools/ttd/`;
- `ttdlib` holds `ttdcontainer.py` (the moved reader, trunk only; it now skips branch parts), `session.py` (the
  branch-aware read, rule for rule the engine's loader, so its verdict on a damaged file is the C++ one; the
  controller's branch record; per-checkpoint content CRCs), `writer.py` (the container writer, byte for byte the
  C++ one) and `folder.py` (segment names, `branches.tdx`, owner pid, the join, crashed folders);
- `extract` takes `--branch <id|guid|name>` with `--lineage` (default) or `--subtree`; branch ids are kept (not
  renumbered): the loader accepts gaps, so only part indices and dependencies change;
- a folder `prune` replaces `branches.tdx` by a rename with one more part of delete records (rebuilt first when it is
  missing or damaged), then deletes the files: the same state as `DeleteLines`;
- `reindex` keeps the old index as `branches.tdx.old`; `join` and `reindex` give the C++ bytes exactly (tested);
- `recover` keeps each file up to its first damaged part and drops a branch's later files after a lost one;
- `info` shows no memory / disk tiers, and none are missing: P3 (2026-11-16) made the tiers a runtime state of a
  recording, not of the format. Every segment keeps its file for the session's life, which segments sit in memory
  depends on the history limit and the seeks of the moment, and neither a session file nor a recording folder
  records it (checked 2026-11-20: no tier field in `ttd.ksy`, the writer or `branches.tdx`). The live split is in
  the memory report of `ttd status` (`disk_tier_active`, `evicted_to_disk_segments`, `read_back_segments`, ...,
  [architecture.md §5.4](architecture.md#54-read-back-from-disk)); `info` on a folder lists its segment files, all of
  them the disk tier; `stats` counts unique bytes as the branch's subtree (what deleting it frees) and shared bytes
  as its ancestors';
- every writing command checks its output by loading it whole and comparing every kept line's positions and content
  CRCs with the original's.

## 1. Layout

```text
tools/python/ttd/
  README.md
  requirements.txt            # zstandard (as the analyzer)
  ttdlib/                     # the shared reader (moved from ttd-analyzer/src/ttdcontainer.py) + branch table,
                              #   folder index, writer of containers (part copy, index, trailer)
  ttdtool.py                  # the command line
  tests/                      # pytest over C++-written fixtures (testdata/ttd/branches/)
```

`tools/verification/ttd-analyzer` keeps its commands and imports `ttdlib`.

## 2. Commands

| Command | Does | Writes |
|---|---|---|
| `info <file|folder>` | sizes per stream and per branch, segment list (tiers are not recorded: see above) | - |
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

Built as below: fixtures in `testdata/ttd/branches/` (written by `TTDBranchFixtures_Test.DISABLED_WriteBranchFixtures`,
`core/tests/debugger/ttd/ttdbranchfixtures_test.cpp`; tool outputs by `tools/python/ttd/tests/make_tool_outputs.py`),
52 pytest cases, and `TTDBranchToolOutputs_Test.ToolOutputsRestoreAsTheOriginals` in core-tests.

- pytest over fixtures written by the C++ writer (a `DISABLED_WriteBranchFixtures` test, as the analyzer's
  fixtures are made): a two-branch session, a deep lineage, a mid-frame fork, many short branches, a folder with
  `branches.tdx`, a crashed folder, damaged copies.
- Cross-check without C++ at test time: `expected.json` holds, per branch and checkpoint, the content CRCs the C++
  reader computed; the tools' outputs (extract, prune, join) are decoded by `ttdlib` and must give the same CRCs
  for every position they keep.
- C++ side: core-tests load every tool output kept as a fixture (extract, prune, join of the fixtures) and restore
  every position to the original's hash (test-strategy.md T-F1 oracle).
- Fuzz F-4 (test-strategy.md): damaged files never raise, same verdict as the C++ reader.
