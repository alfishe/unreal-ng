# TTD cause chain: design

Requirements and acceptance: [README.md](README.md). Steps and measurements: [plan.md](plan.md).

## 1. What already exists

| Piece | Where | Used for |
|-------|-------|----------|
| `FindAccesses` (list mode of `find`): the last writes to an address before a time, with a page filter; the write journal answers at once, other frames are replayed (coverage-pruned); the machine goes back to where it stood | `timetravelcontroller.cpp` | the last write to a memory byte |
| `RestoreCheckpointForReplay` + `ReplayModeScope` + `ReplayWithinFrame(frame, endT)`, `SaveLiveState` / `RestoreLiveState` | `timetravelcontroller.cpp` | replaying one frame up to a moment and returning |
| `Z80::m1TraceHook` (every instruction start, PC of the first prefix) and `Z80::busTraceHook` (every memory read / write and port read / write with its value, `cpu.t` at the access) | `z80.h` | the instruction records of a replayed frame. Null outside the query: no hot-path change (the L1 replay tracer uses them the same way) |
| `analysis::DecodeOp`: length, class, registers read / written, memory form, immediate, I/O | `analyzers/analysis/z80opinfo.h` | classifying each instruction |
| `Z80Disassembler::disassembleSingleCommand` | `disassembler/z80disasm.h` | the instruction text of a link |
| `PortClassifier` (TD-5): the port map device a port belongs to | `ttdcontrollertimeline.cpp`, moved to a header of its own | naming the device of a port read |
| Time of an access: `TTDTimePoint{frame_counter, TtdTInFrame(cpu.t)}` | `memory.cpp`, `z80.cpp` (the probe) | trace times identical to `find`'s |

## 2. The query

```text
TTDCauseQuery
  target      memory byte: z80 address + optional physical page (default: the page mapped at that moment)
              or register: A F B C D E H L IXH IXL IYH IYL I R, pairs AF BC DE HL IX IY SP, primed A' ... L'
  at          a moment (frame, tInFrame); default the current position
  depth       links from the target to a leaf (default 4, max 16)
  maxLinks    links in the whole tree (default 32, max 256)
  maxFrames   frames searched back for a register's producer (default 50, max 1000)
  flags       follow F as an input (default false: F is listed, not followed)
```

A memory target means "the value this byte holds at `at`": the last write to it at or before `at`. A register target
means "the value the register holds at `at`": the last instruction that wrote it before `at`.

## 3. The trace of a frame

`TraceFrame(frame, endT)` restores the frame's checkpoint, sets the two hooks and replays to `endT` (the end of the
instruction crossing it). Each instruction start opens a record:

```text
InsnRecord { time (TTD units), pc, codePage, bytes[4] (read at the M1 hook, before it runs), regs before
             (AF BC DE HL IX IY SP, AF' BC' DE' HL', I, R), interrupted (an INT / NMI was accepted before it),
             first event, event count }
BusEvent   { type R / W / I / O, address, value, physical page (memory), time }
```

The bus events of an instruction are the ones between its start and the next start. An interrupt accepted after an
instruction pushes PC there too: when the next start says `interrupted`, the last two writes of the pending record
are the interrupt's push (and the IM 2 vector reads after them), split off as a pseudo record of kind `interrupt`.
Opcode and operand fetches arrive as reads inside `[pc, pc + length)`: they are the instruction's bytes (its
immediate), not data.

A frame is 17,000 to 140,000 instructions (turbo); a record is about 64 bytes with its events, a few MB at most. The
walker keeps a small cache of traced frames (8) because a chain often returns to the same frame. Every replay ends
with `RestoreLiveState`, as `FindAccesses` does.

## 4. Source rules

One function, `Explain(record)`, gives the instruction's outputs, each with its inputs. A name is a register byte
(A ... L, IXH ..., I, R, the primed set as names of their own) or a memory byte (address + page). The rules work on
the decoder's class and memory form, and on the record's bus events, never on the program.

| Instruction | Output | Inputs (kind of link) |
|-------------|--------|-----------------------|
| `LD (HL)/(BC)/(DE)/(IX+d)/(IY+d)/(nn),r` | the byte written | the register (`store`); `LD (HL),H` / `L` and `LD (IX+d),H` / `L` name H / L |
| `LD (nn),rr`, `PUSH rr`, `EX (SP),rr` (the write) | two bytes | the low half to the lower address, the high half to the higher (`store` / `stack`) |
| `LD (HL)/(IX+d),n` | the byte | the immediate (`constant`, leaf) |
| `CALL`, `RST` | two bytes | the return address (`constant`, leaf: "return address of the call at PC") |
| interrupt push | two bytes | the interrupted PC (`interrupt`, leaf) |
| `LDI` `LDD` `LDIR` `LDDR` | the byte at DE | the byte read at HL in the same record (`block`) |
| `INI` `IND` `INIR` `INDR` | the byte at HL | the port read (`port`, leaf) |
| `INC (HL)`, `DEC`, rotates / shifts, `SET` / `RES` on memory, `RLD` `RRD` | the byte | the byte read at the same address, plus A for RLD / RRD, plus F for a carry-in (`modify`) |
| `LD r,r'` | r | r' (`copy`) |
| `LD r,n`, `LD rr,nn` | r / both halves | the immediate (`constant`, leaf) |
| `LD r,(mem)`, `LD rr,(nn)`, `POP rr`, `EX (SP),rr` (the read) | r / both halves | the bytes read, low half from the lower address (`load`) |
| `IN r,(C)`, `IN A,(n)` | r | the port read: port, value, device (`port`, leaf) |
| `EX AF,AF'`, `EXX`, `EX DE,HL` | each swapped byte | its partner byte (`copy`) |
| `LD A,I`, `LD A,R` | A | I / R (`constant` leaf for R: the refresh counter; I followed) |
| ALU, `CP` (F only), `INC r` / `DEC r` / 16-bit `INC` `DEC` `ADD` `ADC` `SBC`, rotates on registers, `CPL` `NEG` `DAA`, `SET` / `RES` on registers, block pointer and counter updates, `DJNZ`'s B | each register written | the value registers it reads (its memory operand's address registers excluded) and the data bytes it reads (`computed`); F is listed when read, followed only with `flags` |

A record whose outputs do not include the name asked for is not its producer. The register search walks the trace
back from the instruction that read the register; past the frame's start it traces the frame before, up to
`maxFrames`.

## 5. The walk

```text
node(name, beforeTime):
  memory byte:  the producer is the last write to it before beforeTime: first the traced frame's records (cheap),
                then FindAccesses with the page filter (journal or pruned replay). Trace the producer's frame.
                None in the history: the byte held its value at the session start (`initial`, leaf) - in ROM:
                `rom`, leaf
  register:     the producer is the last record before beforeTime whose outputs include it (frames traced back
                up to maxFrames; none: `stop` with the reason)
  link = { kind, producer's time, pc, code page, text, the value written, inputs }
  for each input (breadth bounded by maxLinks, depth by depth):
     register input -> node(register, the producer's start)
     memory input   -> node(byte, the time of its read in the producer)
     port / constant / interrupt -> leaf
```

**Checks (CC-5).** The value a link reports is the one its producer wrote (from the bus event, or the register after
the instruction). A memory input's value is the byte read; its producer must have written the same byte. When it
did not, something other than the CPU wrote the byte in between (a device, a ROM trap, DMA): the link is `device`
(leaf) with both values and the window. The same for an `initial` byte whose value differs from the session start's.

**Stops.** A link that cannot go on carries `stop`: `depth`, `max_links`, `max_frames`, `session_start`,
`barrier` (a marker the replay cannot cross, with its kind and time).

## 6. The reply

JSON (REST, the socket call, MCP `structuredContent`):

```text
{ target: {kind, name, address, page, register, at},
  links: [ { id, parent, depth, kind, name, value, frame, t, pc, page, text, inputs: [names], detail, stop } ],
  stops: n, frames_replayed: n, ms: x }
```

MCP / CLI text: one line per link, indented by depth, newest first:

```text
(#5C78 RAM 5) = #03
  store  f12034+5120  #8A14 LD (HL),A           <- A = #03
    load   f12034+4870  #8A0E LD A,(IX+3)       <- (#C003 RAM 0) = #03
      block  f11990+20  #9000 LDIR              <- (#6003 RAM 2) = #03
        initial  (#6003 RAM 2) = #03 at the session start
```

## 7. Costs

- No recording cost: nothing is captured; the hooks are null except inside the query (CC-7).
- Per link: one `FindAccesses` (journal: at once; else the coverage-pruned replay it already does) and one frame
  trace (a frame's replay with the hooks, 2-6 ms); a register producer far back adds a trace per frame searched.
  Bounded by `maxLinks` x (1 + `maxFrames`) frames in the worst case, typically a few frames per chain.
- Memory: the frame cache (8 frames, a few MB each at most), released at the end of the query.

## 8. The planes

| Plane | Form |
|-------|------|
| CLI | `ttd cause <addr>[:page] | <reg> [frame [t]] [depth N] [links N] [frames N] [flags]` |
| WebAPI | `GET /api/v1/emulator/{id}/ttd/cause-chain?addr=&page=&reg=&frame=&t=&depth=&max_links=&max_frames=&flags=`, the socket `call` of the same route |
| MCP | `time_travel {"action":"cause_chain", "addr" or "reg", "frame", "t", "depth", "max_links", "max_frames", "flags"}`: the text of §6 |
| Lua / Python | `ttd_cause_chain(target, options)`: the JSON as a table / dict |

Docs: command-interface.md (the verb), webapi-interface.md, cli-interface.md, lua-interface.md,
python-interface.md, the MCP README, the reverse-debugging recipe.
