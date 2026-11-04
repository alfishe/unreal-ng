/// The records detector.
///
/// Record arrays: an L1 base + offset pattern ((IX+d), (IY+d), an (HL) walk) whose bases all sit a constant step
/// apart (an HL walk that also restarts inside its records folds those bases onto the record starts, its offsets
/// shifted by the restart's place in the record). Size = the step, count = the bases seen (more than 16 records need a
/// base in one record of four at least); the base the code tested with one read only (a terminator test; in an HL
/// walk, the last base whose offsets past the tested byte no access reached, a restart's own place when the walk
/// tested there) ends the array and is claimed on its own.
/// Fields: one per offset accessed; a word where the code moved the two
/// halves of one register pair through d and d + 1; the bytes no access touched are fields of their own (inferred).
/// Each field gets the range of values seen in its first byte over the records, at the start and at the end.
/// One array seen from two places (a routine reads each record from its start, another from a byte on: handed the
/// record's address + 1) gives two patterns of one step whose starts lie less than a step apart; when the later one's
/// accesses fit in the earlier one's records they are one array, the offsets shifted, and no record runs past it.
///
/// Gathered records: one copy instruction moving a few bytes at a time from records a constant step apart into the
/// slots of a record array (GatheredRecords): the source blocks are records with the slots' fields; blocks a pointer
/// table names on the blocks' lattice are inferred.
///
/// Arrays a fill completes: an array of a known stride (an index base + S x value, or a record claim) and one bounded
/// fill of a constant that starts on its lattice and is a whole number of strides long: one array over both, the rows
/// no access reached inferred (ArraysCompletedByFills). Records of one type: immediates handed to one walk of one
/// extent that stops at one constant are records of one type, each the others' instance (SentinelRecords).
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
#include <numeric>
#include <set>
#include <tuple>
#include <vector>

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

/// Whether a record array of @p stride over the Z80 [lo, hi] (one page) would clash with a claim made before it (the
/// region store's conflict rules: code, text and unused bytes hold nothing else; two record arrays of different
/// strides cannot both hold)
bool RecordsClash(const DetectorContext& context, uint32_t lo, uint32_t hi, uint32_t stride)
{
    if (hi > 0xFFFF || hi < lo || (lo >> 14) != (hi >> 14))
        return true;
    for (const Region* r : context.out.store.Overlapping(context.Key(static_cast<uint16_t>(lo)),
                                                         context.Key(static_cast<uint16_t>(hi))))
    {
        if (r->kind == RegionKind::Untouched || r->title == kRomWriteSink)
            continue;
        if (r->kind == RegionKind::Code || r->kind == RegionKind::Text || r->kind == RegionKind::Unused)
            return true;
        if (r->kind == RegionKind::RecordArray && r->geometry.stride != stride)
            return true;
    }
    return false;
}

bool Printable(uint8_t c)
{
    c = static_cast<uint8_t>(c & 0x7F);
    return c >= 0x20 && c < 0x7F;
}

/// The buffer's sources the replay did not reach: more slots on the sources' lattice (the step: the smallest distance
/// between two sources, the length with one source) between, below and above them in their 16 K bank, while a slot
/// holds no code and no other claim and the buffer's text (its Text claims) is printable there too. A buffer without
/// text gets none: nothing would tell one more level from what follows the levels
std::vector<uint16_t> LatticeSources(const DetectorContext& context, const BufferInfo& b,
                                     const std::vector<uint8_t>& claimed)
{
    std::vector<uint16_t> out;
    if (b.sources.empty())
        return out;
    const uint32_t end = b.dst + b.length - 1;
    std::vector<std::pair<uint32_t, uint32_t>> texts;  // offset, characters
    for (const Region* r : context.out.store.Overlapping(context.Key(b.dst), context.Key(static_cast<uint16_t>(end))))
        if (r->kind == RegionKind::Text && r->z80 >= b.dst && r->Z80End() <= end && r->geometry.width != 0)
            texts.emplace_back(r->z80 - b.dst, r->geometry.width);
    if (texts.empty())
        return out;
    const uint32_t first = *b.sources.begin();
    const uint32_t last = *b.sources.rbegin();
    uint32_t step = 0;
    for (auto it = std::next(b.sources.begin()); it != b.sources.end(); ++it)
    {
        const uint32_t d = static_cast<uint32_t>(*it) - *std::prev(it);
        step = step == 0 ? d : std::min(step, d);
    }
    step = step == 0 ? b.length : step;
    if (step < b.length)
        return out;
    const uint32_t bankLo = first & 0xC000u;
    const uint32_t bankHi = bankLo + 0x3FFFu;
    for (const uint16_t src : b.sources)
        if ((src - first) % step != 0 || src + b.length - 1 > bankHi)
            return out;
    const auto fits = [&](uint32_t slot) {
        if (slot < bankLo || slot + b.length - 1 > bankHi || (slot <= end && slot + b.length - 1 >= b.dst))
            return false;
        for (uint32_t a = slot; a < slot + b.length; ++a)
            if (!context.InScope(static_cast<uint16_t>(a)) || claimed[a] != 0 ||
                DetectorContext::Code(context.Use(static_cast<uint16_t>(a))))
                return false;
        for (const auto& [offset, size] : texts)
            for (uint32_t i = 0; i < size; ++i)
                if (!Printable(context.Start(static_cast<uint16_t>(slot + offset + i))))
                    return false;
        return true;
    };
    for (uint32_t slot = first + step; slot < last; slot += step)
        if (b.sources.count(static_cast<uint16_t>(slot)) == 0 && fits(slot))
            out.push_back(static_cast<uint16_t>(slot));
    for (uint32_t slot = first; slot >= bankLo + step && fits(slot - step); slot -= step)
        out.push_back(static_cast<uint16_t>(slot - step));
    for (uint32_t slot = last + step; fits(slot); slot += step)
        out.push_back(static_cast<uint16_t>(slot));
    std::sort(out.begin(), out.end());
    return out;
}

/// A record array one base + offset pattern found, before arrays seen from two places merge
struct Candidate
{
    uint16_t first = 0;
    uint32_t step = 0;
    uint32_t count = 0;
    std::set<int16_t> offsets;  ///< inside one record
    std::set<int16_t> words;
    bool terminated = false;
    uint16_t terminator = 0;    ///< the base read as the end test (terminated)
    std::vector<const BaseOffsetPattern*> patterns;
    bool merged = false;        ///< folded into another candidate
    uint32_t Hi() const { return first + count * step - 1; }
};

/// The values seen in each field's first byte over @p count records of @p step bytes from @p first
void FieldValues(const DetectorContext& context, std::vector<Field>& fields, uint32_t step, uint16_t first,
                 uint32_t count)
{
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
}

/// The fields of a record of @p step bytes: the offsets (words merged; @p sizes: the bytes a field at that offset
/// spans, a block one copy read), gaps as their own; the values seen in each field's first byte over @p count records
/// from @p first
std::vector<Field> FieldsOf(const DetectorContext& context, const std::set<int16_t>& offsetSet,
                            const std::set<int16_t>& words, uint32_t step, uint16_t first, uint32_t count,
                            const std::map<int16_t, uint16_t>& sizes = {})
{
    std::vector<Field> fields;
    uint32_t cursor = 0;
    for (const int16_t offset : offsetSet)
    {
        const auto d = static_cast<uint32_t>(offset);
        if (d < cursor || d >= step)
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
        if (words.count(offset) != 0 && d + 1 < step)
        {
            f.size = 2;
            f.word = true;
            f.name = "word";
        }
        else if (const auto it = sizes.find(offset); it != sizes.end() && it->second > 1)
            f.size = static_cast<uint16_t>(std::min<uint32_t>(it->second, step - d));
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
    FieldValues(context, fields, step, first, count);
    return fields;
}

/// Records gathered by short block copies: one copy instruction moving L bytes at a time from records S bytes apart
/// into the slots of a record array D bytes apart (a level's objects copied into the live object slots, a few bytes
/// of each slot). Each copy's slot j = (dst - first slot) / D names its record in the source: start = src - j * S; the
/// sources whose starts agree are one block of N records (N: the slots). The source is records of S bytes with the
/// slot's fields at the copied bytes; each block is claimed (observed). A pointer table the earlier detectors found
/// that points at an observed block and at more places on the blocks' lattice (N * S) names the blocks the replay did
/// not copy (its words, and the words right after it while each names a free block on the lattice: entries the
/// replay never read): claimed too (inferred), while their bytes hold no code and no other claim
void GatheredRecords(DetectorContext& context)
{
    constexpr size_t kMinSlots = 4;
    std::map<uint16_t, std::vector<const BlockCopy*>> byCopier;
    for (const BlockCopy& c : context.l1.copies)
        if (c.dir > 0 && c.length >= 2)
            byCopier[c.z80].push_back(&c);
    for (const auto& [copier, copies] : byCopier)
    {
        const uint32_t length = copies.front()->length;
        std::set<std::pair<uint16_t, uint16_t>> pairs;  // src, dst
        std::set<uint16_t> dsts;
        bool same = true;
        for (const BlockCopy* c : copies)
        {
            same = same && c->length == length;
            pairs.emplace(c->src, c->dst);
            dsts.insert(c->dst);
        }
        if (!same || dsts.size() < kMinSlots)
            continue;
        // The slots: one lattice, D >= L
        uint32_t slotStep = 0;
        for (auto it = std::next(dsts.begin()); it != dsts.end(); ++it)
            slotStep = std::gcd(slotStep, static_cast<uint32_t>(*it - *std::prev(it)));
        if (slotStep < length)
            continue;
        const uint16_t firstSlot = *dsts.begin();
        const uint32_t slots = (*dsts.rbegin() - firstSlot) / slotStep + 1;
        // The slots are a record array's: the copied bytes inside one of its records. Its place and fields copied out
        // (the claims below add to the store, which may move its regions)
        const Region* found = nullptr;
        for (const Region* r : context.out.store.Overlapping(context.Key(firstSlot), context.Key(*dsts.rbegin())))
            if (r->kind == RegionKind::RecordArray && r->geometry.stride == slotStep && r->z80 <= firstSlot &&
                r->Z80End() >= *dsts.rbegin() + length - 1)
                found = r;
        if (found == nullptr)
            continue;
        const uint16_t targetZ80 = found->z80;
        const std::vector<Field> targetFields = found->geometry.fields;
        const uint32_t into = (firstSlot - targetZ80) % slotStep;
        if (into + length > slotStep)
            continue;
        // The source step: the commonest distance between the sources of neighbouring slots
        std::map<uint32_t, uint32_t> steps;
        for (const auto& [src, dst] : pairs)
        {
            if (src == 0xFFFF)
                continue;  // no source above it (src + 1 would wrap to the first)
            if (const auto next = pairs.lower_bound({static_cast<uint16_t>(src + 1), 0}); next != pairs.end() &&
                next->second == dst + slotStep)
                ++steps[static_cast<uint32_t>(next->first - src)];
        }
        uint32_t step = 0, best = 0;
        for (const auto& [d, n] : steps)
            if (n > best)
            {
                best = n;
                step = d;
            }
        if (step < length)
            continue;
        const uint32_t blockBytes = slots * step;
        std::map<uint16_t, std::set<uint32_t>> blocks;  // start -> the slots copied from it
        bool lattice = true;
        for (const auto& [src, dst] : pairs)
        {
            const uint32_t j = (dst - firstSlot) / slotStep;
            const int32_t start = static_cast<int32_t>(src) - static_cast<int32_t>(j * step);
            lattice = lattice && start >= 0;
            if (start >= 0)
                blocks[static_cast<uint16_t>(start)].insert(j);
        }
        if (!lattice)
            continue;
        for (auto it = blocks.begin(); it != blocks.end();)
            it = it->second.size() < 2 ? blocks.erase(it) : std::next(it);
        if (blocks.empty())
            continue;
        uint32_t previousEnd = 0;
        for (const auto& [start, js] : blocks)
        {
            lattice = lattice && (previousEnd == 0 || start >= previousEnd) && start + blockBytes - 1 <= 0xFFFF &&
                      (start >> 14) == ((start + blockBytes - 1) >> 14);
            previousEnd = start + blockBytes;
        }
        if (!lattice)
            continue;
        const uint16_t base = blocks.begin()->first;
        // The fields: the slot's fields inside the copied bytes, shifted to the record's start
        std::set<int16_t> offsets, words;
        for (const Field& f : targetFields)
            if (f.name != "unread" && f.offset >= into && f.offset < into + length)
            {
                offsets.insert(static_cast<int16_t>(f.offset - into));
                if (f.word && f.offset + 1u < into + length)
                    words.insert(static_cast<int16_t>(f.offset - into));
            }
        // Blocks a pointer table names on the lattice
        std::set<uint16_t> inferred;
        const std::vector<uint8_t> claimed = context.ClaimedMap();
        const auto free = [&](uint32_t at) {
            if ((at >> 14) != (base >> 14) || ((at + blockBytes - 1) >> 14) != (base >> 14))
                return false;
            for (uint32_t a = at; a < at + blockBytes; ++a)
                if (!context.InScope(static_cast<uint16_t>(a)) || claimed[a] != 0 ||
                    DetectorContext::Code(context.Use(static_cast<uint16_t>(a))))
                    return false;
            return true;
        };
        for (const Region& t : context.out.store.Regions())
        {
            if (t.kind != RegionKind::PointerTable || t.Size() < 2)
                continue;
            std::set<uint16_t> named;
            bool confirms = false;
            // The table's words, and the words right after it while each names a free block on the lattice (a
            // table whose last entries the replay never read)
            for (uint32_t at = t.z80; at + 1 <= 0xFFFF; at += 2)
            {
                const auto v = static_cast<uint16_t>(context.Start(static_cast<uint16_t>(at)) |
                                                     (context.Start(static_cast<uint16_t>(at + 1)) << 8));
                const bool onLattice = v >= base && (v - base) % blockBytes == 0;
                if (at > t.Z80End() && (!onLattice || blocks.count(v) != 0 || !free(v) ||
                                        claimed[at] != 0 || claimed[at + 1] != 0))
                    break;
                if (blocks.count(v) != 0)
                    confirms = true;
                else if (onLattice)
                    named.insert(v);
            }
            if (!confirms)
                continue;
            for (const uint16_t v : named)
                if (free(v))
                    inferred.insert(v);
        }
        const auto claim = [&](uint16_t at, bool observed, uint64_t copied) {
            Region r;
            r.kind = RegionKind::RecordArray;
            r.detector = DetectorId::Records;
            r.status = observed ? RegionStatus::Observed : RegionStatus::Inferred;
            r.geometry.stride = static_cast<uint16_t>(step);
            r.geometry.count = static_cast<uint16_t>(slots);
            r.geometry.fields = FieldsOf(context, offsets, words, step, at, slots);
            r.confidence = observed ? confidence::FromCount(600, 900, copied, 2) : 700;
            r.evidence.push_back(Evidence{"gather", copier, copied,
                                          std::to_string(length) + " bytes a record copied into the " +
                                              std::to_string(slotStep) + "-byte records at $" + [&] {
                                                  char hex[6];
                                                  std::snprintf(hex, sizeof(hex), "%04X", targetZ80);
                                                  return std::string(hex);
                                              }()});
            if (!observed)
                r.evidence.push_back(Evidence{"lattice", base, blocks.size(),
                                              "a pointer table names it on the copied blocks' step of " +
                                                  std::to_string(blockBytes) + " bytes (inferred)"});
            r.users = observed ? context.UsersOf(at, static_cast<uint16_t>(at + blockBytes - 1))
                               : std::vector<uint16_t>{copier};
            // An observed block another claim explains differently (code, text, records of another size) is left to
            // it (the inferred ones were checked free of any claim)
            if (observed && RecordsClash(context, at, at + blockBytes - 1, step))
                return;
            context.Claim(std::move(r), at, static_cast<uint16_t>(at + blockBytes - 1));
        };
        for (const auto& [start, js] : blocks)
            claim(start, true, js.size());
        for (const uint16_t at : inferred)
            claim(at, false, 0);
    }
}

/// Record templates: one copy instruction filling records of an array (a new object, a fresh shot) from a few blocks
/// of L bytes outside it (4 bytes or more, the same length every copy, each landing at one offset of a record, inside
/// it). The sources lie on one lattice of L (a table of templates, one a kind) and fewer of them than the slots they
/// filled: a record array of L-byte templates, from the first copied to the last, with the target's fields. Only
/// block copies wrote the templates' bytes, if anything did (a working copy of an object the code updates and then
/// copies into the slots is no template), and no claim before it explains them differently
void RecordTemplates(DetectorContext& context)
{
    constexpr uint32_t kMinLength = 4;
    std::map<uint16_t, std::vector<const BlockCopy*>> byCopier;
    for (const BlockCopy& c : context.l1.copies)
        if (c.dir > 0 && c.length >= kMinLength)
            byCopier[c.z80].push_back(&c);
    for (const auto& [copier, copies] : byCopier)
    {
        const uint32_t length = copies.front()->length;
        std::set<uint16_t> srcs, dsts;
        bool same = true;
        for (const BlockCopy* c : copies)
        {
            same = same && c->length == length;
            srcs.insert(c->src);
            dsts.insert(c->dst);
        }
        if (!same || dsts.size() < 2 || srcs.size() >= dsts.size())
            continue;
        const Region* found = nullptr;
        for (const Region* r : context.out.store.Overlapping(context.Key(*dsts.begin()), context.Key(*dsts.rbegin())))
            if (r->kind == RegionKind::RecordArray && r->geometry.stride >= length && r->z80 <= *dsts.begin() &&
                r->Z80End() >= *dsts.rbegin() + length - 1 &&
                (found == nullptr || r->Size() > found->Size()))
                found = r;
        if (found == nullptr)
            continue;
        // Copied out: the claim below adds to the store, which may move its regions
        const uint16_t targetZ80 = found->z80;
        const uint16_t targetEnd = found->Z80End();
        const std::vector<Field> targetFields = found->geometry.fields;
        const uint32_t stride = found->geometry.stride;
        const uint32_t into = (*dsts.begin() - targetZ80) % stride;
        bool fits = into + length <= stride;
        for (const uint16_t d : dsts)
            fits = fits && (d - targetZ80) % stride == into;
        const uint32_t lo = *srcs.begin();
        const uint32_t hi = *srcs.rbegin() + length - 1;
        for (const uint16_t src : srcs)
            fits = fits && (src - lo) % length == 0;
        if (!fits || hi > 0xFFFF || (lo >> 14) != (hi >> 14) || (lo <= targetEnd && hi >= targetZ80))
            continue;
        // A working copy is a source the code keeps rewriting: most sources written by code other than the copies
        // are one, while a table of templates may have a field of one patched now and then
        bool data = true;
        size_t rewritten = 0;
        for (const uint16_t src : srcs)
        {
            bool written = false;
            for (uint32_t at = src; at < src + length && data; ++at)
            {
                const ByteUse& u = context.Use(static_cast<uint16_t>(at));
                data = !DetectorContext::Code(u);
                written = written || (u.flags & (use::kOtherWrite | use::kIncDec)) != 0;
            }
            rewritten += written ? 1 : 0;
        }
        data = data && rewritten * 2 <= srcs.size() - 1;
        if (!data || RecordsClash(context, lo, hi, length))
            continue;
        std::set<int16_t> offsets, words;
        for (const Field& f : targetFields)
            if (f.name != "unread" && f.offset >= into && f.offset < into + length)
            {
                offsets.insert(static_cast<int16_t>(f.offset - into));
                if (f.word && f.offset + 1u < into + length)
                    words.insert(static_cast<int16_t>(f.offset - into));
            }
        const uint32_t count = (hi - lo + 1) / length;
        Region r;
        r.kind = RegionKind::RecordArray;
        r.detector = DetectorId::Records;
        r.status = srcs.size() == count ? RegionStatus::Observed : RegionStatus::Inferred;
        r.geometry.stride = static_cast<uint16_t>(length);
        r.geometry.count = static_cast<uint16_t>(count);
        r.geometry.fields = FieldsOf(context, offsets, words, length, static_cast<uint16_t>(lo), count);
        r.confidence = confidence::FromCount(600, 900, dsts.size(), 2);
        char hex[6];
        std::snprintf(hex, sizeof(hex), "%04X", targetZ80);
        r.evidence.push_back(Evidence{"template", copier, dsts.size(),
                                      std::to_string(srcs.size()) + " of them copied into " +
                                          std::to_string(dsts.size()) + " records of the array at $" + hex + ", " +
                                          std::to_string(length) + " bytes from +" + std::to_string(into)});
        r.users = context.UsersOf(static_cast<uint16_t>(lo), static_cast<uint16_t>(hi));
        context.Claim(std::move(r), static_cast<uint16_t>(lo), static_cast<uint16_t>(hi));
    }
}

/// Records read through an index: a table each of whose records some instructions read through an index (L1
/// indexes: a direction, a kind turned into base + S x value), one instruction a field. The bytes those instructions
/// read repeat their reader every S bytes, two readers or more a record, bytes no access touched between them (a
/// field the code never reads); 4 records or more in a row. The smallest S that holds is the record size
void IndexedRecords(DetectorContext& context)
{
    constexpr uint32_t kMaxStride = 16;
    constexpr uint32_t kMinRecords = 4;
    std::set<PhysKey> indexed;
    for (const IndexUse& x : context.l1.indexes)
        indexed.insert(x.instruction);
    const std::vector<uint8_t> claimed = context.ClaimedMap();
    // A byte whose value reached the screen is a picture's (a sprite drawn by unrolled code from base + S x frame)
    const auto drawn = [&](uint16_t at) {
        const PhysKey key = context.Key(at);
        const auto it = std::lower_bound(context.l1.drawn.begin(), context.l1.drawn.end(), key,
                                         [](const DrawnByte& d, PhysKey k) { return d.key < k; });
        return it != context.l1.drawn.end() && it->key == key && it->draws != 0;
    };
    // The one indexed instruction that read the byte (kNoKey: none, or several)
    const auto reader = [&](uint32_t at) -> PhysKey {
        if (at > 0xFFFF || !context.InScope(static_cast<uint16_t>(at)) || claimed[at] != 0 ||
            drawn(static_cast<uint16_t>(at)))
            return kNoKey;
        const ByteUse& u = context.Use(static_cast<uint16_t>(at));
        if ((u.flags & use::kRead) == 0 || (u.flags & (use::kBlockRead | use::kWrite)) != 0 ||
            u.firstReader != u.lastReader || indexed.count(u.firstReader) == 0)
            return kNoKey;
        return u.firstReader;
    };
    const auto untouched = [&](uint32_t at) {
        if (at > 0xFFFF || !context.InScope(static_cast<uint16_t>(at)) || claimed[at] != 0)
            return false;
        const ByteUse& u = context.Use(static_cast<uint16_t>(at));
        return u.reads == 0 && u.writes == 0;
    };
    uint32_t at = 0x4000;
    while (at <= 0xFFFF)
    {
        if (reader(at) == kNoKey)
        {
            ++at;
            continue;
        }
        uint32_t best = 0, bestRecords = 0;
        std::vector<PhysKey> fields;
        for (uint32_t stride = 2; stride <= kMaxStride && best == 0; ++stride)
        {
            std::vector<PhysKey> r(stride);
            std::set<PhysKey> distinct;
            bool ok = true;
            for (uint32_t o = 0; o < stride && ok; ++o)
            {
                r[o] = reader(at + o);
                ok = r[o] != kNoKey || untouched(at + o);
                if (r[o] != kNoKey)
                    ok = ok && distinct.insert(r[o]).second;
            }
            if (!ok || distinct.size() < 2)
                continue;
            uint32_t records = 1;
            for (;; ++records)
            {
                const uint32_t b = at + records * stride;
                bool same = (b >> 14) == (at >> 14) && b + stride - 1 <= 0xFFFF;
                for (uint32_t o = 0; o < stride && same; ++o)
                    same = r[o] == kNoKey ? untouched(b + o) : reader(b + o) == r[o];
                if (!same)
                    break;
            }
            if (records >= kMinRecords)
            {
                best = stride;
                bestRecords = records;
                fields = r;
            }
        }
        if (best == 0)
        {
            ++at;
            continue;
        }
        std::set<int16_t> offsets, words;
        for (uint32_t o = 0; o < best; ++o)
            if (fields[o] != kNoKey)
                offsets.insert(static_cast<int16_t>(o));
        const auto lo = static_cast<uint16_t>(at);
        const auto hi = static_cast<uint16_t>(at + best * bestRecords - 1);
        Region r;
        r.kind = RegionKind::RecordArray;
        r.detector = DetectorId::Records;
        r.status = RegionStatus::Observed;
        r.geometry.stride = static_cast<uint16_t>(best);
        r.geometry.count = static_cast<uint16_t>(bestRecords);
        r.geometry.fields = FieldsOf(context, offsets, words, best, lo, bestRecords);
        r.confidence = confidence::FromCount(600, 850, bestRecords, kMinRecords);
        uint16_t first = 0;
        for (const PhysKey k : fields)
            if (k != kNoKey && first == 0)
                for (const IndexUse& x : context.l1.indexes)
                    if (x.instruction == k)
                    {
                        first = x.z80;
                        break;
                    }
        r.evidence.push_back(Evidence{"indexed", first, offsets.size(),
                                      std::to_string(offsets.size()) + " fields a record, each read by its own "
                                                                       "instruction through an index"});
        r.users = context.UsersOf(lo, hi);
        context.Claim(std::move(r), lo, hi);
        at = static_cast<uint32_t>(hi) + 1;
    }
}

std::string HexWord(uint32_t v)
{
    char text[8];
    std::snprintf(text, sizeof(text), "$%04X", v & 0xFFFFu);
    return text;
}

/// A bounded fill of one value over one span: a loop storing a constant one byte a pass ((HL),n / (IX+d),n, the
/// address moving by one), or a block copy one byte behind itself (LD (HL),n then LDIR from HL to HL + 1)
struct ConstantFill
{
    uint16_t lo = 0;
    uint16_t hi = 0;   ///< inclusive
    uint16_t z80 = 0;  ///< the storing instruction
    PhysKey key = 0;
    bool operator<(const ConstantFill& o) const { return std::tie(lo, hi, z80) < std::tie(o.lo, o.hi, o.z80); }
    bool operator==(const ConstantFill& o) const { return lo == o.lo && hi == o.hi && z80 == o.z80; }
};

std::vector<ConstantFill> ConstantFills(const DetectorContext& context)
{
    constexpr uint32_t kMaxFill = 4096;
    std::vector<ConstantFill> out;
    for (const Loop& loop : context.l1.loops)
        for (const Stride& s : loop.strides)
        {
            // One span, walked once a byte at a time (one start: a bounded fill, not a pointer moving on)
            if (!s.write || !s.constant || s.sweep || std::abs(s.stride) != 1 || s.ranges.size() != 1 ||
                s.rangesMore || s.starts.size() != 1)
                continue;
            const Instruction* in = context.l1.InstructionAt(s.instruction);
            if (in == nullptr)
                continue;
            const bool constant = (in->length == 2 && in->bytes[0] == 0x36) ||
                                  (in->length == 4 && (in->bytes[0] == 0xDD || in->bytes[0] == 0xFD) &&
                                   in->bytes[1] == 0x36);
            const Interval& r = s.ranges.front();
            if (!constant || r.hi < r.lo)
                continue;
            const uint32_t size = r.hi - r.lo + 1;
            if (size < 2 || size > kMaxFill || r.z80 + size - 1 > 0xFFFF)
                continue;
            out.push_back(ConstantFill{r.z80, static_cast<uint16_t>(r.z80 + size - 1), s.z80, s.instruction});
        }
    for (const BlockCopy& c : context.l1.copies)
    {
        if (c.length < 2 || c.length >= kMaxFill)
            continue;
        if (c.dir > 0 && c.dst == static_cast<uint16_t>(c.src + 1) && c.src + c.length <= 0xFFFFu)
            out.push_back(ConstantFill{c.src, static_cast<uint16_t>(c.src + c.length), c.z80, c.instruction});
        else if (c.dir < 0 && c.src == static_cast<uint16_t>(c.dst + 1) && c.src >= c.length)
            out.push_back(ConstantFill{static_cast<uint16_t>(c.src - c.length), c.src, c.z80, c.instruction});
    }
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}

/// Arrays a fill completes: an array whose stride S is known (an index: base + S x value, or a record claim) and one
/// bounded fill of a constant over a span that starts on its lattice, is a whole number of strides long and runs on
/// from the rows the replay saw (overlapping them or right next to them). The fill clears the whole table, the rows
/// no access reached included: one array over both, the unread rows inferred with the same fields (a high score table
/// whose first rows the session played, the rest only cleared).
///
/// The index: L1's indexes of one selector, the access's own base the LD rr,nn whose value it read through (an
/// immediate pointer first used by that access; other bases less than a record above it are fields of the same
/// table). S is the largest step the rows read and the fill agree on: the gcd of each access's span (highest less
/// lowest address read), the fill's distance from the base and its length. Each access is a field at its offset in
/// the record (a block copy reading through the index: its length), the base is row 0.
///
/// The rows the fill adds hold no code, and no instruction but the fill's and those of the routines that used the
/// rows seen read or wrote them (a fill that clears a table and the variables after it stops at the table)
void ArraysCompletedByFills(DetectorContext& context)
{
    constexpr uint32_t kMaxStride = 64;
    constexpr uint32_t kMaxRecords = 256;
    const std::vector<ConstantFill> fills = ConstantFills(context);
    if (fills.empty())
        return;
    std::set<std::tuple<uint16_t, uint16_t, uint32_t>> done;  // lo, hi, stride
    for (const Region& r : context.out.store.Regions())
        if (r.kind == RegionKind::RecordArray)
            done.emplace(r.z80, r.Z80End(), r.geometry.stride);
    const auto plain = [&](uint32_t lo, uint32_t hi) {
        if (hi > 0xFFFF || (lo >> 14) != (hi >> 14))
            return false;
        for (uint32_t a = lo; a <= hi; ++a)
            if (!context.InScope(static_cast<uint16_t>(a)) ||
                DetectorContext::Code(context.Use(static_cast<uint16_t>(a))))
                return false;
        return true;
    };
    // The array the fill makes of the rows seen [seenLo, seenHi] on a lattice of S from the base (no fill: none)
    struct Completion
    {
        const ConstantFill* fill = nullptr;
        uint32_t lo = 0;
        uint32_t hi = 0;
    };
    const auto complete = [&](const ConstantFill& f, uint32_t base, uint32_t stride, uint32_t seenLo,
                              uint32_t seenHi) -> Completion {
        const uint32_t length = f.hi - f.lo + 1u;
        const uint32_t distance = f.lo >= base ? f.lo - base : base - f.lo;
        if (stride < 2 || distance % stride != 0 || length % stride != 0)
            return {};
        if (f.lo > seenHi + 1 || f.hi + 1u < seenLo || (f.hi <= seenHi && f.lo >= seenLo))
            return {};
        Completion c{&f, std::min<uint32_t>(seenLo, f.lo), std::max<uint32_t>(seenHi, f.hi)};
        if ((c.hi - c.lo + 1) / stride > kMaxRecords || !plain(c.lo, c.hi) || RecordsClash(context, c.lo, c.hi, stride))
            return {};
        const std::vector<uint16_t> users =
            context.UsersOf(static_cast<uint16_t>(seenLo), static_cast<uint16_t>(seenHi));
        const uint32_t filler = context.RoutineEntry(f.key);
        const auto known = [&](PhysKey k) {
            if (k == kNoKey || k == f.key)
                return true;
            const uint32_t entry = context.RoutineEntry(k);
            return (entry != 0xFFFFFFFFu && entry == filler) ||
                   std::find(users.begin(), users.end(), entry) != users.end();
        };
        for (uint32_t a = c.lo; a <= c.hi; ++a)
        {
            if (a >= seenLo && a <= seenHi)
                continue;
            const ByteUse& u = context.Use(static_cast<uint16_t>(a));
            if (!known(u.firstReader) || !known(u.lastReader) || !known(u.firstWriter) || !known(u.lastWriter))
                return {};
        }
        return c;
    };
    const auto claim = [&](const Completion& c, uint32_t stride, uint32_t seenRows, std::vector<Field> fields,
                           std::vector<Evidence> evidence) {
        const auto lo = static_cast<uint16_t>(c.lo);
        const auto hi = static_cast<uint16_t>(c.hi);
        if (!done.emplace(lo, hi, stride).second)
            return;
        const uint32_t count = (c.hi - c.lo + 1) / stride;
        FieldValues(context, fields, stride, lo, count);
        Region r;
        r.kind = RegionKind::RecordArray;
        r.detector = DetectorId::Records;
        r.status = seenRows >= count ? RegionStatus::Observed : RegionStatus::Inferred;
        r.geometry.stride = static_cast<uint16_t>(stride);
        r.geometry.count = static_cast<uint16_t>(count);
        r.geometry.fields = std::move(fields);
        r.confidence = confidence::FromCount(600, 850, seenRows, 2);
        r.evidence = std::move(evidence);
        r.evidence.push_back(Evidence{"fill", c.fill->z80, count > seenRows ? count - seenRows : 0,
                                      "one value over " + HexWord(c.fill->lo) + "-" + HexWord(c.fill->hi) + ", " +
                                          std::to_string((c.fill->hi - c.fill->lo + 1u) / stride) +
                                          " records on the lattice: the rows no access reached have the same "
                                          "fields (inferred)"});
        r.users = context.UsersOf(lo, hi);
        context.Claim(std::move(r), lo, hi);
    };

    // Index: the access's own base, an immediate the access read through first; one value only
    std::map<uint16_t, std::set<uint16_t>> ownBases;  // access Z80 -> nn
    for (const PointerUse& p : context.l1.pointers)
        if (p.form == PointerForm::Immediate && p.minDelta >= 0 && !p.code)
            ownBases[p.useZ80].insert(p.value);
    std::map<uint16_t, uint32_t> copyLength;  // copier -> the shortest forward copy it made
    for (const BlockCopy& c : context.l1.copies)
        if (c.dir > 0)
        {
            const auto [it, fresh] = copyLength.emplace(c.z80, c.length);
            if (!fresh)
                it->second = std::min(it->second, c.length);
        }
    const std::vector<IndexUse>& indexes = context.l1.indexes;
    for (size_t i = 0; i < indexes.size();)
    {
        size_t j = i;
        while (j < indexes.size() && indexes[j].selector == indexes[i].selector)
            ++j;
        const auto own = [&](const IndexUse& x) -> int32_t {
            const auto it = ownBases.find(x.z80);
            if (it == ownBases.end() || it->second.size() != 1 || *it->second.begin() > x.lo)
                return -1;
            return *it->second.begin();
        };
        std::set<uint16_t> bases;
        for (size_t k = i; k < j; ++k)
            if (own(indexes[k]) >= 0)
                bases.insert(static_cast<uint16_t>(own(indexes[k])));
        uint32_t covered = 0;  // the bases below: fields of the table claimed from a lower one
        for (const uint16_t base : bases)
        {
            if (base < covered)
                continue;
            uint32_t g = 0, ownHi = base;
            for (size_t k = i; k < j; ++k)
            {
                const int32_t b = own(indexes[k]);
                if (b >= base && static_cast<uint32_t>(b) < base + kMaxStride)
                {
                    g = std::gcd(g, static_cast<uint32_t>(indexes[k].hi - indexes[k].lo));
                    ownHi = std::max<uint32_t>(ownHi, indexes[k].hi);
                }
            }
            if (g == 0)
                continue;  // one row seen: no step
            Completion best;
            uint32_t bestStride = 0, bestRows = 0;
            std::set<int16_t> bestOffsets;
            std::map<int16_t, uint16_t> bestSizes;
            for (const ConstantFill& f : fills)
            {
                if ((f.lo >> 14) != (base >> 14))
                    continue;
                const uint32_t distance = f.lo >= base ? f.lo - base : base - f.lo;
                const uint32_t stride = std::gcd(std::gcd(g, distance), static_cast<uint32_t>(f.hi - f.lo + 1u));
                if (stride < 2 || stride > kMaxStride)
                    continue;
                // The fields: each access at its offset; an access with a base of its own must fit, another one
                // reading near the table joins when it does
                std::set<int16_t> offsets;
                std::map<int16_t, uint16_t> sizes;
                uint32_t top = base;
                bool fits = true;
                for (size_t k = i; k < j && fits; ++k)
                {
                    const IndexUse& x = indexes[k];
                    const int32_t b = own(x);
                    const bool mine = b >= base && static_cast<uint32_t>(b) < base + kMaxStride;
                    if (!mine && (b >= 0 || x.lo < base || x.lo > ownHi + kMaxStride))
                        continue;
                    const uint32_t offset = (x.lo - base) % stride;
                    const auto copied = copyLength.find(x.z80);
                    const uint32_t size = copied != copyLength.end() ? copied->second : 1;
                    const bool ok = (x.hi - x.lo) % stride == 0 && offset + size <= stride &&
                                    (!mine || static_cast<uint32_t>(b) - base < stride);
                    if (!ok)
                    {
                        fits = !mine;
                        continue;
                    }
                    offsets.insert(static_cast<int16_t>(offset));
                    auto& s = sizes[static_cast<int16_t>(offset)];
                    s = static_cast<uint16_t>(std::max<uint32_t>(s, size));
                    top = std::max<uint32_t>(top, x.hi + size - 1);
                }
                if (!fits || offsets.empty())
                    continue;
                const uint32_t rows = (top - base) / stride + 1;
                const Completion c = complete(f, base, stride, base, base + rows * stride - 1);
                if (c.fill != nullptr && (best.fill == nullptr || c.hi - c.lo > best.hi - best.lo))
                {
                    best = c;
                    bestStride = stride;
                    bestRows = rows;
                    bestOffsets = std::move(offsets);
                    bestSizes = std::move(sizes);
                }
            }
            if (best.fill == nullptr)
                continue;
            covered = base + bestStride;
            const uint32_t count = (best.hi - best.lo + 1) / bestStride;
            std::vector<Field> fields =
                FieldsOf(context, bestOffsets, {}, bestStride, static_cast<uint16_t>(best.lo), count, bestSizes);
            claim(best, bestStride, bestRows, std::move(fields),
                  {Evidence{"index", indexes[i].selectorZ80, bestRows,
                            HexWord(base) + " + " + std::to_string(bestStride) + " x the byte at " +
                                HexWord(indexes[i].selectorZ80) + ": " + std::to_string(bestOffsets.size()) +
                                " fields read through it, rows up to " + std::to_string(bestRows - 1)}});
        }
        i = j;
    }

    // A record claim: its stride and fields
    std::vector<Region> arrays;
    for (const Region& r : context.out.store.Regions())
        if (r.kind == RegionKind::RecordArray && r.detector == DetectorId::Records && r.geometry.count >= 2 &&
            r.geometry.stride >= 2 && r.geometry.stride <= kMaxStride && r.geometry.instances.empty())
            arrays.push_back(r);
    for (const Region& r : arrays)
    {
        const uint32_t stride = r.geometry.stride;
        Completion best;
        for (const ConstantFill& f : fills)
            if ((f.lo >> 14) == (r.z80 >> 14))
                if (const Completion c = complete(f, r.z80, stride, r.z80, r.Z80End());
                    c.fill != nullptr && (best.fill == nullptr || c.hi - c.lo > best.hi - best.lo))
                    best = c;
        if (best.fill == nullptr)
            continue;
        std::vector<Field> fields = r.geometry.fields;
        claim(best, stride, r.geometry.count, std::move(fields),
              {Evidence{"records", r.z80, r.geometry.count,
                        std::to_string(r.geometry.count) + " records of " + std::to_string(stride) + " bytes at " +
                            HexWord(r.z80) + " the code used"}});
    }
}

/// Records of one type walked by one routine: two or more immediates (LD rr,nn) handed in one register pair to one
/// access that walks each of them one byte at a time, over the same extent, until a compare with one constant (the
/// value that access loaded) stops it; each instance holds that constant at the walk's far end and nowhere else. Each
/// instance is one record of that size, the sentinel its first field (a walk down) or its last (a walk up), the
/// others its instances. Not over text (strings handed to a print routine are text claims)
void SentinelRecords(DetectorContext& context)
{
    constexpr int32_t kMaxExtent = 32;
    std::map<std::pair<uint16_t, uint8_t>, std::vector<const PointerUse*>> byUse;  // access, pair
    for (const PointerUse& p : context.l1.pointers)
        if (p.form == PointerForm::Immediate && !p.code)
            byUse[{p.useZ80, p.pair}].push_back(&p);
    for (const auto& [key, ps] : byUse)
    {
        const uint16_t access = key.first;
        std::set<uint16_t> values;
        bool same = true;
        for (const PointerUse* p : ps)
        {
            values.insert(p->value);
            same = same && p->minDelta == ps.front()->minDelta && p->maxDelta == ps.front()->maxDelta;
        }
        const int32_t minDelta = ps.front()->minDelta;
        const int32_t maxDelta = ps.front()->maxDelta;
        const int32_t extent = maxDelta - minDelta + 1;
        if (!same || values.size() < 2 || extent < 2 || extent > kMaxExtent)
            continue;
        // The walk: the access reads a byte at a time from the immediate on
        int32_t direction = 0;
        for (const Loop& loop : context.l1.loops)
            for (const Stride& s : loop.strides)
                if (s.z80 == access && !s.write && s.constant && std::abs(s.stride) == 1)
                    direction = s.stride;
        if ((direction < 0 && maxDelta != 0) || (direction > 0 && minDelta != 0) || direction == 0)
            continue;
        // The sentinel: a compare with one constant of what the access loaded
        std::set<uint8_t> constants;
        uint16_t compare = 0;
        for (const ConstantCompare& cc : context.l1.constantCompares)
            if (cc.loaderZ80 == access && cc.source == SourceKind::Memory)
            {
                constants.insert(cc.constant);
                compare = cc.z80;
            }
        if (constants.size() != 1)
            continue;
        const uint8_t sentinel = *constants.begin();
        const uint32_t at = direction < 0 ? 0 : static_cast<uint32_t>(extent - 1);
        std::vector<uint16_t> starts;
        bool ok = true;
        for (const uint16_t v : values)
        {
            const int32_t lo = static_cast<int32_t>(v) + minDelta;
            const int32_t hi = static_cast<int32_t>(v) + maxDelta;
            if (lo < 0 || hi > 0xFFFF || (lo >> 14) != (hi >> 14))
            {
                ok = false;
                break;
            }
            for (int32_t a = lo; a <= hi && ok; ++a)
            {
                const auto z80 = static_cast<uint16_t>(a);
                const bool end = static_cast<uint32_t>(a - lo) == at;
                ok = context.InScope(z80) && !DetectorContext::Code(context.Use(z80)) &&
                     (context.Start(z80) == sentinel) == end &&
                     !context.out.store.ClaimedAs(context.Key(z80), RegionKind::Text);
            }
            starts.push_back(static_cast<uint16_t>(lo));
        }
        if (!ok)
            continue;
        for (const uint16_t lo : starts)
        {
            Region r;
            r.kind = RegionKind::RecordArray;
            r.detector = DetectorId::Records;
            r.status = RegionStatus::Observed;
            r.geometry.stride = static_cast<uint16_t>(extent);
            r.geometry.count = 1;
            char hex[4];
            std::snprintf(hex, sizeof(hex), "%02X", sentinel);
            r.geometry.terminator = std::string("$") + hex;
            Field end{static_cast<uint16_t>(at), 1, "terminator", sentinel, sentinel, false};
            Field walked{static_cast<uint16_t>(direction < 0 ? 1 : 0), static_cast<uint16_t>(extent - 1), "walked", 0,
                         0, false};
            r.geometry.fields = direction < 0 ? std::vector<Field>{end, walked} : std::vector<Field>{walked, end};
            FieldValues(context, r.geometry.fields, static_cast<uint32_t>(extent), lo, 1);
            for (const uint16_t other : starts)
                if (other != lo)
                    r.geometry.instances.push_back(other);
            r.confidence = confidence::FromCount(600, 800, starts.size(), 2);
            r.evidence.push_back(Evidence{"walk", access, starts.size(),
                                          std::to_string(starts.size()) + " bases handed to one walk of " +
                                              std::to_string(extent) + " bytes, " +
                                              (direction < 0 ? "down" : "up") + " to the compare with $" + hex});
            r.evidence.push_back(Evidence{"terminator", compare, 0, "the walk's end test"});
            r.users = context.UsersOf(lo, static_cast<uint16_t>(lo + extent - 1));
            context.Claim(std::move(r), lo, static_cast<uint16_t>(lo + extent - 1));
        }
    }
}

}  // namespace

void DetectRecords(DetectorContext& context)
{
    const L1Result& l1 = context.l1;
    std::vector<BufferInfo> buffers = Buffers(context);
    const std::vector<uint8_t> claimed = context.ClaimedMap();

    // Buffers first: the record arrays inside them take their instances
    for (BufferInfo& b : buffers)
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
        const size_t observed = b.sources.size();
        const std::vector<uint16_t> inferred = LatticeSources(context, b, claimed);
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
        if (!inferred.empty())
        {
            r.evidence.push_back(Evidence{"lattice", inferred.front(), inferred.size(),
                                          std::to_string(inferred.size()) +
                                              " more sources on the sources' step, the buffer's text printable "
                                              "there too (inferred)"});
            r.geometry.instances.insert(r.geometry.instances.end(), inferred.begin(), inferred.end());
            std::sort(r.geometry.instances.begin(), r.geometry.instances.end());
            r.geometry.count = static_cast<uint16_t>(r.geometry.instances.size());
            b.sources.insert(inferred.begin(), inferred.end());
        }
        r.users = context.UsersOf(b.dst, static_cast<uint16_t>(end));
        // The buffer's text in each inferred source
        std::vector<Region> texts;
        for (const Region* t : context.out.store.Overlapping(context.Key(b.dst), context.Key(static_cast<uint16_t>(end))))
            if (t->kind == RegionKind::Text && t->z80 >= b.dst && t->Z80End() <= end && t->geometry.width != 0)
                texts.push_back(*t);
        context.Claim(std::move(r), b.dst, static_cast<uint16_t>(end));
        for (const uint16_t slot : inferred)
            for (const Region& t : texts)
            {
                Region copy = t;
                const auto at = static_cast<uint16_t>(slot + (t.z80 - b.dst));
                copy.status = RegionStatus::Inferred;
                copy.confidence = 700;
                copy.users.clear();
                copy.geometry.text.clear();
                for (uint32_t i = 0; i < t.geometry.width; ++i)
                    copy.geometry.text += static_cast<char>(context.Start(static_cast<uint16_t>(at + i)) & 0x7F);
                copy.evidence = {Evidence{"lattice", b.dst, observed,
                                          "a source of the buffer on its sources' step: the buffer's text there"}};
                context.Claim(std::move(copy), at, static_cast<uint16_t>(at + t.geometry.width - 1));
            }
    }

    const auto bufferOf = [&](uint16_t lo, uint16_t hi) -> const BufferInfo* {
        for (const BufferInfo& b : buffers)
            if (lo >= b.dst && hi <= b.dst + b.length - 1 && !b.sources.empty())
                return &b;
        return nullptr;
    };

    std::vector<Candidate> candidates;
    for (const BaseOffsetPattern& p : l1.patterns)
    {
        if (p.bases.size() < 2)
            continue;
        // The base-step check: every base on one lattice of L1's step (the bases' period). An HL walk may also
        // restart inside a record (bases at 16k and 16k + 2): its bases fold onto the record starts, the residue
        // added to its offsets, when the walk is plain: a few restart places (kMaxRestarts), each seen at two records
        // at least, filling at most half of their lattice's slots, and the records back to back (scattered bases fold
        // onto anything). Else the most common gap between neighbours, every base on its lattice (lists of words with
        // holes: a period of 8, a step of 2)
        constexpr size_t kMaxRestarts = 4;
        const uint16_t first = p.bases.front();
        const bool walk = p.offsetsAtBase.empty();
        std::vector<uint16_t> starts;
        std::map<uint32_t, uint32_t> residues;
        const auto fold = [&](uint32_t by) {
            starts.clear();
            residues.clear();
            for (const uint16_t base : p.bases)
            {
                const uint32_t residue = static_cast<uint16_t>(base - first) % by;
                ++residues[residue];
                const auto start = static_cast<uint16_t>(base - residue);
                if (starts.empty() || starts.back() != start)
                    starts.push_back(start);
            }
            if (residues.size() == 1)
                return true;
            if (!walk || residues.size() > kMaxRestarts)
                return false;
            uint32_t spacing = by;
            for (const auto& [residue, times] : residues)
            {
                if (times < 2)
                    return false;
                spacing = std::gcd(spacing, residue);
            }
            // Restarts fill at most half the slots of their own lattice in a record: more is a list of smaller
            // records with holes
            if (residues.size() * spacing * 2 > by)
                return false;
            for (size_t i = 1; i < starts.size(); ++i)
                if (static_cast<uint16_t>(starts[i] - starts[i - 1]) != by)
                    return false;
            return true;
        };
        uint32_t step = static_cast<uint32_t>(std::abs(p.step));
        if (step < 2 || !fold(step))
        {
            std::map<uint32_t, uint32_t> gaps;
            for (size_t i = 1; i < p.bases.size(); ++i)
                ++gaps[static_cast<uint16_t>(p.bases[i] - p.bases[i - 1])];
            uint32_t best = 0;
            step = 0;
            for (const auto& [gap, times] : gaps)
                if (times > best)
                {
                    best = times;
                    step = gap;
                }
            if (step < 2 || !fold(step) || residues.size() != 1)
                continue;
        }
        // Offsets inside one record
        std::set<int16_t> offsetSet;
        std::set<int16_t> words;
        for (const auto& [residue, times] : residues)
        {
            for (const int16_t d : p.offsets)
                if (d >= 0 && residue + d < step)
                    offsetSet.insert(static_cast<int16_t>(residue + d));
            for (const int16_t d : p.wordOffsets)
                if (d >= 0 && residue + d < step)
                    words.insert(static_cast<int16_t>(residue + d));
        }
        const std::vector<int16_t> offsets(offsetSet.begin(), offsetSet.end());
        if (offsets.empty())
            continue;

        // A terminator test: the last base read at one offset only, where the others were read at more. The byte
        // tested is the last base as the code held it: a walk whose last base was a restart tested the byte at the
        // restart's place in the record, not the record's start
        const uint16_t end = p.bases.back();
        uint16_t last = starts.back();
        bool terminated = false;
        if (walk && starts.size() >= 2)
        {
            // An HL walk (no per-base offsets): the last base's offsets past the tested byte had no register access,
            // the first record's did
            const auto accessed = [&](uint16_t at) {
                return (context.Use(at).flags & (use::kIndexed | use::kAbsolute)) != 0;
            };
            const auto tested = static_cast<int32_t>(static_cast<uint16_t>(end - starts.back()));
            bool rest = false, othersRest = false;
            for (const int16_t d : offsets)
                if (d > tested)
                {
                    rest |= accessed(static_cast<uint16_t>(starts.back() + d));
                    othersRest |= accessed(static_cast<uint16_t>(starts.front() + d));
                }
            if (accessed(end) && !rest && othersRest)
            {
                terminated = true;
                last = starts[starts.size() - 2];
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
        // Density: the records the bases reached, at least a quarter of the array's (a few bases on a step of 2 and one
        // far above them make no 2000-record array); small arrays are left alone
        constexpr uint32_t kMinDensity = 4;
        constexpr uint32_t kFreeCount = 16;
        const uint64_t reached = std::max<uint64_t>(starts.size(), p.basesSeen);
        if (count > kFreeCount && count > kMinDensity * reached)
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

        Candidate c;
        c.first = first;
        c.step = step;
        c.count = count;
        c.offsets = offsetSet;
        c.words = words;
        c.terminated = terminated;
        c.terminator = end;
        c.patterns.push_back(&p);
        candidates.push_back(std::move(c));
    }

    // One array seen from two places: a routine reads a record from its start, another from a byte on (handed the
    // record's address + 1). The two claims have the same step, starts less than a step apart, and the later one's
    // accesses fit in the earlier one's records shifted by the distance: one array, the offsets shifted; its last
    // record does not run past the earlier one's end
    // Stable: candidates of one start and step keep L1's pattern order (the merge below takes the first one)
    std::stable_sort(candidates.begin(), candidates.end(), [](const Candidate& a, const Candidate& b) {
        return std::tie(a.first, a.step) < std::tie(b.first, b.step);
    });
    for (size_t i = 0; i < candidates.size(); ++i)
    {
        Candidate& a = candidates[i];
        if (a.merged)
            continue;
        for (size_t k = i + 1; k < candidates.size(); ++k)
        {
            Candidate& b = candidates[k];
            const uint32_t d = static_cast<uint32_t>(b.first - a.first);
            if (d >= a.step)
                break;
            if (b.merged || b.step != a.step)
                continue;
            int32_t top = *b.offsets.rbegin() + (b.words.count(*b.offsets.rbegin()) != 0 ? 1 : 0);
            for (const int16_t w : b.words)
                top = std::max<int32_t>(top, w + 1);
            if (top < 0 || d + static_cast<uint32_t>(top) >= a.step)
                continue;
            // A terminator ends one of them where the other reads on: two arrays after all
            if ((a.terminated && b.count > a.count) || (b.terminated && a.count > b.count))
                continue;
            for (const int16_t o : b.offsets)
                a.offsets.insert(static_cast<int16_t>(o + d));
            for (const int16_t w : b.words)
                a.words.insert(static_cast<int16_t>(w + d));
            a.count = std::max(a.count, b.count);
            a.patterns.insert(a.patterns.end(), b.patterns.begin(), b.patterns.end());
            b.merged = true;
        }
    }

    for (const Candidate& c : candidates)
    {
        if (c.merged)
            continue;
        const uint16_t first = c.first;
        const uint32_t step = c.step;
        const uint32_t count = c.count;
        const uint32_t hi = c.Hi();
        if (hi > 0xFFFF || (first >> 14) != (hi >> 14))
            continue;
        const BaseOffsetPattern& p = *c.patterns.front();
        Region r;
        r.kind = RegionKind::RecordArray;
        r.detector = DetectorId::Records;
        r.status = RegionStatus::Observed;
        r.geometry.stride = static_cast<uint16_t>(step);
        r.geometry.count = static_cast<uint16_t>(count);
        r.geometry.fields = FieldsOf(context, c.offsets, c.words, step, first, count);
        if (c.terminated)
            r.geometry.terminator = "$" + [&] {
                char hex[4];
                std::snprintf(hex, sizeof(hex), "%02X", context.Start(c.terminator));
                return std::string(hex);
            }();
        if (const BufferInfo* b = bufferOf(first, static_cast<uint16_t>(hi)))
            for (const uint16_t src : b->sources)
                if (src != b->dst)
                    r.geometry.instances.push_back(static_cast<uint16_t>(src + (first - b->dst)));
        uint64_t seen = 0;
        for (const BaseOffsetPattern* q : c.patterns)
            seen = std::max<uint64_t>(seen, q->basesSeen);
        r.confidence = confidence::FromCount(500, 950, seen, 3);
        for (const BaseOffsetPattern* q : c.patterns)
        {
            r.evidence.push_back(Evidence{"pattern", l1.routines[q->routine].z80, q->basesSeen,
                                          std::string(BaseRegName(q->base)) + " step " + std::to_string(step) + ", " +
                                              std::to_string(q->offsets.size()) + " offsets" +
                                              (q->bases.front() != first
                                                   ? ", from byte " + std::to_string(
                                                                          static_cast<uint16_t>(q->bases.front() - first) % step) +
                                                         " of each record"
                                                   : std::string())});
            r.users.push_back(l1.routines[q->routine].z80);
        }
        std::sort(r.users.begin(), r.users.end());
        r.users.erase(std::unique(r.users.begin(), r.users.end()), r.users.end());
        context.Claim(r, first, static_cast<uint16_t>(hi));

        if (c.terminated && context.InScope(c.terminator))
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
            context.Claim(std::move(t), c.terminator, c.terminator);
        }
    }

    GatheredRecords(context);
    RecordTemplates(context);
    IndexedRecords(context);
    ArraysCompletedByFills(context);
    SentinelRecords(context);
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
