# In-memory font finder — technical design (TDD)

- **Date:** 2026-09-29
- **Status:** draft, revision 2.
  - Revision 2 adds §2.4 (the shared `AccessPatternTracer` primitive and the
    Method B/C/D algorithms) per requirements revision 2 (F20-F27, V5), and
    updates the module layout, API, implementation plan and verification
    sections accordingly.
  - Same-day follow-up: `AccessPatternTracer`'s cost model, storage and TTD
    integration were split out into their own document,
    [access-pattern-tracer-design.md](access-pattern-tracer-design.md),
    which also **corrects** two rough spots in this revision's first pass —
    the storage-size estimate (§2.4.2) and the claimed hard dependency on
    TTD v2 for Method D (§5 P1.8, §7). Both corrections are inlined at their
    original locations below with a pointer to the design doc.
- **Requirements:** [requirements.md](requirements.md) (F1-F27, W1-W5, V1-V5)

## 1. Overview

`FontFinder` is a new, standalone analysis component that scans a memory
range for ZX Spectrum-style 8×8/1bpp glyph tables and scores each candidate
address as "looks like the start of a font". It is deliberately independent
of `ScreenOCR`: it can be called directly (reverse-engineering use case) or
called *by* `ScreenOCR` to pick and load a custom font before recognizing
on-screen text (requirements §4.5).

```
                    ┌───────────────────────────┐
                    │   Automation surfaces      │
                    │ CLI · WebAPI · MCP · Lua ·  │
                    │      Python · Qt            │
                    └──────────┬─────────┬────────┘
                               │         │
                W1/W2/W4 (find/use) │  W3 (auto = find + use + ocr)
                               │         │
                               ▼         ▼
                    ┌────────────────┐  ┌───────────────┐
                    │  FontFinder    │  │  ScreenOCR     │
                    │  (font-finder/)│◄─┤  (rom-print/)  │
                    │  scan + score  │  │  useCustomFont │
                    └───────┬────────┘  └───────┬────────┘
                            │                    │
                            ▼                    ▼
                    ┌────────────────────────────────┐
                    │        Memory / paging          │
                    │ (Memory, per-bank enumeration)  │
                    └──────────────────────────────────┘
```

`FontFinder` depends on `Memory`; `ScreenOCR` depends on both (it already
does, for the screen bitmap). `FontFinder` has **no** dependency on
`ScreenOCR` — the "autonomous" requirement (G4) means the dependency arrow
only ever points from OCR to the finder, never the reverse, so the finder
stays usable (and testable) with zero OCR/screen machinery involved.

## 2. Module layout

```
core/src/debugger/analyzers/
    font-finder/
        fontfinder.h
        fontfinder.cpp
        fontcandidate.h          // plain data type shared with automation surfaces
        fontsignaturecatalog.h   // Method B
        fontsignaturecatalog.cpp
    access-pattern-tracer/        // shared, font-agnostic (F26) — see §2.4
        accesspatterntracer.h
        accesspatterntracer.cpp
        accessburst.h            // plain data type: a correlated read/write burst

core/tests/debugger/analyzers/
    font-finder/
        fontfinder_test.cpp
        fontsignaturecatalog_test.cpp
    access-pattern-tracer/
        accesspatterntracer_test.cpp
```

`font-finder/` is placed as a sibling of `rom-print/`, not inside it —
`rom-print` is about *capturing what the ROM's own print/OCR pipeline does*;
this is address-space analysis with no dependency on the ROM print routines
at all (requirements F1). `access-pattern-tracer/` is its own top-level
analyzer directory, not nested under `font-finder/`, precisely because F26
requires it to know nothing about fonts — `FontFinder` is a *consumer* of
it (Method C/D), exactly the way it is a font-agnostic future consumer for
a sprite/tile finder (requirements §7-O5).

### 2.1. `FontCandidate` (data type)

```cpp
struct FontCandidate
{
    uint32_t address;        // linear/logical address per AddressSpec below
    std::optional<uint8_t> page; // bank/page number, if the machine is banked
    uint16_t glyphCount;      // N
    uint8_t glyphBytes;       // 8 today (requirements N3/§7-O2 reserve the field)
    uint8_t firstCharCode;    // best-guess code-point base (F14)

    float score;              // combined confidence, 0..1
    struct SubScores
    {
        float rowPlausibility;      // F8
        float interGlyphCorrelation;// F9
        float romPartialMatch;      // F10
        float alignmentBonus;       // F11
        float nonFontPenalty;       // F12 (subtracted, reported separately)
    } subScores;

    std::vector<std::array<uint8_t, 8>> previewGlyphs; // first few glyphs (F14)

    // Which method(s) produced/corroborated this candidate (F25); a
    // candidate found by more than one method carries every flag that
    // applies, deduplication (F12) keeps the union, not just the first hit.
    enum class Method : uint8_t { StaticPattern = 1, SignatureCatalog = 2,
                                   LiveCorrelation = 4, TtdCorrelation = 8 };
    uint8_t methodsFlags = 0;

    // F24: only populated by Method C/D (methodsFlags & (LiveCorrelation|TtdCorrelation))
    struct WitnessProvenance
    {
        uint32_t printEventCount = 0;
        std::vector<uint16_t> screenCellsWitnessed; // packed row*32+col
        std::unordered_map<uint8_t, uint8_t> confirmedCodeToGlyphOffset; // charCode -> (addr - base)/8
    };
    std::optional<WitnessProvenance> witness;
};

struct FontFinderResult
{
    std::vector<FontCandidate> candidates;  // ranked, deduplicated, capped (F15)
    size_t totalCandidatesFound;            // before capping
};
```

`AddressSpec` (scan-range input, F5/F6):

```cpp
struct AddressSpec
{
    std::optional<uint32_t> startAddress;   // default: scan everything (F5)
    std::optional<uint32_t> endAddress;
    std::optional<uint8_t> page;            // restrict to one bank (F6)
};
```

### 2.2. `FontFinder` API

```cpp
class FontFinder
{
public:
    /// Method A (+ Method B when a catalog is supplied): on-demand scan of
    /// a live emulator instance's memory (F2, F5-F7, F21)
    static FontFinderResult findFonts(const std::string& emulatorId,
                                       const AddressSpec& range = {},
                                       size_t maxResults = 8,
                                       const FontSignatureCatalog* catalog = nullptr);

    /// Same scan over a raw memory blob — no emulator required (F4).
    /// Used directly by unit tests (V1) and any offline/batch tooling.
    static FontFinderResult findFontsInBlob(const uint8_t* data, size_t size,
                                             size_t maxResults = 8,
                                             const FontSignatureCatalog* catalog = nullptr);

    /// Method C (F22): start/stop a live correlation session. While active,
    /// installs an AccessPatternTracer watching writes into the screen's
    /// character-cell area and classifies bursts as print events (§2.4).
    /// Cheap to call repeatedly; getWitnessedFonts() is a snapshot, it does
    /// not stop the session.
    static void startLiveCorrelation(const std::string& emulatorId);
    static void stopLiveCorrelation(const std::string& emulatorId);
    static FontFinderResult getWitnessedFonts(const std::string& emulatorId,
                                               size_t maxResults = 8);

    /// Method D (F23): scan an existing TTD recording/session for print
    /// events across the whole range (or a caller-bounded frame window),
    /// using the same burst classifier as Method C over the TTD replay
    /// timeline instead of a live tracer feed.
    static FontFinderResult findFontsFromTtd(const std::string& emulatorId,
                                              std::optional<uint64_t> fromFrame = std::nullopt,
                                              std::optional<uint64_t> toFrame = std::nullopt,
                                              size_t maxResults = 8);

private:
    // Per-address scoring (F8-F13); operates on a raw span, shared by
    // both public entry points above.
    static std::optional<FontCandidate> scoreCandidateAt(
        const uint8_t* data, size_t size, size_t offset, uint16_t glyphCount);

    static float rowPlausibility(const uint8_t glyph[8]);              // F8
    static float interGlyphCorrelation(const uint8_t* table, uint16_t n); // F9
    static float romPartialMatch(const uint8_t* table, uint16_t n);    // F10, reuses
                                                                        // ScreenOCR's
                                                                        // hash table
                                                                        // read-only
    static float alignmentBonus(size_t offset, uint16_t n);            // F11
    static float nonFontPenalty(/* coverage/screen overlap inputs */); // F12

    static std::vector<FontCandidate> deduplicate(std::vector<FontCandidate>); // F12/F15
};
```

Notes:

- `findFonts`/`findFontsInBlob` are static, mirroring `ScreenOCR`'s existing
  static-call style (F2) — no instance, no activation step for the common
  case.
- `romPartialMatch` reads `ScreenOCR`'s font hash table but does not write
  it — the dependency this direction is read-only and one-way; `FontFinder`
  still builds and links independently of any `ScreenOCR` *instance/runtime*
  state (there is none — `ScreenOCR`'s hash table is a lazily-initialized
  static, safe to read).
- **F3's live mode** is a thin optional wrapper, `FontFinderAnalyzer :
  public IAnalyzer`, that calls `FontFinder::findFonts` from `onFrameEnd()`
  at a configurable stride (default: once per ~10 frames, i.e. ~5 Hz) and
  diffs the result against its last scan to report only *changes* (a font
  candidate appeared/disappeared/moved). It is a separate class in the same
  files, registered with `AnalyzerManager` only when a caller explicitly
  activates live mode (G6/NF3) — the static-call path above never touches
  `AnalyzerManager` at all. **This is not Method C.** F3 is a cheap periodic
  *re-run of Method A/B* (still a structural guess, just repeated); Method C
  (`startLiveCorrelation`/`getWitnessedFonts`, F22, §2.4) is event-driven —
  it watches actual print events via `AccessPatternTracer` and witnesses
  the table directly. The two are independent and can run together: F3
  catches a font swap quickly by shape, Method C confirms/refines it (or
  finds one F3 misses, e.g. a table that doesn't look font-shaped to F8/F9
  but is genuinely printed from) as soon as the program actually uses it.

### 2.3. Scan algorithm (F5-F13)

1. **Enumerate ranges** from `AddressSpec` (§2.1): if unset, enumerate every
   RAM page the machine exposes (reuse whatever page-enumeration API the
   `Memory`/banking model already has for the current machine; on machines
   without page enumeration yet — pending PLAN #40 V1 — fall back to the
   currently paged-in window only, and log/flag the fallback in the result
   metadata so a caller can tell it got a partial scan).
2. **Exclude** the ROM window(s) always; exclude the bitmap screen area and
   attribute area unless explicitly ranged into (F7).
3. **Slide a window** of candidate sizes (`glyphCount` × 8 bytes, trying
   `glyphCount` ∈ {96, 64, 32} first per F11, then a generic sweep) across
   each remaining range. Candidate start offsets are every byte, not just
   256-byte boundaries — F11 rewards alignment, it does not require it (a
   real font at an unaligned offset must still be found).
4. **Score** each candidate window via `scoreCandidateAt` (F8-F13).
5. **Filter** low-confidence candidates below a floor (keeps output and
   `totalCandidatesFound` meaningful without drowning callers in noise);
   the floor is a tuned constant, documented next to its definition and
   revisited once V1-V4 fixtures exist to tune against.
6. **Deduplicate** (F12): candidates whose glyph bytes are bit-identical
   (paging aliases) or whose address ranges overlap heavily keep only the
   highest-scored instance.
7. **Rank and cap** to `maxResults` (F15), keeping `totalCandidatesFound`
   from the pre-cap count.

Performance (NF3): step 3's byte-granularity slide is the dominant cost.
Cheap early-outs before the full F8-F13 scoring pass:
  - reject a window immediately if more than e.g. half its rows are `0x00`
    or `0xFF` (real glyphs are rarely fully blank/full across most rows —
    quick reject for large runs of empty/screen-attribute-like memory);
  - cache `romPartialMatch` lookups (hash table lookup is already O(1) per
    `ScreenOCR`'s design).
A benchmark (`core/benchmarks/`, per the project's hot-path rule — this
scan is not a hot path in the emulation-frame sense, but it does touch
potentially hundreds of KB, so its wall-clock cost on a 128K/1MB address
space needs a measured number, not a guess) gates step 3's implementation
before this lands; target and actual numbers go here once measured.

### 2.4. Methods B/C/D: signature catalog and access-pattern correlation

#### 2.4.1. Method B — `FontSignatureCatalog`

```cpp
// fontsignaturecatalog.h
struct FontSignatureEntry
{
    uint64_t wholeTableHash;              // exact-match key
    std::vector<uint64_t> perGlyphHashes; // same hashing scheme as
                                           // ScreenOCR::hashBitmap, one per glyph
    uint16_t glyphCount;
    uint8_t firstCharCode;
    std::string label;                    // free-text, e.g. "confirmed 2026-09-29,
                                           // V3 verification title"
};

class FontSignatureCatalog
{
public:
    bool loadFromFile(const std::string& path);   // one file, kebab-case per
    bool saveToFile(const std::string& path) const; // docs convention; format TBD
                                                     // (YAML, matching this
                                                     // project's other on-disk
                                                     // catalogs/reports)

    /// Exact hit, or best near-exact hit (fraction of matching per-glyph
    /// hashes) above a caller threshold. Returns nullopt on no match.
    std::optional<FontSignatureEntry> lookup(const uint8_t* table, uint16_t glyphCount,
                                              float nearMatchThreshold = 0.9f) const;

    void addConfirmed(const FontCandidate& confirmed, const std::string& label);

private:
    std::unordered_map<uint64_t, FontSignatureEntry> _byWholeHash;
};
```

`lookup` is called from `scoreCandidateAt` (§2.2) whenever a `catalog` is
supplied to `findFonts`/`findFontsInBlob` — a hit sets `SubScores` to
maximum confidence and tags `methodsFlags |= SignatureCatalog`, but does not
replace F8-F12 (a caller without a catalog, or scanning a never-seen font,
still gets Method A's answer). `addConfirmed` is how V3/V4's verification
titles (and later manual confirmations) populate the shipped catalog file —
never auto-added from an unverified Method A guess (requirements F21).

#### 2.4.2. Methods C/D — `AccessPatternTracer` and burst correlation

This is the shared, font-agnostic primitive (requirements F26). It has two
responsibilities: **capture** an ordered, bounded log of memory accesses,
and **classify** runs of that log into correlated read→write *bursts*. Only
the second step's *destination-shape* check is font-specific, and that
check lives in `FontFinder`, not in the tracer.

**Full design — memory/CPU cost model, storage layout, memory management,
and TTD integration — is its own document**:
[access-pattern-tracer-design.md](access-pattern-tracer-design.md). Summary
of what it settles, since this component is reused outside `FontFinder`
(requirements §7-O5) and its cost model is load-bearing for whether Methods
C/D are practical to leave running during normal play:

- **API shape** (`start`/`stop`/`drainBursts` live, `classifyFromTtd`
  offline) is as sketched in requirements §2.4.2 — see the design doc for
  the exact signatures and the `AccessEvent`/`AccessBurst` types.
- **`AccessBurst` uses a fixed-size `std::array`, not a `std::vector`**, for
  `destinationAddresses` — no per-burst heap allocation on a path that may
  fire hundreds of times per second (design doc §4.2).
- **Storage is two-tier and small**: a ~3 KB sliding classification window
  plus a ~128 KB bounded confirmed-bursts ring — around 256 KB per active
  session total, not the "few thousand events" ring this document's
  revision 2 first sketched (design doc §4).
- **CPU overhead is a planning estimate (~0.15-0.5 ms/frame worst case,
  full-64 KB window) pending a real benchmark** — §8 of the design doc is
  the gate before any default window ships.
- **Method D does not depend on a future TTD v2 replay API.** It is a
  two-phase search on top of what TTD v1 already has:
  `TTDWriteJournal`'s existing scan mechanism (extended to look for
  destination-shape write clusters) finds candidate windows cheaply, then a
  bounded silent replay (`TimeTravelManager`'s existing
  `RestoreCheckpoint`/`RunTStates` — the same mechanism the write journal's
  own "two-pass" fallback already uses) with a live `AccessPatternTracer`
  attached recovers the read side for just those windows (design doc §7).
  This corrects this document's own §5 (P1.8) and §7 risk note below,
  written before this mechanism was traced through — see the correction
  note in each.

Classification algorithm (read run → write run → shape check → transform
detection) is unchanged from the sketch above; see the design doc §4-§7 for
why the storage backing it is much smaller, and cheaper to run continuously,
than first estimated.

**Reuse for sprites/tiles (requirements §7-O5)**: nothing in the tracer
mentions a glyph, a character code, or a font. A future `SpriteFinder`
would supply a `DestinationShapePredicate` matching "a rectangular block of
W×H addresses" instead of one character cell; a `TileFinder` would chain
two `AccessBurst`es (index read → tile-table read → screen write). Both
reuse `AccessPatternTracer` unchanged.

## 3. `ScreenOCR` integration (F16-F19)

```cpp
// screenocr.h additions
class ScreenOCR
{
public:
    // ... existing API unchanged (F19) ...

    /// Rebuild the matching table from an arbitrary memory region using the
    /// same hashing scheme as the ROM table (F16).
    static void useCustomFont(const std::string& emulatorId, uint32_t address,
                               uint16_t glyphCount, uint8_t firstCharCode);

    /// Revert to the ROM font (undoes useCustomFont).
    static void useRomFont();

    /// F17: find a font if the ROM font doesn't explain most of the screen,
    /// load it, OCR, and report which font was used.
    struct AutoOcrResult
    {
        std::string text;
        bool usedCustomFont;
        std::optional<FontCandidate> fontUsed; // absent if ROM font was kept
    };
    static AutoOcrResult ocrScreenAutoFont(const std::string& emulatorId,
                                            float confidenceThreshold = /*TBD*/ 0.7f);
};
```

- `useCustomFont` reuses the *existing* `hashBitmap`/`matchFont` machinery —
  it just repopulates `_fontHashTable` from the given region instead of
  `ZXSpectrum::FONT_BITMAP`. This means `_fontHashTable` is no longer
  "always the ROM font"; `useRomFont()` is the explicit way back, and the
  class must track which font is currently active so `useRomFont` can
  restore it without re-deriving it from the ROM image at each call site
  (cache the ROM-derived table once, as today, and swap a pointer/flag
  rather than rebuild it in `useRomFont`).
- **Thread-safety note**: `_fontHashTable` is currently a plain static with
  a lazy-init flag, safe because it is only ever built once from a constant
  (the ROM). Once it can be *replaced* at runtime (F16), concurrent OCR
  calls from different automation surfaces (e.g. a WebAPI request and an
  MCP `inspect_state` poll landing close together) must not observe a
  half-rebuilt table. Guard the swap (a mutex, or build-then-atomic-swap of
  the table pointer) — this is a correctness requirement, not a
  nice-to-have, since automation surfaces run on different threads today.
- `ocrScreenAutoFont` (F17): runs `ScreenOCR::ocrScreen` first (today's ROM
  font path); if the fraction of unmatched cells (today silently rendered
  as `'?'` per `ocrCell`'s doc comment) exceeds a threshold, runs
  `FontFinder::findFonts`, and if the top candidate clears
  `confidenceThreshold`, calls `useCustomFont` and retries. Falls back to
  the ROM-font result otherwise (F19 — no regression to today's default
  behavior for programs that print through the ROM).
- F18 (multi-font, deferred as "simultaneous"): `ocrScreenAutoFont` only
  ever activates one font per call. If a future per-region need appears
  (§7-O3 in requirements), it is a new entry point, not a change to this
  one.

## 4. Automation surfaces (F16-F19, W1-W5)

Following the existing OCR surface exactly (`capture ocr` / `/capture/ocr` /
`screen_ocr` aspect / `capture_ocr()`), one command family per surface:

| Capability | CLI | WebAPI | MCP | Lua | Python |
|---|---|---|---|---|---|
| **W1 find** | `capture fontfind [range]` | `GET /api/v1/emulator/:id/capture/fontfind` | new `inspect_state` aspect `screen_font_find` **or** a dedicated tool (design decision below) | `emu.capture_fontfind()` | `capture_fontfind()` |
| **W2 use** | `capture fontuse <addr> [count] [firstcode]` / `capture fontuse rom` | `POST /api/v1/emulator/:id/capture/fontuse` (body: address/count/firstCode, or `{"rom": true}`) | tool `set_ocr_font` | `emu.capture_fontuse(...)` | `capture_fontuse(...)` |
| **W3 auto** | `capture ocr auto` | `GET /api/v1/emulator/:id/capture/ocr?autoFont=true` (query flag on the *existing* OCR route, not a third route) | extend the existing `screen_ocr` aspect with an `autoFont` option, or a distinct aspect `screen_ocr_auto` (pick one — avoid two aspects that do almost the same thing) | `emu.capture_ocr(auto_font=true)` | `capture_ocr(auto_font=True)` |
| **W5 (gap close)** | *(already exists)* | *(already exists)* | *(already exists)* | **new**: `emu.capture_ocr()` (closes the pre-existing Lua gap noted in requirements §4.6-W5) | *(already exists)* |

Design decisions to settle before implementation (tracked here so they
don't silently get decided ad hoc per surface — automation parity requires
the *same* shape everywhere):

- **D1 — MCP shape for W1**: a dedicated MCP tool (`find_fonts`) vs. folding
  it into `inspect_state`'s aspect list next to `screen_ocr`. Recommendation:
  a dedicated tool, since the result shape (ranked candidates with
  sub-scores and preview glyphs) doesn't fit the terse per-aspect text
  format `inspect_state` uses for the other aspects (see
  `mcp-tools.cpp`'s existing aspect handling) — forcing it into that format
  would either truncate useful data or bloat every other aspect's output
  format. Confirm during implementation review; this is a recommendation,
  not yet a decision line.
- **D2 — W3's route shape**: a query flag on the existing `/capture/ocr`
  route (as sketched above) keeps one URL doing "OCR the screen" with an
  optional smarter mode, rather than three near-duplicate OCR routes.
  Mirror the same "flag on the existing call" shape on every other surface
  (CLI: `capture ocr auto` reads as a subcommand but should map to the same
  flag internally, not a separate handler function).
- **D3 — OpenAPI**: `openapi_capture.inc` gets the new routes/schemas in the
  same change (mirrors `openapi_video.inc` from the existing OCR work).

### 4.1. Documentation updates (per module, same change as the code)

- [`docs/emulator/design/control-interfaces/command-interface.md`](../../emulator/design/control-interfaces/command-interface.md) —
  no structural change expected (it documents the *interface layer*
  architecture, not every command), but its "Documentation References"
  table is the map to update if any interface's status changes; also update
  it if D1 introduces a new MCP tool category worth mentioning.
- [`cli-interface.md`](../../emulator/design/control-interfaces/cli-interface.md) —
  `capture fontfind` / `capture fontuse` / `capture ocr auto` command
  reference, alongside the existing `capture ocr` entry.
- [`webapi-interface.md`](../../emulator/design/control-interfaces/webapi-interface.md) —
  new routes, request/response JSON shapes.
- [`python-interface.md`](../../emulator/design/control-interfaces/python-interface.md) —
  new bindings.
- [`lua-interface.md`](../../emulator/design/control-interfaces/lua-interface.md) —
  new bindings, **including** the W5 gap-closing `capture_ocr()` binding
  that has no prior entry to update (it's new to Lua, not new to the
  project).
- `core/automation/mcp/README.md` and the MCP tool's own inline description
  strings (the MCP module documents tools primarily through their JSON
  schema `description` fields, per the existing `screen_ocr` aspect
  strings in `mcp-tools.cpp`) — keep both in sync.
- `.recipe/analysis/` — a new recipe (e.g. `font-detection.md`) alongside
  `memory-counters.md`, showing: run `capture fontfind`, inspect candidates,
  apply the top one with `capture fontuse`, OCR, revert with
  `capture fontuse rom`. This is the concrete "how an agent uses this"
  worked example `.recipe/README.md` asks every capability to have.
- Qt debugger (W4): a short mention in whichever doc covers the debugger's
  memory/capture tools today (see
  [2026-08-26-debugger-enhancements](../2026-08-26-debugger-enhancements/) —
  W4's own design lives there or in a follow-up to this folder, not
  duplicated here).

## 5. Implementation plan

Phases are ordered so each is independently mergeable and testable — no
phase blocks on a later one, matching the project's naming convention for
letter/number phase tags used elsewhere in `docs/inprogress/`.

- **P0 — scoring core**: `FontCandidate`/`AddressSpec` types,
  `findFontsInBlob`, all of F8-F13's scoring functions, deduplication.
  No emulator dependency at all — testable purely against synthetic blobs
  (V1). This is the bulk of the actual algorithm risk; get it right against
  V1 fixtures before touching any live-machine code.
- **P1 — live-emulator scan**: `findFonts(emulatorId, ...)`, the
  page-enumeration integration (F5, with the documented PLAN #40 V1
  fallback), the F7 screen/attribute exclusion.
- **P1.5 — signature catalog (Method B)**: `FontSignatureCatalog` (§2.4.1),
  wired into `findFonts`/`findFontsInBlob`'s optional `catalog` parameter.
  Ships with an empty/near-empty catalog file; entries are added by P5.
- **P1.6 — shared access-pattern tracer**: `AccessPatternTracer`,
  `AccessBurst`, the `HostBusOverlay`-backed live capture, and the
  classification algorithm, per
  [access-pattern-tracer-design.md](access-pattern-tracer-design.md), built
  and unit-tested (V5's synthetic print-routine case) with **no
  font-specific code at all** — the destination-shape predicate is a plain
  function parameter, tested here with a synthetic shape, not yet with
  `FontFinder`'s real one. This independence is deliberately verified
  before P2, so the primitive can't quietly grow font assumptions
  (requirements F26). B1-B3 of the design doc's benchmark plan (§8) run as
  part of this phase, before P1.7 depends on the chosen default window.
- **P1.7 — Method C (live correlation)**: `FontFinder::startLiveCorrelation`/
  `stopLiveCorrelation`/`getWitnessedFonts`, supplying the character-cell
  destination-shape predicate and the witness-accumulation logic (F24) on
  top of P1.6's tracer.
- **P1.8 — Method D (TTD correlation)**: `FontFinder::findFontsFromTtd`,
  built on **today's** TTD v1 engine per
  [access-pattern-tracer-design.md](access-pattern-tracer-design.md) §7.2 —
  a `TTDWriteJournal` destination-shape cluster scan (phase 1) followed by
  bounded `RestoreCheckpoint`/`RunTStates` silent replay with
  `AccessPatternTracer` attached (phase 2). **Not blocked on TTD v2** (PLAN
  #40) — corrects this document's earlier (revision 2) framing, written
  before the write-journal/replay reuse was traced through. B4/B5 of the
  design doc's benchmark plan validate phase 1/2 cost before this ships.
- **P2 — OCR integration**: `useCustomFont`/`useRomFont`/
  `ocrScreenAutoFont` on `ScreenOCR`, including the thread-safety guard
  from §3. `ocrScreenAutoFont` initially drives Method A/B only (P0/P1.5);
  wiring Method C/D's witnessed results into the auto-OCR path is a small
  follow-up once P1.7/P1.8 exist, not a hard dependency of P2 itself.
- **P3 — automation surfaces**: CLI, WebAPI + OpenAPI, MCP, Lua (incl. the
  W5 gap close), Python — one commit (or a small tight series) touching all
  five plus their docs, not staggered across unrelated changes, so the
  surface never ships partially parity'd. Covers W1/W2/W3 (Methods A/B via
  P0-P2) in the first pass; Method C/D's session-control calls
  (start/stop/witnessed-results) get their own surface entries once
  P1.7/P1.8 land, following the same one-commit-touches-every-surface rule.
- **P4 — Qt debugger (W4)**: menu action, ranked-candidate view with glyph
  preview rendering, apply/revert wiring into the existing capture tools.
- **P5 — real-software verification (V3/V4/V5)**: identify and check in the
  specific title(s) used for V3/V4, wire them into the test suite the way
  other analyzers' real-software checks are done (e.g. the RZX archive
  corpus, or the SkoolKit co-emulation runner's real-title checks), and
  record which real titles were validated (this TDD should be updated with
  the actual title names once chosen — left as a placeholder here
  deliberately rather than guessing at specific ROMs/tapes without having
  verified their redistribution status).
- **P6 — `.recipe/` and remaining docs polish** (§4.1), if not already done
  incidentally alongside P3/P4.

## 6. Verification

- **Unit tests** (`core/tests/debugger/analyzers/font-finder/fontfinder_test.cpp`):
  V1's synthetic cases — ROM font copied elsewhere (must score high),
  random bytes (near zero), unrelated-frame sprite table (lower than a real
  font despite alignment), partial ROM-glyph-reuse font (found via F10).
  Every test uses `findFontsInBlob` directly — no machine boot, well under
  the 50ms/test budget (NF1).
- **`AccessPatternTracer` unit tests** (`accesspatterntracer_test.cpp`,
  V5's synthetic case): feed a hand-built `AccessEvent` sequence encoding a
  known read-run/write-run/transform and assert the classifier recovers it
  exactly (source address, destination addresses, transform); a sequence
  with *no* correlated burst (unrelated interleaved reads/writes) must
  produce no `AccessBurst`. No emulator, no font logic — exercises §2.4.2 in
  isolation, keeping F26's independence honest.
- **`FontSignatureCatalog` unit tests**: exact hit, near-exact hit above/
  below `nearMatchThreshold`, no match on unrelated data, `addConfirmed`
  round-trip through `saveToFile`/`loadFromFile`.
- **Method C/D integration tests**: boot a small synthetic test program
  with a known print routine (V5), run `startLiveCorrelation` +
  `getWitnessedFonts` and separately `findFontsFromTtd` over a recording of
  the same run, and assert both report the same table address and a
  `confirmedCodeToGlyphOffset` matching the program's actual layout.
- **OCR integration tests** (extend `screenocr_test.cpp` or a new
  `screenocr_customfont_test.cpp` per the project's one-test-file-per-source
  convention if the addition is large enough to warrant its own file):
  `useCustomFont`/`useRomFont` round-trip, `ocrScreenAutoFont`'s
  threshold/fallback behavior (V2: confirms it does *not* switch fonts for
  a ROM-only/BASIC program).
- **Real-software tests** (V3/V4, gated behind the real titles chosen in
  P5): boot the title, let it reach a screen with in-game custom-font text,
  run `ocrScreenAutoFont`, assert the recognized text matches expectation.
  These are boot-bound (`EnableTurboMode()` per the test-writing rules) and
  get the standard "why this test is slower than 50ms" comment.
- **Benchmark** (`core/benchmarks/`): scan-time over a full 128K/1MB address
  space, gating P1's page-enumeration performance approach (§2.3's
  performance note) before merge, per the project's hot-path/A-B benchmark
  rule (this isn't a *hot* path, but it is the one place in this feature
  where an accidentally quadratic scoring pass would visibly hurt
  interactivity); a second benchmark measures `AccessPatternTracer`'s
  per-access overhead while a live session is active (NF3 — must stay cheap
  enough that a caller can leave Method C running through normal gameplay,
  not just a short capture window) and confirms zero overhead when no
  session is active (an idle `HostBusOverlay` costs one flag test per
  `hostbusoverlay.h`'s own doc comment — this benchmark just confirms that
  holds here too).
- **Automation parity check**: for each of P3's five surfaces, a smoke test
  exercising W1/W2/W3 exists in that surface's existing test suite (CLI
  processor tests, WebAPI route tests, MCP tool tests, Lua binding tests,
  Python binding tests) — mirrors how the existing `capture ocr` command is
  already covered per-surface.
- **Cross-platform**: pure portable C++, zero warnings on the mandatory full
  build (gcc/clang/mingw/msvc per `docs/guidelines/cross-platform-compatibility.md`).

## 7. Risks / open items carried from requirements §7

- Scoring-floor tuning (§2.3 step 5) is a "tune against real fixtures" task,
  not a value this document can specify correctly in advance — expect it to
  move during P0/P5 as V1/V3/V4 fixtures accumulate.
- P5's specific real-title choice needs a redistribution-rights check before
  it's checked into `testdata/` (consistent with how other real-software
  fixtures in this repo were sourced) — do not check in a commercial ROM/tape
  without confirming that first.
- PLAN #40 V1 (per-region memory pages) is a soft dependency for full P1
  page enumeration on banked machines; P1 ships with the documented
  single-window fallback if V1 isn't ready first, and gets upgraded when it
  lands (no code needs to change shape, just the enumeration source).
- **Corrected in light of [access-pattern-tracer-design.md](access-pattern-tracer-design.md) §7**:
  P1.8 (Method D) is *not* a hard dependency on TTD v2 — it builds on the
  existing `TTDWriteJournal` scan + `RestoreCheckpoint`/`RunTStates` silent
  replay, both already in the TTD v1 engine. PLAN #40 V1 (per-region memory
  pages) still matters for extending Method D to banked-machine RAM, the
  same soft dependency Methods A/B already have (F5's fallback) — not a new
  blocker specific to Method D.
- The burst-classifier's tuned constants (`maxGapEvents`, ring buffer size,
  `nearMatchThreshold`) are, like the scoring floor above, expected to move
  once V5's synthetic and real fixtures exist — documented as "tuned
  constant" at each definition site precisely so they're easy to find and
  revisit, not treated as load-bearing design decisions here.
