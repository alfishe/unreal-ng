# TTD analyzer

Reads Unreal-NG `.ttd` time-travel sessions without an emulator: the header, checkpoints, journals and the recorded machine.

```bash
cd tools/verification/ttd-analyzer
python3 -m src.main info session.ttd        # header + checkpoint overview, journals, coverage
python3 -m src.main validate session.ttd    # integrity checks (CI-friendly)
python3 -m src.main search session.ttd key 5   # "when did the program ...": the port journals
python3 -m src.main --help                  # every command
python3 -m pytest -q tests                  # its tests
```

The file format: [ttd-v1-architecture-and-format.md](../../../docs/emulator/design/debugger/time-travel-debug/ttd-v1-architecture-and-format.md) and `core/src/debugger/ttd/ttd.ksy`.

## Schema 2: the time-travel engine's session file

The engine that replaces v1 (TTD v2, [migration](../../../docs/inprogress/2026-09-25-ttd-v2-migration/README.md)) writes another format: records in parts, every record checked by CRC32C, one file per segment of history ([phase-4 TDD](../../../docs/inprogress/2026-09-25-ttd-v2-migration/phase-4-session-file-tdd.md), `core/src/debugger/ttd/engine/ttdsession.ksy`). `info` and `validate` tell the schemas apart by the header; `src/ttdcontainer.py` reads schema 2.

```bash
python3 -m src.main info ../../../testdata/ttd/v2/synthetic.ttd      # regions, segments, versions, bytes per stream
python3 -m src.main validate ../../../testdata/ttd/v2/synthetic.ttd  # every CRC, every version decoded, dependencies
python3 -m src.main parts ../../../testdata/ttd/v2/synthetic.ttd     # part by part: frames, bytes, dependencies
python3 -m src.main recover ../../../testdata/ttd/v2/synthetic-unfinished.ttd   # what a crashed recording keeps
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
| `scripts/record_fixtures.py` | Records the `testdata/ttd` corpus sessions through the WebAPI |
| `scripts/record_port_journal_fixtures.py` | Records the port-journal fixtures and their expected answers |
