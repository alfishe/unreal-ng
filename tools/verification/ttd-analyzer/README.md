# TTD analyzer

Reads Unreal-NG `.ttd` session files without an emulator: the time-travel engine's format (schema 2), records in
parts, every record checked by CRC32C, one file per segment of history
([container format](../../../docs/emulator/design/debugger/time-travel-debug/ttd-container-format.md),
`core/src/debugger/ttd/ttd.ksy`; `src/ttdcontainer.py` reads it).

v1's files (schema 1) are refused: v1's reader and the analyses built on it (render, heatmap, anomaly report, port
search) went with v1 (Phase 6 of the [TTD v2 migration](../../../docs/inprogress/2026-09-25-ttd-v2-migration/README.md),
2026-10-30). The emulator answers those questions of an engine file itself: `ttd port-events` with `file=`,
`ttd export-clip`, the coverage verbs.

```bash
cd tools/verification/ttd-analyzer
python3 -m src.main info ../../../testdata/ttd/v2/synthetic.ttd      # regions, segments, versions, bytes per stream
python3 -m src.main validate ../../../testdata/ttd/v2/synthetic.ttd  # every CRC, every version decoded, dependencies
python3 -m src.main parts ../../../testdata/ttd/v2/synthetic.ttd     # part by part: frames, bytes, dependencies
python3 -m src.main recover ../../../testdata/ttd/v2/synthetic-unfinished.ttd   # what a crashed recording keeps
python3 -m pytest -q tests                                          # its tests
```

```
part 0: frames 0+8, 4 records, depends on nothing, ok [file start]
    pieces 861 B, checkpoints 145 B, events 57 B, bus-reads 67 B
part 1: frames 8+8, 4 records, depends on [0], ok
```

`validate` checks what the emulator's reader checks (header, index and trailer, every record, complete parts, unknown required streams) and more: it decodes every piece version through its chain and compares its content CRC, and checks each part's dependency list against the parts it really needs. The fixtures in `testdata/ttd/v2/` are written by the C++ writer (`TTDSessionFile_Test.DISABLED_WriteAnalyzerFixtures`); `tests/test_ttdcontainer.py` finds in them the counts the C++ reader found (`expected.json`).

`info` prints the write journal's spans when the session holds it only for part of its history (D40):

```
write journal: 1,951 records in 1 blocks (682 B on disk, 22.9 KB verbatim -> 36.9x)
  0.35 B/record, globalT 9318421 … 12257287
  covers 1 span(s): (9318403 … 12257287]
```

## Building the write journal of a saved session

The analyzer reads files but cannot replay them. `scripts/build_write_journal.py` adds the write journal to a saved session for any span by replaying it in a running emulator, through the WebAPI:

1. reads the recorded machine of the file;
2. creates an instance of that model with the same General Sound card and loads the session;
3. builds the journal for the frames asked (about 2-4 ms per frame);
4. saves the session with it and removes the instance.

Start the desktop app with the WebAPI (`UNREAL_WEBAPI_PORT` moves it off 8090), then:

```
$ python3 tools/verification/ttd-analyzer/scripts/build_write_journal.py session.ttd --from 130 --to 170
session.ttd (PENTAGON): built 41 frame(s), 1951 writes
the journal covers 1 span(s): frame 130:3 .. 171:7
saved .../session-journal.ttd
```

Options: `--out` (default `<input>-journal.ttd`), `--from` / `--to` (frames; default the whole session), `--base-url` (default `http://localhost:8090`). Frames already covered are left as they are. Exit status 1 when the emulator refuses (a model or card it cannot provide, a file it cannot read).

## Scripts

| Script | What it does |
|---|---|
| `scripts/build_write_journal.py` | Adds the write journal to a saved session by replay (above) |
| `scripts/record_fixtures.py` | Records the `testdata/ttd/engine` corpus sessions through the WebAPI |
| `scripts/record_port_journal_fixtures.py` | Records the port-journal fixtures (`testdata/ttd/port-journals/engine`) and their expected answers |
