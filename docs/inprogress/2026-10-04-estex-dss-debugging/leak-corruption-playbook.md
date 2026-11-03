# Playbook: catching leaks and corruption of DSS system resources in the debugger

Part of research [#99](../PLAN.md). The answer to a "question from the audience": log what happens,
shout to the console when OS memory was not returned, and catch every kind of leak or corruption.
It assumes you have read [dss-system-state.md](dss-system-state.md); the structure facts come from there
(mechanics per 1.70b, anchors of the running 1.71.66 from `system.exp`).

General principle (the same as the NedoOS layer [#78](../2026-09-30-nedoos-integration/README.md) and
Phase 6 [#51](../2026-09-21-devtools-roadmap/unreal-ng-developer-toolchain-design.md) §14):
the emulator knows nothing about DSS "in code": there is an **OS descriptor** (the `dss.yaml` draft in §8)
and a **check engine**. Every trap or violation is an event with the caller's `pc`, the arguments, the
result and the TTD position; violations are raised as findings to the console (CLI/Qt log),
on the TTD timeline and on every automation surface (WebAPI/MCP/Lua/Python): the
"checks → findings" model from the [bus analyzer](../2026-10-11-bus-analyzer/README.md).

Three basic mechanisms that everything else is built from:

1. **Syscall traps**: a BP on `0x0010` (DSS, function number in `C`) and `0x0008` (BIOS, command in `C`/`BC`)
   with argument decoding. Conditional breakpoints (#6) and the event stream (#23) are already on master.
2. **The ledger**: for every alloc/free/open/close, a record of "who (TASK/PC), what, when (TTD)".
3. **Process life points**: EXEC (`#40`), LEAVE (`0x0000`/`#41`), GSWITCH (`#43`): balances are checked
   and deltas taken there. An analogue of the known idea "QUIT (0x0000) is meant to be used
   as a breakpoint in the debugger" from the NedoOS documentation (`src/nedoos.txt`, cp866).

---

## A. A DSS block not returned by a live task (accumulating leak)

Symptom: the FM / a resident runs, INFOMEM (#3C) shows fewer and fewer free pages,
and after the panels are closed the memory has not come back.

Recipe:

- trap `0x0010` with `C=#3D` (GETMEM): record `TASK`, `B` (size), the result `A` (block ID), `PC`;
- trap `0x0010` with `C=#3E` (FREEMEM): write off the ID;
- trap `0x0010` with `C=#3F` (SETMEM): update the size;
- **shout to the console**: on every LEAVE (or on demand), the list of the current task's blocks
  that it did not return itself (LEAVMEM will clean them up, but that is already a signal); for a live task,
  a threshold: total pages and block lifetime.
- block → page map: walk BIOS `#C4` (like `SIZEBLK`, `EXECUTE.ASM:456-469`) or
  use the emulator's own knowledge of the allocator.

Event format: `DSS LEAK? task=3 leaves block id=7 (2x16K, taken @pc=0x8123, ttd=123456)`.

## B. A permanent leak at the BIOS level (the very "OS memory was not given back")

The DSS owner table does not help here: the pages are taken in the BIOS, MEMTAB is clean, INFOMEM
has shrunk for good. Sources: a direct `RST #08` with `C=#C2` (bypassing GETMEM), `BC=#04C2`
("GET DSS PAGES HANDLE": this is how DSS takes its own pages, `DOS-MAIN.ASM:351-368`),
BIOS handles of residents, a forgotten `#04C2` handle.

Recipe:

- trap `0x0008` on commands `#C2/#C3/#C4/#9D/#9E` and `BC=#04C2`: a BIOS-level ledger
  (plus TODO: the full list of memory commands from `Shared_Includes/Docs/BIOS functions.asm`);
- **exit invariant**: take `BC` (free) from INFOMEM on entry to `0x0000` (LEAVE) and after
  returning to the parent; `free_after < free_before` → finding "task N leaked N×16K of OS memory";
- check the delta on GSWITCH (task switch) as well: a resident must keep its balance;
- emulator poison mode: fill freshly allocated BIOS pages with a pattern (0xA5); the first
  task that reads the 0xA5 pattern reads someone else's or uninitialized memory.

## C. File handle leaks

LEAVE does not close the task's files (see dss-system-state §4): handles with the `FTASK` of a dead task
stay in the FM forever, and CLOSE will no longer take them (error 19 "not the owner").

Recipe:

- trap `#11/#12` (OPEN/CLOSE): balance per `TASK`;
- **on LEAVE**: scan the FM table for `FTASK==current task` → finding with the file name,
  the FP position and the PC of the last OPEN (from the ledger): `task 3 exits leaving handle 'LOG.TXT' open`;
- scan for "dangling" entries: `FTASK != 0` and not matching a live task → garbage in the FM
  (this also catches corruption of the FM entry itself);
- for the new FM this is the main regression test: a "launch → copy → exit ×N" loop
  with no loss of free handles.

## D. Corruption of the system zones 0000-3FFF (vectors, MEMTAB, ADRST10, FM)

Recipe:

- **watchpoints** with a context filter: writes to 0000-00FF, 0100-01FF, 0200-03FF,
  the FM area, EXSTACK from code whose `PC` does not belong to the system pages →
  an immediate finding with the PC and the value (the cheap variant: the page-ownership trigger from §E,
  since 0000-3FFF is the PAGE0 system window);
- **checksum analyzer**: a reference of the vectors / ADRST10 / system page code is taken after
  DSS has loaded and checked in pauses / at life points; a mismatch → diff highlighting
  (like the analyzers on master);
- MEMTAB invariant: a non-zero entry must match a live BIOS-level block, otherwise
  either the table is corrupted or a BIOS handle was lost (class B);
- a separate watch on `LAST_PSP_PTR`/`TASK`: an application replacing the "current task".

## E. Writes to someone else's pages (page-ownership violation)

A direct analogue of Phase 6 §14.2 ("the dominant bug class of a multitasking OS on a Z80 without memory
protection"). Owner map: system pages + MEMTAB blocks (block boundaries via `#C4`)
+ the PSP of the current process (`PSP[-2]` = the image block). Trigger: an `M1` write (TTD already records all
memory events) to a page whose owner is not the current task → finding. The same the other way round:
a read from an uninitialized (poison) page.

## F. Windows and stacks: out of sync after EXEC/LEAVE

Recipe: on EXEC, a snapshot of the PAGE1-3 ports and SP; on return from LEAVE, a check against the
parent's reference (`LEAVE` restores the windows from the PROCESS STACK BUFFER, `EXECUTE.ASM:1081-1092`).
A mismatch → "task N corrupted window state". Separately: a task stack overflow into the
dedicated PROCESS STACK BUFFER (2EAF-2FAE, 256 bytes).

## G. "A less greedy FM": how to prove it is less greedy

Metrics of one session (collected for free from the ledger):

- peak and final number of pages in use (by lifetime, on the TTD timeline);
- a table: block → owner → PC of allocation → PC of return → pages;
- INFOMEM delta: before launch / at the peak / after exit (after LEAVE it must equal "before");
- number of FM handles in use at the peak and "left after exit" (must be 0);
- lifetime of resident blocks (GSWITCH sessions).

A technique from the bus analyzer: **mutation tests**: mutants are planted in the FM on purpose
(FREEMEM removed; CLOSE removed; SETWIN of someone else's block; a write to 0x0100), and each detector must
catch one. A detector that does not catch its mutant catches nothing.

## H. Shouting to the console: what it looks like

- **CLI/Qt log**: `warn DSS: task 3 (FM.EXE) exited leaving 1 block (16K) @pc=0x8123`;
  the events already go through the event stream (#23) and diagnostic observability;
- **TTD**: every event tags its position; "block id=7 leaked" → right click → reverse to
  the last `#C2/#3D` that created this block (reverse-search index, 2026-08-20) → the culprit's PC;
- **MCP/Lua**: a script runs the scenario and collects findings and deltas, so a regression is caught
  in CI, not by eye:
  ```lua
  local before = dss.infomem()            -- { total=..., free=... }
  run("fm.exe"); wait_exit()
  local findings = dss.findings()          -- {leaks=..., handles=...}
  assert(#findings.leaks == 0 and dss.infomem().free == before.free)
  ```

## 8. Draft of the `dss.yaml` descriptor (Phase 6 style)

```yaml
os: EstexDSS
version_range: ["1.60", "1.71"]
symbols: system.exp                # CORE_BUFFERS.MemoryTable / FM_BUF / XSTACK ... + system.sym
syscall_trap:
  - { address: 0x0010, function_reg: C, table: { lo: 0x0200, hi: 0x0300 } }  # RST #10 DSS
  - { address: 0x0008, function_reg: C }                                     # RST #08 BIOS
memory:
  owner_table: { symbol: CORE_BUFFERS.MemoryTable, size: 256, value: task_id } # 1.70b: 0x0100; 1.71.66: 0x38EB
  alloc:  { dss: 0x3D, bios: 0xC2, size_reg: B, id_reg: A }
  free:   { dss: 0x3E, bios: 0xC3, id_reg: A }
  info:   { dss: 0x3C, free: BC, total: HL }
  resize: { dss: 0x3F }
  setwin: { dss: [0x38, 0x39, 0x3A, 0x3B], bios: 0xC4 }
  windows: [ { port: 0x82, addr: 0x0000 }, { port: 0xA2, addr: 0x4000 },
             { port: 0xC2, addr: 0x8000 }, { port: 0xE2, addr: 0xC000 } ]
files:
  handle_table: { symbol: CORE_BUFFERS.FM_BUF, count: 10, stride: 54, owner_off: 42 }  # 1.70b: stride 44
  open: 0x11
  close: 0x12
process:
  exec: 0x40
  exit: { dss: 0x41, vector: 0x0000 }
  gswitch: 0x43
  psp: { last_ptr: LAST_PSP_PTR, task_off: -1, mem_block_off: -2, exe_handle_off: -3 }
checks:                            # each → a finding with pc + ttd
  - exit_balance_memory           # free before/after LEAVE (class B)
  - exit_closes_files             # FTASK scan on LEAVE (class C)
  - owner_table_consistency       # MEMTAB ↔ BIOS blocks (B, D)
  - system_zone_checksum          # 0000-3FFF reference zones (D)
  - page_ownership_write          # write to someone else's / a system page (E)
  - window_restore                # windows after LEAVE == before EXEC (F)
```

What the descriptor deliberately does not describe (yet): 16-bit BIOS codes like `#04C2`,
FAT32 file system specifics and address differences between versions; these are covered by the `version` +
`symbols` fields (the export of each build: 1.70b, the `DSS_MAP.TXT` map; 1.71.66, `system.exp`),
see TODO.
