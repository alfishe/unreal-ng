# Recipe: Insert / Eject / Inspect a Disk Image

Goal: mount a disk image into drive A or B, know it mounted, and read its
catalog without touching TR-DOS.

Supported container formats: `.trd .scl .fdi .udi .dsk .td0 .mgt .img`.

Related: [autostart-disk.md](../run/autostart-disk.md) (boot it too),
[manual-trdos-run.md](../run/manual-trdos-run.md) (choose a file yourself).

> **How to use the sections:** [MCP](#mcp-preferred) is preferred — `load_software`
> covers insertion, `invoke_api` the inspection endpoints. Use
> [WebAPI](#webapi) only inside host-side Python/bash pipelines or when MCP
> is unavailable (policy: [_common/transports.md](../_common/transports.md)).

## MCP (preferred)

```text
load_software {"path":"scratch/game.trd","drive":"A"}        # insert (local file → uploaded automatically)
load_software {"path":"scratch/game.trd","autostart":true}   # insert + boot — see ../run/autostart-disk.md
invoke_api   {"method":"GET","path":"/api/v1/emulator/{id}/disk/A/info"}
invoke_api   {"method":"GET","path":"/api/v1/emulator/{id}/disk/A/catalog"}
invoke_api   {"method":"POST","path":"/api/v1/emulator/{id}/disk/B/create",
              "body":{"format":"auto"}}                      # +3: formatted +3DOS; elsewhere: unformatted 80/2
invoke_api   {"method":"POST","path":"/api/v1/emulator/{id}/disk/A/eject"}
```

`load_software` reads the file on the agent host when it can and uploads the
bytes; otherwise the path passes through for the emulator to load. The disk
inspection endpoints (info/catalog/sysinfo/sector/track) have no smart tool —
call them with `invoke_api`; `{id}` is substituted with the resolved target.

## WebAPI

The curl walkthrough below — right choice when a Python/bash pipeline drives
these steps directly.

### Insert by path (emulator-readable location)

```bash
curl -s -X POST "$BASE/emulator/$EMU_ID/disk/A/insert" \
     -H 'Content-Type: application/json' \
     -d '{"path": "/Volumes/.../scratch/game.trd"}' | jq .
```

Response (assert `status == "success"`):

```json
{
  "status": "success",
  "message": "Disk inserted successfully",
  "path": "/Volumes/.../scratch/game.trd",
  "drive": "A"
}
```

Drive names: `A` and `B`. Add `"autostart": true` **only for drive A** (see
[autostart-disk.md](../run/autostart-disk.md)); requesting it for B is a hard
400, never silently ignored.

### Insert by upload (bytes from the agent's machine)

Same endpoint, multipart form instead of JSON; the emulator saves the file
into its uploads area and mounts it. The MCP `load_software` tool does this
automatically when the path exists locally:

```bash
curl -s -X POST "$BASE/emulator/$EMU_ID/disk/A/insert" \
     -F "file=@scratch/game.trd" | jq .
```

MCP one-liner:

```bash
curl -s -X POST http://localhost:8092/mcp -H 'Content-Type: application/json' -d '{
  "jsonrpc":"2.0","id":1,"method":"tools/call",
  "params":{"name":"load_software","arguments":{"path":"scratch/game.trd","drive":"A"}}}' | jq .
```

### Verify what is in the drive

```bash
# Mounted image + write-protection state
curl -s "$BASE/emulator/$EMU_ID/disk/A/info" | jq .
# → {"status":"inserted","drive":"A","file":"...game.trd","write_protected":false}

# Drives overview
curl -s "$BASE/emulator/$EMU_ID/disk" | jq '.drives'
```

`status` is `"inserted"` or `"empty"` — poll it to confirm ejects/inserts.

### Read the catalog (find the .B / .C files)

```bash
curl -s "$BASE/emulator/$EMU_ID/disk/A/catalog" | jq '.files'
```

Response per file (TR-DOS directory, track 0):

```json
{
  "name": "scroller ",
  "type": "B",
  "start": 23750,
  "length": 1234,
  "sectors": 7,
  "first_sector": 1,
  "first_track": 2
}
```

- `type`: `"B"` = BASIC program (what `RUN "name"` executes), `"C"` = code
  block (has a `start` address), `"D"`/`#` = data/print/other.
- Names are **8 chars, space-padded** — strip trailing spaces before quoting
  them in TR-DOS commands: `RUN "scroller"`.
- Deleted entries are skipped; `file_count` totals the visible ones.

MCP:

```bash
curl -s -X POST http://localhost:8092/mcp -H 'Content-Type: application/json' -d '{
  "jsonrpc":"2.0","id":1,"method":"tools/call",
  "params":{"name":"invoke_api","arguments":{
    "method":"GET","path":"/api/v1/emulator/{id}/disk/A/catalog"}}}' | jq '.result.structuredContent.files'
```

### Create a blank disk (for SAVE experiments)

```bash
curl -s -X POST "$BASE/emulator/$EMU_ID/disk/B/create" \
     -H 'Content-Type: application/json' \
     -d '{"format": "unformatted", "cylinders": 80, "sides": 2}' | jq .
# → {"success":true,"drive":"B","format":"unformatted","cylinders":80,"sides":2}
```

- `format`: `auto` (the default) is `plus3` on a +3 and `unformatted` elsewhere.
  `unformatted` has no sectors: FORMAT it from TR-DOS (or +3DOS) before saving.
  `plus3` is a formatted +3DOS disk (9 x 512-byte sectors per track): SAVE right away.
- `cylinders` 40 or 80, `sides` 1 or 2; left out, the format's own (80/2
  unformatted, 40/1 plus3).
- Blank disks report `file: "<blank>"` in `/disk/B/info`.
- CLI: `disk create B 80 2 unformatted`; Lua/Python: `disk_create(1, 0, 0, "plus3")`.

### ZX Spectrum +3 (.dsk, +3DOS)

The `PLUS3` model has its own controller (uPD765A) and drives A and B; +3DOS
reads and writes `.dsk` / Extended DSK images. The `PLUS2A` model is the same
machine without the controller: no drives (its menu says "Drive M: available").

```text
emulator_manage {"action":"create","model":"PLUS3"}
load_software   {"path":"scratch/game.dsk","drive":"A"}            # or disk/A/create for a blank one
invoke_api      {"method":"POST","path":"/api/v1/emulator/{id}/basic/run","body":{"command":"CAT"}}
invoke_api      {"method":"POST","path":"/api/v1/emulator/{id}/basic/run","body":{"command":"LOAD \"game\""}}
inspect_state   {"aspects":["fdc"]}      # [fdc] uPD765A (+3): phase, last command C H R N, ST0, motor, drives
```

- `basic/run` leaves the menu for `+3 BASIC` by itself and types there; +3DOS
  commands are ordinary BASIC keywords (`CAT`, `LOAD "name"`, `SAVE "name"`).
- The `fdc` aspect (`GET /state/fdc`) reports the uPD765A on a +3: phase,
  main status, the command in hand, ST0-ST2, SPECIFY times, units and drives.
- Disk access runs at the real speed (the drive spins up, the head steps, the
  disk turns): give `basic/run` the frames a real +3 would need.

### Eject

```bash
curl -s -X POST "$BASE/emulator/$EMU_ID/disk/A/eject" | jq '.status'
# then confirm:
curl -s "$BASE/emulator/$EMU_ID/disk/A/info" | jq '.status'   # → "empty"
```

## Pitfalls

- **Path unreadable by the emulator** → `400` with `Failed to insert disk`.
  Paths resolve in the emulator process, not the agent's shell; prefer
  absolute paths or the multipart upload.
- **Non-TR-DOS images** (CP/M, +3 DSK, MGT) mount fine but autostart refuses
  them (mount-only; on a +3 use `basic/run` with `LOAD "name"`) and `/catalog` reports `dos_type: "TR-DOS"` only for real
  TRD directories — inspect via `/disk/{drive}/sector/...` instead.
- **Dirty writes**: disk images opened read-write accumulate changes. Keep
  master images pristine; work on copies under `scratch/`.
- **48K + Beta 128**: works, but for smooth TR-DOS flows use `128k`/`PENTAGON`
  models (see [setup.md](../_common/setup.md)).
