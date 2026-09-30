# DeZog debugger — capability survey

**Source:** https://github.com/maziac/DeZog · local checkout commit `283d18ef` (2026-07-30), package version 3.7.4 · TypeScript, VS Code debug adapter (DAP) + VS Code webviews
**Surveyed:** 2026-09-28 (source reading)
**Scope note:** DeZog is a debugger *front end* only. It has no emulator of its own except `zsim`, a small internal Z80 simulator. Everything else is reached through a "Remote": ZEsarUX over ZRCP (text/telnet), CSpect and ZX Next hardware over DZRP (binary), and MAME over the gdb remote serial protocol. It also ships a Z80 unit-test runner, a smart disassembler with flow-chart and call-graph views, and a reverse-engineering list-file mode.

Paths below are relative to the DeZog checkout root and written as `DeZog/...`.

---

## 1. Capability registry

| Area | Feature | What it does / values it shows | Where |
|---|---|---|---|
| Remote debug protocols | Remote factory | `remoteType` is one of `zrcp` (ZEsarUX), `cspect`, `zxnext` (serial), `zsim`, `mame` | `DeZog/src/remotes/remotefactory.ts:20` |
| Remote debug protocols | DZRP (DeZog Remote Protocol) | Binary request/response plus one `NTF_PAUSE` notification. Commands 1–23, 40–43, 50–51 | `DeZog/design/DeZogProtocol.md:87`, `DeZog/src/remotes/dzrp/dzrpremote.ts:32` |
| Remote debug protocols | ZRCP client (ZEsarUX) | Text commands over TCP (default port 10000). Requires ZEsarUX 10.3 or later | `DeZog/src/remotes/zesarux/zesaruxremote.ts:38`, `DeZog/src/settings/settings.ts:576` |
| Remote debug protocols | CSpect DZRP over TCP | Default port 11000 | `DeZog/src/remotes/dzrpbuffer/cspectremote.ts:17`, `DeZog/src/settings/settings.ts:595` |
| Remote debug protocols | ZX Next over serial | DZRP 2.1 through the on-target `dbg_uart_if` program. Breakpoints are software breakpoints that patch opcodes | `DeZog/src/remotes/dzrpbuffer/zxnextserialremote.ts:77`, `:287` |
| Remote debug protocols | MAME gdb stub | gdb RSP packets `g/G/p/P/m/M/c/s/Z/z` and Ctrl-C, default port 12000. Only the `z80` architecture is accepted | `DeZog/src/remotes/mame/mamegdbremote.ts:131`, `:248`, `:507` |
| Remote debug protocols | Pass-through console | `-exec <cmd>` sends a raw remote command. For DZRP remotes, `cmd_*` names send single DZRP commands | `DeZog/src/debugadapter.ts:2056`, `DeZog/src/remotes/dzrp/dzrpremote.ts:314` |
| CPU & registers | Register panes | Main and secondary register sets, a flags breakdown, and editable values (`supportsSetVariable`) | `DeZog/src/variables/shallowvar.ts:196`, `:284`, `DeZog/src/debugadapter.ts:474` |
| CPU & registers | Register hover / inline values | Hovering a register or label evaluates it. A VS Code InlineValuesProvider shows values in the editor | `DeZog/src/debugadapter.ts:471`, `DeZog/src/extension.ts:365` |
| CPU & registers | Memory slots view | Shows which bank is paged into each slot | `DeZog/src/variables/shallowvar.ts:151` |
| CPU & registers | T-states counter | Delta T-states since the last step, plus CPU frequency | `DeZog/src/remotes/remotebase.ts:1382`, `DeZog/src/remotes/zesarux/zesaruxremote.ts:734` |
| Disassembly | Brute-force disassembly pane | A fixed number of lines around PC (`disassemblerArgs.numberOfLines`) | `DeZog/src/settings/settings.ts:379` |
| Disassembly | Smart disassembler | Follows code flow from labels and entry points and keeps code/data/string distinctions | `DeZog/src/disassembler/smartdisassembler.ts:33` |
| Disassembly | Flow chart / call graph | Graphviz-style renders of a subroutine's control flow and call tree, opened from the cursor | `DeZog/src/disassembler/renderflowchart.ts:10`, `DeZog/src/disassembler/rendercallgraph.ts:11`, `DeZog/src/extension.ts:206` |
| Disassembly | Disassemble-at-cursor | Re-disassembles a region as code, data or string | `DeZog/src/extension.ts:179` |
| Memory views | Memory dump webview (`-mv`) | Multi-range hex/ASCII view that is editable, has hover info and search, and colors bytes that registers point to | `DeZog/src/views/memorydumpview.ts:32`, `:99`, `DeZog/src/settings/settings.ts:474` |
| Memory views | Word memory view (`-mvw`) | Words, little- or big-endian | `DeZog/src/views/memorydumpviewword.ts:24` |
| Memory views | Diff memory view (`-mvd`) | Snapshot, then later filter by `--` (decremented), `!=` or `==` | `DeZog/src/views/memorydiffview.ts:231` |
| Memory views | Register memory view (`-rmv`) | Memory around the addresses in selected registers | `DeZog/src/views/memoryregisterview.ts:20` |
| Memory views | Console dump/fill/search | `-md`, `-msetb`, `-msetw`, `-ml`, `-ms`, `-mdelta` (search by byte deltas, so it works on unknown text encodings) | `DeZog/src/debugadapter.ts:2471` |
| Memory views | WATCH expressions with types | `label[idx],type,count,endianness`. The type can be `b`, `w` or an assembler STRUCT | `DeZog/src/debugadapter.ts:2276` |
| Symbols & labels | List/label parsers | sjasmplus SLD (bank-aware long addresses), z80asm, z88dk v1/v2 (map file), reverse-engineering list | `DeZog/src/labels/` |
| Symbols & labels | Label lookup | `-label XXX` with `*` wildcards. `-eval` returns the value together with any matching labels | `DeZog/src/debugadapter.ts:2059`, `:2053` |
| Symbols & labels | Source ↔ address mapping | Breakpoints set in `.asm` files, and PC shown in the source | `DeZog/src/remotes/remotebase.ts:1251`, `:1273` |
| Breakpoints | PC breakpoints | "Long" (address + bank) or 64K addresses. The bank is honored when paging differs | `DeZog/src/remotes/dzrp/dzrpremote.ts:1366`, `DeZog/design/DeZogProtocol.md:236` |
| Breakpoints | Conditional breakpoints | VS Code condition field. Evaluated by DeZog in JavaScript after the remote stops | `DeZog/src/remotes/dzrp/dzrpremote.ts:778` |
| Breakpoints | Hit-count breakpoints | `== 5`, `% 3`, `>= 6`. A bare number means `===` | `DeZog/src/debugadapter.ts:902`, `DeZog/src/remotes/dzrp/dzrpremote.ts:1024` |
| Breakpoints | Logpoints (VS Code) | Log message with `${expr:format}` substitutions. Can read memory via `b@()` / `w@()` | `DeZog/src/debugadapter.ts:479`, `DeZog/src/misc/logeval.ts:7` |
| Breakpoints | `ASSERTION` source annotation | A comment `; ASSERTION expr` becomes a breakpoint with the negated condition | `DeZog/src/remotes/remotebase.ts:402` |
| Breakpoints | `LOGPOINT [group]` source annotation | Logpoints from source comments, switched on and off per group | `DeZog/src/remotes/remotebase.ts:475`, `DeZog/src/exceptionbreakpoints.ts:75` |
| Breakpoints | Break on interrupt | zsim only | `DeZog/src/exceptionbreakpoints.ts:81`, `DeZog/src/remotes/zsimulator/zsimremote.ts:855` |
| Watchpoints & watches | `WPMEM` source annotation | `; WPMEM [addr [,len [,r\|w\|rw [,cond]]]]` becomes a memory watchpoint (bank-aware) | `DeZog/src/remotes/remotebase.ts:321` |
| Watchpoints & watches | `-wpadd` / `-wprm` | Ad hoc 64K watchpoints that fire in any bank | `DeZog/src/debugadapter.ts:2098` |
| Watchpoints & watches | Exception-breakpoint toggles | ASSERTION / WPMEM / LOGPOINT / Break-on-Interrupt switches in the BREAKPOINTS pane | `DeZog/src/exceptionbreakpoints.ts:57` |
| Execution control | Continue / pause / step into / step over / step out | Built from DZRP `CONTINUE` with up to 2 temporary breakpoints, then a loop in DeZog | `DeZog/src/remotes/dzrp/dzrpremote.ts:1039`–`:1248` |
| Execution control | Move PC to cursor | A custom command (DAP goto is disabled) | `DeZog/src/extension.ts:163`, `DeZog/src/debugadapter.ts:468` |
| Execution control | RST skip handling | Step-over skips data bytes after `RST` according to label "skip" info | `DeZog/src/remotes/remotebase.ts:1545` |
| History / rewind / time travel | Reverse debugging (step back, reverse continue) | Registers only. Memory is not rewound | `DeZog/design/reversedebugging.md:202`, `DeZog/src/debugadapter.ts:624`, `:1555`, `:1960` |
| History / rewind / time travel | True CPU history | Recorded by the remote: zsim's internal ring buffer, or ZEsarUX `cpu-history` | `DeZog/src/remotes/zsimulator/zsimcpuhistory.ts:23`, `DeZog/src/remotes/zesarux/zesaruxcpuhistory.ts:93` |
| History / rewind / time travel | Step history ("lite") | Only the positions where the user stopped. Works with any remote | `DeZog/src/remotes/stephistory.ts:88`, `DeZog/design/reversedebugging.md:10` |
| History / rewind / time travel | History spot | Highlights the last and next N executed lines, with changed registers | `DeZog/src/remotes/stephistory.ts:392`, `DeZog/src/decoration.ts:522` |
| History / rewind / time travel | State save/restore | `-state save/restore/list/clear`. Uses DZRP `READ_STATE`/`WRITE_STATE` (zsim) or ZRCP `snapshot-save/load` | `DeZog/src/remotes/dzrp/dzrpremote.ts:1749`, `DeZog/src/remotes/zesarux/zesaruxremote.ts:1386` |
| Video, raster & beam | zsim ULA screen (Spectrum) | Whole-frame snapshot of bank 5/7 and the border every 20 ms. No beam position, no raster effects | `DeZog/src/remotes/zsimulator/spectrumulascreen.ts:11`, `:118`, `:136` |
| Video, raster & beam | zsim ULA (ZX81) | Scanline-level HSYNC/NMI timing: 207 T-states per line, hi-res mode, Chroma81 | `DeZog/src/remotes/zsimulator/zx81ulascreen.ts:46`, `:252` |
| Video, raster & beam | ZX Next sprite / pattern views | `-sprites`, `-patterns`. Palette and clip window come from DZRP 16–19 or ZRCP `tbblue-*` | `DeZog/src/views/zxnextspritesview.ts:476`, `DeZog/src/views/zxnextspritepatternsview.ts:33` |
| Sound & device views | zsim beeper | Port 0xFE bit 4 is sampled into an audio buffer that the webview plays | `DeZog/src/remotes/zsimulator/zxbeeper.ts`, `DeZog/src/remotes/zsimulator/zsimwebview/zxaudiobeeper.ts` |
| Sound & device views | zsim keyboard / joysticks / zxnDMA / custom ports | Simulated peripherals, plus user JavaScript peripherals with a custom HTML UI | `DeZog/src/remotes/zsimulator/zsimremote.ts:332`, `DeZog/src/remotes/zsimulator/customcode.ts:42` |
| Profiling, coverage | Code coverage | Executed lines are highlighted in source and disassembly. Cleared at every step/continue | `DeZog/src/decoration.ts:369`, `DeZog/src/remotes/zsimulator/codecovarray.ts:6`, `DeZog/src/remotes/zesarux/zesaruxremote.ts:763` |
| Profiling, coverage | zsim "visual memory" | 256-cell strip showing read/write/execute per address region | `DeZog/src/remotes/zsimulator/simulatedmemory.ts:76`, `:625` |
| Scripting & automation | Z80 unit tests | `UT_*` labels, `TC_END`, `TEST_MEMORY_BYTE` macros built on ASSERTION, with per-test timeout and coverage | `DeZog/documentation/UnitTests.md`, `DeZog/documentation/unit_tests.inc`, `DeZog/src/z80unittests/z80unittestrunner.ts:515` |
| Scripting & automation | zsim custom JavaScript | `API.tick/readPort/writePort/generateInterrupt/log/sendToCustomUi` | `DeZog/src/remotes/zsimulator/customcode.ts:42`–`:124` |
| Scripting & automation | `commandsAfterLaunch` | Debug-console commands that run after connecting | `DeZog/src/settings/settings.ts:460` |
| Import / export & persistence | Program loading | `.sna`, `.z80`, `.nex`, `.p` (ZX81) and raw `loadObjs`. For DZRP these are written with `WRITE_BANK`/`WRITE_MEM` | `DeZog/src/remotes/dzrp/dzrpremote.ts:1450`, `:1600`, `:1712` |
| UI conveniences | Break/revdbg/coverage decorations | Whole-line colors for current break, coverage, history and spot | `DeZog/src/decoration.ts:58` |
| UI conveniences | `-view` redirection | Any console command's output can open in its own view | `DeZog/src/debugadapter.ts:2471` |

---

## 2. Remote protocols: what DeZog expects from a backend

This matters most for unreal-ng. `core/automation/dezog/` (DZRP server) and `core/automation/zesarux/` (ZRCP server) in the unreal-ng repo implement the two protocol families described here.

### 2.1 Remote abstraction and capability flags

- `RemoteBase` is the interface every backend implements. It covers `init/load/continue/pause/stepOver/stepInto/stepOut/reverseContinue`, register get/set, `readMemoryDump/writeMemoryDump`, `setBreakpoint/removeBreakpoint`, `setWatchpoint`, `enableAssertionBreakpoints`, `enableLogpoints`, `enableBreakOnInterrupt`, `stateSave/stateRestore`, T-state queries, and the tbblue sprite getters (`DeZog/src/remotes/remotebase.ts:158`–`:1683`).
- Feature flags tell DeZog which source annotations to enable: `supportsASSERTION`, `supportsWPMEM`, `supportsLOGPOINT`, `supportsBreakOnInterrupt` (`DeZog/src/remotes/remotebase.ts:93`–`:102`). Values per remote:
  - zsim: all four (`DeZog/src/remotes/zsimulator/zsimremote.ts:149`).
  - ZEsarUX: ASSERTION + WPMEM, **no LOGPOINT** (`DeZog/src/remotes/zesarux/zesaruxremote.ts:68`).
  - CSpect and ZX Next: ASSERTION + LOGPOINT, no WPMEM (`DeZog/src/remotes/dzrpbuffer/cspectremote.ts:27`, `DeZog/src/remotes/dzrpbuffer/zxnextserialremote.ts:72`).
  - MAME: ASSERTION + WPMEM + LOGPOINT (`DeZog/src/remotes/mame/mamegdbremote.ts:43`).
- Break reasons are unified as `BREAK_REASON_NUMBER`: 0 none (temporary or step breakpoint), 1 manual, 2 breakpoint, 3 watchpoint read, 4 watchpoint write, 5 CPU error, 100 stepping not allowed, 101 interrupt, 255 other (`DeZog/src/remotes/remotebase.ts:21`).

### 2.2 DZRP framing and versioning

- **Command:** `len(4, LE, payload only) · seq(1, 1..255) · cmdId(1) · payload`.
- **Response:** `len(4) · seq(1) · payload`.
- **Notification:** `len(4) · seq=0 · ntfId · payload`.
- Strictly one command is outstanding at a time; the next is sent only after the previous response arrives (`DeZog/design/DeZogProtocol.md:9`, `:189`–`:233`).
- Version check in `CMD_INIT`: the major versions must be equal, and DeZog's minor version must be ≤ the remote's minor version (`DeZog/src/remotes/dzrpbuffer/dzrpbufferremote.ts:390`). DeZog sends `[2,0,0]` (`DeZog/src/remotes/dzrp/dzrpremote.ts:135`); the serial ZX Next remote sends `[2,1,0]` (`DeZog/src/remotes/dzrpbuffer/zxnextserialremote.ts:77`).
- **Long addresses:** `addr16 + (bank+1)` in one extra byte. A value of 0 means a plain 64K address (`DeZog/design/DeZogProtocol.md:236`–`:242`).
- **Machine type** returned by `CMD_INIT`: 1=ZX16K, 2=ZX48K, 3=ZX128K, 4=ZXNEXT. Any other value makes DeZog throw "Unknown machine type" (`DeZog/src/remotes/dzrp/dzrpremote.ts:242`–`:261`). This is the only way the remote tells DeZog its memory model, so there is no DZRP way to describe Pentagon 512/1024, Scorpion or ATM paging *(inferred from the enum)*.
- **Timeouts:** 5 s response timeout by default (`socketTimeout`) and 1 s connection timeout (`DeZog/src/settings/settings.ts:597`, `DeZog/src/remotes/dzrpbuffer/dzrpbufferremote.ts:10`).

### 2.3 DZRP command set, and which remotes DeZog uses it with

Use per remote comes from `DeZog/design/DeZogProtocol.md:89`–`:119`. Payloads are at the given lines of the same file.

| ID | Command | Payload essentials | zsim | CSpect | ZXNext | MAME* |
|---|---|---|---|---|---|---|
| 1 | INIT | → version(3) + name. ← err, version(3), machineType, name (`:247`) | – | X | X | – |
| 2 | CLOSE | – (`:269`) | – | X | X | X |
| 3 | GET_REGISTERS | ← PC SP AF BC DE HL IX IY AF' BC' DE' HL' (LE words), R, I, IM, reserved, Nslots, slot banks (`:288`) | X | X | X | – |
| 4 | SET_REGISTER | regIdx(1) value(2). 0=PC … 13=IM, 14..33 = 8-bit halves, 34=R, 35=I (`:322`) | X | X | X | X |
| 5 | WRITE_BANK | bank + whole-bank data. Used to load .sna/.nex (`:337`) | X | X | X | – |
| 6 | CONTINUE | bp1 en/addr, bp2 en/addr (temporary), alternate cmd (step-over range / step-out; **not implemented**) (`:366`–`:401`) | X | X | X | X |
| 7 | PAUSE | – (`:404`) | X | X | – | X |
| 8/9 | READ_MEM / WRITE_MEM | 64K address, size (`:419`, `:438`) | X | X | X | X |
| 10 | SET_SLOT | slot, bank (0xFF = ROM) (`:456`) | X | X | X | – |
| 11 | GET_TBBLUE_REG | reg → value (`:484`) | X | X | X | – |
| 12 | SET_BORDER | color (`:498`) | X | X | X | – |
| 13/14 | SET_BREAKPOINTS / RESTORE_MEM | software breakpoints by opcode patching (ZX Next hardware only) (`:512`, `:541`) | – | – | X | – |
| 15 | LOOPBACK | serial transport test (`:571`) | – | – | X | – |
| 16–19 | sprite palette / clip+control / sprites / patterns (`:593`–`:658`) | | X | X | X/– | – |
| 20/21 | READ_PORT / WRITE_PORT (`:661`, `:676`) | | – | * | * | – |
| 22 | EXEC_ASM | small code run in debugger context; returns AF BC DE HL (`:691`) | – | * | * | – |
| 23 | INTERRUPT_ON_OFF (`:721`) | | X | X | X | – |
| 40/41 | ADD_BREAKPOINT / REMOVE_BREAKPOINT | long addr + 0-terminated condition string → bpId(2), 0 = none left (`:736`–`:766`) | X | X | – | X |
| 42/43 | ADD_WATCHPOINT / REMOVE_WATCHPOINT | long addr, size(2), access bit0=r bit1=w (`:769`–`:802`) | X | – | – | X |
| 50/51 | READ_STATE / WRITE_STATE | opaque blob that the remote defines (`:805`–`:831`) | X | – | – | – |
| NTF 1 | NTF_PAUSE | reason(1), addr(2), bank+1(1), reason string (`:836`–`:846`) | | | | |

\* In the MAME column, DZRP calls are translated into gdb packets. In the CSpect and ZXNext columns, `*` means implemented in the remote but not used by DeZog.

### 2.4 Semantics a backend must get right (from the client code)

1. **Conditions and logpoints are evaluated in DeZog, not in the remote.**
   - `ADD_BREAKPOINT` carries the condition string (`DeZog/src/remotes/dzrpbuffer/dzrpbufferremote.ts:507`–`:513`), but after every `NTF_PAUSE` DeZog runs `evalBpConditionAndLog` anyway (`DeZog/src/remotes/dzrp/dzrpremote.ts:920`).
   - That function fetches registers (`GET_REGISTERS`), evaluates the condition and hit count, prints logpoints, and if the break should not happen it sends `CONTINUE` again (`DeZog/src/remotes/dzrp/dzrpremote.ts:1047`–`:1069`).
   - So a remote can stop unconditionally at the address. Evaluating the condition remotely only saves round trips; a remote-side evaluation that disagrees with DeZog cannot hide a hit that DeZog would report.
   - Each logpoint or false-condition hit costs at least two round trips (NTF_PAUSE, GET_REGISTERS, then CONTINUE) *(inferred)*.
2. **Stepping is built from `CONTINUE` plus up to two temporary 64K breakpoints.**
   - `calcStepBp` decodes the opcode at PC and chooses the next PC and a branch target, skipping `RST` data bytes (`DeZog/src/remotes/remotebase.ts:1545`).
   - The remote must report reaching a temporary breakpoint as reason 0 (`NO_REASON`) at that PC (`DeZog/src/remotes/dzrp/dzrpremote.ts:989`).
   - Step-out loops step-over until SP is above the start SP *and* the previous instruction was an executed `RET`/`RETI`/`RETN`/`RET cc`. That check reads 2 bytes at the previous PC every iteration (`DeZog/src/remotes/dzrp/dzrpremote.ts:1170`–`:1248`).
   - The `CONTINUE` "alternate command" optimization (step-over range / step-out) is defined but DeZog always sends 0 (`DeZog/src/remotes/dzrp/dzrpremote.ts:83`–`:91`, `DeZog/src/remotes/dzrpbuffer/dzrpbufferremote.ts:483`).
3. **The `CONTINUE` response is immediate.** The actual stop arrives later as `NTF_PAUSE` (`DeZog/design/DeZogProtocol.md:398`–`:401`). `PAUSE` must result in an `NTF_PAUSE` with reason 1.
4. **Watchpoint hits** are `NTF_PAUSE` reason 3 or 4 with the *accessed* address and its bank+1. DeZog discards hits whose bank does not match the slot mapping (`DeZog/src/remotes/dzrp/dzrpremote.ts:661`–`:686`, `:925`–`:946`). WPMEM conditions are parsed but ignored ("Condition not used at the moment", `DeZog/src/remotes/dzrp/dzrpremote.ts:940`).
5. **Only PC breakpoints are supported over DZRP.** A breakpoint with no address produces the warning "DZRP does only support PC breakpoints" (`DeZog/src/remotes/dzrp/dzrpremote.ts:1369`).
6. **Breakpoint IDs:** `ADD_BREAKPOINT` returns a 16-bit ID; 0 means "no more breakpoints", and DeZog then shows the breakpoint as unverified (`DeZog/src/remotes/dzrp/dzrpremote.ts:1377`–`:1379`).
7. **Call stack** is reconstructed by DeZog from `READ_MEM` of the stack.
   - Each stack word is classified by reading the 3 bytes before it and checking for `CALL nn` / `CALL cc,nn`. `RST` detection is intentionally off because it misfires about 1 time in 32 (`DeZog/src/remotes/remotebase.ts:668`–`:700`).
   - So a remote does not provide a call stack; it only needs fast `READ_MEM`.
8. **History is not part of DZRP.**
   - With DZRP remotes other than zsim, DeZog falls back to the step history (`DeZog/design/reversedebugging.md:113`–`:119`).
   - unreal-ng's server defines `CMD_GET_SUPPORTED_COMMANDS` (24) and extensions 0xE0/0xE1 for true history (`core/automation/dezog/include/dzrptypes.h:41`–`:60` in the unreal-ng repo). Upstream DeZog `283d18ef` does not define or send any of them (the enum at `DeZog/src/remotes/dzrp/dzrpremote.ts:32`–`:71` stops at 51). So they only matter to a patched client.

### 2.5 ZRCP (ZEsarUX) commands DeZog sends

- **Startup:** `close-all-menus`, `about`, `get-version`, `set-debug-settings 32|0` (skip interrupt), `hard-reset-cpu`, `enter-cpu-step`, `get-current-machine` (matched on "tbblue"/"zx spectrum next", "128k", "48k") (`DeZog/src/remotes/zesarux/zesaruxremote.ts:198`–`:260`).
- **Coverage and stack:**
  - `cpu-code-coverage enabled yes|no`, `cpu-code-coverage clear`.
  - `extended-stack enabled yes` (enabled after a deliberate "no" first, as a workaround for a ZEsarUX bug) and `extended-stack get <depth>`.
  - Sources: `DeZog/src/remotes/zesarux/zesaruxremote.ts:310`–`:321`, `:502`.
- **State and memory:** `get-registers`, `set-register R=v`, `get-memory-pages` (e.g. `RO1 RA5 RA2 RA0`), `tbblue-set-register 0x50+slot bank`, `read-memory a n`, `write-memory-raw`, `write-memory`, `snapshot-save/load`, `smartload`, `load-binary` (`DeZog/src/remotes/zesarux/zesaruxremote.ts:363`, `:409`, `:424`, `:438`, `:1316`–`:1399`, `:1589`).
- **Run control:**
  - `run` is an interruptable command; pause sends a bare newline (`DeZog/src/remotes/zesarux/zesaruxremote.ts:532`, `:597`, `DeZog/src/remotes/zesarux/zesaruxsocket.ts:479`).
  - `cpu-step`, and `cpu-step-over` only for `LDIR/LDDR/CPIR/CPDR`.
  - `CALL`/`RST` are stepped over by running with a temporary breakpoint 100 with condition `SP>=<sp>`. The client reads `disassemble <pc>` to decide which case applies (`DeZog/src/remotes/zesarux/zesaruxremote.ts:603`–`:700`).
- **Breakpoints:**
  - `set-breakpointaction id` (empty action), `set-breakpoint id <cond>`, `set-breakpointpasscount id n` (ZEsarUX ≥ 12.1), `enable-breakpoint`, `disable-breakpoint`, `enable-breakpoints`. IDs 1..100 (`DeZog/src/remotes/zesarux/zesaruxremote.ts:44`, `:61`, `:340`–`:349`, `:1144`–`:1154`).
  - The address is encoded in the condition: `PC=0XXXXh`, plus `and ROM=n` / `and RAM=n` for 128K or `and SEGn=bank` for Next (`DeZog/src/remotes/zesarux/zesaruxremote.ts:1076`–`:1120`).
- **Condition translation:**
  - Labels are replaced by numbers.
  - `==`→`=`, `!=`→`<>`, `&&`→`AND`, `||`→`OR`, `!`→`NOT`, `0x12`→`12H` (`DeZog/src/remotes/zesarux/zesaruxremote.ts:1022`–`:1060`).
- **Memory breakpoints:** `set-membreakpoint <addr>h <type> <size>`, where type 0 clears; `clear-membreakpoints` (`DeZog/src/remotes/zesarux/zesaruxremote.ts:922`, `:936`, `:340`).
- **History:** `cpu-history enabled yes`, `set-max-size N`, `clear`, `started yes`, `ignrephalt yes`, `ignrepldxr yes`, `get <index>` (index 0 = newest) (`DeZog/src/remotes/zesarux/zesaruxcpuhistory.ts:93`–`:120`).
- **Next views:** `tbblue-get-register`, `tbblue-get-palette sprite`, `tbblue-get-clipwindow sprite`, `tbblue-get-sprite`, `tbblue-get-pattern` (`DeZog/src/remotes/zesarux/zesaruxremote.ts:1416`–`:1524`).
- **Code coverage format:** a string of 4-hex-digit addresses at a 5-character stride. DeZog converts them to long addresses using the *current* slots, which is wrong if paging changed during the run. The source acknowledges this (`DeZog/src/remotes/zesarux/zesaruxremote.ts:776`–`:789`).

### 2.6 MAME gdb

- **Init:** `qXfer:features:read:target.xml` (the architecture must be z80).
- **Commands:** `g` reads all registers; `p0b` reads PC; `P<n>=<LE>` writes a register; `Z1/z1,<addr>,0` sets and clears the temporary step breakpoints; `c` continues; Ctrl-C (0x03) breaks, followed by `p0b` (`DeZog/src/remotes/mame/mamegdbremote.ts:131`, `:532`, `:576`, `:605`, `:647`, `:670`, `:695`).
- **Stop replies:** `T05…`. Watch stop replies end in `watch`, `rwatch` or `awatch` (`DeZog/src/remotes/mame/mamegdbremote.ts:331`, `:398`).
- **Memory model:** `MemoryModelUnknown` (flat 64K) (`DeZog/src/remotes/mame/mamegdbremote.ts:142`).

---

## 3. Breakpoints, conditions and expressions

- **Data structure.**
  - `GenericBreakpoint { bpId, longAddress, condition, log, hitCountCondition, hitCounter }` (`DeZog/src/genericwatchpoint.ts:21`–`:24`).
  - On every continue or step, `createTemporaryBreakpoints` merges user breakpoints, enabled logpoints and assertion breakpoints into a `Map<longAddress, GenericBreakpoint[]>`. Several breakpoints can share one address (`DeZog/src/remotes/dzrp/dzrpremote.ts:711`–`:739`).
  - Lookup tries the long address first, then the 64K address (`DeZog/src/remotes/dzrp/dzrpremote.ts:697`–`:702`).
- **Expression grammar.**
  - `Utility.evalExpression` textually replaces register names (including `HL'`), labels (sjasmplus `@global`, `.local`, module prefixes) and numbers with decimal values, then runs JavaScript `eval` (`DeZog/src/misc/utility.ts:246`–`:327`).
  - As a result every JavaScript operator works: `+ - * / % & | ^ ~ << >> >>> == === != < > <= >= && || ! ?:` and parentheses *(inferred from the use of `eval`)*.
  - Booleans map to 1/0.
  - Number literals: `0x1F`, `$1F`, `1Fh`, `0101b`, decimal, `'c'` (character code), and `_szhpnc` flag masks (`DeZog/src/misc/utility.ts:144`–`:220`).
  - Breakpoint conditions cannot read memory: evaluation is synchronous, and the source comments "If I would allow 'await evalExpression' I could also allow e.g. memory checks" (`DeZog/src/remotes/dzrp/dzrpremote.ts:782`).
- **Logpoint format.**
  - `${expr[:format]}` is an arbitrary expression. `b@(…)` and `w@(…)` become asynchronous memory reads, so logpoints *can* read memory, unlike breakpoint conditions. Example: `2*b@(HL+LABEL):hex8` (`DeZog/src/misc/logeval.ts:7`–`:82`).
  - Formats: `string`, `hex8`, `hex16`, `int8`, `int16`, `uint8`, `uint16`, `bits`, `flags` (`DeZog/src/misc/logeval.ts:85`, `:220`–`:260`).
  - The older `Utility.evalLogString` uses `hex/dhex/bits/unsigned/signed/name` (`DeZog/src/misc/utility.ts:388`, `:490`–`:512`).
  - LOGPOINT syntax: `; LOGPOINT [GROUP] text ${A} ${w@(HL):hex}` (`DeZog/src/remotes/remotebase.ts:475`–`:515`).
- **ASSERTION.**
  - `; ASSERTION expr` is negated into a breakpoint condition. Several ASSERTIONs at one address are OR-combined. A bare `ASSERTION` always breaks (`DeZog/src/remotes/remotebase.ts:402`–`:470`).
  - On a hit the reason reads "Assertion failed: <expr with values substituted>" (`DeZog/src/remotes/dzrp/dzrpremote.ts:830`–`:840`).
- **WPMEM.** `; WPMEM [addr [, len [, r|w|rw [, cond]]]]`. The address defaults to the annotated line's address, the length to 1, the access to `rw` (`DeZog/src/remotes/remotebase.ts:321`–`:393`).
- **Hit counts.** The VS Code hit condition is checked once with `eval("1 " + cond)`. A leading number gets `=== ` prepended. At runtime DeZog evaluates `"<counter> <cond>"` (`DeZog/src/debugadapter.ts:902`–`:921`, `DeZog/src/remotes/dzrp/dzrpremote.ts:1024`–`:1033`).

## 4. Execution control and performance (zsim)

- **Main loop.** `z80CpuContinue` runs chunks of about 20 000 T-states (`5000*4`), then yields to the VS Code event loop (`DeZog/src/remotes/zsimulator/zsimremote.ts:744`, `:931`).
- **Per-instruction work, in order:**
  1. Push a history record.
  2. Run the custom-code tick if due.
  3. Execute.
  4. Mark visual memory.
  5. Record coverage.
  6. Look up `tmpBreakpoints` by long PC and evaluate conditions and logpoints *in the simulator*, avoiding a round trip to the adapter.
  7. Check the watchpoint hit flag.
  8. Check the temporary step breakpoints (64K compare).
  9. Check the interrupt flag for Break on Interrupt.
  10. Check for manual stop.

  Sources: `DeZog/src/remotes/zsimulator/zsimremote.ts:748`–`:883`.
- **Cheap watchpoints.**
  - `watchPointMemory` is a 64K array of `{read, write}` reference counts.
  - Each memory read or write tests one entry and latches `hitAddress` (`DeZog/src/remotes/zsimulator/simulatedmemory.ts:184`, `:427`–`:520`).
- **Speed control.**
  - `limitSpeed` sleeps to hold `cpuFrequency`. It re-syncs every 20 ms and caps each sleep at 500 ms (`DeZog/src/remotes/zsimulator/zsimremote.ts:907`–`:928`).
  - `cpuLoad` computes the load as 1 − (HALT T-states ÷ T-states between interrupts), averaged over `cpuLoad` interrupts (`DeZog/src/remotes/zsimulator/z80cpu.ts:31`–`:81`, `:362`–`:378`).
- **Other remotes.** ZEsarUX and CSpect decide breakpoint hits natively and DeZog only evaluates conditions afterwards (§2.4).

## 5. History, reverse debugging and coverage

- **Two history kinds.**
  - True CPU history is recorded by the remote.
  - Step history holds only the user's stop points (`DeZog/design/reversedebugging.md:6`–`:16`).
  - `supportsStepBack` is announced when `history.reverseDebugInstructionCount > 0`, which defaults to 10000 (`DeZog/src/debugadapter.ts:622`–`:624`, `DeZog/src/settings/settings.ts:1063`).
- **Record format (zsim).** A `Uint16Array` holding all registers plus 4 opcode bytes at PC plus the word at (SP) (`DeZog/src/remotes/zsimulator/z80cpu.ts:490`–`:510`).
  - The ring buffer grows by push until `maxSize`. Repeated PC (LDIR, HALT) is recorded only once (`DeZog/src/remotes/zsimulator/zsimcpuhistory.ts:7`–`:23`, `:100`; `DeZog/src/remotes/zsimulator/zsimremote.ts:698`–`:707`).
  - unreal-ng's 0xE1 extension returns exactly this layout.
- **Reverse call stack.** Rebuilt by interpreting CALL/RST/PUSH/POP/RET and SP changes between records, including interrupt detection (`DeZog/src/remotes/cpuhistory.ts:243`–`:760`).
- **Limits.**
  - Memory, ports and sprites are **not** rewound; only the displayed registers and slots change (`DeZog/design/reversedebugging.md:202`, `:289`–`:299`).
  - Reverse continue stops at PC breakpoints only (`DeZog/src/remotes/stephistory.ts:438`).
- **History spot.** Shows `spotCount` lines around the current position, default 10 and at most 20, optionally with the changed registers (`DeZog/src/settings/settings.ts:463`–`:468`, `:499`, `:1066`).
- **Coverage.**
  - Enabled by default except for CSpect and MAME (`DeZog/src/settings/settings.ts:1079`–`:1087`).
  - zsim keeps a `Set` of long PCs that is reset at every `startProcessing`, so the display shows only "code executed since last stop" (`DeZog/src/remotes/zsimulator/zsimremote.ts:970`–`:993`, `DeZog/src/remotes/zsimulator/codecovarray.ts:6`–`:36`).

## 6. Video, raster, beam and devices (zsim)

- **Spectrum ULA.**
  - An interrupt fires every 20 ms of *simulated* time (T-states / cpuFreq), and the webview then receives bytes 0..0x1AFF of the display bank plus the border color (`DeZog/src/remotes/zsimulator/spectrumulascreen.ts:118`–`:146`).
  - The border is a single color per frame.
  - There is no beam position, no per-line border, no floating bus and **no memory contention** anywhere in the source (a search for "contend/contention" finds nothing).
  - 128K shadow-screen switching is decoded from port 0x7FFD bit 3 (`DeZog/src/remotes/zsimulator/spectrumulascreen.ts:83`–`:92`).
- **ZX81 ULA.** This one is timing-accurate by scanline: 207 T-states per line, HSYNC low periods and NMI generation, plus hi-res and Chroma81 options (`DeZog/src/remotes/zsimulator/zx81ulascreen.ts:46`–`:50`, `:252`–`:282`; `DeZog/src/settings/settings.ts:230`–`:260`).
- **Machine models:** `RAM`, `ZX16K`, `ZX48K`, `ZX128K`, `ZXNEXT`, `COLECOVISION`, `CUSTOM` (user-defined slots and banks), and `ZX81-1K` through `ZX81-56K` (`DeZog/src/remotes/zsimulator/zsimremote.ts:373`–`:417`).
- **Peripherals.**
  - Beeper, keyboard, Interface 2 / Kempston / custom joysticks, zxnDMA, and TBBlue registers (memory slots, CPU speed) (`DeZog/src/remotes/zsimulator/zsimremote.ts:201`–`:320`, `:332`–`:370`).
  - Custom JavaScript peripherals receive `API.tstates` and can raise interrupts (`DeZog/src/remotes/zsimulator/customcode.ts:42`–`:124`).

## 7. Unit tests of Z80 code

- **Macro contract.** `UNITTEST_INITIALIZE` defines these labels:
  - `UNITTEST_TEST_WRAPPER` (`di; ld sp,UNITTEST_STACK; call 0`).
  - `UNITTEST_CALL_ADDR` (patched per test).
  - `UNITTEST_TEST_READY_SUCCESS` (a loop that gets a breakpoint).
  - `UNITTEST_STACK` / `UNITTEST_STACK_BOTTOM`.

  `TC_END` jumps to success. `TEST_MEMORY_BYTE/WORD` and `TEST_FAIL` expand to `nop ; ASSERTION …` (`DeZog/documentation/unit_tests.inc`).
- **Runner flow.** The runner patches the call address with `writeMemoryDump`, sets PC, sets success and failure breakpoints, continues, and classifies the stop reason. A per-test timeout (`unitTestTimeout`) sends a break; this is not possible on ZX Next (`DeZog/src/z80unittests/z80unittestrunner.ts:220`–`:249`, `:515`–`:526`, `:587`–`:600`).
- **Backend requirement.** Plain memory writes, set PC, breakpoints and ASSERTION support are all a backend needs to host DeZog unit tests.

## 8. Notable and unique ideas

1. **Source-comment debugging annotations** (`ASSERTION`, `WPMEM`, `LOGPOINT [group]`). Debug intent lives in the `.asm` file, and each kind has a switch in the BREAKPOINTS pane. It would be cheap to support in unreal-ng's own symbol loader, with the checks done emulator-side.
2. **Temporary-breakpoint stepping protocol.** The backend only has to implement "run until one of two addresses, or a real breakpoint". Step-over, step-out and `RST`-data skipping all live in the client, which keeps backends minimal.
3. **Bank-aware "long addresses" everywhere** (breakpoints, watchpoints, coverage, labels via sjasmplus SLD). A wrong-bank hit is silently resumed. This maps directly onto unreal-ng's physical-page breakpoints.
4. **Diff memory view** (`-mvd`). Take a snapshot, run, then filter for `--`, `!=` or `==`. This is the classic cheat-finder workflow built into a debugger.
5. **`-mdelta` search.** Finds strings in unknown encodings by matching byte deltas.
6. **Unit-testing Z80 code** against the real remote, with ASSERTION macros, coverage and timeouts.
7. **History spot plus changed-register annotation** in the source gutter. A lightweight "what just happened" view without opening a trace.
8. **zsim custom JavaScript peripherals with an HTML UI.** A user can prototype hardware without touching the emulator.
9. **ZX81 scanline-timed ULA** in a JavaScript simulator. The Spectrum side, by contrast, has no beam or contention, so DeZog brings nothing for raster work on the Spectrum.

## 9. Gaps and caveats

- **Round trips.** Conditions, hit counts and logpoints are evaluated client-side after a full stop. On DZRP/ZRCP remotes each false hit costs round trips, which makes hot-loop conditional breakpoints slow *(inferred from §2.4)*. zsim avoids this by evaluating in-process.
- **No memory in conditions.** Breakpoint conditions cannot read memory (`DeZog/src/remotes/dzrp/dzrpremote.ts:782`), and WPMEM conditions are parsed but ignored (`DeZog/src/remotes/dzrp/dzrpremote.ts:940`).
- **Registers-only reverse debugging.** Memory is not rewound (`DeZog/design/reversedebugging.md:289`). A backend with full time travel (unreal-ng TTD) can offer more than DeZog can display.
- **Machine model limits.** DZRP knows only 16K/48K/128K/Next. Other clones must pretend to be one of these, and wider paging (Pentagon 512/1024, ATM, Profi) cannot be described (`DeZog/src/remotes/dzrp/dzrpremote.ts:97`–`:102`).
- **Unused protocol features.** `CONTINUE` alternate commands (step-over range / step-out) are defined but never sent, so every step-out iteration is a round trip (`DeZog/src/remotes/dzrp/dzrpremote.ts:83`–`:91`).
- **ZEsarUX coverage** maps 64K addresses with the slots at stop time, which is wrong after paging changes (`DeZog/src/remotes/zesarux/zesaruxremote.ts:782`).
- **ZEsarUX step-over** is emulated with an `SP>=` breakpoint for CALL/RST because native step-over hangs on `jp cc` (`DeZog/src/remotes/zesarux/zesaruxremote.ts:607`–`:650`).
- **Stale enum reference.** `MameGdbRemote.sendDzrpCmdInit` returns `DzrpMachineType.ALL_ROM`, which the enum does not define (`DeZog/src/remotes/mame/mamegdbremote.ts:568` vs `DeZog/src/remotes/dzrp/dzrpremote.ts:97`). The method is bypassed because MAME overrides `onConnect` (`DeZog/src/remotes/mame/mamegdbremote.ts:126`).
- **No DAP data breakpoints** (`supportsDataBreakpoints = false`). Watchpoints come only from WPMEM or `-wpadd` (`DeZog/src/debugadapter.ts:495`).
- **Call stack heuristic.** The call stack is guessed from `CALL` opcodes before return addresses, and RST frames are deliberately not detected (`DeZog/src/remotes/remotebase.ts:668`–`:700`).
- **No capability negotiation.** Upstream DZRP has no way for a remote to report which commands it supports; DeZog hardcodes the subset per remote type (`DeZog/design/DeZogProtocol.md:122`). unreal-ng's `CMD_GET_SUPPORTED_COMMANDS` (24) is an unreal-ng addition.
