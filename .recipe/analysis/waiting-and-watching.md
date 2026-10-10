# Waiting for a condition instead of `sleep`, and finding who writes an address

A script that drives the emulator and then `sleep`s is timing-dependent: the host is loaded, the machine runs at a
different speed, a boot takes a frame longer, and the next keypress lands in the wrong place. Every wait below is
either counted in emulated time or ends on an event the emulator reports. Works on every machine (main Z80).

| You want to wait until... | Use | Notes |
|:--|:--|:--|
| a fixed amount of emulated time has passed | `POST /run_frames {"frames":N}` (or `run_tstates`) on a paused machine | Deterministic: the same N frames every run, whatever the host load. Keyboard input queued before it is played inside those frames ([keyboard](../input/keyboard.md)). |
| PC reaches an address | `POST /skip_until {"pc":"0x8000","max_tstates":N}` | Synchronous; breakpoints are skipped on the way. For a free-running machine: an `exec` breakpoint + [the wait loop](#the-wait-loop). |
| a byte / range of memory is written | a `write` breakpoint (range, or a physical page) + [the wait loop](#the-wait-loop) | Needs the `breakpoints` feature. Stops at the write: see [who writes an address](#who-writes-an-address). |
| a port is read or written (FDC command, `#7FFD`, ...) | a `port_in` / `port_out` breakpoint (`port_mask` for partial decoding) + the wait loop | |
| the program waits for a key | an `exec` breakpoint on the program's own key-wait routine + the wait loop; or `run_frames` chunks + `capture/ocr` until the prompt is on screen | A `port_in #FE` breakpoint is no signal on its own: the 48K / 128K ROM interrupt handler and TR-DOS scan the keyboard every frame. Use it only for a program that runs with interrupts off or its own handler, or with `hits` to skip the ROM's reads. |
| some text appears on screen | `run_frames` in chunks (e.g. 10) + `GET /capture/ocr` until the text is there, with a frame budget | |
| the screen stops changing | `run_frames` chunks + `GET /state/screen/digest` until `.combined` repeats | Flashing attributes change the digest every 16 frames; compare over 32+ frames. |
| a typed sequence is played out | `GET /keyboard/status` until `sequence_running` is false | |

Run control in [execution-control-and-stepping.md](execution-control-and-stepping.md), breakpoint kinds in
[breakpoints-and-events.md](breakpoints-and-events.md), the long-poll and the snapshot in
[debugger-snapshot.md](debugger-snapshot.md).

## The wait loop

`GET /debug/wait?since=SEQ&timeout_ms=M` answers when the debugger's change counter `seq` moves past `SEQ`, or after
`M` ms (at most 60000). `seq` moves on **every** stop (breakpoint, pause), on a direct run's end (`step`, `run_frames`)
and on tool edits of memory. So a `changed: true` answer is a hint, not the event: check `state` and `pause` and wait
again otherwise. `pause` is the *last* stop and stays after a resume, and a deleted breakpoint's id is given to the
next one: a match on `pause.breakpoint_id` alone can be an old stop. Check `state == "paused"` with it.

Order matters: set the breakpoint and read `seq` while the machine is paused, then resume. A breakpoint hit between
"resume" and "read seq" would otherwise be lost.

```bash
BASE=http://localhost:8090/api/v1
curl -s -X POST $BASE/emulator/$EMU/pause > /dev/null
curl -s -X PUT  $BASE/emulator/$EMU/feature/breakpoints -H 'Content-Type: application/json' -d '{"enabled":true}' > /dev/null
BP=$(curl -s -X POST $BASE/emulator/$EMU/breakpoints -H 'Content-Type: application/json' \
         -d '{"type":"exec","address":"0x8000"}' | jq .id)
SEQ=$(curl -s "$BASE/emulator/$EMU/debug/wait?timeout_ms=0" | jq .seq)   # timeout_ms=0: answer at once
curl -s -X POST $BASE/emulator/$EMU/resume > /dev/null

for i in $(seq 1 12); do                                                  # 12 x 5 s budget
  R=$(curl -s "$BASE/emulator/$EMU/debug/wait?since=$SEQ&timeout_ms=5000")
  SEQ=$(jq .seq <<<"$R")
  jq -e --argjson bp "$BP" '.state=="paused" and .pause.reason=="breakpoint" and .pause.breakpoint_id==$bp' \
     <<<"$R" > /dev/null && break
done
jq -c . <<<"$R"
# {"changed":true,"pause":{"address":32768,"breakpoint_id":1,"reason":"breakpoint"},"seq":6,"state":"paused"}
# budget spent, never reached: {"changed":false,"pause":{"reason":"step"},"seq":6,"state":"running"}
```

Python (`tools/` scripts speak plain HTTP) is the same loop: `requests.get(f"{base}/emulator/{emu}/debug/wait",
params={"since": seq, "timeout_ms": 5000})`. MCP: `control_execution {"action":"wait","since":SEQ,"timeout_ms":5000}`;
CLI `debug-wait SEQ --timeout 5000`; Lua `debug_wait(SEQ, 5000)`; Python binding `emu.debug_wait(since=SEQ,
timeout_ms=5000)`. Instead of polling, the WebSocket `/api/v1/websocket` pushes a `paused` event
([breakpoints-and-events.md](breakpoints-and-events.md#events-apiv1websocket)).

Delete the breakpoint (`DELETE /breakpoints/{id}`) when done: a forgotten one stops the next scenario.

## Who writes an address

Three ways, from cheapest (a worked example on MASM 2.0, from the data to the code that keeps it:
[program-state-reverse.md](../articles/program-state-reverse.md)):

1. **Live, the next write.** A `write` breakpoint on the address (or range, or `page` for a physical page wherever it
   is mapped) + the wait loop. The machine stops during the access, after the PC has moved past the
   instruction: the writer is the newest entry of `GET /debug/pchist?depth=4` (read it once before the resume:
   the first read starts the history). On a 48K, a write breakpoint on `FRAMES` (`#5C78`) stops with pchist
   `[62, 61, 58, 57]`: `#003E` is `LD (FRAMES),HL` in the ROM's interrupt handler. Resume and wait again for the
   next writer; `hit_count` in `GET /breakpoints` tells how often it fired. Each hit stops the machine; there is
   no log-only mode yet.
2. **Back in time, the last write.** With a TTD recording running, `POST /ttd/find-last
   {"addr":"0x5C3A","access":"write"}` returns the last write before the current point with its `pc`, `value`,
   `frame` and `phys_page`; repeat with `before` to walk further back. Without a recording the answer is just
   `{"found": false}`, not an error
   ([ttd-reverse-debugging.md](ttd-reverse-debugging.md), the article [bug-hunt-ttd.md](../articles/bug-hunt-ttd.md)).
3. **Statically.** `POST /memory/find` for the address bytes as an operand (`"pattern_hex":"32 3A 5C"` is `LD
   (#5C3A),A`, `22 3A 5C` is `LD (#5C3A),HL`) over `"space":"ram"` (every RAM page, results as page + offset; ROM
   code needs `"space":"cpu"` or `"rom0"`..), then
   `GET /disasm` around the hits ([memory-search-map-and-regions.md](memory-search-map-and-regions.md)).

Comparing two dumps on the host finds *what* changed; one of the three above finds *who*.

## Pitfalls

- **`changed: true` is not "my breakpoint"**: a `run_frames` from another client, a pause, or a memory write through
  the API also moves `seq`. Check `pause.breakpoint_id`.
- **`run_frames`, `run_tstates`, `run_to_*`, `skip_until`, `listing/run_to_line` skip breakpoints** by default: a
  breakpoint wait needs a free-running machine (`resume`) or `steps`.
- **Memory and port breakpoints need the `breakpoints` feature**: `PUT /feature/breakpoints {"enabled":true}`
  (it switches debug mode on too). `PUT /debugmode` alone does not: the breakpoint is accepted, listed as
  active, and never fires (`hit_count` stays 0).
- **Disk loading and breakpoints in TR-DOS**: with `fastdisk` on (the default) the sector transfer at `#3FEC` is done
  in one step, without the `INI` loop: a `port_in #7F` breakpoint and the port trace see no data bytes. Turn it off
  for that: `PUT /feature/fastdisk {"enabled":false}` ([port-trace.md](port-trace.md#pitfalls)).
