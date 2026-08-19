# Conditional Breakpoints — Comparative Research Summary

Date: 2026-08-17
Status: research complete; design to follow (`design.md`)

Detailed per-emulator reports live in `research/`:
- [`research/unreal-speccy.md`](research/unreal-speccy.md) — grammar extracted from actual source (dbgbpx.cpp) + tslabs ZX-Evo fork extensions
- [`research/specemu.md`](research/specemu.md) — verified against the shipping 3.4 b250426 binary + bundled docs
- [`research/spectaculator.md`](research/spectaculator.md) — official 9.0/9.1 manual + developer blog
- [`research/zx-m8xxx.md`](research/zx-m8xxx.md) — the user's own web emulator (source of the "Screen Region Breakpoints" screenshots)
- [`research/automation-modules-survey.md`](research/automation-modules-survey.md) — our automation infrastructure (for the DeZog module)
- [`research/dezog-dzrp.md`](research/dezog-dzrp.md) — DZRP protocol deep dive (see also `dezog-integration.md`)
- [`codebase-survey.md`](codebase-survey.md) — our existing BreakpointManager / Memory paging infrastructure
- [`research/performance-prior-art.md`](research/performance-prior-art.md) — fast condition evaluation in GDB/MAME/Mesen2/Dolphin/LLDB/VS (input to `performance.md`)

## Comparison matrix

| Capability | Unreal Speccy 0.38 | SpecEmu 3.4 | Spectaculator 9.1 | DeZog (client) |
|---|---|---|---|---|
| Execute BP | yes, + ranges | yes | yes | yes (long addresses) |
| Mem R/W BP | yes, + ranges (64K bit-array) | panel + via `MRA/MWA` vars | yes (single address) | WPMEM ranges |
| Port BP | via cbp `IN/OUT/VAL` only (fires 1 instr late) | via `PRA/PWA/PRV/PWV` + Run-until menu | first-class, **with port mask** | no |
| Conditions | C-like, RPN-compiled, 16 max | flat comparisons, implicit AND only | C-like, full precedence | evaluated client-side |
| Number literals | hex-only, digit-first (`0DFFD`) | dec default, `#hex` | dec + `0x`/`$`/`#`/`h` — all four | dec/hex |
| Mem deref in cond | `M(x)`, `x->y` | `(addr)`, `(addr.w)` | `rb() rw() rwb()` functions | via client |
| Flags in cond | mask `F` manually | mask `F` manually (no mnemonics) | `Z_FLAG` etc. mask constants | mnemonics |
| Access addr/value in cond | fork only: `RD/WR/MDT` | `MRA/MWA/MRV/MWV` — best in class | no | no |
| Paging in cond | `DOS`, `FD` (7FFD latch); fork: `PG0-3` | `P0-P3`, `PAGING`, `SCREEN` | none (checkbox instead) | n/a |
| Bank-bound BP | no | no | **yes** — "break only when executing from RAM n" checkbox | **yes** — long addresses (bank+1 byte) |
| Physical (any-slot) BP | no | no | no | no |
| Hit counts | no | no | **yes** — always / ==N / multiple-of / >=N, live counter + reset | ignore via conditions |
| Labels/symbols in cond | no | no | no (labels exist, not in expressions) | **yes** (sjasmplus etc.) |
| Persistence | bpx.ini (but NOT cbp!) | no | .dzx debugging projects — **best in class** | launch.json / source |
| One-shot "run until" | F4 to cursor | rich menu: cycle/opcode/port/tape/disk events | trace menu incl. Run Until Condition | stepping |
| Error handling | "Please do RTFM" msgbox | silent | distinct **Error state** on breakpoint icon | client-side |
| Canned helpers | no | keyboard half-rows, TR-DOS regs, FDC cmds | ULA/7FFD/kempston/half-row helpers with masks | no |

## What to take from each

**From Unreal Speccy:**
- The *architecture*: expressions compiled once (shunting-yard → RPN) and evaluated by a tiny stack machine — cheap per-hit evaluation. We should do the same (or bytecode equivalent).
- Bus-event operands for port I/O with value matching (`OUT`, `IN`, `VAL`) — but fix the "fires one instruction late" flaw by evaluating at the access hook, not the next instruction boundary.
- Range breakpoints as first-class citizens.
- The tslabs fork's `RD/WR/MDT` (access address/value) and `PG0-3` (per-slot page) operands — the community explicitly demanded these; they compose access-breakpoints with conditions.

**What to avoid from Unreal:** hex-only digit-first literals; C precedence trap (`out & 0FF == 0FE`); cbp not persisted; conditions and address-breakpoints as two non-composable systems; slow-core-for-everything once any cbp exists.

**From SpecEmu:**
- The richest set of machine-state condition variables: `MRA/MWA/MRV/MWV`, `PRA/PWA/PRV/PWV`, `P0-P3`, `PAGING`, `SCREEN`, `FRAMEINTS`, FDC state, `optime` — this vocabulary is the right ambition level for a hardware-accurate emulator.
- Readable, low-ceremony comparisons (`hl=de`, `(23560)=13`).
- `(expr)` / `(expr.w)` dereference syntax — nicer than `M(x)`.
- One-shot `stop <cond>` console command and `/bpc` CLI arg.
- The "Run until <event>" one-shot trigger menu concept.

**What to avoid from SpecEmu:** no OR/grouping (implicit AND only); 16-bit-only `TS`; no hit counts; no per-BP bank binding (conditions-only paging); `=`-only equality quirk.

**From Spectaculator:**
- The unified dialog model: one breakpoint = location + type + optional page restriction + optional condition + hit-count policy + label + enabled — exactly the "one entity, many optional facets" shape we want.
- Full C-like expression language with proper precedence, all four hex notations accepted, `rb()/rw()` documented clearly.
- Structured hit counts (always / equal / multiple of / >= N) with a live counter and reset — friendlier than expression-encoded counters, and cheap to implement.
- Port **masks** on I/O breakpoints (essential for Spectrum's partial decoding).
- Canned helpers encoding platform knowledge (keyboard half-rows with correct masks, 7FFD, ULA).
- Distinct *Error* state for breakpoints whose condition fails to evaluate.
- Debugging-project persistence (breakpoints + labels + comments auto-loaded alongside a snapshot).

**What to avoid from Spectaculator:** conditions can't see paging state (page restriction is UI-only); single-address breakpoints (no ranges); no scripting surface; page restriction limited to one bank via checkbox.

**From DeZog:** long-address (bank+offset) breakpoint model as the wire/API-level representation; conditions optionally evaluated by the frontend when the backend can't (graceful degradation). See `dezog-integration.md`.

## Gaps nobody fills — our differentiators

1. **Physical-page breakpoints** ("break on write to `ram7:0000-1FFF` through *any* slot"). Nobody has this; our `MapZ80AddressToPhysicalPage`-based hot path makes it nearly free.
2. **Slot-filtered range breakpoints** on machines beyond 7FFD (ATM Turbo 2+ / ZX Evolution, 256 pages): all three reference emulators either ignore paging or model it as 7FFD bits. Our model must bind to the Memory slot→page table, not port latches.
3. **Composition of access-breakpoints with conditions** in one entity (Unreal classic can't; SpecEmu can only via bus variables; Spectaculator can but without ranges/paging vars).
4. **Every facet available from every frontend** (GUI, CLI, Lua, Python, WebAPI, DeZog) over one core model — the reference emulators are each locked to a single UI paradigm.
5. **Hit counts + conditions + ranges + bank-binding simultaneously** — each reference has at most two of the four.

## Resolved: the "Screen Region Breakpoints" screenshots

The screenshots showing "Screen Region Breakpoints" (C,R,W,H rectangles) and "Tape Block / Disk Read / Disk Sector" triggers with skip counts are from the user's own **ZX-M8XXX** project (`/Volumes/TB4-4Tb/Projects/emulators/github/ZX-M8XXX`, `ui/help-content.html`) — which is why no public emulator matched. It is now the fourth reference: see [`research/zx-m8xxx.md`](research/zx-m8xxx.md). Key takeaways: screen-region breakpoints with a Normal/Shadow/Both page selector (user-validated, physical-page-framed), device-level tape/disk triggers with skip counts, ΔT-since-last-break counter, comparison breakpoints from POKE-search, and standalone flag mnemonics (`Z`, `NZ`, `C`…) as the friendliest flag syntax of all references. Its condition language (single comparison, interpreted per hit) is the floor, not the target.
