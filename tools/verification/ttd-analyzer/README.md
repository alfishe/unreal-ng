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
