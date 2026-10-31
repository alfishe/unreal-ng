#include "debugger/analyzers/analysis/l1valueflow.h"

#include <algorithm>
#include <numeric>
#include <tuple>
#include <unordered_map>
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
    bool masked = false;      ///< came in as an AND's operand: a mask, not the picture
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

constexpr size_t kCarryReach = 4;           ///< a rewrite this few writes later: a shifted sprite's carry, same pass
constexpr uint32_t kScreenPixels = 0x1800;
constexpr size_t kMaxPass = 2 * kScreenPixels;
constexpr size_t kMaxFrames = size_t{1} << 17;

/// One screen write of a drawing pass
struct PassWrite
{
    PhysKey src = kNoKey;
    PhysKey mask = kNoKey;
    PhysKey writer = kNoKey;
    uint8_t x = 0;
    uint8_t y = 0;
    uint8_t screen = 0;
};

struct FrameKey
{
    PhysKey base = 0;
    uint8_t width = 0;
    uint8_t height = 0;
    int16_t rowStep = 0;
    int16_t colStep = 0;
    int16_t maskDelta = 0;
    bool operator==(const FrameKey& o) const
    {
        return base == o.base && width == o.width && height == o.height && rowStep == o.rowStep &&
               colStep == o.colStep && maskDelta == o.maskDelta;
    }
};

struct FrameKeyHash
{
    size_t operator()(const FrameKey& k) const
    {
        uint64_t h = (static_cast<uint64_t>(k.base) << 16) ^ (static_cast<uint64_t>(k.width) << 8) ^ k.height;
        h = h * 0x9E3779B97F4A7C15ull ^ (static_cast<uint64_t>(static_cast<uint16_t>(k.rowStep)) << 32) ^
            (static_cast<uint64_t>(static_cast<uint16_t>(k.colStep)) << 16) ^ static_cast<uint16_t>(k.maskDelta);
        return static_cast<size_t>(h * 0xC2B2AE3D27D4EB4Full);
    }
};

struct FrameAcc
{
    uint16_t bytes = 0;
    uint32_t count = 0;
    uint8_t x = 0;
    uint8_t y = 0;
    uint8_t screen = 0;
    PhysKey writer = kNoKey;
};

/// Where the pages were last seen in the Z80's address space: a page's bank (-1: never seen), a bank's page
struct Paging
{
    std::vector<int8_t> bankOf;
    uint16_t pageAt[4] = {0, 0, 0, 0};

    /// @p key's Z80 address (false: its page never seen)
    bool Z80(PhysKey key, int32_t& z80) const
    {
        const uint16_t page = KeyPage(key);
        if (page >= bankOf.size() || bankOf[page] < 0)
            return false;
        z80 = bankOf[page] * 0x4000 + static_cast<int32_t>(KeyOffset(key));
        return true;
    }
};

/// The step from @p a to @p b: in one page, or across pages the Z80 sees side by side (a set stored a pixel row a page
/// on, running past $C000); false: no step
bool StepOf(const Paging& paging, PhysKey a, PhysKey b, int32_t& step)
{
    if (a == b)
        return false;
    if (KeyPage(a) == KeyPage(b))
    {
        step = static_cast<int32_t>(KeyOffset(b)) - static_cast<int32_t>(KeyOffset(a));
        return true;
    }
    int32_t za = 0, zb = 0;
    if (!paging.Z80(a, za) || !paging.Z80(b, zb) || za == zb)
        return false;
    step = zb - za;
    return true;
}

/// The pictures of one drawing pass (Drawn frames in l1valueflow.h)
class PassFit
{
public:
    PassFit(const std::vector<PassWrite>& pass, const Paging& paging) : _pass(pass), _paging(paging) {}

    template <typename Emit>
    void Run(Emit&& emit)
    {
        const size_t n = _pass.size();
        _order.resize(n);
        std::iota(_order.begin(), _order.end(), 0u);
        std::sort(_order.begin(), _order.end(), [this](uint32_t i, uint32_t j) {
            const PassWrite& a = _pass[i];
            const PassWrite& b = _pass[j];
            return std::tie(a.screen, a.x, a.y) < std::tie(b.screen, b.x, b.y);
        });
        // Column pieces: consecutive lines of one byte column, a constant step between their sources
        for (size_t k = 0; k < n; ++k)
        {
            const PassWrite& w = _pass[_order[k]];
            if (!_pieces.empty())
            {
                Piece& p = _pieces.back();
                const PassWrite& last = _pass[_order[p.first + p.length - 1]];
                int32_t step = 0;
                if (last.screen == w.screen && last.x == w.x && last.y + 1 == w.y &&
                    StepOf(_paging, last.src, w.src, step) && (p.length == 1 || step == p.rowStep))
                {
                    p.rowStep = step;
                    ++p.length;
                    continue;
                }
            }
            _pieces.push_back(Piece{static_cast<uint32_t>(k), 1, 0});
        }
        _parent.resize(_pieces.size());
        std::iota(_parent.begin(), _parent.end(), 0u);
        _groups.assign(_pieces.size(), Group{});
        for (size_t i = 0; i < _pieces.size(); ++i)
        {
            Group& g = _groups[i];
            g.pieces.push_back(static_cast<uint32_t>(i));
            g.rowStep = _pieces[i].rowStep;
            g.hasRow = _pieces[i].length > 1;
        }
        // Pieces of neighbouring columns join when one model holds for both groups
        for (size_t i = 0; i < _pieces.size(); ++i)
        {
            const PassWrite& top = Write(i, 0);
            for (size_t j = i + 1; j < _pieces.size(); ++j)
            {
                const PassWrite& other = Write(j, 0);
                if (other.screen != top.screen || other.x > top.x + 1)
                    break;
                if (other.x == top.x + 1)
                    TryJoin(static_cast<uint32_t>(i), static_cast<uint32_t>(j));
            }
        }
        for (size_t i = 0; i < _pieces.size(); ++i)
            if (Find(static_cast<uint32_t>(i)) == i)
                EmitGroup(_groups[i], emit);
    }

private:
    struct Piece
    {
        uint32_t first = 0;  ///< in _order
        uint32_t length = 0;
        int32_t rowStep = 0;
    };
    struct Group
    {
        std::vector<uint32_t> pieces;
        int32_t rowStep = 0;
        int32_t colStep = 0;
        bool hasRow = false;
        bool hasCol = false;
    };

    const PassWrite& Write(size_t piece, uint32_t line) const { return _pass[_order[_pieces[piece].first + line]]; }

    uint32_t Find(uint32_t i)
    {
        while (_parent[i] != i)
            i = _parent[i] = _parent[_parent[i]];
        return i;
    }

    /// The source a piece shows at pixel line @p y (false: none there)
    bool At(uint32_t piece, int y, PhysKey& src) const
    {
        const Piece& p = _pieces[piece];
        const int y0 = Write(piece, 0).y;
        if (y < y0 || y >= y0 + static_cast<int>(p.length))
            return false;
        src = Write(piece, static_cast<uint32_t>(y - y0)).src;
        return true;
    }

    void TryJoin(uint32_t left, uint32_t right)
    {
        // The step between the two columns, the same on every line they share
        const PassWrite& l0 = Write(left, 0);
        const PassWrite& r0 = Write(right, 0);
        const int lo = std::max<int>(l0.y, r0.y);
        const int hi = std::min<int>(l0.y + static_cast<int>(_pieces[left].length),
                                     r0.y + static_cast<int>(_pieces[right].length)) - 1;
        if (lo > hi)
            return;
        int32_t colStep = 0;
        for (int y = lo; y <= hi; ++y)
        {
            PhysKey a = kNoKey, b = kNoKey;
            int32_t step = 0;
            At(left, y, a);
            At(right, y, b);
            if (!StepOf(_paging, a, b, step) || (y > lo && step != colStep))
                return;
            colStep = step;
        }
        const uint32_t ga = Find(left), gb = Find(right);
        if (ga == gb)
            return;
        Group& a = _groups[ga];
        Group& b = _groups[gb];
        if ((a.hasCol && a.colStep != colStep) || (b.hasCol && b.colStep != colStep) ||
            (a.hasRow && b.hasRow && a.rowStep != b.rowStep))
            return;
        Group joined;
        joined.hasRow = a.hasRow || b.hasRow;
        joined.rowStep = a.hasRow ? a.rowStep : b.rowStep;
        joined.hasCol = true;
        joined.colStep = colStep;
        joined.pieces = a.pieces;
        joined.pieces.insert(joined.pieces.end(), b.pieces.begin(), b.pieces.end());
        if (!Holds(joined))
            return;
        const uint32_t root = a.pieces.size() >= b.pieces.size() ? ga : gb;
        _parent[ga == root ? gb : ga] = root;
        _groups[root] = std::move(joined);
        _groups[ga == root ? gb : ga] = Group{};
    }

    /// Every byte of the group at base + line * rowStep + column * colStep (relative to its first byte)
    bool Holds(const Group& g) const
    {
        const PassWrite& ref = Write(g.pieces.front(), 0);
        for (const uint32_t piece : g.pieces)
            for (uint32_t line = 0; line < _pieces[piece].length; ++line)
            {
                const PassWrite& w = Write(piece, line);
                const int dy = w.y - ref.y;
                const int dx = w.x - ref.x;
                if ((dy != 0 && !g.hasRow) || (dx != 0 && !g.hasCol))
                    return false;
                int32_t step = 0;
                const int32_t expected = dy * g.rowStep + dx * g.colStep;
                if (expected == 0 ? w.src != ref.src : !StepOf(_paging, ref.src, w.src, step) || step != expected)
                    return false;
            }
        return true;
    }

    template <typename Emit>
    void EmitGroup(const Group& g, Emit&& emit) const
    {
        const PassWrite& ref = Write(g.pieces.front(), 0);
        int x0 = 255, x1 = 0, y0 = 255, y1 = 0;
        uint32_t bytes = 0;
        bool masked = true;
        int32_t maskDelta = 0;
        PhysKey writer = kNoKey;
        for (const uint32_t piece : g.pieces)
            for (uint32_t line = 0; line < _pieces[piece].length; ++line)
            {
                const PassWrite& w = Write(piece, line);
                x0 = std::min<int>(x0, w.x);
                x1 = std::max<int>(x1, w.x);
                y0 = std::min<int>(y0, w.y);
                y1 = std::max<int>(y1, w.y);
                int32_t delta = 0;
                if (masked &&
                    (w.mask == kNoKey || !StepOf(_paging, w.src, w.mask, delta) || (bytes != 0 && delta != maskDelta)))
                    masked = false;
                maskDelta = delta;
                if (writer == kNoKey)
                    writer = w.writer;
                ++bytes;
            }
        if (bytes < 2)
            return;
        // The top left byte: in the reference's page, or in the page the Z80 saw there
        const int32_t delta = (y0 - ref.y) * g.rowStep + (x0 - ref.x) * g.colStep;
        const int32_t offset = static_cast<int32_t>(KeyOffset(ref.src)) + delta;
        int32_t z80 = 0;
        PhysKey base = kNoKey;
        if (offset >= 0 && offset < 0x4000)
            base = MakeKey(KeyPage(ref.src), static_cast<uint16_t>(offset));
        else if (_paging.Z80(ref.src, z80) && z80 + delta >= 0 && z80 + delta < 0x10000)
            base = MakeKey(_paging.pageAt[(z80 + delta) >> 14], static_cast<uint16_t>(z80 + delta));
        if (base == kNoKey)
            return;
        FrameKey key;
        key.base = base;
        key.width = static_cast<uint8_t>(x1 - x0 + 1);
        key.height = static_cast<uint8_t>(y1 - y0 + 1);
        key.rowStep = static_cast<int16_t>(key.height > 1 ? g.rowStep : 0);
        key.colStep = static_cast<int16_t>(key.width > 1 ? g.colStep : 0);
        key.maskDelta = static_cast<int16_t>(masked ? maskDelta : 0);
        emit(key, static_cast<uint16_t>(bytes), static_cast<uint8_t>(x0), static_cast<uint8_t>(y0), ref.screen,
             writer);
    }

    const std::vector<PassWrite>& _pass;
    const Paging& _paging;
    std::vector<uint32_t> _order;
    std::vector<Piece> _pieces;
    std::vector<uint32_t> _parent;
    std::vector<Group> _groups;
};

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
    std::vector<PassWrite> pass;                                       ///< the drawing pass in progress
    std::vector<int32_t> passAt = std::vector<int32_t>(2 * kScreenPixels, -1);  ///< per screen byte: its write
    std::unordered_map<FrameKey, FrameAcc, FrameKeyHash> frames;
    Paging paging;

    /// The drawing pass ends: its pictures join the frames drawn
    void EndPass()
    {
        if (pass.empty())
            return;
        PassFit(pass, paging).Run([this](const FrameKey& key, uint16_t bytes, uint8_t x, uint8_t y, uint8_t screen,
                                 PhysKey writer) {
            auto it = frames.find(key);
            if (it == frames.end())
            {
                if (frames.size() >= kMaxFrames)
                    return;
                it = frames.emplace(key, FrameAcc{}).first;
            }
            FrameAcc& acc = it->second;
            acc.bytes = std::max(acc.bytes, bytes);
            if (acc.count != 0xFFFFFFFFu)
                ++acc.count;
            acc.x = x;
            acc.y = y;
            acc.screen = screen;
            acc.writer = writer;
        });
        for (const PassWrite& w : pass)
            passAt[(w.screen == 7 ? kScreenPixels : 0) + ScreenOffset(w.x, w.y)] = -1;
        pass.clear();
    }

    /// @p origin (with @p mask merged into it) drawn at screen byte @p at, in the drawing pass
    void PassDraw(PhysKey origin, PhysKey mask, PhysKey at, PhysKey writer, int x, int y)
    {
        const auto screen = static_cast<uint8_t>(KeyPage(at));
        const size_t pos = (screen == 7 ? kScreenPixels : 0) + KeyOffset(at);
        if (passAt[pos] >= 0)
        {
            const auto at_ = static_cast<size_t>(passAt[pos]);
            if (pass.size() - at_ <= kCarryReach)
            {
                pass[at_].src = origin;
                pass[at_].mask = mask;
                pass[at_].writer = writer;
                return;
            }
            EndPass();
        }
        if (pass.size() >= kMaxPass)
            EndPass();
        passAt[pos] = static_cast<int32_t>(pass.size());
        pass.push_back(PassWrite{origin, mask, writer, static_cast<uint8_t>(x), static_cast<uint8_t>(y), screen});
    }

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

    /// @p origin drawn at screen byte @p at (@p mask: the mask byte merged into it)
    void Draw(PhysKey origin, PhysKey mask, PhysKey at, PhysKey writer, uint64_t frame)
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
        PassDraw(origin, mask, at, writer, x, y);
    }

    /// A memory write of @p value: the latest source that is not the byte written. A byte copied from another one
    /// passes that one's source on while each value written into it goes to one or two places (a buffer: to the
    /// screen, both screens); one whose value goes to more places is a table (a font unpacked from a pool of rows, the
    /// sprites a level unpacks) and the source itself
    void Store(PhysKey key, const Value& value, PhysKey writer, uint64_t frame)
    {
        PhysKey origin = kNoKey;
        PhysKey mask = kNoKey;  // an AND's operand, else the next source that is no screen byte (ANDed in before)
        PhysKey before = kNoKey;
        bool found = false;
        const bool anyPlain = std::any_of(std::begin(value.s), std::end(value.s), [&](const Source& s) {
            return s.read != kNoKey && s.read != key && !s.masked;
        });
        for (const Source& s : value.s)
        {
            if (s.read == kNoKey || s.read == key)
                continue;
            if (s.masked && anyPlain)
            {
                if (mask == kNoKey && !IsScreen(s.read))
                    mask = s.origin;
                continue;
            }
            if (found)
            {
                if (before == kNoKey && !IsScreen(s.read))
                    before = s.origin;
                continue;
            }
            found = true;
            origin = s.origin;
            if (origin != s.read && !IsScreen(s.read))
            {
                Fanout& f = FanoutOf(s.read);
                if (f.Add(key) > kBufferPlaces)
                    origin = s.read;
            }
        }
        if (mask == kNoKey)
            mask = before;
        SetShadow(key, origin);
        if (origin != kNoKey && IsPixels(key) && !IsScreen(origin))
            Draw(origin, mask, key, writer, frame);
    }

    void Instruction(const OpInfo& op, const Access* accesses, int count, PhysKey writer, uint64_t frame)
    {
        if (frame != lastFrame)
        {
            // A pass goes on over a frame's end: a copy of a back buffer longer than a frame is one picture
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
            const uint16_t page = KeyPage(accesses[i].key);
            if (page >= paging.bankOf.size())
                paging.bankOf.resize(page + 1u, -1);
            paging.bankOf[page] = static_cast<int8_t>(accesses[i].z80 >> 14);
            paging.pageAt[accesses[i].z80 >> 14] = page;
        }
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
            // AND's operand is the mask (OR (HL) / AND (HL) with the mask after the graphic); A keeps its role
            const auto operand = [&](Value v) {
                if (op.aluOp == 4)
                    for (Source& e : v.s)
                        e.masked = true;
                return v;
            };
            if (op.memRead && nr >= 1)
                regs[0] = Merge(operand(Read(reads[0]->key)), regs[0]);
            else if (op.immBytes == 0)
            {
                const int other = Bit(static_cast<uint16_t>(op.regsRead & ~(reg::kA | reg::kF)));
                if (other >= 0)
                    regs[0] = Merge(operand(regs[other]), regs[0]);
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

std::vector<DrawnFrame> ValueFlow::BuildFrames()
{
    _impl->EndPass();
    std::vector<DrawnFrame> out;
    out.reserve(_impl->frames.size());
    for (const auto& [key, acc] : _impl->frames)
    {
        DrawnFrame f;
        f.base = key.base;
        f.width = key.width;
        f.height = key.height;
        f.rowStep = key.rowStep;
        f.colStep = key.colStep;
        f.maskDelta = key.maskDelta;
        f.bytes = acc.bytes;
        f.count = acc.count;
        f.x = acc.x;
        f.y = acc.y;
        f.screen = acc.screen;
        f.writer = acc.writer;
        out.push_back(f);
    }
    std::sort(out.begin(), out.end(), [](const DrawnFrame& a, const DrawnFrame& b) {
        return std::tie(a.base, a.width, a.height, a.rowStep, a.colStep, a.maskDelta) <
               std::tie(b.base, b.width, b.height, b.rowStep, b.colStep, b.maskDelta);
    });
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
    bytes += _impl->passAt.size() * sizeof(int32_t) + _impl->pass.capacity() * sizeof(PassWrite);
    bytes += _impl->frames.size() * (sizeof(FrameKey) + sizeof(FrameAcc) + 24);
    return bytes;
}

}  // namespace analysis
