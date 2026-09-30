# NedoOS kernel reference for the layer (ZX-Evo build, verified live)

**Status:** reference, 2026-09-30. Requirements: [requirements-nedoos-layer.md](requirements-nedoos-layer.md).
Everything in this file was checked against a running NedoOS (full SD card,
`sd_boot.$C`, NedoOS rev 44049473) in unreal-ng, using only the existing WebAPI
and a small Python prototype (§8). Addresses are for this one build; the layer
takes them from the build's symbols (§2), never from this table.

---

## 1. Memory model in one picture

NedoOS on ZX-Evo runs with the ATM memory manager: four 16 KB windows
(`#0000`, `#4000`, `#8000`, `#C000`), each backed by any RAM page. The ATM has
**two sets of window registers**, selected by bit 4 of port `#FD` (`#7FFD`):
the kernel uses one set, every task the other.

| Window | Task view (user set) | Kernel view (system set, on `CALL #0005`) |
|---|---|---|
| `#0000` | the task's **main page** (`app.mainpg`): user kernel, command line, program start at `#0100` | **pgsys**: kernel code and data (`syscode`) |
| `#4000` | task page (`curpg16k`, byte `#44` of the main page) | pgkillable, pgfatfs, pgtrdosfs (drivers), per call |
| `#8000` | task page (`curpg32klow`, byte `#4A`) | pgkillable, or the task's page via `BDOS_setdepage` |
| `#C000` | task page (`curpg32khigh`, byte `#50`) | pgkillable, pgfatfs2 (structures), ... |

Page values are **port values**: physical page = value XOR `#FF`
(`pagexor`, `atm=1`). With `TOPDOWNMEM=0` the system pages are fixed:

| Role | Port value | Physical page | Holds |
|---|---|---|---|
| pgsys | `#F5` | 10 | kernel code + task table + page owners + pipes |
| pgtrdosfs | `#F7` | 8 | idle task, TR-DOS FS, **W5300 driver** (`#4000` window), resident |
| pgfatfs | `#F6` | 9 | FatFS code |
| pgfatfs2 | `#F4` | 11 | FatFS structures: volumes, **open files** (`#C000` window) |
| pgkillable | `#FB` | 4 | scratch page the kernel may destroy |
| screens | `#FE` / `#FA` / `#FC` / `#F8` | 1 / 5 / 3 / 7 | pgscr0_0 / pgscr0_1 / pgscr1_0 / pgscr1_1 |

**Consequence (verified).** When the CPU is inside the kernel, the emulator's
current CPU view (`/state/paging`, `/memory/{addr}`) does **not** show the
task's memory: at the kernel entry the task's `#8000` window read page 4
(pgkillable), and the user stack word came back as `00 00`. Every task-memory
access must translate through the task's own window bytes (§4.3).

## 2. Symbols

- The kernel is assembled by sjasmplus (`src/kernel/main.asm`, flags written to
  `src/_sdk/syssets.asm` by `build_kernel_*.bat`). The build writes a flat label
  list to `us/user.l` (`:ADDR name`, 1767 labels for the Evo build).
- `user.l` has **no page information**, and the kernel is assembled in several
  address spaces (`disp` blocks): syscode at `#0000` (pgsys), the user kernel at
  `#0000` (every main page), the resident at `#6000`, the W5300 driver at
  `#4F18..` (pgtrdosfs), structures at `#C000` (pgfatfs2). The same number means
  different things in different pages.
- `sjasmplus --sld=main.sld` writes the compile page for every label
  (`w5300.asm|141||0|0|20322|F|wiznet_open`: compile page 0;
  `syskrnl.asm|865||0|4|2903|F|callbdos`: compile page 4). Compile pages map to
  runtime roles through `main.asm` (`COMPILEPG_INIT=0`, `COMPILEPG_SYS0=4`,
  `COMPILEPG_SYS1=6`) and the `disp` target of each block. The layer's symbol
  file therefore stores `name, address, role` (role = pgsys, userkernel,
  pgtrdosfs, pgfatfs2, resident, ...), generated once per build.
- **Reproducible on macOS/Linux.** sjasmplus builds from
  `tools/src/sjasmplus` (`make USE_LUA=0`). FatFS needs IAR, which is not
  available; its exports already sit in `src/fatfs4os/fatfs.exp`, and
  `ffsfunc.asm` is generated from them
  (`ffsfunc` + `.<name> EQU 0x<addr>` per line). Then
  `sjasmplus --nologo --msg=war --sld=main.sld main.asm` (after
  `ngsinst.asm`) produces `syscode.c`, `initcode.c`, `us/user.l`.
- **The rebuilt kernel matches the running one.** Comparing `syscode.c` with
  live page 10 over `#0000..#33FF`: every differing byte is a variable
  (`sys_timer`, `appaddr`, the task table `#0100..#0791`, `tsys_pages`, pipe
  buffers) or a self-modified operand (`callbdos+#18`, `callbdos_sp`,
  `BDOS_*+n` ...). No instruction byte differs.

**Self-modifying code.** The kernel keeps much of its state in instruction
operands, declared `label=$+1`: `appaddr` (the `ld sp,nn` in `sys_intgo`),
`focusappaddr`, `callbdos_sp`, `sys_curpg4000`, `BDOS_readhandle_pipe_addr`,
`curpg16k/32klow/32khigh` in each main page. Build identification (NK-2)
must mask these operands (every `=$+1` / `=$+2` label, width from the
instruction) before hashing, or hash the kernel file on the boot medium
instead.

## 3. Kernel anchors (Evo build `INETDRV=1`, `PS2KBD=1`)

| Name | Where | Address | Meaning |
|---|---|---|---|
| `callbdos` | pgsys | `#0B57` | kernel entry for every `CALL #0005` (via `jp callbdos` at `#0009` of pgsys) |
| handler operand | pgsys | `#0B6F` (word) | the `call nn` the dispatcher patched: handler of the current / last command |
| `callbdos_sp` | pgsys | `#0B79` (word) | the calling task's SP at the kernel entry |
| `tbdoscmds` | pgsys | `#3400` / `#3500` | handler address per command: low bytes / high bytes, indexed by `C` |
| `sys_timer` | pgsys | `#002E` (4 bytes) | frame counter; the scheduler compares its low byte with `app.lasttime` |
| `appaddr` | pgsys | `#0786` (word) | current task (`app` address) |
| `focusappaddr` | pgsys | `#0952` (word) | task with screen and keyboard |
| `safestack` / `app1` | pgsys | `#0100` / `#0112` | task table: 16 slots of `app_sz = #68` |
| `tsys_pages` | pgsys | `#2B29` | page owner per **physical** page, `sys_npages = 192` bytes: 0 free, `#FF` system, else task id |
| `freepipes` / `pipeowners` / `pipetypes` | pgsys | `#2565` / `#256D` / `#2575` | 8 pipes: open sides, reader task, terminal lines |
| `pipebufs` | pgsys | `#2BE9` | 8 x 256: `[size][255 data]` |
| `BDOSSTACK` | - | `#4000` | the **single** kernel stack (grows down in pgsys) |
| `w53_socflags` (+1) | pgtrdosfs | `#4F1A` | 8 sockets x 5 bytes (§4.6) |
| `wizlocalport` | pgtrdosfs | `#4F18` | next local port (`#C000` + n) |
| `ffilearray` | pgfatfs2 | `#DC97` | 16 x `FIL` (`#220` bytes each) |
| `COMMANDLINE` | main page | `#0080` | the task's command line, NUL-terminated (128 bytes) |
| `curpg16k` / `curpg32klow` / `curpg32khigh` | main page | `#0044` / `#004A` / `#0050` | the task's window values for `#4000` / `#8000` / `#C000` |

## 4. Structures

### 4.1 Task slot (`safestack` + `app`, `#68` bytes, 16 slots from `#0100`)

```
slot base = #0100 + n * #68        app = slot base + #12

-#10  word af'     \
-#0E  word ix       |  frame pushed by sys_intgo when the task was preempted
-#0C  word hl'      |  (also the frame YIELD leaves); read-only for the layer
-#0A  word de'      |
-#08  word bc'      |
-#06  word iy       |
-#04  word hl       |
-#02  word sp      /   task SP; the task's own stack holds de, bc, af, pc (§4.2)
+#00  byte flags        bit0 factive, bit1 fchildfinished, bit5 fgfx, bit7 fwaiting
+#01  byte id           0 = free slot
+#02  byte parentid
+#03  byte mainpg       port value of the main page
+#04  byte stdin        #80+n = pipe n, else file handle
+#05  byte stdout
+#06  byte stderr
+#07  byte lasttime     low byte of sys_timer when last scheduled
+#08  byte border
+#09  byte screen       fd_user + 8*screen
+#0A  byte gfxmode      value for #BD77
+#0B  byte gfxkeep      bit7 = the task keeps its own screen pages
+#0C  byte scr0low, scr0high, scr1low, scr1high   screen pages (port values)
+#10  word childresult
+#12  word textcuraddr
+#14  byte curcolor
+#15  word dta
+#17  byte vol          current drive
+#18  dword dircluster  current directory
+#1C  DIR[#1A] dir      directory read buffer
+#36  pal[32]
```

The same layout as C++ (the layer reads bytes, never casts host structs):

```cpp
// core/src/debugger/analyzers/nedoos/nedooskernel.h (sketch)
struct NedoOsTask
{
    uint8_t slot = 0;         // 0..15
    uint16_t appAddress = 0;  // in pgsys
    uint8_t flags = 0, id = 0, parentId = 0, mainPage = 0;  // mainPage: port value
    uint8_t stdIn = 0, stdOut = 0, stdErr = 0, lastTime = 0;
    uint8_t border = 0, screen = 0, gfxMode = 0, gfxKeep = 0;
    uint8_t screenPages[4] = {};
    uint16_t childResult = 0, textCursor = 0, dta = 0;
    uint8_t volume = 0;
    uint32_t dirCluster = 0;
    uint8_t windows[4] = {};  // port values seen by the task at #0000/#4000/#8000/#C000
    std::string commandLine;  // main page #0080
    NedoOsSavedFrame frame;   // af', ix, hl', de', bc', iy, hl, sp (+ de, bc, af, pc from the task stack)
};
```

### 4.2 Where a task stopped

- A task that is **not running** was left by the interrupt handler or by
  `YIELD`: SP is in the slot frame (`-#02`), and the task's stack (read through
  its windows) holds `de, bc, af, pc` (pushed by the user interrupt entry at
  `#0038` of the main page). Verified: `nv.com` at `pc=#3AEF`, `cmd.com` at
  `#05DF`.
- The **current** task (`appaddr`) is running now: its registers are the CPU's,
  its frame in the slot is stale (zxdb's frame said `pc=#8300`, the CPU was at
  `#5183`).
- A task **inside the kernel** is the current task while bank 0 is pgsys
  (page 10). Its call: `C` at entry, the handler from the patched operand
  (`#0B6F`) mapped back through `tbdoscmds`, the caller from `callbdos_sp` and
  the word at that SP (in the task's windows) = return address.

### 4.3 Task address translation

```python
def task_read(task, addr, n):
    out = bytearray()
    for k in range(n):
        a = (addr + k) & 0xFFFF
        phys = task.windows[a >> 14] ^ 0xFF        # pagexor, atm=1
        out.append(ram_page(phys)[a & 0x3FFF])
    return bytes(out)

# task.windows = [app.mainpg, main[0x44], main[0x4A], main[0x50]]
```

### 4.4 Page owners

`tsys_pages[phys]`: 0 free, `#FF` system, else the owning task id. Live: 17
system pages, 156 free, task 2 owns 12-13, task 3 owns 14, task 4 owns 15-16,
25-26, 32-35, task 6 (zxdb) owns 17-24. Checked against a kernel call (§7.3).

### 4.5 Pipes (console plumbing)

Handles `#80..#87`. `freepipes[i]` = open sides (2 both, 1 one closed),
`pipeowners[i]` = the reader (woken on write), `pipetypes[i]` = terminal
lines (from the pipe name, e.g. `a25`), `pipebufs[i]` = `[size][data...]`,
the queue always at the start. Live: `#80` read by term (2), `#81` read by
nv (4), both empty, 25 lines.

### 4.6 Sockets (W5300 driver, `INETDRV=1`)

```
w53_socflags+1 + 5*i, i = 0..7      socket handle returned to programs = 1 + 5*i
  +0 RX register state     +1 W5300 socket number + 8
  +2 word RX count         +4 owner task id (0 = free)
```

The kernel keeps **only** owner and RX counters. Protocol, state, local and
remote address and port live in the chip (`Sn_MR`, `Sn_SSR`, `Sn_DIPR`,
`Sn_DPORTR`) and are written straight to ports. So the socket view (NK-14)
comes from the emulated adapter plus the call trace (arguments of
`WIZNETOPEN` connect / `WIZNETWRITE`), not from kernel memory. Live: handle 1
owned by zxdb, `wizlocalport = #C001`.

### 4.7 Open files (FatFS)

`ffilearray` in pgfatfs2 (map it at `#C000` window, as `BDOS_setpgstructs`
does), 16 x `FIL`:

```
+#00 word FS (pointer to FATFS)  +#02 word ID  +#04 byte FLAG
+#05 byte PAD1 = owner task id (used by BDOS_dropapp to close files)
+#06 dword FPTR  +#0A dword FSIZE  +#0E dword FCLUST  +#12 dword CLUST
+#16 dword DSECT  +#1A dword DIR_SECT  +#1E word DIR_PTR  +#20 BUF[512]
```

A `FIL` has **no name**. The directory entry is at sector `DIR_SECT`, offset
`DIR_PTR - (FS + 51)` (51 = offset of `win` in `FATFS`). The window usually
holds another sector by the time we look (live: `winsect = 2403`,
`DIR_SECT = 2438`), so the name comes from reading `DIR_SECT` on the medium.
Live: file 0, owner cmd.com (3), 79 of 79 bytes read = `bin/autoexec.bat`
(matched by size on the card).

## 5. Kernel calls

- Entry: the task does `ld c,CMD; call #0005`. The user kernel at `#0005`
  switches `#FD` to the system set; pgsys `#0009` jumps to `callbdos`.
- `callbdos` saves SP in `callbdos_sp`, switches to `BDOSSTACK` (`#4000`),
  dispatches through `tbdoscmds[C]` by patching `call nn` at `#0B6E`, then
  returns through `endsys_result_a`.
- `bdosstack_sz = 0`: no per-task kernel stack, **no mutex**. The kernel is
  not reentrant: one `callbdos_sp`, one `BDOSSTACK`.
- **The kernel is not preemptive.** An interrupt while the CPU is in the
  kernel goes to `sys_sysint`: it ticks the timer, reads the keyboard, sets the
  palette and returns to the same place. No task switch. Verified: over one
  second in the hang, `sys_timer` went 33 -> 84 while every task's `lasttime`
  stayed put.
- Command numbers: `src/_sdk/sysdefs.asm`; the arguments and results per
  command are in the macro comments of `src/_sdk/sys_h.asm`
  (e.g. `OS_GETAPPMAINPAGES ;e=id ;out: d,e,h,l=pages in 0000,4000,8000,c000, c=flags, b=id, a=error`).
  The layer ships a table generated from these two files.

## 6. The zxdb case, read with the layer (2026-09-30)

What the prototype printed, then how it reads:

```
id=1 parent=0 inactive    cmd='idle'
id=2 parent=1 active,gfx  cmd='term.com cmd.com autoexec.bat'
id=3 parent=2 inactive    cmd='cmd.com autoexec.bat'
id=4 parent=3 active      cmd='M:/bin/nv.com'
id=6 parent=1 active,gfx  cmd='zxdb'   CURRENT FOCUS
kernel: in CMD_WIZNETWRITE (#DE) for task 6, caller pc #647A (returns to #647D), user sp #8675
cpu: pc #5183 = w53_cmd0 (pgtrdosfs), bc=#03AB, a=#FF
socket 1: owner 6 (UDP by the driver's view: Sn_MR reads #FF, bit 1 set)
data: 31 bytes at task 6 #88E3 = DNS query id #1122, A next.zxart.ee
scheduler: frozen (kernel busy; sys_timer runs, no task scheduled)
```

1. zxdb (task 6) resolves `next.zxart.ee`: it opens a socket and sends a
   31-byte DNS query with `CMD_WIZNETWRITE`; the HTTP request
   (`GET ... Host: next.zxart.ee`) is already built at `#C1B0`.
2. There is no W5300: every port of the `#xxAB` family reads `#FF`. The driver
   takes `Sn_MR = #FF` for UDP, `Sn_SSR = #FF` for "open", free space
   `#FFFF`, copies the query, writes `SEND` and waits in `w53_cmd0` for
   `Sn_CR` to read 0. It never does, and there is no timeout.
3. The kernel is not preemptive, so the whole OS stops, not only zxdb: no
   other task runs and zxdb cannot see Esc. Only the timer and the clock
   move.

The first draft of the requirements guessed this as "zxdb spins in its own
loop after the calls returned". The live read shows the call never returned.
This is why NK-21 and NK-22 exist.

## 7. Operations tried live

### 7.1 Kernel call trace (NK-15)

An execution breakpoint at `#0B57`, filtered on bank 0 = page 10 (the plain
address also matches user code at `#0B57` of any main page), then reading `C`
and `appaddr` at each stop, then resuming. It needs debug mode
(`PUT /debugmode {"enabled":true}`); without it the breakpoint never fires.
150 calls in about 40 s:

| Task | Calls |
|---|---|
| term (2) | `CHECKPID`, `READHANDLE` (pipe), `YIELD` - its idle loop |
| nv (4) | `READHANDLE`, `YIELD` |
| zxdb (6) | `YIELD` |

Round trips through the WebAPI give about 4 calls per second. The real trace
needs a core hook (a page-qualified execution hook on `callbdos` and on the
return path), not API breakpoints.

### 7.2 Unsticking a stuck call (NK-22)

With the machine paused in `w53_cmd0`, setting `PC = wiznet_fail_1` (`#5331`:
`ld a,ERR_NOTCONN : ld h,-1 : ret`) made the call return an error to zxdb. The
stack top was `#0B71` (the return into `callbdos`), so a plain `ret` was
right. Result: the scheduler ran again, zxdb printed "No results found", the
clock kept going. The general rule: return from the handler to the address on
`BDOSSTACK` with the command's error convention (`A = error`, `HL = -1` for
the network calls), never jump into the middle of `callbdos`.

### 7.3 Direct kernel call on behalf of a task (NK-11)

The procedure that worked, all through existing WebAPI calls:

```python
# 1. wait for a safe point: task T enters the kernel with CMD_YIELD
bp(callbdos)            ; stop when pc == #0B57, bank0 == page 10, app(appaddr).id == T, C == CMD_YIELD
ret = task_read(T, sp, 2)                     # return address, through T's windows
# 2. let the yield finish; stop back in T's user code
bp(ret)                 ; stop when pc == ret and app(appaddr).id == T
saved = registers()
# 3. call the kernel from there
sp -= 2; write(sp, pc)                          # return to where T was
C = CMD_GETAPPMAINPAGES; E = 4; PC = #0005
# 4. the call returns to pc with the original sp
bp(pc)                  ; stop when pc == saved.pc and sp == saved.sp and task == T
result = registers()    # a=00 b=04 c=01 d=F0 e=DC h=DD l=DF
# 5. restore af, bc, de, hl, ix, iy from saved; resume
```

The result (`#F0 #DC #DD #DF` = physical 15, 35, 34, 32) equals what §4.3 read
from memory for nv (task 4). The system went on normally.

Rules learned:
- A safe point is a task in **user mode** with interrupts on, and no task in
  the kernel (`bank 0 != pgsys`). The kernel is single-entry (§5).
- Stop on the address the task will return to, qualified by task (the same
  address runs in other tasks' pages).
- Registers the kernel promises to preserve are not documented per command;
  the layer restores all of them after the call.
- While the kernel is stuck (§6), there is no safe point. The layer says so
  ("kernel busy in CMD_WIZNETWRITE for task 6 since frame N") instead of
  waiting forever.

## 8. Prototype

The prototype that produced all of the above is POC
[020-nedoos-layer](../../../tools/poc/020-nedoos-layer/): a command-line tool
in Python over the WebAPI (`detect`, `tasks`, `pages`, `pipes`, `sockets`,
`files`, `kernel`, `trace`, `call`, `unstick`) plus `generate-symbols.sh`, which
rebuilds the kernel symbols from a NedoOS checkout. Its
[results](../../../tools/poc/020-nedoos-layer/results.md) are the reference
for the core implementation's tests: each printed fact becomes an assertion on
a TTD recording of the same session.

## 9. Emulator gaps found on the way

| Gap | Needed for |
|---|---|
| `/state/paging` shows only the active ATM register set, not the other one | NK-6 (task windows while in the kernel) |
| Page-qualified execution breakpoints exist in the core (`AddExecutionBreakpointInPage`) but not on the WebAPI / MCP | NK-15, NK-16 |
| No sector read for block media (SD, IDE); only floppies have `/disk/{drive}/sector` | NK-13 (file names from `DIR_SECT`) |
| Breakpoints need debug mode; the layer must turn it on itself and say so | NK-15, NK-16 |
| `GET /memory/ram/{page}/{offset}` accepts a **negative offset** and ignores `len` then, returning host process memory (seen with offset `-16384`) | bug, fix separately |
