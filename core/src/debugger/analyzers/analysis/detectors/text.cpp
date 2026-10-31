/// The text detector.
///
/// Printed text: a run of printable bytes ($20..$7E, the last may carry bit 7) that code read as data and never
/// wrote, read by a routine on a print path: one that reads a font (a Font claim: the ROM character set or one the
/// font detector found), runs a glyph blit (one byte by 8 rows to the pixels, three or more frames on one lattice of
/// 8: the font of the game, which the font detector can only name once the text is known) or prints through the ROM
/// (RST $10, the ROM print routines), itself or through a callee (two calls deep). The terminator is a zero after the run, bit 7 on its last character, a length byte before it,
/// or none (the printing code holds the count). Text in a buffer: bytes only block copies wrote (a room's name copied
/// to the room buffer and printed from there) are text when they are printable in every source the copies brought
/// them from (the page the copy read: code and text copied out of a 128K bank the start did not map); the text is claimed
/// in the buffer and in each source. What a block copy took to the screen is a picture,
/// not text, at either end. A byte the glyph reader reads (an instruction that reads a font's bytes) is pixels too: a
/// blitter that draws the characters and the sprites (Hungry Horace) reads both, the text is what selects a glyph.
///
/// Source remnant: a run of bytes no code touched (the stack aside) that parses as tokenized assembler source - at
/// least four lines of [line number lo, hi][length][text] with rising line numbers - is Unused, with the decoded
/// lines as its text.

#include <algorithm>
#include <cstdio>
#include <map>
#include <set>
#include <string>

#include "debugger/analyzers/analysis/detectors/detectors.h"
#include "debugger/analyzers/analysis/detectors/media.h"

namespace analysis
{

namespace
{

constexpr uint32_t kMinText = 3;
constexpr uint32_t kMinRemnant = 64;
constexpr int kMinSourceLines = 4;
constexpr uint32_t kMinBufferCopy = 16;  ///< the shortest block copy whose destination is a buffer (records.cpp)
constexpr uint32_t kDisplayStart = 0x4000;
constexpr uint32_t kDisplayEnd = 0x5B00;

std::string Hex4(uint16_t v)
{
    char text[8];
    std::snprintf(text, sizeof(text), "$%04X", v);
    return text;
}

bool Printable(uint8_t c)
{
    return c >= 0x20 && c < 0x7F;
}

bool SourceChar(uint8_t c)
{
    return Printable(c) || c == 0x09;
}

bool IsPrintSignature(const std::string& label)
{
    static const char* const kPrint[] = {"PRINT_A_1", "PRINT_A_2", "PRINT_OUT", "PR_STRING", "PO_MSG"};
    return std::any_of(std::begin(kPrint), std::end(kPrint), [&](const char* p) { return label == p; });
}

/// A blit that draws characters: 8 x 8 frames one byte wide to the pixels, at least three frames on one lattice of 8
bool GlyphBlit(const media::Blit& b)
{
    if (b.rowBytes != 1 || b.height != 8 || b.mask || !b.toPixels || b.starts.size() < 3)
        return false;
    return std::all_of(b.starts.begin(), b.starts.end(), [&](uint16_t s) { return (s - b.starts.front()) % 8 == 0; });
}

/// Routine indices on a print path (directly)
std::set<uint32_t> PrintRoutines(DetectorContext& context, const std::vector<media::Blit>& blits)
{
    const L1Result& l1 = context.l1;
    std::vector<const Region*> fonts;
    std::set<uint16_t> printEntries;
    for (const Region& r : context.out.store.Regions())
    {
        if (r.kind == RegionKind::Font)
            fonts.push_back(&r);
        if (r.kind == RegionKind::Code && IsPrintSignature(r.label))
            printEntries.insert(r.z80);
    }
    std::set<uint32_t> out;
    for (uint32_t i = 0; i < l1.routines.size(); ++i)
    {
        const Routine& routine = l1.routines[i];
        bool prints = printEntries.count(routine.z80) != 0;
        for (const Interval& read : routine.profile.reads)
            for (const Region* f : fonts)
                if (read.lo <= f->hi && read.hi >= f->lo)
                    prints = true;
        if (prints)
            out.insert(i);
    }
    // RST $10 and calls to the ROM print routines
    for (const CallEdge& c : l1.calls)
    {
        const Routine& callee = l1.routines[c.callee];
        if (printEntries.count(callee.z80) != 0 || (c.kind == CallKind::Rst && callee.z80 == 0x0010))
            out.insert(c.caller);
    }
    for (const Instruction& ins : l1.instructions)
        if (ins.bytes[0] == 0xD7)  // RST $10
            if (const Routine* r = l1.RoutineOf(ins.key))
                out.insert(l1.IndexOf(*r));
    for (const media::Blit& b : blits)
        if (GlyphBlit(b))
            if (const Routine* r = l1.RoutineOf(b.loop->header))
                out.insert(l1.IndexOf(*r));
    return out;
}

/// 0: not on a print path; 1: prints itself; 2: through a callee (two calls deep)
int PrintDepth(const L1Result& l1, uint32_t routine, const std::set<uint32_t>& printers)
{
    if (printers.count(routine) != 0)
        return 1;
    std::set<uint32_t> level = {routine};
    for (int depth = 0; depth < 2; ++depth)
    {
        std::set<uint32_t> next;
        for (const CallEdge& c : l1.calls)
            if (level.count(c.caller) != 0 && (c.kind == CallKind::Call || c.kind == CallKind::Rst ||
                                               c.kind == CallKind::Tail || c.kind == CallKind::FallsInto))
                next.insert(c.callee);
        for (const uint32_t n : next)
            if (printers.count(n) != 0)
                return 2;
        level = std::move(next);
    }
    return 0;
}

void PrintedText(DetectorContext& context)
{
    const L1Result& l1 = context.l1;
    const std::vector<media::Blit> blits = media::FindBlits(context, media::FindDisplay(context));
    const std::set<uint32_t> printers = PrintRoutines(context, blits);
    if (printers.empty())
        return;

    // The block copies into buffers: (destination, length, source), the lowest byte of each range; the source's page
    // byte as the copy read it (another bank than the start's paging shows at that address: code and text copied out
    // of a 128K page)
    struct Copy
    {
        uint16_t dst = 0;
        uint32_t length = 0;
        uint16_t src = 0;
        PhysKey srcKey = kNoKey;  ///< the lowest source byte (kNoKey: not known)
        /// The page byte the copy read at offset @p i (the start's paging past the end of the source's page)
        PhysKey Source(const DetectorContext& context, uint32_t i) const
        {
            if (srcKey != kNoKey && KeyOffset(srcKey) + i <= 0x3FFFu)
                return srcKey + i;
            return context.Key(static_cast<uint16_t>(src + i));
        }
    };

    std::vector<Copy> copies;
    std::vector<uint8_t> picture(0x10000, 0);  // the sources of copies to the screen
    for (const BlockCopy& c : l1.copies)
    {
        if (c.length < kMinBufferCopy)
            continue;
        const auto dst = static_cast<uint16_t>(c.dir > 0 ? c.dst : c.dst - (c.length - 1));
        const auto src = static_cast<uint16_t>(c.dir > 0 ? c.src : c.src - (c.length - 1));
        if (static_cast<uint32_t>(dst) + c.length - 1 > 0xFFFF || static_cast<uint32_t>(src) + c.length - 1 > 0xFFFF)
            continue;
        // A copy to the screen is a picture, whatever a reader of the attribute file or of its source takes from it
        if (dst < kDisplayEnd && static_cast<uint32_t>(dst) + c.length > kDisplayStart)
        {
            if (src >= kDisplayEnd || static_cast<uint32_t>(src) + c.length <= kDisplayStart)
                std::fill(picture.begin() + src, picture.begin() + src + c.length, 1);
            continue;
        }
        PhysKey srcKey = c.srcKey;
        if (c.dir < 0)
            srcKey = KeyOffset(c.srcKey) >= c.length - 1 ? c.srcKey - (c.length - 1) : kNoKey;
        if (std::none_of(copies.begin(), copies.end(),
                         [&](const Copy& o) { return o.dst == dst && o.length == c.length && o.src == src; }))
            copies.push_back(Copy{dst, c.length, src, srcKey});
    }
    const auto printable = [](uint8_t c) { return Printable(c) || Printable(static_cast<uint8_t>(c & 0x7F)); };
    // A byte only block copies wrote: the one value they wrote, else printable in every source (its first source's
    // value; a source page the start did not map has no start image: one value written is the evidence then; false:
    // not such text)
    const auto copied = [&](uint16_t z80, const ByteUse& u, uint8_t& value) {
        bool any = false;
        for (const Copy& c : copies)
        {
            if (z80 < c.dst || z80 > c.dst + c.length - 1)
                continue;
            uint8_t v = 0;
            if (u.distinct == 1)
                v = u.minWritten;
            else if (!l1.StartByte(c.Source(context, z80 - c.dst), v))
                return false;
            if (!printable(v))
                return false;
            if (!any)
                value = v;
            any = true;
        }
        return any;
    };

    // The instructions that read glyphs: what else they read is drawn as pixels
    std::set<PhysKey> glyphReaders;
    for (const Region& f : context.out.store.Regions())
        if (f.kind == RegionKind::Font)
            for (uint32_t g = f.z80; g <= f.Z80End(); ++g)
            {
                const ByteUse& u = context.Use(static_cast<uint16_t>(g));
                for (const PhysKey reader : {u.firstReader, u.lastReader})
                    if (reader != kNoKey)
                        glyphReaders.insert(reader);
            }
    for (const media::Blit& b : blits)
        if (GlyphBlit(b))
            for (const uint16_t reader : b.readers)
                glyphReaders.insert(context.Key(reader));

    // Per byte: read, never written (or written by block copies only: text in a buffer), printable, by a printing
    // reader
    std::vector<uint8_t> depthOf(0x10000, 0);
    std::vector<uint16_t> readerOf(0x10000, 0);
    std::vector<uint8_t> valueOf(0x10000, 0);
    std::vector<uint8_t> buffered(0x10000, 0);
    for (uint32_t a = 0; a <= 0xFFFF; ++a)
    {
        const auto z80 = static_cast<uint16_t>(a);
        if (!context.InScope(z80))
            continue;
        const ByteUse& u = context.Use(z80);
        if ((u.flags & use::kRead) == 0 || DetectorContext::Code(u) || picture[a] != 0 ||
            glyphReaders.count(u.firstReader) != 0 || glyphReaders.count(u.lastReader) != 0)
            continue;
        uint8_t c = context.Start(z80);
        if ((u.flags & use::kWrite) != 0)
        {
            if ((u.flags & (use::kOtherWrite | use::kIncDec)) != 0 || !copied(z80, u, c))
                continue;
            buffered[a] = 1;
        }
        else if (!printable(c))
            continue;
        valueOf[a] = c;
        int best = 0;
        for (const PhysKey reader : {u.firstReader, u.lastReader})
        {
            const Routine* r = reader != kNoKey ? l1.RoutineOf(reader) : nullptr;
            if (r == nullptr)
                continue;
            const int depth = PrintDepth(l1, l1.IndexOf(*r), printers);
            if (depth != 0 && (best == 0 || depth < best))
            {
                best = depth;
                readerOf[a] = r->z80;
            }
        }
        depthOf[a] = static_cast<uint8_t>(best);
    }

    uint32_t a = 0;
    while (a <= 0xFFFF)
    {
        if (depthOf[a] == 0)
        {
            ++a;
            continue;
        }
        uint32_t b = a;
        // A bit-7 character ends a string
        while (b + 1 <= 0xFFFF && depthOf[b + 1] != 0 && buffered[b + 1] == buffered[a] &&
               ((b + 1) >> 14) == (a >> 14) && (valueOf[b] & 0x80) == 0)
            ++b;
        const uint32_t length = b - a + 1;
        if (length >= kMinText)
        {
            Region r;
            r.kind = RegionKind::Text;
            r.detector = DetectorId::Text;
            r.status = RegionStatus::Observed;
            int depth = 2;
            std::set<uint16_t> readers;
            for (uint32_t i = a; i <= b; ++i)
            {
                depth = std::min<int>(depth, depthOf[i]);
                readers.insert(readerOf[i]);
                r.geometry.text += static_cast<char>(valueOf[i] & 0x7F);
            }
            r.confidence = depth == 1 ? 900 : 800;
            const uint8_t last = valueOf[b];
            const uint8_t after = context.Start(static_cast<uint16_t>(b + 1));
            const uint8_t before = context.Start(static_cast<uint16_t>(a - 1));
            if ((last & 0x80) != 0)
                r.geometry.terminator = "bit7";
            else if (buffered[a] != 0)
                r.geometry.terminator = "count-in-code";
            else if (after == 0 && (context.Use(static_cast<uint16_t>(b + 1)).flags & use::kRead) != 0)
                r.geometry.terminator = "zero";
            else if (before == length)
                r.geometry.terminator = "length";
            else
                r.geometry.terminator = "count-in-code";
            r.geometry.width = static_cast<uint16_t>(length);
            for (const uint16_t reader : readers)
            {
                r.evidence.push_back(Evidence{"reader", reader, 0, depth == 1 ? "prints" : "a callee prints"});
                r.users.push_back(reader);
            }
            uint32_t hi = b;
            if (r.geometry.terminator == "zero")
                hi = b + 1;
            if (buffered[a] != 0)
            {
                // The same text in each source, its own characters
                std::map<uint16_t, PhysKey> sources;
                for (const Copy& c : copies)
                    if (a >= c.dst && b <= c.dst + c.length - 1u)
                        sources.emplace(static_cast<uint16_t>(c.src + (a - c.dst)), c.Source(context, a - c.dst));
                r.evidence.push_back(Evidence{"copy", static_cast<uint16_t>(a), sources.size(),
                                              "block copies brought it from " + std::to_string(sources.size()) +
                                                  (sources.size() == 1 ? " source" : " sources")});
                for (const auto& [at, key] : sources)
                {
                    Region source = r;
                    // Each source its own characters (a page the start did not map: the buffer's)
                    for (uint32_t i = 0; i < length; ++i)
                    {
                        uint8_t v = 0;
                        if (l1.StartByte(key + i, v))
                            source.geometry.text[i] = static_cast<char>(v & 0x7F);
                    }
                    source.evidence.back().detail = "copied to the buffer at " + Hex4(static_cast<uint16_t>(a)) +
                                                    " and printed there";
                    context.Claim(std::move(source), key, at, static_cast<uint16_t>(at + length - 1));
                }
            }
            context.Claim(std::move(r), static_cast<uint16_t>(a), static_cast<uint16_t>(hi));
        }
        a = b + 1;
    }
}

/// [lo, hi][length][text] lines from @p at; the number of good lines and the decoded text
int ParseSource(DetectorContext& context, uint32_t at, uint32_t end, std::string* text, uint32_t* stop)
{
    int lines = 0;
    int previous = -1;
    uint32_t p = at;
    while (p + 3 <= end + 1)
    {
        const int number = context.Start(static_cast<uint16_t>(p)) | (context.Start(static_cast<uint16_t>(p + 1)) << 8);
        const uint8_t length = context.Start(static_cast<uint16_t>(p + 2));
        if (number <= previous || length == 0 || p + 3 + length > end + 1)
            break;
        bool good = true;
        for (uint32_t i = 0; i < length && good; ++i)
            good = SourceChar(context.Start(static_cast<uint16_t>(p + 3 + i)));
        if (!good)
            break;
        if (text != nullptr)
        {
            *text += std::to_string(number) + " ";
            for (uint32_t i = 0; i < length; ++i)
            {
                const char c = static_cast<char>(context.Start(static_cast<uint16_t>(p + 3 + i)));
                *text += c == '\t' ? ' ' : c;
            }
            *text += "\n";
        }
        previous = number;
        ++lines;
        p += 3 + length;
    }
    if (stop != nullptr)
        *stop = p;
    return lines;
}

void SourceRemnants(DetectorContext& context)
{
    const std::vector<uint8_t> claimed = context.ClaimedMap();
    const auto free = [&](uint32_t a) {
        const auto z80 = static_cast<uint16_t>(a);
        return context.InScope(z80) && !DetectorContext::Touched(context.Use(z80)) && claimed[a] == 0;
    };
    uint32_t a = 0;
    while (a <= 0xFFFF)
    {
        if (!free(a))
        {
            ++a;
            continue;
        }
        uint32_t b = a;
        while (b + 1 <= 0xFFFF && ((b + 1) >> 14) == (a >> 14) && free(b + 1))
            ++b;
        if (b - a + 1 >= kMinRemnant)
        {
            // The run may start inside a line: try each start in the first 256 bytes
            for (uint32_t s = a; s < std::min(b, a + 256); ++s)
            {
                if (ParseSource(context, s, b, nullptr, nullptr) < kMinSourceLines)
                    continue;
                Region r;
                r.kind = RegionKind::Unused;
                r.detector = DetectorId::Text;
                r.status = RegionStatus::Inferred;
                uint32_t stop = 0;
                std::string decoded;
                const int lines = ParseSource(context, s, b, &decoded, &stop);
                // The partial line before the first whole one
                std::string head;
                for (uint32_t i = a; i < s; ++i)
                {
                    const uint8_t c = context.Start(static_cast<uint16_t>(i));
                    if (SourceChar(c))
                        head += c == '\t' ? ' ' : static_cast<char>(c);
                }
                r.geometry.text = (head.empty() ? "" : "... " + head + "\n") + decoded;
                r.geometry.count = static_cast<uint16_t>(std::min(lines, 0xFFFF));
                r.confidence = confidence::FromCount(600, 950, static_cast<uint64_t>(lines), 16);
                r.evidence.push_back(Evidence{"source", static_cast<uint16_t>(s), static_cast<uint64_t>(lines),
                                              "tokenized assembler source lines, no code read them"});
                r.evidence.push_back(Evidence{"untouched", static_cast<uint16_t>(a), b - a + 1, "no data access"});
                (void)stop;
                context.Claim(std::move(r), static_cast<uint16_t>(a), static_cast<uint16_t>(b));
                break;
            }
        }
        a = b + 1;
    }
}

}  // namespace

void DetectText(DetectorContext& context)
{
    PrintedText(context);
    SourceRemnants(context);
}

}  // namespace analysis
