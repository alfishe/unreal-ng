# 020 - Results (2026-09-30)

Setup: unreal-ng master, ZX-Evo (`ATM3`), full NedoOS SD card
(`testdata/machines/zxevo/nedoos/sdcard-full`, kernel `sd_boot.$C`, NedoOS
44049473 / SVN r2698), NedoOS running `term`, `cmd`, `nv` and `zxdb`. All
output below is copied from the tool, run against that machine.

## 1. Symbols (P-2)

`generate-symbols.sh <checkout> evo <out>` builds sjasmplus from the NedoOS tree,
assembles the kernel with the Evo flags and writes 1767 labels. Rebuilt twice:
the label file is byte-identical to the committed one.

Comparing the rebuilt `syscode.c` with live RAM page 10 over `#0000..#33FF`:
every difference is a variable or a self-modified operand (`sys_timer`,
`appaddr`, the task table, `tsys_pages`, `callbdos_sp`, the patched
`call nn` in `callbdos`, `BDOS_*+n` operands, pipe buffers). No code byte
differs.

## 2. Read-only views (P-1, P-3 .. P-5)

```
$ ./nedoos-layer.py --url http://localhost:8197 all
NedoOS kernel (symbols evo-inetdrv1-44049473): yes
  ok  #0004 jp sys_quit
  ok  #0009 jp callbdos
  ok  #0038 jp sys_sysint

== tasks
id par state                  pages(0/4/8/C)  pc    sp    cmdline
 1   0 inactive               [08 0C 0D 0D]   cpu   cpu   'idle' CURRENT
 2   1 active,gfx             [0C 0D 04 04]   01E3  3FF8  'term.com cmd.com autoexec.bat'
 3   2 inactive               [0E 0F 10 10]   05DF  3FEA  'cmd.com autoexec.bat'
 4   3 active                 [0F 23 22 20]   3AEF  3FF2  'M:/bin/nv.com'
 6   1 active,gfx             [11 12 13 14]   6244  86B3  'zxdb' FOCUS

== pages
free     156 pages  [36, 37, 38, ...]
task 2     2 pages  [12, 13]
task 3     1 pages  [14]
task 4     8 pages  [15, 16, 25, 26, 32, 33, 34, 35]
task 6     8 pages  [17, 18, 19, 20, 21, 22, 23, 24]
system    17 pages  [0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 27, 28, 29, 30, 31]

== pipes
pipe #80: open sides 2, reader task 2, 25 lines, queued 0 bytes b''
pipe #81: open sides 2, reader task 4, 25 lines, queued 0 bytes b''

== sockets
next local port #C001
(protocol, state and addresses live in the chip only)

== files
file  0: owner task 3, 79/79 bytes, dir entry in sector 2438 at +#20 (name needs a sector read)
```

Reading it:
- Task 1 is the kernel's idle task (in pgtrdosfs). Pages are physical numbers.
- `nv` was started by `cmd`, which was started by `term`. `zxdb` (launched
  from nv) has parent 1, the idle task: how the kernel records the parent of
  a program started from nv is not checked yet.
- The open file is `bin/autoexec.bat` (the only 79-byte file on the card), held
  by `cmd`. The name itself is not in kernel memory.
- Where each task stopped comes from its slot frame plus its own stack, read
  through its own windows.

## 3. The zxdb hang (P-4, P-6, P-9)

In zxdb: `S`, type `dizzy`, Enter. The screen stays at "Sending request...".

```
$ ./nedoos-layer.py --url http://localhost:8197 kernel
== kernel
CPU IN KERNEL for task 6 (zxdb): CMD_WIZNETWRITE (#DE)
  handler BDOS_wiznetwrite+#0, called from #647A, user sp #8675
  cpu pc #5187 = w53_cmd0+#4, bc=#03AB, a=#FF

$ ./nedoos-layer.py --url http://localhost:8197 sockets
next local port #C002
socket  1: owner task 6, chip socket 0, rx 0
```

- zxdb sends a 31-byte DNS query (`A next.zxart.ee`, id `#1122`, at task 6
  `#88E3`, found through the task windows) on socket 1.
- There is no W5300: the driver reads `#FF` from every register, takes the
  socket for UDP, copies the data, writes `SEND` and waits for `Sn_CR`
  (`#03AB`) to read 0. No timeout.
- The kernel is not preemptive: over one second `sys_timer` advanced 51 frames
  while no task was scheduled. The whole OS stands still.

```
$ ./nedoos-layer.py --url http://localhost:8197 unstick
ended CMD_WIZNETWRITE (#DE) of task 6 with ERR_NOTCONN
```

zxdb then shows "No results found" and the system runs normally. The tool
checks first that the kernel stack top is the return into `callbdos`, so the
handler's own error exit (`wiznet_fail_1`) returns cleanly.

## 4. Changing the machine (P-7, P-8, P-10)

```
$ ./nedoos-layer.py --url http://localhost:8197 trace 40
task 2 (term.com): CMD_READHANDLE x7
task 2 (term.com): CMD_YIELD x7
task 4 (M:/bin/nv.com): CMD_READHANDLE x7
task 4 (M:/bin/nv.com): CMD_YIELD x7
task 6 (zxdb): CMD_YIELD x6
task 2 (term.com): CMD_CHECKPID x6

$ ./nedoos-layer.py --url http://localhost:8197 call 6 GETAPPMAINPAGES 4
a=#00 f=#44 bc=#0401 de=#F0DC hl=#DDDF ix=#86A1 iy=#031A
$ ./nedoos-layer.py --url http://localhost:8197 call 6 GETPAGEOWNER 17
a=#11 f=#28 bc=#00CB de=#0006 hl=#2B3A ix=#86A1 iy=#031A
$ ./nedoos-layer.py --url http://localhost:8197 call 2 GETTIMER
a=#00 f=#42 bc=#0BF1 de=#0001 hl=#11BB ix=#0000 iy=#031A
```

- `GETAPPMAINPAGES 4`: `D E H L` = `#F0 #DC #DD #DF` = physical 15, 35, 34, 32,
  the same windows `tasks` read from memory for nv; `B` = 4, `C` = flags 1.
- `GETPAGEOWNER 17`: `E` = 6, as in `pages`.
- `GETTIMER` from term: `DEHL` = `#000111BB` frames.
- Each call waited for the task's next `YIELD`, called `#0005` from the task's
  own context, caught the return by task + PC + SP, restored every register.
  The tasks carried on; the screen and clock kept updating.
- Trace speed: 40 calls in about 10 s. Enough to see who calls what, far too
  slow for a real trace.

## 5. Build differences (why symbols are per build)

The ESP kernel (`generate-symbols.sh ... evo-esp`, 1686 labels) keeps the task
table, `callbdos`, `appaddr`, `focusappaddr` and the FatFS structures where the
Wiznet kernel has them, but moves everything after the network driver:

| Label | Wiznet kernel | ESP kernel |
|---|---|---|
| `tsys_pages` | `#2B29` | `#2259` |
| `pipebufs` | `#2BE9` | `#2319` |
| `tbdoscmds` | `#3400` | `#2E00` |
| `w53_socflags` | `#4F19` | - (ESPNET has its own socket state, `espk_socket`, `esp_socket`) |

## 6. Emulator issues found on the way

| Issue | Where it matters |
|---|---|
| `/state/paging` shows only the active ATM register set | task memory while in the kernel |
| page-qualified breakpoints exist in the core, not on WebAPI / MCP | trace, per-task breakpoints |
| no sector read for SD / IDE media | open file names |
| breakpoints fire only in debug mode | every control operation |
| `pause` / `resume` answer 400 when already in that state | clients must check first |
| `GET /memory/ram/{page}/{offset}` with a negative offset returns host process memory | bug |

## 7. Conclusions

- Everything the layer needs is reachable from outside the machine, with the
  build's symbols and the page roles. No cooperation from NedoOS is needed.
- Three things are not in kernel memory and need other sources: program names
  (only the command line), open file names (directory entry on the medium),
  socket addresses and state (the network adapter).
- Direct kernel calls work with a simple, checkable safe-point rule; the
  kernel's single entry and missing preemption are the limits, and they are
  exactly what the busy report must explain.
- The core implementation needs a page-qualified hook at `callbdos` and its
  exit; WebAPI breakpoints are two orders of magnitude too slow for a trace.
