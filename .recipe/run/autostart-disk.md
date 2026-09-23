# Recipe: Disk Autostart (One-Call Boot)

Goal: insert a disk **and** get its program running with a single call — no
typing, no TR-DOS navigation. This is the same machinery as the Qt UI's
drag-and-drop autostart.

When you need to *choose* among several programs on a disk, use
[manual-trdos-run.md](manual-trdos-run.md) instead.

> **How to use the sections:** [MCP](#mcp-preferred) is preferred — `load_software`
> is the one-call boot. Use [WebAPI](#webapi) only inside host-side
> Python/bash pipelines, for the `X-Autostart` multipart-upload form, or when
> MCP is unavailable (policy: [_common/transports.md](../_common/transports.md)).

## MCP (preferred)

```text
load_software      {"path":"scratch/game.trd","autostart":true}   # the one-call boot
control_execution  {"action":"run_frames","frames":150}          # give the boot a moment
inspect_state      {"aspects":["screen_ocr"]}                    # menu? title screen?
inspect_state      {"aspects":["screen_digest"]}                 # deterministic confirmation
```

The response carries the same `autostarted` / `autostart_message` fields in
`structuredContent`; drive-A-only and quick-reset semantics below apply
identically.

## WebAPI

Curl form — right choice for Python/bash pipelines and for raw multipart
uploads (which carry autostart via the `X-Autostart: true` header instead of
a JSON body).

### The one call

WebAPI:

```bash
curl -s -X POST "$BASE/emulator/$EMU_ID/disk/A/insert" \
     -H 'Content-Type: application/json' \
     -d '{"path": "/abs/scratch/game.trd", "autostart": true}' | jq .
```

Response:

```json
{
  "autostarted": true,
  "autostart_message": "boot.b started",
  "status": "success",
  "message": "Disk inserted successfully",
  "path": "/abs/scratch/game.trd",
  "drive": "A"
}
```

Assert on `status == "success"`; `autostarted` tells you whether the boot
sequence also engaged (`false` = mounted only, reason in
`autostart_message`).

MCP:

```bash
curl -s -X POST http://localhost:8092/mcp -H 'Content-Type: application/json' -d '{
  "jsonrpc":"2.0","id":1,"method":"tools/call",
  "params":{"name":"load_software","arguments":{"path":"scratch/game.trd","autostart":true}}}' | jq .
```

For multipart/raw uploads the WebAPI also honors an `X-Autostart: true`
header.

## What autostart actually does (the decision table)

The emulator inspects the TR-DOS catalog and picks the action — no keystrokes
are ever synthesized:

| Disk content | Action |
|:--|:--|
| Not TR-DOS format (CP/M, +3 DSK, MGT...) | Mount only |
| Has a `boot.B` file | Quick reset + direct entry into TR-DOS, boots it |
| No `.B` files at all | Mount only |
| Exactly **one** `.B` file (no `boot`) | Quick reset + direct entry; a one-shot hook rewrites TR-DOS's cold-start `RUN "boot"` into `RUN "<name>"` — disk stays untouched |
| Several `.B` files (no `boot`) | Injects the bundled Unreal commander as `boot.B` and boots it — you get a file menu on screen |

"Direct entry" pages the TR-DOS ROM in at `PC=0` and runs it — that is why
no keyboard timing is needed and why this is reliable where typed
`RANDOMIZE USR 15616` can race.

### Confirm the program is running

```bash
# Give the boot a moment (TR-DOS loads the file), then:
curl -s -X POST "$BASE/emulator/$EMU_ID/run_frames" \
     -H 'Content-Type: application/json' -d '{"frames": 150}' >/dev/null
curl -s "$BASE/emulator/$EMU_ID/capture/ocr" | jq -r '.text'      # menu? title screen?
curl -s "$BASE/emulator/$EMU_ID/state/screen/digest" | jq -r .digest
```

Heavier verification (digest stability, per-model expectations) is laid out
in [demo-boot-verification.md](../articles/demo-boot-verification.md).

## Constraints

- **Drive A only.** `autostart: true` on drive B is a hard error by design
  (TR-DOS/Beta 128 boots from A), not silently ignored.
- **TR-DOS-capable model.** Create a `PENTAGON` or `128k` instance
  ([setup.md](../_common/setup.md) §3). A `48K` machine without Beta 128
  just mounts.
- Autostart **quick-resets the machine**: register/RAM state from before the
  call is gone. Do your snapshot/TTD setup *after* autostart lands.

## Pitfalls

- **`autostarted: false` with `status: "success"`** — the disk mounted but
  the policy decided not to boot (typically "no B files"). Not an error;
  switch to the manual recipe if you expected a program to run.
- **Commander instead of the game** (several `.B` files): read the catalog
  (`GET .../disk/A/catalog`), pick the target, and follow
  [manual-trdos-run.md](manual-trdos-run.md).
- **Write-protected / pristine masters**: autostart never writes the disk in
  the hook path, but the injected-`boot.B` path *does* modify the image —
  work on a copy in `scratch/`.
- **Race with instance creation**: autostart needs the instance fully
  created; if you fire it at a still-starting instance you get a 404. Retry
  once after a second.
