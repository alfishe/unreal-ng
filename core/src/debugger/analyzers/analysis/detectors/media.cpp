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

    // Copies by source, destination and length (the lowest byte of each range)
    std::map<std::tuple<uint16_t, uint16_t, uint32_t>, DisplayCopy> byKey;
    for (const BlockCopy& c : context.l1.copies)
    {
        if (c.length < kMinDisplayCopy || c.length > 0x4000)
            continue;
        const uint16_t src = c.dir > 0 ? c.src : static_cast<uint16_t>(c.src - (c.length - 1));
        const uint16_t dst = c.dir > 0 ? c.dst : static_cast<uint16_t>(c.dst - (c.length - 1));
        if (static_cast<uint32_t>(src) + c.length - 1 > 0xFFFF || static_cast<uint32_t>(dst) + c.length - 1 > 0xFFFF)
            continue;
        // A fill (the source overlaps the destination) or a scroll inside the screen moves no picture
        if (src < dst + c.length && dst < src + c.length)
            continue;
        if (src >= 0x4000 && static_cast<uint32_t>(src) + c.length - 1 < 0x5B00)
            continue;
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
            c.toScreen = c.dst >= 0x4000 && c.dst + c.length - 1 < 0x5B00;
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
