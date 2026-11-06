/// The display set and the blit loops (media.h), shared by the graphics, level map and screen detectors.

#include "debugger/analyzers/analysis/detectors/media.h"

#include <algorithm>
#include <cstdlib>
#include <map>
#include <set>
#include <tuple>

namespace analysis::media
{

namespace
{

constexpr uint32_t kMinDisplayCopy = 32;
constexpr uint32_t kMinRowCopy = 8;         ///< a row of a back buffer one short copy (an LDI chain a row)
constexpr uint32_t kMinCopierBytes = 512;   ///< what the copier of short rows moved in all
constexpr int kMaxChain = 8;

bool WrittenByCode(const ByteUse& u)
{
    return (u.flags & (use::kOtherWrite | use::kIncDec)) != 0;
}

/// A loop around @p inner: same routine, its header before inner's and its back edge after inner's
bool Encloses(const Loop& outer, const Loop& inner)
{
    return &outer != &inner && outer.routine == inner.routine && outer.header < inner.header &&
           outer.latch > inner.latch;
}

bool Subset(const std::vector<uint16_t>& a, const std::vector<uint16_t>& b)
{
    return a.size() <= b.size() && std::includes(b.begin(), b.end(), a.begin(), a.end());
}

/// Strides of one group with the same starts (alternative paths reading or writing the same byte) count once
uint32_t DistinctOffsets(const std::vector<const Stride*>& strides)
{
    uint32_t n = 0;
    for (size_t i = 0; i < strides.size(); ++i)
    {
        bool alternative = false;
        for (size_t j = 0; j < strides.size() && !alternative; ++j)
        {
            if (i == j || strides[i]->starts.empty())
                continue;
            // i repeats j: its starts are a subset of j's (the larger, or the earlier of two equal lists)
            if (Subset(strides[i]->starts, strides[j]->starts) &&
                (strides[i]->starts.size() < strides[j]->starts.size() || j < i))
                alternative = true;
        }
        n += alternative ? 0 : 1;
    }
    return n;
}

}  // namespace

std::vector<BufferCopy> Buffers(const DetectorContext& context)
{
    std::vector<BufferCopy> out;
    for (const Region& r : context.out.store.Regions())
        if (r.kind == RegionKind::Buffer)
            out.push_back(BufferCopy{r.z80, r.Z80End(), r.geometry.instances});
    return out;
}

uint32_t RoutineOfZ80(const DetectorContext& context, uint16_t z80)
{
    const Routine* r = context.l1.RoutineOf(context.Key(z80));
    return r != nullptr ? r->z80 : 0xFFFFFFFFu;
}

std::vector<std::pair<uint16_t, uint16_t>> Runs(const DetectorContext& context, const std::vector<Interval>& ranges,
                                                uint32_t slack)
{
    std::vector<std::pair<uint16_t, uint16_t>> runs;
    for (const Interval& iv : ranges)
    {
        uint16_t z80 = 0;
        if (!context.Z80Of(iv.lo, z80))
            z80 = iv.z80;
        const uint32_t hi = static_cast<uint32_t>(z80) + (iv.hi - iv.lo);
        if (hi > 0xFFFF)
            continue;
        runs.emplace_back(z80, static_cast<uint16_t>(hi));
    }
    std::sort(runs.begin(), runs.end());
    std::vector<std::pair<uint16_t, uint16_t>> merged;
    for (const auto& r : runs)
    {
        if (!merged.empty() && static_cast<uint32_t>(r.first) <= static_cast<uint32_t>(merged.back().second) + 1 + slack)
            merged.back().second = std::max(merged.back().second, r.second);
        else
            merged.push_back(r);
    }
    return merged;
}

DisplayInfo FindDisplay(const DetectorContext& context)
{
    DisplayInfo d;
    d.map.assign(0x10000, kNotDisplay);
    for (uint32_t a = 0x4000; a < 0x5800; ++a)
        d.map[a] = kPixels;
    for (uint32_t a = 0x5800; a < 0x5B00; ++a)
        d.map[a] = kAttributes;

    // Copies by source, destination and length (the lowest byte of each range). A short copy counts when its copier
    // moved enough in all and it goes to the screen: a back buffer copied a row at a time (POP the row's display
    // address, an LDI chain)
    const auto lowest = [](const BlockCopy& c, uint16_t first) {
        return c.dir > 0 ? first : static_cast<uint16_t>(first - (c.length - 1));
    };
    const auto moves = [&](const BlockCopy& c) {
        const uint16_t src = lowest(c, c.src), dst = lowest(c, c.dst);
        if (c.length < kMinRowCopy || c.length > 0x4000 || static_cast<uint32_t>(src) + c.length - 1 > 0xFFFF ||
            static_cast<uint32_t>(dst) + c.length - 1 > 0xFFFF)
            return false;
        // A fill (the source overlaps the destination) or a scroll inside the screen moves no picture
        return !(src < dst + c.length && dst < src + c.length) &&
               !(src >= 0x4000 && static_cast<uint32_t>(src) + c.length - 1 < 0x5B00);
    };
    std::map<PhysKey, uint64_t> copierBytes;
    for (const BlockCopy& c : context.l1.copies)
        if (moves(c))
            copierBytes[c.instruction] += c.length;
    std::map<std::tuple<uint16_t, uint16_t, uint32_t>, DisplayCopy> byKey;
    for (const BlockCopy& c : context.l1.copies)
    {
        if (!moves(c) || (c.length < kMinDisplayCopy && copierBytes[c.instruction] < kMinCopierBytes))
            continue;
        const uint16_t src = lowest(c, c.src), dst = lowest(c, c.dst);
        DisplayCopy& copy = byKey[{src, dst, c.length}];
        copy.src = src;
        copy.dst = dst;
        copy.length = c.length;
        if (std::find(copy.copiers.begin(), copy.copiers.end(), c.z80) == copy.copiers.end())
            copy.copiers.push_back(c.z80);
        copy.runs += c.count;
    }
    std::vector<DisplayCopy> all;
    for (auto& [key, copy] : byKey)
    {
        std::sort(copy.copiers.begin(), copy.copiers.end());
        all.push_back(std::move(copy));
    }

    // Copies into the display set, back through chains of back buffers (sources the code also wrote)
    std::vector<uint8_t> taken(all.size(), 0);
    for (int round = 0; round < kMaxChain; ++round)
    {
        bool changed = false;
        for (size_t i = 0; i < all.size(); ++i)
        {
            if (taken[i])
                continue;
            DisplayCopy& c = all[i];
            // A short copy is a row only into the screen itself: one into a back buffer is a copy routine's chunk
            const bool screen = c.dst >= 0x4000 && c.dst + c.length - 1 < 0x5B00;
            if (c.length < kMinDisplayCopy && !screen)
                continue;
            uint32_t pixels = 0, attributes = 0;
            for (uint32_t k = 0; k < c.length; ++k)
            {
                const uint8_t t = d.map[c.dst + k];
                pixels += t == kPixels ? 1 : 0;
                attributes += t == kAttributes ? 1 : 0;
            }
            if ((pixels + attributes) * 10 < c.length * 9)
                continue;
            taken[i] = 1;
            changed = true;
            c.type = pixels >= attributes ? kPixels : kAttributes;
            c.toScreen = screen;
            for (uint32_t k = 0; k < c.length && !c.written; ++k)
                c.written = WrittenByCode(context.Use(static_cast<uint16_t>(c.src + k)));
            if (c.written)
                for (uint32_t k = 0; k < c.length; ++k)
                    if (d.map[c.src + k] == kNotDisplay)
                        d.map[c.src + k] = d.map[c.dst + k];
            d.copies.push_back(c);
        }
        if (!changed)
            break;
    }
    std::sort(d.copies.begin(), d.copies.end(), [](const DisplayCopy& a, const DisplayCopy& b) {
        return std::tie(a.src, a.dst, a.length) < std::tie(b.src, b.dst, b.length);
    });
    return d;
}

namespace
{

/// Per Z80 address of [@p lo, @p hi] (the start's paging; index 0 is @p lo): 1 when the byte's value was drawn to the
/// screen's pixels (L1 drawn), or a block copy brought it from a byte whose value was
std::vector<uint8_t> ReachesScreen(const DetectorContext& context, uint32_t lo, uint32_t hi)
{
    const std::vector<DrawnByte>& drawn = context.l1.drawn;
    const auto isDrawn = [&](PhysKey key) {
        const auto it = std::lower_bound(drawn.begin(), drawn.end(), key,
                                         [](const DrawnByte& d, PhysKey k) { return d.key < k; });
        return it != drawn.end() && it->key == key;
    };
    std::vector<uint8_t> out(hi - lo + 1, 0);
    for (uint32_t z80 = lo; z80 <= hi; ++z80)
        out[z80 - lo] = isDrawn(context.Key(static_cast<uint16_t>(z80))) ? 1 : 0;
    for (const BlockCopy& c : context.l1.copies)
    {
        // The copy's destination bytes (LDD runs walk down from dst), clipped to [lo, hi]
        const int64_t first = c.dir < 0 ? static_cast<int64_t>(c.dst) - c.length + 1 : c.dst;
        const int64_t last = first + c.length - 1;
        if (c.length == 0 || last < lo || first > hi)
            continue;
        for (int64_t dst = std::max<int64_t>(first, lo); dst <= std::min<int64_t>(last, hi); ++dst)
        {
            const int64_t offset = static_cast<int64_t>(KeyOffset(c.srcKey)) + (dst - c.dst);
            if (out[static_cast<size_t>(dst - lo)] == 0 && offset >= 0 && offset <= 0x3FFF &&
                isDrawn(MakeKey(KeyPage(c.srcKey), static_cast<uint16_t>(offset))))
                out[static_cast<size_t>(dst - lo)] = 1;
        }
    }
    return out;
}

/// The byte at @p key was an index: its value picked an address another instruction read (L1 indexes)
bool IsSelector(const L1Result& l1, PhysKey key)
{
    const auto it = std::lower_bound(l1.indexes.begin(), l1.indexes.end(), key,
                                     [](const IndexUse& x, PhysKey k) { return x.selector < k; });
    return it != l1.indexes.end() && it->selector == key;
}

}  // namespace

std::vector<DrawnBuffer> FindDrawnBuffers(const DetectorContext& context)
{
    constexpr uint32_t kMinDrawnBuffer = 2048;
    constexpr uint32_t kMaxDrawnGap = 32;
    constexpr uint32_t kMinRewrites = kDrawnMinRewrites;            // drawn into again and again, not unpacked once
    constexpr uint64_t kMaxReadsPerWrite = kDrawnMaxReadsPerWrite;  // a scrolled buffer redraws a strip a pass
    std::vector<DrawnBuffer> out;
    std::vector<uint32_t> readBy(0x10000, 0xFFFFFFFFu);  // the screen-writing routine that read the byte
    for (uint32_t i = 0; i < context.l1.routines.size(); ++i)
    {
        const Routine& routine = context.l1.routines[i];
        bool screen = false;
        for (const Interval& w : routine.profile.writes)
            screen = screen || (w.z80 <= 0x57FF && w.z80 + (w.hi - w.lo) >= 0x4000);
        if (!screen)
            continue;
        for (const Interval& rd : routine.profile.reads)
            for (uint32_t at = rd.z80; at <= rd.z80 + (rd.hi - rd.lo) && at <= 0xFFFF; ++at)
                readBy[at] = i;
    }
    // 1: read by a screen writer and rewritten; 2: read by one, not rewritten; 0: not read by one
    const auto classOf = [&](uint32_t z80) -> int {
        if (z80 < 0x5B00 || !context.InScope(static_cast<uint16_t>(z80)))
            return 2;
        if (readBy[z80] == 0xFFFFFFFFu)
            return 0;
        const ByteUse* u = context.l1.UseAt(context.Key(static_cast<uint16_t>(z80)));
        if (u == nullptr || u->writes < kMinRewrites)
            return 2;
        return static_cast<uint64_t>(u->writes) * kMaxReadsPerWrite >= u->reads ? 1 : 2;
    };
    uint32_t z80 = 0x5B00;
    while (z80 <= 0xFFFF)
    {
        if (classOf(z80) != 1)
        {
            ++z80;
            continue;
        }
        DrawnBuffer b;
        b.lo = static_cast<uint16_t>(z80);
        uint32_t hi = z80;
        uint32_t gap = 0;
        for (uint32_t at = z80; at <= 0xFFFF; ++at)
        {
            const int c = classOf(at);
            if (c == 2 || (c == 0 && ++gap > kMaxDrawnGap))
                break;
            if (c == 1)
            {
                hi = at;
                gap = 0;
                ++b.count;
            }
        }
        z80 = hi + 1;
        if (hi - b.lo + 1 < kMinDrawnBuffer)
            continue;
        b.hi = static_cast<uint16_t>(hi);
        // Its values reach the screen. A room map unpacked per room and read by the routine that draws the room only
        // selects the pictures: most of its rewritten bytes are indexes (L1 indexes: the value picked an address) and
        // few were drawn (L1 drawn) or block-copied from bytes that were. A buffer's bytes are drawn as their own
        // sources when the code computed them, as the sprite's or the tile's when it merged or copied one in: the
        // index test is what tells a map
        const std::vector<uint8_t> reaches = ReachesScreen(context, b.lo, hi);
        uint32_t reaching = 0;
        uint32_t selectors = 0;
        for (uint32_t at = b.lo; at <= hi; ++at)
            if (classOf(at) == 1)
            {
                reaching += reaches[at - b.lo] != 0 ? 1u : 0u;
                selectors += IsSelector(context.l1, context.Key(static_cast<uint16_t>(at))) ? 1u : 0u;
            }
        if (reaching * 2 <= b.count && selectors * 2 > b.count)
            continue;
        for (uint32_t at = b.lo; at <= hi; ++at)
            if (readBy[at] != 0xFFFFFFFFu)
                ++b.readers[readBy[at]];
        out.push_back(std::move(b));
    }
    return out;
}

Coverage Cover(const DetectorContext& context, const DisplayInfo& display, const Stride& s, uint8_t type)
{
    Coverage c;
    for (const auto& [lo, hi] : Runs(context, s.ranges, 0))
        for (uint32_t a = lo; a <= hi; ++a)
        {
            ++c.bytes;
            const uint8_t t = display.At(a);
            c.display += (type == 0 ? t != kNotDisplay : t == type) ? 1 : 0;
        }
    return c;
}

std::vector<Blit> FindBlits(const DetectorContext& context, const DisplayInfo& display)
{
    const L1Result& l1 = context.l1;
    std::vector<Blit> out;
    for (const Loop& loop : l1.loops)
    {
        if (loop.delay || loop.strides.empty() || loop.instances == 0)
            continue;
        std::vector<const Stride*> writes;
        bool horizontal = true;
        bool hasAnd = false;
        // Reads by base register (MemForm): the candidate source streams
        std::map<uint8_t, std::vector<const Stride*>> streams;
        std::set<uint8_t> excluded;
        for (const Stride& s : loop.strides)
        {
            const Instruction* ins = l1.InstructionAt(s.instruction);
            if (ins == nullptr)
                continue;
            const OpInfo& op = DecodeOp(ins->bytes);
            if (op.cls == OpClass::Block)
                continue;
            if (op.aluOp == 4 && op.memRead)
                hasAnd = true;
            if (s.write)
            {
                const Coverage c = Cover(context, display, s, kPixels);
                if (c.bytes != 0 && c.display * 10 >= c.bytes * 9)
                {
                    writes.push_back(&s);
                    horizontal = horizontal && s.constant && std::abs(s.stride) == 1;
                }
                continue;
            }
            if (op.mem != MemForm::HL && op.mem != MemForm::DE && op.mem != MemForm::BC && op.mem != MemForm::IX &&
                op.mem != MemForm::IY)
                continue;
            const Coverage c = Cover(context, display, s, 0);
            if (c.bytes == 0 || c.display * 10 >= c.bytes)
                continue;  // reads the display: what is under the sprite, not the sprite
            const uint8_t form = static_cast<uint8_t>(op.mem);
            // A stream that loads an address register is a table of addresses, not graphics; only loads into
            // A, B, C and the AND / XOR / OR of a graphic byte count
            const uint16_t addressRegs = reg::kD | reg::kE | reg::kH | reg::kL | reg::kIX | reg::kIY;
            const bool graphicUse = (op.cls == OpClass::Load && (op.loadDest & addressRegs) == 0 && op.loadDest != 0) ||
                                    (op.cls == OpClass::Alu && op.aluOp >= 4 && op.aluOp <= 6);
            if (!graphicUse)
                excluded.insert(form);
            streams[form].push_back(&s);
        }
        if (writes.empty() || horizontal)
            continue;

        // The source: the stream with the most reads (ties: the lower register form), one constant stride
        const std::vector<const Stride*>* source = nullptr;
        for (const auto& [form, list] : streams)
        {
            if (excluded.count(form) != 0)
                continue;
            if (source == nullptr || list.size() > source->size())
                source = &list;
        }
        if (source == nullptr)
            continue;
        const int32_t stride = source->front()->stride;
        bool uniform = stride > 0 && stride <= 16;
        for (const Stride* s : *source)
            uniform = uniform && s->constant && s->stride == stride;
        if (!uniform)
            continue;
        // Bytes the stream read are not a table the earlier detectors named
        const Stride& first = *source->front();
        if (!first.ranges.empty())
        {
            uint16_t z80 = 0;
            if (context.Z80Of(first.ranges.front().lo, z80))
            {
                bool table = false;
                for (const Region* r : context.out.store.ClaimsAt(context.Key(z80)))
                    table = table || r->kind == RegionKind::InitTable || r->kind == RegionKind::PointerTable ||
                            r->kind == RegionKind::HandlerTable || r->kind == RegionKind::Text;
                if (table)
                    continue;
            }
        }

        Blit b;
        b.loop = &loop;
        b.srcStride = stride;
        const uint32_t reads = DistinctOffsets(*source);
        const uint32_t writers = DistinctOffsets(writes);
        b.mask = hasAnd && reads == 2 * writers && static_cast<uint32_t>(stride) == reads;
        b.rowBytes = static_cast<uint16_t>(b.mask ? stride / 2 : stride);
        for (const auto& [trips, instances] : loop.trips)
            if (instances > b.dominant)
            {
                b.dominant = static_cast<uint32_t>(instances);
                b.height = static_cast<uint16_t>(std::min<uint32_t>(trips, 0xFFFF));
            }
        if (loop.trips.empty())
        {
            b.height = static_cast<uint16_t>(std::min<uint32_t>(loop.maxTrips, 0xFFFF));
            b.dominant = static_cast<uint32_t>(loop.instances);
        }
        b.instances = loop.instances;
        if (b.rowBytes == 0 || b.rowBytes > 8 || b.height < 2 || b.height > 64)
            continue;
        b.frameBytes = static_cast<uint32_t>(b.rowBytes) * b.height * (b.mask ? 2u : 1u);

        // Frame starts: every read's instance starts, clustered within one source stride (the bytes of one row)
        std::vector<uint16_t> starts;
        for (const Stride* s : *source)
        {
            starts.insert(starts.end(), s->starts.begin(), s->starts.end());
            b.startsMore = b.startsMore || s->startsSeen > s->starts.size();
            b.origins.insert(b.origins.end(), s->origins.begin(), s->origins.end());
            b.readers.push_back(s->z80);
        }
        std::sort(starts.begin(), starts.end());
        starts.erase(std::unique(starts.begin(), starts.end()), starts.end());
        for (const uint16_t s : starts)
            if (b.starts.empty() || static_cast<uint32_t>(s - b.starts.back()) >= static_cast<uint32_t>(stride))
                b.starts.push_back(s);
        for (const Stride* s : writes)
            b.writers.push_back(s->z80);
        std::sort(b.readers.begin(), b.readers.end());
        std::sort(b.writers.begin(), b.writers.end());
        b.toPixels = true;

        // Tiles: the nearest loop around it that walked memory one byte a pass (a map), outside display pixels
        const Loop* best = nullptr;
        const Stride* bestRead = nullptr;
        for (const Loop& outer : l1.loops)
        {
            if (!Encloses(outer, loop) || (best != nullptr && outer.header < best->header))
                continue;
            for (const Stride& s : outer.strides)
            {
                if (s.write || !s.constant || std::abs(s.stride) != 1)
                    continue;
                const Instruction* ins = l1.InstructionAt(s.instruction);
                if (ins == nullptr)
                    continue;
                const OpInfo& op = DecodeOp(ins->bytes);
                if (op.cls == OpClass::Block || op.mem == MemForm::Abs || op.mem == MemForm::Stack)
                    continue;
                const Coverage c = Cover(context, display, s, kPixels);
                if (c.bytes < 16 || c.display != 0)
                    continue;
                best = &outer;
                bestRead = &s;
                break;
            }
        }
        b.mapLoop = best;
        b.mapRead = bestRead;
        out.push_back(std::move(b));
    }
    return out;
}

}  // namespace analysis::media
