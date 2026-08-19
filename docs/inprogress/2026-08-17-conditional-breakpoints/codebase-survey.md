# Conditional Breakpoints — Existing Infrastructure Survey

Date: 2026-08-17
Status: input material for design (see `research.md` for external emulator research, `design.md` for the design itself)

## Summary

The core already has a solid breakpoint subsystem (`BreakpointManager`) with page-aware
memory breakpoints, port breakpoints, groups, owners, and five automation frontends
(CLI, Lua, Python, WebAPI, Qt UI). What does **not** exist anywhere is condition
expression evaluation — no parser, no evaluator, nothing that understands `A==0`.
The descriptor has no `condition` field. This is a green field.

## 1. Breakpoint core

- `core/src/debugger/breakpoints/breakpointmanager.h` (~291 lines) — types + `BreakpointManager`
- `core/src/debugger/breakpoints/breakpointmanager.cpp` (~1490 lines)
- `core/src/debugger/debugmanager.h` — owns `BreakpointManager*` (`GetBreakpointsManager()`, :43); thin legacy wrappers :59-64
- Tests: `core/tests/debugger/breakpoints_test.cpp` (page-specific matching tests from :653)

### Descriptor (`breakpointmanager.h:52-77`)

```cpp
struct BreakpointDescriptor
{
    uint16_t breakpointID = BRK_INVALID;        // 0xFFFF sentinel
    uint32_t keyAddress = 0xFFFF'FFFF;          // Composite bank + address key
    BreakpointTypeEnum type = BRK_MEMORY;       // BRK_MEMORY | BRK_IO | BRK_KEYBOARD
    BreakpointAddressMatchEnum matchType = BRK_MATCH_ADDR;  // or BRK_MATCH_BANK_ADDR
    uint8_t memoryType = BRK_MEM_READ | BRK_MEM_WRITE | BRK_MEM_EXECUTE;
    uint8_t ioType   = BRK_IO_IN | BRK_IO_OUT;
    uint8_t keyType  = BRK_KEY_PRESS | BRK_KEY_RELEASE;
    uint16_t z80address = 0xFFFF;
    uint8_t page = 0xFF;                        // ROM 0-63 or RAM 0-255
    MemoryBankModeEnum pageType = BANK_RAM;     // BANK_ROM / BANK_RAM / BANK_CACHE
    uint16_t bankOffset = 0xFFFF;               // 0-0x3FFF
    bool active = true;
    std::string owner = "interactive";          // or "analyzer_manager"
    std::string note;
    std::string group = "default";
};
```

Notes:
- **No `condition` field** — the natural place to add one (source string + compiled form).
- `BreakpointRangeDescription` (:82-103) exists as a type but is **not wired** into any
  map or runtime path — ranges are effectively unimplemented.

### Storage & keys

- `unordered_map<uint32_t, BreakpointDescriptor*> _breakpointMapByAddress`,
  `_breakpointMapByPort` (by `uint16_t` port), `map<uint16_t id, ...> _breakpointMapByID`.
- IDs never reused (`_breakpointIDSeq`); `_lastTriggeredBreakpointID` +
  `GetLastTriggeredBreakpointInfo()` (`BreakpointStatusInfo`, h:165-178) for automation.
- Key encoding (`breakpointmanager.cpp:1350-1388`, lookup :1447-1450):
  - `BRK_MATCH_ADDR`      → `0xFFFF'0000 | z80address` (wildcard, any page)
  - `BRK_MATCH_BANK_ADDR` → `(pageType<<24) | (page<<16) | z80address`
  - Caveat: page-specific key stores the **full Z80 address**, not offset-in-page —
    so a page-bound breakpoint today only matches through the slot it was defined at.

### Hot path

`HandlePCChange / HandleMemoryRead / HandleMemoryWrite / HandlePortIn / HandlePortOut`
(h:243-247; impl .cpp:1139-1290, finders :1428-1489). Each:
1. early-returns if the relevant map is empty (zero-breakpoint fast path);
2. `FindAddressBreakpoint` calls `Memory::MapZ80AddressToPhysicalPage(addr)` on every
   access, then two hash lookups (exact page key, then wildcard);
3. checks `active` + type bit.

Hook call sites (all follow `Pause()` → `MessageCenter::Post(NC_EXECUTION_BREAKPOINT)` →
`WaitWhilePaused()`):
- `core/src/emulator/memory/memory.cpp:200` (`MemoryReadDebug`), `:269` (`MemoryWriteDebug`)
  — gated by `_feature_breakpoints_enabled && pDebugManager`
- `core/src/emulator/cpu/z80.cpp:200` (execute) — gated by `cpu.isDebugMode && !skipBreakpoints`;
  consults `AnalyzerManager::ownsBreakpointAtAddress()` so analyzer-owned BPs don't pause
- `core/src/emulator/ports/portdecoder.cpp:104,147` (IN), `:177,213` (OUT)

Fast memory paths (`MemoryReadFast`/`MemoryWriteFast`) contain **no** checks — the
interface is swapped via `GetFastMemoryInterface()`/`GetDebugMemoryInterface()`.
Feature flag: `features.ini [breakpoints]`, cached in `Memory::_feature_breakpoints_enabled`.

## 2. Memory paging model

`core/src/emulator/memory/memory.h/.cpp`; constants `core/src/platform.h:231-261`.

- Flat host buffer: RAM → CACHE → MISC → ROM; `PAGE_SIZE=0x4000`, `MAX_RAM_PAGES=256`,
  `MAX_CACHE_PAGES=2`, `MAX_MISC_PAGES=1` (ROM-write trash page), `MAX_ROM_PAGES=64`,
  `MAX_PAGES=323`. Already sized for ATM2+/ZX Evolution (4MB = 256×16K RAM pages).
- Four 16K Z80 slots (index = addr>>14): `_bank_mode[4]` (`BANK_ROM/RAM/CACHE`),
  `_bank_read[4]`, `_bank_write[4]` host pointers (memory.h:110-112).
- Mapping setters: `UpdateZ80Banks()`, `SetROMPage()`, `SetRAMPageToBank0..3()`, `SetROMMode()`.
- Query "what's in slot N": `GetROMPageForBank / GetRAMPageForBank` (return
  `MEMORY_UNMAPPABLE=0xFFFF` on mode mismatch), `GetPageForBank` (absolute 0..322),
  `GetMemoryBankMode(bank)` (memory.cpp:887-941).
- **Physical abstraction exists** (memory.h:45-50, memory.cpp:1089-1125):

```cpp
struct MemoryPageDescriptor { MemoryBankModeEnum mode; uint8_t page; uint16_t addressInPage; };
MemoryPageDescriptor MapZ80AddressToPhysicalPage(uint16_t address);
```

  Caveat: `page` is left **uninitialized** for `BANK_CACHE`/`BANK_INVALID`
  (memory.cpp:1109-1120, no default branch) — must be fixed before building
  page:offset keys on it.
- ROM (0-63) and RAM (0-255) page numbers are separate namespaces disambiguated by `mode`.
- Non-intrusive debugger access: `DirectReadFromZ80Memory` / `DirectWriteToZ80Memory`
  (memory.h:299-303) — bypasses debug hooks; what a condition evaluator should use
  for `(addr)` dereferences to avoid recursive breakpoint hits.

## 3. Condition/expression evaluation today

**None.** Grep across `core/src/debugger`, `core/automation`, `unreal-qt/src/debugger`
finds only Z80 instruction condition codes in the disassembler
(`z80cfdecoder.h:148-167 IsConditionMet`) — unrelated. `docs/features/debugging.md:183`
lists "Memory watches with condition triggers" as a planned TODO. The assembler
(`core/src/debugger/assembler/z80asm.*`) has operand/expression parsing that may serve
as a style reference only.

## 4. Frontends adding breakpoints today

| Frontend | Surface |
|---|---|
| CLI | `core/automation/cli/` — `bp <addr> [note]`, `wp <addr> r\|w\|rw`, `bport <port> i\|o\|io`, `bplist`, `bpclear`, `bpgroup`, `bpon`, `bpoff`. Dispatch `cli-processor.cpp:105-114`, handlers `commands/cli-processor-breakpoint.cpp`. NB: trailing args become `note` — a condition argument needs explicit delimiting (e.g. `if <expr>`). |
| Lua | `core/automation/lua/src/emulator/lua_emulator.h:574-690` — richest: exec/mem-r/mem-w/port-in/port-out add, remove, clear, (de)activate, count, list, last-triggered info. |
| Python | `core/automation/python/src/automation-python.cpp:401,425-428` — thin: exec add + remove only. |
| WebAPI | `core/automation/webapi/src/api/debug_api.cpp` — GET/POST/DELETE `/breakpoints` (:600,:693,:800,:837), type-string dispatch :749-769; spec in `openapi_spec.cpp`. |
| Qt UI | `unreal-qt/src/debugger/` — `breakpointdialog.*` (list/CRUD), `breakpointeditor.*` (modal editor; `validateInput()` is where a condition field goes), `breakpointgroupdialog.*`, gutter toggle in `disassemblerwidget.cpp:130`, step-out/IM1/IM2 temp groups in `debuggerwindow.cpp:1026-1141`. `menumanager.cpp:348` Ctrl+B toggle is a disabled TODO. |

## 5. Related docs

- `docs/features/debugging.md` — main breakpoint feature doc (:5-42), planned items :161+
- `core/automation/cli/README.md:35-55` — slightly stale CLI doc
- `docs/inprogress/2026-01-11-debugger-events/`, `docs/inprogress/2026-01-23-pause-refactoring/` — pause/state refactor context
- Perf context for per-hit evaluation cost: `docs/inprogress/cpu-optimization-proposal.md`,
  `docs/inprogress/2026-01-10-performance-optimizations/discovery.md`

## 6. Implications for the design

1. Add `condition` (source string) + compiled representation to `BreakpointDescriptor`;
   evaluate inside the five `Handle*` methods after map lookup + `active`/type-bit checks —
   the single funnel for all hits; the empty-map early-return keeps the zero-BP fast path intact.
2. Evaluator inputs: Z80 registers via `_context->pCore->GetZ80()`, memory via
   `DirectReadFromZ80Memory`, current slot mapping via `GetRAMPageForBank`/`GetROMPageForBank`/
   `GetMemoryBankMode`, T-states from CPU state.
3. Page-bound breakpoints exist (`BRK_MATCH_BANK_ADDR`) but key on full Z80 address —
   for true physical (page:offset, any-slot) breakpoints the key should switch to
   `addressInPage`, and the CACHE-page initialization bug must be fixed.
4. Ranges (`BreakpointRangeDescription`) are declared but dead — range support has to be
   designed in (map-per-address expansion vs. interval structure).
5. All five frontends need the condition threaded through; CLI needs a delimiter
   convention because of note-swallowing.
