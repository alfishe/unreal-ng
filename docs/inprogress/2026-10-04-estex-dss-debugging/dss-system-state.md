# Estex DSS system state from the sources: what to guard and what to check against

Part of investigation [#99](../PLAN.md) (the first item of the [TODO](TODO.md): "the DSS memory model, call
mechanics, structures and addresses"); answers the question from the floor about leaks, see
[leak-corruption-playbook.md](leak-corruption-playbook.md).

References with paths and line numbers point to the official sources
`/Volumes/TB4-4Tb/Projects/emulators/gitlab/sprinter-computer-dos` (Estex DSS v1.70 beta,
short name **DSS** in [materials.md](../2026-09-28-sprinter/materials.md)). Sources by their
short names from there: **DSS** (official sources 1.52-1.70), **DSS-162**
(`github/SprinterFirmware`, reconstructed), **INC** (`zxgit/Shared_Includes`:
`constants/SP2000.inc`, `constants/BIOS_equ.inc`, `Docs/BIOS functions.asm`,
`Docs/DSS_1.71_Functions.asm`), **BIOS-TT** (`zxgit/Sprinter-BIOS`), **MAN/DOC-RU**
(the Ivan Mak manual / doc.sprinter.ru).

**Important about versions.** The `DSS_MAP.TXT` map is the 1.70b layout. The real anchors of the working
build 1.71.66 (the one the emulator runs) come from its export
`system.exp` ([estex-dss-build.md](../2026-09-28-sprinter/estex-dss-build.md) §3,
`testdata/machines/sprinter/dss/1.71.66/`):

```
CORE_BUFFERS.FM_BUF            0x318E   file handles (FM table)
CORE_BUFFERS.FS_Buffer         0x33AA
CORE_BUFFERS.EXEBUFF           0x33EB
CORE_BUFFERS.XSTACK            0x35EB   PROCESS STACK BUFFER (the parent's stack during EXEC)
CORE_BUFFERS.BUFFER            0x36EB   SECTOR_BUFFER
CORE_BUFFERS.MemoryTable       0x38EB   MEMTAB - the memory owner table
CORE_BUFFERS.CurrentDirectory  0x039ED
CORE_BUFFERS.WorkDirectory     0x03AED
```

(addresses are in the kernel's own page, built with `ORG 0`; the full set of names is `system.sym`,
3 640 labels). Below, the mechanics are described for 1.70b, where the addresses are visible right in the code; 1.71 has the same
structures at other offsets, and the detector must take them from the export, not hardcode them.

## 1. OS entry points (where to put traps)

| Point | What it is | Where in the sources |
|:--|:--|:--|
| `0x0010` (`RST #10`) | **DSS function call**: number in `C`, the handler address comes from the `ADRST10` table (low bytes at 0x0200, high bytes at 0x0300) | `DOS-MAIN.ASM:59` (A0010), `:120-128` (RST_10), `:250-299` (table) |
| `0x0008` (`RST #08`) | **BIOS call**: command in `C` (16-bit codes in `BC` also occur, for example `#04C2`); `A0008` switches the BIOS page and does `RET`; the real handler is in the system page | `DOS-MAIN.ASM:53-57`, `:133-138` (ENTER) |
| `0x0000` | `JP LEAVE` - task close (the same as `EXIT` #41); a convenient point for a "balance check on exit" | `DOS-MAIN.ASM:51`, `EXECUTE.ASM:1076` |
| `0x0038` | the main interrupt handler (keyboard etc.) | `DOS-MAIN.ASM:79-108` |
| `0x0030` | `RST #30` - inter-page BIOS call (used by DSS itself) | `DOS-MAIN.ASM:94` |

Functions that matter for resource tracking (numbers from `DSS.INC`, semantics from `docs/r_dsslist.htm`
and the implementations):

| # | Name | Arguments/result | Implementation |
|:--|:--|:--|:--|
| #3C | INFOMEM | → HL=total pages, BC=free (BIOS #C0) | `EXECUTE.ASM:361-363` |
| #3D | GETMEM | B=block size in 16K pages → A=block ID, CF=0; otherwise code 30 | `EXECUTE.ASM:368-380` |
| #3E | FREEMEM | A=block ID; not the owner → code 31 | `EXECUTE.ASM:384-404` |
| #3F | SETMEM | A=ID, B=new size | `EXECUTE.ASM:409-454` |
| #38-#3B | SETWIN/1/2/3 | logical page of a block → window; BIOS #C4 | `EXECUTE.ASM:495-535` |
| #11/#12 | OPEN/CLOSE | file handle | `DOS5.ASM` |
| #40 | EXEC | load and run an EXE | `EXECUTE.ASM` |
| #41 | EXIT (=LEAVE) | B=return code | `EXECUTE.ASM:1076-1096` |
| #43 | GSWITCH | task switching (subfunction 7 `EX_PATH` #45) | `EXECUTE.ASM:104-135, 340` |
| #47 | APPINFO | B=0: command line, 1: path, 2: full name (from the PSP) | `EXECUTE.ASM:31-91` |

BIOS commands that `RST #08` sees (by their use in the DSS sources; the full list is
`Docs/BIOS functions.asm` and `constants/BIOS_equ.inc` in **INC**, see the TODO):

`#C0` - memory statistics; `#C2` - allocate a block of pages (B=size → A=ID);
`#C3` - free a block; `#C4` - get the physical page of a block (A=ID, B=number);
`#9D`/`#9E` - merge blocks on SETMEM; `#04C2` - "GET DSS PAGES HANDLE" - allocate
a handle with pages **bypassing the DSS owner table** (`DOS-MAIN.ASM:351-368`, DEPLOY).

## 2. Map of the low 16K (system page, `DSS_MAP.TXT`)

| Range | Contents | Guard status |
|:--|:--|:--|
| 0000-0038 | RST vectors (0000 = LEAVE, 0008 = BIOS, 0010 = DSS, 0030, 0038) | checksum: unchanged after boot |
| 0066-0068 | NMI | checksum |
| 0080-0085 | inter-page CALL gate | checksum |
| 0086-00C3 | BPB (boot disk parameters) | watch while a task runs |
| **0100-01FF** | **MEMTAB - the memory owner table**: one byte per block ID = task ID (0 = free) | the main OS-aware object; see §3 |
| **0200-03FF** | **ADRST10 - the DSS function address table** | checksum: an application rewriting it → OS hook |
| 0400-043F | keyboard buffer | light watch |
| 0440-0BD1 | keyboard driver; 0BD2-0EF5 screen driver; 0EF6-1100 FAT; 1101-1342 disk I/O | checksum of the system page image |
| 1343-1443 | current directory buffer | watch |
| **1444-16BD** | file handles and file I/O (FM table, see §4) | OS-aware object |
| 25C8-27AC | EXEC procedures; 27AD-2882 memory; 2883-2CAE EXEC | — |
| **2EAF-2FAE** | PROCESS STACK BUFFER (`EXSTACK`) - saved windows/stack of the parent | watch at EXEC/LEAVE time |
| 2FAF-30CB | environment (ENVIRON #46; data on ENVPAGE=2 from #E400, `DOS-MAIN.ASM:30-37`) | — |
| 36E1-370D | DTA buffer | watch |
| 3800-39FF | BUFFER/SECBUF | — |

Damage by task code to any of the "checksum" ranges = damage to system property, class G of the playbook.

## 3. Memory: three levels of ownership

```
BIOS level:     handles of 16K page blocks (RST #08 #C2/#C3/#C4/#04C2), tables in BIOS data
DSS level:      MEMTAB[0100..01FF]: block ID -> task ID (TASK)
window level:   PAGE0=#82 (0000-3FFF, system), PAGE1=#A2 (4000-7FFF),
                PAGE2=#C2 (8000-BFFF), PAGE3=#E2 (C000-FFFF)   [HARDWARE.INC:4-7]
```

- `GETMEM` (#3D): BIOS `#C2` allocates B pages → block ID; then `MEMTAB[ID]=TASK`
  (`EXECUTE.ASM:368-380`). So **the owner in MEMTAB is the only DSS-visible trace of an allocation**.
- `FREEMEM` (#3E): checks `MEMTAB[ID]==TASK` (otherwise error 31 - "not your block"),
  BIOS `#C3`, clears the entry (`EXECUTE.ASM:384-404`).
- `SETWIN` (#38): picks the window by HL (BIT7/BIT6 of H → PAGE1/2/3), BIOS `#C4` turns
  the block's logical page into a physical one, writes the port, returns the old value
  (`EXECUTE.ASM:495-535`).
- **LEAVE semantics** (`EXECUTE.ASM:1076-1096`): save the return code → `LEAVMEM`
  (`:471-486`) - a CPIR pass over the whole MEMTAB and `RETMEM` of **every** block of the current task →
  `TASK--` → restore windows PAGE1/2/3 and SP from the PROCESS STACK BUFFER → `JP (HL)` into the parent.
  Conclusions:
  - the OS forgives "forgot FREEMEM before exit" (LEAVMEM cleans it up), but it is already a symptom
    of carelessness, and the detector should show it as a warning;
  - **a real permanent leak** is pages still taken after LEAVE: BIOS handles taken
    directly (`#C2` bypassing DSS, `#04C2`), or damaged/lost MEMTAB entries.

## 4. File handles (FM table)

`DOS_FM.ASM`: `FMCOUNT=10` handles, in 1.70 an entry is 44 bytes (1.7X: `_sFM` 54 bytes, FAT32 fields).
Key fields: `HND1/2` (+38) handle number, `FDRV` (+40) drive, `AMODE` (+41) mode,
**`FTASK` (+42) - the owner task**; then the FP position, the directory cluster etc.

- `OPEN` writes the current `TASK` to `FTASK` (`DOS5.ASM:280`);
- `CLOSE` requires `TASK==FTASK`, otherwise error 19 (`DOS5.ASM:305-308`);
- **LEAVE neither closes nor checks file handles**: a task exiting with open files
  leaves "dead" entries in the FM. Class C in the playbook.

## 5. Process: PSP and anchors

`M_PSP` (`EXECUTE.ASM:1102-1164`) builds the program prefix in the image block:

```
PSP[-3] = EXE_FM  - the file handle the EXE was loaded with
PSP[-2] = EXE_MEM - the memory block ID of the program image
PSP[-1] = TASK    - the task ID
PSP[0]   = length, then the command line, APP_PATH, APP_FULLNAME
LAST_PSP_PTR (double word in DSS data) - the PSP of the last started program
```

EXE header (`Estex-DSS/DSS/Structures.inc`, `_sEXE_HEADER`): `LD_ADDR` (load address
#4100-#FFFF), `PC_REG`, `SP_REG`; the return point to `RETFAR → LEAVE` is pushed by the loader
(`EXECUTE.ASM:1070-1074`).

`TASK` is a single byte of the current task in DSS data (in 1.70 it is, in effect, the EXEC nesting
depth; task switching is `GSWITCH` #43). For decoding in the debugger, the addresses of the symbols
`TASK`, `MEMTAB`/`MemoryTable`, `FM_BUF`, `XSTACK`, `ERLEVEL` come from the build export:
`system.exp` (`CORE_BUFFERS.*`, see the header) and `system.sym` - all 3 640 kernel labels
([estex-dss-build.md](../2026-09-28-sprinter/estex-dss-build.md) §3).

## 6. The system "clean state" (what to check)

1. Vectors 0000-00FF and ADRST10 == the reference after DSS boot.
2. Every MEMTAB entry: 0 (free) or the ID of a block that exists at the BIOS level;
   the sum of taken pages == total − free (INFOMEM).
3. Every FM entry: `FTASK` == 0/empty or the ID of a live task; after a task exits - empty.
4. Windows PAGE1-3 after LEAVE == the snapshot before EXEC; EXSTACK untouched outside EXEC/LEAVE.
5. No pages "owned" by BIOS handles that MEMTAB does not see, except
   the known system ones (DSS pages DIRPAGE/FATPAGE/TXTPAGE/ENVPAGE/DRVPAGE,
   `DOS-MAIN.ASM:30-34`, and the DEPLOY blocks `:351-381`).

Known system traps that illustrate the damage classes - `KNOWN.BUG`: `WINCOPY/WINREST`
do not disable interrupts before calling the BIOS (output through the stack - a risk of damaging the DSS stack); write errors
in the FAT/DIR system areas are silently lost; "system files are skipped" when listing
a directory. These are ready-made cases for mutation tests of the detectors.
