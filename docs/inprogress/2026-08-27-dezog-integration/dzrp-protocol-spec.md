# DZRP Protocol Specification (v2.2.0)

> **Source:** This document is derived from the official DeZog protocol specification.
> See [Authoritative References](#authoritative-references) for links to upstream documentation.
>
> **DZRP 2.2.0 changes from 2.0.0:**
> - Sequence number range changed from 1-255 to **1-15** (4 bits)
> - **NAK bit** (bit 7) added to response sequence byte for unsupported commands
> - **CMD_GET_SUPPORTED_COMMANDS (24)** added to query capabilities

## Message Format

### Command (DeZog → Emulator)

```
┌─────────────┬─────────────┬─────────────┬─────────────────────┐
│ Length (4B) │ SeqNo (1B)  │ CmdID (1B)  │ Payload (variable)  │
│ little-end  │ bits 0-3    │             │                     │
└─────────────┴─────────────┴─────────────┴─────────────────────┘
```

| Offset | Size | Description |
|--------|------|-------------|
| 0 | 4 | Payload length (little-endian) |
| 4 | 1 | Sequence number (1-15, bits 0-3) |
| 5 | 1 | Command ID |
| 6+ | N | Payload data |

### Response (Emulator → DeZog)

```
┌─────────────┬─────────────┬─────────────────────┐
│ Length (4B) │ SeqNo (1B)  │ Payload (variable)  │
│ little-end  │ bit7=NAK    │                     │
└─────────────┴─────────────┴─────────────────────┘
```

| Offset | Size | Description |
|--------|------|-------------|
| 0 | 4 | Response length (little-endian) |
| 4 | 1 | Bit 7: NAK flag, Bits 0-3: SeqNo |
| 5+ | N | Payload data |

**NAK Response** (command not implemented):
- Length = 1
- Byte 4 = 0x80 | seqNo

### Notification (Emulator → DeZog)

Same as Response but SeqNo = 0.

## Commands to Implement

### Tier 1: Core (MVP)

| ID | Command | Purpose |
|----|---------|---------|
| 1 | CMD_INIT | Handshake, version exchange |
| 2 | CMD_CLOSE | Graceful disconnect |
| 3 | CMD_GET_REGISTERS | Read all Z80 registers + slots |
| 4 | CMD_SET_REGISTER | Write single register |
| 6 | CMD_CONTINUE | Resume execution |
| 7 | CMD_PAUSE | Break execution |
| 8 | CMD_READ_MEM | Read memory range |
| 9 | CMD_WRITE_MEM | Write memory |
| 24 | CMD_GET_SUPPORTED_COMMANDS | Capability query |

### Tier 2: Debugging

| ID | Command | Purpose |
|----|---------|---------|
| 40 | CMD_ADD_BREAKPOINT | Add breakpoint with condition |
| 41 | CMD_REMOVE_BREAKPOINT | Remove breakpoint by ID |
| 42 | CMD_ADD_WATCHPOINT | Add memory watchpoint |
| 43 | CMD_REMOVE_WATCHPOINT | Remove watchpoint |

### Tier 3: ZX Spectrum Features

| ID | Command | Purpose |
|----|---------|---------|
| 5 | CMD_WRITE_BANK | Write memory bank |
| 10 | CMD_SET_SLOT | Configure memory slot |
| 12 | CMD_SET_BORDER | Set border color |
| 20 | CMD_READ_PORT | Read I/O port (DeZog custom dumps) |
| 21 | CMD_WRITE_PORT | Write I/O port |
| 50 | CMD_READ_STATE | Capture emulator state |
| 51 | CMD_WRITE_STATE | Restore emulator state |

### Tier 4: Optional (ZX Next)

| ID | Command | Purpose |
|----|---------|---------|
| 11 | CMD_GET_TBBLUE_REG | ZX Next registers |
| 16 | CMD_GET_SPRITES_PALETTE | Sprite palettes |
| 17 | CMD_GET_SPRITES_CLIP_WINDOW_AND_CONTROL | Sprite clipping |
| 18 | CMD_GET_SPRITES | Sprite attributes |
| 19 | CMD_GET_SPRITE_PATTERNS | Sprite patterns |

### Notification

| ID | Notification | Purpose |
|----|--------------|---------|
| 1 | NTF_PAUSE | Execution paused (breakpoint, etc.) |

---

## Command Details

### CMD_INIT (1)

**Command:**
| Offset | Size | Value | Description |
|--------|------|-------|-------------|
| 0 | 3 | major.minor.patch | DZRP version (big-endian) |
| 3 | N | string + NUL | Program name (e.g., "DeZog v3.5.0") |

**Response:**
| Offset | Size | Value | Description |
|--------|------|-------|-------------|
| 0 | 1 | seqNo | Sequence number |
| 1 | 1 | 0/1 | Error: 0=OK, 1=error |
| 2 | 3 | major.minor.patch | Our DZRP version |
| 5 | 1 | 0-4 | Machine: 0=UNK, 1=ZX16K, 2=ZX48K, 3=ZX128K, 4=ZXNEXT |
| 6 | N | string + NUL | Our name (e.g., "Unreal-NG v1.0.0") |

On error=1 the response is just `[seqNo][1]` — DeZog reports "Remote returned
an error code: 1" and closes the session. Used when no emulator target exists
(and none appears within a 2 s grace window): answering with machine=0 would
make DeZog throw "Unknown machine type 0 received" while a session bound to
nothing silently serves default (all-zero) data for every later command.

**Machine type numbers must match DeZog's `DzrpMachineType` exactly**
(`ZX16K=1, ZX48K=2, ZX128K=3, ZXNEXT=4` — dzrpremote.ts). DeZog builds its
whole memory model (long addresses, bank/slot math) from this byte.

### CMD_GET_REGISTERS (3)

**Command:** (empty)

**Response:**
| Offset | Size | Value | Description |
|--------|------|-------|-------------|
| 0 | 1 | seqNo | |
| 1 | 2 | PC | little-endian |
| 3 | 2 | SP | |
| 5 | 2 | AF | |
| 7 | 2 | BC | |
| 9 | 2 | DE | |
| 11 | 2 | HL | |
| 13 | 2 | IX | |
| 15 | 2 | IY | |
| 17 | 2 | AF' | |
| 19 | 2 | BC' | |
| 21 | 2 | DE' | |
| 23 | 2 | HL' | |
| 25 | 1 | R | |
| 26 | 1 | I | |
| 27 | 1 | IM | |
| 28 | 1 | reserved | |
| 29 | 1 | Nslots | Number of memory slots |
| 30+ | Nslots | slot[i] | Bank number per slot |

### CMD_SET_REGISTER (4)

**Command:**
| Offset | Size | Value | Description |
|--------|------|-------|-------------|
| 0 | 1 | regId | Register ID (see table) |
| 1 | 2 | value | Value (little-endian) |

Register IDs:
```
0=PC, 1=SP, 2=AF, 3=BC, 4=DE, 5=HL, 6=IX, 7=IY,
8=AF', 9=BC', 10=DE', 11=HL', 13=IM,
14=F, 15=A, 16=C, 17=B, 18=E, 19=D, 20=L, 21=H,
22=IXL, 23=IXH, 24=IYL, 25=IYH,
26=F', 27=A', 28=C', 29=B', 30=E', 31=D', 32=L', 33=H',
34=R, 35=I
```

**Response:**
| Offset | Size | Value |
|--------|------|-------|
| 0 | 1 | seqNo |

### CMD_CONTINUE (6)

**Command:**
| Offset | Size | Value | Description |
|--------|------|-------|-------------|
| 0 | 1 | 0/1 | Enable BP1 |
| 1 | 2 | addr | BP1 address |
| 3 | 1 | 0/1 | Enable BP2 |
| 4 | 2 | addr | BP2 address |
| 6 | 1 | 0/1/2 | Alternate: 0=none, 1=step-over, 2=step-out |
| 7 | 2 | start | Range start (step-over) |
| 9 | 2 | end | Range end (step-over) |

**Response:**
| Offset | Size | Value |
|--------|------|-------|
| 0 | 1 | seqNo |

### CMD_PAUSE (7)

**Command:** (empty)

**Response:**
| Offset | Size | Value |
|--------|------|-------|
| 0 | 1 | seqNo |

### CMD_READ_MEM (8)

**Command:**
| Offset | Size | Value | Description |
|--------|------|-------|-------------|
| 0 | 1 | 0 | Reserved |
| 1 | 2 | addr | Start address |
| 3 | 2 | size | Bytes to read |

**Response:**
| Offset | Size | Value |
|--------|------|-------|
| 0 | 1 | seqNo |
| 1 | N | data | Memory contents |

### CMD_WRITE_MEM (9)

**Command:**
| Offset | Size | Value | Description |
|--------|------|-------|-------------|
| 0 | 1 | 0 | Reserved |
| 1 | 2 | addr | Start address |
| 3 | N | data | Bytes to write |

**Response:**
| Offset | Size | Value |
|--------|------|-------|
| 0 | 1 | seqNo |

### CMD_ADD_BREAKPOINT (40)

**Command:**
| Offset | Size | Value | Description |
|--------|------|-------|-------------|
| 0 | 2 | addr | Address |
| 2 | 1 | bank+1 | Bank (0 = 64K address) |
| 3 | N | condition | NUL-terminated condition string |

**Response:**
| Offset | Size | Value | Description |
|--------|------|-------|-------------|
| 0 | 1 | seqNo | |
| 1 | 2 | bpId | Breakpoint ID (0 = failed) |

### CMD_REMOVE_BREAKPOINT (41)

**Command:**
| Offset | Size | Value |
|--------|------|-------|
| 0 | 2 | bpId |

**Response:**
| Offset | Size | Value |
|--------|------|-------|
| 0 | 1 | seqNo |

### CMD_ADD_WATCHPOINT (42)

**Command:**
| Offset | Size | Value | Description |
|--------|------|-------|-------------|
| 0 | 2 | addr | Start address |
| 2 | 1 | bank+1 | Bank info |
| 3 | 2 | size | Range size |
| 5 | 1 | access | Bit0=read, Bit1=write |

**Response:**
| Offset | Size | Value | Description |
|--------|------|-------|-------------|
| 0 | 1 | seqNo | |
| 1 | 1 | error | 0=OK, 1=failed |

### CMD_GET_SUPPORTED_COMMANDS (24)

**Command:** (empty)

**Response:**
| Offset | Size | Value | Description |
|--------|------|-------|-------------|
| 0 | 1 | seqNo | |
| 1 | 32 | bitfield | Command support (little-endian) |

Bit N = 1 means CMD N is supported.

### CMD_READ_PORT (20)

**Command:** `port(2, LE)` — DeZog sends this for custom dumps
(`port: 0x...` entries in launch.json, rendered in the variables view).

**Response:** exactly **one** data byte (the value). DeZog reads `data[0]`
verbatim (`cspectremote.ts sendDzrpCmdReadPort`); an empty ACK yields
`undefined` in the UI.

### CMD_WRITE_PORT (21)

**Command:** `port(2, LE) + value(1)`.

**Response:** empty (ACK only).

Both commands run the real port bus (`Ports::In/Out`) and therefore observe
the same latches/banking the CPU sees. Callers must keep the
paused-while-debugging discipline (same as direct memory access).

### NTF_PAUSE (1)

**Notification:**
| Offset | Size | Value | Description |
|--------|------|-------|-------------|
| 0 | 1 | reason | 0=none, 1=manual, 2=BP, 3=WP-read, 4=WP-write, 255=other |
| 1 | 2 | addr | Break address |
| 3 | 1 | bank+1 | Bank info |
| 4 | N | string | NUL-terminated reason string |

---

## Implementation Notes

### Mode Selection

Unreal-NG uses **NORMAL_MODE**:
- Use CMD_ADD_BREAKPOINT / CMD_REMOVE_BREAKPOINT
- Do NOT use CMD_SET_BREAKPOINTS / CMD_RESTORE_MEM (those are for ZX Next HW)

### Long Addresses

DZRP 2.0+ uses "long addresses" with bank info:
- bank = 0: standard 64K address
- bank = N+1: specific bank N

For ZX 48K: always use bank = 0.
For ZX 128K: use bank+1 for paged memory.

### Sequence Numbers

- Range: 1-15 (4 bits)
- Increment per command
- Response echoes the command's seqNo
- Notifications use seqNo = 0

### Error Handling

- Unknown command: respond with NAK (bit 7 = 1)
- Command-specific errors: use error field in response

---

## Authoritative References

| Resource | URL |
|----------|-----|
| **DZRP Protocol Spec** | https://github.com/maziac/DeZog/blob/main/design/DeZogProtocol.md |
| **Adding New Remotes** | https://github.com/maziac/DeZog/blob/main/design/AddingNewRemotes.md |
| **Common DZRP Notes** | https://github.com/maziac/DeZog/blob/main/design/CommonDzrp.md |
| **CSpect Plugin (C#)** | https://github.com/maziac/DeZogPlugin |
| **DeZog Source** | https://github.com/maziac/DeZog/tree/main/src/remotes/dzrp |

### Version History

This spec tracks **DZRP v2.2.0** (current as of DeZog 3.x).

Key changes from prior versions:
- v2.2.0: Added NAK response, CMD_GET_SUPPORTED_COMMANDS
- v2.0.0: Added long addresses (bank info), memory model in CMD_INIT
- v1.6.0: Added CMD_CLOSE
- v1.3.0: Split into "simple" and "normal" modes

### Unreal-NG Extensions (not part of upstream DZRP)

Beyond upstream DZRP v2.2.0, this server implements two extension commands for
TTD-backed instruction history (reverse debugging): `CMD_GET_HISTORY_INFO`
(0xE0) and `CMD_GET_HISTORY_ENTRY` (0xE1). Upstream DeZog does not send these
(its history feature is zsim-local); they are exercised by our Python
verification tooling. Wire format, semantics and measured performance are
specified in [reverse-debugging.md](reverse-debugging.md) §2–§5. They answer
with a normal ACK (never the NAK bit), so a stock DeZog session is unaffected.
An `OUT_OF_RANGE` reply to 0xE1 is **non-destructive**: the browsable history
stays intact and in-range indices keep answering — only a forward command
(pause/resume/step/register-write) returns the target to the present.

## Real-DeZog compatibility notes (verified against DeZog sources)

Verified against `dzrpbufferremote.ts` / `cspectremote.ts` / `dzrpremote.ts`
(the only TCP transport in DeZog is CSpectRemote — plain `remoteType: "cspect"
pointed at our port):

- **Command queue is serialized**: DeZog sends one command at a time and only
  sends the next after the previous response (`receivedMsg` shifts the queue).
  A single unanswered command therefore freezes the whole session with no
  client-side error — the server logs every received command (`[DZRP] cmd ...`)
  precisely so the last one is visible when diagnosing a stall.
- **The `continue` promise has no timeout**: after CMD_CONTINUE DeZog waits for
  NTF_PAUSE indefinitely. A lost NTF = permanently stuck "fetching registers,
  stack" UI.
- **CMD_READ_MEM/CMD_WRITE_MEM payloads start with a reserved flags byte**
  (`[0, addrLo, addrHi, sizeLo, sizeHi]`); a size of 0x10000 is never sent —
  DeZog splits it into two 0x8000 reads.
- **Strings are NUL-terminated ASCII** in both directions (breakpoint
  conditions, INIT names, NTF reason text).
- **NTF_PAUSE layout**: `[seq=0][ntfId=1][reason][addrLo][addrHi][bank]
  [reasonString NUL]` with reason values matching `BREAK_REASON_NUMBER`
  (0=none, 1=manual, 2=breakpoint, 3/4=watchpoint r/w).
- **Not sent by the CSpect remote**: CMD_SET_BREAKPOINTS(13),
  CMD_RESTORE_MEM(14) (both throw "not implemented" in cspectremote.ts), and
  the ZX Next sprite commands. CMD_EXEC_ASM(22) is used by unit-test/revEng
  flows only and stays unimplemented (empty ACK).
