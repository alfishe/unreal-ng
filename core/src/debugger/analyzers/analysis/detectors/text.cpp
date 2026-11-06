/// The text detector.
///
/// Printed text: a run of printable bytes ($20..$7E, the last may carry bit 7) that code read as data and never
/// wrote, read by a routine on a print path: one that reads a font (a Font claim: the ROM character set or one the
/// font detector found), runs a glyph blit (one byte by 8 rows to the pixels, three or more frames on one lattice of
/// 8: the font of the game, which the font detector can only name once the text is known) or prints through the ROM
/// (RST $10, the ROM print routines), itself or through a callee (two calls deep). The terminator is a zero after the
/// run, bit 7 on its last character, a length byte before it, a control code the printing routine read next (a line
/// break, a colour), or none (the printing code holds the count). Text in a buffer: bytes only block copies wrote (a
/// room's name copied to the room buffer and printed from there) are text when they are printable in every source the
/// copies brought them from (the page the copy read: code and text copied out of a 128K bank the start did not map);
/// the text is claimed in the buffer and in each source. What a block copy took to the screen is a picture, not text,
/// at either end. A byte the glyph reader reads (an instruction that reads a font's bytes) is pixels too: a blitter
/// that draws the characters and the sprites (Hungry Horace) reads both, the text is what selects a glyph. A routine
/// that read more than a 16 K bank (a random number generator walking memory, a checksum) reads no glyphs, whatever
/// font bytes it met on its way.
///
/// Text through its glyphs' index: an instruction that read through an index (IndexUse: LD L,A / LD H,n / LD A,(HL))
/// taken from bytes no code wrote reads a font, whatever its layout (8 bytes a glyph, one pixel column a page, widths
/// for a proportional font), when those bytes are words: at least kMinGlyphCodes distinct codes, nearly all printable
/// (bit 7 aside: it marks a last character) and most of them letters or spaces. A tile map's codes run over the whole
/// byte range, a tune's notes index their pitches with digits and letters mixed. The bytes whose values a font reader
/// took are printed, by the routine that read them, with no print path needed. Such a run needs a terminator of its own (a
/// zero, bit 7, a length byte, a control code) and the shape of a word: at least half of its characters other than
/// spaces are letters. A level map of printable tile codes has no terminator; a blank filler, a score's digits and the
/// expressions of a BASIC line (`n)=`, `f(i)`) select glyphs as well, but say nothing a word does. What the glyph reader
/// reads is pixels.
///
/// A printer's argument: an address an immediate load put in a register pair (L1 `immediateAddresses`, LD rr,nn
/// pointers) that a routine already printing claimed text read through (the printer) is a string too, whatever its
/// bytes: control records (a colour code, a row and a column) and cells code fills before each print leave too few
/// printable characters for the rules above. Its terminator is the byte value the printer stops at after its other
/// strings (the last byte of the run it read past each of them; a zero terminator counts as zero), when that is no
/// printable character. The string runs from the address over bytes the printer (or a routine it calls) read, up to
/// and including the first one of that value no code wrote; at another claim, at code, at a byte the printer did not
/// read or after kMaxArgument bytes it is none. The printer reads it as a string: its first byte, its terminator and at
/// least half of its bytes were read by an instruction that read the printer's other strings (a font or a table the
/// printer looks up is read by other instructions). It needs a control code (a byte no code wrote that is no character)
/// or the shape of a word: a score's digits passed to the printer alone are no text. Bytes code wrote are fields of
/// the text (`filled`), control codes show as {XX} in its characters.
///
/// Source remnant: a run of bytes no code touched (the stack aside) that parses as tokenized assembler source - at
/// least four lines of [line number lo, hi][length][text] with rising line numbers - is Unused, with the decoded
/// lines as its text.

#include <algorithm>
#include <cctype>
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
constexpr uint32_t kMemoryWalk = 0x4000;  ///< bytes a routine read in all: more is a walk over memory, no glyph read
constexpr size_t kMinGlyphCodes = 8;          ///< distinct codes a font reader took
constexpr uint32_t kGlyphPrintablePct = 95;  ///< of its selectors, printable (bit 7 aside)
constexpr uint32_t kGlyphLetterPct = 75;     ///< of its selectors, letters or spaces
constexpr uint32_t kDisplayStart = 0x4000;
constexpr uint32_t kDisplayEnd = 0x5B00;
constexpr uint32_t kMaxArgument = 255;  ///< bytes a printer's argument (or the run past a printed string) may span

std::string Hex4(uint16_t v)
{
    char text[8];
    std::snprintf(text, sizeof(text), "$%04X", v);
    return text;
}

std::string Hex2(uint8_t v)
{
    char text[4];
    std::snprintf(text, sizeof(text), "%02X", v);
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

/// @p routine read more than kMemoryWalk bytes in all
bool WalksMemory(const Routine& routine)
{
    uint32_t bytes = 0;
    for (const Interval& read : routine.profile.reads)
        bytes += read.hi - read.lo + 1;
    return bytes > kMemoryWalk;
}

bool WalksMemory(const L1Result& l1, PhysKey instruction)
{
    const Routine* r = l1.RoutineOf(instruction);
    return r != nullptr && WalksMemory(*r);
}

/// At least one letter and at least half of the characters other than spaces
bool WordShaped(const std::string& text)
{
    uint32_t letters = 0;
    uint32_t others = 0;
    for (const char c : text)
        if (std::isalpha(static_cast<unsigned char>(c)) != 0)
            ++letters;
        else if (c != ' ')
            ++others;
    return letters != 0 && letters >= others;
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
        if (WalksMemory(routine))
            continue;
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

/// The byte at @p z80 was read by one of the routines @p readers (Z80 entries)
bool ReadBy(const DetectorContext& context, uint16_t z80, const std::set<uint16_t>& readers)
{
    const ByteUse& u = context.Use(z80);
    for (const PhysKey reader : {u.firstReader, u.lastReader})
        if (const Routine* r = reader != kNoKey ? context.l1.RoutineOf(reader) : nullptr)
            if (readers.count(r->z80) != 0)
                return true;
    return false;
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

    // The font readers among the index readers (see the file comment), and per selector byte the reader of its glyph
    struct Selected
    {
        std::set<uint8_t> codes;
        uint32_t bytes = 0;
        uint32_t printable = 0;
        uint32_t letters = 0;
    };
    std::map<PhysKey, Selected> selectedBy;
    for (const IndexUse& u : l1.indexes)
    {
        const ByteUse* use = l1.UseAt(u.selector);
        uint8_t v = 0;
        if (use == nullptr || (use->flags & use::kWrite) != 0 || !l1.StartByte(u.selector, v))
            continue;
        const auto c = static_cast<uint8_t>(v & 0x7F);
        Selected& s = selectedBy[u.instruction];
        ++s.bytes;
        if (!Printable(c))
            continue;
        s.codes.insert(c);
        ++s.printable;
        if (c == ' ' || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'))
            ++s.letters;
    }
    std::set<PhysKey> indexReaders;
    for (const auto& [instruction, s] : selectedBy)
        if (s.codes.size() >= kMinGlyphCodes && s.printable * 100 >= s.bytes * kGlyphPrintablePct &&
            s.letters * 100 >= s.bytes * kGlyphLetterPct)
            indexReaders.insert(instruction);
    std::map<PhysKey, uint16_t> glyphOf;  // selector -> the glyph reader (Z80)
    for (const IndexUse& u : l1.indexes)
        if (indexReaders.count(u.instruction) != 0)
            glyphOf.emplace(u.selector, u.z80);
    if (printers.empty() && glyphOf.empty())
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
                    if (reader != kNoKey && !WalksMemory(l1, reader))
                        glyphReaders.insert(reader);
            }
    for (const media::Blit& b : blits)
        if (GlyphBlit(b))
            for (const uint16_t reader : b.readers)
                glyphReaders.insert(context.Key(reader));
    glyphReaders.insert(indexReaders.begin(), indexReaders.end());

    // Per byte: read, never written (or written by block copies only: text in a buffer), printable, by a printing
    // reader
    std::vector<uint8_t> depthOf(0x10000, 0);
    std::vector<uint16_t> readerOf(0x10000, 0);
    std::vector<uint8_t> valueOf(0x10000, 0);
    std::vector<uint8_t> buffered(0x10000, 0);
    std::vector<uint8_t> indexed(0x10000, 0);  // printed only as its glyph's index
    std::vector<uint16_t> glyphReaderOf(0x10000, 0);
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
        if (best == 0)
            if (const auto g = glyphOf.find(context.Key(z80)); g != glyphOf.end())
            {
                best = 1;
                indexed[a] = 1;
                glyphReaderOf[a] = g->second;
                const Routine* r = u.firstReader != kNoKey ? l1.RoutineOf(u.firstReader) : nullptr;
                readerOf[a] = r != nullptr ? r->z80 : g->second;
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
            else if (after < 0x20 && ReadBy(context, static_cast<uint16_t>(b + 1), readers))
                r.geometry.terminator = "control";
            else
                r.geometry.terminator = "count-in-code";
            const bool byIndex =
                std::all_of(indexed.begin() + a, indexed.begin() + b + 1, [](uint8_t i) { return i != 0; });
            if ((byIndex && buffered[a] == 0 && r.geometry.terminator == "count-in-code") ||
                (byIndex && !WordShaped(r.geometry.text)))
            {
                a = b + 1;
                continue;
            }
            std::set<uint16_t> glyphs;
            for (uint32_t i = a; i <= b; ++i)
                if (indexed[i] != 0)
                    glyphs.insert(glyphReaderOf[i]);
            for (const uint16_t g : glyphs)
                r.evidence.push_back(Evidence{"glyph", g, 0, "its codes are the index of the glyphs read there"});
            if (byIndex)
                r.confidence = 850;
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

/// The strings a printer's immediate argument names (see the file comment), after PrintedText's claims
void PrinterArguments(DetectorContext& context)
{
    const L1Result& l1 = context.l1;
    constexpr uint32_t kNone = 0xFFFFFFFFu;

    // Per Z80 address: 0 free, 1 a text claim, 2 another claim (the code map's fallbacks do not count)
    std::vector<uint8_t> owner(0x10000, 0);
    // Per printer (the Z80 entry of the routine that read claimed text): its strings in the start's paging
    struct Printed
    {
        uint16_t lo = 0;
        uint16_t hi = 0;
        bool zero = false;  ///< its claim holds its zero terminator
    };
    std::map<uint16_t, std::vector<Printed>> stringsOf;
    for (const Region& r : context.out.store.Regions())
    {
        if (r.kind == RegionKind::Untouched ||
            (r.detector == DetectorId::CodeMap && r.kind != RegionKind::Code && r.kind != RegionKind::Unused))
            continue;
        uint16_t z80 = 0;
        if (!context.Z80Of(r.lo, z80))
            continue;
        const bool text = r.kind == RegionKind::Text && r.detector == DetectorId::Text;
        for (uint32_t i = 0; i < r.Size() && z80 + i <= 0xFFFF; ++i)
            owner[z80 + i] = std::max(owner[z80 + i], static_cast<uint8_t>(text ? 1 : 2));
        if (!text || r.lo != context.Key(r.z80))
            continue;
        for (const uint16_t user : r.users)
            stringsOf[user].push_back(Printed{r.z80, r.Z80End(), r.geometry.terminator == "zero"});
    }
    if (stringsOf.empty())
        return;

    // A byte in the bank of @p bank, in scope, no code, read by one of @p family, no other claim than text
    const auto readByFamily = [&](uint32_t a, uint32_t bank, const std::set<uint16_t>& family) {
        if (a > 0xFFFF || (a >> 14) != bank || owner[a] > 1)
            return false;
        const auto z80 = static_cast<uint16_t>(a);
        const ByteUse& u = context.Use(z80);
        return context.InScope(z80) && !DetectorContext::Code(u) && (u.flags & use::kRead) != 0 &&
               ReadBy(context, z80, family);
    };

    // Each printer's family (itself and the routines it calls) and terminator: the value most of its strings end on
    struct Printer
    {
        std::set<uint16_t> family;
        std::set<PhysKey> readers;  ///< the instructions (of the family) that read its other strings
        uint8_t terminator = 0;
        size_t strings = 0;
    };
    std::map<uint16_t, Printer> printers;
    const auto printable = [](uint8_t c) { return Printable(c) || Printable(static_cast<uint8_t>(c & 0x7F)); };
    for (const auto& [entry, strings] : stringsOf)
    {
        const Routine* routine = l1.RoutineAtZ80(entry);
        if (routine == nullptr || routine->z80 != entry)
            continue;
        const uint32_t index = l1.IndexOf(*routine);
        Printer p;
        p.family.insert(entry);
        for (const CallEdge& c : l1.calls)
            if (c.caller == index && (c.kind == CallKind::Call || c.kind == CallKind::Rst ||
                                      c.kind == CallKind::Tail || c.kind == CallKind::FallsInto))
                p.family.insert(l1.routines[c.callee].z80);
        for (const Printed& s : strings)
            for (uint32_t a = s.lo; a <= s.hi; ++a)
            {
                const ByteUse& u = context.Use(static_cast<uint16_t>(a));
                for (const PhysKey reader : {u.firstReader, u.lastReader})
                    if (const Routine* r = reader != kNoKey ? l1.RoutineOf(reader) : nullptr)
                        if (p.family.count(r->z80) != 0)
                            p.readers.insert(reader);
            }
        std::map<uint8_t, uint32_t> votes;
        for (const Printed& s : strings)
        {
            if (s.zero)
            {
                ++votes[0];
                continue;
            }
            // The printer read on past the string (control records, the next string) until it stopped
            const auto bank = static_cast<uint32_t>(s.hi >> 14);
            uint32_t stop = kNone;
            for (uint32_t a = s.hi + 1u; a <= s.hi + kMaxArgument && readByFamily(a, bank, p.family); ++a)
                stop = a;
            if (stop != kNone)
                ++votes[context.Start(static_cast<uint16_t>(stop))];
        }
        uint32_t best = 0;
        for (const auto& [value, count] : votes)
            if (count > best)
            {
                best = count;
                p.terminator = value;
            }
        if (best == 0 || printable(p.terminator) || p.readers.empty())
            continue;
        p.strings = strings.size();
        printers.emplace(entry, std::move(p));
    }
    if (printers.empty())
        return;

    // The immediate arguments: address -> (the load, its first use, the uses), the first fact for an address wins
    struct Argument
    {
        uint16_t load = 0;
        uint16_t use = 0;
        uint64_t uses = 0;
    };
    std::map<uint16_t, Argument> arguments;
    for (const ImmediateAddress& i : l1.immediateAddresses)
        arguments.emplace(i.value, Argument{i.z80, i.useZ80, i.uses});
    for (const PointerUse& p : l1.pointers)
        if (p.form == PointerForm::Immediate && p.uses != 0)
            arguments.emplace(p.value, Argument{p.z80, p.useZ80, p.uses});

    for (const auto& [address, argument] : arguments)
    {
        const Routine* reader = l1.RoutineAtZ80(argument.use);
        const auto printer = reader != nullptr ? printers.find(reader->z80) : printers.end();
        if (printer == printers.end() || owner[address] != 0)
            continue;
        const Printer& p = printer->second;
        const auto bank = static_cast<uint32_t>(address >> 14);
        uint32_t end = kNone;
        for (uint32_t a = address; a < address + kMaxArgument && owner[a & 0xFFFF] == 0 &&
                                   readByFamily(a, bank, p.family);
             ++a)
        {
            const ByteUse& u = context.Use(static_cast<uint16_t>(a));
            const bool written = (u.flags & use::kWrite) != 0;
            if (context.Start(static_cast<uint16_t>(a)) == p.terminator &&
                (!written || (u.minWritten == p.terminator && u.maxWritten == p.terminator)))
            {
                end = a;
                break;
            }
        }
        if (end == kNone || end == address)
            continue;
        // Read where the printer reads its other strings: the first byte, the terminator and at least half of the
        // bytes (a record's row and column may go through other instructions). A font, a table the printer looks up
        // through the same family is read elsewhere
        const auto readAsString = [&](uint32_t a) {
            const ByteUse& u = context.Use(static_cast<uint16_t>(a));
            return p.readers.count(u.firstReader) != 0 || p.readers.count(u.lastReader) != 0;
        };
        uint32_t asString = 0;
        for (uint32_t a = address; a <= end; ++a)
            asString += readAsString(a) ? 1u : 0u;
        if (!readAsString(address) || !readAsString(end) || asString * 2 < end - address + 1)
            continue;
        // A string by its layout (a control code the printer read) or by its words: digits and blanks alone (a score,
        // a filler) say nothing a word does
        bool control = false;
        std::string plain;
        for (uint32_t a = address; a < end; ++a)
        {
            const auto z80 = static_cast<uint16_t>(a);
            if ((context.Use(z80).flags & use::kWrite) != 0)
                continue;
            const uint8_t v = context.Start(z80);
            if (Printable(v))
                plain += static_cast<char>(v);
            else
                control = true;
        }
        if (!control && !WordShaped(plain))
            continue;

        Region r;
        r.kind = RegionKind::Text;
        r.detector = DetectorId::Text;
        r.status = RegionStatus::Observed;
        r.confidence = 850;
        r.geometry.width = static_cast<uint16_t>(end - address);
        r.geometry.terminator = p.terminator == 0 ? "zero" : "$" + Hex2(p.terminator);
        for (uint32_t a = address; a < end; ++a)
        {
            const auto z80 = static_cast<uint16_t>(a);
            const uint8_t v = context.Start(z80);
            r.geometry.text += Printable(v) ? std::string(1, static_cast<char>(v)) : "{" + Hex2(v) + "}";
            const ByteUse& u = context.Use(z80);
            if ((u.flags & use::kWrite) == 0)
                continue;
            // A cell code fills: one field a run of written bytes
            const auto offset = static_cast<uint16_t>(a - address);
            const uint8_t lo = std::min(v, u.minWritten);
            const uint8_t hi = std::max(v, u.maxWritten);
            Field* last = r.geometry.fields.empty() ? nullptr : &r.geometry.fields.back();
            if (last != nullptr && last->offset + last->size == offset)
                ++last->size;
            else
                r.geometry.fields.push_back(Field{offset, 1, "filled", lo, hi, false});
        }
        r.evidence.push_back(Evidence{"argument", argument.load, argument.uses,
                                      "an immediate load passes it to the printer " + Hex4(printer->first)});
        r.evidence.push_back(Evidence{"reader", printer->first, p.strings,
                                      "prints " + std::to_string(p.strings) +
                                          (p.strings == 1 ? " other string" : " other strings") + ", each ended by " +
                                          r.geometry.terminator});
        for (const Field& f : r.geometry.fields)
            r.evidence.push_back(Evidence{"field", static_cast<uint16_t>(address + f.offset), f.size,
                                          "written by code before it is printed"});
        r.users.push_back(printer->first);
        if (context.Claim(std::move(r), address, static_cast<uint16_t>(end)))
            std::fill(owner.begin() + address, owner.begin() + end + 1, uint8_t{1});
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
    PrinterArguments(context);
    SourceRemnants(context);
}

}  // namespace analysis
