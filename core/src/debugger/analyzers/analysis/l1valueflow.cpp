#include "debugger/analyzers/analysis/l1valueflow.h"

#include <algorithm>
#include <utility>

namespace analysis
{

namespace
{

/// A block copy that reads a byte one of its last few steps wrote is a fill (LDIR with DE = HL + 1..4): it moves no
/// picture. One that moves bytes a step down (a scroll, DE = HL - 1) is a copy
constexpr int kFillDistance = 4;

constexpr int kSlots = 12;  ///< reg bits 0..11: A F B C D E H L IXH IXL IYH IYL (F unused)

struct Source
{
    PhysKey read = kNoKey;    ///< the byte the instruction read
    PhysKey origin = kNoKey;  ///< the byte the value was first read from
    uint64_t seq = 0;         ///< when it was read
};

/// A value's sources, the latest first
struct Value
{
    Source s[3];
    bool Empty() const { return s[0].read == kNoKey; }
};

/// Both operands' sources, the latest read first
Value Merge(const Value& a, const Value& b)
{
    Source all[6];
    int n = 0;
    for (const Value* v : {&a, &b})
        for (const Source& e : v->s)
            if (e.read != kNoKey &&
                std::none_of(all, all + n, [&](const Source& o) { return o.read == e.read && o.seq == e.seq; }))
                all[n++] = e;
    std::sort(all, all + n, [](const Source& x, const Source& y) { return x.seq > y.seq; });
    Value out;
    for (int i = 0; i < n && i < 3; ++i)
        out.s[i] = all[i];
    return out;
}

bool IsScreen(PhysKey key)
{
    const uint16_t page = KeyPage(key);
    return (page == 5 || page == 7) && KeyOffset(key) < 0x1B00;
}

bool IsPixels(PhysKey key)
{
    const uint16_t page = KeyPage(key);
    return (page == 5 || page == 7) && KeyOffset(key) < 0x1800;
}

int Bit(uint16_t mask)
{
    for (int b = 0; b < 16; ++b)
        if (mask == (1u << b))
            return b;
    return -1;
}

/// The pair's high and low register bits (BC DE HL IX IY AF), -1 when @p mask is no pair
std::pair<int, int> PairBits(uint16_t mask)
{
    switch (mask)
    {
        case reg::kBC:
            return {2, 3};
        case reg::kDE:
            return {4, 5};
        case reg::kHL:
            return {6, 7};
        case reg::kIX:
            return {8, 9};
        case reg::kIY:
            return {10, 11};
        case reg::kAF:
            return {0, 1};
        default:
            return {-1, -1};
    }
}

uint16_t AddressRegs(MemForm form)
{
    switch (form)
    {
        case MemForm::HL:
            return reg::kHL;
        case MemForm::BC:
            return reg::kBC;
        case MemForm::DE:
            return reg::kDE;
        case MemForm::IX:
            return reg::kIX;
        case MemForm::IY:
            return reg::kIY;
        default:
            return 0;
    }
}

constexpr uint16_t kValue8 = reg::kA | reg::kBC | reg::kDE | reg::kHL | reg::kIX | reg::kIY;

/// The screen offset of byte column @p x, pixel line @p y
uint16_t ScreenOffset(int x, int y)
{
    return static_cast<uint16_t>(((y & 0xC0) << 5) | ((y & 7) << 8) | ((y & 0x38) << 2) | x);
}

/// Where a byte's value was copied to since it was last written: the first two places and how many different ones
/// (0 .. 3, 3 or more)
struct Fanout
{
    PhysKey to[2] = {kNoKey, kNoKey};
    uint8_t places = 0;

    uint8_t Add(PhysKey key)
    {
        if (places < 3 && to[0] != key && to[1] != key)
        {
            if (places < 2)
                to[places] = key;
            ++places;
        }
        return places;
    }
};

/// The places a value written into a buffer goes to: both screens (double buffered)
constexpr uint8_t kBufferPlaces = 2;

struct DrawAcc
{
    uint32_t draws = 0;
    uint32_t frames = 0;
    uint32_t lastFrame = 0;
    uint8_t x = 0;
    uint8_t y = 0;
    uint8_t screen = 0;
    uint8_t positions = 0;
    PhysKey writer = kNoKey;
    DrawStep right;
    DrawStep down;
};

/// One more neighbour @p delta bytes away: a majority vote keeps the most frequent distance
void AddStep(DrawStep& step, int32_t delta)
{
    if (step.seen != 0xFFFFFFFFu)
        ++step.seen;
    if (step.count != 0 && step.delta == delta)
        ++step.count;
    else if (step.count == 0)
        step = DrawStep{static_cast<int16_t>(delta), 1, step.seen};
    else
        --step.count;
}

}  // namespace

struct ValueFlow::Impl
{
    Value regs[kSlots];
    Value alt[kSlots];  ///< A' and B' .. L'
    std::vector<std::unique_ptr<PhysKey[]>> shadow;
    std::vector<std::unique_ptr<Fanout[]>> fanout;  ///< per byte: where its value was copied to
    std::vector<std::unique_ptr<DrawAcc[]>> draws;
    uint64_t seq = 0;
    uint64_t lastFrame = ~0ull;             ///< the frame of the last instruction
    PhysKey blockWriter = kNoKey;           ///< the block copy instruction of the last steps
    PhysKey blockWrote[kFillDistance] = {};  ///< the bytes its last steps wrote
    int blockAt = 0;

    PhysKey Shadow(PhysKey key) const
    {
        const uint32_t page = KeyPage(key);
        return page < shadow.size() && shadow[page] ? shadow[page][KeyOffset(key)] : kNoKey;
    }

    void SetShadow(PhysKey key, PhysKey origin)
    {
        const uint32_t page = KeyPage(key);
        if (origin == kNoKey && (page >= shadow.size() || !shadow[page]))
            return;
        if (page >= shadow.size())
            shadow.resize(page + 1);
        if (!shadow[page])
        {
            shadow[page].reset(new PhysKey[0x4000]);
            std::fill_n(shadow[page].get(), 0x4000, kNoKey);
        }
        shadow[page][KeyOffset(key)] = origin;
        if (page < fanout.size() && fanout[page])
            fanout[page][KeyOffset(key)] = Fanout{};
    }

    Fanout& FanoutOf(PhysKey key)
    {
        const uint32_t page = KeyPage(key);
        if (page >= fanout.size())
            fanout.resize(page + 1);
        if (!fanout[page])
            fanout[page].reset(new Fanout[0x4000]);
        return fanout[page][KeyOffset(key)];
    }

    Value Read(PhysKey key)
    {
        ++seq;
        const PhysKey s = Shadow(key);
        Value v;
        v.s[0] = Source{key, s != kNoKey ? s : key, seq};
        return v;
    }

    DrawAcc* Acc(PhysKey key, bool make)
    {
        const uint32_t page = KeyPage(key);
        if (page >= draws.size() || !draws[page])
        {
            if (!make)
                return nullptr;
            if (page >= draws.size())
                draws.resize(page + 1);
            draws[page].reset(new DrawAcc[0x4000]);
        }
        return &draws[page][KeyOffset(key)];
    }

    /// The screens as a frame ends: each drawn source byte shown votes for the distance in memory to the source byte
    /// shown right of it and below it. What stays on the screen (the picture) outvotes what a redraw leaves next to
    /// another frame's bytes for a moment
    void Sample()
    {
        for (const uint16_t page : {5, 7})
        {
            const PhysKey* shown = page < shadow.size() ? shadow[page].get() : nullptr;
            if (shown == nullptr)
                continue;
            const auto origin = [&](int x, int y) {
                const PhysKey o = shown[ScreenOffset(x, y)];
                return o != kNoKey && !IsScreen(o) ? o : kNoKey;
            };
            for (int y = 0; y < 192; ++y)
                for (int x = 0; x < 32; ++x)
                {
                    const PhysKey o = origin(x, y);
                    DrawAcc* acc = o != kNoKey ? Acc(o, false) : nullptr;
                    if (acc == nullptr || acc->draws == 0)
                        continue;
                    const PhysKey right = x < 31 ? origin(x + 1, y) : kNoKey;
                    const PhysKey down = y < 191 ? origin(x, y + 1) : kNoKey;
                    if (right != kNoKey && KeyPage(right) == KeyPage(o) && right != o)
                        AddStep(acc->right, static_cast<int32_t>(KeyOffset(right)) - static_cast<int32_t>(KeyOffset(o)));
                    if (down != kNoKey && KeyPage(down) == KeyPage(o) && down != o)
                        AddStep(acc->down, static_cast<int32_t>(KeyOffset(down)) - static_cast<int32_t>(KeyOffset(o)));
                }
        }
    }

    /// @p origin drawn at screen byte @p at
    void Draw(PhysKey origin, PhysKey at, PhysKey writer, uint64_t frame)
    {
        const uint16_t offset = KeyOffset(at);
        const int x = offset & 31;
        const int y = ((offset >> 8) & 7) | ((offset >> 2) & 0x38) | ((offset >> 5) & 0xC0);
        const auto screen = static_cast<uint8_t>(KeyPage(at));
        DrawAcc& acc = *Acc(origin, true);
        if (acc.draws != 0xFFFFFFFFu)
            ++acc.draws;
        const auto f = static_cast<uint32_t>(frame);
        if (acc.frames == 0 || acc.lastFrame != f)
        {
            ++acc.frames;
            acc.lastFrame = f;
        }
        if (acc.positions == 0 || ((acc.x != x || acc.y != y || acc.screen != screen) && acc.positions != 255))
            ++acc.positions;
        acc.x = static_cast<uint8_t>(x);
        acc.y = static_cast<uint8_t>(y);
        acc.screen = screen;
        acc.writer = writer;
    }

    /// A memory write of @p value: the latest source that is not the byte written. A byte copied from another one
    /// passes that one's source on while each value written into it goes to one or two places (a buffer: to the
    /// screen, both screens); one whose value goes to more places is a table (a font unpacked from a pool of rows, the
    /// sprites a level unpacks) and the source itself
    void Store(PhysKey key, const Value& value, PhysKey writer, uint64_t frame)
    {
        PhysKey origin = kNoKey;
        for (const Source& s : value.s)
            if (s.read != kNoKey && s.read != key)
            {
                origin = s.origin;
                if (origin != s.read && !IsScreen(s.read))
                {
                    Fanout& f = FanoutOf(s.read);
                    if (f.Add(key) > kBufferPlaces)
                        origin = s.read;
                }
                break;
            }
        SetShadow(key, origin);
        if (origin != kNoKey && IsPixels(key) && !IsScreen(origin))
            Draw(origin, key, writer, frame);
    }

    void Instruction(const OpInfo& op, const Access* accesses, int count, PhysKey writer, uint64_t frame)
    {
        if (frame != lastFrame)
        {
            if (lastFrame != ~0ull)
                Sample();
            lastFrame = frame;
        }
        const Access* reads[4] = {};
        const Access* writes[4] = {};
        const Access* stackReads[2] = {};
        const Access* stackWrites[2] = {};
        int nr = 0, nw = 0, nsr = 0, nsw = 0;
        for (int i = 0; i < count; ++i)
        {
            const Access& a = accesses[i];
            if (a.push)
                Store(a.key, Value{}, writer, frame);  // an interrupt's or a call's return address
            else if (a.stack)
            {
                if (a.write && nsw < 2)
                    stackWrites[nsw++] = &a;
                else if (!a.write && nsr < 2)
                    stackReads[nsr++] = &a;
            }
            else if (a.write && nw < 4)
                writes[nw++] = &a;
            else if (!a.write && nr < 4)
                reads[nr++] = &a;
        }
        const uint16_t w = op.regsWritten;

        if (op.cls == OpClass::Exchange)
        {
            if ((w & reg::kAlt) != 0 && (w & reg::kB) != 0)
                std::swap_ranges(regs + 2, regs + 8, alt + 2);
            else if ((w & reg::kAlt) != 0)
                std::swap(regs[0], alt[0]);
            else
            {
                std::swap(regs[4], regs[6]);
                std::swap(regs[5], regs[7]);
            }
            return;
        }

        if (op.cls == OpClass::Block)
        {
            if (nr >= 1 && nw >= 1)
            {
                const PhysKey from = reads[0]->key, to = writes[0]->key;
                if (blockWriter != writer)
                {
                    blockWriter = writer;
                    std::fill_n(blockWrote, kFillDistance, kNoKey);
                }
                const bool fill = std::find(blockWrote, blockWrote + kFillDistance, from) != blockWrote + kFillDistance;
                blockWrote[blockAt] = to;
                blockAt = (blockAt + 1) % kFillDistance;
                Store(to, fill ? Value{} : Read(from), writer, frame);
            }
            else if (nw >= 1)
                Store(writes[0]->key, Value{}, writer, frame);  // INI / IND
            return;
        }

        if (op.mem == MemForm::Stack)
        {
            const auto [hi, lo] = PairBits(op.loadDest != 0 ? op.loadDest
                                                           : static_cast<uint16_t>(op.regsRead & ~reg::kSP));
            const auto order = [](const Access** two, int n) {
                if (n == 2 && two[1]->z80 == static_cast<uint16_t>(two[0]->z80 - 1))
                    std::swap(two[0], two[1]);  // two[0]: the lower address (the low byte)
            };
            order(stackReads, nsr);
            order(stackWrites, nsw);
            Value oldLo, oldHi;
            if (hi >= 0)
            {
                oldLo = regs[lo];
                oldHi = regs[hi];
            }
            const bool data = op.flow == Flow::None && hi >= 0;
            if (data && nsr == 2)
            {
                regs[lo] = Read(stackReads[0]->key);
                regs[hi] = Read(stackReads[1]->key);
            }
            for (int i = 0; i < nsw; ++i)
                Store(stackWrites[i]->key, data && nsw == 2 ? (i == 0 ? oldLo : oldHi) : Value{}, writer, frame);
            return;
        }

        if (op.aluOp != 0xFF)
        {
            if (op.aluOp == 7)
                return;  // CP
            if (op.memRead && nr >= 1)
                regs[0] = Merge(Read(reads[0]->key), regs[0]);
            else if (op.immBytes == 0)
            {
                const int other = Bit(static_cast<uint16_t>(op.regsRead & ~(reg::kA | reg::kF)));
                if (other >= 0)
                    regs[0] = Merge(regs[other], regs[0]);
                else if (op.aluOp == 2 || op.aluOp == 3 || op.aluOp == 5)
                    regs[0] = Value{};  // SUB A / SBC A,A / XOR A: a constant
            }
            return;
        }

        if (op.memRead && op.memWrite && nr >= 1)
        {
            // INC (HL), RLC (HL), SET b,(HL), RLD: the byte keeps its source (the DDCB forms copy it to a register too)
            const Value v = Read(reads[0]->key);
            for (int i = 0; i < nw; ++i)
                Store(writes[i]->key, v, writer, frame);
            const int copy = Bit(static_cast<uint16_t>(w & kValue8));
            if (copy >= 0)
                regs[copy] = v;
            return;
        }

        if (op.memWrite)
        {
            const uint16_t data = static_cast<uint16_t>(op.regsRead & kValue8 & ~AddressRegs(op.mem));
            const int single = Bit(data);
            const auto [hi, lo] = PairBits(data);
            if (op.immBytes == 1 && !op.immIsAddress)
                for (int i = 0; i < nw; ++i)
                    Store(writes[i]->key, Value{}, writer, frame);  // LD (HL),n
            else if (single >= 0 && nw >= 1)
                Store(writes[0]->key, regs[single], writer, frame);
            else if (hi >= 0 && nw == 2)
            {
                const bool firstLow = writes[0]->z80 != static_cast<uint16_t>(writes[1]->z80 + 1);
                Store(writes[0]->key, regs[firstLow ? lo : hi], writer, frame);
                Store(writes[1]->key, regs[firstLow ? hi : lo], writer, frame);
            }
            else
                for (int i = 0; i < nw; ++i)
                    Store(writes[i]->key, Value{}, writer, frame);
            return;
        }

        if (op.loadDest != 0 && nr >= 1)
        {
            const int single = Bit(op.loadDest);
            const auto [hi, lo] = PairBits(op.loadDest);
            if (single >= 0 && single < kSlots)
                regs[single] = Read(reads[0]->key);
            else if (hi >= 0 && nr >= 2)
            {
                regs[lo] = Read(reads[0]->key);
                regs[hi] = Read(reads[1]->key);
            }
            return;
        }

        if (op.copyDest != 0)
        {
            const int to = Bit(op.copyDest);
            const int from = Bit(op.copySrc);
            if (to >= 0 && to < kSlots)
                regs[to] = from >= 0 && from < kSlots ? regs[from] : Value{};
            return;
        }

        // A register changed in place keeps its sources (a shift of a graphic byte, a counter); anything else that
        // writes a register (an immediate, IN, LD A,I) gives it none
        if ((op.cls == OpClass::IncDec || op.cls == OpClass::Rotate || op.cls == OpClass::Bit ||
             op.cls == OpClass::Alu) &&
            op.immBytes == 0)
            return;
        for (int bit = 0; bit < kSlots; ++bit)
            if (bit != 1 && (w & (1u << bit)) != 0)
                regs[bit] = Value{};
    }
};

ValueFlow::ValueFlow() : _impl(std::make_unique<Impl>()) {}
ValueFlow::~ValueFlow() = default;

void ValueFlow::Instruction(const OpInfo& op, const Access* accesses, int count, PhysKey writer, uint64_t frame)
{
    _impl->Instruction(op, accesses, count, writer, frame);
}

std::vector<DrawnByte> ValueFlow::Build() const
{
    std::vector<DrawnByte> out;
    for (size_t page = 0; page < _impl->draws.size(); ++page)
    {
        const DrawAcc* table = _impl->draws[page].get();
        if (table == nullptr)
            continue;
        for (uint32_t offset = 0; offset < 0x4000; ++offset)
        {
            const DrawAcc& a = table[offset];
            if (a.draws == 0)
                continue;
            DrawnByte d;
            d.key = MakeKey(static_cast<uint16_t>(page), static_cast<uint16_t>(offset));
            d.draws = a.draws;
            d.frames = a.frames;
            d.positions = a.positions;
            d.x = a.x;
            d.y = a.y;
            d.screen = a.screen;
            d.writer = a.writer;
            d.right = a.right;
            d.down = a.down;
            out.push_back(d);
        }
    }
    return out;
}

size_t ValueFlow::MemoryBytes() const
{
    size_t bytes = 0;
    for (const auto& t : _impl->shadow)
        bytes += t ? 0x4000 * sizeof(PhysKey) : 0;
    for (const auto& t : _impl->fanout)
        bytes += t ? 0x4000 * sizeof(Fanout) : 0;
    for (const auto& t : _impl->draws)
        bytes += t ? 0x4000 * sizeof(DrawAcc) : 0;
    return bytes;
}

}  // namespace analysis
