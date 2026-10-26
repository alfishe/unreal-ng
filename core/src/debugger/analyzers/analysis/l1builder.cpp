#include "debugger/analyzers/analysis/l1builder.h"

#include <algorithm>
#include <cstring>
#include <deque>
#include <map>
#include <random>
#include <tuple>
#include <unordered_map>

namespace analysis
{

namespace
{

constexpr uint32_t kNone = 0xFFFFFFFFu;
constexpr size_t kMaxIntervals = 8;     ///< per instruction and direction; nearest neighbours merge beyond
constexpr size_t kMaxPorts = 16;
constexpr size_t kMaxBases = 64;
constexpr size_t kMaxSuccessors = 256;  ///< per instruction (a JP (HL) table, a RET to many places)
constexpr size_t kMaxStack = 256;       ///< shadow call frames (the oldest go first)
constexpr size_t kMaxActiveLoops = 32;
constexpr size_t kMaxTripBuckets = 32;
constexpr uint32_t kRunGap = 64;        ///< bytes of unexecuted code a routine's run may have inside
constexpr int kLoopRegs = 12;           ///< BC DE HL IX IY A B C D E H L
constexpr int kMaxSites = 8;
constexpr int kMaxPatternInstructions = 16;

const char* const kLoopRegNames[kLoopRegs] = {"BC", "DE", "HL", "IX", "IY", "A", "B", "C", "D", "E", "H", "L"};

/// The hash of the builder's unordered containers, salted per builder: an output that followed their order would
/// change from one build to the next
struct SaltedHash
{
    uint64_t salt = 0;
    size_t operator()(uint32_t key) const
    {
        uint64_t x = key ^ salt;
        x ^= x >> 33;
        x *= 0xFF51AFD7ED558CCDull;
        x ^= x >> 33;
        x *= 0xC4CEB9FE1A85EC53ull;
        x ^= x >> 33;
        return static_cast<size_t>(x);
    }
};

struct Succ
{
    uint32_t to = 0;
    EdgeKind kind = EdgeKind::Fallthrough;
    uint64_t count = 0;
};

struct StrideSlot
{
    PhysKey loop = kNoKey;
    uint64_t instance = 0;
    uint64_t pass = 0;
    uint16_t lastAddress = 0;
    int32_t stride = 0;
    bool hasDelta = false;
    bool varied = false;
};

struct Node
{
    PhysKey key = 0;
    uint16_t z80 = 0;
    uint8_t bytes[4] = {};
    uint8_t length = 1;
    const OpInfo* op = nullptr;
    bool smc = false;

    uint64_t exec = 0;
    uint64_t tstates = 0;
    uint64_t firstFrame = 0;
    uint64_t lastFrame = ~0ull;
    uint32_t frames = 0;

    std::vector<Succ> succ;
    uint32_t lastSucc = 0;

    std::vector<Interval> reads;
    std::vector<Interval> writes;
    std::vector<uint16_t> portsIn;
    std::vector<uint16_t> portsOut;
    bool portsInMore = false;
    bool portsOutMore = false;

    // As an entry
    uint16_t entryKinds = 0;
    uint64_t entries = 0;
    uint64_t inclusiveT = 0;
    uint16_t minDepth = 0xFFFF;
    uint16_t maxDepth = 0;
    uint16_t liveIn = 0;
    uint16_t liveOut = 0;
    uint16_t written = 0;

    // Base + offset (the first memory operand)
    uint8_t baseKind = 0xFF;  ///< BaseReg
    uint64_t disp[4] = {};    ///< offsets -128..127
    std::vector<uint16_t> bases;
    uint32_t basesSeen = 0;
    bool baseRead = false;
    bool baseWrite = false;

    StrideSlot stride[2];  ///< the first data read, the first data write

    // DataFlow
    uint16_t immValue = 0;
    uint16_t immUseZ80 = 0;
    uint64_t immUses = 0;
    uint8_t cmpConst = 0;
    SourceKind cmpSource = SourceKind::None;
    uint16_t cmpAddress = 0;
    uint16_t cmpLoader = 0;
    uint64_t cmpCount = 0;
    uint16_t ptTable = 0;
    uint16_t ptReader = 0;
    uint64_t ptCount = 0;

    // BranchCondition
    uint64_t taken = 0;
    uint64_t notTaken = 0;
    bool hasProducer = false;
    bool varies = false;
    uint16_t producerZ80 = 0;
    OpClass producerClass = OpClass::Nop;
    SourceKind tested = SourceKind::None;
    uint16_t testedAddress = 0;
    uint8_t testedConstant = 0;
};

enum FrameKind : uint8_t
{
    kRootFrame,
    kCallFrame,
    kInterruptFrame,
    kDispatchFrame,
};

struct Frame
{
    uint32_t entry = kNone;
    uint32_t site = kNone;
    uint16_t retSlot = 0;
    uint16_t returnAddress = 0;
    uint8_t kind = kRootFrame;
    bool anyReturn = false;  ///< a dispatch frame with no known caller: any RET at its slot ends it
    uint64_t tEntry = 0;
    uint16_t defined = 0;
    uint16_t liveIn = 0;
    uint16_t preserved = 0;
    uint32_t pendingCallee = kNone;
    uint16_t pendingMask = 0;
};

struct LoopStats
{
    PhysKey header = 0;
    uint16_t z80 = 0;
    PhysKey latch = 0;
    uint16_t latchZ80 = 0;
    uint64_t instances = 0;
    uint64_t passes = 0;
    uint32_t minTrips = 0xFFFFFFFFu;
    uint32_t maxTrips = 0;
    std::map<uint32_t, uint64_t> trips;
    int32_t step[kLoopRegs] = {};
    uint8_t state[kLoopRegs] = {};  ///< 0 no delta, 1 one value, 2 the same again, 3 varied
    bool anyAccess = false;
    uint16_t written = 0;  ///< value registers the body wrote, F aside (reg:: bits)
};

struct ActiveLoop
{
    PhysKey header = 0;
    uint32_t depth = 0;
    PhysKey lo = 0;
    PhysKey hi = 0;
    uint64_t passes = 0;
    uint64_t instance = 0;
    uint16_t regs[kLoopRegs] = {};
    bool access = false;
    uint16_t written = 0;
};

struct PairTag
{
    uint32_t node = kNone;
    uint16_t value = 0;
    uint64_t seq = 0;
};

struct Prov
{
    SourceKind kind = SourceKind::None;
    uint16_t address = 0;
    uint16_t loader = 0;
    uint64_t seq = 0;
};

struct Producer
{
    bool valid = false;
    uint16_t z80 = 0;
    OpClass cls = OpClass::Nop;
    SourceKind tested = SourceKind::None;
    uint16_t address = 0;
    uint8_t constant = 0;
};

struct Pending
{
    bool active = false;
    uint32_t node = kNone;
    uint16_t pc = 0;
    L1Builder::Regs regs;
    uint64_t tStart = 0;
    uint64_t frame = 0;
};

/// Pair tags: BC DE HL IX IY
constexpr uint16_t kPairMask[5] = {reg::kBC, reg::kDE, reg::kHL, reg::kIX, reg::kIY};

int PairOfMask(uint16_t mask)
{
    for (int i = 0; i < 5; ++i)
        if (mask == kPairMask[i])
            return i;
    return -1;
}

int PairOfMem(MemForm form)
{
    switch (form)
    {
        case MemForm::BC:
            return 0;
        case MemForm::DE:
            return 1;
        case MemForm::HL:
        case MemForm::Block:
            return 2;
        case MemForm::IX:
            return 3;
        case MemForm::IY:
            return 4;
        default:
            return -1;
    }
}

/// 8-bit provenance slots: A B C D E H L (reg bits 0, 2..7)
int ProvSlot(int bit)
{
    if (bit == 0)
        return 0;
    if (bit >= 2 && bit <= 7)
        return bit - 1;
    return -1;
}

uint16_t RegValue(const L1Builder::Regs& r, int slot)
{
    switch (slot)
    {
        case 0:
            return r.bc;
        case 1:
            return r.de;
        case 2:
            return r.hl;
        case 3:
            return r.ix;
        case 4:
            return r.iy;
        case 5:
            return static_cast<uint16_t>(r.af >> 8);
        case 6:
            return static_cast<uint16_t>(r.bc >> 8);
        case 7:
            return static_cast<uint16_t>(r.bc & 0xFF);
        case 8:
            return static_cast<uint16_t>(r.de >> 8);
        case 9:
            return static_cast<uint16_t>(r.de & 0xFF);
        case 10:
            return static_cast<uint16_t>(r.hl >> 8);
        default:
            return static_cast<uint16_t>(r.hl & 0xFF);
    }
}

int LowestBit(uint16_t mask)
{
    for (int bit = 0; bit < 16; ++bit)
        if ((mask >> bit) & 1u)
            return bit;
    return -1;
}

bool SamePage(PhysKey a, PhysKey b)
{
    return KeyPage(a) == KeyPage(b);
}

void MergeAdjacent(std::vector<Interval>& v)
{
    if (v.size() < 2)
        return;
    std::sort(v.begin(), v.end(), [](const Interval& a, const Interval& b) { return a.lo < b.lo; });
    size_t out = 0;
    for (size_t i = 1; i < v.size(); ++i)
    {
        Interval& last = v[out];
        if (SamePage(last.lo, v[i].lo) && v[i].lo <= last.hi + 1)
            last.hi = std::max(last.hi, v[i].hi);
        else
            v[++out] = v[i];
    }
    v.resize(out + 1);
}

void AddInterval(std::vector<Interval>& v, PhysKey key, uint16_t z80)
{
    for (Interval& iv : v)
    {
        if (key >= iv.lo && key <= iv.hi)
            return;
        if (key == iv.hi + 1 && SamePage(key, iv.hi))
        {
            iv.hi = key;
            MergeAdjacent(v);
            return;
        }
        if (key + 1 == iv.lo && SamePage(key, iv.lo))
        {
            iv.lo = key;
            iv.z80 = z80;
            MergeAdjacent(v);
            return;
        }
    }
    if (v.size() >= 2 * kMaxIntervals)
        return;  // spread over too many pages to keep: the access counters of L0 still have it
    v.push_back(Interval{key, key, z80});
    std::sort(v.begin(), v.end(), [](const Interval& a, const Interval& b) { return a.lo < b.lo; });
    if (v.size() <= kMaxIntervals)
        return;
    // Merge the two nearest neighbours of one page
    size_t best = v.size();
    uint32_t bestGap = 0xFFFFFFFFu;
    for (size_t i = 0; i + 1 < v.size(); ++i)
    {
        if (!SamePage(v[i].hi, v[i + 1].lo))
            continue;
        const uint32_t gap = v[i + 1].lo - v[i].hi;
        if (gap < bestGap)
        {
            bestGap = gap;
            best = i;
        }
    }
    if (best < v.size())
    {
        v[best].hi = v[best + 1].hi;
        v.erase(v.begin() + static_cast<std::ptrdiff_t>(best) + 1);
    }
}

void AddPort(std::vector<uint16_t>& ports, bool& more, uint16_t port)
{
    const auto it = std::lower_bound(ports.begin(), ports.end(), port);
    if (it != ports.end() && *it == port)
        return;
    if (ports.size() >= kMaxPorts)
    {
        more = true;
        return;
    }
    ports.insert(it, port);
}

/// Insert into a sorted, capped set; true when it was new
bool AddSorted(std::vector<uint16_t>& values, uint16_t value, size_t cap)
{
    const auto it = std::lower_bound(values.begin(), values.end(), value);
    if (it != values.end() && *it == value)
        return false;
    if (values.size() >= cap)
        return true;  // new, but not kept
    values.insert(it, value);
    return true;
}

}  // namespace

struct L1Builder::Impl
{
    PageOf pageOf;
    uint64_t salt = 0;

    std::deque<Node> nodes;
    std::vector<std::unique_ptr<uint32_t[]>> pageIndex;
    std::vector<uint8_t> dataPages;  ///< pages a data access touched

    std::vector<Frame> stack;
    int topInterrupt = -1;  ///< index of the highest interrupt frame (-1: none)
    std::unordered_map<PhysKey, LoopStats, SaltedHash> loops;
    std::vector<ActiveLoop> active;
    uint64_t loopInstances = 0;

    PairTag pairs[5];
    PairTag hTag;
    uint64_t lFromRegSeq = 0;  ///< blockSeq + 1 of the last LD L,r (0: none)
    Prov prov[7];
    Producer producer;
    uint64_t blockSeq = 4;
    bool anchorValid = false;
    uint16_t anchor = 0;
    struct
    {
        bool valid = false;
        uint16_t slot = 0;
        uint16_t value = 0;
    } lastPush;

    Pending pending;
    bool first = true;
    uint64_t instructions = 0;
    uint64_t interrupts = 0;
    uint64_t tstates = 0;

    Impl(PageOf pageOfIn, uint64_t saltIn)
        : pageOf(std::move(pageOfIn)), salt(saltIn), loops(64, SaltedHash{saltIn})
    {
        Frame root;
        stack.push_back(root);
    }

    uint32_t GetNode(uint16_t page, uint16_t z80)
    {
        if (page >= pageIndex.size())
            pageIndex.resize(static_cast<size_t>(page) + 1);
        std::unique_ptr<uint32_t[]>& table = pageIndex[page];
        if (!table)
        {
            table.reset(new uint32_t[0x4000]);
            std::fill(table.get(), table.get() + 0x4000, 0u);
        }
        uint32_t& slot = table[z80 & 0x3FFF];
        if (slot == 0)
        {
            nodes.emplace_back();
            Node& node = nodes.back();
            node.key = MakeKey(page, z80);
            node.z80 = z80;
            slot = static_cast<uint32_t>(nodes.size());
        }
        return slot - 1;
    }

    uint32_t FindNode(PhysKey key) const
    {
        const uint16_t page = KeyPage(key);
        if (page >= pageIndex.size() || !pageIndex[page])
            return kNone;
        const uint32_t slot = pageIndex[page][KeyOffset(key)];
        return slot == 0 ? kNone : slot - 1;
    }

    void NoteDataPage(uint16_t page)
    {
        if (page >= dataPages.size())
            dataPages.resize(static_cast<size_t>(page) + 1, 0);
        dataPages[page] = 1;
    }

    void AddSucc(Node& from, uint32_t to, EdgeKind kind)
    {
        if (from.lastSucc < from.succ.size())
        {
            Succ& last = from.succ[from.lastSucc];
            if (last.to == to && last.kind == kind)
            {
                ++last.count;
                return;
            }
        }
        for (uint32_t i = 0; i < from.succ.size(); ++i)
        {
            Succ& s = from.succ[i];
            if (s.to == to && s.kind == kind)
            {
                ++s.count;
                from.lastSucc = i;
                return;
            }
        }
        if (from.succ.size() >= kMaxSuccessors)
            return;
        from.succ.push_back(Succ{to, kind, 1});
        from.lastSucc = static_cast<uint32_t>(from.succ.size() - 1);
    }

    void UpdateTopInterrupt()
    {
        topInterrupt = -1;
        for (int i = static_cast<int>(stack.size()) - 1; i >= 0; --i)
            if (stack[static_cast<size_t>(i)].kind == kInterruptFrame)
            {
                topInterrupt = i;
                break;
            }
    }

    void PushFrame(uint8_t kind, uint32_t entryNode, uint32_t site, uint16_t retSlot, uint16_t returnAddress,
                   uint64_t now, bool anyReturn = false)
    {
        if (stack.size() >= kMaxStack)
            stack.erase(stack.begin() + 1);
        Node& entry = nodes[entryNode];
        const auto depth = static_cast<uint16_t>(stack.size());
        entry.minDepth = std::min(entry.minDepth, depth);
        entry.maxDepth = std::max(entry.maxDepth, depth);
        ++entry.entries;
        Frame frame;
        frame.entry = entryNode;
        frame.site = site;
        frame.retSlot = retSlot;
        frame.returnAddress = returnAddress;
        frame.kind = kind;
        frame.anyReturn = anyReturn;
        frame.tEntry = now;
        stack.push_back(frame);
        if (kind == kInterruptFrame)
            topInterrupt = static_cast<int>(stack.size()) - 1;
    }

    void PopFrame(uint64_t now, bool normal)
    {
        const Frame frame = stack.back();
        stack.pop_back();
        if (frame.kind == kInterruptFrame)
            UpdateTopInterrupt();
        Node& entry = nodes[frame.entry];
        entry.inclusiveT += now >= frame.tEntry ? now - frame.tEntry : 0;
        entry.liveIn |= frame.liveIn;
        entry.written |= frame.defined;
        if (frame.kind == kInterruptFrame || !normal)
            return;
        Frame& caller = stack.back();
        caller.liveIn |= static_cast<uint16_t>(frame.liveIn & ~caller.defined);
        caller.defined |= frame.defined;
        caller.pendingCallee = frame.entry;
        caller.pendingMask = frame.defined;
    }

    /// complete false: the replay ended inside the loop. Its passes and accesses count, its trips only when the loop
    /// never finished an instance (a cut instance would report too few trips)
    void CloseTop(bool complete = true)
    {
        const ActiveLoop a = active.back();
        active.pop_back();
        LoopStats& s = loops[a.header];
        if (!complete && s.instances > 0)
        {
            s.passes += a.passes;
            s.anyAccess |= a.access;
            s.written |= a.written;
            if (!active.empty())
            {
                active.back().access |= a.access;
                active.back().written |= a.written;
            }
            return;
        }
        const uint64_t tripsWide = a.passes + 1;
        const uint32_t trips = tripsWide > 0xFFFFFFFFu ? 0xFFFFFFFFu : static_cast<uint32_t>(tripsWide);
        ++s.instances;
        s.passes += a.passes;
        s.minTrips = std::min(s.minTrips, trips);
        s.maxTrips = std::max(s.maxTrips, trips);
        auto it = s.trips.find(trips);
        if (it != s.trips.end())
            ++it->second;
        else if (s.trips.size() < kMaxTripBuckets)
            s.trips.emplace(trips, 1);
        else
        {
            // Not kept: counted in the nearest lower bucket (the first one when none is lower)
            auto bucket = s.trips.upper_bound(trips);
            if (bucket != s.trips.begin())
                --bucket;
            ++bucket->second;
        }
        s.anyAccess |= a.access;
        s.written |= a.written;
        if (!active.empty())
        {
            active.back().access |= a.access;
            active.back().written |= a.written;
        }
    }

    void CloseLoops(PhysKey key, uint32_t depth)
    {
        while (!active.empty())
        {
            const ActiveLoop& a = active.back();
            if (a.depth > depth || (a.depth == depth && (key < a.lo || key > a.hi)))
                CloseTop();
            else
                break;
        }
    }

    void TrackInduction(LoopStats& s, ActiveLoop& a, const Regs& after)
    {
        for (int r = 0; r < kLoopRegs; ++r)
        {
            const uint16_t now = RegValue(after, r);
            const int32_t delta = r < 5 ? static_cast<int16_t>(static_cast<uint16_t>(now - a.regs[r]))
                                        : static_cast<int8_t>(static_cast<uint8_t>(now - a.regs[r]));
            switch (s.state[r])
            {
                case 0:
                    s.step[r] = delta;
                    s.state[r] = 1;
                    break;
                case 1:
                case 2:
                    s.state[r] = delta == s.step[r] ? 2 : 3;
                    break;
                default:
                    break;
            }
            a.regs[r] = now;
        }
    }

    void OnBackEdge(uint32_t headerNode, const Node& latch, uint32_t depth, const Regs& after)
    {
        const PhysKey header = nodes[headerNode].key;
        const PhysKey latchEnd = latch.key + latch.length - 1;
        // The same loop deeper in the stack (an inner loop left through this back edge): close the ones above it
        for (size_t i = active.size(); i-- > 0;)
        {
            if (active[i].depth != depth)
                break;
            if (active[i].header == header)
            {
                while (active.size() > i + 1)
                    CloseTop();
                break;
            }
        }
        LoopStats* stats = nullptr;
        if (!active.empty() && active.back().header == header && active.back().depth == depth)
        {
            ActiveLoop& a = active.back();
            ++a.passes;
            a.hi = std::max(a.hi, latchEnd);
            stats = &loops[header];
            TrackInduction(*stats, a, after);
        }
        else
        {
            if (active.size() >= kMaxActiveLoops)
                active.erase(active.begin());
            ActiveLoop a;
            a.header = header;
            a.depth = depth;
            a.lo = header;
            a.hi = latchEnd;
            a.passes = 1;
            a.instance = ++loopInstances;
            for (int r = 0; r < kLoopRegs; ++r)
                a.regs[r] = RegValue(after, r);
            active.push_back(a);
            auto [it, created] = loops.try_emplace(header);
            stats = &it->second;
            if (created)
            {
                stats->header = header;
                stats->z80 = nodes[headerNode].z80;
            }
        }
        stats->latch = latch.key;
        stats->latchZ80 = latch.z80;
        anchor = after.hl;
        anchorValid = true;
    }

    void TrackStride(StrideSlot& s, const ActiveLoop& loop, uint16_t address)
    {
        if (s.loop != kNoKey && s.loop != loop.header)
            return;  // the instruction runs in more than one loop: the first one keeps it
        if (s.loop == kNoKey)
            s.loop = loop.header;
        if (s.instance == loop.instance)
        {
            if (loop.passes == s.pass)
                return;  // the first access of a pass counts
            if (loop.passes == s.pass + 1)
            {
                const int32_t delta = static_cast<int16_t>(static_cast<uint16_t>(address - s.lastAddress));
                if (!s.hasDelta)
                {
                    s.stride = delta;
                    s.hasDelta = true;
                }
                else if (delta != s.stride)
                    s.varied = true;
            }
        }
        s.instance = loop.instance;
        s.pass = loop.passes;
        s.lastAddress = address;
    }

    void BaseAccess(Node& n, const OpInfo& op, const Regs& before, const uint8_t* bytes, bool read)
    {
        int32_t d = 0;
        uint16_t base = 0;
        BaseReg kind = BaseReg::HL;
        if (op.mem == MemForm::IX || op.mem == MemForm::IY)
        {
            kind = op.mem == MemForm::IX ? BaseReg::IX : BaseReg::IY;
            base = op.mem == MemForm::IX ? before.ix : before.iy;
            d = static_cast<int8_t>(bytes[op.dispOffset]);
        }
        else
        {
            if (!anchorValid)
            {
                anchor = before.hl;
                anchorValid = true;
            }
            d = static_cast<int16_t>(static_cast<uint16_t>(before.hl - anchor));
            if (d < -128 || d > 127)
            {
                anchor = before.hl;
                d = 0;
            }
            base = anchor;
        }
        if (n.baseKind == 0xFF)
            n.baseKind = static_cast<uint8_t>(kind);
        else if (n.baseKind != static_cast<uint8_t>(kind))
            return;
        const int bit = d + 128;
        n.disp[bit >> 6] |= 1ull << (bit & 63);
        if (AddSorted(n.bases, base, kMaxBases))
            ++n.basesSeen;
        if (read)
            n.baseRead = true;
        else
            n.baseWrite = true;
    }

    /// Complete the pending instruction: @p succNode / @p nextPc is where execution went on
    void Finish(uint32_t succNode, uint16_t nextPc, const Regs& after, uint64_t now, const BusEvent* events, int count)
    {
        const Pending p = pending;
        Node& n = nodes[p.node];

        // Decode from the fetched bytes (the first reads of the instruction)
        uint8_t b[4] = {0, 0, 0, 0};
        int fetched = 0;
        for (int i = 0; i < count && fetched < 4; ++i)
        {
            if (events[i].type != 'R')
                break;
            b[fetched++] = events[i].value;
        }
        const OpInfo& op = DecodeOp(b);
        const int len = op.length;
        const int dataStart = std::min(len, fetched);
        if (n.op == nullptr)
        {
            n.op = &op;
            n.length = static_cast<uint8_t>(len);
            std::memcpy(n.bytes, b, 4);
        }
        else if (n.op != &op || std::memcmp(n.bytes, b, static_cast<size_t>(std::min(len, 4))) != 0)
        {
            n.smc = true;
            n.op = &op;
            n.length = static_cast<uint8_t>(len);
            std::memcpy(n.bytes, b, 4);
        }

        ++n.exec;
        ++instructions;
        const uint64_t dt = now >= p.tStart ? now - p.tStart : 0;
        n.tstates += dt;
        tstates += dt;
        if (n.lastFrame != p.frame)
        {
            if (n.frames == 0)
                n.firstFrame = p.frame;
            ++n.frames;
            n.lastFrame = p.frame;
        }

        const auto depth = static_cast<uint32_t>(stack.size() - 1);
        CloseLoops(n.key, depth);
        ActiveLoop* loop = !active.empty() && active.back().depth == depth ? &active.back() : nullptr;
        // An access counts for the innermost loop unless an interrupt came in since that loop started
        ActiveLoop* accessLoop =
            !active.empty() && static_cast<int>(active.back().depth) >= topInterrupt ? &active.back() : nullptr;

        if (accessLoop)
            accessLoop->written |= static_cast<uint16_t>(op.regsWritten & reg::kValueRegs & ~reg::kF);

        // Operands
        bool firstRead = true, firstWrite = true, firstData = true;
        bool hasRead = false, hasPortIn = false;
        uint16_t readAddress = 0, portIn = 0;
        uint16_t pushLo = 0, pushHi = 0;
        int pushBytes = 0;
        for (int i = dataStart; i < count; ++i)
        {
            const BusEvent& e = events[i];
            if (e.type == 'R' || e.type == 'W')
            {
                const bool read = e.type == 'R';
                if (op.mem == MemForm::Stack)
                {
                    if (!read)
                    {
                        if (e.address == after.sp)
                            pushLo = e.value, ++pushBytes;
                        else if (e.address == static_cast<uint16_t>(after.sp + 1))
                            pushHi = e.value, ++pushBytes;
                    }
                    continue;
                }
                const uint16_t page = pageOf(e.address);
                NoteDataPage(page);
                AddInterval(read ? n.reads : n.writes, MakeKey(page, e.address), e.address);
                if (accessLoop)
                    accessLoop->access = true;
                bool& firstOfKind = read ? firstRead : firstWrite;
                if (firstOfKind)
                {
                    firstOfKind = false;
                    if (loop)
                        TrackStride(n.stride[read ? 0 : 1], *loop, e.address);
                    if (read)
                    {
                        hasRead = true;
                        readAddress = e.address;
                    }
                }
                if (firstData)
                {
                    firstData = false;
                    if (op.mem == MemForm::IX || op.mem == MemForm::IY || op.mem == MemForm::HL)
                        BaseAccess(n, op, p.regs, b, read);
                }
            }
            else if (e.type == 'I' || e.type == 'O')
            {
                if (e.type == 'I')
                {
                    AddPort(n.portsIn, n.portsInMore, e.address);
                    if (!hasPortIn)
                    {
                        hasPortIn = true;
                        portIn = e.address;
                    }
                }
                else
                    AddPort(n.portsOut, n.portsOutMore, e.address);
                if (accessLoop)
                    accessLoop->access = true;
            }
        }

        // Live registers of the frame
        {
            Frame& f = stack.back();
            uint16_t reads = op.regsRead & reg::kValueRegs;
            uint16_t writes = op.regsWritten & reg::kValueRegs;
            const bool isPush = op.cls == OpClass::Stack && op.memWrite && !op.memRead && op.flow == Flow::None;
            const bool isPop = op.cls == OpClass::Stack && op.memRead && !op.memWrite && op.loadDest != 0;
            if (isPush)
            {
                f.preserved |= static_cast<uint16_t>(reads & ~f.defined);
                reads = 0;
            }
            const uint16_t popped = op.loadDest & reg::kValueRegs;
            if (isPop && popped != 0 && (popped & f.preserved) == popped)
            {
                f.preserved &= static_cast<uint16_t>(~popped);
                f.defined &= static_cast<uint16_t>(~popped);
                writes = 0;
            }
            f.liveIn |= static_cast<uint16_t>(reads & ~f.defined);
            if (f.pendingMask != 0)
            {
                const uint16_t used = reads & f.pendingMask;
                if (used != 0 && f.pendingCallee != kNone)
                    nodes[f.pendingCallee].liveOut |= used;
                f.pendingMask &= static_cast<uint16_t>(~(used | writes));
            }
            f.defined |= writes;
        }

        // DataFlow: the uses first (the tags before this instruction), then its definitions
        const auto window = [this](uint64_t seq) { return seq != 0 && blockSeq - seq <= 1; };
        const int memPair = PairOfMem(op.mem);
        if ((op.memRead || op.memWrite) && memPair >= 0)
        {
            const PairTag& tag = pairs[memPair];
            if (tag.node != kNone && window(tag.seq))
            {
                Node& src = nodes[tag.node];
                if (src.immUses++ == 0)
                    src.immUseZ80 = p.pc;
                src.immValue = tag.value;
            }
            if (op.mem == MemForm::Block && op.memWrite && op.memRead)
            {
                const PairTag& de = pairs[1];
                if (de.node != kNone && window(de.seq))
                {
                    Node& src = nodes[de.node];
                    if (src.immUses++ == 0)
                        src.immUseZ80 = p.pc;
                    src.immValue = de.value;
                }
            }
        }
        if (op.mem == MemForm::HL && op.memRead && hTag.node != kNone && window(hTag.seq) && window(lFromRegSeq))
        {
            Node& src = nodes[hTag.node];
            if (src.ptCount++ == 0)
                src.ptReader = p.pc;
            src.ptTable = static_cast<uint16_t>(hTag.value << 8);
        }
        const uint8_t imm8 = op.immBytes == 1 ? b[op.immOffset & 3] : 0;
        if (op.aluOp == 7 && op.immBytes == 1)
        {
            const Prov& a = prov[0];
            if (a.kind != SourceKind::None && window(a.seq))
            {
                ++n.cmpCount;
                n.cmpConst = imm8;
                n.cmpSource = a.kind;
                n.cmpAddress = a.address;
                n.cmpLoader = a.loader;
            }
        }

        // The flag producer, from the provenance before this instruction
        Producer made;
        if ((op.regsWritten & reg::kF) != 0)
        {
            made.valid = true;
            made.z80 = p.pc;
            made.cls = op.cls;
            if (hasRead)
            {
                made.tested = SourceKind::Memory;
                made.address = readAddress;
            }
            else if (hasPortIn)
            {
                made.tested = SourceKind::Port;
                made.address = portIn;
            }
            else
            {
                made.tested = SourceKind::Register;
                for (int bit = 0; bit < 8; ++bit)
                {
                    const int slot = ProvSlot(bit);
                    if (slot < 0 || (op.regsRead & (1u << bit)) == 0)
                        continue;
                    const Prov& pr = prov[slot];
                    if (pr.kind != SourceKind::None && window(pr.seq))
                    {
                        made.tested = pr.kind;
                        made.address = pr.address;
                        break;
                    }
                }
                if (made.tested == SourceKind::Register && op.immBytes == 1 && op.aluOp != 0xFF)
                    made.tested = SourceKind::Constant;
            }
            made.constant = imm8;
        }

        // Definitions: pair tags
        const uint16_t w = op.regsWritten;
        if (op.cls == OpClass::Exchange && w == (reg::kDE | reg::kHL))
        {
            std::swap(pairs[1], pairs[2]);
            hTag = PairTag{};
            lFromRegSeq = 0;
        }
        else
        {
            int keep = -1;
            const uint16_t wNoF = static_cast<uint16_t>(w & ~reg::kF);
            if ((op.cls == OpClass::IncDec || op.cls == OpClass::Alu) && op.mem == MemForm::None && op.immBytes == 0)
                keep = PairOfMask(wNoF);  // INC rr / DEC rr / ADD HL,rr: still the same pointer
            int set = -1;
            if (op.cls == OpClass::Load && op.immBytes == 2 && !op.immIsAddress && op.immDest != 0)
                set = PairOfMask(op.immDest);
            for (int i = 0; i < 5; ++i)
            {
                if (i == set)
                {
                    pairs[i].node = p.node;
                    pairs[i].value = static_cast<uint16_t>(b[op.immOffset & 3] | (b[(op.immOffset + 1) & 3] << 8));
                    pairs[i].seq = blockSeq;
                }
                else if ((w & kPairMask[i]) != 0 && i != keep)
                    pairs[i] = PairTag{};
            }
            if (op.immDest == reg::kH && op.immBytes == 1)
                hTag = PairTag{p.node, imm8, blockSeq};
            else if ((w & reg::kH) != 0)
                hTag = PairTag{};
            if (op.copyDest == reg::kL)
                lFromRegSeq = blockSeq;
            else if ((w & reg::kL) != 0)
                lFromRegSeq = 0;
        }

        // Definitions: 8-bit provenance
        {
            Prov loaded;
            if (op.loadDest != 0)
            {
                if (op.ioIn && hasPortIn)
                    loaded = Prov{SourceKind::Port, portIn, p.pc, blockSeq};
                else if (hasRead && op.mem != MemForm::Stack)
                    loaded = Prov{SourceKind::Memory, readAddress, p.pc, blockSeq};
            }
            const bool keepA = op.aluOp >= 4 && op.aluOp <= 6 &&
                               (op.immBytes == 1 || (op.regsRead & ~reg::kF) == reg::kA);
            Prov copied;
            const int copySrc = op.copySrc != 0 ? ProvSlot(LowestBit(op.copySrc)) : -1;
            if (copySrc >= 0)
                copied = prov[copySrc];
            for (int bit = 0; bit < 8; ++bit)
            {
                const int slot = ProvSlot(bit);
                if (slot < 0 || (w & (1u << bit)) == 0)
                    continue;
                if ((op.loadDest & (1u << bit)) != 0)
                    prov[slot] = loaded;
                else if ((op.copyDest & (1u << bit)) != 0)
                    prov[slot] = copied;
                else if (bit == 0 && keepA)
                    continue;
                else
                    prov[slot] = Prov{};
            }
        }

        // The HL anchor of (HL) walks: a new HL starts a new base, INC / DEC of HL, H or L walk on
        if ((w & reg::kHL) != 0)
        {
            const bool walks = op.cls == OpClass::IncDec && op.mem == MemForm::None &&
                               (static_cast<uint16_t>(w & ~reg::kF) & ~reg::kHL) == 0;
            if (!walks)
            {
                anchor = after.hl;
                anchorValid = true;
            }
        }

        // Control flow
        const auto fall = static_cast<uint16_t>(p.pc + len);
        const bool moved = nextPc != fall;
        EdgeKind kind = EdgeKind::Fallthrough;
        bool addEdge = true;
        switch (op.flow)
        {
            case Flow::None:
            case Flow::Halt:
                kind = moved ? EdgeKind::Taken : EdgeKind::Fallthrough;
                break;
            case Flow::Jp:
            case Flow::Jr:
            case Flow::Djnz:
                kind = moved ? EdgeKind::Taken : EdgeKind::Fallthrough;
                break;
            case Flow::Call:
            case Flow::Rst:
                if (moved || op.flow == Flow::Rst)
                {
                    kind = EdgeKind::Call;
                    nodes[succNode].entryKinds |= op.flow == Flow::Rst ? entry::kRst : entry::kCall;
                    lastPush.valid = false;
                    PushFrame(kCallFrame, succNode, p.node, after.sp, fall, now);
                }
                break;
            case Flow::Ret:
            case Flow::Reti:
            case Flow::Retn:
                if (op.cond != kAlways && !moved)
                    break;
                addEdge = false;
                Return(n, p, succNode, nextPc, now);
                break;
            case Flow::JpIndirect:
                kind = EdgeKind::Indirect;
                nodes[succNode].entryKinds |= entry::kIndirect;
                break;
        }
        if (addEdge)
            AddSucc(n, succNode, kind);

        // Conditional branches: the outcome and what was tested
        if ((op.cond != kAlways || op.flow == Flow::Djnz) && op.flow != Flow::None)
        {
            if (moved)
                ++n.taken;
            else
                ++n.notTaken;
            Producer use = producer;
            if (op.flow == Flow::Djnz)
            {
                use.valid = true;
                use.z80 = p.pc;
                use.cls = OpClass::Jump;
                use.tested = SourceKind::Register;
                use.address = 0;
                use.constant = 0;
            }
            if (use.valid)
            {
                if (!n.hasProducer)
                {
                    n.hasProducer = true;
                    n.producerZ80 = use.z80;
                    n.producerClass = use.cls;
                    n.tested = use.tested;
                    n.testedAddress = use.address;
                    n.testedConstant = use.constant;
                }
                else if (n.producerZ80 != use.z80 || n.tested != use.tested || n.testedAddress != use.address)
                    n.varies = true;
            }
        }
        if (made.valid)
            producer = made;

        // PUSH: what it put where (a RET of it is a dispatch)
        if (op.cls == OpClass::Stack && op.memWrite && !op.memRead && op.flow == Flow::None && pushBytes == 2)
        {
            lastPush.valid = true;
            lastPush.slot = after.sp;
            lastPush.value = static_cast<uint16_t>(pushLo | (pushHi << 8));
        }

        // Back edges make loops
        if (kind == EdgeKind::Taken)
        {
            const Node& target = nodes[succNode];
            if (target.key <= n.key && SamePage(target.key, n.key))
                OnBackEdge(succNode, n, depth, after);
        }

        if (op.flow != Flow::None || kind != EdgeKind::Fallthrough)
            ++blockSeq;
    }

    void Return(Node& n, const Pending& p, uint32_t succNode, uint16_t nextPc, uint64_t now)
    {
        const uint16_t spBefore = p.regs.sp;
        const bool pushed = lastPush.valid && lastPush.slot == spBefore && lastPush.value == nextPc;
        if (pushed)
            lastPush.valid = false;
        // Frames whose return slot is below the stack pointer were abandoned (the stack was reset or unwound)
        while (stack.size() > 1 && stack.back().retSlot < spBefore)
            PopFrame(now, false);

        if (stack.size() > 1 && stack.back().retSlot == spBefore &&
            (stack.back().anyReturn || stack.back().returnAddress == nextPc))
        {
            uint32_t site = kNone;
            bool callFrame = false;
            while (stack.size() > 1 && stack.back().retSlot == spBefore &&
                   (stack.back().anyReturn || stack.back().returnAddress == nextPc))
            {
                site = stack.back().site;
                callFrame = stack.back().kind == kCallFrame;
                PopFrame(now, true);
            }
            if (callFrame && site != kNone)
                AddSucc(nodes[site], succNode, EdgeKind::AfterCall);
            return;
        }

        if (pushed || (stack.size() > 1 && stack.back().retSlot > spBefore))
        {
            // PUSH + RET: a dispatch. The target runs at the level of the code that pushed
            nodes[succNode].entryKinds |= entry::kDispatch;
            AddSucc(n, succNode, EdgeKind::Dispatch);
            const bool known = stack.size() > 1;
            const uint16_t slot = known ? stack.back().retSlot : static_cast<uint16_t>(spBefore + 2);
            const uint16_t back = known ? stack.back().returnAddress : 0;
            PushFrame(kDispatchFrame, succNode, kNone, slot, back, now, !known);
            return;
        }

        if (stack.size() > 1 && stack.back().retSlot == spBefore)
        {
            // The return address was changed: a patched return goes somewhere else
            PopFrame(now, true);
            nodes[succNode].entryKinds |= entry::kDispatch;
            AddSucc(n, succNode, EdgeKind::Dispatch);
            return;
        }
        // A return to a caller from before the replay started: nothing to match
    }

    bool OnStart(uint16_t pc, uint16_t page, const Regs& regs, uint64_t now, uint64_t frame, bool halted,
                 bool interrupted, bool nmi, uint16_t returnAddress, const BusEvent* events, int count)
    {
        if (halted && !interrupted && pending.active && pending.pc == pc)
            return true;  // the HALT repeating: an idle fetch, its time goes to the HALT

        const uint32_t current = GetNode(page, pc);
        uint32_t succNode = current;
        uint16_t nextPc = pc;
        if (interrupted)
        {
            nextPc = returnAddress;
            succNode = GetNode(pageOf(returnAddress), returnAddress);
        }
        if (pending.active)
        {
            Regs after = regs;
            if (interrupted)
            {
                after.sp = static_cast<uint16_t>(regs.sp + 2);
                after.pc = returnAddress;
            }
            Finish(succNode, nextPc, after, now, events, count);
        }
        if (first)
        {
            nodes[current].entryKinds |= entry::kRoot;
            first = false;
        }
        if (interrupted)
        {
            ++interrupts;
            if (pending.active)
                AddSucc(nodes[pending.node], current, EdgeKind::Interrupt);
            nodes[current].entryKinds |= nmi ? entry::kNmi : entry::kInterrupt;
            PushFrame(kInterruptFrame, current, pending.active ? pending.node : kNone, regs.sp, returnAddress, now);
            blockSeq += 2;
        }
        pending.active = true;
        pending.node = current;
        pending.pc = pc;
        pending.regs = regs;
        pending.tStart = now;
        pending.frame = frame;
        return false;
    }

    L1Result Build(const ReplaySummary& summary, const std::function<std::string(uint16_t)>& pageName);
};

L1Builder::L1Builder(PageOf pageOf, uint64_t salt)
{
    if (salt == 0)
    {
        std::random_device random;
        salt = (static_cast<uint64_t>(random()) << 32) ^ random() ^ 0x9E3779B97F4A7C15ull;
    }
    _impl = std::make_unique<Impl>(std::move(pageOf), salt);
}

L1Builder::~L1Builder() = default;

void L1Builder::OnStart(uint16_t pc, uint16_t page, const Regs& regs, uint64_t now, uint64_t frame, bool halted,
                        bool interrupted, bool nmi, uint16_t returnAddress)
{
    if (_impl->OnStart(pc, page, regs, now, frame, halted, interrupted, nmi, returnAddress, _events, _eventCount))
    {
        _eventCount = std::min(_eventCount, 1);  // keep the HALT's own fetch, drop the idle one
        return;
    }
    _eventCount = 0;
}

void L1Builder::Flush(const Regs& regs, uint64_t now)
{
    Impl& impl = *_impl;
    if (!impl.pending.active)
        return;
    const uint16_t next = regs.pc;
    const uint32_t succNode = impl.GetNode(impl.pageOf(next), next);
    impl.Finish(succNode, next, regs, now, _events, _eventCount);
    impl.pending.active = false;
    _eventCount = 0;
}

uint64_t L1Builder::Instructions() const
{
    return _impl->instructions;
}

size_t L1Builder::MemoryBytes() const
{
    const Impl& impl = *_impl;
    size_t bytes = impl.nodes.size() * sizeof(Node);
    for (const Node& n : impl.nodes)
        bytes += n.succ.capacity() * sizeof(Succ) + (n.reads.capacity() + n.writes.capacity()) * sizeof(Interval) +
                 (n.portsIn.capacity() + n.portsOut.capacity() + n.bases.capacity()) * sizeof(uint16_t);
    for (const auto& table : impl.pageIndex)
        if (table)
            bytes += 0x4000 * sizeof(uint32_t);
    bytes += impl.loops.size() * (sizeof(LoopStats) + 32) + impl.stack.capacity() * sizeof(Frame);
    return bytes;
}

L1Result L1Builder::Build(const ReplaySummary& summary, const std::function<std::string(uint16_t)>& pageName)
{
    return _impl->Build(summary, pageName);
}

L1Result L1Builder::Impl::Build(const ReplaySummary& summary, const std::function<std::string(uint16_t)>& pageName)
{
    while (!active.empty())
        CloseTop(/*complete=*/false);

    L1Result out;
    out.replay = summary;
    out.replay.instructions = instructions;
    out.replay.interrupts = interrupts;
    out.replay.tstates = tstates;

    const auto count = static_cast<uint32_t>(nodes.size());
    std::vector<uint32_t> order(count);
    for (uint32_t i = 0; i < count; ++i)
        order[i] = i;
    std::sort(order.begin(), order.end(), [this](uint32_t a, uint32_t b) { return nodes[a].key < nodes[b].key; });

    const auto intra = [](EdgeKind kind) {
        return kind == EdgeKind::Fallthrough || kind == EdgeKind::Taken || kind == EdgeKind::AfterCall;
    };

    // Ownership: flood from every entry over the edges inside routines, until no new entry appears
    std::vector<uint16_t> kinds(count);
    for (uint32_t i = 0; i < count; ++i)
        kinds[i] = nodes[i].entryKinds;
    std::vector<uint32_t> owner(count, kNone);
    std::vector<uint16_t> added(count);
    std::vector<uint32_t> work;
    for (int iteration = 0; iteration < 32; ++iteration)
    {
        std::fill(owner.begin(), owner.end(), kNone);
        std::fill(added.begin(), added.end(), 0);
        const auto flood = [&](uint32_t start) {
            owner[start] = start;
            work.clear();
            work.push_back(start);
            while (!work.empty())
            {
                const uint32_t u = work.back();
                work.pop_back();
                for (const Succ& s : nodes[u].succ)
                {
                    if (!intra(s.kind) || kinds[s.to] != 0)
                        continue;
                    if (owner[s.to] == kNone)
                    {
                        owner[s.to] = start;
                        work.push_back(s.to);
                    }
                    else if (owner[s.to] != start)
                        added[s.to] |= entry::kShared;
                }
            }
        };
        for (const uint32_t u : order)
            if (kinds[u] != 0)
                flood(u);
        for (const uint32_t u : order)
            if (owner[u] == kNone)
            {
                kinds[u] |= entry::kRoot;
                flood(u);
            }

        // Runs: the address-contiguous stretches of one owner's code
        std::vector<uint32_t> run(count, 0);
        uint32_t runId = 0;
        uint32_t prev = kNone;
        for (const uint32_t u : order)
        {
            if (prev == kNone || owner[u] != owner[prev] || !SamePage(nodes[u].key, nodes[prev].key) ||
                nodes[u].key > nodes[prev].key + nodes[prev].length + kRunGap)
                ++runId;
            run[u] = runId;
            prev = u;
        }
        // A JP into another run of its own routine enters a routine of its own
        for (const uint32_t u : order)
        {
            const Node& n = nodes[u];
            if (n.op == nullptr || n.op->flow != Flow::Jp)
                continue;
            for (const Succ& s : n.succ)
                if (s.kind == EdgeKind::Taken && kinds[s.to] == 0 && owner[s.to] == owner[u] && run[s.to] != run[u])
                    added[s.to] |= entry::kJump;
        }
        bool changed = false;
        for (uint32_t i = 0; i < count; ++i)
            if (added[i] != 0 && kinds[i] == 0)
            {
                kinds[i] |= added[i];
                changed = true;
            }
        if (!changed)
            break;
    }

    // Routines in address order
    std::vector<uint32_t> routineOfOwner(count, kNone);
    for (const uint32_t u : order)
        if (kinds[u] != 0 && owner[u] == u)
        {
            routineOfOwner[u] = static_cast<uint32_t>(out.routines.size());
            Routine r;
            r.entry = nodes[u].key;
            r.z80 = nodes[u].z80;
            r.kinds = kinds[u];
            out.routines.push_back(r);
        }
    std::vector<uint32_t> routineOf(count, 0);
    for (uint32_t i = 0; i < count; ++i)
        routineOf[i] = owner[i] != kNone ? routineOfOwner[owner[i]] : 0;

    // Basic blocks
    const auto intraSuccCount = [&](uint32_t u) {
        uint32_t c = 0;
        for (const Succ& s : nodes[u].succ)
            if (intra(s.kind))
                ++c;
        return c;
    };
    const auto chainable = [&](uint32_t u) {
        const Node& n = nodes[u];
        return n.op != nullptr && n.op->flow == Flow::None && !n.op->repeats && intraSuccCount(u) == 1 &&
               n.succ.size() >= 1;
    };
    std::vector<uint32_t> predCount(count, 0), lastPred(count, kNone);
    std::vector<EdgeKind> lastPredKind(count, EdgeKind::Fallthrough);
    for (const uint32_t u : order)
        for (const Succ& s : nodes[u].succ)
            if (intra(s.kind) && owner[s.to] == owner[u])
            {
                ++predCount[s.to];
                lastPred[s.to] = u;
                lastPredKind[s.to] = s.kind;
            }
    std::vector<uint8_t> leader(count, 0);
    for (uint32_t i = 0; i < count; ++i)
        leader[i] = kinds[i] != 0 || predCount[i] != 1 || lastPredKind[i] != EdgeKind::Fallthrough ||
                    !chainable(lastPred[i]);
    std::vector<uint32_t> blockOf(count, kNone);
    std::vector<std::vector<uint32_t>> members;
    const auto growBlock = [&](uint32_t start) {
        std::vector<uint32_t> chain;
        uint32_t cur = start;
        while (true)
        {
            chain.push_back(cur);
            blockOf[cur] = 0;
            if (!chainable(cur))
                break;
            uint32_t next = kNone;
            for (const Succ& s : nodes[cur].succ)
                if (intra(s.kind))
                    next = s.to;
            if (next == kNone || leader[next] || blockOf[next] != kNone || owner[next] != owner[cur])
                break;
            cur = next;
        }
        members.push_back(std::move(chain));
    };
    for (const uint32_t u : order)
        if (leader[u])
            growBlock(u);
    for (const uint32_t u : order)
        if (blockOf[u] == kNone)
            growBlock(u);
    std::sort(members.begin(), members.end(), [this](const std::vector<uint32_t>& a, const std::vector<uint32_t>& b) {
        return nodes[a.front()].key < nodes[b.front()].key;
    });
    for (uint32_t bi = 0; bi < members.size(); ++bi)
        for (const uint32_t u : members[bi])
            blockOf[u] = bi;
    out.blocks.resize(members.size());
    for (uint32_t bi = 0; bi < members.size(); ++bi)
    {
        const std::vector<uint32_t>& chain = members[bi];
        Block& block = out.blocks[bi];
        const Node& head = nodes[chain.front()];
        const Node& tail = nodes[chain.back()];
        block.start = head.key;
        block.z80 = head.z80;
        block.z80End = static_cast<uint16_t>(tail.z80 + tail.length - 1);
        block.routine = routineOf[chain.front()];
        block.instructions = static_cast<uint32_t>(chain.size());
        block.executions = head.exec;
        for (const Succ& s : tail.succ)
        {
            if (intra(s.kind) && owner[s.to] == owner[chain.back()])
                block.successors.push_back(Edge{blockOf[s.to], s.kind, s.count});
            else if (s.kind == EdgeKind::Indirect)
                block.indirectTargets.emplace_back(nodes[s.to].z80, s.count);
        }
        std::sort(block.successors.begin(), block.successors.end(), [](const Edge& a, const Edge& b) {
            return std::tie(a.to, a.kind) < std::tie(b.to, b.kind);
        });
        std::sort(block.indirectTargets.begin(), block.indirectTargets.end());
        out.routines[block.routine].blocks.push_back(bi);
    }

    // The call graph
    std::map<std::tuple<uint32_t, uint32_t, uint8_t>, CallEdge> calls;
    for (const uint32_t u : order)
    {
        const Node& n = nodes[u];
        const uint32_t from = routineOf[u];
        for (const Succ& s : n.succ)
        {
            const uint32_t to = routineOf[s.to];
            const bool targetIsEntry = kinds[s.to] != 0 && owner[s.to] == s.to;
            CallKind kind = CallKind::Call;
            switch (s.kind)
            {
                case EdgeKind::Call:
                    kind = n.op != nullptr && n.op->flow == Flow::Rst ? CallKind::Rst : CallKind::Call;
                    break;
                case EdgeKind::Taken:
                    if (!targetIsEntry || to == from)
                        continue;
                    kind = CallKind::Tail;
                    break;
                case EdgeKind::Fallthrough:
                    if (!targetIsEntry || to == from)
                        continue;
                    kind = CallKind::FallsInto;
                    break;
                case EdgeKind::Indirect:
                    kind = CallKind::Indirect;
                    break;
                case EdgeKind::Dispatch:
                    kind = CallKind::Dispatch;
                    break;
                case EdgeKind::Interrupt:
                    kind = CallKind::Interrupt;
                    break;
                default:
                    continue;
            }
            CallEdge& edge = calls[std::make_tuple(from, to, static_cast<uint8_t>(kind))];
            edge.caller = from;
            edge.callee = to;
            edge.kind = kind;
            edge.count += s.count;
            const auto site = std::lower_bound(edge.sites.begin(), edge.sites.end(), n.z80);
            if ((site == edge.sites.end() || *site != n.z80) && edge.sites.size() < kMaxSites)
                edge.sites.insert(site, n.z80);
        }
    }
    for (auto& [key, edge] : calls)
    {
        (void)key;
        out.calls.push_back(std::move(edge));
    }
    // A routine entered by a jump from another routine (a tail call) is a JP entry as well
    for (const CallEdge& edge : out.calls)
        if (edge.kind == CallKind::Tail)
            out.routines[edge.callee].kinds |= entry::kJump;

    // Profiles
    for (const uint32_t u : order)
    {
        const Node& n = nodes[u];
        Routine& r = out.routines[routineOf[u]];
        RoutineProfile& prof = r.profile;
        ++r.instructions;
        prof.reads.insert(prof.reads.end(), n.reads.begin(), n.reads.end());
        prof.writes.insert(prof.writes.end(), n.writes.begin(), n.writes.end());
        prof.executes.push_back(Interval{n.key, n.key + n.length - 1, n.z80});
        for (const uint16_t port : n.portsIn)
            AddPort(prof.portsIn, prof.portsInMore, port);
        for (const uint16_t port : n.portsOut)
            AddPort(prof.portsOut, prof.portsOutMore, port);
        prof.portsInMore |= n.portsInMore;
        prof.portsOutMore |= n.portsOutMore;
        prof.tstates += n.tstates;
        if (n.frames > 0)
        {
            if (prof.activeFrames == 0 || n.firstFrame < prof.firstFrame)
                prof.firstFrame = n.firstFrame;
            prof.lastFrame = std::max(prof.lastFrame, n.lastFrame);
            prof.activeFrames = std::max(prof.activeFrames, n.frames);
        }
        if (owner[u] == u)
        {
            prof.liveIn = n.liveIn;
            prof.liveOut = n.liveOut;
            prof.written = n.written;
            prof.entries = n.entries;
            prof.tstatesInclusive = n.inclusiveT;
            prof.minDepth = n.minDepth;
            prof.maxDepth = n.maxDepth;
        }
    }
    // Entered counts the jumps in as well: a tail call enters without a frame of its own
    for (const CallEdge& edge : out.calls)
        if (edge.kind == CallKind::Tail || edge.kind == CallKind::FallsInto)
            out.routines[edge.callee].profile.entries += edge.count;
    for (Routine& r : out.routines)
    {
        MergeAdjacent(r.profile.reads);
        MergeAdjacent(r.profile.writes);
        MergeAdjacent(r.profile.executes);
        if (r.profile.minDepth == 0xFFFF)
            r.profile.minDepth = 0;
    }

    // Loops
    for (const auto& [header, s] : loops)
    {
        Loop loop;
        loop.header = header;
        loop.z80 = s.z80;
        loop.latch = s.latch;
        loop.latchZ80 = s.latchZ80;
        const uint32_t headerNode = FindNode(header);
        loop.routine = headerNode != kNone ? routineOf[headerNode] : 0;
        loop.instances = s.instances;
        loop.passes = s.passes;
        loop.minTrips = s.instances != 0 ? s.minTrips : 0;
        loop.maxTrips = s.maxTrips;
        loop.trips.assign(s.trips.begin(), s.trips.end());
        bool pairShown[5] = {};
        for (int r = 0; r < kLoopRegs; ++r)
        {
            if ((s.state[r] != 1 && s.state[r] != 2) || s.step[r] == 0)
                continue;
            // A pair that moves by whole 256s is its high byte counting (DJNZ's B): named by that byte
            if (r < 3 && s.step[r] % 256 == 0)
                continue;
            if (r >= 6)
            {
                const int pair = (r - 6) / 2;  // B C -> BC, D E -> DE, H L -> HL
                if (pairShown[pair])
                    continue;
            }
            if (r < 5)
                pairShown[r] = true;
            loop.induction.push_back(Induction{kLoopRegNames[r], s.step[r]});
        }
        // A delay loop only counts: no memory or port access, and it writes nothing but its counters
        static const uint16_t kLoopRegBits[kLoopRegs] = {reg::kBC, reg::kDE, reg::kHL, reg::kIX, reg::kIY, reg::kA,
                                                         reg::kB,  reg::kC,  reg::kD,  reg::kE,  reg::kH,  reg::kL};
        uint16_t counters = 0;
        for (int r = 0; r < kLoopRegs; ++r)
            if ((s.state[r] == 1 || s.state[r] == 2) && s.step[r] != 0)
                counters |= kLoopRegBits[r];
        loop.delay = !s.anyAccess && s.passes > 0 && (s.written & ~counters) == 0;
        out.loops.push_back(std::move(loop));
    }
    std::sort(out.loops.begin(), out.loops.end(), [](const Loop& a, const Loop& b) { return a.header < b.header; });
    std::map<PhysKey, uint32_t> loopIndex;
    for (uint32_t i = 0; i < out.loops.size(); ++i)
        loopIndex[out.loops[i].header] = i;
    for (const uint32_t u : order)
    {
        const Node& n = nodes[u];
        for (int slot = 0; slot < 2; ++slot)
        {
            const StrideSlot& s = n.stride[slot];
            if (s.loop == kNoKey || !s.hasDelta)
                continue;
            const auto it = loopIndex.find(s.loop);
            if (it == loopIndex.end())
                continue;
            out.loops[it->second].strides.push_back(Stride{n.key, n.z80, slot == 1, s.stride, !s.varied});
        }
    }

    // Base + offset patterns per routine and base register
    struct PatternAcc
    {
        uint64_t disp[4] = {};
        std::vector<uint16_t> bases;
        uint32_t basesSeen = 0;
        bool reads = false;
        bool writes = false;
        std::vector<uint16_t> instructions;
    };
    std::map<std::pair<uint32_t, uint8_t>, PatternAcc> patterns;
    for (const uint32_t u : order)
    {
        const Node& n = nodes[u];
        if (n.baseKind == 0xFF)
            continue;
        PatternAcc& acc = patterns[std::make_pair(routineOf[u], n.baseKind)];
        for (int i = 0; i < 4; ++i)
            acc.disp[i] |= n.disp[i];
        for (const uint16_t base : n.bases)
            AddSorted(acc.bases, base, kMaxBases);
        acc.basesSeen = std::max<uint32_t>({acc.basesSeen, n.basesSeen, static_cast<uint32_t>(acc.bases.size())});
        acc.reads |= n.baseRead;
        acc.writes |= n.baseWrite;
        if (acc.instructions.size() < kMaxPatternInstructions)
            AddSorted(acc.instructions, n.z80, kMaxPatternInstructions);
    }
    for (const auto& [key, acc] : patterns)
    {
        BaseOffsetPattern pattern;
        pattern.routine = key.first;
        pattern.base = static_cast<BaseReg>(key.second);
        for (int bit = 0; bit < 256; ++bit)
            if ((acc.disp[bit >> 6] >> (bit & 63)) & 1u)
                pattern.offsets.push_back(static_cast<int16_t>(bit - 128));
        pattern.bases = acc.bases;
        pattern.basesSeen = acc.basesSeen;
        pattern.reads = acc.reads;
        pattern.writes = acc.writes;
        pattern.instructions = acc.instructions;
        if (pattern.base == BaseReg::HL && pattern.offsets.size() < 2 && pattern.bases.size() < 2)
            continue;
        // The step: the most common distance between consecutive bases (the smallest of equals)
        std::map<int32_t, uint32_t> gaps;
        for (size_t i = 1; i < pattern.bases.size(); ++i)
            ++gaps[static_cast<int32_t>(pattern.bases[i]) - static_cast<int32_t>(pattern.bases[i - 1])];
        uint32_t best = 0;
        for (const auto& [gap, times] : gaps)
            if (times > best)
            {
                best = times;
                pattern.step = gap;
            }
        out.patterns.push_back(std::move(pattern));
    }

    // DataFlow, branch conditions, instructions
    for (const uint32_t u : order)
    {
        const Node& n = nodes[u];
        if (n.immUses > 0)
            out.immediateAddresses.push_back(ImmediateAddress{n.key, n.z80, n.immValue, n.immUseZ80, n.immUses});
        if (n.cmpCount > 0)
            out.constantCompares.push_back(
                ConstantCompare{n.key, n.z80, n.cmpConst, n.cmpSource, n.cmpAddress, n.cmpLoader, n.cmpCount});
        if (n.ptCount > 0)
            out.pageTables.push_back(PageTable{n.key, n.z80, n.ptTable, n.ptReader, n.ptCount});
        if (n.taken + n.notTaken > 0)
        {
            BranchCondition bc;
            bc.instruction = n.key;
            bc.z80 = n.z80;
            bc.cond = n.op != nullptr && n.op->flow == Flow::Djnz ? 8 : (n.op != nullptr ? n.op->cond : kAlways);
            bc.taken = n.taken;
            bc.notTaken = n.notTaken;
            bc.producerZ80 = n.producerZ80;
            bc.producerClass = n.producerClass;
            bc.tested = n.hasProducer ? n.tested : SourceKind::None;
            bc.address = n.testedAddress;
            bc.constant = n.testedConstant;
            bc.varies = n.varies;
            out.branches.push_back(bc);
        }
        Instruction ins;
        ins.key = n.key;
        ins.z80 = n.z80;
        std::memcpy(ins.bytes, n.bytes, 4);
        ins.length = n.length;
        ins.cls = n.op != nullptr ? n.op->cls : OpClass::Nop;
        ins.flow = n.op != nullptr ? n.op->flow : Flow::None;
        ins.executions = n.exec;
        ins.tstates = n.tstates;
        ins.selfModified = n.smc;
        out.instructions.push_back(ins);
        out.instructionRoutine.push_back(routineOf[u]);
    }

    // Pages, named
    std::vector<uint16_t> pages;
    for (uint16_t page = 0; page < pageIndex.size(); ++page)
        if (pageIndex[page])
            pages.push_back(page);
    for (uint16_t page = 0; page < dataPages.size(); ++page)
        if (dataPages[page])
            pages.push_back(page);
    std::sort(pages.begin(), pages.end());
    pages.erase(std::unique(pages.begin(), pages.end()), pages.end());
    for (const uint16_t page : pages)
        out.pageNames.emplace_back(page, pageName ? pageName(page) : std::to_string(page));

    out.BuildIndex();
    return out;
}

}  // namespace analysis
