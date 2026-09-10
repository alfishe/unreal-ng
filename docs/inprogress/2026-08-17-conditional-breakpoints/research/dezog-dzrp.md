# DeZog / DZRP — Protocol Research Report

Deep technical research for implementing DZRP ("DeZog Remote Protocol") support in unreal-ng, so DeZog can attach to it as a remote. Companion article: `../dezog-integration.md`.

Primary sources:
- DeZog repo: https://github.com/maziac/DeZog
- DZRP spec: https://github.com/maziac/DeZog/blob/main/design/DeZogProtocol.md
- Remote architecture: https://github.com/maziac/DeZog/blob/main/design/AddingNewRemotes.md
- Usage/capability matrix: https://github.com/maziac/DeZog/blob/main/documentation/Usage.md
- CSpect plugin (reference DZRP server, C#): https://github.com/maziac/DeZogPlugin
- ZX Next serial remote (reference DZRP server, Z80 asm): https://github.com/maziac/dezogif
- DeZog client-side DZRP implementation: `src/remotes/dzrp/dzrpremote.ts`
- Changelog (protocol churn): https://github.com/maziac/DeZog/blob/main/CHANGELOG.md

---

## 1. Architecture: how DeZog talks to remotes

DeZog is a VS Code Debug Adapter (DAP on the VS Code side). On the back side it abstracts targets behind a `Remote` class hierarchy:

```
RemoteBase
├── ZesaruxRemote            — ZEsarUX via ZRCP (text protocol, TCP :10000). Legacy; explicitly NOT recommended.
├── MameRemote               — MAME gdbstub over TCP (overrides the sendDzrpCmd* functions with gdb packets)
└── DzrpRemote
    ├── ZSimRemote           — internal simulator (no transport)
    └── DzrpQueuedRemote     — RECOMMENDED base: DZRP semantics + request queue
        ├── CSpectRemote     — DZRP binary over TCP socket (default port 11000)
        ├── ZxNextSerialRemote — DZRP binary over USB serial (UART)
        └── DzrpBufferRemote — shared buffered-transport framing
```

Key facts:

- **Recommended path for a new emulator: implement a DZRP binary server over a TCP socket in the emulator.** `AddingNewRemotes.md` says new remotes should derive from `DzrpQueuedRemote` and that only the transport differs; the CSpect plugin is exactly this — a TCP DZRP server inside the emulator process.
- **There is no generic `remoteType: "dzrp"` in launch.json.** Shipped types: `zsim`, `zrcp`, `cspect`, `zxnext`, `mame`. The pragmatic route: implement the same DZRP-over-TCP server the CSpect plugin implements, and have users configure `"remoteType": "cspect", "cspect": {"port": <yourport>, "hostname": ...}`. DeZog can't tell the difference — CMD_INIT even carries your program name string, which DeZog displays.
  - Caveat: the `cspect` remote may issue Next-specific commands (CMD_GET_TBBLUE_REG, sprite commands) when the user opens those views; respond with empty/zero data gracefully.
  - The "clean" long-term route is a small PR to DeZog adding an own `Remote` subclass / remoteType (TypeScript, mostly configuration + machine-type mapping) — relevant for the ATM/Evolution banking issue in §3.
- DZRP is **request–response**: DeZog sends a command, remote answers; the only remote-initiated traffic is **notifications** (seq = 0), currently just NTF_PAUSE.

## 2. DZRP protocol specification

Spec file: `design/DeZogProtocol.md`. Spec document version **2.1.0**; DeZog `main` announces `DZRP_VERSION = [2, 0, 0]` in `dzrpremote.ts` (2.1.0 = experimental addition of CMD_INTERRUPT_ON_OFF). Version is exchanged in CMD_INIT; compatibility is judged on the major version — mismatched major should produce an error.

### 2.1 Framing

All integers little-endian except the 3-byte version triplet in CMD_INIT (Major, Minor, Patch bytes).

**Command (DeZog → remote):**

| Offset | Size | Field |
|---|---|---|
| 0 | 4 | Length of the following data (SeqNo + CmdID + payload) |
| 4 | 1 | Sequence number, 1–255, wraps; incremented per command |
| 5 | 1 | Command ID |
| 6+ | n | Payload |

**Response (remote → DeZog):** `[4-byte length][1-byte seq (echoes the command's)][payload]`. **No command ID in responses** — correlation is purely by sequence number, and DZRP is strictly serialized (one outstanding command), so answer in order.

**Notification (remote → DeZog):** `[4-byte length][seq = 0][1-byte notification ID][payload]`.

### 2.2 Command table (IDs, payloads)

| ID | Command | Cmd payload | Response payload |
|---|---|---|---|
| 1 | CMD_INIT | ver Major,Minor,Patch (3B) + null-terminated client name string | error (1B, 0=ok) + ver (3B) + machine type (1B: 0 UNKNOWN, 1 ZX16K, 2 ZX48K, 3 ZX128K, 4 ZXNEXT) + null-terminated server name string |
| 2 | CMD_CLOSE | — | — (then close socket) |
| 3 | CMD_GET_REGISTERS | — | 29+ bytes: PC,SP,AF,BC,DE,HL,IX,IY,AF',BC',DE',HL' (each 2B LE), R, I, IM, reserved (1B each), Nslots (1B), then Nslots bank numbers (1B each) |
| 4 | CMD_SET_REGISTER | reg no (1B) + value (2B LE) | — |
| 5 | CMD_WRITE_BANK | bank no (1B) + bank contents (8K for Next) | error (1B) + null-term error string |
| 6 | CMD_CONTINUE | bp1 enable (1B), bp1 addr (2B), bp2 enable (1B), bp2 addr (2B), alternate cmd (1B: 0 none, 1 step-over, 2 step-out), range start (2B, incl.), range end (2B, excl.) | — (response sent immediately; the stop arrives later as NTF_PAUSE) |
| 7 | CMD_PAUSE | — | — (then NTF_PAUSE with reason 1) |
| 8 | CMD_READ_MEM | reserved (1B=0), start addr (2B), size (2B) | memory bytes |
| 9 | CMD_WRITE_MEM | reserved (1B=0), start addr (2B), data | — |
| 10 | CMD_SET_SLOT | slot (1B), bank (1B; 0xFE/0xFF = ROM on Next) | error (1B) |
| 11 | CMD_GET_TBBLUE_REG | reg (1B) | value (1B) — Next only; return 0 |
| 12 | CMD_SET_BORDER | color (1B) | — |
| 13 | CMD_SET_BREAKPOINTS | 3B per bp (addr 2B + bank+1) | overwritten memory bytes — **ZX Next only** (RST-patching scheme) |
| 14 | CMD_RESTORE_MEM | 4B tuples | — ZX Next only |
| 15 | CMD_LOOPBACK | up to 8192B echo test | same data — ZX Next serial only |
| 16–19 | sprite palette / clip window / sprites / patterns | — | Next-only; safe to stub |
| 20 | CMD_READ_PORT | port (2B) | value (1B) |
| 21 | CMD_WRITE_PORT | port (2B) + value (1B) | — |
| 22 | CMD_EXEC_ASM | context (1B) + machine code | error (1B: 0 ok, 1 too long, 2 not stopped) + AF,BC,DE,HL (2B each) — remote executes the snippet with RET appended |
| 23 | CMD_INTERRUPT_ON_OFF | 0/1 (1B) | — (DZRP 2.1.0, DeZog ≥3.7.0, used with CSpect ≥3.0.15.2) |
| 40 | CMD_ADD_BREAKPOINT | addr (2B) + bank+1 (1B) + null-terminated condition string | breakpoint ID (2B LE; 0 = could not set) |
| 41 | CMD_REMOVE_BREAKPOINT | breakpoint ID (2B) | — |
| 42 | CMD_ADD_WATCHPOINT | start addr (2B) + bank+1 (1B) + size (2B) + access (1B: bit0 read, bit1 write) | status (1B, 0=ok) |
| 43 | CMD_REMOVE_WATCHPOINT | same 6B layout | — |
| 50 | CMD_READ_STATE | — | opaque state blob (empty = failure) |
| 51 | CMD_WRITE_STATE | blob from CMD_READ_STATE | — |

Register numbers for CMD_SET_REGISTER: 0 PC, 1 SP, 2 AF, 3 BC, 4 DE, 5 HL, 6 IX, 7 IY, 8 AF', 9 BC', 10 DE', 11 HL', 13 IM, 14 F, 15 A, 16 C, 17 B, 18 E, 19 D, 20 L, 21 H, 22 IXL, 23 IXH, 24 IYL, 25 IYH, 26 F', 27 A', 28 C', 29 B', 30 E', 31 D', 32 L', 33 H', 34 R, 35 I.

### 2.3 NTF_PAUSE (the only notification)

Sent whenever execution halts (breakpoint, watchpoint, CMD_PAUSE, step complete). Layout (total length = 6+n):

| Index | Size | Value |
|---|---|---|
| 0 | 1 | 0 (instead of seq no) |
| 1 | 1 | 1 = NTF_PAUSE |
| 2 | 1 | Break reason: 0 no reason (e.g. step finished), 1 manual break, 2 breakpoint hit, 3 watchpoint read, 4 watchpoint write, 255 other |
| 3 | 2 | Breakpoint/watchpoint address (LE) |
| 5 | 1 | bank+1 of that address (0 = plain 64K address) |
| 6 | n | Null-terminated reason string (shown to user; can be empty = single 0x00) |

## 3. Memory model, slots/banks, long addresses

- **Slots**: the remote declares its current paging in every CMD_GET_REGISTERS reply — `Nslots` followed by the bank number mapped into each slot. Slot geometry is implied by the machine type from CMD_INIT: ZX128K → 4×16K slots (banks: ROM0/ROM1 + RAM 0–7), ZXNEXT → 8×8K slots (banks 0–223, 0xFE/0xFF ROM). DeZog instantiates a matching `MemoryModel` (`MemoryModelZx128k`, `MemoryModelZxNextOneROM`, etc.) from the machine-type byte; UNKNOWN → flat 64K.
- **Long addresses**: internally DeZog uses `addr + ((bank+1) << 16)`; on the wire a long address is always `addr(2B) + (bank+1)(1B)`, where **bank byte 0 means "plain 64K address, ignore banking"**. Applies to breakpoints, watchpoints, and NTF_PAUSE. With long addresses, a breakpoint fires only when PC=addr **and** that bank is currently paged into the slot containing addr — the remote must check both.
- **ATM Turbo 2+ / ZX Evolution problem (256 RAM pages)**: two hard constraints.
  1. The bank field is **one byte carrying bank+1**, so the max representable bank number is 254. 256 RAM pages do not fit.
  2. DZRP's machine-type byte only knows 16K/48K/128K/ZXNEXT — no way to declare a custom slot/bank layout over the wire (custom memory models exist only for zsim via `customMemory` in launch.json).
  Practical options: (a) report machine type **ZX128K** with 4×16K slots when in a 128K-compatible config, plain 64K breakpoints — fully functional, not bank-aware; (b) for real ATM bank-aware debugging, contribute a DeZog-side change (new machine type or custom-memory-model handshake) — DeZog's `MemoryModel` machinery already supports arbitrary slot layouts internally; the DZRP init handshake is the bottleneck. maziac is responsive in Discussions.

## 4. Breakpoints, conditions, watchpoints, assertions, logpoints — division of labor

**The remote's job is small.**

- **Plain PC breakpoints**: remote implements CMD_ADD/REMOVE_BREAKPOINT, checks PC (+bank) each instruction, sends NTF_PAUSE reason 2.
- **Conditional breakpoints**: the condition string travels in CMD_ADD_BREAKPOINT, but for every remote except zsim/ZEsarUX **DeZog evaluates conditions itself**: on NTF_PAUSE, `evalBpConditionAndLog()` evaluates the expression via `Utility.evalExpression()` (reading regs/memory over DZRP), and if false silently sends CMD_CONTINUE again. The remote **may ignore the condition string entirely** ("slow" mode per Usage.md; "fast" = remote-side evaluation is an optional optimization). Usage.md: "Conditions, ASSERTION and LOGPOINT are evaluated in DeZog, not by the Remote."
- **ASSERTION** (`; ASSERTION expr` in source): DeZog translates it to a breakpoint with inverted condition — nothing extra for the remote.
- **LOGPOINT**: a breakpoint DeZog resolves on NTF_PAUSE by printing the message and auto-continuing — nothing extra for the remote.
- **Watchpoints (WPMEM)**: DO require remote support — CMD_ADD/REMOVE_WATCHPOINT with address range + read/write mask, NTF_PAUSE reasons 3/4 on hit. CSpect and ZX Next remotes don't support them at all, so DeZog tolerates their absence; implementing them puts unreal-ng ahead of CSpect.
- **Stepping**: there are no step commands. StepInto/Over/Out are all CMD_CONTINUE with the two temporary breakpoints + "alternate command" byte + step-over range (DeZog computes candidate next-PC addresses from disassembly). The remote must honor: temp bp1/bp2, and, for alternate cmd 1/2, its own step-over (run until PC leaves [rangeStart,rangeEnd) at same-or-lower call depth — the CSpect plugin's simpler take: repeat single-steps until out of range) and step-out (run until RET executed / SP above start). Simplest correct v1: implement only the two temp breakpoints and treat alternate commands via naive single-stepping.

## 5. Extra features — what can be skipped

| Feature | Remote must supply | Skippable? |
|---|---|---|
| Reverse debugging "lite" | Nothing — DeZog records reg/stack snapshots itself at each step/pause | Free |
| Reverse debugging "true" | Instruction-level CPU history from the remote (only zsim & ZEsarUX; not part of DZRP) | Skip |
| Save/restore state | CMD_READ_STATE / CMD_WRITE_STATE, opaque blob (own snapshot format) | Optional; easy win since unreal-ng has snapshots |
| Z80 unit tests (z80-unit-tests) | Works over the normal command set (solid breakpoints + CMD_WRITE_MEM / registers; WPMEM optional) | Comes ~free once core works |
| Sprites/TBBlue (cmds 11, 16–19) | Next-only | Stub with zeros/empty |
| CMD_EXEC_ASM, CMD_READ/WRITE_PORT | Port I/O is easy and useful; EXEC_ASM optional | Ports: yes; EXEC_ASM: skip initially |
| Custom peripherals (zsim custom JS) | zsim-only feature | Skip |
| CMD_WRITE_BANK / CMD_SET_SLOT | Used for loading .nex files and slot manipulation | SET_SLOT trivial; WRITE_BANK optional |

History caveat from Usage.md: lite history stores only registers + stack; memory views are not rewound.

## 6. Reference implementations of the remote side

1. **CSpect DeZog plugin** (C#, maziac/DeZogPlugin; continued in the community CSpectPlugins repo) — **the best template**: a TCP DZRP v2.0.0 server (~a dozen source files: `Server.cs`, `Commands.cs`, `CSpectSocket.cs`). Shows exact framing, command dispatch, and stepping mapped onto an emulator API. Notably doesn't implement watchpoints or condition evaluation — proof of how small a viable server is.
2. **dezogif** (Z80 asm, maziac/dezogif + its `Design.md`) — DZRP over UART on real ZX Next hardware; implements breakpoints by patching memory (hence CMD_SET_BREAKPOINTS/CMD_RESTORE_MEM 13/14, which exist *only* for this remote — an emulator should use 40/41 instead). Useful for NTF_PAUSE flows and the loopback test.
3. **MameRemote** shows even a gdbstub can back DeZog — but DZRP-native is strictly better (MAME path lacks banking; DeZog discussion #127).

## 7. Practical notes, pitfalls, MVP checklist

Pitfalls:

- **Protocol churn**: DZRP changed rapidly through 1.x (command renumbering at 1.2.0 and 1.4.0!) but has been stable at 2.0.x since DeZog ~2.x; 2.1.0 only adds CMD_INTERRUPT_ON_OFF. Pin against 2.0.0 and check the major version in CMD_INIT; reject mismatches with error byte 1.
- **Serialized protocol**: never send a response out of order; only NTF_PAUSE may interleave. CMD_CONTINUE must be answered *immediately* (ack), with the stop reported later via NTF_PAUSE — a classic first-implementation bug is blocking the response until the target stops.
- **Seq 0 is reserved** for notifications; command seq runs 1–255 and wraps to 1.
- **Bank+1 encoding**: off-by-one errors silently break bank-aware breakpoints; bank byte 0 = "64K address" must match any bank.
- DeZog will hammer CMD_GET_REGISTERS / CMD_READ_MEM after every stop (variables, disassembly, memory views) — make reads fast and side-effect-free (don't tick contention/FDC on debug reads).
- The emulator should keep running its UI/video while "paused" for DeZog — pause means CPU halted, socket alive.
- Test tip: dezogif's CMD_LOOPBACK idea, plus DeZog logs raw DZRP traffic when transport logging is enabled in launch.json — invaluable for byte-level debugging.

**Minimum viable remote (first working session under `remoteType: "cspect"`):**

1. TCP server (one client), 4-byte-length framing, seq echo.
2. CMD_INIT (1) — version 2.0.0, machine type 3 (ZX128K) or 2 (ZX48K), name string "unreal-ng x.y".
3. CMD_CLOSE (2).
4. CMD_GET_REGISTERS (3) — incl. slot list matching the declared machine type.
5. CMD_SET_REGISTER (4).
6. CMD_READ_MEM (8) / CMD_WRITE_MEM (9).
7. CMD_ADD_BREAKPOINT (40) / CMD_REMOVE_BREAKPOINT (41) — PC (+bank) match, unique 16-bit IDs starting at 1; condition string may be ignored.
8. CMD_CONTINUE (6) — temp bp1/bp2; naive single-step fallback for alternate cmds 1/2 acceptable initially.
9. CMD_PAUSE (7).
10. NTF_PAUSE — reasons 0/1/2 with address+bank and string.
11. Stubs returning zero/empty for 5, 10, 11, 12, 16–19, 22, 23 (never crash on unknown commands — log and return an empty response).

Phase 2: watchpoints (42/43, reasons 3/4), CMD_READ/WRITE_PORT (20/21), CMD_SET_SLOT (10), CMD_READ/WRITE_STATE (50/51) wired to snapshot code, real step-over/step-out semantics. Phase 3: DeZog-side PR for an ATM/Evolution memory model (the 1-byte bank+1 field caps banks at 254 — the one hard wire-format conflict with 256-page machines).

## Sources

[DZRP spec](https://github.com/maziac/DeZog/blob/main/design/DeZogProtocol.md) · [AddingNewRemotes](https://github.com/maziac/DeZog/blob/main/design/AddingNewRemotes.md) · [Usage.md](https://github.com/maziac/DeZog/blob/main/documentation/Usage.md) · [CHANGELOG](https://github.com/maziac/DeZog/blob/main/CHANGELOG.md) · [DeZogPlugin](https://github.com/maziac/DeZogPlugin/blob/main/Readme.md) · [dezogif](https://github.com/maziac/dezogif) · [MAME gdbstub limitations discussion](https://github.com/maziac/DeZog/discussions/127) · `src/remotes/dzrp/dzrpremote.ts`
