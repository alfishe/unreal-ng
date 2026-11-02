# TTD explain frame: design

Requirements: [README.md](README.md). Plan: [plan.md](plan.md).

## 1. What exists

| Need | Existing | Gap |
|------|----------|-----|
| Every instruction of a frame with its registers, pages and bus cycles | `TraceCauseFrame(cpIndex, endT)` (the cause chain's frame trace: `TTDTraceInsn` per instruction, one record per `HALT` with its idle fetches, a pseudo record per interrupt acknowledge, `TTDTraceEvent` R / W / I / O per bus cycle with the physical page) | Used only for one value's chain |
| The bytes at the frame's start | `RestoreRegion(cpIndex, region, out)` | - |
| Port devices by name | `PortClassifier` (the machine's port decoder rows, as the timeline names devices) | - |
| Keys from a keyboard read | `KeysSeenDown(port, value)`, `KeyboardRowsRead(port)` | - |
| Input | `_inputJournal.Events()` | No per-frame listing |
| Screen layout | `UlaScreenLayout(region, model, p7FFD)` (find event) | - |
| Markers | `FirstBarrierBetween`, the markers list | - |
| Instruction text | the disassembler (`disassembleSingleCommand`) | - |

So the whole answer comes from one replay of the frame (`TraceCauseFrame`), the frame's start bytes and the journals.
Nothing new is recorded.

## 2. The query

```
TTDExplainFrameQuery
  frame        the frame (default: the current position's frame)
  sections     bitmask: time, routines, interrupt, paging, ports, memory, screen, input, markers (default all)
  limit        per list (routines, ports, memory ranges, writers per range), default 8, max 100
```

The frame is replayed from its checkpoint to its end, or to the session end in the last frame (`partial`), or to the
first barrier inside it (`barrier`, listed). A frame outside the history is an error naming the window. Models without
the frame trace (the cause chain's limits: no Z80 main CPU) are refused with the same message as the cause chain.

## 3. The sections

Times are TTD time units inside the frame (T-states at the base clock, as every TTD reply). An instruction's T is the
next record's start minus its own (the last one: the frame end minus its start).

**time.** The frame's T split three ways: the interrupt routine (from the acknowledge to its return, see routines),
`HALT` (the HALT records' durations), the rest; and the instruction count. "ISR 9 %, HALT 31 %, code 60 %".

**routines.** The call structure from the trace, generic for any Z80 code:

- A frame is pushed by a taken `CALL` / `CALL cc`, an `RST`, and the interrupt pseudo record. Its entry is the next
  record's PC (with its code page); its `entrySP` is the SP after the push.
- A taken `RET`, `RET cc`, `RETI`, `RETN` pops: every frame whose `entrySP` is below the SP after the return (a
  routine that dropped its return address and returned to its caller's caller pops two). A return with no frame open
  ends the frame's starting context and starts a new one at the return target.
- Nothing else pops, so a routine that leaves by `POP` + `JP` stays open until a return below it; stack tricks that
  move SP far (a `PUSH`-based screen fill) do not pop anything because no return is taken.
- The code before the first call runs in the starting context, named by the PC at the frame's start ("at #8A31 at the
  frame start").

Per entry: calls, inclusive T (every open frame of the entry counts once, so recursion is not double counted), own T
(the top frame only), instructions, and the first caller (the PC of the first call). The list is sorted by inclusive T,
`limit` of them, then "+N more". The interrupt routine is marked `isr`.

**interrupt.** Each acknowledge: time, the mode (IM 1 / IM 2 with the vector read), the routine entry, its T until the
return, and the PC it returned to (the interrupted PC). None: "no interrupt (DI)" when IFF1 was off at the frame start.

**paging.** A change of `banks[0..3]` between two records: time, the PC of the instruction before (the `OUT`), the
bank, the new page and the old one. At most `limit`, then "+N more".

**ports.** The I and O bus events grouped by direction and device (`PortClassifier`; unknown ports by the port
itself): count, the first time and PC, up to four distinct values in order with "...". A ULA read lists the half-rows
read and the keys seen down (`KeysSeenDown`) once per key. Sorted by count.

**memory.** The bytes the frame changed and who changed them:

- The start: `RestoreRegion` of the RAM region at the frame's checkpoint. The end: the RAM after the replay (read
  before the live state is put back). Changed bytes: start != end.
- The writes: every W event, by physical page and offset, with its instruction.
- Ranges: changed or written offsets merged when the gap is 16 bytes or less, per page. Each range is named by region:
  the shown screen's pixels and attributes (the screen layout), the stack (inside the SP span the frame used, plus 2),
  else the page and the Z80 address it was mapped at by the first write ("RAM 2 #A130").
- Per range: bytes changed, bytes written, the start and end value for a range of one or two bytes, and the writers:
  the instructions that wrote it, by count, at most `limit` (PC, text, count).
- Changed bytes with no W event are a range of their own marked `device` (a DMA, a loader trap, a device writing RAM).
- Sorted by bytes changed; the screen and the stack each count as one range.

**screen.** The shown picture's bytes changed (as find event counts them), in % of the picture and per area (pixels,
attributes); a screen switch (the 7FFD bit) says so. The border: the colours written to the ULA in order, with counts
of repeats ("7 -> 2 x3 -> 7").

**input.** The keys held at the frame's start (the input journal folded up to it), pressed and released inside the
frame with their time.

**markers.** The external events inside the frame (snapshot load, tape, reset ...), the bookmarks.

## 4. The answer

Structured:

```
frame, partial, barrier{...}, t_frame, instructions
time{isr, halt, code}           T each
routines[] {pc, code_page, isr, calls, t, own_t, insns, caller, text}, more
interrupts[] {tinframe, mode, vector, entry, t, returned_to}
paging[] {tinframe, pc, bank, page, was}, more
ports[] {dir, device, port, count, tinframe, pc, values[], keys[]}, more
memory[] {region, page, addr, length, changed, written, before, after, device, writers[] {pc, text, count}}, more
screen{changed, pixels, attributes, switched, border[] {colour, count}}
input{held[], events[] {tinframe, key, pressed}}
markers[]
frames_replayed, ms, text
```

Text (MCP, CLI; `text=true` on REST), one block per section, the empty ones left out:

```
frame 1204 (69888 T, 18312 instructions): ISR 9 % (6210 T), HALT 31 %, code 60 %
routines by time:
  #8A14        1x  31220 T 45 %  own 2110  4812 insns  from #8003
  #9A00 (isr)  1x   6210 T  9 %  own 1240             IM 1 via #0038
  ... +12 more
interrupt: +0 IM 1 -> #0038, 6210 T, back to #8A31
paging: +14320 #8044 bank 3 RAM 0 -> RAM 7
ports: out ULA x3 (#07 #02 #07) from +210 #8B02; in ULA x8 rows #FE..#7F (SPACE down) from +240 #8C10
memory:
  screen pixels #4800-#4FFF  1024 changed  by #9A12 ld (hl),a x1024
  RAM 2 #A130  #10 -> #00  by #A46B ld (#A130),a
  stack #FF40-#FF5F  18 written
screen: 15 % changed (pixels 16 %, attributes 4 %); border 7 -> 2 x3 -> 7
input: held SPACE; +12000 Q pressed
```

## 5. Costs

One replay of the frame with the M1 and bus hooks (the cause chain measured 2-6 ms on a 69888 T frame), one RAM
restore at the checkpoint (a memcpy of the RAM region), a pass over the trace. Read-only: `SessionOperation` with the
live state saved and put back, `PauseRecordingForBrowsing` for a live recording, as the cause chain.

## 6. The planes

| Plane | Form |
|-------|------|
| Control verb | `explain-frame` with `frame`, `sections`, `limit`, `text` |
| CLI | `ttd explain [F] [sections=...] [limit=N]`: the text |
| WebAPI | `GET /api/v1/emulator/{id}/ttd/explain-frame?frame=F&sections=...&limit=N&text=true`; the socket `call` of the route |
| MCP | `time_travel {"action":"explain_frame", "frame", "sections", "limit"}`: the text |
| Lua | `ttd_explain_frame([frame], [opts])`: a table |
| Python | `ttd_explain_frame(frame=None, **opts)`: a dict |

## 7. What it does not do

- No per-game names: a routine is an address; the symbols (labels loaded into the debugger) name it when present.
- No call graph across frames: one frame, and the routines open at its start are named by their entry only if they
  were called inside it.
- Not a profiler over many frames: one frame per call (the timeline already summarises a range).
