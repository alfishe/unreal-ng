/// The text detector.
///
/// Printed text: a run of printable bytes ($20..$7E, the last may carry bit 7) that code read as data and never
/// wrote, read by a routine on a print path: one that reads a font (a Font claim: the ROM character set or one the
/// font detector found) or prints through the ROM (RST $10, the ROM print routines), itself or through a callee
/// (two calls deep). The terminator is a zero after the run, bit 7 on its last character, a length byte before it,
/// or none (the printing code holds the count).
///
/// Source remnant: a run of bytes no code touched (the stack aside) that parses as tokenized assembler source - at
/// least four lines of [line number lo, hi][length][text] with rising line numbers - is Unused, with the decoded
/// lines as its text.

#include <algorithm>
#include <set>

#include "debugger/analyzers/analysis/detectors/detectors.h"

namespace analysis
{

namespace
{

constexpr uint32_t kMinText = 3;
constexpr uint32_t kMinRemnant = 64;
constexpr int kMinSourceLines = 4;

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

/// Routine indices on a print path (directly)
std::set<uint32_t> PrintRoutines(DetectorContext& context)
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
    const std::set<uint32_t> printers = PrintRoutines(context);
    if (printers.empty())
        return;

    // Per byte: read, never written, printable, by a printing reader
    std::vector<uint8_t> depthOf(0x10000, 0);
    std::vector<uint16_t> readerOf(0x10000, 0);
    for (uint32_t a = 0; a <= 0xFFFF; ++a)
    {
        const auto z80 = static_cast<uint16_t>(a);
        if (!context.InScope(z80))
            continue;
        const ByteUse& u = context.Use(z80);
        if ((u.flags & use::kRead) == 0 || (u.flags & use::kWrite) != 0 || DetectorContext::Code(u))
            continue;
        const uint8_t c = context.Start(z80);
        if (!Printable(c) && !Printable(static_cast<uint8_t>(c & 0x7F)))
            continue;
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
        while (b + 1 <= 0xFFFF && depthOf[b + 1] != 0 && ((b + 1) >> 14) == (a >> 14) &&
               (context.Start(static_cast<uint16_t>(b)) & 0x80) == 0)
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
                r.geometry.text += static_cast<char>(context.Start(static_cast<uint16_t>(i)) & 0x7F);
            }
            r.confidence = depth == 1 ? 900 : 800;
            const uint8_t last = context.Start(static_cast<uint16_t>(b));
            const uint8_t after = context.Start(static_cast<uint16_t>(b + 1));
            const uint8_t before = context.Start(static_cast<uint16_t>(a - 1));
            if ((last & 0x80) != 0)
                r.geometry.terminator = "bit7";
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
