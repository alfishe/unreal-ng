#pragma once

/// @file l2model.h
/// @brief L2 of the analysis layers (docs/emulator/analysis/analysis-layers.md, "L2: structure"): what the bytes
/// are. The detectors (detectors/detectors.h) read L1 (the replay's code behaviour and per-byte uses) and the claims
/// made before them, and add regions to one RegionStore. A region names a physical range (the Z80 view is derived
/// through the paging the replay started with), a kind, its geometry, the routines that use it, the evidence that
/// produced it and an integer confidence 0..1000. A byte may carry several claims; conflicts are kept, nothing is
/// resolved silently. The store's order is fixed (address, then detector, then the claim's own order), so one
/// recording gives the same bytes on every compiler.
///
/// @code
///   analysis::L2Result l2 = analysis::RunDetectors(l1);       // detectors/detectors.h
///   for (const analysis::Region& r : l2.store.Regions()) ...
///   std::string ctl = analysis::ExportCtl(l1, l2, options);   // l2export.h
/// @endcode

#include <cstdint>
#include <string>
#include <vector>

#include "debugger/analyzers/analysis/l1model.h"
#include "emulator/state/statenode.h"

namespace analysis
{

enum class RegionKind : uint8_t
{
    Code,          ///< executed (or reached from executed code)
    Data,          ///< read or written as data, no finer claim
    Text,          ///< printable characters a print path reads
    Font,
    Sprite,
    Tile,
    Screen,
    Buffer,        ///< filled by a copy from several sources (geometry.instances)
    InitTable,     ///< written once early, then only read; content checked against a generator
    RecordArray,   ///< base + offset accesses with a constant step
    Variable,      ///< a byte or a word outside the other regions (geometry.role)
    PointerTable,  ///< words that address data
    HandlerTable,  ///< words that address code (JP (rr), PUSH + RET)
    RngState,
    MusicData,
    Packed,
    LevelMap,
    Unused,        ///< never touched, or only by the stack; geometry.text holds decoded source lines
    Untouched,     ///< no access seen and no claim fits: exported as data, at the lowest confidence
};
const char* RegionKindName(RegionKind kind);
bool ParseRegionKind(const std::string& text, RegionKind& kind);

/// The detectors, in their fixed order (the store sorts claims of one address by it)
enum class DetectorId : uint8_t
{
    Signatures,
    CodeMap,
    Text,
    InitTables,
    Records,
    PointerTables,
    Variables,
    Graphics,   ///< fonts, sprites, tiles: what blit loops drew
    LevelMaps,  ///< the maps a tile loop walked
    Screens,    ///< pictures and back buffers copied to the screen
    Rng,
    Unpackers,
    Music,      ///< player signatures and the data an AY player read
};
const char* DetectorName(DetectorId id);

enum class RegionStatus : uint8_t
{
    Observed,  ///< the evidence is a direct observation of the replay
    Inferred,  ///< derived (a branch never taken, a field size from a gap, a pattern in unread bytes)
};
const char* RegionStatusName(RegionStatus status);

/// One fact a claim rests on ("reader $92BA x12", "terminator $FF read at +0", "pattern rom48.print-out at +0")
struct Evidence
{
    std::string what;      ///< reader, writer, pattern, generator, step, terminator, loop, pointer, copy, ...
    uint16_t z80 = 0;      ///< the instruction / routine / address the fact names (0: none)
    uint64_t count = 0;
    std::string detail;
};

/// Variable roles (Geometry::role)
enum class VarRole : uint8_t
{
    None,
    Counter,  ///< changed only by INC / DEC
    Flag,     ///< two values
    Pointer,  ///< its value addressed memory
    Word,     ///< read or written as a pair
    Value,    ///< anything else
};
const char* VarRoleName(VarRole role);

struct Field
{
    uint16_t offset = 0;
    uint16_t size = 1;
    std::string name;      ///< from the kind of the claim inside the field ("text", "word", "counter", ...)
    uint8_t minValue = 0;  ///< the values seen in the field's first byte over all instances, start and end images
    uint8_t maxValue = 0;
    bool word = false;
};

struct Geometry
{
    uint16_t width = 0;   ///< glyph / sprite width in pixels; text: characters; table: entries; maps: cells
    uint16_t height = 0;  ///< graphics: pixel rows; maps: cells
    uint16_t frames = 0;
    uint16_t stride = 0;  ///< records: the step; tables: the entry size; graphics: the step from frame to frame
    uint16_t count = 0;   ///< records / entries
    std::vector<Field> fields;
    std::vector<uint16_t> instances;  ///< records / buffers: the Z80 bases of the other copies (sorted)
    std::string terminator;           ///< text / records: "zero", "bit7", "length", "$FF", ...
    std::string generator;            ///< init tables: "screen-rows", "x*8", "shift", "sine8", "bit-reverse"
    std::string text;                 ///< text: the characters; unused source: the decoded lines
    VarRole role = VarRole::None;
    uint8_t minValue = 0;             ///< variables: the values seen
    uint8_t maxValue = 0;
    /// Graphics: a mask byte before each graphic byte (mask, graphic, mask, graphic ...)
    bool mask = false;
    /// Graphics: bytes before each frame's pixel rows (a tile's attribute)
    uint16_t prefix = 0;
    /// Graphics and screens: how the bytes are laid out: "rows" (row by row, width / 8 bytes a row), "screen" (the
    /// display file's order), "attributes" (one byte a cell, 32 a row), "cells" (a map: one byte a cell)
    std::string layout;
};

struct Region
{
    PhysKey lo = 0;
    PhysKey hi = 0;       ///< inclusive, same page as lo
    uint16_t z80 = 0;     ///< the Z80 address of lo through the start's paging
    RegionKind kind = RegionKind::Data;
    Geometry geometry;
    std::vector<uint16_t> users;  ///< Z80 entries of the routines that read or wrote it (sorted, at most 16)
    std::vector<Evidence> evidence;
    uint16_t confidence = 0;      ///< 0..1000
    RegionStatus status = RegionStatus::Observed;
    DetectorId detector = DetectorId::CodeMap;
    std::string label;            ///< signatures, roles; empty: the export names it
    std::string title;            ///< empty: the export writes it from kind and geometry
    uint32_t order = 0;           ///< the claim's order within its detector (set by the store)

    uint32_t Size() const { return hi - lo + 1; }
    uint16_t Z80End() const { return static_cast<uint16_t>(z80 + (hi - lo)); }
};

/// Integer confidence 0..1000: the same numbers on every compiler (no floating point anywhere)
namespace confidence
{
constexpr uint16_t kMax = 1000;
constexpr uint16_t kReview = 700;   ///< below: the review queue
constexpr uint16_t kWeak = 300;     ///< below: a guess the export shows as a guess
uint16_t Clamp(int64_t value);
/// Two independent supports of one claim: 1 - (1 - a)(1 - b), in thousandths
uint16_t Combine(uint16_t a, uint16_t b);
/// @p value scaled by @p num / @p den, rounded down
uint16_t Scale(uint16_t value, uint32_t num, uint32_t den);
/// The confidence that grows with @p count observations: base + (max - base) * count / (count + half)
uint16_t FromCount(uint16_t base, uint16_t max, uint64_t count, uint64_t half);
}  // namespace confidence

class RegionStore
{
public:
    /// False (and nothing added) when the region has no evidence, an empty or inverted range, or a range across
    /// pages
    bool Add(Region region);
    /// Sort into the fixed order: lo, detector, order. Called once the detectors ran
    void Finish();

    const std::vector<Region>& Regions() const { return _regions; }
    /// The claims covering @p key (in the store's order)
    std::vector<const Region*> ClaimsAt(PhysKey key) const;
    /// The claims overlapping [lo, hi]
    std::vector<const Region*> Overlapping(PhysKey lo, PhysKey hi) const;
    /// Whether any claim of @p kind (or, with Data, any claim at all but Untouched) covers @p key
    bool Claimed(PhysKey key) const;
    bool ClaimedAs(PhysKey key, RegionKind kind) const;

    /// Two claims on one byte whose kinds cannot both hold (code and text, two record geometries, ...)
    struct Conflict
    {
        uint32_t a = 0;  ///< region indices
        uint32_t b = 0;
    };
    std::vector<Conflict> Conflicts() const;

    size_t Size() const { return _regions.size(); }
    /// Replace the field list of region @p index (buffers get theirs once every detector ran)
    void SetFields(size_t index, std::vector<Field> fields) { _regions.at(index).geometry.fields = std::move(fields); }

private:
    std::vector<Region> _regions;
    uint32_t _next = 0;
    bool _sorted = true;
};

/// An instruction of the code map: executed, or reached from executed code over a branch never taken
struct CodeLine
{
    PhysKey key = 0;
    uint16_t z80 = 0;
    uint8_t length = 1;
    uint8_t bytes[4] = {};
    bool executed = false;
    bool entry = false;       ///< a routine entry (L1) or a CALL / JP target from outside its routine
    bool smc = false;         ///< written while it was code
};

struct L2Result
{
    RegionStore store;
    std::vector<CodeLine> code;  ///< sorted by key
    /// Routine entries (Z80, the start's paging) with their names: signatures, roles, then sub_XXXX
    std::vector<std::pair<uint16_t, std::string>> entries;
    std::vector<std::string> notes;  ///< what the detectors could not do (a page not mapped at the start, ...)

    const CodeLine* CodeAt(PhysKey key) const;

    StateNode ToStateNode() const;
};

StateNode RegionNode(const Region& region);

}  // namespace analysis
