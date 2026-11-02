# TTD cause chain: where a value came from

**Status 2026-11-02:** design. Plan row [#112](../PLAN.md). The first item of the agent UX roadmap after TD-5
(owner 2026-11-01: cause chain, then find-event, then explain-frame; each accepted only if WebAPI / MCP measurements
prove it).

Files:

| File | What |
|------|------|
| [design.md](design.md) | the query, the replay, the source rules per instruction, the reply, costs, the planes |
| [plan.md](plan.md) | steps, tests, benchmarks (the one call against the old loop), acceptance |

## The problem

An agent asks "why is this byte 0x03?" (a lives counter, a sprite byte on the screen, a level number) or "where did A
get this value?". Today it answers with a loop of calls:

1. `find_last` write to the address: the writer's PC, time and value (the machine seeks there);
2. `disassemble` at the PC: `LD (HL),A`, so the value came from A;
3. `reverse_step` one instruction at a time, `registers` and `disassemble` after each, until an instruction that set
   A (`LD A,(IX+3)`): tens to thousands of steps when the value was loaded early in the frame;
4. the address that instruction read (IX + 3, worked out by hand from the registers), then `find_last` again for it,
   and so on for every step back;
5. an ALU result (`ADD A,B`) has two inputs: the agent follows one by hand and forgets the other.

Each step costs a round trip, a reply and the agent's tokens; the hand work (effective addresses, EXX, the stack, an
interrupt in between) is where it goes wrong.

## The goal

One call returns the chain back from a value: the write that put it there, the instruction's source (a register, a
memory byte, a port, a constant), and the same for that source, as far back as asked. A computed value lists its
inputs, each followed in turn (a small tree). Every link has its time, PC, instruction text and value, so the agent
can seek to any of them with the existing verbs.

## Requirements

| # | Requirement |
|---|-------------|
| CC-1 | One query, `cause-chain`, on all six planes: CLI, WebAPI (and the socket call), Lua, Python, MCP. Docs chain command-interface.md -> plane docs |
| CC-2 | Targets: a memory byte (Z80 address, optional physical page, at a moment: the current position by default, or a frame / time) and a register (A, F, B, C, D, E, H, L, IXH, IXL, IYH, IYL, the pairs, SP; at a moment) |
| CC-3 | Each link: kind (store, load, copy, computed, constant, port read, stack, block copy, read-modify-write, interrupt push, initial), time, PC, physical page of the code, instruction text, the value, its inputs |
| CC-4 | Computed values (ALU, INC / DEC, rotates, read-modify-write) list every data input; each is followed up to the depth. The tree is bounded: `depth` (default 4, max 16) and `max_links` (default 32, max 256) |
| CC-5 | Exact: the links come from a replay of the recording with the instruction and bus hooks, not from a guess. A value that does not match its source (an emulator bug, a device write the CPU did not do) is reported, not hidden |
| CC-6 | Read-only for the session: the machine returns to where it stood (as `find` in list mode); a live recording pauses for browsing as other queries do |
| CC-7 | Zero cost when unused: the hooks are null outside the query; no recording change, no new capture, no file format change |
| CC-8 | Cost bounded per link: a register source is searched back at most `max_frames` frames (default 50, max 1000); a link that stops says why (the session start, a barrier marker, the frame limit, a byte never written in the history, a write by a device) |
| CC-9 | Generic: rules per instruction class from the decoder table (`z80opinfo`), nothing per game or per program |
| CC-10 | The reply fits an agent: the MCP text is the chain as indented lines (one per link), the JSON holds the same with numbers |
| CC-11 | Measured against the old loop through WebAPI and MCP: calls, reply bytes, MCP text characters, wall time, as ratios. Accepted only if it is better |
| CC-12 | Tests about 10 ms each, integration under 1 s; long variants behind `UNREAL_LONG_TESTS=1` |

## Acceptance

- The synthetic programs of plan.md (S1-S10) give the expected chain, link by link, on Pentagon and 48K.
- A real recording (a game's lives counter or score digit, chosen by the coverage of one write site, no address known
  in advance) gives a chain that ends at a constant, a port read or the initial memory, never at "unknown".
- The one call against the old loop: fewer calls, fewer reply bytes and MCP text characters, not slower; numbers in
  plan.md.
- The recording rows of `frame_profile_benchmark` unchanged (no hot-path change: the hooks exist already).
- Zero warnings on clang, gcc, MSVC, MinGW; full core-tests; the live MCP script.
