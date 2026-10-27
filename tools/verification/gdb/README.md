# GDB Protocol Verification

Live checks of the GDB Remote Serial Protocol server (`core/automation/gdb/src/gdbserver.cpp`) against a real
emulator instance: a raw-RSP suite that covers every packet the dispatcher handles, and a session script that drives a
real gdb.

| File | What |
|------|------|
| `rsp_client.py` | RSP client (framing, acks / no-ack, binary escaping, RLE, `O` output, 0x03), an agent-expression assembler (`AgentExpr`, opcodes of gdb's `gdbsupport/ax.def`), the host launcher and the WebAPI calls the scripts need |
| `verify_gdb_protocol.py` | Raw-RSP suite: every packet, with assertions on the values |
| `verify_gdb_session.py` | gdb 17.2 in batch mode: connect, registers, memory, conditional breakpoints, watchpoints, reverse-continue, detach |

Both are stdlib-only Python 3.

## Usage

```bash
# Start unreal-qt offscreen on free ports of its own (UNREAL_WEBAPI_PORT / _GDB_PORT / ...), create a muted PENTAGON
# through the WebAPI, verify, stop it
python3 tools/verification/gdb/verify_gdb_protocol.py \
    --launch cmake-build-agent-release/bin/unreal-qt.app/Contents/MacOS/unreal-qt
python3 tools/verification/gdb/verify_gdb_session.py \
    --launch cmake-build-agent-release/bin/unreal-qt.app/Contents/MacOS/unreal-qt

# Against a running host with one emulator instance (GDB port 2000 by default). The WebAPI port enables the
# cross-checks (the core breakpoint list, run state) and the recovery of a run the server can not interrupt
python3 tools/verification/gdb/verify_gdb_protocol.py --port 2000 --webapi-port 8090

# Options
#   -v              print every packet (protocol) / the gdb output (session)
#   -k <text>       protocol: run the groups whose title contains the text
#   --model MODEL   model of the launched instance (PENTAGON)
#   --port-base N   WebAPI port of the launched host (GDB = N+3); default: the first free range from 21000
#   --gdb PATH      session: the gdb binary (/opt/homebrew/bin/gdb)
```

Exit code 0: every check passed. The host log and the gdb transcripts (`gdb-<scenario>.gdb/.out`, with the
`set debug remote 1` packet log) go to `scratch/verification-gdb/`.

The scripts install a Z80 loop at 0x8000 (arrival k at 0x8005 has `A = HL = k`, `(9000h) = k`; it writes 9000h and
reads 9001h each round), so every hit is deterministic. The protocol suite saves the registers, the program and data
memory and the paging latch first and restores them at the end.

The launched host starts with the debug mode and the `breakpoints` feature off, as unreal-qt does. The protocol suite
first checks that gdb's breakpoints work that way, then turns both on through the WebAPI for the rest (the session
script turns them on at once). A WebAPI call that changes the machine opens an implicit run-control session that
refuses gdb's resume; the scripts release it after each call (`DELETE /control/session`).

## What the protocol suite covers

| Group | Packets and checks |
|-------|--------------------|
| ack mode / framing | `+` after every packet, `-` and no reply for a bad checksum, retransmission, `QStartNoAckMode`, no `+` afterwards |
| handshake | `qSupported` features (`PacketSize=4000`, `QStartNoAckMode+`, `qXfer:features/osdata/threads:read+`, `swbreak+`, `hwbreak+`, `vContSupported+`, `multiprocess-`, `ConditionalBreakpoints+`, `ReverseStep+`/`ReverseContinue+`), `vMustReplyEmpty` |
| unsupported packets | `$#00` for unknown `q`/`Q`/`v`/`b`/`Z5`/`!`/`vCont;t`/unknown `qXfer` annexes |
| stop reason / threads / queries | `?`, `qC`, `qfThreadInfo`/`qsThreadInfo`, `qAttached`, `qThreadExtraInfo`, `H`, `T`, `qXfer:threads:read`, `qSymbol`, `qOffsets`, trace packets (`qTStatus`, `qTfV`, ...), `qL`, `qP`, the empty `qXfer` objects (exec-file, auxv, libraries, memory-map, spu, siginfo) |
| qXfer target.xml / osdata | the XML (architecture, 18 registers in order with sizes and types), reads at an offset and past the end, `osdata:processes` (pid, instance id, state) |
| registers | `g` (15 registers) against `p` for each, `p` of the paging registers, `P`/`p` round trip af..hl', `iff`, `im`, `ir` with R's bit 7 kept apart across an M1 cycle, `G` round trip, malformed `p`/`P`/`G` (`E01`/`E02`), `pfe` and `p7ffd` writes (bankinfo follows), the WebAPI register view |
| memory | `M`/`m` round trip, length 0, 0xFFFF wrap, malformed `m`/`M` (`E01`), the physical view `0x01PPAAAA` (read and write), a missing page, `X` with escaped binary bytes, `M`/`P`/`G` while running (`E0D`) |
| debug mode | `Z0` / `Z2` stop a host as it starts (debug mode and the breakpoints feature off) |
| breakpoints | `Z0`/`z0` with the core list (owner gdb), `c` to three hits (`T05swbreak:`, PC, HL, A), `s`, `S05`, `vCont;s:1`, `vCont?`, `vCont;c`, `vCont;c:01`, `C05`, `vCont;S05`, `vCont;C05`, `Z1` (`hwbreak:`), malformed `Z`/`z` |
| watchpoints | `Z2` write (`watch:9000`), `Z3` read (`rwatch:9001`), `Z4` access on a read and on a written byte (`awatch:`), a 2-byte range; the PC at the stop |
| other clients' breakpoints | `z0` / `z2` at an address holding a WebAPI user breakpoint keep it; `Z2` at an address holding a user's execute breakpoint still fires |
| interrupt 0x03 | a lone 0x03 while paused, 0x03 followed by a packet in one write, 0x03 while a `c` runs; an external pause during `c` |
| conditional breakpoints | `Z0,addr,1;X len,bytes`: HL == 3 (stop value, core condition text), re-`Z0` replaces the condition, `$a == 9` as gdb compiles it, a memory byte (`ref8`), two expressions (OR), `if_goto` paths (`||`), `less_signed`/`log_not`/`bit_and`, a false-forever condition (no stop, core running), untranslatable ones (backward jump, `trace_quick`, `p1ffd`, division by a register) and a malformed list fall back to unconditional, a plain `Z0` replaces a conditional one, `z0` removes it |
| monitor (`qRcmd`) | `help`, `model`, `status`, `instances`, `frame`, `bankinfo`, `regions`, `mem`, `load` (errors), unknown command, `bport in/out/clear` (with the core list; ids of 10 and more), `ttd findlast/seek` errors, `reset`/`load`/`ttd start` refused while running |
| reverse (TTD) | `bs`/`bc` without a session (`E01`), `monitor ttd start/status/stop`, `bs` (one instruction back), `bc` (the previous breakpoint hit), `P`/`M`/`G` in the past (`E0D`), `monitor ttd findlast`, `bc` to the start (`replaylog:begin`), `c` forward from the past, back to live |
| run control | `c` while a WebAPI client holds an explicit control session |
| attach / detach / kill | `vAttach` (`E01`, `E31`, the stop reply, run control claimed), `D` (`OK`, the connection closes, claim released), `k` (the emulator survives) |

## What the session script covers

gdb 17.2 (`set architecture z80`, `target remote`): `info registers` (the 18 registers), `x/8xb`, `break *0x8005 if
$hl == 3` (the `Z0` carries `;X`, one stop reaches gdb, `$hl == 3`), `info breakpoints`, `break ... if ($af >> 8) == 7`,
`tbreak`, `monitor status`, `detach`; `watch` and `rwatch` (gdb must report the watchpoint, not resume forever);
`monitor ttd start` + `reverse-continue` + `continue`. gdb's Z80 target has no `$a`: use `($af >> 8)`.

## Known Issues

### GDB 17.2 Z80 Frame Unwinding Bug

GDB 17.2 has a bug in Z80 frame unwinding that aborts gdb:

```
../../gdb/trad-frame.h:144: internal-error: addr: Assertion `m_kind == trad_frame_saved_reg_kind::ADDR' failed.
```

It fires on `bt`, `stepi`, `step`, `finish` and on any memory or register write from gdb (`set {char}0x9100 = 1`,
`set $hl = 0`: gdb unwinds before it writes, so the packet is never sent). The session script avoids them; the raw
suite covers `M`, `X`, `P`, `G` and `s`.

**Workarounds:**
- Use GDB 15.x or earlier
- Use IDA Pro's GDB debugger
- Avoid backtrace operations

This is a GDB bug, not an issue with this server implementation.

## Protocol Reference

- [GDB Remote Serial Protocol](https://sourceware.org/gdb/onlinedocs/gdb/Remote-Protocol.html)
- [Stop Reply Packets](https://sourceware.org/gdb/onlinedocs/gdb/Stop-Reply-Packets.html)
- [Agent Expressions](https://sourceware.org/gdb/onlinedocs/gdb/Agent-Expressions.html) (bytecodes: gdb's
  `gdbsupport/ax.def`)
- Stop-reply forms and error codes of this server: [gdb-reverse-debugging-tdd.md
  §4.7](../../../docs/emulator/design/debugger/time-travel-debug/gdb-reverse-debugging-tdd.md)
