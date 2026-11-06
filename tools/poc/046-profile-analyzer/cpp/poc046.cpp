/// @file poc046.cpp
/// @brief POC 046 (tools/poc/046-profile-analyzer/README.md): what a CPU time profile costs on the emulation thread
/// per option, and how its attribution compares with L1 on the same execution.
///
///   poc046-profile bench --snapshot S [--model 48K] [--frames N] [--rounds R] [--variants A,B,C,...]
///   poc046-profile replay --session S.ttd [--model 48K] [--frames N] --out profile.json [--variant D] [--core 1]
///
/// bench: per round each variant runs N turbo frames from the snapshot on this thread (thread CPU time), the order
/// alternating between rounds. replay: the variant's step function on the replay of a TTD recording (the L1 build's
/// instruction stream, analysis::ReplayWindow), the profile written as JSON for compare_l1.py; the acknowledge is
/// folded into the interrupted instruction as L1 books it.

#include <algorithm>
#include <chrono>
#include <cinttypes>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

#include <time.h>

#include "base/featuremanager.h"
#include "debugger/analyzers/analysis/replaytracer.h"
#include "debugger/analyzers/analysis/replaywindow.h"
#include "debugger/analyzers/analyzermanager.h"
#include "debugger/analyzers/ianalyzer.h"
#include "debugger/analyzers/profile/profileanalyzer.h"
#include "debugger/analyzers/profile/profilecontrol.h"
#include "debugger/debugmanager.h"
#include "debugger/ttd/timetravelcontroller.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/emulatormanager.h"
#include "emulator/memory/memory.h"
#include "emulator/slots/slotcontrol.h"
#include "emulator/sound/soundmanager.h"

namespace
{
double ThreadCpuMs()
{
    timespec ts{};
    clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts);
    return static_cast<double>(ts.tv_sec) * 1000.0 + static_cast<double>(ts.tv_nsec) / 1e6;
}

uint64_t NowNs()
{
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
}

// ---------------------------------------------------------------------------
// Opcode flow classes (the shadow stack's view of an instruction)
// ---------------------------------------------------------------------------
enum Flow : uint8_t
{
    FlowNone = 0,
    FlowCall = 1,  ///< CALL nn / CALL cc,nn (3 bytes)
    FlowRst = 2,   ///< RST n (1 byte)
    FlowRet = 3,   ///< RET / RET cc
    FlowEd = 4,    ///< ED prefix: RETN / RETI by the second byte
};

struct FlowTable
{
    uint8_t main[256] = {};
    uint8_t ed[256] = {};
    FlowTable()
    {
        main[0xCD] = FlowCall;
        for (uint8_t op : {0xC4, 0xCC, 0xD4, 0xDC, 0xE4, 0xEC, 0xF4, 0xFC})
            main[op] = FlowCall;
        for (int op = 0xC7; op <= 0xFF; op += 8)
            main[op] = FlowRst;
        main[0xC9] = FlowRet;
        for (int op = 0xC0; op <= 0xF8; op += 8)
            main[op] = FlowRet;
        main[0xED] = FlowEd;
        for (int op = 0x45; op <= 0x7D; op += 8)
            ed[op] = FlowRet;
    }
};
const FlowTable kFlow;

// ---------------------------------------------------------------------------
// The profile
// ---------------------------------------------------------------------------
constexpr uint32_t kPageCells = 0x4000;
constexpr uint32_t kRidShift = 40;  ///< Cell16::count: bits 0-39 executions, 40-63 routine id + 1 (an entry)
constexpr uint64_t kCountMask = (uint64_t(1) << kRidShift) - 1;
constexpr uint32_t kBeamBins = 64;

struct Cell16
{
    uint64_t count;
    uint64_t t;
};
struct Cell8
{
    uint32_t count;
    uint32_t t;
};

enum EntryKind : uint8_t
{
    KindCall = 0,
    KindRst,
    KindInterrupt,
    KindDispatch,
    KindCount
};

struct Routine
{
    uint32_t phys = 0;
    uint16_t addr = 0;
    uint16_t page = 0;
    uint64_t self = 0;
    uint64_t incl = 0;
    uint64_t outerEnter = 0;
    uint32_t onStack = 0;
    uint32_t maxDepth = 0;
    uint64_t entries[KindCount] = {};
    uint64_t jumpIns = 0;      ///< arrivals at the entry not by a call, a return or an interrupt (JP / fall in)
    uint64_t abandoned = 0;    ///< frames dropped by an SP past them (a discarded return address, SP moved)
    uint64_t patched = 0;      ///< returns to an address other than the pushed one (the slot rewritten)
    uint64_t dispatches = 0;   ///< RET with no frame for its slot: a PUSH + RET dispatch from this routine
    std::vector<std::pair<uint32_t, uint64_t>> callers;  ///< caller routine id, count (small, linear)
    // per-frame series bookkeeping
    uint64_t prevSelf = 0;
    uint64_t prevIncl = 0;
};

struct Frame
{
    uint32_t rid;
    uint16_t slot;
    uint16_t ret;
};

struct Options
{
    bool time = true;
    bool wide = true;     ///< 16 B cells (else 8 B with a spill)
    bool stack = false;
    bool beam = false;
    bool frames = false;
    bool splitAck = true; ///< the acknowledge apart (live); false: in the interrupted instruction as L1 books it
};

class Profile
{
public:
    Options opt;
    Memory* mem = nullptr;
    uint32_t frameT = 69888;

    // cells
    void* pages[MAX_PAGES] = {};
    int8_t pageBank[MAX_PAGES];
    uint32_t pagesAllocated = 0;
    void* lastCell = nullptr;
    uint32_t lastOff = 0;
    std::unordered_map<uint32_t, uint64_t> spillT;  ///< 8 B cells: the carries past 2^32 per offset
    uint64_t spills = 0;

    // time
    bool started = false;
    uint32_t lastT = 0;
    uint64_t now = 0;  ///< T-states booked since the start
    uint64_t acks = 0;
    uint64_t ackT = 0;
    uint64_t frames = 0;
    uint64_t instructions = 0;

    // stack
    std::vector<Routine> routines;  ///< 0: the root (outside any discovered routine)
    std::vector<Frame> stack;
    uint64_t topSince = 0;
    uint8_t prevFlow = FlowNone;
    uint16_t prevSp = 0;
    uint16_t prevPc = 0;
    uint64_t stackOverflows = 0;

    // beam
    uint32_t beamMul = 0;
    std::vector<uint64_t> beamT;  ///< routine * kBeamBins (stack) or kBeamBins (no stack)

    // per-frame series
    std::vector<uint8_t> ring;
    size_t ringHead = 0;
    uint64_t ringWraps = 0;
    uint64_t flushNs = 0;
    uint64_t flushMaxNs = 0;
    uint64_t recordsTotal = 0;
    uint32_t recordsMax = 0;

    Profile()
    {
        std::memset(pageBank, -1, sizeof(pageBank));
        routines.emplace_back();
        stack.reserve(1024);
    }
    ~Profile() { Free(); }
    void Free()
    {
        for (auto& p : pages)
        {
            std::free(p);
            p = nullptr;
        }
    }

    void Begin(Memory* m, uint32_t frameTStates)
    {
        mem = m;
        frameT = frameTStates;
        beamMul = static_cast<uint32_t>((uint64_t(kBeamBins) << 32) / frameT);
        beamT.assign(opt.stack ? kBeamBins * 4096 : kBeamBins, 0);
        if (opt.frames)
            ring.assign(4u << 20, 0);
    }

    size_t CellSize() const { return opt.wide ? sizeof(Cell16) : sizeof(Cell8); }

    __attribute__((noinline)) void* AllocPage(uint32_t page, uint16_t pc)
    {
        void* p = std::calloc(kPageCells, CellSize());
        pages[page] = p;
        pageBank[page] = static_cast<int8_t>(pc >> 14);
        ++pagesAllocated;
        return p;
    }

    // ---- routines ----
    uint32_t RoutineAt(uint32_t phys, uint16_t pc)
    {
        Cell16* c = static_cast<Cell16*>(pages[phys >> 14]) + (phys & 0x3FFF);  // allocated: the step booked it
        const uint64_t rid = c->count >> kRidShift;
        if (rid)
            return static_cast<uint32_t>(rid - 1);
        Routine r;
        r.phys = phys;
        r.addr = pc;
        r.page = static_cast<uint16_t>(phys >> 14);
        routines.push_back(std::move(r));
        const uint32_t id = static_cast<uint32_t>(routines.size() - 1);
        c->count |= uint64_t(id + 1) << kRidShift;
        if (beamT.size() < routines.size() * kBeamBins)
            beamT.resize(beamT.size() * 2, 0);
        return id;
    }
    uint32_t Top() const { return stack.empty() ? 0 : stack.back().rid; }

    void CloseSelf()
    {
        routines[Top()].self += now - topSince;
        topSince = now;
    }
    void Push(uint32_t rid, uint16_t slot, uint16_t ret, EntryKind kind)
    {
        CloseSelf();
        const uint32_t caller = Top();
        Routine& r = routines[rid];
        r.entries[kind]++;
        if (r.onStack++ == 0)
            r.outerEnter = now;
        bool found = false;
        for (auto& c : r.callers)
            if (c.first == caller)
            {
                c.second++;
                found = true;
                break;
            }
        if (!found && r.callers.size() < 16)
            r.callers.emplace_back(caller, 1);
        if (stack.size() >= 1024) [[unlikely]]
        {
            stackOverflows++;
            DropFrame(0, false);
        }
        stack.push_back(Frame{rid, slot, ret});
        r.maxDepth = std::max<uint32_t>(r.maxDepth, static_cast<uint32_t>(stack.size()));
    }
    void DropFrame(size_t index, bool abandoned)
    {
        Routine& r = routines[stack[index].rid];
        if (abandoned)
            r.abandoned++;
        if (--r.onStack == 0)
            r.incl += now - r.outerEnter;
        stack.erase(stack.begin() + static_cast<std::ptrdiff_t>(index));
    }
    void PopTop(bool abandoned)
    {
        CloseSelf();
        DropFrame(stack.size() - 1, abandoned);
    }

    /// The previous instruction's flow, resolved by the SP it left (@p spAfter) and where it went (@p next)
    /// @return the instruction arrived by a call or a return
    bool ResolveFlow(uint16_t spAfter, uint16_t next, uint16_t pc, uint32_t phys)
    {
        const uint8_t flow = prevFlow;
        if (flow == FlowCall || flow == FlowRst)
        {
            if (spAfter != static_cast<uint16_t>(prevSp - 2))
                return false;  // a conditional call not taken
            const uint16_t ret = static_cast<uint16_t>(prevPc + (flow == FlowCall ? 3 : 1));
            // the callee is where the call went: the current instruction, or the interrupted one (next)
            const uint32_t calleePhys = next == pc ? phys : mem->ExecPhysOffset(next);
            if (!pages[calleePhys >> 14])
                AllocPage(calleePhys >> 14, next);
            Push(RoutineAt(calleePhys, next), spAfter, ret, flow == FlowCall ? KindCall : KindRst);
            return true;
        }
        if (flow == FlowRet)
        {
            if (spAfter != static_cast<uint16_t>(prevSp + 2))
                return false;  // a conditional return not taken
            const uint16_t slot = prevSp;
            // frames below the slot were left: their return addresses were discarded or SP moved past them
            while (!stack.empty() && stack.back().slot < slot)
                PopTop(true);
            if (!stack.empty() && stack.back().slot == slot)
            {
                const Frame top = stack.back();
                if (top.ret != next)
                    routines[top.rid].patched++;
                PopTop(false);
                // frames of the same slot and return leave together (a dispatch frame over its caller)
                while (!stack.empty() && stack.back().slot == slot && stack.back().ret == top.ret)
                    PopTop(false);
                return true;
            }
            // no frame owns the slot: a PUSH + RET dispatch from the top routine into the target
            routines[Top()].dispatches++;
            const uint32_t calleePhys = next == pc ? phys : mem->ExecPhysOffset(next);
            if (!pages[calleePhys >> 14])
                AllocPage(calleePhys >> 14, next);
            const Frame parent = stack.empty() ? Frame{0, 0xFFFF, 0} : stack.back();
            Push(RoutineAt(calleePhys, next), parent.slot, parent.ret, KindDispatch);
            return true;
        }
        return false;
    }

    // ---- per-frame series ----
    void Append(const void* p, size_t n)
    {
        const uint8_t* b = static_cast<const uint8_t*>(p);
        for (size_t i = 0; i < n; ++i)
        {
            ring[ringHead++] = b[i];
            if (ringHead == ring.size())
            {
                ringHead = 0;
                ringWraps++;
            }
        }
    }
    __attribute__((noinline)) void FlushFrame()
    {
        const uint64_t start = NowNs();
        CloseSelf();
        struct Header
        {
            uint64_t frame;
            uint32_t count;
            uint32_t total;
        } header{frames, 0, 0};
        struct Rec
        {
            uint32_t id, self, incl;
        };
        static thread_local std::vector<Rec> recs;
        recs.clear();
        for (uint32_t id = 0; id < routines.size(); ++id)
        {
            Routine& r = routines[id];
            const uint64_t incl = r.incl + (r.onStack ? now - r.outerEnter : 0);
            if (r.self == r.prevSelf && incl == r.prevIncl)
                continue;
            recs.push_back(Rec{id, static_cast<uint32_t>(r.self - r.prevSelf), static_cast<uint32_t>(incl - r.prevIncl)});
            r.prevSelf = r.self;
            r.prevIncl = incl;
        }
        header.count = static_cast<uint32_t>(recs.size());
        Append(&header, sizeof(header));
        Append(recs.data(), recs.size() * sizeof(Rec));
        recordsTotal += recs.size();
        recordsMax = std::max<uint32_t>(recordsMax, header.count);
        const uint64_t ns = NowNs() - start;
        flushNs += ns;
        flushMaxNs = std::max(flushMaxNs, ns);
    }

    // ---- the step ----
    template <bool kTime, bool kWide, bool kStack, bool kBeam, bool kFrames>
    static void Step(void* ctx, Z80* cpu, uint16_t pc)
    {
        Profile& p = *static_cast<Profile*>(ctx);
        const uint32_t t = cpu->t;
        uint32_t d = 0;
        bool interrupted = false;
        if constexpr (kTime || kStack)
        {
            if (!p.started) [[unlikely]]
            {
                p.started = true;
                p.lastT = t;
                p.acks = cpu->acknowledges;
            }
            const uint32_t lastStart = p.lastT;
            bool wrapped = false;
            if (t < lastStart) [[unlikely]]
            {
                d = t + p.frameT - lastStart;  // the frame ended between the two starts
                wrapped = true;
            }
            else
                d = t - lastStart;
            p.lastT = t;
            uint32_t ack = 0;
            if (cpu->acknowledges != p.acks) [[unlikely]]
            {
                p.acks = cpu->acknowledges;
                interrupted = true;
                if (p.opt.splitAck)
                {
                    ack = std::min(cpu->lastAcknowledgeT, d);
                    d -= ack;
                    p.ackT += ack;
                }
            }
            if (kTime && p.lastCell) [[likely]]
            {
                if constexpr (kWide)
                    static_cast<Cell16*>(p.lastCell)->t += d;
                else
                {
                    Cell8* c = static_cast<Cell8*>(p.lastCell);
                    const uint32_t sum = c->t + d;
                    if (sum < d) [[unlikely]]
                    {
                        p.spills++;
                        p.spillT[p.lastOff] += uint64_t(1) << 32;
                    }
                    c->t = sum;
                }
            }
            p.now += d;
            if constexpr (kBeam)
                p.beamT[(kStack ? size_t(p.Top()) * kBeamBins : 0) + ((uint64_t(lastStart) * p.beamMul) >> 32)] += d;
            if (wrapped) [[unlikely]]
            {
                p.frames++;
                if constexpr (kFrames)
                    p.FlushFrame();
            }
            // the acknowledge: the handler's first instruction carries it, the time base moves past the
            // interrupt frame's push (the handler's routine owns it)
            d = ack;
        }
        const uint32_t off = p.mem->ExecPhysOffset(pc);
        const uint32_t page = off >> 14;
        void* base = p.pages[page];
        if (!base) [[unlikely]]
            base = p.AllocPage(page, pc);
        const bool repeat = cpu->halted != 0;  // a halted CPU's idle fetch: no execution, the HALT's time
        uint64_t ridBits = 0;
        if constexpr (kWide)
        {
            Cell16* c = static_cast<Cell16*>(base) + (off & 0x3FFF);
            if (!repeat)
            {
                ridBits = ++c->count >> kRidShift;
                p.instructions++;
            }
            if constexpr (kTime)
                c->t += d;
            p.lastCell = c;
            p.lastOff = off;
        }
        else
        {
            Cell8* c = static_cast<Cell8*>(base) + (off & 0x3FFF);
            if (!repeat)
            {
                ++c->count;
                p.instructions++;
            }
            if constexpr (kTime)
                c->t += d;
            p.lastCell = c;
            p.lastOff = off;
        }
        if constexpr (kStack)
        {
            if (!repeat)
            {
                const uint16_t sp = cpu->sp;
                uint16_t next = pc;
                uint16_t spAfter = sp;
                uint16_t retAddr = 0;
                if (interrupted) [[unlikely]]
                {
                    retAddr = static_cast<uint16_t>(p.mem->DirectReadFromZ80Memory(sp) |
                                                    (p.mem->DirectReadFromZ80Memory(static_cast<uint16_t>(sp + 1)) << 8));
                    next = retAddr;
                    spAfter = static_cast<uint16_t>(sp + 2);
                }
                bool arrived = false;
                if (p.prevFlow) [[unlikely]]
                    arrived = p.ResolveFlow(spAfter, next, pc, off);
                if (interrupted) [[unlikely]]
                {
                    p.Push(p.RoutineAt(off, pc), sp, retAddr, KindInterrupt);
                    arrived = true;
                }
                if (!arrived && ridBits) [[unlikely]]
                {
                    // ridBits was read before an entry here could be made by this step: re-read
                    const uint32_t rid = static_cast<uint32_t>(ridBits - 1);
                    if (rid != p.Top())
                        p.routines[rid].jumpIns++;
                }
                uint8_t flow = kFlow.main[p.mem->PhysByte(off)];
                if (flow == FlowEd) [[unlikely]]
                    flow = kFlow.ed[p.mem->PhysByte(off + 1)];
                p.prevFlow = flow;
                p.prevSp = sp;
                p.prevPc = pc;
            }
        }
        if constexpr (kTime || kStack)
            p.now += d;  // the acknowledge (0 without one)
    }

    using StepFn = void (*)(void*, Z80*, uint16_t);
    StepFn Fn() const
    {
        if (!opt.time && !opt.stack)
            return &Step<false, true, false, false, false>;
        if (!opt.wide)
            return &Step<true, false, false, false, false>;
        if (!opt.stack)
            return opt.beam ? &Step<true, true, false, true, false> : &Step<true, true, false, false, false>;
        if (opt.beam && opt.frames)
            return &Step<true, true, true, true, true>;
        if (opt.beam)
            return &Step<true, true, true, true, false>;
        if (opt.frames)
            return &Step<true, true, true, false, true>;
        return &Step<true, true, true, false, false>;
    }

    /// Close the open frames' inclusive time at the end (a report reads them so)
    void Finish()
    {
        if (opt.stack)
        {
            CloseSelf();
            for (auto& r : routines)
                if (r.onStack)
                {
                    r.incl += now - r.outerEnter;
                    r.outerEnter = now;
                }
        }
    }

    uint64_t CellCount(uint32_t page, uint32_t i) const
    {
        if (opt.wide)
            return (static_cast<const Cell16*>(pages[page]) + i)->count & kCountMask;
        return (static_cast<const Cell8*>(pages[page]) + i)->count;
    }
    uint64_t CellT(uint32_t page, uint32_t i) const
    {
        if (opt.wide)
            return (static_cast<const Cell16*>(pages[page]) + i)->t;
        return (static_cast<const Cell8*>(pages[page]) + i)->t;
    }

    /// A mark's baseline: copy every allocated page and the routine sums (the cost the design weighs)
    uint64_t MarkCopyNs(std::vector<std::unique_ptr<uint8_t[]>>& baseline) const
    {
        const uint64_t start = NowNs();
        baseline.clear();
        for (uint32_t page = 0; page < MAX_PAGES; ++page)
            if (pages[page])
            {
                std::unique_ptr<uint8_t[]> copy(new uint8_t[kPageCells * CellSize()]);
                std::memcpy(copy.get(), pages[page], kPageCells * CellSize());
                baseline.push_back(std::move(copy));
            }
        std::vector<uint64_t> sums(routines.size() * 2);
        for (size_t i = 0; i < routines.size(); ++i)
        {
            sums[i * 2] = routines[i].self;
            sums[i * 2 + 1] = routines[i].incl;
        }
        const uint64_t ns = NowNs() - start;
        if (sums.empty())
            std::fprintf(stderr, "?");
        return ns;
    }
};

class PocAnalyzer : public IAnalyzer
{
public:
    Profile* profile = nullptr;
    bool idleTolerant = false;
    void onActivate(AnalyzerManager* mgr) override
    {
        mgr->subscribeCPUStep(profile->Fn(), profile, "poc046", idleTolerant);
    }
    void onDeactivate() override {}
    std::string getName() const override { return "poc046"; }
    std::string getUUID() const override { return "poc046"; }
};

// ---------------------------------------------------------------------------
// Variants
// ---------------------------------------------------------------------------
struct Variant
{
    std::string name;
    bool on = false;
    Options opt;
    bool idleTolerant = false;
    /// the core ProfileAnalyzer through profile::Execute (the final code), with these start options
    bool core = false;
    profile::Options coreStart;
    /// profile::Settings::flowSource: 0 the CPU's flow events, 1 the previous opcode, 2 the byte from memory
    uint8_t flowSource = 0;
};

std::vector<Variant> AllVariants()
{
    std::vector<Variant> v;
    v.push_back({"A-off", false, {}, false});
    Options b;
    b.time = false;
    v.push_back({"B-counts", true, b, false});
    Options c;
    v.push_back({"C-time16", true, c, false});
    Options c8;
    c8.wide = false;
    v.push_back({"C8-time8", true, c8, false});
    Options d;
    d.stack = true;
    v.push_back({"D-stack", true, d, false});
    Options e = d;
    e.beam = true;
    v.push_back({"E-beam", true, e, false});
    Options f = d;
    f.frames = true;
    v.push_back({"F-frames", true, f, false});
    Options g = d;
    g.beam = true;
    g.frames = true;
    v.push_back({"G-all", true, g, false});
    v.push_back({"Di-stack-idle", true, d, true});
    v.push_back({"Gi-all-idle", true, g, true});
    // the core analyzer (final A/B): cells only, + stack, + frames (the default), everything
    v.push_back({"K0-core-cells", true, {}, true, true, {{"stack", "false"}, {"frames", "false"}, {"reset", "true"}}});
    v.push_back({"K1-core-stack", true, {}, true, true, {{"stack", "true"}, {"frames", "false"}, {"beam", "false"}, {"reset", "true"}}});
    v.push_back({"K1o-core-stack-opcode", true, {}, true, true, {{"stack", "true"}, {"frames", "false"}, {"beam", "false"}, {"reset", "true"}}, 1});
    v.push_back({"K1m-core-stack-memory", true, {}, true, true, {{"stack", "true"}, {"frames", "false"}, {"beam", "false"}, {"reset", "true"}}, 2});
    v.push_back({"K2-core-default", true, {}, true, true, {{"stack", "true"}, {"frames", "true"}, {"beam", "false"}, {"reset", "true"}}});
    v.push_back({"K3-core-all", true, {}, true, true, {{"stack", "true"}, {"beam", "true"}, {"frames", "true"}, {"reset", "true"}}});
    return v;
}

struct Instance
{
    std::shared_ptr<Emulator> emulator;
    EmulatorContext* context = nullptr;
    /// @p slot "edge.1=kempston,ay-socket=tsfm" fits cards (the recording's slot set)
    bool Make(const std::string& id, const std::string& model, bool timeTravel, const std::string& slot = {})
    {
        std::function<void(CONFIG&)> override;
        if (!slot.empty())
        {
            std::string error;
            std::vector<std::pair<std::string, std::string>> keyValues;
            for (size_t from = 0; from < slot.size();)
            {
                size_t comma = slot.find(',', from);
                if (comma == std::string::npos)
                    comma = slot.size();
                const std::string item = slot.substr(from, comma - from);
                const size_t eq = item.find('=');
                keyValues.emplace_back(item.substr(0, eq), eq == std::string::npos ? std::string{} : item.substr(eq + 1));
                from = comma + 1;
            }
            if (!SlotControl::CreateOverride(keyValues, override, error))
            {
                std::fprintf(stderr, "slot %s: %s\n", slot.c_str(), error.c_str());
                return false;
            }
        }
        emulator = EmulatorManager::GetInstance()->CreateEmulatorWithModel(id, model, LoggerLevel::LogNone, nullptr,
                                                                           override);
        if (!emulator)
            return false;
        context = emulator->GetContext();
        context->pFeatureManager->setFeature(Features::kTimeTravel, timeTravel);
        context->pMemory->UpdateFeatureCache();
        context->pSoundManager->UpdateFeatureCache();
        context->pSoundManager->mute();  // an instance starts muted; said again: this tool never plays audio
        return true;
    }
    ~Instance()
    {
        if (emulator)
            EmulatorManager::GetInstance()->RemoveEmulator(emulator->GetUUID());
    }
};

std::string Arg(int argc, char** argv, const char* name, const char* def)
{
    for (int i = 2; i + 1 < argc; ++i)
        if (std::strcmp(argv[i], name) == 0)
            return argv[i + 1];
    return def;
}

struct RunResult
{
    double ms = 0;
    uint64_t instructions = 0;
    uint32_t pages = 0;
    size_t routines = 0;
    uint64_t flushNs = 0, flushMaxNs = 0, records = 0, frames = 0, markNs = 0;
    uint32_t recordsMax = 0;
    uint64_t halted = 0, now = 0;
};

RunResult RunOnce(Instance& s, const std::string& snapshot, const Variant& v, unsigned frames)
{
    RunResult r;
    if (!s.emulator->LoadSnapshot(snapshot))
    {
        std::fprintf(stderr, "cannot load %s\n", snapshot.c_str());
        std::exit(2);
    }
    if (s.emulator->IsPaused())
        s.emulator->Resume();
    AnalyzerManager* am = s.context->pDebugManager->GetAnalyzerManager();
    if (v.core)
    {
        {
            // start keeps the settings it does not name: the flow source is set beforehand
            auto* analyzer = static_cast<ProfileAnalyzer*>(am->getAnalyzer(ProfileAnalyzer::kId));
            profile::Settings settings = analyzer->ReadTotals().settings;
            settings.flowSource = v.flowSource;
            std::string error;
            if (!analyzer->Configure(settings, error))
            {
                std::fprintf(stderr, "core configure: %s\n", error.c_str());
                std::exit(2);
            }
        }
        const profile::Reply started = profile::Execute(*s.emulator, "start", v.coreStart);
        if (!started.Ok())
        {
            std::fprintf(stderr, "core start: %s\n", started.message.c_str());
            std::exit(2);
        }
        const double start = ThreadCpuMs();
        s.emulator->RunNFrames(frames, true);
        r.ms = ThreadCpuMs() - start;
        profile::Execute(*s.emulator, "stop");
        auto* analyzer = static_cast<ProfileAnalyzer*>(am->getAnalyzer(ProfileAnalyzer::kId));
        const profile::Totals t = analyzer->ReadTotals();
        r.instructions = t.instructions;
        r.pages = t.pages;
        r.routines = t.routines;
        r.frames = t.frames;
        r.now = t.tstates;
        uint64_t best = UINT64_MAX;
        for (int i = 0; i < 5; ++i)
        {
            const auto m0 = std::chrono::steady_clock::now();
            std::string error;
            analyzer->Mark("bench", error);
            best = std::min<uint64_t>(best, static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                                                      std::chrono::steady_clock::now() - m0)
                                                                      .count()));
        }
        r.markNs = best;
        return r;
    }
    Profile profile;
    profile.opt = v.opt;
    if (v.on)
    {
        profile.Begin(s.context->pMemory, s.context->GetFrameTStates());
        auto a = std::make_unique<PocAnalyzer>();
        a->profile = &profile;
        a->idleTolerant = v.idleTolerant;
        am->registerAnalyzer("poc046", std::move(a));
        am->activate("poc046");
    }
    const uint64_t haltedBefore = s.context->pCore->GetZ80()->interruptsAcceptedHalted;
    const double start = ThreadCpuMs();
    s.emulator->RunNFrames(frames, true);
    r.ms = ThreadCpuMs() - start;
    r.halted = s.context->pCore->GetZ80()->interruptsAcceptedHalted - haltedBefore;
    if (v.on)
    {
        am->deactivate("poc046");
        am->unregisterAnalyzer("poc046");
        profile.Finish();
        r.instructions = profile.instructions;
        r.pages = profile.pagesAllocated;
        r.routines = profile.routines.size() - 1;
        r.flushNs = profile.flushNs;
        r.flushMaxNs = profile.flushMaxNs;
        r.records = profile.recordsTotal;
        r.recordsMax = profile.recordsMax;
        r.frames = profile.frames;
        r.now = profile.now;
        std::vector<std::unique_ptr<uint8_t[]>> baseline;
        uint64_t best = UINT64_MAX;
        for (int i = 0; i < 5; ++i)
            best = std::min(best, profile.MarkCopyNs(baseline));
        r.markNs = best;
    }
    return r;
}

int Bench(int argc, char** argv)
{
    const std::string snapshot = Arg(argc, argv, "--snapshot", "");
    const std::string model = Arg(argc, argv, "--model", "48K");
    const unsigned frames = static_cast<unsigned>(std::atoi(Arg(argc, argv, "--frames", "500").c_str()));
    const int rounds = std::atoi(Arg(argc, argv, "--rounds", "6").c_str());
    const std::string only = Arg(argc, argv, "--variants", "");
    std::vector<Variant> variants;
    for (auto& v : AllVariants())
        if (only.empty() || ("," + only + ",").find("," + v.name.substr(0, v.name.find('-')) + ",") != std::string::npos)
            variants.push_back(v);
    Instance s;
    if (!s.Make("poc046-bench", model, false))
        return 2;
    // warm-up
    RunOnce(s, snapshot, variants[0], frames / 4 + 1);
    std::map<std::string, std::vector<RunResult>> results;
    for (int round = 0; round < rounds; ++round)
    {
        std::vector<size_t> order(variants.size());
        for (size_t i = 0; i < order.size(); ++i)
            order[i] = round % 2 ? order.size() - 1 - i : i;
        for (size_t i : order)
            results[variants[i].name].push_back(RunOnce(s, snapshot, variants[i], frames));
    }
    // report: median ms, % vs A-off, ns per instruction
    auto median = [](std::vector<double> x) {
        std::sort(x.begin(), x.end());
        return x.empty() ? 0.0 : (x.size() % 2 ? x[x.size() / 2] : (x[x.size() / 2 - 1] + x[x.size() / 2]) / 2);
    };
    uint64_t instructions = 0;
    for (auto& [name, rs] : results)
        if (rs[0].instructions)
            instructions = rs[0].instructions;
    const double offMs = results.count("A-off") ? median([&] {
        std::vector<double> x;
        for (auto& r : results["A-off"])
            x.push_back(r.ms);
        return x;
    }()) : 0;
    std::printf("# snapshot %s model %s frames %u rounds %d instructions %" PRIu64 "\n", snapshot.c_str(), model.c_str(),
                frames, rounds, instructions);
    std::printf("%-14s %9s %9s %9s %8s %8s %6s %8s %9s %9s %8s %8s %10s\n", "variant", "med_ms", "min_ms", "max_ms", "pct",
                "ns/inst", "pages", "routines", "flush_us", "flushmax", "rec/fr", "recmax", "mark_us");
    for (auto& v : variants)
    {
        auto& rs = results[v.name];
        std::vector<double> ms;
        for (auto& r : rs)
            ms.push_back(r.ms);
        const double med = median(ms);
        const RunResult& r0 = rs[0];
        const double pct = offMs > 0 ? (med - offMs) / offMs * 100.0 : 0;
        const double nsPer = instructions ? (med - offMs) * 1e6 / static_cast<double>(instructions) : 0;
        std::printf("%-14s %9.2f %9.2f %9.2f %+7.2f%% %8.2f %6u %8zu %9.2f %9.2f %8.1f %8u %10.1f\n", v.name.c_str(), med,
                    *std::min_element(ms.begin(), ms.end()), *std::max_element(ms.begin(), ms.end()), pct, nsPer,
                    r0.pages, r0.routines, r0.frames ? r0.flushNs / 1000.0 / r0.frames : 0, r0.flushMaxNs / 1000.0,
                    r0.frames ? double(r0.records) / r0.frames : 0, r0.recordsMax, r0.markNs / 1000.0);
    }
    // the raw rounds
    std::printf("# raw ms per round\n");
    for (auto& v : variants)
    {
        std::printf("%-14s", v.name.c_str());
        for (auto& r : results[v.name])
            std::printf(" %.2f", r.ms);
        std::printf("\n");
    }
    return 0;
}

// ---------------------------------------------------------------------------
// Replay of a recording (accuracy against L1)
// ---------------------------------------------------------------------------
void WriteJson(const Profile& p, const std::string& path, uint64_t firstFrame, uint64_t lastFrame)
{
    FILE* f = std::fopen(path.c_str(), "w");
    if (!f)
    {
        std::fprintf(stderr, "cannot write %s\n", path.c_str());
        std::exit(2);
    }
    std::fprintf(f, "{\"format\":\"poc046 profile\",\"first_frame\":%" PRIu64 ",\"last_frame\":%" PRIu64
                    ",\"frames\":%" PRIu64 ",\"instructions\":%" PRIu64 ",\"tstates\":%" PRIu64
                    ",\"acks\":%" PRIu64 ",\"ack_tstates\":%" PRIu64 ",\"pages\":%u,\"stack_overflows\":%" PRIu64 ",\n",
                 firstFrame, lastFrame, p.frames, p.instructions, p.now, p.acks, p.ackT, p.pagesAllocated, p.stackOverflows);
    std::fprintf(f, "\"instructions_rows\":[");
    bool first = true;
    for (uint32_t page = 0; page < MAX_PAGES; ++page)
    {
        if (!p.pages[page])
            continue;
        for (uint32_t i = 0; i < kPageCells; ++i)
        {
            const uint64_t c = p.CellCount(page, i), t = p.CellT(page, i);
            if (!c && !t)
                continue;
            const uint32_t addr = (static_cast<uint32_t>(p.pageBank[page] & 3) << 14) | i;
            std::fprintf(f, "%s[%u,%u,%" PRIu64 ",%" PRIu64 ",%u]", first ? "" : ",", addr, page, c, t,
                         p.mem->PhysByte((page << 14) | i));
            first = false;
        }
    }
    std::fprintf(f, "],\n\"routines\":[");
    for (size_t id = 1; id < p.routines.size(); ++id)
    {
        const Routine& r = p.routines[id];
        std::fprintf(f, "%s{\"addr\":%u,\"page\":%u,\"self\":%" PRIu64 ",\"incl\":%" PRIu64 ",\"calls\":%" PRIu64
                        ",\"rsts\":%" PRIu64 ",\"ints\":%" PRIu64 ",\"dispatches_in\":%" PRIu64 ",\"jump_ins\":%" PRIu64
                        ",\"abandoned\":%" PRIu64 ",\"patched\":%" PRIu64 ",\"dispatches_out\":%" PRIu64
                        ",\"max_depth\":%u,\"callers\":[",
                     id == 1 ? "" : ",\n", r.addr, r.page, r.self, r.incl, r.entries[KindCall], r.entries[KindRst],
                     r.entries[KindInterrupt], r.entries[KindDispatch], r.jumpIns, r.abandoned, r.patched, r.dispatches,
                     r.maxDepth);
        for (size_t i = 0; i < r.callers.size(); ++i)
            std::fprintf(f, "%s[%u,%" PRIu64 "]", i ? "," : "", r.callers[i].first, r.callers[i].second);
        std::fprintf(f, "]}");
    }
    std::fprintf(f, "],\n\"root_self\":%" PRIu64 "}\n", p.routines[0].self);
    std::fclose(f);
}

int Replay(int argc, char** argv)
{
    const std::string session = Arg(argc, argv, "--session", "");
    const std::string model = Arg(argc, argv, "--model", "48K");
    const std::string out = Arg(argc, argv, "--out", "profile.json");
    const uint64_t maxFrames = std::strtoull(Arg(argc, argv, "--frames", "0").c_str(), nullptr, 10);
    const std::string variantName = Arg(argc, argv, "--variant", "D");
    Instance src;
    if (!src.Make("poc046-src", model, true, Arg(argc, argv, "--slot", "")))
        return 2;
    std::string error;
    ttd::TimeTravelController* ttd = src.context->pTimeTravelController;
    if (!ttd || !ttd->DeserializeSessionFile(session, error))
    {
        std::fprintf(stderr, "cannot load the session %s: %s\n", session.c_str(), error.c_str());
        return 2;
    }
    analysis::TraceBundle bundle;
    if (!analysis::TraceBundle::Capture(*src.emulator, bundle, error))
    {
        std::fprintf(stderr, "capture: %s\n", error.c_str());
        return 2;
    }
    analysis::ReplayWindow window;
    if (!window.Open(bundle, "poc046-replay", ttd::TTDTimePoint{0, 0}, error))
    {
        std::fprintf(stderr, "open: %s\n", error.c_str());
        return 2;
    }
    uint64_t end = window.SessionEnd().frame;
    if (maxFrames && end > window.Start().frame + maxFrames)
        end = window.Start().frame + maxFrames;
    // --save-at F --save PATH: the recorded machine at frame F as a snapshot (a live workload mid-game), nothing else
    const uint64_t saveAt = std::strtoull(Arg(argc, argv, "--save-at", "0").c_str(), nullptr, 10);
    if (saveAt)
    {
        const std::string savePath = Arg(argc, argv, "--save", "");
        window.RunTo(saveAt, 250, analysis::ReplayHooks{});
        Emulator* clone = window.Context()->pEmulator;
        const bool saved = clone && clone->SaveSnapshot(savePath);
        std::fprintf(stderr, "frame %" PRIu64 " saved to %s: %s\n", window.State().frame_counter, savePath.c_str(),
                     saved ? "ok" : "failed");
        return saved ? 0 : 2;
    }
    if (Arg(argc, argv, "--core", "0") == "1")
    {
        // the core ProfileAnalyzer on the replay clone (the final code), exported as the kit's L1 subset
        Emulator* clone = window.Context()->pEmulator;
        AnalyzerManager* am = window.Context()->pDebugManager->GetAnalyzerManager();
        auto* analyzer = static_cast<ProfileAnalyzer*>(am->getAnalyzer(ProfileAnalyzer::kId));
        profile::Settings settings;
        settings.frames = false;
        settings.countReplay = true;
        if (!analyzer || !analyzer->Configure(settings, error) || !am->activate(ProfileAnalyzer::kId))
        {
            std::fprintf(stderr, "core profile: %s\n", error.c_str());
            return 2;
        }
        const double start = ThreadCpuMs();
        const auto run = window.RunTo(end, 250, analysis::ReplayHooks{});
        const double ms = ThreadCpuMs() - start;
        am->deactivate(ProfileAnalyzer::kId);
        const profile::Totals t = analyzer->ReadTotals();
        const profile::Reply exported = profile::Execute(*clone, "export", {{"format", "kit"}, {"path", out}});
        std::fprintf(stderr, "core replay %s frames %" PRIu64 "..%" PRIu64 " run %d in %.0f ms: %" PRIu64
                             " instructions, %" PRIu64 " T, %u routines, %u pages, %" PRIu64 " discontinuities: %s\n",
                     session.c_str(), window.Start().frame, end, static_cast<int>(run), ms, t.instructions, t.tstates,
                     t.routines, t.pages, t.discontinuities, exported.Ok() ? exported.text.c_str() : exported.message.c_str());
        return exported.Ok() ? 0 : 2;
    }
    Variant v;
    for (auto& x : AllVariants())
        if (x.name.substr(0, x.name.find('-')) == variantName)
            v = x;
    Profile profile;
    profile.opt = v.opt;
    profile.opt.splitAck = Arg(argc, argv, "--split-ack", "0") == "1";
    profile.Begin(window.Mem(), window.FrameT());
    const Profile::StepFn fn = profile.Fn();
    Z80* cpu = window.Cpu();
    analysis::ReplayHooks hooks;
    hooks.instruction = [&](uint16_t pc) { fn(&profile, cpu, pc); };
    const double start = ThreadCpuMs();
    const auto run = window.RunTo(end, 250, hooks);
    const double ms = ThreadCpuMs() - start;
    profile.Finish();
    std::fprintf(stderr, "replay %s frames %" PRIu64 "..%" PRIu64 " run %d in %.0f ms: %" PRIu64 " instructions, %" PRIu64
                         " T, %zu routines, %u pages\n",
                 session.c_str(), window.Start().frame, end, static_cast<int>(run), ms, profile.instructions, profile.now,
                 profile.routines.size() - 1, profile.pagesAllocated);
    WriteJson(profile, out, window.Start().frame, end);
    return 0;
}
}  // namespace

int main(int argc, char** argv)
{
    if (argc < 2)
    {
        std::fprintf(stderr, "usage: poc046-profile bench|replay ...\n");
        return 1;
    }
    if (std::strcmp(argv[1], "bench") == 0)
        return Bench(argc, argv);
    if (std::strcmp(argv[1], "replay") == 0)
        return Replay(argc, argv);
    return 1;
}
