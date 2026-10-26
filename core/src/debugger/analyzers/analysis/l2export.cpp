#include "debugger/analyzers/analysis/l2export.h"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdio>
#include <map>
#include <set>
#include <sstream>

#include "debugger/analyzers/analysis/z80opinfo.h"
#include "unrealasm/symbols/codec.h"
#include "unrealasm/symbols/symbol.h"

namespace analysis
{

namespace
{

std::string Hex4(uint32_t value)
{
    char text[8];
    std::snprintf(text, sizeof(text), "$%04X", value & 0xFFFF);
    return text;
}

std::string Hex2(uint32_t value)
{
    char text[8];
    std::snprintf(text, sizeof(text), "$%02X", value & 0xFF);
    return text;
}

std::string HexName(const char* prefix, uint16_t value)
{
    char text[24];
    std::snprintf(text, sizeof(text), "%s%04X", prefix, value);
    return text;
}

bool InRanges(const std::vector<ExportRange>& ranges, uint32_t z80)
{
    for (const ExportRange& r : ranges)
        if (z80 >= r.start && z80 <= r.end)
            return true;
    return false;
}

/// Which claim describes a byte best (containers aside)
int Priority(const Region& r)
{
    switch (r.kind)
    {
        case RegionKind::Code:
            return r.status == RegionStatus::Observed ? 100 : 60;
        case RegionKind::Text:
            return 90;
        case RegionKind::Unused:
            return 85;
        case RegionKind::RecordArray:
            return 80;
        case RegionKind::HandlerTable:
        case RegionKind::PointerTable:
            return 75;
        case RegionKind::InitTable:
            return 70;
        case RegionKind::Font:
        case RegionKind::Sprite:
        case RegionKind::Tile:
        case RegionKind::Screen:
        case RegionKind::LevelMap:
        case RegionKind::MusicData:
        case RegionKind::Packed:
        case RegionKind::RngState:
            return 65;
        case RegionKind::Data:
            return r.title == "terminator" ? 55 : 20;
        case RegionKind::Variable:
            return 50;
        case RegionKind::Untouched:
            return 10;
        case RegionKind::Buffer:
            return -1;
    }
    return 0;
}

char CtlOf(const Region& r)
{
    switch (r.kind)
    {
        case RegionKind::Code:
            return 'c';
        case RegionKind::Text:
            return 't';
        case RegionKind::Unused:
            return 'u';
        case RegionKind::Variable:
            return 'g';
        case RegionKind::PointerTable:
        case RegionKind::HandlerTable:
            return 'w';
        case RegionKind::InitTable:
            return r.geometry.stride == 2 ? 'w' : 'b';
        default:
            return 'b';
    }
}

const char* LabelPrefix(const Region& r)
{
    switch (r.kind)
    {
        case RegionKind::Code:
            return "sub_";
        case RegionKind::Text:
            return "text_";
        case RegionKind::Unused:
            return "unused_";
        case RegionKind::RecordArray:
            return "records_";
        case RegionKind::Buffer:
            return "buffer_";
        case RegionKind::PointerTable:
            return "ptrs_";
        case RegionKind::HandlerTable:
            return "handlers_";
        case RegionKind::InitTable:
            return "table_";
        case RegionKind::Font:
            return "font_";
        case RegionKind::Sprite:
            return "sprite_";
        case RegionKind::Tile:
            return "tile_";
        case RegionKind::Screen:
            return "screen_";
        case RegionKind::LevelMap:
            return "map_";
        case RegionKind::MusicData:
            return "music_";
        case RegionKind::Packed:
            return "packed_";
        case RegionKind::RngState:
            return "rng_";
        case RegionKind::Variable:
            return "var_";
        case RegionKind::Untouched:
            return "untouched_";
        default:
            return "data_";
    }
}

std::string Plural(uint64_t n, const char* one, const char* many)
{
    return std::to_string(n) + " " + (n == 1 ? one : many);
}

/// The entry title from kind and geometry
std::string TitleOf(const Region& r, uint32_t size)
{
    if (!r.title.empty() && r.title != "terminator")
        return r.title;
    const Geometry& g = r.geometry;
    std::string users;
    if (!r.users.empty())
    {
        users = ", used by ";
        for (size_t i = 0; i < r.users.size() && i < 3; ++i)
            users += (i ? " " : "") + Hex4(r.users[i]);
        if (r.users.size() > 3)
            users += " ...";
    }
    switch (r.kind)
    {
        case RegionKind::Code:
            return r.status == RegionStatus::Observed ? "Routine at " + Hex4(r.z80)
                                                      : "Code at " + Hex4(r.z80) + " (not executed in the replay)";
        case RegionKind::Text:
            return "Text, " + Plural(size, "character", "characters") +
                   (g.terminator.empty() ? "" : " (" + g.terminator + ")") + users;
        case RegionKind::Unused:
            return g.text.empty() ? "Unused" : "Unused: leftover source text, " + Plural(g.count, "line", "lines");
        case RegionKind::RecordArray:
            return Plural(g.count, "record", "records") + " of " + std::to_string(g.stride) + " bytes" +
                   (g.terminator.empty() ? "" : ", terminated by " + g.terminator) + users;
        case RegionKind::Buffer:
            return "Buffer, " + std::to_string(size) + " bytes, copied from " +
                   Plural(g.instances.size(), "source", "sources") + users;
        case RegionKind::PointerTable:
            return "Table of " + Plural(g.count, "pointer", "pointers") + users;
        case RegionKind::HandlerTable:
            return "Table of " + Plural(g.count, "handler", "handlers") + users;
        case RegionKind::InitTable:
            return "Table, " + std::to_string(size) + " bytes" +
                   (g.generator.empty() ? "" : " (" + g.generator + ")") + users;
        case RegionKind::Font:
            return g.frames != 0 ? "Font, " + Plural(g.frames, "character", "characters") + " of " +
                                       std::to_string(g.width) + "x" + std::to_string(g.height) + users
                                 : "Font" + users;
        case RegionKind::Sprite:
        case RegionKind::Tile:
            return std::to_string(g.width) + "x" + std::to_string(g.height) +
                   (r.kind == RegionKind::Sprite ? " sprite" : " tile") + (g.frames == 1 ? "" : "s") +
                   (g.frames > 1 ? ", " + Plural(g.frames, "frame", "frames") : std::string()) +
                   (g.mask ? ", masked" : "") +
                   (g.prefix != 0 ? ", " + Plural(g.prefix, "byte", "bytes") + " before each" : std::string()) +
                   users;
        case RegionKind::Screen:
            return (g.layout == "attributes" ? "Attributes, " : "Screen pixels, ") + std::to_string(g.width) + "x" +
                   std::to_string(g.height) + users;
        case RegionKind::LevelMap:
            return "Level map, " + std::to_string(g.width) + "x" + std::to_string(g.height) + " cells" + users;
        case RegionKind::MusicData:
            return "Music data, " + std::to_string(size) + " bytes" + users;
        case RegionKind::Packed:
            return "Packed data, " + std::to_string(size) + " bytes" + users;
        case RegionKind::RngState:
            return "Random number generator state" + users;
        case RegionKind::Variable:
        {
            std::string role = VarRoleName(g.role);
            if (!role.empty())
                role[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(role[0])));
            const std::string range = (g.role == VarRole::Word || g.role == VarRole::Pointer)
                                          ? std::string()
                                          : " (" + Hex2(g.minValue) + "-" + Hex2(g.maxValue) + ")";
            return (role.empty() ? std::string("Variable") : role) + range + users;
        }
        case RegionKind::Data:
            return r.title == "terminator" ? "Terminator" + users : "Data, " + std::to_string(size) + " bytes" + users;
        case RegionKind::Untouched:
            return "Not accessed in the replay";
        default:
            return std::string(RegionKindName(r.kind)) + users;
    }
}

/// The SkoolKit image macro showing a graphics claim's frames inside [start, end] (Z80), wrapped in #UDGTABLE (which
/// skool2asm drops: the draft's assembly does not change); empty when the geometry has no picture SkoolKit can draw.
/// Sprites and tiles: one #UDGARRAY a frame (at most 16), the mask (when interleaved) assumed before each graphic
/// byte, a tile's attribute from the byte before its rows; fonts: #FONT over the characters; screen pixels: #SCR on
/// the buffer as the display file. Decimal addresses: the export turns $nnnn into #R references.
std::string ImageMacro(const Region& r, uint32_t start, uint32_t end)
{
    const Geometry& g = r.geometry;
    const std::string name = [&] {
        std::string text = HexName(LabelPrefix(r), static_cast<uint16_t>(start));
        std::transform(text.begin(), text.end(), text.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return text;
    }();
    if (r.kind == RegionKind::Font)
    {
        const uint32_t chars = std::min<uint32_t>(96u, (end - start + 1u) / 8u);
        if (g.width != 8 || g.height != 8 || chars == 0)
            return std::string();
        return "#UDGTABLE { #FONT" + std::to_string(start) + "," + std::to_string(chars) + "(" + name + ") } TABLE#";
    }
    if (r.kind == RegionKind::Screen)
    {
        if (g.layout != "screen" || g.height < 8 || g.height % 8 != 0 || end - start + 1u < 2048u)
            return std::string();
        return "#UDGTABLE { #SCR2,0,0,32," + std::to_string(std::min<uint32_t>(g.height / 8u, 24u)) + "," +
               std::to_string(start) + "(" + name + ") } TABLE#";
    }
    if (r.kind != RegionKind::Sprite && r.kind != RegionKind::Tile)
        return std::string();
    if (g.width == 0 || g.width % 8 != 0 || g.height == 0 || g.height % 8 != 0 || g.width > 64)
        return std::string();
    const uint32_t columns = g.width / 8u;
    const uint32_t rowStep = columns * (g.mask ? 2u : 1u);
    const uint32_t frameBytes = rowStep * g.height + g.prefix;
    const uint32_t stride = g.stride != 0 ? g.stride : frameBytes;
    const uint32_t scale = g.width <= 16 ? 4u : 2u;
    std::string cells;
    uint32_t shown = 0;
    for (uint32_t base = r.z80; base + frameBytes - 1u <= std::min<uint32_t>(end, 0xFFFFu) && shown < 16;
         base += stride)
    {
        if (base < start)
            continue;
        const uint32_t pixels = base + g.prefix;
        const uint32_t across = g.mask ? 2u : 1u;
        const uint32_t down = 8u * rowStep;
        const uint32_t first = pixels + (g.mask ? 1u : 0u);
        const uint32_t last = first + (columns - 1u) * across + (g.height / 8u - 1u) * down;
        std::string spec = std::to_string(first) + "-" + std::to_string(last) + "-" + std::to_string(across) + "-" +
                           std::to_string(down);
        if (g.mask)
            spec += ":" + std::to_string(first - 1u) + "-" + std::to_string(last - 1u) + "-" + std::to_string(across) +
                    "-" + std::to_string(down);
        std::string attrs;
        if (g.prefix != 0)
            attrs = "[" + std::to_string(base) + "]";
        cells += std::string(shown == 0 ? "" : " | ") + "#UDGARRAY" + std::to_string(columns) + ",56," +
                 std::to_string(scale) + "," + std::to_string(rowStep) + "(" + spec + ")" + attrs + "(" + name + "_" +
                 std::to_string(shown) + ")";
        ++shown;
    }
    return cells.empty() ? std::string() : "#UDGTABLE { " + cells + " } TABLE#";
}

/// The 16-bit immediate of an instruction (op.immBytes == 2)
uint16_t Imm16(const OpInfo& op, const uint8_t* b)
{
    return static_cast<uint16_t>(b[op.immOffset & 3] | (b[(op.immOffset + 1) & 3] << 8));
}

/// The static branch target of a JP / JR / DJNZ / CALL / RST (false: none)
bool BranchTarget(const OpInfo& op, uint16_t z80, const uint8_t* b, uint16_t& target)
{
    switch (op.flow)
    {
        case Flow::Jp:
        case Flow::Call:
            target = Imm16(op, b);
            return true;
        case Flow::Jr:
        case Flow::Djnz:
            target = static_cast<uint16_t>(z80 + 2 + static_cast<int8_t>(b[1]));
            return true;
        default:
            return false;
    }
}

/// Everything the skool writer needs besides the plan: labels, line starts, operand rewrites
struct Relocation
{
    std::map<uint16_t, std::string> labels;      ///< address -> label (entries and referenced lines)
    std::set<uint16_t> breaks;                   ///< data lines start here
    std::map<uint16_t, std::string> isub;        ///< instruction -> its rewritten text
    std::set<uint16_t> keep;                     ///< instruction -> @keep
    std::set<uint16_t> align;                    ///< ALIGN 256 before this line
};

struct Exporter
{
    const L1Result& l1;
    const L2Result& l2;
    const ExportOptions& options;
    DraftPlan plan;
    Relocation reloc;
    std::vector<uint8_t> image;      ///< the start image in the Z80 view
    std::vector<const CodeLine*> lineAt;  ///< instruction starting at an address
    std::vector<const CodeLine*> lineOver;  ///< instruction covering an address

    Exporter(const L1Result& l1In, const L2Result& l2In, const ExportOptions& optionsIn)
        : l1(l1In), l2(l2In), options(optionsIn), image(0x10000, 0), lineAt(0x10000, nullptr), lineOver(0x10000, nullptr)
    {
        for (uint32_t a = 0; a < 0x10000; ++a)
        {
            uint8_t v = 0;
            l1.StartByte(l1.StartKey(static_cast<uint16_t>(a)), v);
            image[a] = v;
        }
        for (const CodeLine& c : l2.code)
        {
            lineAt[c.z80] = &c;
            for (uint8_t i = 0; i < c.length; ++i)
                lineOver[static_cast<uint16_t>(c.z80 + i)] = &c;
        }
    }

    std::vector<ExportRange> Ranges() const
    {
        if (!options.ranges.empty())
            return options.ranges;
        return {ExportRange{0x4000, 0xFFFF}};
    }

    uint16_t Z80(const Region& r) const { return r.z80; }

    void BuildPlan()
    {
        plan.ranges = Ranges();
        // The best claim of each byte, and the container (buffer) around it
        std::vector<const Region*> owner(0x10000, nullptr);
        std::vector<const Region*> container(0x10000, nullptr);
        for (const Region& r : l2.store.Regions())
        {
            const uint32_t lo = r.z80;
            const uint32_t hi = std::min<uint32_t>(0xFFFF, lo + r.Size() - 1);
            for (uint32_t a = lo; a <= hi; ++a)
            {
                if (r.kind == RegionKind::Buffer)
                {
                    if (container[a] == nullptr || container[a]->Size() < r.Size())
                        container[a] = &r;
                    continue;
                }
                // Variable groups and strided variables describe variables the export writes one by one (their
                // fields go to the fields JSON and the buffer's field list)
                if (r.kind == RegionKind::RecordArray && r.detector == DetectorId::Variables)
                    continue;
                const Region* o = owner[a];
                if (o == nullptr || Priority(r) > Priority(*o) ||
                    (Priority(r) == Priority(*o) && r.confidence > o->confidence))
                    owner[a] = &r;
            }
        }
        std::set<uint16_t> entryStarts;
        for (const auto& e : l2.entries)
            entryStarts.insert(e.first);

        for (const ExportRange& range : plan.ranges)
        {
            uint32_t a = range.start;
            bool first = true;
            while (a <= range.end)
            {
                const Region* box = container[a];
                if (box != nullptr && box->z80 == a && box->z80 + box->Size() - 1 <= range.end)
                {
                    DraftEntry e;
                    e.start = static_cast<uint16_t>(a);
                    e.end = static_cast<uint16_t>(a + box->Size() - 1);
                    e.ctl = 'b';
                    e.region = box;
                    e.org = first;
                    BufferSubs(e, owner);
                    plan.entries.push_back(std::move(e));
                    a = static_cast<uint32_t>(plan.entries.back().end) + 1;
                    first = false;
                    continue;
                }
                const Region* o = owner[a];
                uint32_t b = a;
                while (b + 1 <= range.end && owner[b + 1] == o && (container[b + 1] == nullptr || container[b + 1]->z80 != b + 1))
                {
                    if (o != nullptr && o->kind == RegionKind::Code && entryStarts.count(static_cast<uint16_t>(b + 1)) != 0)
                        break;
                    // A code region: an instruction never splits
                    ++b;
                }
                DraftEntry e;
                e.start = static_cast<uint16_t>(a);
                e.end = static_cast<uint16_t>(b);
                e.ctl = o != nullptr ? CtlOf(*o) : 'i';
                e.region = o;
                e.org = first;
                Subs(e);
                plan.entries.push_back(std::move(e));
                a = b + 1;
                first = false;
            }
        }

        // The relocation layout: the range with the most code moves; its fixed tail starts after its last unused
        // block of at least $101 bytes
        uint32_t bestCode = 0;
        const ExportRange* moved = plan.ranges.empty() ? nullptr : &plan.ranges.front();
        for (const ExportRange& range : plan.ranges)
        {
            uint32_t code = 0;
            for (uint32_t a = range.start; a <= range.end; ++a)
                code += lineOver[a] != nullptr ? 1 : 0;
            if (code > bestCode)
            {
                bestCode = code;
                moved = &range;
            }
        }
        if (moved != nullptr)
        {
            plan.regionStart = moved->start;
            plan.shiftEnd = static_cast<uint16_t>(std::min<uint32_t>(moved->end + 1u, 0xFFFF));
            const DraftEntry* lastUnused = nullptr;
            for (const DraftEntry& e : plan.entries)
                if (e.start >= moved->start && e.end <= moved->end && e.ctl == 'u' &&
                    static_cast<uint32_t>(e.end - e.start + 1) >= 0x101)
                    lastUnused = &e;
            if (lastUnused != nullptr && lastUnused->end < moved->end)
            {
                plan.shiftEnd = static_cast<uint16_t>(lastUnused->end + 1);
                for (DraftEntry& e : plan.entries)
                    if (e.start == plan.shiftEnd)
                        e.org = true;
            }
        }

        // Labels: entries first
        for (DraftEntry& e : plan.entries)
        {
            std::string name;
            if (e.region != nullptr && !e.region->label.empty() && e.region->z80 == e.start)
                name = e.region->label;
            if (e.ctl == 'c')
                for (const auto& en : l2.entries)
                    if (en.first == e.start && !en.second.empty())
                        name = en.second;
            if (name.empty())
                name = HexName(e.region != nullptr ? LabelPrefix(*e.region) : "data_", e.start);
            e.label = name;
            e.title = e.region != nullptr ? TitleOf(*e.region, e.end - e.start + 1u) : "Not claimed";
            if (e.region != nullptr && e.region->kind == RegionKind::Code && e.region->z80 != e.start)
                e.title = (e.region->status == RegionStatus::Observed ? "Routine at " : "Code at ") + Hex4(e.start) +
                          (e.region->status == RegionStatus::Observed ? "" : " (not executed in the replay)");
            reloc.labels[e.start] = name;
        }
        ApplyNoteLabels();
        Describe();
        ApplyNoteComments();
    }

    /// Sub-blocks of a plain entry: words for tables and word variables, a record's fields per record
    void Subs(DraftEntry& e)
    {
        const uint32_t size = e.end - e.start + 1u;
        if (e.region == nullptr)
            return;
        const Region& r = *e.region;
        if (e.ctl == 'w' || (e.ctl == 'g' && (r.geometry.role == VarRole::Word || r.geometry.role == VarRole::Pointer)))
        {
            e.subs.push_back(DraftSub{e.start, static_cast<uint16_t>(size & ~1u), 'W'});
            if (size & 1u)
                e.subs.push_back(DraftSub{static_cast<uint16_t>(e.end), 1, 'B'});
        }
        else if (r.kind == RegionKind::RecordArray && r.geometry.stride != 0 && !r.geometry.fields.empty())
        {
            for (uint32_t base = e.start; base + r.geometry.stride - 1 <= e.end; base += r.geometry.stride)
                for (const Field& f : r.geometry.fields)
                    e.subs.push_back(DraftSub{static_cast<uint16_t>(base + f.offset), f.size, f.word ? 'W' : 'B'});
        }
        else if (e.ctl == 't')
            e.subs.push_back(DraftSub{e.start, static_cast<uint16_t>(size), 'T'});
    }

    /// A buffer: one sub-block per owner run inside it (text runs T, words W, the rest B)
    void BufferSubs(DraftEntry& e, const std::vector<const Region*>& owner)
    {
        uint32_t a = e.start;
        while (a <= e.end)
        {
            const Region* o = owner[a];
            uint32_t b = a;
            while (b + 1 <= e.end && owner[b + 1] == o)
                ++b;
            char type = 'B';
            if (o != nullptr && o->kind == RegionKind::Text)
                type = 'T';
            else if (o != nullptr && o->kind == RegionKind::Variable &&
                     (o->geometry.role == VarRole::Word || o->geometry.role == VarRole::Pointer) && b - a + 1 == 2)
                type = 'W';
            if (o != nullptr && o->kind == RegionKind::RecordArray && o->geometry.stride != 0 &&
                !o->geometry.fields.empty() && o->z80 == a)
            {
                for (uint32_t base = a; base + o->geometry.stride - 1 <= b; base += o->geometry.stride)
                    for (const Field& f : o->geometry.fields)
                        e.subs.push_back(DraftSub{static_cast<uint16_t>(base + f.offset), f.size, f.word ? 'W' : 'B'});
            }
            else
                e.subs.push_back(DraftSub{static_cast<uint16_t>(a), static_cast<uint16_t>(b - a + 1), type});
            // Inner claims get labels at their starts
            if (o != nullptr && o->z80 == a && o->kind != RegionKind::Untouched &&
                !(o->kind == RegionKind::Data && o->title != "terminator"))
                reloc.labels.emplace(static_cast<uint16_t>(a),
                                     o->label.empty() ? HexName(LabelPrefix(*o), static_cast<uint16_t>(a)) : o->label);
            a = b + 1;
        }
    }

    /// L3: the labels and titles the review gave entry starts (a label another entry has stays the draft's)
    void ApplyNoteLabels()
    {
        if (options.notes.empty())
            return;
        std::set<std::string> taken;
        for (const DraftEntry& e : plan.entries)
            taken.insert(e.label);
        for (DraftEntry& e : plan.entries)
        {
            const auto it = options.notes.find(e.start);
            if (it == options.notes.end())
                continue;
            const ExportNote& note = it->second;
            if (!note.label.empty() && note.label != e.label && taken.count(note.label) == 0)
            {
                taken.erase(e.label);
                taken.insert(note.label);
                e.label = note.label;
                reloc.labels[e.start] = note.label;
            }
            if (!note.title.empty())
                e.title = note.title;
        }
    }

    /// L3: the comments, first in an entry's description; a note inside an entry names its address
    void ApplyNoteComments()
    {
        if (options.notes.empty())
            return;
        for (DraftEntry& e : plan.entries)
        {
            std::vector<std::string> lines;
            for (auto it = options.notes.lower_bound(e.start); it != options.notes.end() && it->first <= e.end; ++it)
            {
                const bool atStart = it->first == e.start;
                for (const std::string& comment : it->second.comments)
                    lines.push_back(atStart ? comment : Hex4(it->first) + ": " + comment);
                if (!atStart && !it->second.label.empty())
                    lines.push_back(Hex4(it->first) + ": named " + it->second.label + " in the review.");
            }
            if (!lines.empty())
                e.description.insert(e.description.begin(), lines.begin(), lines.end());
        }
    }

    void Describe()
    {
        std::map<uint16_t, std::set<uint16_t>> callers;
        for (const CallEdge& c : l1.calls)
            if (c.kind != CallKind::Interrupt && c.kind != CallKind::FallsInto)
                callers[l1.routines[c.callee].z80].insert(l1.routines[c.caller].z80);
        for (DraftEntry& e : plan.entries)
        {
            if (e.region == nullptr)
                continue;
            const Region& r = *e.region;
            if (const std::string image = ImageMacro(r, e.start, e.end); !image.empty())
                e.description.push_back(image);
            if (e.ctl == 'c')
            {
                const auto it = callers.find(e.start);
                if (it != callers.end() && !it->second.empty())
                {
                    std::string line = "Used by";
                    size_t n = 0;
                    for (const uint16_t c : it->second)
                    {
                        if (n++ == 8)
                        {
                            line += " ...";
                            break;
                        }
                        line += " " + Hex4(c);
                    }
                    e.description.push_back(line + ".");
                }
                const Routine* routine = l1.RoutineAtZ80(e.start);
                if (routine != nullptr && routine->z80 == e.start)
                    e.description.push_back("Entered " + Plural(routine->profile.entries, "time", "times") + " (" +
                                            EntryKindNames(routine->kinds) + "), " +
                                            std::to_string(routine->instructions) + " instructions executed.");
            }
            for (const Evidence& ev : r.evidence)
            {
                std::string line = ev.what;
                if (ev.z80 != 0)
                    line += " " + Hex4(ev.z80);
                if (ev.count != 0)
                    line += " x" + std::to_string(ev.count);
                if (!ev.detail.empty())
                    line += ": " + ev.detail;
                e.description.push_back(line + ".");
                if (e.description.size() >= 8)
                    break;
            }
            if (r.kind == RegionKind::Unused && !r.geometry.text.empty())
            {
                std::istringstream lines(r.geometry.text);
                std::string line;
                int n = 0;
                while (std::getline(lines, line) && n++ < 6)
                    e.description.push_back("Source: " + line);
            }
            if (!r.geometry.fields.empty())
            {
                std::string line = "Fields:";
                for (const Field& f : r.geometry.fields)
                    line += " +" + std::to_string(f.offset) + "/" + std::to_string(f.size) +
                            (f.name.empty() ? "" : " " + f.name);
                e.description.push_back(line);
            }
            if (!r.geometry.instances.empty())
            {
                std::string line = "Instances:";
                size_t n = 0;
                for (const uint16_t i : r.geometry.instances)
                {
                    if (n++ == 24)
                    {
                        line += " ...";
                        break;
                    }
                    line += " " + Hex4(i);
                }
                e.description.push_back(line);
            }
            e.description.push_back("Confidence " + std::to_string(r.confidence) + ", " +
                                    RegionStatusName(r.status) + " (" + DetectorName(r.detector) + ").");
        }
    }

    bool PointerEvidence(PhysKey key) const
    {
        for (const ImmediateAddress& i : l1.immediateAddresses)
            if (i.instruction == key && i.uses != 0)
                return true;
        for (const PointerUse& p : l1.pointers)
            if (p.form == PointerForm::Immediate && p.origin == key && p.uses != 0)
                return true;
        return false;
    }

    const PointerUse* HighPointer(PhysKey key) const
    {
        for (const PointerUse& p : l1.pointers)
            if (p.form == PointerForm::ImmediateHigh && p.origin == key && p.uses != 0)
                return &p;
        return nullptr;
    }

    std::string LabelFor(uint16_t address)
    {
        auto it = reloc.labels.find(address);
        if (it == reloc.labels.end())
            it = reloc.labels.emplace(address, HexName("L", address)).first;
        reloc.breaks.insert(address);
        return it->second;
    }

    /// The instruction text with the number @p value (as $XXXX or $XX) replaced by @p expr
    static std::string Replace(const std::string& text, uint32_t value, bool word, const std::string& expr)
    {
        const std::string hex = word ? Hex4(value) : Hex2(value);
        const size_t at = text.rfind(hex);
        if (at == std::string::npos)
            return {};
        return text.substr(0, at) + expr + text.substr(at + hex.size());
    }

    std::string Text(const CodeLine& c) const
    {
        uint8_t bytes[4] = {};
        for (int i = 0; i < 4; ++i)
            bytes[i] = image[static_cast<uint16_t>(c.z80 + i)];
        return options.disassemble ? SkoolSpelling(options.disassemble(bytes, c.z80)) : std::string();
    }

    void Relocate()
    {
        const auto inRanges = [this](uint32_t a) { return InRanges(plan.ranges, a); };
        // Pass 1: the addresses code refers to get labels (line starts) or @isub (inside an instruction)
        std::vector<std::pair<const CodeLine*, uint16_t>> midRefs;
        for (const CodeLine& c : l2.code)
        {
            if (!inRanges(c.z80))
                continue;
            uint8_t b[4];
            for (int i = 0; i < 4; ++i)
                b[i] = image[static_cast<uint16_t>(c.z80 + i)];
            const OpInfo& op = DecodeOp(b);
            uint16_t target = 0;
            if (BranchTarget(op, c.z80, b, target) && inRanges(target))
            {
                const CodeLine* over = lineOver[target];
                if (over == nullptr || over->z80 == target)
                    LabelFor(target);
                else
                    midRefs.emplace_back(&c, target);
                continue;
            }
            if (op.immBytes == 2 && (op.immIsAddress || PointerEvidence(c.key)))
            {
                const uint16_t v = Imm16(op, b);
                if (!inRanges(v))
                    continue;
                const CodeLine* over = lineOver[v];
                if (over == nullptr || over->z80 == v)
                    LabelFor(v);
                else
                    midRefs.emplace_back(&c, v);
                continue;
            }
            if (const PointerUse* high = HighPointer(c.key); high != nullptr && op.immBytes == 1)
            {
                const auto table = static_cast<uint16_t>(high->value << 8);
                if (!inRanges(table) || !inRanges(static_cast<uint16_t>(table + 0xFF)))
                    continue;
                const std::string name = LabelFor(table);
                const std::string text = Text(c);
                const std::string sub = Replace(text, high->value, false, name + "/256");
                if (!sub.empty())
                    reloc.isub[c.z80] = sub;
                AlignFor(table);
            }
        }
        for (const auto& [c, v] : midRefs)
        {
            const CodeLine* over = lineOver[v];
            const std::string name = LabelFor(over->z80);
            const std::string text = Text(*c);
            uint8_t b[4];
            for (int i = 0; i < 4; ++i)
                b[i] = image[static_cast<uint16_t>(c->z80 + i)];
            const OpInfo& op = DecodeOp(b);
            uint16_t target = 0;
            const bool branch = BranchTarget(op, c->z80, b, target);
            const std::string sub = Replace(text, v, true, name + "+" + std::to_string(v - over->z80));
            if (!sub.empty() && !branch)
                reloc.isub[c->z80] = sub;
        }
        // Data words that point into the ranges: pointer tables and pointer variables (their DEFW gets the label)
        for (const DraftEntry& e : plan.entries)
            for (const DraftSub& s : e.subs)
            {
                if (s.type != 'W' || e.region == nullptr)
                    continue;
                const RegionKind k = e.region->kind;
                const bool pointers = k == RegionKind::PointerTable || k == RegionKind::HandlerTable ||
                                      (k == RegionKind::Variable && e.region->geometry.role == VarRole::Pointer);
                if (!pointers)
                    continue;
                for (uint32_t a = s.start; a + 1 < static_cast<uint32_t>(s.start) + s.length; a += 2)
                {
                    const auto v = static_cast<uint16_t>(image[a] | (image[a + 1] << 8));
                    if (inRanges(v) && (lineOver[v] == nullptr || lineOver[v]->z80 == v))
                        LabelFor(v);
                }
            }
        // Pass 2: constants that equal a label stay numbers
        for (const CodeLine& c : l2.code)
        {
            if (!inRanges(c.z80))
                continue;
            uint8_t b[4];
            for (int i = 0; i < 4; ++i)
                b[i] = image[static_cast<uint16_t>(c.z80 + i)];
            const OpInfo& op = DecodeOp(b);
            if (op.immBytes != 2 || op.immIsAddress || op.flow != Flow::None)
                continue;
            if (!c.executed)
                continue;  // no DataFlow seen: SkoolKit's default (a label when one is there)
            if (PointerEvidence(c.key))
                continue;
            if (reloc.labels.count(Imm16(op, b)) != 0)
                reloc.keep.insert(c.z80);
        }
    }

    /// ALIGN 256 before the table at @p table, or before the outermost buffer that holds it at a page offset
    void AlignFor(uint16_t table)
    {
        uint16_t at = table;
        for (const DraftEntry& e : plan.entries)
            if (e.start <= table && table <= e.end && e.region != nullptr && e.region->kind == RegionKind::Buffer &&
                (e.start & 0xFF) == 0)
                at = e.start;
        reloc.align.insert(at);
        reloc.breaks.insert(at);
    }

    // ---------------------------------------------------------------- writers

    std::string Ctl()
    {
        std::ostringstream out;
        out << "; Control file written by the unreal-ng analysis layers (L2 draft, sna2skool.py -c)\n";
        bool started = false;
        for (const DraftEntry& e : plan.entries)
        {
            if (!started)
            {
                out << "@ " << Hex4(e.start) << " start\n";
                started = true;
            }
            if (e.org)
                out << "@ " << Hex4(e.start) << " org\n";
            if (!e.label.empty())
                out << "@ " << Hex4(e.start) << " label=" << e.label << "\n";
            out << e.ctl << " " << Hex4(e.start) << " " << e.title << "\n";
            for (const std::string& d : e.description)
                out << "D " << Hex4(e.start) << " " << d << "\n";
            for (const DraftSub& s : e.subs)
            {
                if (s.start == e.start && s.length == e.end - e.start + 1u && s.type == std::toupper(e.ctl))
                    continue;
                out << s.type << " " << Hex4(s.start) << "," << s.length << "\n";
            }
            for (auto it = reloc.labels.upper_bound(e.start); it != reloc.labels.end() && it->first <= e.end; ++it)
                out << "@ " << Hex4(it->first) << " label=" << it->second << "\n";
            // A range ends: nothing claimed after it
            for (const ExportRange& r : plan.ranges)
                if (r.end == e.end && r.end < 0xFFFF)
                    out << "i " << Hex4(r.end + 1u) << "\n";
        }
        return out.str();
    }

    static std::string Defm(const std::vector<uint8_t>& bytes)
    {
        std::string out;
        bool quoted = false;
        for (const uint8_t c : bytes)
        {
            const bool plain = c >= 0x20 && c < 0x7F && c != '"' && c != '\\' && c != ';';
            if (plain)
            {
                if (!quoted)
                {
                    if (!out.empty())
                        out += ",";
                    out += "\"";
                    quoted = true;
                }
                out += static_cast<char>(c);
            }
            else
            {
                if (quoted)
                {
                    out += "\"";
                    quoted = false;
                }
                if (!out.empty())
                    out += ",";
                out += Hex2(c);
            }
        }
        if (quoted)
            out += "\"";
        return out;
    }

    std::string Skool()
    {
        std::ostringstream out;
        std::vector<uint16_t> entryList;
        for (const DraftEntry& e : plan.entries)
            entryList.push_back(e.start);
        out << "; Skool draft written by the unreal-ng analysis layers (L2). Comments state facts of the replay only.\n";
        out << "@start\n";
        for (const DraftEntry& e : plan.entries)
        {
            out << "\n";
            if (e.org)
                out << kMark << "ORG #" << Hex4(e.start).substr(1) << "\n";
            out << "; " << CommentRefs(e.title, entryList) << "\n";
            if (!e.description.empty())
            {
                out << ";\n";
                for (size_t i = 0; i < e.description.size(); ++i)
                {
                    if (i != 0)
                        out << "; .\n";
                    out << "; " << CommentRefs(e.description[i], entryList) << "\n";
                }
            }
            if (e.ctl == 'c')
                CodeLines(out, e, entryList);
            else
                DataLines(out, e);
        }
        return PlaceInsertions(out.str());
    }

    /// A line the writer marks for an instruction of its own before the next one: an ORG at a range start or the
    /// fixed tail, ALIGN 256 before a page-aligned table
    static constexpr char kMark = '\x01';

    /// The marked ORG / ALIGN lines go after the instruction before them (@isub=+ on the previous instruction
    /// line, after its own directives). skool2asm -c (zx-reaper's zxq asm) gives the first instruction of an entry
    /// a label, and an instruction @isub=> inserts there has no address: skool2asm fails on it. Where no
    /// instruction comes before (the first entry), an ORG stays a plain @org and the ALIGN goes (an ORG is where
    /// it says). The ORG is in '#' hex: skool2asm turns a '$' address in an inserted instruction into a label.
    static std::string PlaceInsertions(const std::string& text)
    {
        std::vector<std::string> lines;
        std::istringstream in(text);
        for (std::string line; std::getline(in, line);)
            lines.push_back(line);
        std::vector<std::string> outLines;
        outLines.reserve(lines.size() + 16);
        size_t lastInstruction = std::string::npos;  ///< index in outLines of the previous instruction line
        size_t inserted = 0;                         ///< lines put before it so far (they keep their order)
        for (const std::string& line : lines)
        {
            if (!line.empty() && line[0] == kMark)
            {
                const std::string op = line.substr(1);
                if (lastInstruction == std::string::npos)
                {
                    if (op.rfind("ORG", 0) == 0)
                        outLines.push_back("@org");
                    continue;
                }
                outLines.insert(outLines.begin() + static_cast<std::ptrdiff_t>(lastInstruction + inserted),
                                "@isub=+" + op);
                ++inserted;
                continue;
            }
            const bool instruction = line.size() > 1 && line[0] != ';' && line[0] != '@' && line[1] == '$';
            if (instruction)
            {
                lastInstruction = outLines.size();
                inserted = 0;
            }
            outLines.push_back(line);
        }
        std::string out;
        for (const std::string& line : outLines)
            out += line + "\n";
        return out;
    }

    void Directives(std::ostringstream& out, uint16_t address, bool entryStart, const DraftEntry& e)
    {
        if (reloc.align.count(address) != 0)
            out << kMark << "ALIGN 256\n";
        const auto label = reloc.labels.find(address);
        if (label != reloc.labels.end() && (!entryStart || !label->second.empty()))
            out << "@label=" << label->second << "\n";
        (void)e;
    }

    void CodeLines(std::ostringstream& out, const DraftEntry& e, const std::vector<uint16_t>& entryList)
    {
        uint32_t a = e.start;
        bool first = true;
        while (a <= e.end)
        {
            const CodeLine* c = lineAt[a];
            std::string text, comment;
            uint32_t length = 1;
            if (c != nullptr)
            {
                uint8_t b[4];
                for (int i = 0; i < 4; ++i)
                    b[i] = image[static_cast<uint16_t>(a + i)];
                const OpInfo& op = DecodeOp(b);
                length = op.length;
                text = Text(*c);
                if (op.length != c->length || text.empty() || text.find('?') != std::string::npos)
                {
                    text.clear();
                    for (uint32_t i = 0; i < c->length; ++i)
                        text += (i ? "," : "DEFB ") + Hex2(image[static_cast<uint16_t>(a + i)]);
                    length = c->length;
                    comment = "bytes as loaded";
                }
                if (!c->executed)
                    comment = comment.empty() ? "not executed" : comment + "; not executed";
                if (c->smc)
                    comment = comment.empty() ? "self-modifying" : comment + "; self-modifying";
                for (const Loop& loop : l1.loops)
                    if (loop.z80 == a && loop.instances != 0)
                    {
                        const std::string trips = loop.minTrips == loop.maxTrips
                                                      ? std::to_string(loop.minTrips)
                                                      : std::to_string(loop.minTrips) + "-" + std::to_string(loop.maxTrips);
                        comment += (comment.empty() ? "" : "; ") + std::string("loop: ") + trips + " passes";
                        break;
                    }
            }
            else
            {
                text = "DEFB " + Hex2(image[a]);
                comment = "not decoded";
            }
            Directives(out, static_cast<uint16_t>(a), first, e);
            if (const auto it = reloc.isub.find(static_cast<uint16_t>(a)); it != reloc.isub.end())
                out << "@isub=" << it->second << "\n";
            if (reloc.keep.count(static_cast<uint16_t>(a)) != 0)
                out << "@keep\n";
            out << (first ? e.ctl : ' ') << Hex4(a) << " " << text;
            if (!comment.empty())
                out << " ; " << CommentRefs(comment, entryList);
            out << "\n";
            first = false;
            a += length;
        }
    }

    void DataLines(std::ostringstream& out, const DraftEntry& e)
    {
        // Pieces: the sub-blocks, or one block of the entry's kind
        struct Piece
        {
            uint16_t start;
            uint16_t length;
            char type;
        };
        std::vector<Piece> pieces;
        uint32_t cursor = e.start;
        std::vector<DraftSub> subs = e.subs;
        std::sort(subs.begin(), subs.end(), [](const DraftSub& x, const DraftSub& y) { return x.start < y.start; });
        const char whole = e.ctl == 't' ? 'T' : e.ctl == 'w' ? 'W' : 'B';
        for (const DraftSub& s : subs)
        {
            if (s.start < cursor || s.start > e.end)
                continue;
            if (s.start > cursor)
                pieces.push_back(Piece{static_cast<uint16_t>(cursor), static_cast<uint16_t>(s.start - cursor), 'B'});
            const uint32_t length = std::min<uint32_t>(s.length, e.end - s.start + 1u);
            pieces.push_back(Piece{s.start, static_cast<uint16_t>(length), s.type});
            cursor = s.start + length;
        }
        if (cursor <= e.end)
            pieces.push_back(Piece{static_cast<uint16_t>(cursor), static_cast<uint16_t>(e.end - cursor + 1u),
                                   subs.empty() ? whole : 'B'});

        std::vector<uint16_t> breaks;
        for (auto it = reloc.breaks.lower_bound(e.start); it != reloc.breaks.end() && *it <= e.end; ++it)
            breaks.push_back(*it);
        for (auto it = reloc.labels.lower_bound(e.start); it != reloc.labels.end() && it->first <= e.end; ++it)
            breaks.push_back(it->first);
        std::sort(breaks.begin(), breaks.end());
        breaks.erase(std::unique(breaks.begin(), breaks.end()), breaks.end());

        bool first = true;
        for (const Piece& p : pieces)
        {
            char type = p.type;
            // A word split by a label inside it is written as bytes
            if (type == 'W')
                for (const uint16_t b : breaks)
                    if (b > p.start && b < p.start + p.length && (b - p.start) % 2 != 0)
                        type = 'B';
            if (type == 'W' && p.length % 2 != 0)
                type = 'B';
            const uint16_t width = type == 'T' ? 48 : type == 'W' ? static_cast<uint16_t>(std::max<int>(2, options.defbWidth / 2 * 2))
                                                                  : options.defbWidth;
            for (const auto& [start, length] : SplitLines(p.start, p.length, width, breaks))
            {
                std::vector<uint8_t> bytes(image.begin() + start, image.begin() + start + length);
                std::string text;
                if (type == 'T')
                    text = "DEFM " + Defm(bytes);
                else if (type == 'W')
                {
                    text = "DEFW ";
                    for (uint32_t i = 0; i + 1 < length; i += 2)
                        text += (i ? "," : "") + Hex4(bytes[i] | (bytes[i + 1] << 8));
                }
                else
                {
                    text = "DEFB ";
                    for (uint32_t i = 0; i < length; ++i)
                        text += (i ? "," : "") + Hex2(bytes[i]);
                }
                Directives(out, start, first, e);
                // A DEFW of a plain data entry keeps its numbers (SkoolKit would put labels into it)
                if (type == 'W' && e.region != nullptr && e.region->kind != RegionKind::PointerTable &&
                    e.region->kind != RegionKind::HandlerTable && e.region->geometry.role != VarRole::Pointer)
                    out << "@keep\n";
                out << (first ? e.ctl : ' ') << Hex4(start) << " " << text << "\n";
                first = false;
            }
        }
    }

    std::string Fields() const
    {
        std::ostringstream out;
        out << "{\"records\": [";
        bool firstRecord = true;
        for (const DraftEntry& e : plan.entries)
            (void)e;
        for (const Region& r : l2.store.Regions())
        {
            if ((r.kind != RegionKind::RecordArray && r.kind != RegionKind::Buffer) || r.geometry.fields.empty() ||
                !InRanges(plan.ranges, r.z80))
                continue;
            out << (firstRecord ? "\n" : ",\n");
            firstRecord = false;
            const auto label = reloc.labels.find(r.z80);
            const std::string name = label != reloc.labels.end() ? label->second : HexName(LabelPrefix(r), r.z80);
            out << "  {\"name\": \"" << name << "\", \"kind\": \"" << RegionKindName(r.kind) << "\", \"base\": \""
                << Hex4(r.z80) << "\", \"stride\": " << r.geometry.stride << ", \"count\": " << r.geometry.count
                << ", \"confidence\": " << r.confidence << ",\n   \"instances\": [";
            for (size_t i = 0; i < r.geometry.instances.size(); ++i)
                out << (i ? ", " : "") << "\"" << Hex4(r.geometry.instances[i]) << "\"";
            out << "],\n   \"fields\": [";
            for (size_t i = 0; i < r.geometry.fields.size(); ++i)
            {
                const Field& f = r.geometry.fields[i];
                out << (i ? ",\n              " : "") << "{\"offset\": " << f.offset << ", \"size\": " << f.size
                    << ", \"name\": \"" << f.name << "\", \"min\": " << static_cast<int>(f.minValue)
                    << ", \"max\": " << static_cast<int>(f.maxValue) << "}";
            }
            out << "]}";
        }
        out << "\n]}\n";
        return out.str();
    }

    std::string Geometry() const
    {
        std::ostringstream out;
        out << "{\"regions\": [";
        bool first = true;
        for (const Region& r : l2.store.Regions())
        {
            const bool graphic = r.kind == RegionKind::Font || r.kind == RegionKind::Sprite ||
                                 r.kind == RegionKind::Tile || r.kind == RegionKind::Screen ||
                                 r.kind == RegionKind::LevelMap;
            if (!graphic || !InRanges(plan.ranges, r.z80))
                continue;
            const struct Geometry& g = r.geometry;
            out << (first ? "\n" : ",\n") << "  {\"start\": \"" << Hex4(r.z80) << "\", \"end\": \""
                << (r.z80 + r.Size() > 0xFFFFu ? std::string("$10000") : Hex4(r.z80 + r.Size()))
                << "\", \"kind\": \"" << RegionKindName(r.kind) << "\", \"width\": " << g.width << ", \"height\": " << g.height << ", \"frames\": " << g.frames
                << ", \"stride\": " << g.stride << ", \"mask\": " << (g.mask ? "true" : "false")
                << ", \"prefix\": " << g.prefix << ", \"layout\": \"" << g.layout << "\", \"detector\": \""
                << DetectorName(r.detector) << "\", \"confidence\": " << r.confidence << "}";
            first = false;
        }
        out << "\n]}\n";
        return out.str();
    }

    std::string Layout() const
    {
        char text[256];
        std::snprintf(text, sizeof(text),
                      "# Relocation layout written by the unreal-ng analysis layers (zx-reaper re/layout.mk lines)\n"
                      "REGION_START := 0x%04X\nSHIFT_END := 0x%04X\n",
                      plan.regionStart, plan.shiftEnd);
        return text;
    }

    std::string Map() const
    {
        std::ostringstream out;
        char line[160];
        std::snprintf(line, sizeof(line), "%-11s %-13s %-14s %5s %-9s %s\n", "range", "kind", "detector", "conf",
                      "status", "label / title");
        out << line;
        for (const Region& r : l2.store.Regions())
        {
            if (!InRanges(plan.ranges, r.z80))
                continue;
            const std::string range = Hex4(r.z80).substr(1) + "-" + Hex4(r.Z80End()).substr(1);
            const std::string name = !r.label.empty() ? r.label + " " + TitleOf(r, r.Size()) : TitleOf(r, r.Size());
            std::snprintf(line, sizeof(line), "%-11s %-13s %-14s %5u %-9s %s\n", range.c_str(), RegionKindName(r.kind),
                          DetectorName(r.detector), static_cast<unsigned>(r.confidence), RegionStatusName(r.status),
                          name.c_str());
            out << line;
        }
        return out.str();
    }

    ExportResult Symbols() const
    {
        using namespace unrealasm::symbols;
        ExportResult result;
        SymbolFile file;
        SymbolSet set;
        set.id = "analysis";
        set.title = "unreal-ng analysis layers (L2)";
        std::set<uint16_t> starts;
        for (const DraftEntry& e : plan.entries)
        {
            Symbol s;
            s.name = e.label;
            s.location.offset = e.start;
            s.kind = e.ctl == 'c' ? SymbolKind::Code : SymbolKind::Data;
            s.size = e.end - e.start + 1u;
            s.comment = e.title;
            s.traits.push_back(std::string("skoolkit:") + e.ctl);
            set.symbols.push_back(std::move(s));
            starts.insert(e.start);
        }
        for (const auto& [address, name] : reloc.labels)
        {
            if (starts.count(address) != 0)
                continue;
            Symbol s;
            s.name = name;
            s.location.offset = address;
            s.kind = lineOver[address] != nullptr ? SymbolKind::Code : SymbolKind::Data;
            s.traits.push_back("skoolkit:point");
            set.symbols.push_back(std::move(s));
        }
        file.sets.push_back(std::move(set));
        const ISymbolCodec* codec = SymbolCodecRegistry::Builtin().Find(options.symbolCodec);
        if (codec == nullptr)
        {
            result.error = "no symbol codec '" + options.symbolCodec + "'";
            return result;
        }
        const SymbolEncodeResult encoded = codec->Encode(file, SymbolEncodeOptions{});
        if (!encoded.ok)
        {
            result.error = "the symbol codec '" + options.symbolCodec + "' refused the set";
            return result;
        }
        result.ok = true;
        result.text.assign(encoded.bytes.begin(), encoded.bytes.end());
        result.entries = static_cast<uint32_t>(file.sets.front().symbols.size());
        return result;
    }
};

}  // namespace

const std::vector<std::string>& ExportFormats()
{
    static const std::vector<std::string> formats = {"ctl", "skool", "fields", "geometry", "layout", "symbols", "map"};
    return formats;
}

std::string SkoolSpelling(const std::string& text)
{
    std::string out;
    out.reserve(text.size());
    bool quoted = false;
    for (const char c : text)
    {
        if (c == '"' || c == '\'')
        {
            // AF' is no quote
            if (c == '\'' && !out.empty() && (out.back() == 'F' || out.back() == 'f'))
            {
                out += c;
                continue;
            }
            quoted = !quoted;
            out += c;
            continue;
        }
        if (quoted)
            out += c;
        else if (c == '#')
            out += '$';
        else
            out += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }
    // The undocumented index halves in the spelling SkoolKit and sjasmplus share (the disassembler writes HX / LX)
    static const char* const kHalves[][2] = {{"HX", "IXH"}, {"LX", "IXL"}, {"HY", "IYH"}, {"LY", "IYL"}};
    for (const auto& half : kHalves)
    {
        size_t at = 0;
        while ((at = out.find(half[0], at)) != std::string::npos)
        {
            const auto word = [&](size_t i) {
                return std::isalnum(static_cast<unsigned char>(out[i])) != 0 || out[i] == '_';
            };
            const bool alone = (at == 0 || !word(at - 1)) && (at + 2 >= out.size() || !word(at + 2));
            const bool inQuote = std::count(out.begin(), out.begin() + static_cast<std::ptrdiff_t>(at), '"') % 2 != 0;
            if (alone && !inQuote)
            {
                out.replace(at, 2, half[1]);
                at += 3;
            }
            else
                at += 2;
        }
    }
    return out;
}

std::string CommentRefs(const std::string& text, const std::vector<uint16_t>& entries)
{
    std::string out;
    size_t i = 0;
    while (i < text.size())
    {
        const bool prefixed = i >= 2 && text[i - 2] == '#' && text[i - 1] == 'R';
        if (text[i] == '$' && i + 4 < text.size() + 1 && !prefixed)
        {
            size_t n = 0;
            while (i + 1 + n < text.size() && n < 5 && std::isxdigit(static_cast<unsigned char>(text[i + 1 + n])))
                ++n;
            if (n == 4)
            {
                const auto value = static_cast<uint16_t>(std::stoul(text.substr(i + 1, 4), nullptr, 16));
                if (std::binary_search(entries.begin(), entries.end(), value))
                {
                    out += "#R" + text.substr(i, 5);
                    i += 5;
                    continue;
                }
            }
        }
        out += text[i++];
    }
    return out;
}

std::vector<std::pair<uint16_t, uint16_t>> SplitLines(uint16_t start, uint16_t length, uint16_t width,
                                                      const std::vector<uint16_t>& breaks)
{
    std::vector<std::pair<uint16_t, uint16_t>> out;
    if (width == 0)
        width = 8;
    uint32_t a = start;
    const uint32_t end = static_cast<uint32_t>(start) + length;
    auto next = std::upper_bound(breaks.begin(), breaks.end(), static_cast<uint16_t>(a));
    while (a < end)
    {
        uint32_t stop = std::min<uint32_t>(end, a + width);
        while (next != breaks.end() && *next <= a)
            ++next;
        if (next != breaks.end() && *next < stop)
            stop = *next;
        out.emplace_back(static_cast<uint16_t>(a), static_cast<uint16_t>(stop - a));
        a = stop;
    }
    return out;
}

std::string RegionTitle(const Region& region)
{
    return TitleOf(region, region.Size());
}

DraftPlan MakePlan(const L1Result& l1, const L2Result& l2, const ExportOptions& options)
{
    Exporter exporter(l1, l2, options);
    exporter.BuildPlan();
    exporter.Relocate();
    return exporter.plan;
}

ExportResult Export(const L1Result& l1, const L2Result& l2, const std::string& format, const ExportOptions& options)
{
    ExportResult result;
    if (std::find(ExportFormats().begin(), ExportFormats().end(), format) == ExportFormats().end())
    {
        result.error = "format: ctl, skool, fields, geometry, layout, symbols or map";
        return result;
    }
    Exporter exporter(l1, l2, options);
    exporter.BuildPlan();
    exporter.Relocate();
    if (format == "symbols")
        return exporter.Symbols();
    if (format == "ctl")
        result.text = exporter.Ctl();
    else if (format == "skool")
    {
        if (!options.disassemble)
        {
            result.error = "the skool draft needs a disassembler";
            return result;
        }
        result.text = exporter.Skool();
    }
    else if (format == "fields")
        result.text = exporter.Fields();
    else if (format == "geometry")
        result.text = exporter.Geometry();
    else if (format == "layout")
        result.text = exporter.Layout();
    else
        result.text = exporter.Map();
    result.ok = true;
    result.entries = static_cast<uint32_t>(exporter.plan.entries.size());
    return result;
}

}  // namespace analysis
