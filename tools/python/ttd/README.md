# TTD session tools

Maintenance of time-travel sessions with branches, without an emulator: look inside a `.ttd` file or a recording
folder, check it, extract a branch's lineage, prune branches, join or recover a crashed recording, rebuild a folder's
`branches.tdx`, delete old crashed recordings. Design:
[tools.md](../../../docs/inprogress/2026-11-08-ttd-branches/tools.md); format:
[storage.md](../../../docs/inprogress/2026-11-08-ttd-branches/storage.md).

```bash
pip install -r requirements.txt          # zstandard (pytest for the tests)
python3 ttdtool.py tree session.ttd
```

## Layout

| Path | What |
|---|---|
| `ttdtool.py` | the command line |
| `ttdlib/ttdcontainer.py` | the container and the trunk-only reader (moved from the analyzer; it uses it too) |
| `ttdlib/session.py` | the branch-aware read, rule for rule as the engine's loader; branch records; content CRCs |
| `ttdlib/writer.py` | the container writer, byte for byte as the C++ one (verbatim part copy, index, trailer) |
| `ttdlib/folder.py` | recording folders: segment names, `branches.tdx`, owner pid, the join, crashed folders |
| `tests/` | pytest over the C++-written fixtures in `testdata/ttd/branches/` |

Every error the library or the tool raises on purpose is a `ttdlib.TtdError`; damaged input never raises anything
else.

## Commands

A `<path>` is a `.ttd` file or a recording folder (a folder is joined in memory, as Save would join it).

| Command | Does |
|---|---|
| `info <path>` | header, stream totals, parts, each line's frames, checkpoints, parts and bytes; a folder's segments |
| `tree <path>` | the branch tree: id, name, fork frame and T, length, created, by, run, reason, tags, pinned, GUID, the active branch |
| `validate <path> [--json]` | everything the loader checks (every part, every branch's create and fork), every version decoded to its CRC, the branch table (cycles, parents, forks inside the parent, unique GUIDs), each part's dependencies inside its lineage, a folder's `branches.tdx` against its files. Exit 1 on any problem |
| `stats <path> [--json]` | per line: frames, checkpoints, parts, bytes, unique bytes (what deleting it with its descendants frees), shared bytes (its ancestors') |
| `extract <path> --branch B [--lineage \| --subtree] -o OUT` | a new file with the trunk and B's ancestors whole and B (`--subtree`: and every descendant). Parts are copied verbatim, branch ids kept, the index rewritten |
| `prune <path> <criteria> [-o OUT] [--force]` | deletes every branch all the given criteria match, with its descendants. Criteria: `--older-than DAYS`, `--created-by WHO` (glob), `--run RUN` (glob), `--tag TAG`, `--untagged`, `--unpinned`, `--shorter-than FRAMES`, `--longer-than FRAMES`, `--not-lineage-of B`. Never the trunk; never a pinned branch (or one over a pinned descendant) without `--force`. A file gives a new file `-o OUT`; a folder gets delete records in `branches.tdx` (one part), then the branches' files are deleted |
| `join <folder> -o OUT` | the folder's files as one file, byte for byte what the emulator's Save writes (its source file first when the folder continues one) |
| `reindex <folder>` | `branches.tdx` written again from the segment files, keeping its delete and source records; byte for byte the C++ rebuild. The old one is kept as `branches.tdx.old` |
| `recover <folder> [-o OUT]` | what a crashed recording keeps, per line: each file up to its first damaged part, a cut file's incomplete last part dropped, a branch's later files dropped after a lost one |
| `gc [--root DIR] [--older-than DAYS]` | recording folders under `~/.unreal-ng/ttd` (or DIR) whose `owner.pid` names no running process and whose newest file is older than DAYS (default 7), each listed by name with its age from `stat`. Saved `.ttd` files in the root are never touched |
| `flatten <path> --branch B -o OUT [--url URL]` | a branch's lineage as one linear file: needs the C++ engine, so it asks a running emulator over the WebAPI (create an instance of the recorded model, take `/control/session`, `ttd/load`, switch to B, `ttd/dump` with `lineage=true`, delete the instance). Without an emulator it says so (`extract --lineage` gives a branched file instead) |

Rules for every command that writes (`extract`, `prune`, `join`, `reindex`, `recover -o`, `gc`, `flatten`):

- a dry run unless `--apply`: the plan (lines, files, bytes) is printed and nothing changes;
- never in place: a new file is written as `OUT.partial`, synced and renamed; an existing `OUT` is refused; a
  folder's `branches.tdx` is replaced by a rename, and its delete records land before any file is deleted;
- a folder (or a file in one) whose `owner.pid` names a running process is refused;
- the output is checked before success is reported: it loads whole, holds exactly the lines asked for, and every
  checkpoint of every line gives the original's positions and content CRCs.

Exit codes: 0 done (or sound), 1 problems found or refused, 2 bad arguments.

### Examples

```bash
python3 ttdtool.py tree testdata/ttd/branches/deep-lineage.ttd
python3 ttdtool.py extract testdata/ttd/branches/deep-lineage.ttd --branch side -o scratch/side.ttd --apply
python3 ttdtool.py prune testdata/ttd/branches/many-short.ttd --run run-1 -o scratch/pruned.ttd   # the plan
python3 ttdtool.py join testdata/ttd/branches/folder-crashed -o scratch/crashed.ttd --apply
python3 ttdtool.py gc --older-than 14                                                            # the list
```

## Fixtures

`testdata/ttd/branches/` (see [its README section](../../../testdata/ttd/README.md#branched-sessions-for-the-maintenance-tools-branches))
is written by the C++ code, then the tool outputs in `tool-outputs/` by this tool. After a format change:

```bash
tools/build/build.sh core-tests
cmake-build-agent-release/bin/core-tests --gtest_also_run_disabled_tests \
  --gtest_filter='TTDBranchFixtures_Test.DISABLED_WriteBranchFixtures'
python3 tools/python/ttd/tests/make_tool_outputs.py
tools/build/test.sh --gtest_filter='TTDBranchToolOutputs_Test.*'      # the outputs restore as the originals
```

## Tests

```bash
python3 -m pytest -q tools/python/ttd/tests
```

- the reader against `expected.json` (the C++ reader's lines and per-checkpoint content CRCs of every fixture, its
  verdict on every damaged copy, its join of both folders and its `branches.tdx` rebuild);
- every command on the fixtures, writes into a temporary folder;
- the writer: a file copied part by part is byte-identical; a Python join equals the C++ join byte for byte;
- F-4 fuzz: seeded truncations and bit flips of every fixture, bounded to a few seconds; only `TtdError` may come
  out, and the verdict (loads or not, the lines kept) must match a re-read.
