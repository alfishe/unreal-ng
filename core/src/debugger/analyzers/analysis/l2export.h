#pragma once

/// @file l2export.h
/// @brief The L2 draft export (docs/emulator/analysis/analysis-layers.md, "Export"): the region store as a SkoolKit
/// control file (sna2skool.py -c), a skool draft that builds relocatable (@label on every referenced address, @isub
/// where an operand points inside an instruction or names a page, @keep on constants that equal an address,
/// ALIGN 256 before page-aligned tables, @org at the fixed tail), the record field lists and graphics geometry as
/// JSON (tools/verification/analysis/score_draft.py --fields / --geometry), the relocation layout (zx-reaper
/// re/layout.mk lines), a symbol set through the unreal-asm codecs, and a plain region map.
///
/// @code
///   analysis::ExportOptions options;
///   options.ranges = {{0x5900, 0x59FF}, {0x8000, 0xFFFF}};
///   options.disassemble = [](const uint8_t* bytes, uint16_t z80) { return std::string("nop"); };
///   analysis::ExportResult skool = analysis::Export(l1, l2, "skool", options);
/// @endcode

#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

#include "debugger/analyzers/analysis/l1model.h"
#include "debugger/analyzers/analysis/l2model.h"

namespace analysis
{

struct ExportRange
{
    uint16_t start = 0;
    uint16_t end = 0;  ///< inclusive
};

/// What the review (L3, l3findings.h) adds at one address: a label and a title an agent or a human gave, comments,
/// the coverage notes ("needs poke $8100=5")
struct ExportNote
{
    std::string label;  ///< empty: the draft's own
    std::string title;
    std::vector<std::string> comments;
    /// The kind the review gave [address, kindEnd] ("code", "text", "data", "sprite", ...; RegionKindName): the range
    /// becomes one entry of that kind (code: decoded from the start image), the claims under it leave the fields and
    /// geometry JSON. Empty: the draft's own.
    std::string kind;
    uint16_t kindEnd = 0;
    bool entry = false;  ///< a routine starts here (an entry candidate the review accepted): the code entry splits
};

struct ExportOptions
{
    /// The Z80 ranges written (each starts an @org); empty: $4000-$FFFF
    std::vector<ExportRange> ranges;
    /// Disassembly of one instruction (Zilog syntax; '#' or '$' hex): the skool's instruction text
    std::function<std::string(const uint8_t* bytes, uint16_t z80)> disassemble;
    uint16_t defbWidth = 8;               ///< bytes per DEFB line (skool2asm writes them as they are)
    std::string symbolCodec = "skoolkit-ctl";
    /// L3 notes by Z80 address. At an entry's start: its label (unless another entry has it), its title, comments
    /// first in its description; inside an entry: the label replaces the draft's point label there (or starts one;
    /// a comment line "$XXXX: named ..." where no label can go: inside an instruction), the title and comments are
    /// comment lines "$XXXX: ..." in the entry's description
    std::map<uint16_t, ExportNote> notes;
};

struct ExportResult
{
    bool ok = false;
    std::string error;
    std::string text;   ///< the file
    uint32_t entries = 0;
};

/// "ctl", "skool", "fields", "geometry", "layout", "symbols", "map"
ExportResult Export(const L1Result& l1, const L2Result& l2, const std::string& format, const ExportOptions& options);
const std::vector<std::string>& ExportFormats();

/// One entry of the draft (the plan the ctl and the skool share)
struct DraftSub
{
    uint16_t start = 0;
    uint16_t length = 0;
    char type = 'B';  ///< B T W
};

struct DraftEntry
{
    uint16_t start = 0;
    uint16_t end = 0;     ///< inclusive
    char ctl = 'b';       ///< c b t u w g
    std::string label;
    std::string title;
    std::vector<std::string> description;
    std::vector<DraftSub> subs;
    bool org = false;     ///< an @org starts here (a range start, the fixed tail)
    bool align = false;   ///< ALIGN 256 before it when shifted (a page-aligned table starts here or inside)
    const Region* region = nullptr;
    /// A source of a buffer (the copies brought it there; Geometry::instances): written as the buffer is, one entry
    /// with its claims as sub-blocks; `sourceIndex` counts the sources on their step from the lowest
    const Region* source = nullptr;
    uint32_t sourceIndex = 0;
};

struct DraftPlan
{
    std::vector<DraftEntry> entries;
    std::map<uint16_t, std::string> labels;          ///< every label: entry starts and points inside entries
    std::map<uint16_t, std::vector<uint16_t>> refs;  ///< labelled address -> the instructions that refer to it
    std::map<uint16_t, bool> relativeOnly;           ///< labelled address -> only JR / DJNZ refer to it
    std::vector<ExportRange> ranges;
    uint16_t regionStart = 0;   ///< the moved region of the relocation layout
    uint16_t shiftEnd = 0;      ///< the fixed tail starts here (= the end of the region + 1 when there is none)
};
DraftPlan MakePlan(const L1Result& l1, const L2Result& l2, const ExportOptions& options);

/// The title the draft gives a claim's entry ("16x16 sprites, 4 frames, used by $8E3C")
std::string RegionTitle(const Region& region);

/// "$XXXX" in a comment -> "#R$XXXX" where XXXX is an entry of @p entries (sorted)
std::string CommentRefs(const std::string& text, const std::vector<uint16_t>& entries);
/// Split @p bytes at @p start into DEFB lines of at most @p width bytes, breaking at each of @p breaks (sorted)
std::vector<std::pair<uint16_t, uint16_t>> SplitLines(uint16_t start, uint16_t length, uint16_t width,
                                                      const std::vector<uint16_t>& breaks);
/// Zilog text (lower case, '#' hex) in SkoolKit / sjasmplus spelling (upper case, '$' hex)
std::string SkoolSpelling(const std::string& text);

}  // namespace analysis
