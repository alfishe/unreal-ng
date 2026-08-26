# ZX-M8XXX (local project) — Breakpoint System Notes

Source: `/Volumes/TB4-4Tb/Projects/emulators/github/ZX-M8XXX` (the user's own JS/web ZX Spectrum emulator).
The "Screen Region Breakpoints" / "Tape & Disk Triggers" screenshots that could not be matched to any public emulator are from this project's help page (`ui/help-content.html`, section `help-breakpoints`).

## Feature set (from help + `core/spectrum.js`)

**Execution breakpoints**: gutter click / F9, three gutter states (none / disabled / active).

**Conditional breakpoints** (`spectrum.js` `evaluateCondition()`, ~:5313-5450):
- Operands: all main/alternate registers (8/16-bit incl. primed), `I`, `R`; memory deref `(HL) (DE) (BC) (SP) (IX) (IY) (IX±n) (IY±n) (nnnn)`; `T`/`TSTATES`; context vars `VAL` (watchpoint data) and `PORT` (port breakpoints).
- Flags as standalone mnemonics: `Z NZ C NC P M PE PO N H S`.
- Operators: `== != <> < > <= >= & |`.
- Literals: decimal if pure digits; hex if `h` suffix or contains A–F (so `4000` is decimal, `4000h` hex — ambiguity resolved toward decimal).
- **Shape: a single comparison (`value op value`) or a single flag mnemonic** — no AND/OR chaining of comparisons, no parentheses for grouping.
- **Interpreted per hit** with regexes — re-parsed every evaluation, not compiled (works fine in a JS emulator; not a model for our hot path).

**Memory breakpoints**: break on read / break on write, single address or selected range from the memory-view context menu.

**Port breakpoints**: IN/OUT, with `PORT`/`VAL` usable in conditions.

**Screen Region Breakpoints** — the distinctive feature:
- Write Bitmap / Write Attr on a **rectangular region** entered as `C,R,W,H` (character cells, 0-31 × 0-23), or pixel coordinates `X,Y,W,H` (px checkbox).
- **Screen dropdown: Normal (page 5 at $4000) / Shadow (page 7 at $C000) / Both** — i.e. it is *physical-page-aware*: it targets the screen page, not the Z80 address window.

**Tape & Disk Triggers** (device-level, with skip counts):
- Tape Block: break after a tape block loads (flash or real-time); skip=N breaks after the (N+1)th block.
- Disk Read: break after any sector read (TR-DOS or +3DOS); skip count supported.
- Disk Sector: break when a specific `TT:SS` track:sector is read.

**Comparison Breakpoint** (Cheats/POKE-search integration, help ~:762): break when two watched memory cells satisfy an operator (e.g. `hero_x = enemy_x`) — evaluated only on writes to either address via memory write callbacks.

**Delta T-states** (`ΔT`, help ~:187): accumulated T-states since the last breakpoint fired; resets on any breakpoint (exec/mem/port) — cycle-exact timing between breakpoints.

**JS API** (help ~:1675): `addBreakpoint(addr)`, `addBreakpointWithCondition('4000', 'A==0')`, `removeBreakpoint`, `getBreakpoints`, `clearBreakpoints`; declarative trigger objects with `condition` fields.

## Takeaways for the unreal-ng design

1. **Screen-region breakpoints with Normal/Shadow/Both targeting** — user-validated feature, and the page-dropdown proves the physical-page framing is the natural UX for it. Maps directly onto our physical-page breakpoints (`ram5`/`ram7` + rectangle→address-set precomputation).
2. **Device-level triggers with skip counts** (tape block, disk read, track:sector) — live above the memory bus (tape loader / Beta Disk / FDC), confirming the "predefined event triggers" phase-2 concept; skip count is just a hit-count facet.
3. **ΔT since last break** — cheap and genuinely useful for timing work; worth carrying over.
4. **Comparison breakpoint** — sugar over "condition evaluated on write to watched addresses"; falls out of our model (write-watchpoint on two addresses + condition) but the POKE-search → breakpoint workflow is a UX idea worth keeping.
5. The single-comparison condition language is the floor, not the target — our design needs full boolean composition (that's the gap all four references share to some degree).
6. Flag mnemonics (`Z`, `NZ`, `C`…) as standalone conditions are the friendliest flag syntax of all four references — adopt (with mask constants as the power-user fallback, à la Spectaculator).
