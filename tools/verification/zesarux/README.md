# ZRCP Protocol Verification

Verification tools for the ZEsarUX ZRCP (ZEsarUX Remote Control Protocol)
server that impersonates ZEsarUX for DeZog's `remoteType: "zrcp"` sessions.

## Files

| File | Description |
|------|-------------|
| `zrcp_client.py` | Python ZRCP client library (DeZog-shaped: prompt framing, blank-line interrupt, register/history line parsers) |
| `verify_zrcp_emulator.py` | End-to-end verification against a **real emulator**: launches a headless unreal-qt (`--launch`) or connects to a running host |

## Usage

```bash
# Launch unreal-qt offscreen on free ports of its own (first free base from 18600), create a PENTAGON, mute it,
# run every check, stop the host. Log: scratch/verification-zesarux/unreal-qt-<base>.log
python3 tools/verification/zesarux/verify_zrcp_emulator.py --launch

# Another binary / port base / model (48K pages the 48K BASIC ROM: the RST 38h step-over runs there)
python3 tools/verification/zesarux/verify_zrcp_emulator.py \
    --launch cmake-build-agent-release/bin/unreal-qt.app/Contents/MacOS/unreal-qt --port-base 18600 --model 48K

# Against a running host (start an emulator in it first); --webapi-port adds the core breakpoint cross-checks
python3 tools/verification/zesarux/verify_zrcp_emulator.py --port 10000 --webapi-port 8090

# A KNOWN check fails the run
python3 tools/verification/zesarux/verify_zrcp_emulator.py --launch --strict
```

`--launch` sets the ports of every server of the launched host from one base (WebAPI = base, CLI +100, MCP +200,
GDB +300, DZRP +400, ZRCP +500, through the `UNREAL_*_PORT` variables), so it never meets another session's
emulator. It creates the machine through the WebAPI (`POST /api/v1/emulator/start`), mutes it
(`PUT .../audio/mixer/master`), and terminates only the process it started.

The ZEsarUX module is started by `Automation::start()` in every host
(unreal-qt, unreal-videowall, standalone `automation`) when the build has
`ENABLE_ZESARUX_AUTOMATION=ON` (default; requires `ENABLE_DEZOG_AUTOMATION`).
Port: `UNREAL_ZRCP_PORT`, else 10000. In unreal-qt an emulator instance must exist (the UI or the WebAPI) — the
module reports machine UNKNOWN until then. The in-process smoke path for this protocol is the GTest suite
(`core/tests/automation/zesarux/`).

Result kinds: `PASS`, `FAIL` (exit code 1), `KNOWN` (a documented divergence from ZEsarUX: reported, not failing
unless `--strict`), `XPASS` (a `KNOWN` check that now holds: turn it into a plain check), `SKIP` (does not apply to
the machine). The summary lists every command sent.

## Test programs

| Program | Code at 8000h | Used for |
|---------|---------------|----------|
| Loop | `DI; L: LD A,1; LD (9000h),A; JP L` | run / breakpoints / pass count / write watchpoint / history |
| Counter | `DI; LD B,0; L: INC B; LD A,B; LD (9000h),A; LD A,(9002h); JR L` | conditional breakpoints: at the k-th arrival at 8003h B = k, (9000h) = k, F = the flags of the INC; read watchpoints |
| Caller | `DI; L: CALL 8010h; JR L` / `8010h: NOP; NOP; RET` | step into / step-over CALL, DeZog's step-over and step-out conditions |
| Block | `LD HL,9100h; LD DE,9200h; LD BC,16; LDIR; NOP` | step-over runs the whole LDIR |

Registers and the memory at 8000h, 9000h-92FFh and FE00h-FFFFh are captured after attaching and restored before
the disconnect sequence (checked).

## Test Coverage

| Area | Commands | Checks |
|------|----------|--------|
| Session | banner, `quit` | Exact welcome line; before `enter-cpu-step` `cpu-step` / `cpu-step-over` / `run` answer `Error. You must first enter cpu-step mode`; `exit-cpu-step` refuses them again; quit closes the socket, the server accepts a new session |
| Init / disconnect | `close-all-menus`, `about`, `get-version`, `set-debug-settings`, `hard-reset-cpu`, `enter-cpu-step`, `load`, `smartload`, `get-current-machine`, `clear-membreakpoints`, `enable-breakpoints`, 100× `disable-breakpoint`, `cpu-code-coverage`, `extended-stack`, `cpu-history ...` | DeZog's exact streams in order; exact `about` / version / machine text; log-only answers (`log> `) complete empty; `enter-cpu-step` pauses the machine and quit resumes it (WebAPI); the session teardown removes the adapter's breakpoints and watchpoints from the core |
| Registers | `get-registers`, `set-register` | `print_registers` shape (field order, two spaces after `R=`, flag letters bit by bit, `MEMPTR=0000`, `IM`, `IFF--`, 8 MMU words, 48K layout `8000 0005 0002 0000`); every pair, 8-bit half, alternate, `I`, `R`, `IM` read back; decimal / `0x` / `#` / `h` numbers, lowercase names, 16-bit masking; `IFF1`/`IFF2` acknowledged; `Error. Unknown parameter` / `Error. Unknown register XY` |
| Memory | `read-memory`, `write-memory-raw` | Round trip, number forms, hex in any case split over words, default length 1, length 0 / negative, full 64 KiB (`len > 65536` → `Error. Invalid length`, wrap at FFFFh), `ERROR. No parameter set`, `Error. Invalid data` (nothing written) |
| Disassembly | `disassemble` | Instruction lengths over the counter program, uppercase, 7-char prefix with the bank digit of the slot (ROM page at 0000h), default address PC, count < 1, `Error. Invalid address` |
| Stepping | `cpu-step`, `cpu-step-over`, `set-debug-settings 1` | PC and registers after each step; registers line with ` TSTATES: 0`; step into CALL pushes the return address; step-over CALL (one line, SP back), plain instruction, whole LDIR (BC = 0, data copied), RST 38h through the 48K ROM handler |
| Run | `run`, blank line | Banner, `Breakpoint fired: <condition as sent>`, stop line = PC; the blank line stops without a `Breakpoint fired` line |
| Breakpoints | `set-breakpoint`, `set-breakpointaction`, `set-breakpointpasscount`, `enable/disable-breakpoint(s)`, `get-breakpoints` | DeZog's create (set + action + enable) and remove (`disable` + `set-breakpoint <id> 0`) idioms; a pass count fires once, when its counter reaches it (ZEsarUX `debug.c`), and `set-breakpoint` resets it; action acknowledged and ignored; condition without an address logged as "can not fire"; `Error. No parameters set`; index 1..100, else `Error. Index out of range`; DeZog's step-over `SP>=n` and step-out `PC=PEEKW(SP-2) AND SP>=n` (no PC literal); `get-breakpoints` lines `Enabled N: cond` / `Disabled N: None` |
| Conditions in the core | `set-breakpoint N PC=8003h AND ...` | Stops exactly at the first arrival where the condition holds (B, PEEK, PEEKW, NOT, `<>`/`!=`, OR, XOR, `* + /`, `& \| ^`, the flag names `FS FZ FH FP FV FN FC`, H and Z flags from F after 16 / 256 passes, lowercase, a PC literal after another term); the core breakpoint (WebAPI `?all=true`, owner `dezog`) holds the translated condition and `match_count` = arrivals, `hit_count` = 1 (the false hits never stopped the machine) |
| Conditions in the server | `RAM=<bank>`, `B/C`, unknown names | RAM is ZEsarUX's: the bank at C000h on the 128K-style machines, 0 on the 48K (`expression_parser.c` TPI_V_RAM); ROM the ROM page in slot 0; no condition in the core, `hit_count` = arrivals (every arrival stopped, the server resumed), still stops at the right B; a false bank condition never stops; an unknown name stops on the first arrival (conservative) |
| Several at one address | | The one whose condition holds reports (translated + translated, refused + translated), both true → one stop reporting either (the server keeps its entries in an `unordered_map`, the order is unspecified); condition + pass count; redefining an id replaces its core breakpoint |
| Core state | `disable-breakpoint`, `enable-breakpoint`, `disable-breakpoints`, `enable-breakpoints`, clear | Each one reaches the core list, and runs stop / do not stop accordingly |
| Watchpoints | `set-membreakpoint`, `clear-membreakpoints` | Write, read, read+write, a 2-byte range (the access address reported), no match, removal by address + size (type 0), clear; core list shows the read/write flags; `ERROR. No parameter set`, `Error. Invalid parameter`, `Error. Invalid type` |
| Reverse debugging | `cpu-history` | `get 0` = the most recent instruction (ZEsarUX / DeZog semantics), (PC) opcodes and (SP) word, loop order backwards, deep index, cache stability, size, max size, enabled / started flags, `ERROR: index out of range`, `Error. Unknown parameter` |
| Extended stack | `extended-stack` | `call` / `rst` / `push` classification, explicit start address, count 0, `ERROR. No parameter set`, `Error. It's not enabled`, `Error. Unknown parameter` |
| Misc | `get-tstates-partial`, `reset-tstates-partial`, `get-cpu-frequency`, `get-memory-pages`, `cpu-code-coverage` | `0` (no counter, documented), `3500000`, page tokens consistent with the MMU words, coverage forms acknowledged |
| Memory and files | `write-memory`, `hexdump`, `load-binary`, `snapshot-save`, `snapshot-load` | `write-memory addr v...` (DeZog's writeMemory); the `hexdump` line shape (`  8000H 41 42 ... \|AB\|`, a short line padded); `load-binary "<file>" addr 0` (DeZog's loadObj: the whole file) and a length; DeZog's `-state save/restore` (`snapshot-save <path>.zsf`, the fullest format the build writes, detected by content on load) round trip memory and registers; missing files `ERROR...` |
| ZX Next | `tbblue-get-register`, `tbblue-set-register` | `%02XH`; DeZog's setSlot `tbblue-set-register (0x50 + slot) bank` moves the MMU; `ERROR. Machine is not TBBlue` elsewhere |
| Not implemented | `cpu-history get-extended / find-pc / get-pc / restore` | `Error. Unknown parameter` |

### Fixed divergences and bugs

Every check of the earlier `KNOWN` and `FAIL` lists is a plain check now: the pass count fires once and
`set-breakpoint` resets it; indexes are 1..100; the flag names, `& | ^` and `XOR` parse (and translate into the
core); a PC literal after another term (`B=5 AND PC=8003h`) keeps the whole condition; `cpu-step` leaves a
breakpoint it stopped on, also after a `cpu-history get`; breakpoints and watchpoints of the server own their ids
(two at one address, a user's breakpoint at the same address stay); `RAM` / `ROM` are ZEsarUX's paging. GTests:
`core/tests/automation/zesarux/zrcpzesaruxsemantics_test.cpp`, `zesaruxcondition_test.cpp`.

The core prints a translated condition in its canonical form: outer brackets dropped, numbers below 10 decimal,
others `#HEX` (`PC=8003h AND B*2+1=11` → `((B > 3) ...) && (((B * 2) + 1) == #B)`).

## DeZog Integration

A ready-made VS Code project with this config, helper tasks (launch unreal-ng,
create emulator, load demo snapshot) and docs lives at
`docs/inprogress/2026-08-27-dezog-integration/test-project/`.
To test with the actual DeZog extension, use this `launch.json`:

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

Notes:
- `remoteType: "zrcp"` enables DeZog's native ZEsarUX features that the DZRP
  (`remoteType: "cspect"`, port 12000) path does not expose client-side:
  watchpoints without `supportedCommands`, and reverse debugging through the
  ZesaruxCpuHistory UI (`cpu-history`)
- Both servers can listen concurrently (10000 ZRCP + 12000 DZRP); only one
  DeZog session should own breakpoints at a time
- The DZRP-path verifier lives in `tools/verification/dezog/`

## GTest

The C++ side is covered by `core/tests/automation/zesarux/` (condition
evaluator truth tables, byte-exact wire goldens, full scripted sessions) — see
`docs/inprogress/2026-08-27-dezog-integration/zrcp-server.md`.
