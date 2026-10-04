# Recipe: TTD write journal on demand — record it, switch it, build it later

The write journal records every memory write (time, address, value, PC). It
answers "who wrote address X last" at once. It is **off by default**: on
busy programs it is many times larger than the rest of a recording
([write-journal-e7.md](../../docs/inprogress/2026-09-25-ttd-v2-migration/write-journal-e7.md)).
Without it the same search replays one frame and gives the same answer.

Three ways to have it:

| When | How |
|:--|:--|
| For the whole recording | start with it: MCP `{"action":"start","journal":true}`, WebAPI `{"journal": true}` |
| For a part of the recording | switch it on and off while recording; each span is a segment |
| After the recording | build it for any span by replaying it (about 2-4 ms per frame) |

Port writes do not need it: every OUT is in the port journals of every session.

## MCP (preferred)

```text
time_travel {"action":"start"}                                   # record, journal off
time_travel {"action":"journal_on"}                              # from here the journal records (a segment starts)
time_travel {"action":"journal_off"}                             # the segment ends here
time_travel {"action":"stop"}
time_travel {"action":"journal_build","from_frame":1200,"to_frame":1500}   # build it for frames 1200..1500
time_travel {"action":"status"}                                  # "... write journal off (covers 2 span(s): ...)"
time_travel {"action":"find_last","addr":"0x5C3A"}               # answers at once inside a span
```

## WebAPI

```bash
curl -s -X POST "$BASE/emulator/$EMU_ID/ttd/journal" -H 'Content-Type: application/json' \
     -d '{"enabled": true}' | jq '{write_journal_enabled, write_journal_segments}'

# after "ttd/stop": build frames 1200..1500 (blocks until done)
curl -s -X POST "$BASE/emulator/$EMU_ID/ttd/journal/build" -H 'Content-Type: application/json' \
     -d '{"from_frame": 1200, "to_frame": 1500}' | jq '{ok, frames_built, records, write_journal_segments}'

# from another shell while it runs: progress, or stop it (what was built is kept)
curl -s "$BASE/emulator/$EMU_ID/ttd/journal" | jq .write_journal_build
curl -s -X POST "$BASE/emulator/$EMU_ID/ttd/journal/build/cancel"
```

## CLI

```text
ttd start --journal         # or: ttd start, then ttd journal on / off
ttd journal                 # Write journal: off, N records / Covers: ...
ttd journal build 1200 1500
```

## Lua / Python

```lua
ttd_set_journal_enabled(true)          -- any moment
local r = ttd_build_journal(1200, 1500) -- r.ok, r.frames_built, r.records
```

```python
emu.ttd_set_journal_enabled(True)
emu.ttd_build_journal(from_frame=1200, to_frame=1500)
```

## A saved file, from a script

`tools/verification/ttd-analyzer/scripts/build_write_journal.py` loads a `.ttd`
in a running emulator, builds the journal for the frames asked and saves the
result ([README](../../tools/verification/ttd-analyzer/README.md#building-the-write-journal-of-a-saved-session)):

```text
$ python3 tools/verification/ttd-analyzer/scripts/build_write_journal.py session.ttd --from 130 --to 170
session.ttd (PENTAGON): built 41 frame(s), 1951 writes
the journal covers 1 span(s): frame 130:3 .. 171:7
saved .../session-journal.ttd
```

## Notes

- Building is refused while recording (stop first). The session's last frame
  is not built (no checkpoint after it), nor a frame holding a marker without
  its data (counted in `frames_refused`).
- Frames already covered are left as they are (`frames_covered`); a built
  span joins a recorded one into one segment.
- The segments are saved in the `.ttd` file and come back on load.
