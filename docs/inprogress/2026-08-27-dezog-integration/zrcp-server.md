# ZEsarUX ZRCP Server (Wire Reference)

> **Module:** `core/automation/zesarux/` — a ZEsarUX-impersonating text-protocol
> server that lets DeZog connect with `remoteType: "zrcp"`.
> Wire behavior verified against DeZog `src/remotes/zesarux/*` and ZEsarUX
> `src/zrcp/remote.c` + `debug.c` (see `scratch/`).
>
> **Status: validated end-to-end against the installed DeZog 3.7.4** — full
> attach/init, registers, disassembly, breakpoints (conditions, pass count),
> watchpoints, stepping, memory views and reverse debugging work. A ready-made
> VS Code project lives in [test-project/](test-project/).

## Why

DeZog gates its ZEsarUX-specific features behind the remote type: the reverse
debugging UI (`ZesaruxCpuHistory`) is only instantiated for `remoteType:
"zrcp"`/`"zsim"`, and `CSpectRemote` (our DZRP path) client-side throws on
watchpoints, state and batch breakpoints. Serving the ZRCP text protocol next
to our DZRP binary server gives DeZog the full feature set over the same
emulator, and both transports stay switchable from `launch.json`.

## Design Decisions

1. **Reuse, don't duplicate.** `zrcp::Server` consumes the existing
   `dzrp::IDebugInterface` (`DezogDebugAdapter`). `AutomationZesarux` owns a
   *second* adapter instance plus the ZRCP server; module `automation_zesarux`
   links `automation_dezog`. Both servers may listen concurrently (10000 ZRCP +
   12000 DZRP); only one DeZog session should own breakpoints at a time (each
   adapter keeps independent session/bp state).
2. **Additive capability virtuals** on `dzrp::IDebugInterface` (safe defaults,
   implemented in `DezogDebugAdapter`):
   - `stepOnce()` → `Emulator::RunSingleCPUCycle(false)` (paused-only; the same
     call the CLI step-in uses)
   - `disassembleInstruction(addr, lenOut)` → `DebugManager` disassembler over
     4 bytes read at `addr`
   - `getTStates()` / `getCpuFrequencyHz()` (defaults 0)
3. **Server = single-threaded line session.** Commands serialize naturally.
   Blocking `run`: emit the `Running until...` line first → `resume()` → wait
   on a condvar (signaled by the adapter PauseNotifier) *or* a 5 ms
   socket-poll → client data (blank line) ⇒ `pause()`, wait for the MANUAL
   stop, respond **without** `Breakpoint fired`. On a bp/watchpoint stop:
   evaluate the condition → false ⇒ silently re-resume; true ⇒ the
   `Breakpoint fired: ...` line.
4. **Session lifecycle.** First `enter-cpu-step` → `waitForTarget()` +
   `onSessionOpened()` (starts TTD recording — this is what makes history
   browsable). Disconnect / `quit` → `onSessionClosed()`.
5. **Breakpoint conditions** are parsed/evaluated server-side (dialect below).
   The `PC=` literal is extracted to install the actual breakpoint; the rest is
   evaluated at every hit. Unparseable / `value=`-style conditions conservatively
   stop (and log) — never swallow a user breakpoint.

## Transport

- TCP, default port **10000** (`UNREAL_ZRCP_PORT` env override).
- Commands are one `\n`-terminated line.
- **Every** response ends with the prompt `command...> ` (no trailing newline).
  DeZog detects it via `startsWith('command') && endsWith('> ')` — **and only
  accepts the response when the chunk before the prompt splits into ≥ 2 lines**.
  Consequences enforced centrally in `sendPrompt` (flag `m_answerHasData`, set
  by `sendLine` only):
  - an answer with no data line (e.g. the `close-all-menus` ack) is prefixed
    with an empty `\n` so the prompt never lands on split index 0;
  - `log> ` lines do **not** count as data — DeZog strips them *before* the
    prompt check, so a log-only answer (e.g. `hard-reset-cpu`) still needs the
    leading `\n`. `sendLog` therefore sends via `sendRaw`, not `sendLine`.
- On connect: welcome banner lines + prompt.
- Response lines starting with `error` (first 5 chars, case-insensitive) only
  warn client-side; `log> `-prefixed lines are forwarded to DeZog's output
  (used for diagnostics and no-op acks like `load`).
- A bare `\n` interrupts a running `run` (DeZog's pause).
- `read-memory` serves up to the whole address space in one request — DeZog's
  disassembly view fetches 64 KiB at once (`read-memory 0 65536`) and asserts
  the answer is exactly `len*2` hex chars (internally read in `uint16_t`
  chunks; lengths above 65536 answer `Error. Invalid length`).

## The Register Line (byte-exact `print_registers`)

```
PC=%04x SP=%04x AF=%04x BC=%04x HL=%04x DE=%04x IX=%04x IY=%04x AF'=%04x BC'=%04x HL'=%04x DE'=%04x I=%02x R=%02x␣␣F=<8ch> F'=<8ch> MEMPTR=0000 IM%d IFF-- VPS: 50 MMU=<8×%04x contiguous>
```

- Lowercase hex; **two** spaces after `R=`; flags in `SZ5H3PNC` order with `-`
  when clear; `MEMPTR` always `0000` (client ignores); `IFF--` placeholder.
- Field order is load-bearing: DeZog memoizes `indexOf` offsets per line shape.
- **MMU**: first 4 words = the 16 KB slots (`0x8000|romPage` for slot 0 when
  ROM0/ROM1 is paged in, else the RAM bank number); groups 5–8 repeat them.
  DeZog parses only the first 4 and maps `>= 0x8000` to ROM bank `8+(v&1)`.

Example (128K, ROM1 in slot 0):

```
PC=8000 SP=ff00 AF=1234 BC=beef HL=dead DE=cafe IX=1111 IY=2222 AF'=3333 BC'=4444 HL'=5555 DE'=6666 I=3f R=21  F=--5H-P-- F'=--5H--NC MEMPTR=0000 IM1 IFF-- VPS: 50 MMU=80010005000200008001000500020000
```

## Commands

### Connect / init (the exact order DeZog sends)

| Command | Response |
|---------|----------|
| `close-all-menus` | empty |
| `about` | free text (name + version) |
| `get-version` | `12.1` (≥ DeZog MIN 10.3; 12.1 enables bp pass counts) |
| `set-debug-settings <n>` | empty (stored; **bit 0** = include regs in step/run output; DeZog sends 0 or 32) |
| `hard-reset-cpu` | `log> ` ack (no-op + log) |
| `enter-cpu-step` | empty (first call opens the session + TTD recording) |
| `load <path>` / `smartload <path>` | `log> ` ack (loading stays in unreal-ng) |
| `get-current-machine` | `ZX Spectrum 128K` / `ZX Spectrum 48K` (client matches `includes("128k"/"48k")` case-insensitively; all banked models incl. Pentagon report 128K — documented approximation) |
| `clear-membreakpoints` | empty |
| `enable-breakpoints` / `disable-breakpoints` | empty |
| `disable-breakpoint <id>` / `enable-breakpoint <id>` | empty (unknown ids tolerated — DeZog clears its whole 100-slot table on connect) |
| `cpu-code-coverage enabled no` / `get` / `enabled get` | empty |
| `extended-stack enabled <y/n>` | empty |
| `exit-cpu-step` | empty |

### Execution

| Command | Response |
|---------|----------|
| `run` | `Running until a breakpoint, key press or data sent, menu opening or other event` **immediately**, then blocks until a stop (see Design 3); ends with the step output |
| `run return` | temp bp at word(SP), then the same loop |
| `cpu-step` | optional regs line (settings bit 0, ` TSTATES: <n>` suffix) + **one** disassembly line |
| `cpu-step-over` | CALL/RST ⇒ temp bp at PC+len (same one-line output, no banner/break echo); else a single step |
| — wrong state | `Error. You must first enter cpu-step mode` |

Disassembly line format: `%04X %X ` (7 chars: address, space, zone digit =
RAM bank / ROM page visible at that address, space) + uppercased mnemonic.
DeZog slices `substring(7, 7+4)` for CALL/RST detection.

### Breakpoints (ids 1..100; 100 = DeZog's step-out temp slot)

| Command | Notes |
|---------|-------|
| `set-breakpointaction <id>` | empty ack |
| `set-breakpoint <id> <cond>` | e.g. `PC=08000h and RAM=5`; the `PC=` literal installs the bp. A plain numeric condition (DeZog's removal idiom, e.g. `set-breakpoint 1 0`) never fires — the entry is silently dropped |
| `set-breakpointpasscount <id> <n>` | skips the first n−1 hits (version ≥ 12.1 feature) |
| `enable/disable-breakpoint <id>` | install/uninstall the emulator bp |

Stop message: `Breakpoint fired: PC=XXXXH` (condition-false hits are silently
resumed; manual interrupts print no break message).

### Watchpoints

`set-membreakpoint <hexaddr>h <type> <size>` — type 1=R, 2=W, 3=RW, **0=remove**;
maps to adapter watchpoints (bank 0 = any). Fires with
`Breakpoint fired: Memory Breakpoint Read/Write Address: %04XH`.

### Memory

`read-memory <addrDec> <lenDec>` → contiguous **uppercase** hex;
`write-memory-raw <addrDec> <hex>` → empty.

### cpu-history (index 0 = most recent — same semantics as our DZRP history)

| Command | Response |
|---------|----------|
| `enabled/set-max-size/clear/started/ignrephalt/ignrepldxr <val>` | empty |
| `is-enabled` / `is-started` | `0`/`1` |
| `get-size` / `get-max-size` | decimal |
| `get <n>` | entry line (below); out of range → `ERROR: index out of range` |

Entry line: the register prefix, then `IM%d IFF-- (PC)=<8 lowercase hex: 4
opcode bytes at PC in memory order> (SP)=<4 hex: word at SP, hi byte first>
MMU=<32 contiguous hex>` + a **trailing space**. No `F=`/`MEMPTR`/`VPS` in
this variant. Backed by `getHistoryEntry` (TTD frame cache; see
[reverse-debugging.md](reverse-debugging.md)).

### extended-stack

`extended-stack get <n>` → n lines `%04XH <type>` read upward from SP; type =
`call`/`rst` when the opcode at (value−3) == `0xCD` / (value−1) ∈ RST set,
else `push`. Not enabled → `Error. It's not enabled`.

### Misc

| Command | Response |
|---------|----------|
| `set-register <NAME>=<dec>` | maps to `dzrp::RegisterId` (AF', IM supported; IFF1/2 → ack + ignore) |
| `get-memory-pages` | per 16 KB slot: `R` + `O|A` + digit, e.g. `RO1 RA5 RA2 RA0 ` (**trailing space**) |
| `get-tstates-partial` / `reset-tstates-partial` / `get-cpu-frequency` | plain integers |
| unknown command | `Unknown command` (session survives) |

### Quit sequence (must all succeed)

```
(blank)
cpu-history enabled no
cpu-code-coverage enabled no
extended-stack enabled no
clear-membreakpoints
disable-breakpoints
exit-cpu-step
quit
```

## Condition Dialect (server-side evaluator)

```
expr  := or
or    := and (OR and)*
and   := not (AND not)*
not   := NOT not | cmp
cmp   := sum (= | <> | != | < | > | <= | >=) sum | sum
sum   := term ((+|-) term)*
term  := factor ((*|/) factor)*
factor:= '(' expr ')' | number | name | PEEK '(' expr ')' | PEEKB '(' expr ')' | PEEKW '(' expr ')' | ('-'|'+') factor
number:= hexDigits [h|H] | 0x hexDigits | decimal
```

- Names: 16-bit registers (`PC SP AF BC HL DE IX IY` + primed), 8-bit
  (`A F B C D E H L IXH IXL IYH IYL`), `I R IM`, and the banking terms
  `ROM=<n>` / `RAM=<bank>` evaluated **at the breakpoint address** (slot =
  `addr>>14`; ROM = dzrp bank ≥ 8).
- `PEEKW` is little-endian. Keywords and names are case-insensitive.
- Parse failures stop conservatively (breakpoint honored + log) — DeZog shapes
  we deliberately do not model (`value=`, `(value)=`) fall into this bucket.

## DeZog launch.json

A fully commented, working configuration (plus helper `tasks.json` that
launches unreal-qt, creates an emulator instance and loads a demo snapshot
via the WebAPI) is maintained in [test-project/.vscode/](test-project/.vscode/).
Minimal form:

```json
{
    "type": "dezog",
    "request": "launch",
    "name": "Unreal-NG Debug (ZRCP)",
    "remoteType": "zrcp",
    "zrcp": {
        "hostname": "localhost",
        "port": 10000
    },
    "rootFolder": "${workspaceFolder}",
    "startAutomatically": false
}
```

## Live Validation (DeZog 3.7.4)

Attaching real DeZog surfaced three wire-level incompatibilities that the
scripted clients had modeled away (each captured as a regression test + a
verifier step afterwards):

1. **Prompt framing for data-less answers** — DeZog's `receiveSocket` accepts a
   response only when the chunk splits into ≥ 2 lines with the prompt last;
   bare-prompt answers to `close-all-menus`-style acks never completed
   ("ZEsarUX did not answer in time!"). Fixed by the `m_answerHasData` prefix
   rule (see Transport).
2. **Log lines before the prompt check** — DeZog strips `log> ` lines *first*;
   log-only answers (`hard-reset-cpu`, `load`) therefore complete with an empty
   result. `sendLog` must not mark the answer as having data. The test clients
   replicate DeZog's pipeline *in order* (strip logs → check `\n`+prompt).
3. **Full-64K `read-memory`** — the disassembly view fetches 64 KiB in one
   command and asserts `len*2` hex chars; a `uint16_t` length cast turned
   65536 into 0 and crashed DeZog's `readMemoryDump` (`Error: 'assert'`).
   Fixed by chunked reads (see Transport).

## Testing

- **GTest** `core/tests/automation/zesarux/`: condition truth tables, byte-exact
  wire goldens (register/history/MMU/pages lines), scripted full sessions
  (init incl. 100 disables, conditions, pass counts, watchpoints, interruptable
  run, full-64K read, quit + reconnect, lifecycle). Run:
  `./cmake-build-release/bin/core-tests --gtest_filter='Zesarux*:*Zrcp*:AutomationZesarux*'`
- **Python verifier** `tools/verification/zesarux/` (19-step end-to-end run
  against any running host with a live emulator, DeZog-shaped client): see its
  README.
- **DeZog 3.7.4 attach**: use the [test-project/](test-project/) — task
  `launch unreal-ng + load demo`, then F5.

## Documented Limitations

- `cpu-code-coverage get` returns empty; `MEMPTR` is always 0000;
  `hard-reset-cpu` is a no-op; DeZog-side file loading is ack-only.
- Clone models (Pentagon/Scorpion/ATM/Profi) are reported as
  `ZX Spectrum 128K` (standard-paging approximation).
- `value=`/`(value)=` conditions are unsupported (stop conservatively).
- Register line history entries expose the MMU **at the recorded instruction**
  (TTD snapshot), not the current paging.
