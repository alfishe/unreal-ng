/// The records detector.
///
/// Record arrays: an L1 base + offset pattern ((IX+d), (IY+d), an (HL) walk) whose bases all sit a constant step
/// apart. Size = the step, count = the bases seen; the base the code tested with one read only (a terminator test;
/// in an HL walk, the last base whose other offsets no access reached) ends the array and is claimed on its own.
/// Fields: one per offset accessed; a word where the code moved the two
/// halves of one register pair through d and d + 1; the bytes no access touched are fields of their own (inferred).
/// Each field gets the range of values seen in its first byte over the records, at the start and at the end.
///
/// Buffers: the destination of block copies (LDIR, LDI chains) that code then used, from one or more sources: the
/// sources are the buffer's instances. A record array inside a buffer gets the instances shifted by its offset.
/// The buffer's field list is the claims made inside it, filled once every detector ran (FinishBufferFields); the
/// spans between them are unclaimed fields, split where a page the code named by its high byte starts. A group of
/// variables used together (the variables detector) is a field followed by its variables, nested in it.

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <set>

#include "debugger/analyzers/analysis/detectors/detectors.h"

namespace analysis
{

namespace
{

constexpr uint32_t kMinBufferUse = 1;

struct BufferInfo
{
    uint16_t dst = 0;
    uint32_t length = 0;
    std::set<uint16_t> sources;
    std::set<uint16_t> copiers;
    uint64_t runs = 0;
};

/// Distinct block-copy destinations (dst, length) with their sources
std::vector<BufferInfo> Buffers(const DetectorContext& context)
{
    std::map<std::pair<uint16_t, uint32_t>, BufferInfo> byDst;
    for (const BlockCopy& c : context.l1.copies)
    {
        if (c.length < 16)
            continue;
        const uint16_t lo = c.dir > 0 ? c.dst : static_cast<uint16_t>(c.dst - (c.length - 1));
        const uint16_t src = c.dir > 0 ? c.src : static_cast<uint16_t>(c.src - (c.length - 1));
        BufferInfo& b = byDst[{lo, c.length}];
        b.dst = lo;
        b.length = c.length;
        b.sources.insert(src);
        b.copiers.insert(c.z80);
        b.runs += c.count;
    }
    std::vector<BufferInfo> out;
    for (auto& [key, b] : byDst)
        out.push_back(std::move(b));
    return out;
}

}  // namespace

void DetectRecords(DetectorContext& context)
{
    const L1Result& l1 = context.l1;
    const std::vector<BufferInfo> buffers = Buffers(context);

    // Buffers first: the record arrays inside them take their instances
    for (const BufferInfo& b : buffers)
    {
        const uint32_t end = b.dst + b.length - 1;
        if (end > 0xFFFF || (b.dst >> 14) != (end >> 14))
            continue;
        // Used after the copy: some byte of it is read other than by a block copy
        uint32_t used = 0;
        for (uint32_t a = b.dst; a <= end; ++a)
        {
            const ByteUse& u = context.Use(static_cast<uint16_t>(a));
            if ((u.flags & (use::kIndexed | use::kAbsolute)) != 0 || (u.flags & use::kWordRead) != 0)
                ++used;
        }
        if (used < kMinBufferUse)
            continue;
        Region r;
        r.kind = RegionKind::Buffer;
        r.detector = DetectorId::Records;
        r.status = RegionStatus::Observed;
        r.geometry.stride = static_cast<uint16_t>(b.length);
        r.geometry.count = static_cast<uint16_t>(b.sources.size());
        r.geometry.instances.assign(b.sources.begin(), b.sources.end());
        r.confidence = confidence::FromCount(600, 950, b.sources.size(), 2);
        for (const uint16_t copier : b.copiers)
            r.evidence.push_back(Evidence{"copy", copier, b.runs,
                                          std::to_string(b.sources.size()) + " sources, " +
                                              std::to_string(b.length) + " bytes"});
        r.evidence.push_back(Evidence{"used", b.dst, used, "bytes the code read or wrote after the copy"});
        r.users = context.UsersOf(b.dst, static_cast<uint16_t>(end));
        context.Claim(std::move(r), b.dst, static_cast<uint16_t>(end));
    }

    const auto bufferOf = [&](uint16_t lo, uint16_t hi) -> const BufferInfo* {
        for (const BufferInfo& b : buffers)
            if (lo >= b.dst && hi <= b.dst + b.length - 1 && !b.sources.empty())
                return &b;
        return nullptr;
    };

    for (const BaseOffsetPattern& p : l1.patterns)
    {
        const uint32_t step = static_cast<uint32_t>(std::abs(p.step));
        if (step < 2 || p.bases.size() < 2)
            continue;
        // The base-step check: every base on one lattice
        const uint16_t first = p.bases.front();
        bool lattice = true;
        for (const uint16_t base : p.bases)
            lattice = lattice && static_cast<uint16_t>(base - first) % step == 0;
        if (!lattice)
            continue;
        // Offsets inside one record
        std::vector<int16_t> offsets;
        for (const int16_t d : p.offsets)
            if (d >= 0 && static_cast<uint32_t>(d) < step)
                offsets.push_back(d);
        if (offsets.empty())
            continue;

        // A terminator test: the last base read at one offset only, where the others were read at more
        uint16_t last = p.bases.back();
        bool terminated = false;
        if (p.offsetsAtBase.empty() && p.bases.size() >= 2)
        {
            // An HL walk (no per-base offsets): the last base's other offsets had no register access, the others'
            // did
            const auto accessed = [&](uint16_t at) {
                return (context.Use(at).flags & (use::kIndexed | use::kAbsolute)) != 0;
            };
            bool rest = false, othersRest = false;
            for (const int16_t d : offsets)
                if (d > 0)
                {
                    rest |= accessed(static_cast<uint16_t>(p.bases.back() + d));
                    othersRest |= accessed(static_cast<uint16_t>(p.bases.front() + d));
                }
            if (accessed(p.bases.back()) && !rest && othersRest)
            {
                terminated = true;
                last = p.bases[p.bases.size() - 2];
            }
        }
        else if (p.offsetsAtBase.size() == p.bases.size() && p.bases.size() >= 2)
        {
            const uint8_t atLast = p.offsetsAtBase.back();
            const uint8_t most = *std::max_element(p.offsetsAtBase.begin(), p.offsetsAtBase.end());
            if (atLast == 1 && most > 1)
            {
                terminated = true;
                last = p.bases[p.bases.size() - 2];
            }
        }
        const uint32_t count = (static_cast<uint16_t>(last - first)) / step + 1;
        const uint32_t hi = first + count * step - 1;
        if (hi > 0xFFFF || (first >> 14) != (hi >> 14) || !context.InScope(first))
            continue;
        // A walk over a pointer / handler table (its words used as addresses): the table says it already
        const auto inTable = [&](uint16_t at) {
            for (const Region* c : context.out.store.ClaimsAt(context.Key(at)))
                if (c->kind == RegionKind::PointerTable || c->kind == RegionKind::HandlerTable)
                    return true;
            return false;
        };
        if (inTable(static_cast<uint16_t>(first)) && inTable(static_cast<uint16_t>(hi)))
            continue;

        Region r;
        r.kind = RegionKind::RecordArray;
        r.detector = DetectorId::Records;
        r.status = RegionStatus::Observed;
        r.geometry.stride = static_cast<uint16_t>(step);
        r.geometry.count = static_cast<uint16_t>(count);
        // Fields: the offsets, words merged, gaps as their own
        std::set<int16_t> words(p.wordOffsets.begin(), p.wordOffsets.end());
        std::vector<Field> fields;
        uint32_t cursor = 0;
        for (size_t i = 0; i < offsets.size(); ++i)
        {
            const auto d = static_cast<uint32_t>(offsets[i]);
            if (d < cursor)
                continue;
            if (d > cursor)
            {
                Field gap;
                gap.offset = static_cast<uint16_t>(cursor);
                gap.size = static_cast<uint16_t>(d - cursor);
                gap.name = "unread";
                fields.push_back(gap);
            }
            Field f;
            f.offset = static_cast<uint16_t>(d);
            f.size = 1;
            if (words.count(offsets[i]) != 0 && d + 1 < step)
            {
                f.size = 2;
                f.word = true;
                f.name = "word";
            }
            fields.push_back(f);
            cursor = d + f.size;
        }
        if (cursor < step)
        {
            Field gap;
            gap.offset = static_cast<uint16_t>(cursor);
            gap.size = static_cast<uint16_t>(step - cursor);
            gap.name = "unread";
            fields.push_back(gap);
        }
        for (Field& f : fields)
        {
            bool any = false;
            for (uint32_t k = 0; k < count; ++k)
            {
                const auto at = static_cast<uint16_t>(first + k * step + f.offset);
                for (const uint8_t v : {context.Start(at), context.End(at)})
                {
                    f.minValue = any ? std::min(f.minValue, v) : v;
                    f.maxValue = any ? std::max(f.maxValue, v) : v;
                    any = true;
                }
            }
        }
        r.geometry.fields = std::move(fields);
        if (terminated)
            r.geometry.terminator = "$" + [&] {
                char hex[4];
                std::snprintf(hex, sizeof(hex), "%02X", context.Start(p.bases.back()));
                return std::string(hex);
            }();
        if (const BufferInfo* b = bufferOf(first, static_cast<uint16_t>(hi)))
            for (const uint16_t src : b->sources)
                if (src != b->dst)
                    r.geometry.instances.push_back(static_cast<uint16_t>(src + (first - b->dst)));
        r.confidence = confidence::FromCount(500, 950, p.basesSeen, 3);
        r.evidence.push_back(Evidence{"pattern", l1.routines[p.routine].z80, p.basesSeen,
                                      std::string(BaseRegName(p.base)) + " step " + std::to_string(step) + ", " +
                                          std::to_string(offsets.size()) + " offsets"});
        r.users = {l1.routines[p.routine].z80};
        context.Claim(r, first, static_cast<uint16_t>(hi));

        if (terminated && context.InScope(p.bases.back()))
        {
            Region t;
            t.kind = RegionKind::Data;
            t.detector = DetectorId::Records;
            t.status = RegionStatus::Observed;
            t.title = "terminator";
            t.label = "";
            t.geometry.terminator = r.geometry.terminator;
            t.confidence = 800;
            t.evidence.push_back(
                Evidence{"terminator", l1.routines[p.routine].z80, 0, "the record walk's end test reads it"});
            context.Claim(std::move(t), p.bases.back(), p.bases.back());
        }
    }
}

void FinishBufferFields(DetectorContext& context)
{
    RegionStore& store = context.out.store;
    // Pages the code named by their high byte (LD H,n / LD D,n then an access in that page): an unclaimed span of a
    // buffer splits at the start of such a page (a page-aligned table or graphics block)
    std::set<uint16_t> named;
    for (const PointerUse& p : context.l1.pointers)
        if (p.form == PointerForm::ImmediateHigh && p.value <= 0xFF && p.uses != 0 && p.minDelta >= 0 &&
            p.maxDelta <= 0xFF)
            named.insert(static_cast<uint16_t>(p.value << 8));
    for (size_t i = 0; i < store.Size(); ++i)
    {
        const Region& buffer = store.Regions()[i];
        if (buffer.kind != RegionKind::Buffer)
            continue;
        // The claims inside, outermost first: one field per claim, gaps as their own
        std::vector<std::pair<uint32_t, uint32_t>> spans;  // offset, size
        std::vector<std::string> names;
        std::vector<bool> groups;  // a group of variables used together: listed, then its variables inside it
        for (const Region* r : store.Overlapping(buffer.lo, buffer.hi))
        {
            if (r == &buffer || r->lo < buffer.lo || r->hi > buffer.hi || r->kind == RegionKind::Untouched ||
                r->kind == RegionKind::Code || r->kind == RegionKind::Data)
                continue;
            const bool group = r->kind == RegionKind::RecordArray && r->detector == DetectorId::Variables &&
                               r->geometry.count == 1;
            spans.emplace_back(r->lo - buffer.lo, r->Size());
            names.push_back(r->title == "terminator" ? "terminator" : group ? "group" : RegionKindName(r->kind));
            groups.push_back(group);
        }
        for (const Region* r : store.Overlapping(buffer.lo, buffer.hi))
            if (r->kind == RegionKind::Data && r->title == "terminator" && r->lo >= buffer.lo && r->hi <= buffer.hi)
            {
                spans.emplace_back(r->lo - buffer.lo, r->Size());
                names.push_back("terminator");
                groups.push_back(false);
            }
        std::vector<size_t> order(spans.size());
        for (size_t k = 0; k < order.size(); ++k)
            order[k] = k;
        std::sort(order.begin(), order.end(), [&](size_t a, size_t b) {
            if (spans[a].first != spans[b].first)
                return spans[a].first < spans[b].first;
            if (spans[a].second != spans[b].second)
                return spans[a].second > spans[b].second;
            return groups[a] && !groups[b];  // a group before a variable as large as it
        });
        std::vector<Field> fields;
        uint32_t cursor = 0;
        const uint32_t size = buffer.Size();
        const auto gap = [&](uint32_t from, uint32_t to) {  // [from, to)
            uint32_t at = from;
            for (uint32_t cut = from + 1; cut < to; ++cut)
                if (named.count(static_cast<uint16_t>(buffer.z80 + cut)) != 0 && (buffer.z80 + cut) % 256 == 0)
                {
                    fields.push_back(Field{static_cast<uint16_t>(at), static_cast<uint16_t>(cut - at), "unclaimed", 0,
                                           0, false});
                    at = cut;
                }
            fields.push_back(
                Field{static_cast<uint16_t>(at), static_cast<uint16_t>(to - at), "unclaimed", 0, 0, false});
        };
        uint32_t groupEnd = 0;  // the end of the group being listed: its variables follow it, no gaps inside
        for (const size_t k : order)
        {
            const auto [offset, length] = spans[k];
            if (offset >= groupEnd)
                cursor = std::max(cursor, groupEnd);
            const bool member = !groups[k] && offset < groupEnd && offset + length <= groupEnd;
            if (offset < cursor || (!member && offset < groupEnd))
                continue;  // inside a field already listed
            if (offset > cursor && !member)
                gap(cursor, offset);
            fields.push_back(
                Field{static_cast<uint16_t>(offset), static_cast<uint16_t>(length), names[k], 0, 0, length == 2});
            if (groups[k])
            {
                groupEnd = offset + length;
                cursor = offset;  // its variables are listed after it
            }
            else
                cursor = offset + length;
        }
        cursor = std::max(cursor, groupEnd);
        if (cursor < size && !fields.empty())
            gap(cursor, size);
        for (Field& f : fields)
        {
            uint16_t z80 = 0;
            if (!context.Z80Of(buffer.lo + f.offset, z80))
                continue;
            f.minValue = f.maxValue = context.Start(z80);
            for (const uint16_t src : buffer.geometry.instances)
            {
                const uint8_t v = context.Start(static_cast<uint16_t>(src + f.offset));
                f.minValue = std::min(f.minValue, v);
                f.maxValue = std::max(f.maxValue, v);
            }
        }
        store.SetFields(i, std::move(fields));
    }
}

}  // namespace analysis
