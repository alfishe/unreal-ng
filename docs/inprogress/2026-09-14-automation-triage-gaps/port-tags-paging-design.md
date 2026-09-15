# Tagged Port Registry & Unified Paging Reporting — Design

**Date:** 2026-09-15
**Status:** Approved with minor technical refinements (design review
2026-09-15); Phase 1 of the §11 rollout implemented same day. The four
refinements — `constexpr` bitwise operator overloads for `enum class
PortTag`, default member initializers on `PortMapEntry` (legacy 5-element
brace rows keep compiling), `ReadPagingLatch` PascalCase rename, and the
explicit `decoded`-keys dictionary (§5.1) — are folded in below.
**Addresses:** E-1 (bank reporting 7FFD-centric), B-2 (mode-selecting paging
registers invisible) — remediation item **P1-2** from
[recommendations.md](recommendations.md).
**Builds on:** P1-5 static port map (`GET /ports`, `PortMapEntry`,
`PortDecoder::getPortMapEntries()` — shipped 2026-09-14/15 with full
CLI/Lua/Python parity).
**Core idea (user proposal):** each machine port decoder must register its
ports not only by address/mask, but also with **semantic tag(s)**. The decoder
then manages index collections per tag — memory-access ports, ROM-access
ports, screen-management ports, sound ports (with a per-soundcard tag) — and
every reporting surface becomes a trivial enumeration instead of a per-model
special case.

## 1. Problem

Two open gaps block state-based triage on extended models:

- **E-1** — `/state/memory` reports paging through 7FFD-derived bits only
  (`ram.bank0..3` via `GetRAMPageForBankN()`, `paging.ram_bank_3 = p7FFD & 7`,
  static annotations like `"Screen 0 location"`). The MCP `memory_banks`
  aspect inherits this verbatim. On extended models the reverse mapping
  returns `MEMORY_UNMAPPABLE (65535)` for banks 1–3 (bug-report #2) — values
  that *look* like broken mappings but are a reporting artifact.
- **B-2** — the mode-selecting latches (`pFF77` on ATM710/ATM3, `aFE` bits 5–6
  on ATM450, `pDFFD` on Profi, `p1FFD` on Scorpion, `pEFF7` on ATM3) are plain
  `EmulatorState` members already checkpointed by TTD, but **no endpoint
  reports them**. The first question of every paging triage — "what did the
  software write into the latch, and is the decode honoring it?" — has no read
  surface.

The root cause is structural: the per-model knowledge "which ports steer what"
lives implicitly inside each decoder's `if`-chains and inside the
`getPortMapEntries()` row descriptions (free-text `device` strings such as
`"Memory paging (RAM bank, shadow screen, ROM)"`). Nothing is *queryable by
meaning*. Every reporting feature (P1-2 today, capabilities introspection
P2-3 tomorrow) has to re-encode that knowledge as another hand-written
per-model switch — a new parity debt every time.

## 2. Goals / non-goals

**Goals**

1. Ports registered by a decoder carry machine-readable semantic tags
   (multiple per port — 7FFD is memory + ROM + screen at once).
2. The decoder owns tag-indexed collections; reporting layers enumerate them
   instead of re-deriving per-model knowledge.
3. P1-2 `/state/paging` falls out of the tagged registry: static half (which
   latches exist, tagged, gated), live half (latch values from
   `EmulatorState` — the single source), derived half (bank table from the
   `Memory` manager — the single source for window truth).
4. Full parity from day one: WebAPI, MCP, CLI, Lua, Python (automation parity
   rule — same information from the same source).
5. Tags are **metadata only**: zero effect on decode behavior, TTD format,
   or performance of the I/O path.

**Non-goals**

- Fixing the ATM reverse-mapping (`GetRAMPageFromAddress`) — that is
  atm-branch work; the endpoint surfaces whatever the memory manager answers.
- New decode semantics, gate changes, or peripheral fitment logic.
- TSConf/GMX/Quorum latch coverage — their decoders are not on master; tag
  enum values are reserved so the atm branch can adopt the scheme without a
  schema break.
- GUI debugger surfaces.

## 3. Tag taxonomy

### 3.1 Categories and the Sound family

Tags are a bitmask (`PortTagSet`, `uint32_t`): one port row commonly carries
several. The Sound family gets member tags so a consumer can ask either
"all sound ports" or "the AY ports specifically" — the per-soundcard tag from
the proposal.

```cpp
// core/src/emulator/ports/portdecoder.h

/// Semantic categories for decoder-registered ports. Metadata only —
/// decode behavior never branches on these. One port may carry several.
enum class PortTag : uint32_t
{
    None     = 0,

    // ---- Categories ----
    Keyboard = (1u << 0),   // #FE matrix half
    Memory   = (1u << 1),   // latch steers RAM window mapping
    Rom      = (1u << 2),   // latch steers ROM selection (shadow monitor, ROM page)
    Screen   = (1u << 3),   // latch steers video mode / shadow surface
    Storage  = (1u << 4),   // mass-storage register sets (Beta128 FDC)
    Mouse    = (1u << 5),   // pointer input registers
    Joystick = (1u << 6),   // Kempston joystick
    System   = (1u << 7),   // service windows, config latches (SMUC, ProfROM...)

    // ---- Sound family: category bit + member bit ----
    // Member tags embed the Sound bit, so family membership is a single
    // AND test: (tag & PortTag::Sound) != 0.
    Sound          = (1u << 8),
    SoundAy        = Sound | (1u << 16),  // #FFFD/#BFFD (TurboSound pairs)
    SoundCovox     = Sound | (1u << 17),  // #FB-class DAC
    SoundSoundDrive= Sound | (1u << 18),  // #F1/#F3/#F9/#FB quad DAC
    SoundGs        = Sound | (1u << 19),  // reserved (GS not on master)
    SoundMoonsound = Sound | (1u << 20),  // reserved (P2-2/P2-4 design)
};

typedef uint32_t PortTagSet;

constexpr PortTagSet PORT_TAG_CATEGORY_MASK = 0x0000FFFFu;  // category bits
constexpr PortTagSet PORT_TAG_SOUND_MEMBERS = 0xFFFF0000u;  // member bits

// ---- enum class needs explicit operators for set arithmetic (review
// refinement 1). All constexpr/inline: no runtime cost, and the zero-warnings
// policy holds on gcc/clang/msvc (no implicit-conversion warnings). ----
constexpr PortTagSet operator|(PortTag a, PortTag b)
{
    return static_cast<PortTagSet>(a) | static_cast<PortTagSet>(b);
}
constexpr PortTagSet operator|(PortTagSet a, PortTag b)
{
    return a | static_cast<PortTagSet>(b);
}
constexpr PortTagSet operator&(PortTagSet a, PortTag b)
{
    return a & static_cast<PortTagSet>(b);
}
constexpr PortTagSet operator&(PortTag a, PortTag b)
{
    return static_cast<PortTagSet>(a) & static_cast<PortTagSet>(b);
}

/// Single-tag conversion for row tables — `enum class` has no implicit
/// conversion to the set type, so bare `PortTag::Keyboard` in a brace
/// initializer would not compile. `Tags(PortTag::Keyboard)` reads better
/// than a cast at every static row.
constexpr PortTagSet Tags(PortTag tag)
{
    return static_cast<PortTagSet>(tag);
}
```

Notes:

- `Memory` vs `Rom` vs `Screen` are separate bits because triage asks
  separate questions ("which ports move RAM?", "which ports can page the
  Shadow Monitor?", "which ports change the video mode?"). A latch that does
  several carries several bits — 7FFD is `Memory|Rom|Screen`, Scorpion 1FFD
  is `Memory|Rom|Screen`, Profi DFFD is `Memory|Screen`, ATM FF77 is
  `Memory|Rom|Screen`.
- `device` free text stays (human display); tags are its machine-readable
  counterpart. The parity rule applies: both come from the same row.

### 3.2 Tag assignment (per model, master decoders)

| Port | Decoder(s) | Tags |
|:--|:--|:--|
| `#FE` | all | `Keyboard` |
| `#FFFD` / `#BFFD` | all | `SoundAy` |
| `#7FFD` | spectrum128/3, pentagon*, profi, scorpion* | `Memory\|Rom\|Screen` |
| `#1FFD` | spectrum3 (+3 special paging) | `Memory\|Rom` |
| `#1FFD` | scorpion256 (window latch / Shadow Monitor) | `Memory\|Rom\|Screen` |
| `#7EFD` | scorpion prof (ProfROM service window) | `Memory\|Rom\|System` |
| `#DFFD` | profi (extended paging + video mode bit 7) | `Memory\|Screen` |
| `#FB` | pentagon128 (Covox/SoundDrive) | `SoundCovox\|SoundSoundDrive` |
| `#FF1F` | scorpion256 (Kempston joystick) | `Joystick` |
| `#00BA` | scorpion prof (SMUC) | `System` |
| Beta128 five-port set | all (fitment) | `Storage` |
| Mouse three-port set | all (fitment) | `Mouse` |
| ATM `#FF77` / `aFE` | atm branch (reserved) | `Memory\|Rom\|Screen` |

The table is normative for the unit tests (§8): every master decoder's
`getPortMapEntries()` rows must match it.

## 4. Registry architecture

### 4.1 Tagged rows — one source, extended

Tags extend the existing `PortMapEntry` (P1-5) rather than introducing a
parallel table — the row already is the per-model single source, and a second
source would recreate the sync problem this design removes:

```cpp
struct PortMapEntry
{
    uint16_t port;
    uint16_t mask;
    uint16_t match;
    const char* device;                 // human-readable (unchanged)
    const char* gate;                   // nullptr = ungated (unchanged)
    PortTagSet tags = 0;                // NEW: semantic categories (§3.1);
                                        //      0 = legacy registered row (§8)
    PagingLatch latch = PagingLatch::None;  // NEW: live-value binding; None
                                            //      for non-latch registers
};
```

Default member initializers (review refinement 2) keep every existing
5-element brace row compiling unchanged — only rows that opt in grow to six
or seven elements — and the struct remains an aggregate (C++14+).

/// Live-value binding for latch rows: how a tagged row finds its current
/// value in EmulatorState. Kept as an enum (not a pointer/member offset) so
/// the struct stays a POD aggregate usable in constexpr-ish row tables.
enum class PagingLatch : uint8_t
{
    None, P7FFD, P1FFD, PDFFD, PFDFD, P7EFD, PEFF7, PFF77,
    AFE, AFB,                     // ATM 4.50 system ports (atm branch)
    PFFF7Window0, ..PFFF7Window3, // ATM 7.10/ATM3 per-window latches (reserved)
    PBD, PTS, PMEM                // TSConf (reserved)
};
```

`PortDecoder::ReadPagingLatch(PagingLatch, const EmulatorState&)` is the
single switch mapping the enum to the `EmulatorState` field (the fields
already exist and are TTD-checkpointed — no new state). Static: the mapping
depends on no decoder state, so tests and future transports can call it
without a live machine. Rows whose live value is not a latch (AY data, FDC
registers, mouse axes) use `PagingLatch::None`.

### 4.2 Decoder-managed index collections

The proposal's core: the decoder owns the indexes, consumers never iterate
raw rows looking for meaning.

```cpp
class PortDecoder
{
public:
    /// Entries carrying ALL of the given tag bits (exact-subset match over
    /// the full tag set - a member bit in the query demands that member, so
    /// HasAnyTaggedPort(SoundCovox) is a true per-soundcard fitment answer;
    /// use GetSoundEntries to enumerate a whole family). Rows are returned BY VALUE:
    /// getPortMapEntries() builds a fresh vector per call, so pointer views
    /// into it would dangle (caught during Phase-1 implementation; the
    /// reporting path is cold, the copies are trivial).
    std::vector<PortMapEntry> GetEntriesByTags(PortTagSet categories) const;

    /// Entries of one Sound family member (SoundAy, SoundCovox, ...).
    std::vector<PortMapEntry> GetSoundEntries(PortTag member) const;

    /// Fast existence checks used by reporting and capabilities surfaces.
    bool HasAnyTaggedPort(PortTagSet categories) const;

    /// All latch rows that can steer the given categories, each with its live
    /// binding — the direct input to /state/paging's static half.
    std::vector<PortMapEntry> GetPagingLatches(PortTagSet categories
                                               = Tags(PortTag::Memory)) const;

    /// Single switch PagingLatch -> EmulatorState field. Static: no decoder
    /// state involved (§5 live half; reserved atm/TSConf members read 0 until
    /// their decoders land).
    static uint32_t ReadPagingLatch(PagingLatch latch, const EmulatorState& state);
};
```

Implementation notes:

- `getPortMapEntries()` stays the single row source; the tag queries are thin
  filters over it (a pre-built `tag -> rows` index is *not* cached — the row
  count is a few dozen and fitment-conditional rows would invalidate it;
  a linear filter is allocation-light and simpler to keep correct).
- **Query semantics (Phase-1 lesson):** the match is an exact subset over the
  full tag set, NOT a category-masked match. The first draft masked the query
  to category bits, which silently degraded `HasAnyTaggedPort(SoundCovox)`
  to "any sound port" (true on 128K via the AY rows) — the unit test caught
  it. Member bits in a query therefore demand that member, which is exactly
  the per-soundcard fitment answer §10.2/P2-3 want.
- Registered peripherals participate: `RegisterPortHandler` grows a tagged
  overload (default keeps the old signature — back-compat):

```cpp
bool RegisterPortHandler(uint16_t port, PortDevice* device,
                         PortTagSet tags = PortTag::None);
```

  so a dynamically attached soundcard/storage device lands in the collections
  with its category instead of the anonymous `"Registered peripheral device"`
  row. The P1-5 dedupe logic is unchanged.

### 4.3 What deliberately does NOT move into tags

- **Decode behavior** — `IsPort_*` predicates, gates and `DecodePortIn`
  chains are untouched; tags describe them, never replace them.
- **Bank-table derivation** — which physical page sits in which 16K window is
  answered by the `Memory` manager (`GetRAMPageForBankN()`, `GetROMPage()`,
  `IsBank0ROM()`), the single source of window truth. Tags identify *which
  ports steer paging*; the endpoint reports *the effect* by asking Memory.
  Duplicating the window math in the decoder would create a second source of
  truth and a new way to be wrong.
- **Fitment** — a tagged row exists only while the device is fitted (mouse,
  Beta128), exactly as today; `HasAnyTaggedPort(PortTag::SoundCovox)` is
  therefore also a fitment answer and can feed P2-3 capabilities later.

## 5. `/state/paging` — assembled from the registry

`GET /api/v1/emulator/{id}/state/paging` (P1-2 schema, now tag-driven):

```json
{
  "model": "SCORPION",
  "latches": [
    { "port": "0x7FFD", "tags": ["memory", "rom", "screen"],
      "device": "Memory paging (incl. extended RAM bits)", "gate": null,
      "latch": "p7FFD", "value": "0x10",
      "decoded": { "ram_bank": 0, "shadow_screen": false,
                   "rom_select": 1, "locked": false } },
    { "port": "0x1FFD", "tags": ["memory", "rom", "screen"],
      "device": "Window latch / Shadow Monitor (bit 1)", "gate": null,
      "latch": "p1FFD", "value": "0x00",
      "decoded": { "shadow_monitor_paged": false } }
  ],
  "banks": [
    { "bank": 0, "address_range": "0x0000-0x3FFF", "type": "ROM",
      "page": 0, "read_write": "read-only",
      "role": "128K Editor/Menu ROM",
      "name": "128k ROM 0 (128k editor & menu)",
      "signature": "3ba308f2…1425" },
    { "bank": 1, "address_range": "0x4000-0x7FFF", "type": "RAM",
      "page": 5, "contended": true, "note": "Screen 0 location" },
    { "bank": 2, "address_range": "0x8000-0xBFFF", "type": "RAM", "page": 2 },
    { "bank": 3, "address_range": "0xC000-0xFFFF", "type": "RAM", "page": 0 }
  ],
  "paging_locked": false,
  "trdos_active": false
}
```

Three halves, three single sources:

1. **`latches` (static + live)** — every row whose `latch` binding is not
   `None`, with its value read through `readPagingLatch()` from
   `EmulatorState`. The `decoded` sub-object is a small per-latch bit
   interpreter (7FFD bits, Scorpion 1FFD bit 1, Profi DFFD bit 7 video flag)
   — same knowledge the decoders apply, reported instead of applied.
   Non-latch rows (joystick, AY, FDC) stay in `/ports`; they are not paging
   state.
2. **`banks` (derived)** — the `Memory` manager getters, presented
   model-agnostically. No 7FFD-centric derivation; the 65535 artifact dies
   with the atm-branch reverse-mapping fix, and until then the endpoint
   reports what Memory answers rather than inventing values.
3. **`paging_locked` / `trdos_active`** — `EmulatorState` flags (lock honors
   the hardware `_7FFD_Locked` latch exposed via a getter, not just the
   cached p7FFD bit).

Comparison with the P1-2 draft in recommendations.md: same endpoint, same
bank-table acceptance; the register-dump object (`p7FFD`, `p1FFD`, ... as
top-level fields) is replaced by the self-describing `latches` array — the
field set is discovered from the registry instead of hardcoded per model, so
atm-branch latches appear automatically once their decoders tag them.

### 5.1 `decoded`-keys dictionary (review refinement 4)

Every key of the per-latch `decoded` sub-object is fixed by this table and
mirrored verbatim into `openapi_paging.inc` (Phase 2) and the typed Lua/Python
dictionaries — one vocabulary, generated from the interpreter that produces
the values. Keys are snake_case; values are ints for numbered selections and
bools for flags; bits not listed are not reported (never invented):

| `PagingLatch` | Model scope | `decoded` keys |
|:--|:--|:--|
| `P7FFD` | all banked | `ram_bank` (int, bits 0–2; Pentagon 512K folds bits 6–7 into the 5-bit index — `PortDecoder_Pentagon512::switchRAMPage`, the only master decoder that does; Scorpion's #7FFD D6/D7 are unused, its extensions live in #1FFD), `shadow_screen` (bool, bit 3), `rom_select` (int 0/1, bit 4), `locked` (bool, bit 5 — cross-checked against the hardware latch at top level) |
| `P1FFD` | Scorpion | `shadow_monitor_paged` (bool, bit 1) |
| `P1FFD` | +3 | `special_paging` (int, bits 0–2), `disk_motor` (bool, bit 3) |
| `PDFFD` | Profi | `extended_ram_bank` (int, bits 0–2, 1024K extension), `video_512x240` (bool, bit 7) |
| `PFF77` | ATM (atm branch) | `video_mode` (string, bits 3–1), `ram_page` (int, bits 7–4), `rom_page` (int, bits 2–0) |
| `PEFF7` | ATM3 (atm branch) | dictionary lands with the decoder (P1-4) |

Reserved members (`PFFF7Window*`, `AFE`/`AFB`, TSConf) define their rows when
their decoders land; the OpenAPI table grows additively, never renames.

### 5.2 ROM page identification — recognized signatures and naming

The banks table above shows **two names per ROM row, deliberately distinct**
(added 2026-09-15 from the review follow-up; closes the paging-surface half
of E-2):

- **`role`** — what the model's ROM *layout* says the slot is: per-model
  semantic names ("128K Editor/Menu ROM", "TR-DOS ROM", "Service ROM",
  "+3DOS ROM", "Shadow Monitor"...). Today this knowledge lives as a
  hand-written per-model `description` switch inside `state_memory_api.cpp`
  (`addPageInfo`); it moves into the core single source as
  `ROM::GetROMPageRole(page)` so every interface shares one table.
- **`name`** — what the page *content* is recognized as, from the signature
  engine that already exists in core (`ROM::_signatures`: SHA-256 → title,
  ~17 entries — 48K/128K ROM 0+1/Toaster/+2/+3, Gluck, HRom, Pentagon,
  Scorpion ZS256, TR-DOS 5.03/5.04T/5.04TM/5.13f). Unknown content keeps the
  existing `GetROMTitle` fallback (`"Unknown ROM, <digest>"`) — never a
  blank, never a guess.
- **`signature`** — the page's SHA-256 (short form in compact surfaces; the
  engine serves it from the process-wide `SignatureCache`, so repeated
  `/state/paging` calls do not re-digest).

**`role` ≠ `name` is the visual triage signal**: a bank whose slot says
"128K Editor/Menu ROM" but whose content titles as "48K BASIC" (or "Unknown
ROM, …") is a wrong-ROM-loaded finding visible at a glance — the exact
question E-2/P3-2 pose ("did the right ROM even load?").

Single-source notes:

- No new catalog: the recognized-signature table IS `ROM::_signatures`; the
  P3-2 curated-table growth (more ROMs, optional data-file loading — the
  `// TODO: load known ROM signatures from file` in rom.cpp) enriches that
  one table and every surface benefits.
- `ROM::CalculateSignatures()` already digests every loaded page at init;
  add a per-page title cache (`ROM::GetROMPageTitle(page)`, invalidated on
  `LoadROM`) so reporting reads cached strings instead of re-resolving.
- The GUI debugger already consumes the same engine
  (`GetROMTitleByAddress` in the disassembler / 16K memory widgets) —
  automation parity brings the same naming to headless surfaces.

CLI rendering (`paging` command) — one line per bank, mismatch flagged:

```text
Banks:
  #0  0x0000-0x3FFF  ROM p0  "128k ROM 0 (128k editor & menu)"  [128K Editor/Menu ROM]
  #1  0x4000-0x7FFF  RAM p5  (contended, Screen 0 location)
  ...
  #0  0x0000-0x3FFF  ROM p0  "Unknown ROM, 9f31c2…"  [Service ROM]  << MISMATCH
```

Lua/Python `paging_state()` carry `role`/`name`/`signature` on ROM bank
entries verbatim; the MCP `paging` aspect inherits them through
`/state/paging`. The WebAPI-only `/state/memory/rom` pages block (which
already reports `signature`/`title`) keeps working — its CLI/Lua/Python
parity remains E-2 scope, fed from the same `ROM` source.

## 6. Parity surfaces (same information, same source)

Per the automation parity rule, the tagged registry and `/state/paging` ship
on all interfaces in the same change:

| Surface | Addition |
|:--|:--|
| WebAPI | `GET /state/paging` (+ OpenAPI `openapi_paging.inc`); `GET /ports` rows gain `tags: ["memory", ...]` and `latch: "p1FFD" | null` (additive) |
| MCP | `inspect_state` aspect `paging` (fans out to `/state/paging`, mirrors `fdc`/`mouse` handlers); `machine` aspect unchanged — paging is queried, not identity |
| CLI | new `paging` command (latch table with live values + decoded bits, bank table with ROM `name`/`[role]` and the `<< MISMATCH` flag — §5.2, lock/TR-DOS line); `ports` table gains a `Tags` column |
| Lua | `paging_state()` (table mirroring the JSON, ROM banks carry `role`/`name`/`signature`), `ports_map()` entries gain `tags`/`latch` keys |
| Python | `paging_state()` (dict mirroring the JSON, ROM banks carry `role`/`name`/`signature`), `ports_map()` entries gain `tags`/`latch` keys |

Tag names serialize as lowercase strings (`memory`, `sound_ay`,
`sound_sounddrive`, ...) via one `PortTagToString(PortTag)` overload per
member — the same names everywhere, no per-interface vocabularies.

## 7. Migration & back-compat

- **`PortMapEntry` grows two fields** — all aggregate brace-init sites are the
  single `getPortMapEntries()` switch plus the registered-peripheral fallback
  (internal; compiler finds them all).
- **`/ports` schema is additive** — `tags`/`latch` appear on rows; existing
  consumers ignore unknown fields. Golden fixtures for `/ports` are updated
  once, in the same commit.
- **`/state/memory` is not changed** — it keeps its consumers; E-1 is solved
  by the new endpoint plus a deprecation note in OpenAPI pointing at
  `/state/paging` for paging questions (fields stay, zero deletions).
- **`RegisterPortHandler`** — old two-argument signature keeps working
  (`tags = None`); call sites migrate opportunistically.
- **Porttrace** — `PortTraceDecodeRule` export can carry tags in a v2 format
  later (optional follow-up, not in scope; noted so the enum is designed to
  survive it).

## 8. Test plan

All tests in `core/tests/` (GTest, CUT pattern), names follow the
`ClassName_Test` convention:

- `PortDecoder_PortTag_Test` — per creatable model (48K, 128K, +2/+2A/+3,
  Pentagon128/512, Scorpion, Profi):
  - every static row has at least one category bit (no `None`-only rows;
    the single exception is the legacy untagged registered-peripheral
    fallback row, whose `tags = 0` reads as "category unknown");
  - the §3.2 table holds exactly (7FFD tagged `Memory|Rom|Screen` on all
    banked models; 48K has no `Memory`-tagged row; Scorpion 1FFD present
    with `Memory|Rom|Screen`; Profi DFFD with `Memory|Screen`;
    AY rows `SoundAy`; Pentagon `#FB` carries both `SoundCovox` and
    `SoundSoundDrive`);
  - family query: `GetSoundEntries(SoundAy)` returns exactly the AY rows;
  - latch binding: every `Memory`-tagged row has a non-None `latch`, and
    `ReadPagingLatch` maps it to the field the decoder actually writes
    (pinned by writing the `EmulatorState` field and asserting the read-back;
    the decoder-writes-the-field half is already pinned by the
    portdecoder_models_test decode sweeps).
- `StatePaging_Test` — endpoint handler: 128K golden response matches
  `/state/memory` values (acceptance from recommendations.md); Scorpion
  shows `p1FFD` + window decode; lock honors the hardware latch
  (`LockPaging()` → `paging_locked: true` even when the p7FFD cached bit
  disagrees); 48K `latches` is empty and banks report the fixed 48K layout.
- `RomIdentification_Test` (§5.2): a page buffer whose digest is in
  `ROM::_signatures` titles through `GetROMPageTitle`; an unknown buffer
  yields `"Unknown ROM, <digest>"` (never blank); `GetROMPageRole` matches
  the per-model layout table for every creatable model; the paging banks
  rows carry `role`+`name`+`signature`, and a role/name disagreement is
  visible in the same response.
- Parity tests — `CliPaging_Test` (formatter), Lua/Python smoke via the
  existing binding test harness where present; `/ports` fixture updates.
- Invariant test — `GetPagingLatches()` output equals the set of rows with
  non-None `latch` filtered by the requested categories (guards the two
  query paths from drifting).

## 9. Acceptance criteria

1. After `OUT (#FF77),0` on an ATM710 build, `/state/paging` shows the FF77
   latch row with `value: "0x00"` (rides the atm branch tagging its rows —
   master acceptance uses 1FFD on Scorpion: after `OUT (#1FFD),2` the
   `shadow_monitor_paged` decode flips).
2. Bank table never reports 65535 for mapped RAM on master's creatable
   models; 128K output matches today's `/state/memory` values.
3. `GET /ports` rows carry `tags`; `HasAnyTaggedPort(PortTag::SoundAy)` is
   true on every model — the sound index answers without model switches.
4. All five interfaces (WebAPI, MCP, CLI, Lua, Python) return the same
   latch set and values for the same machine state (parity).
5. Zero warnings; full suite green; `/state/memory` byte-identical for its
   existing fields (no regressions).
6. On a healthy 128K boot the bank-0 ROM row reads
   `role: "128K Editor/Menu ROM"`,
   `name: "128k ROM 0 (128k editor & menu)"` — matching; swapping in a
   wrong ROM image flips `name` to a different/`Unknown` title with the
   role unchanged (the mismatch is visible in one response, §5.2).

## 10. Open questions — resolved in the 2026-09-15 review

1. **`state ports` vs `paging` CLI command** — *decided:* `paging` is the
   dedicated command for latch values and the bank table; `state ports`
   stays reserved for a future, broader per-register last-written dump
   (keeps the `command-interface.md` planned row meaningful).
2. **Joystick category placement** — *decided:* `Joystick` and `Mouse` stay
   distinct top-level category bits as drafted; if an "all input ports"
   query ever appears, an `Input` bitmask alias over the two composes them
   without schema changes.
3. **Decoded-bits vocabulary** — *decided:* the §5.1 dictionary is
   normative and mirrored into `openapi_paging.inc` as a per-`PagingLatch`
   table generated from the same interpreter that produces the values.
4. **GS / MoonSound member tags** — *decided:* enforced by the §8 invariant
   (any `Sound`-family row on a build with the device present must carry a
   member bit); reserved member values already encode this intent.

## 11. Rollout sequence

1. ✅ **Done (2026-09-15, Phase 1):** `PortTag`/`PagingLatch` enums +
   constexpr operators + `Tags()` helper, `PortMapEntry` extension with
   default member initializers, full tag assignment in `getPortMapEntries()`
   (§3.2 table), the tagged `RegisterPortHandler` overload (tags stored,
   legacy signature unchanged), `ReadPagingLatch`, and the query methods —
   core only, no surface change. Tests: `PortDecoder_PortTag_Test` (13
   cases, portdecoder_porttag_test.cpp). Full 20-shard suite green.
2. ✅ **Done (2026-09-15, Phase 2):** WebAPI `GET /state/paging` (+ OpenAPI
   `openapi_state.inc` entry), MCP `inspect_state` aspect `paging`, and the
   §5.2 ROM identification surface: `ROM::GetROMPageRole` as the core
   single-source layout table (moved out of `state_memory_api.cpp`), ROM
   bank rows carry `role`/`name`/`signature`. The per-page title cache
   `GetROMPageTitle` was folded into the existing `SignatureCache` +
   `GetROMTitle` path (a separate cache would have duplicated it).
3. ✅ **Done (2026-09-15, Phase 3 — full parity wiring):** CLI `paging`
   command, Lua/Python `paging_state()` — and the `/ports` tagging wiring
   on **every** surface: `/ports` rows carry `tags`+`latch` (WebAPI,
   OpenAPI `openapi_ports.inc`), the CLI `ports` table gained
   **Tags**/**Latch** columns, Lua/Python `ports_map()` entries gained the
   `tags`/`latch` keys, and MCP gained a first-class `ports` aspect
   (previously only generic `invoke_api`). All names flow from three core
   single-source serializers — `PortTagSetToStrings` (reports **every**
   sound member; the first WebAPI-local draft dropped the second member of
   Pentagon #FB — caught by `TagNamesCarryEverySoundMember`),
   `PagingLatchToString`, `DecodePagingLatch` (§5.1 dictionary, native
   int/bool per surface) — plus `ROM::GetROMPageRole`. The per-surface
   decode/role copies in WebAPI/CLI/Lua/Python were deleted, not just
   supplemented. Docs updated: `command-interface.md` (§3.2 row + parity
   matrix — MCP cell now a real aspect), `webapi-interface.md` (ports row
   + previously missing `/state/paging` row), `lua-interface.md` /
   `python-interface.md` (`ports_map` shape + `paging_state()` blocks),
   `mcp/README.md` aspect list. Tests: `PortDecoder_PortTag_Test` grew to
   19 cases (serialization ×5, dictionary, role table).
4. Optional follow-ups (separate decisions): porttrace export tags,
   P2-3 capabilities fed from `HasAnyTaggedPort`.
