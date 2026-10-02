# In-memory font finder — requirements

- **Date:** 2026-09-29
- **Status:** draft, revision 2.
  - Revision 2 adds §4.0 (F20-F27): four independent, combinable detection
    strategies (static pattern scan, signature/catalog scan, live runtime
    correlation, TTD-based offline correlation) instead of one heuristic
    scoring pass, plus the requirement that the correlation machinery behind
    the two runtime strategies is built as a font-agnostic shared primitive,
    because the same "source block read → destination block write" pattern
    also identifies sprite blits and tile-map rendering (§4.0, F26). N1 is
    narrowed accordingly (signature matching is now partly in scope, as
    Method B).
- **Scope:** an autonomous analyzer that scans a loaded program's memory for
  ZX Spectrum-style 8×8 character bitmap tables (custom fonts), reports what
  it finds, and can hand a found table to [`ScreenOCR`](../../../core/src/debugger/analyzers/rom-print/screenocr.h)
  so screen OCR can recognize text drawn with the program's own font instead
  of (or in addition to) the ROM font.

## 0. Summary

`ScreenOCR` today matches every on-screen 8×8 cell bitmap against a hash
table built once from the 48K ROM font
([`zxspectrumfont.h`](../../../core/src/debugger/analyzers/rom-print/zxspectrumfont.h)).
That covers BASIC and any program that prints through the ROM's own
character generator unchanged. It misses the very common case of a game or
demo that:

- redefines the UDG block or POKEs a whole new character set into RAM and
  repoints the character generator (`CHARS`, `#5C36`, or a custom print
  routine) at it — "font swap", the majority of commercial ZX games and demos
  that don't use `PRINT`/`RST 0x10` for in-game text;
- carries more than one such table (a normal-size font, a "big" title font,
  a symbol/icon set), switching between them, or showing more than one at
  once (status bar vs. dialogue).

Today an agent driving the emulator through automation has no way to answer
"does this program use a custom font, and if so, where is it and how many
are there" — it would have to eyeball a memory dump. This feature builds
that answer as a standalone analysis, and makes it available everywhere
`capture ocr` already is.

## 1. Goals

- **G1 — Find candidate font tables, by more than one method.** A single
  static heuristic cannot cover both "eyeball a snapshot with no execution
  info" and "watch the program actually print and know for certain what it
  read and drew". Four independent, combinable strategies are required
  (§4.0): a static pattern scan, a signature/catalog scan, a live runtime
  correlation, and a TTD-based offline correlation. Each returns the same
  result shape (§4.4) so results from different methods rank together.
- **G2 — Score and rank candidates**, not just flag "font-shaped data" —
  false positives (screen data, sprite frames, compressed data, code) must be
  scored lower than real fonts so the top result is usually right.
- **G3 — Report structure, not just an address**: base address, glyph count,
  glyph size (today only 8×8/1bpp — see §7 for out-of-scope variants),
  the code-point mapping guess (which glyph is character `'0'`/`'A'`/space),
  and a confidence score.
- **G4 — Callable two ways**:
  - **standalone / autonomous**: any automation surface can run it directly
    on demand, independent of OCR, e.g. as a reverse-engineering /
    memory-analysis tool ("does this game have more than one font, where are
    they");
  - **as an OCR helper**: `ScreenOCR` can call it internally to discover a
    font it should use before/instead of the ROM font, and load the winning
    candidate into its matching table (§4.5).
- **G5 — Automation parity.** Every capability is reachable from CLI,
  WebAPI (+OpenAPI), MCP, Lua, and Python, and from the Qt debugger, per the
  project's [automation parity rule](../../../.agents/AGENTS.md); `.recipe/`
  gets a worked example.
- **G6 — No cost when unused.** Like every analyzer in
  `core/src/debugger/analyzers/`, this only runs when explicitly invoked or
  activated; it must add nothing to the hot per-instruction/per-frame paths
  when idle.

## 2. Non-goals

- **N1 (narrowed in revision 2)**: recognizing *which game* a font belongs
  to is out of scope — this feature never claims "this is Jet Set Willy's
  font". What *is* in scope (Method B, §4.0) is exact/near-exact byte
  matching against a small, growable catalog of previously-confirmed font
  tables (seeded from this feature's own verification titles, §6 V3/V4,
  the same way the [ROM signature catalog](../../../docs/inprogress/2026-09-14-automation-triage-gaps/)
  (PLAN #16) is seeded from ROMs this project has already identified) —
  used purely to raise confidence on a repeat sighting of a table this
  analyzer has seen before, not to build or ship a large third-party font
  database.
- **N2**: fonts on video hardware this emulator doesn't yet expose as a flat
  bitmap font model (ATM/ZX-Evo hardware "font RAM" upload is tracked
  separately — PLAN #55 phase E8 (font RAM); this feature's memory scan
  still finds a *software* font table even on those machines, but does not
  reach into font-RAM-only hardware paths).
- **N3**: proportional/variable-width fonts, non-1bpp fonts (4-color
  Pentagon 1024 16-color glyphs, etc.), and font sizes other than the
  ZX-standard 8×8 cell (a 6×8 "compressed" UDG-style font is in scope since
  it is still byte-per-row; anything not byte-aligned per row is not).
- **N4**: automatically rewriting the screen's text-mode/attribute
  interpretation — this feature only supplies *glyph shapes*; cell layout
  (32×24, attributes, etc.) is unchanged and still `ScreenOCR`'s job.

## 3. Background: what a font table looks like in memory

- **Standard shape**: `N` glyphs × 8 bytes, one byte per pixel row, MSB =
  leftmost pixel (matching `zxspectrumfont.h`'s convention exactly — the ROM
  table is a valid example this feature must also recognize as *a* font,
  just one already known to `ScreenOCR`).
- **Common conventions** seen across ZX software (informs scoring in §4.3,
  not hard filters — real games break every one of these individually):
  - table often starts on a 256-byte boundary (fast character-generator
    indexing: `(code - base) * 8` needs no page-crossing math), but far from
    always;
  - `N` is usually 96 (full ASCII 0x20–0x7F, ROM-compatible) or 64 (UDG-style
    subset), occasionally fewer (digits + a handful of symbols for a score
    display);
  - most glyph rows are sparse relative to a full 0xFF (real letters have
    background/padding rows, especially rows 0 and 7);
  - within one table, glyphs correlate with each other far more than random
    bytes do (shared strokes, consistent left/right margins) — this is the
    strongest discriminator against non-font data (§4.3, feature F5);
  - many tables reuse several ROM glyphs verbatim (digits, punctuation) even
    when letters are redrawn — a partial hash-table hit against the *known*
    ROM font (already available in `ScreenOCR::_fontHashTable`) is itself a
    positive signal, not just a negative ("this isn't a new font").
- **Where they live**: contiguous free RAM the program owns — most commonly
  just above the screen (`0x5B00`-ish upward on 48K-style layouts), in the
  UDG area (`0xFF58` on 48K BASIC, but games repoint `CHARS` freely), a
  128K-machine's paged-out RAM bank, or anywhere in a loader-relocated
  binary. The scanner must not assume a fixed location (§4.2).

## 4. Functional requirements

### 4.0. Detection strategies (four methods)

No single technique reliably finds a font: a static scan can only guess from
shape, and guessing is wrong exactly on the cases that matter most (a font
that reuses many ROM glyphs, or one laid out unconventionally). The
strongest evidence comes from *watching the program draw with it* — but that
requires either the program to be doing so right now (live) or a recording
of a session where it did (TTD). The four methods below are independent,
combinable, and share one result shape (§4.4) so a caller can start cheap
and escalate:

- **Method A — static pattern scan** (§4.3, F8-F13, unchanged from revision
  1). Works on a single memory snapshot, no execution required. Cheapest,
  least certain; a pure structural guess.
- **Method B — signature/catalog scan** (F21). Hashes each candidate window
  (from Method A's sliding scan, or independently) and looks it up in a
  small growable catalog of font tables this analyzer has confirmed before
  (§ N1). An exact or near-exact (few bytes different — e.g. only digits
  redrawn between two versions of the same game) hit is very high
  confidence, cheap to compute (one hash lookup, reusing Method A's window
  enumeration), and does not require the program to be running.
- **Method C — live runtime correlation** (F22). While the program runs,
  watches actual "read N bytes from table, write them (or a simple
  transform of them) to a screen character cell" events as they happen —
  the literal act of printing — and reports the table address, stride, and
  which specific glyphs were witnessed. Requires the program to print
  something on screen while the tracer is active; sees only what is drawn,
  not the whole table. Directly observed, not inferred — strictly more
  certain than A/B for whatever it does catch.
- **Method D — TTD-based offline correlation** (F23). The same
  burst-correlation algorithm as Method C, run against an already-recorded
  TTD session instead of requiring the tracer to have been switched on in
  advance. Scans an entire play session after the fact and finds every font
  actually used anywhere in it (title screen, in-game, game-over screen),
  including screens the caller didn't know to watch for live.

#### Requirements

- **F20**: Method A is exactly §4.3 (F8-F13); named here only so the four
  methods have parallel identifiers.
- **F21 (Method B)**: a signature catalog keyed by a content hash of the
  glyph table bytes (whole-table hash for exact hits, plus a coarser
  per-glyph hash set for near-exact/partial hits — reusing F10's existing
  per-glyph ROM-hash mechanism as the model). Catalog entries are added
  only from confirmed candidates (human-reviewed or Method C/D-witnessed,
  never from an unverified Method A guess, to avoid the catalog teaching
  itself false positives). Ships empty or near-empty; grows from this
  feature's own verification work (§6) and later real-world use — no
  attempt to pre-populate it from an external font database (N1).
- **F22 (Method C)**: requires a new shared capability — an ordered,
  bounded memory-access event tracer (§ F26/F27) — watching writes into the
  screen's character-cell bitmap area (or text-mode buffer) and, for each
  write burst, looking at the immediately preceding read burst recorded by
  the tracer. A burst is classified as a "print event" when: N consecutive
  bytes are read from one ascending address range, then N bytes are written
  to one screen character cell's 8 row addresses (ZX-interleaved, per
  `ScreenOCR::getScreenAddr`'s addressing), where each written byte is
  either identical to, or a simple fixed transform (AND/OR/XOR with a
  constant, bit-reverse, shift) of, the corresponding read byte in the same
  position. A hit reports: source base address = read range start, the
  screen cell written, and (from the CPU's current opcode/operand, or from
  the character code already known to be in a register/RAM slot at the
  print call, when determinable) which character code produced this glyph.
  Accumulating many print events narrows the source table's base and
  stride with much higher confidence than Method A's shape-only guess, and
  requires no assumption about table alignment, size, or code-point base at
  all — every fact is witnessed, not guessed.
- **F23 (Method D)**: identical burst classification to F22, but the input
  is a TTD recording's replayed instruction/memory timeline (PLAN #40 TTD
  v2) instead of a live tracer feed — reuses the same event-search shape as
  PLAN #46's planned "TTD port-events search" (generalized here from port
  events to memory read/write bursts) and TTD v2's write-journal (already
  used to answer `find-last`-style queries, per the TTD docs). Runs once
  over a session (or a caller-bounded frame range) and reports every
  distinct font/table address witnessed across it, not just what's on
  screen at one instant.
- **F24**: Method C/D results use the same `FontCandidate` shape as A/B
  (§4.4) plus provenance fields: number of corroborating print events, the
  set of screen cells/frames where they were witnessed, and a *confirmed*
  (not guessed) code→glyph mapping for every glyph actually witnessed
  (glyphs never printed during the observed window are absent from the
  mapping, not wrongly guessed).
- **F25**: a caller can run methods in any combination and get back one
  merged, ranked list (§4.4/F15) — e.g. Method A/B for an instant opinion,
  then Method C/D while the program keeps running to confirm or promote a
  candidate. When two methods report overlapping/duplicate tables (F12),
  a Method C/D witnessed hit outranks a same-scoring Method A/B guess,
  since it is directly observed.
- **F26 (shared, font-agnostic primitive)**: the ordered-access tracing and
  burst-classification machinery behind F22/F23 (read a block in order,
  correlate it with a write block in the same order under a simple
  transform) is built as its own component, independent of any font
  semantics — it reports "block read at X (N bytes) correlates with block
  write at Y (N bytes), transform T, repeated K times", nothing about
  glyphs. `FontFinder` is its first consumer, classifying a correlated
  burst as a font print event by *destination shape* (writes to one
  screen character cell's 8 interleaved row addresses). The same primitive,
  with a different destination-shape classifier, is expected to power a
  later sprite finder (destination: an arbitrary rectangular screen
  region, not one character cell — a blit) and tile-map finder (two
  correlated hops: index read from a map array, then a tile bitmap read
  from a table, then the screen write) — see §7-O5. Placement/naming of
  this shared component is a design (TDD) decision, not specified here.
- **F27 (new instrumentation needed)**: F26 needs more than the project
  already has. The existing `MemoryAccessTracker`
  (`core/src/emulator/memory/memoryaccesstracker.h`, backing
  [`.recipe/analysis/memory-counters.md`](../../../.recipe/analysis/memory-counters.md))
  counts *how often* an address was touched and *from which caller*, but
  not the *order* of accesses relative to each other, which is exactly what
  burst correlation needs. This feature requires a new opt-in, bounded
  (ring-buffer) ordered event log — address, direction (R/W), size, PC,
  t-state/frame — cheap enough to run continuously while active and, per
  G6, costing nothing when inactive. This is additive: the existing
  per-address counters and their recipe are unaffected and remain useful on
  their own for other analyses.

### 4.1. The analyzer itself

- **F1**: a new component (working name `FontFinder`) under
  `core/src/debugger/analyzers/font-finder/`, alongside the existing
  `rom-print` analyzer it complements — not inside `rom-print` itself, since
  it is address-space analysis, not a screen/print capture.
- **F2**: a synchronous, on-demand scan API taking an emulator id (or a raw
  memory snapshot for offline/testing use) and an optional address range,
  returning a ranked list of `FontCandidate` results (§4.4) — no activation
  step required for the common case (mirrors `ScreenOCR::ocrScreen`, a
  static call, not an `IAnalyzer` you must register first).
- **F3**: optionally registrable as an `IAnalyzer`
  (`core/src/debugger/analyzers/ianalyzer.h`) for a *live* mode that
  re-scans on `onFrameEnd()` at a low rate (configurable, default a few
  Hz, not every frame) to catch runtime font swaps and report when the set
  of active fonts changes — this is the only part of the feature that touches
  the frame loop, and it is opt-in (G6).
- **F4**: must work identically on a live emulator instance and on a
  detached memory blob (a saved snapshot, a `.sna`/`.z80` load, or a TTD
  frame) so it is usable both interactively and in batch/offline analysis
  and in unit tests without booting a machine.

### 4.2. Scan range

- **F5**: default scan range is "everything the program could have written":
  RAM excluding the 16K ROM window(s) and, on banked machines, every mapped
  RAM page (not just the currently paged-in 16K window) — reuses whatever
  memory-page enumeration the TTD v2 per-region work (PLAN #40 Phase 1) or the
  existing `Memory` paging API already expose; if V1 isn't ready when this
  starts, fall back to "current mapping only" and note the gap in TODO.md
  rather than block on it.
- **F6**: caller-supplied range/page restriction (e.g. "only bank 5", "only
  0x8000-0xBFFF") for targeted searches and for the "watch a specific
  address for a font" recipe case.
- **F7**: skip regions known *not* to be font data without a full scan of
  them — video RAM's bitmap area (not the UDG/attribute area), the current
  screen's attribute area, and any region a running `MediaManager` /
  `IBlockDevice` load buffer occupies — as a performance and false-positive
  optimization, not a correctness requirement (a font manually placed in
  the bitmap area, unusual but not impossible, is still found if the caller
  explicitly ranges the scan there, per F6).

### 4.3. Detection heuristics (scoring, not a single hard rule)

Each address is scored as "start of an N-glyph font table" by combining
independent signals — no single signal is authoritative, since real fonts
routinely violate any one of them:

- **F8 — row plausibility**: per-glyph, per-row bit-density histogram close
  to real fonts' typical shape (sparse edge rows, denser middle rows), vs.
  the near-uniform or highly periodic histograms of code/graphics/compressed
  data.
- **F9 — inter-glyph correlation**: adjacent glyphs in a real font correlate
  (shared margins/strokes) more than shuffled 8-byte windows of the same
  data would — computed as an autocorrelation/self-similarity score across
  the candidate table, distinguishing a font from e.g a sprite sheet with
  visually unrelated 8-byte-per-row frames.
- **F10 — known-glyph partial match**: how many candidate glyphs exactly
  match a ROM glyph (reusing `ScreenOCR`'s existing hash table) — high
  partial overlap is strong positive evidence even at low F8/F9 scores
  (handles "redrew the letters, kept the digits" fonts).
- **F11 — table-boundary alignment bonus**: small positive weight for
  256-byte alignment and for `N` ∈ {96, 64, 32} — a bonus, not a filter (§3).
- **F12 — non-font penalties**: table overlaps executed code (per a
  disassembly/coverage pass if one is available — reuse the `coverage`
  analyzer's data when present, degrade gracefully when it isn't),
  overlaps the currently displayed bitmap screen, or is bit-identical to a
  region already flagged by another table's scan (duplicate/alias
  detection, so paging aliases of the same physical RAM don't produce two
  "different" candidates).
- **F13**: the combined score and every contributing sub-score are reported
  per candidate (§4.4) — an agent (or a human) must be able to see *why*
  something scored the way it did, not just a single opaque number.

### 4.4. Result shape

- **F14**: each `FontCandidate` reports: base address (+ page/bank if
  applicable), glyph count `N`, glyph byte size (8 today, §7 for the future),
  overall confidence score, the F8-F12 sub-scores, a best-guess code-point
  mapping (`firstCharCode`, typically 0x20 or 0x00 for a UDG-only set — the
  mapping guess uses F10's ROM-glyph alignment when available, otherwise
  assumes the ROM's own 0x20 base), and a compact preview (the first few
  glyphs' bitmaps, in the same visual format used in `zxspectrumfont.h`'s
  comments, for eyeballing over the wire).
- **F15**: results are ranked by confidence, deduplicated (F12), and capped
  at a caller-supplied limit (default a small number — most programs have
  1-3 real fonts, not dozens); the full unfiltered candidate count is still
  reported so a caller knows if results were truncated.

### 4.5. OCR integration

- **F16**: `ScreenOCR` gets a new entry point (naming TBD in design, e.g.
  `ScreenOCR::useCustomFont(base, glyphCount, firstCharCode)`) that (re)builds
  its matching hash table from an arbitrary memory region using the same
  hashing scheme as the ROM table, so OCR can match cells against a custom
  font exactly as it does the ROM one today.
- **F17**: a convenience path — "OCR this screen, auto-detecting a custom
  font if the ROM font doesn't explain most of what's on screen" — that runs
  `FontFinder`, picks its top candidate above a confidence threshold, calls
  F16, retries the OCR pass, and reports which font (ROM or which candidate)
  was actually used. Falls back to the ROM font (today's behavior) when no
  candidate clears the threshold.
- **F18**: multi-font support is explicitly a "try candidates, report which
  one matched more cells" flow, not simultaneous merged matching — if two
  fonts are on screen in different areas (§0), F17's auto path picks the
  single best match; recognizing per-region fonts is a candidate follow-on,
  not required here (see §7).
- **F19**: `ScreenOCR`'s existing ROM-only behavior (`ocrScreen`,
  `containsText`) is unchanged by default — F16/F17 are additive entry
  points, not a behavior change to the existing calls (backward
  compatibility for existing recipes/tests).

### 4.6. Automation surfaces

Every capability below must land on every surface in the same change (per
`.agents/AGENTS.md` automation-parity rule) — see the TDD for the concrete
command/route/binding names:

- **W1 (find)**: run a scan, return ranked candidates (§4.4) — CLI command,
  WebAPI route + OpenAPI schema, MCP tool (or an `inspect_state` aspect,
  design decides which fits better next to `screen_ocr`), Lua binding,
  Python binding.
- **W2 (use)**: apply a candidate (or an explicit base/count/firstCode) as
  OCR's active font (F16), and a way to revert to the ROM font.
- **W3 (auto)**: the F17 auto-detect-and-OCR convenience call, returning
  both the OCR'd text and which font was used.
- **W4**: Qt debugger surface — at minimum a menu action ("Find fonts in
  memory") producing the same ranked list plus glyph preview rendering
  (reuses whatever bitmap-preview widget the memory viewer already has), and
  a way to apply a candidate to the OCR/print-capture tools already in the
  debugger. Full design left to the TDD/design.md (this is a requirements
  document, not a UI spec).
- **W5**: today's OCR surface has a **pre-existing gap** — no Lua binding
  for `capture ocr` at all (checked 2026-09-29: CLI, WebAPI, Python, MCP all
  have it; Lua does not). Since this feature explicitly promises Lua parity
  for the *new* font-finder calls, closing the OCR Lua gap in the same pass
  is in scope (small, and leaving new Lua font calls next to a missing Lua
  OCR call would be an obviously incomplete surface).

## 5. Non-functional requirements

- **NF1 — Test speed**: per the project's test rules, unit tests exercise
  the scoring logic against small synthetic memory blobs (no full machine
  boot needed) and stay well under 50ms each; a handful of slower
  integration tests may boot a real program known to redefine its font (see
  §6, V3/V4) — each such test needs the standard comment justifying the
  boot per `core/tests/README.md`.
- **NF2 — Determinism**: given the same memory contents, the scanner
  produces the same ranked candidate list every time (no reliance on
  wall-clock, thread interleaving, or unordered-container iteration order
  leaking into the reported order).
- **NF3 — No hot-path cost**: F3's optional live-analyzer mode must cost
  nothing when not activated (G6); the on-demand scan (F2) itself runs
  outside the emulation thread's timing budget (like `ScreenOCR::ocrScreen`
  today) and does not need to hit a frame deadline, but should not be *so*
  slow that "scan all of a 128K machine's paged RAM" becomes impractical
  for interactive use — a rough target and the actual measured number belong
  in the TDD/benchmarks, not guessed here.
- **NF4 — Cross-platform**: pure portable C++ (bit manipulation, no
  platform-specific code), consistent with every other analyzer in
  `core/src/debugger/analyzers/`.

## 6. Verification scenarios

Real programs to validate against (mirrors the project's preference for
validating against real software over synthetic-only fixtures, per e.g. the
GS debugger requirements §4.16):

- **V1 — synthetic**: hand-built memory blobs — a copy of the ROM font at a
  non-standard address (must score high — it's a real font, just not at the
  ROM's own address), pure random bytes (must score near zero), a sprite
  table with unrelated 8-byte frames (must score lower than a real font
  despite passing F11's alignment bonus), a font with only the digits
  redrawn and letters kept from ROM (must still be found via F10).
- **V2 — BASIC / ROM-only program**: confirms F17's auto-path correctly
  chooses *not* to switch off the ROM font (no candidate clears the
  threshold, or the ROM font itself is the top "candidate" and F17 is a
  no-op either way).
- **V3 — a known custom-font game**: at least one real, freely distributable
  title known to redefine its character set for in-game text (candidates to
  confirm during design: something with a simple, readable in-game font and
  UDG/CHARS redefinition — the design doc should name and check in the
  specific title(s), the way V1-derived candidates were validated for other
  analyzers in this codebase). Confirms the end-to-end `FontFinder` →
  `useCustomFont` → OCR pipeline reads real in-game text correctly.
- **V4 — a title with more than one font** (e.g. a small pixel font for a
  status line and a larger title font), to exercise F15/F18's
  multiple-candidates and "OCR picks the best-matching one" behavior.
- **V5 — runtime/TTD correlation (Methods C/D)**: using the same V3/V4
  titles, confirm the live tracer (Method C) and the TTD replay scan
  (Method D) witness the same table address and code→glyph mapping that
  Method A/B found by inference, and that F25's ranking correctly promotes
  the witnessed result. A synthetic case (a small test program with a
  hand-written print routine of known behavior, incl. a non-identity
  transform such as XOR-with-attribute) exercises the burst classifier
  (F22) without depending on a real title at all — this one belongs with
  V1 as a fast, boot-light unit test.

## 7. Open questions / follow-ons (not blocking this design)

- **O1**: should a confirmed candidate be cacheable/persistable (like a ROM
  signature catalog entry) so a second run against the same program doesn't
  re-scan? Left to design; likely a TTD-session-scoped cache at most, not a
  persistent file, to avoid the "signature catalog" scope creep of N1.
- **O2**: proportional-width and non-8×8 fonts (N3) — worth a follow-on once
  the fixed-cell case is solid and its false-positive rate is known.
- **O3**: per-region multi-font OCR (F18's deferred "simultaneous" case) —
  revisit if V4-style titles turn out to be common enough to matter.
- **O4**: hardware font-RAM (ATM/ZX-Evo, N2) integration — tracked under
  PLAN #55 phase E8; revisit linking the two once that lands.
- **O5 (generalization, revision 2)**: once the shared access-correlation
  primitive (F26) exists for fonts, a **sprite finder** (destination: an
  arbitrary rectangular blit target rather than one character cell) and a
  **tile-map finder** (a two-hop correlation: map-array index read → tile
  table read → screen write) are natural, low-marginal-cost follow-ons —
  same tracer, same burst classifier, a different destination-shape
  matcher. Not designed here; noted so the shared component (F26) isn't
  accidentally built in a font-specific way that would need rework later.
