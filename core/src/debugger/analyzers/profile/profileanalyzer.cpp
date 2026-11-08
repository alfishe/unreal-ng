#include "debugger/analyzers/profile/profileanalyzer.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>

#include "common/filehelper.h"
#include "debugger/analyzers/analyzermanager.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulatorcontext.h"
#include "emulator/memory/memory.h"

using namespace profile;

namespace
{
constexpr uint32_t kPageCells = 0x4000;
constexpr uint32_t kRidShift = 40;
constexpr uint64_t kCountMask = (uint64_t(1) << kRidShift) - 1;

/// How an instruction moves the control flow, as the shadow stack needs it
enum Flow : uint8_t
{
    FlowNone = 0,
    FlowCall = 1,  ///< CALL nn, CALL cc,nn (3 bytes)
    FlowRst = 2,   ///< RST n (1 byte)
    FlowRet = 3,   ///< RET, RET cc, RETN, RETI
    FlowEd = 4,    ///< the ED prefix: the second byte decides
};

struct FlowTable
{
    uint8_t main[256] = {};
    uint8_t ed[256] = {};
    constexpr FlowTable()
    {
        main[0xCD] = FlowCall;
        for (int op = 0xC4; op <= 0xFC; op += 8)
            main[op] = FlowCall;
        for (int op = 0xC7; op <= 0xFF; op += 8)
            main[op] = FlowRst;
        for (int op = 0xC0; op <= 0xF8; op += 8)
            main[op] = FlowRet;
        main[0xC9] = FlowRet;
        main[0xED] = FlowEd;
        for (int op = 0x45; op <= 0x7D; op += 8)
            ed[op] = FlowRet;
    }
};
constexpr FlowTable kFlow;

uint64_t Load(const std::atomic<uint64_t>& a)
{
    return a.load(std::memory_order_relaxed);
}
}  // namespace

ProfileAnalyzer::ProfileAnalyzer(EmulatorContext* context) : _context(context)
{
    std::memset(_pageWindow, -1, sizeof(_pageWindow));
    _stack.reserve(kMaxDepth);
}

ProfileAnalyzer::~ProfileAnalyzer()
{
    StopStream();
    for (auto& page : _pages)
        delete[] page.load(std::memory_order_relaxed);
    for (auto& chunk : _chunks)
        delete[] chunk.load(std::memory_order_relaxed);
}

bool ProfileAnalyzer::Validate(const Settings& settings, std::string& error)
{
    if (settings.ringKiB < 64 || settings.ringKiB > 262144)
    {
        error = "ring_kib must be 64..262144";
        return false;
    }
    if ((settings.beam || settings.frames) && !settings.stack)
    {
        error = "beam and frames need the call stack (stack=true)";
        return false;
    }
    return true;
}

bool ProfileAnalyzer::Configure(const Settings& settings, std::string& error)
{
    if (_active.load(std::memory_order_relaxed))
    {
        error = "the profile is running: stop it before changing its settings";
        return false;
    }
    if (!Validate(settings, error))
        return false;
    if (settings.stack != _settings.stack)
        _fresh = true;  // the routines are the stack's: a window without them cannot go on with them
    _settings = settings;
    return true;
}

void ProfileAnalyzer::Reset()
{
    StopStream();
    for (auto& page : _pages)
        if (Cell* cells = page.load(std::memory_order_relaxed))
            for (uint32_t i = 0; i < kPageCells; ++i)
            {
                cells[i].count.store(0, std::memory_order_relaxed);
                cells[i].t.store(0, std::memory_order_relaxed);
            }
    // the routines go: their ids live in the cells' high bits, cleared above
    const uint32_t routines = _chunks[0].load(std::memory_order_relaxed) ? _routineCount.load(std::memory_order_relaxed) : 0;
    for (uint32_t id = 0; id < routines; ++id)
    {
        Routine& r = *RoutinePtr(id);
        r.~Routine();
        new (&r) Routine();
    }
    _routineCount.store(1, std::memory_order_release);
    _stack.clear();
    _depth.store(0, std::memory_order_relaxed);
    _lastCell = nullptr;
    _started = false;
    _prevFlow = FlowNone;
    _topSince.store(0, std::memory_order_relaxed);
    _top.store(0, std::memory_order_relaxed);
    for (auto* a : {&_now, &_instructions, &_acks, &_ackT, &_frames, &_discontinuities, &_stackOverflows,
                    &_routineOverflows, &_streamed, &_streamLost})
        a->store(0, std::memory_order_relaxed);
    _ringHead.store(0, std::memory_order_relaxed);
    _indexHead.store(0, std::memory_order_relaxed);
    {
        std::lock_guard<std::mutex> lock(_marksMutex);
        _marks.clear();
    }
    _startFrame.store(_context ? _context->emulatorState.frame_counter : 0, std::memory_order_relaxed);
    _fresh = false;
}

void ProfileAnalyzer::onActivate(AnalyzerManager* manager)
{
    _manager = manager;
    _memory = _context ? _context->pMemory : nullptr;
    if (_fresh || _routineCount.load(std::memory_order_relaxed) == 0)
        Reset();
    // the root routine's chunk
    if (!_chunks[0].load(std::memory_order_relaxed))
        _chunks[0].store(new Routine[kChunk], std::memory_order_release);
    _frameT = _context ? _context->GetFrameTStates() : 69888;
    if (_frameT == 0)
        _frameT = 69888;
    _frameTPublished.store(_frameT, std::memory_order_relaxed);
    _beamMul = static_cast<uint32_t>((uint64_t(kBeamBins) << 32) / _frameT);
    if (_settings.frames)
    {
        uint64_t words = 1;
        while (words * 4 < uint64_t(_settings.ringKiB) * 1024)
            words <<= 1;
        if (words != _ringWords)
        {
            _ring.reset(new std::atomic<uint32_t>[words]);
            _ringWords = words;
            _ringHead.store(0, std::memory_order_relaxed);
            _indexHead.store(0, std::memory_order_relaxed);
        }
        if (!_index)
            _index.reset(new FrameIndex[kIndexSize]);
    }
    _started = false;  // a stop and a start between: the time between is not the CPU's
    _lastCell = nullptr;
    ClearStack();
    _active.store(true, std::memory_order_release);
    // The idle stretch of a halted CPU is one delta booked to its HALT: the CPU may run it in one go
    manager->subscribeCPUStep(PickStep(), this, kId, /*idleTolerant=*/true);
    // the call stack's transfers from the CPU: only CALL / RST / RET-family instructions pay for them
    if (_settings.stack && _settings.flowSource == 0 && _context && _context->pCore)
    {
        _hookedCpu = _context->pCore->GetZ80();
        _hookedCpu->SetFlowHook(&OnFlow, this);
    }
    if (!_settings.stream.empty())
    {
        _streamStop.store(false, std::memory_order_relaxed);
        _streamThread = std::thread([this] { StreamLoop(); });
    }
}

void ProfileAnalyzer::onDeactivate()
{
    // the subscription is removed by the manager; the last instruction's time and the open frames' time are closed now
    Settle();
    if (_settings.stack)
    {
        CloseSelf();
        const uint64_t now = Load(_now);
        const uint32_t routines = _routineCount.load(std::memory_order_relaxed);
        for (uint32_t id = 0; id < routines; ++id)
        {
            Routine& r = *RoutinePtr(id);
            if (r.onStack.load(std::memory_order_relaxed))
            {
                Add(r.inclusive, now - Load(r.outerEnter));
                r.outerEnter.store(now, std::memory_order_relaxed);
            }
        }
    }
    if (_hookedCpu)
    {
        if (_hookedCpu->flowHookContext == this)
            _hookedCpu->SetFlowHook(nullptr, nullptr);
        _hookedCpu = nullptr;
    }
    _active.store(false, std::memory_order_release);
    StopStream();
    _manager = nullptr;
}

template <bool kStack, bool kBeam, bool kFrames>
ProfileAnalyzer::StepFn ProfileAnalyzer::PickFlow() const
{
    if (_settings.flowSource == 1)
        return &Step<kStack, kBeam, kFrames, 1>;
    if (_settings.flowSource == 2)
        return &Step<kStack, kBeam, kFrames, 2>;
    return &Step<kStack, kBeam, kFrames, 0>;
}

ProfileAnalyzer::StepFn ProfileAnalyzer::PickStep() const
{
    if (!_settings.stack)
        return &Step<false, false, false, 0>;
    if (_settings.beam && _settings.frames)
        return PickFlow<true, true, true>();
    if (_settings.beam)
        return PickFlow<true, true, false>();
    if (_settings.frames)
        return PickFlow<true, false, true>();
    return PickFlow<true, false, false>();
}

ProfileAnalyzer::Cell* ProfileAnalyzer::AllocPage(uint32_t page, uint16_t pc)
{
    Cell* cells = new Cell[kPageCells];
    _pageWindow[page] = static_cast<int8_t>(pc >> 14);
    _pages[page].store(cells, std::memory_order_release);
    _pagesAllocated.fetch_add(1, std::memory_order_relaxed);
    return cells;
}

/// region <Routines and the shadow stack>

uint32_t ProfileAnalyzer::RoutineAt(uint32_t phys, uint16_t pc)
{
    Cell* page = _pages[phys >> 14].load(std::memory_order_relaxed);
    if (!page)
        page = AllocPage(phys >> 14, pc);
    Cell& cell = page[phys & 0x3FFF];
    const uint64_t bits = Load(cell.count);
    if (const uint64_t rid = bits >> kRidShift)
        return static_cast<uint32_t>(rid - 1);
    const uint32_t id = _routineCount.load(std::memory_order_relaxed);
    if (id >= kMaxRoutines)
    {
        Inc(_routineOverflows);
        return Top();
    }
    if (id % kChunk == 0 && !_chunks[id / kChunk].load(std::memory_order_relaxed))
        _chunks[id / kChunk].store(new Routine[kChunk], std::memory_order_release);
    Routine& r = *RoutinePtr(id);
    r.phys = phys;
    r.address = pc;
    r.page = static_cast<uint16_t>(phys >> 14);
    cell.count.store(bits | (uint64_t(id + 1) << kRidShift), std::memory_order_relaxed);
    _routineCount.store(id + 1, std::memory_order_release);
    return id;
}

void ProfileAnalyzer::CloseSelf()
{
    const uint64_t now = Load(_now);
    Add(RoutinePtr(Top())->self, now - Load(_topSince));
    _topSince.store(now, std::memory_order_relaxed);
}

void ProfileAnalyzer::PublishTop()
{
    _top.store(Top(), std::memory_order_relaxed);
}

uint64_t ProfileAnalyzer::SelfNow(uint32_t id, const Routine& r, uint64_t now) const
{
    // the routine on top has run since the last transition: its open stretch counts
    const uint64_t self = Load(r.self);
    if (_active.load(std::memory_order_relaxed) && id == _top.load(std::memory_order_relaxed))
    {
        const uint64_t since = Load(_topSince);
        return now > since ? self + (now - since) : self;
    }
    return self;
}

void ProfileAnalyzer::Push(uint32_t rid, uint16_t slot, uint16_t ret, EntryKind kind)
{
    CloseSelf();
    const uint32_t caller = Top();
    Routine& r = *RoutinePtr(rid);
    Inc(r.entries[kind]);
    if (r.onStack.load(std::memory_order_relaxed) == 0)
        r.outerEnter.store(Load(_now), std::memory_order_relaxed);
    r.onStack.store(r.onStack.load(std::memory_order_relaxed) + 1, std::memory_order_relaxed);
    bool counted = false;
    for (uint32_t i = 0; i < kCallerSlots; ++i)
    {
        const uint32_t id = r.callerId[i].load(std::memory_order_relaxed);
        if (id == caller + 1)
        {
            Inc(r.callerCount[i]);
            counted = true;
            break;
        }
        if (id == 0)
        {
            r.callerCount[i].store(1, std::memory_order_relaxed);
            r.callerId[i].store(caller + 1, std::memory_order_release);
            counted = true;
            break;
        }
    }
    if (!counted)
        Inc(r.callersMore);
    if (_stack.size() >= kMaxDepth) [[unlikely]]
    {
        Inc(_stackOverflows);
        DropFrame(0, false);
    }
    _stack.push_back(Frame{rid, slot, ret, static_cast<uint8_t>(kind)});
    PublishTop();
    const uint32_t depth = static_cast<uint32_t>(_stack.size());
    _depth.store(depth, std::memory_order_relaxed);
    if (depth > r.maxDepth.load(std::memory_order_relaxed))
        r.maxDepth.store(depth, std::memory_order_relaxed);
}

void ProfileAnalyzer::DropFrame(size_t index, bool abandoned)
{
    Routine& r = *RoutinePtr(_stack[index].rid);
    if (abandoned)
        Inc(r.abandoned);
    const uint32_t on = r.onStack.load(std::memory_order_relaxed) - 1;
    if (on == 0)
        Add(r.inclusive, Load(_now) - Load(r.outerEnter));
    r.onStack.store(on, std::memory_order_relaxed);
    _stack.erase(_stack.begin() + static_cast<std::ptrdiff_t>(index));
    PublishTop();
    _depth.store(static_cast<uint32_t>(_stack.size()), std::memory_order_relaxed);
}

void ProfileAnalyzer::PopTop(bool abandoned)
{
    CloseSelf();
    DropFrame(_stack.size() - 1, abandoned);
}

void ProfileAnalyzer::ClearStack()
{
    if (!_chunks[0].load(std::memory_order_relaxed))
        return;
    CloseSelf();
    while (!_stack.empty())
        DropFrame(_stack.size() - 1, false);
    _prevFlow = FlowNone;
}

bool ProfileAnalyzer::ResolveFlow(uint8_t flow, uint16_t spAfter, uint16_t next, uint16_t pc, uint32_t phys)
{
    if (flow == FlowCall || flow == FlowRst)
    {
        if (spAfter != static_cast<uint16_t>(_prevSp - 2))
            return false;  // a conditional call not taken
        const uint16_t ret = static_cast<uint16_t>(_prevPc + (flow == FlowCall ? 3 : 1));
        Called(next, next == pc ? phys : _memory->ExecPhysOffset(next), spAfter, ret,
               flow == FlowCall ? EntryCall : EntryRst);
        return true;
    }
    if (flow != FlowRet || spAfter != static_cast<uint16_t>(_prevSp + 2))
        return false;  // a conditional return not taken
    Returned(next, next == pc ? phys : _memory->ExecPhysOffset(next), _prevSp);
    return true;
}

void ProfileAnalyzer::Called(uint16_t target, uint32_t phys, uint16_t slot, uint16_t ret, EntryKind kind)
{
    AbandonAt(slot);
    Push(RoutineAt(phys, target), slot, ret, kind);
}

void ProfileAnalyzer::Returned(uint16_t target, uint32_t phys, uint16_t slot)
{
    // frames below the slot were left: their return addresses discarded, or SP moved past them
    while (!_stack.empty() && _stack.back().slot < slot)
        PopTop(true);
    if (!_stack.empty() && _stack.back().slot == slot)
    {
        const Frame top = _stack.back();
        if (top.ret != target)
            Inc(RoutinePtr(top.rid)->patched);
        PopTop(false);
        // frames of the same slot and return leave together (a dispatch frame over the routine it came from)
        while (!_stack.empty() && _stack.back().slot == slot && _stack.back().ret == top.ret)
            PopTop(false);
        return;
    }
    // no frame owns the slot: a PUSH + RET dispatch (or a RET chain) from the top routine into the target, which
    // returns where the top routine would
    Inc(RoutinePtr(Top())->dispatchesOut);
    // a dispatched routine that dispatches again (a RET chain, a RET-driven main loop, a coroutine switch) has left:
    // it returned into the next one, which replaces it. Only the first dispatch runs inside the routine that started
    // the chain, so a chain is at most one frame deep, whichever stack its RETs run on
    if (!_stack.empty() && _stack.back().kind == EntryDispatch)
        PopTop(false);
    const Frame parent = _stack.empty() ? Frame{0, kRootSlot, 0, EntryDispatch} : _stack.back();
    Push(RoutineAt(phys, target), parent.slot, parent.ret, EntryDispatch);
}

void ProfileAnalyzer::OnFlow(void* context, Z80* cpu, Z80::FlowEvent event, uint16_t target, uint16_t slot,
                             uint16_t ret)
{
    ProfileAnalyzer& p = *static_cast<ProfileAnalyzer*>(context);
    if (!p._started || (p._context->ttdReplayActive && !p._settings.countReplay)) [[unlikely]]
        return;
    // the instruction's time so far (all of it: the event comes at its end) is booked now, as the next step would
    // book it, so the instruction stays with the routine it ran in and the transfer happens after it
    const uint32_t t = cpu->t;
    const uint32_t lastStart = p._lastT;
    if (t >= lastStart && t - lastStart <= p._frameT && p._lastCell) [[likely]]
    {
        const uint32_t d = t - lastStart;
        Add(p._lastCell->t, d);
        Add(p._now, d);
        if (p._settings.beam)
            Add(p.RoutinePtr(p.Top())->beam[(uint64_t(lastStart) * p._beamMul) >> 32], d);
        p._lastT = t;
    }
    const uint32_t phys = p._memory->ExecPhysOffset(target);
    if (event == Z80::FlowEvent::Ret)
        p.Returned(target, phys, slot);
    else
        p.Called(target, phys, slot, ret, event == Z80::FlowEvent::Call ? EntryCall : EntryRst);
    p._arrivedAt = Load(p._instructions);
}

void ProfileAnalyzer::AbandonAt(uint16_t slot)
{
    // a push at slot overwrites the return address of a frame there and lies past every frame below it: those
    // frames were left (POP of the return address, SP moved) without a return. kRootSlot frames hold no slot
    while (!_stack.empty() && _stack.back().slot <= slot && _stack.back().slot != kRootSlot)
        PopTop(true);
}

void ProfileAnalyzer::Settle()
{
    // The CPU is held at an instruction start (a stop holds it, a replay run has returned): the time since the last
    // step is the last instruction's, booked as the next step would book it, and a frame that ended meanwhile is
    // closed. Without this a window of N frames would show N - 1 and miss its last instruction's T-states
    if (!_started || !_lastCell || !_context || !_context->pCore || !_memory)
        return;
    if (_context->ttdReplayActive && !_settings.countReplay)
        return;
    const Z80* cpu = _context->pCore->GetZ80();
    if (!cpu)
        return;
    const uint32_t t = cpu->t;
    const uint32_t lastStart = _lastT;
    uint32_t d;
    bool wrapped = false;
    if (t < lastStart)
    {
        d = t + _frameT - lastStart;
        wrapped = true;
        if (_context->emulatorState.frame_counter != _lastFrameCounter + 1 || d > _frameT)
            return;  // moved under the profile: nothing to book, the next start begins afresh
        _lastFrameCounter = _context->emulatorState.frame_counter;
    }
    else
    {
        d = t - lastStart;
        if (d > _frameT)
            return;
    }
    _lastT = t;
    if (cpu->acknowledges != _ackSeen)
    {
        // an interrupt accepted after the last instruction: its acknowledge, apart (its handler has not run yet)
        _ackSeen = cpu->acknowledges;
        const uint32_t ack = std::min(cpu->lastAcknowledgeT, d);
        d -= ack;
        Inc(_acks);
        Add(_ackT, ack);
    }
    Add(_lastCell->t, d);
    Add(_now, d);
    if (_settings.beam && _settings.stack)
        Add(RoutinePtr(Top())->beam[(uint64_t(lastStart) * _beamMul) >> 32], d);
    if (wrapped)
    {
        Inc(_frames);
        if (_settings.frames && _settings.stack)
            FlushFrame();
    }
    _started = false;
}

void ProfileAnalyzer::Discontinuity()
{
    Inc(_discontinuities);
    ClearStack();
    _lastCell = nullptr;
}

/// endregion

/// region <The per-frame series>

uint64_t ProfileAnalyzer::InclusiveNow(const Routine& r, uint64_t now) const
{
    const uint64_t inclusive = Load(r.inclusive);
    return r.onStack.load(std::memory_order_relaxed) ? inclusive + (now - Load(r.outerEnter)) : inclusive;
}

void ProfileAnalyzer::FlushFrame()
{
    CloseSelf();
    const uint64_t now = Load(_now);
    const uint32_t routines = _routineCount.load(std::memory_order_relaxed);
    _flushScratch.clear();
    uint64_t frameTotal = 0;
    for (uint32_t id = 0; id < routines; ++id)
    {
        Routine& r = *RoutinePtr(id);
        const uint64_t self = Load(r.self);
        const uint64_t inclusive = InclusiveNow(r, now);
        if (self == r.prevSelf && inclusive == r.prevInclusive)
            continue;
        _flushScratch.push_back({id, static_cast<uint32_t>(self - r.prevSelf), static_cast<uint32_t>(inclusive - r.prevInclusive)});
        frameTotal += self - r.prevSelf;
        r.prevSelf = self;
        r.prevInclusive = inclusive;
    }
    // words: the records (3 each); the index entry says where they are
    const uint64_t pos = _ringHead.load(std::memory_order_relaxed);
    const uint64_t mask = _ringWords - 1;
    uint64_t w = pos;
    for (const auto& rec : _flushScratch)
        for (uint32_t v : rec)
            _ring[w++ & mask].store(v, std::memory_order_relaxed);
    _ringHead.store(w, std::memory_order_release);
    const uint64_t i = _indexHead.load(std::memory_order_relaxed);
    FrameIndex& entry = _index[i % kIndexSize];
    entry.frame.store(_context ? _context->emulatorState.frame_counter - 1 : Load(_frames), std::memory_order_relaxed);
    entry.pos.store(pos, std::memory_order_relaxed);
    entry.count.store(static_cast<uint32_t>(_flushScratch.size()), std::memory_order_relaxed);
    entry.total.store(static_cast<uint32_t>(frameTotal), std::memory_order_relaxed);
    _indexHead.store(i + 1, std::memory_order_release);
}

std::vector<FrameView> ProfileAnalyzer::ReadFrames(uint32_t last) const
{
    std::vector<FrameView> out;
    if (!_index || !_ring)
        return out;
    const uint64_t head = _indexHead.load(std::memory_order_acquire);
    uint64_t first = head > kIndexSize ? head - kIndexSize + 1 : 0;  // the slot being rewritten is skipped
    if (last && head - first > last)
        first = head - last;
    const uint64_t mask = _ringWords - 1;
    for (uint64_t i = first; i < head; ++i)
    {
        const FrameIndex& entry = _index[i % kIndexSize];
        FrameView view;
        view.frame = entry.frame.load(std::memory_order_relaxed);
        view.total = entry.total.load(std::memory_order_relaxed);
        const uint64_t pos = entry.pos.load(std::memory_order_relaxed);
        const uint32_t count = entry.count.load(std::memory_order_relaxed);
        view.routines.resize(count);
        for (uint32_t k = 0; k < count; ++k)
            for (uint32_t j = 0; j < 3; ++j)
                view.routines[k][j] = _ring[(pos + k * 3 + j) & mask].load(std::memory_order_relaxed);
        // valid only when neither the index slot nor the words were rewritten meanwhile
        std::atomic_thread_fence(std::memory_order_acquire);
        const uint64_t indexNow = _indexHead.load(std::memory_order_relaxed);
        const uint64_t ringNow = _ringHead.load(std::memory_order_relaxed);
        if (indexNow - i >= kIndexSize || ringNow - pos > _ringWords)
            continue;
        out.push_back(std::move(view));
    }
    return out;
}

void ProfileAnalyzer::StreamLoop()
{
    std::ofstream file(FileHelper::ToFsPath(_settings.stream), std::ios::app);
    if (!file.is_open())
        return;
    uint64_t next = _indexHead.load(std::memory_order_acquire);
    uint32_t routinesWritten = 0;
    const auto writeRoutines = [&] {
        const uint32_t routines = _routineCount.load(std::memory_order_acquire);
        for (; routinesWritten < routines; ++routinesWritten)
        {
            const Routine& r = *RoutinePtr(routinesWritten);
            char line[96];
            std::snprintf(line, sizeof(line), "{\"routine\":%u,\"entry\":\"$%04X\",\"page\":%u}\n", routinesWritten,
                          r.address, r.page);
            file << line;
        }
    };
    for (;;)
    {
        const bool stopping = _streamStop.load(std::memory_order_acquire);
        const uint64_t head = _indexHead.load(std::memory_order_acquire);
        if (head > next)
        {
            const uint64_t available = head - next;
            std::vector<FrameView> frames = ReadFrames(static_cast<uint32_t>(std::min<uint64_t>(available, kIndexSize)));
            const uint64_t got = frames.size();
            if (got < available)
                _streamLost.fetch_add(available - got, std::memory_order_relaxed);
            writeRoutines();
            for (const FrameView& f : frames)
            {
                file << "{\"frame\":" << f.frame << ",\"t\":" << f.total << ",\"r\":[";
                for (size_t k = 0; k < f.routines.size(); ++k)
                    file << (k ? ",[" : "[") << f.routines[k][0] << ',' << f.routines[k][1] << ',' << f.routines[k][2]
                         << ']';
                file << "]}\n";
            }
            _streamed.fetch_add(got, std::memory_order_relaxed);
            next = head;
            file.flush();
        }
        if (stopping)
            break;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

void ProfileAnalyzer::StopStream()
{
    if (!_streamThread.joinable())
        return;
    _streamStop.store(true, std::memory_order_release);
    _streamThread.join();
}

/// endregion

/// region <The step>

template <bool kStack, bool kBeam, bool kFrames, int kFlowSource>
void ProfileAnalyzer::Step(void* context, Z80* cpu, uint16_t pc)
{
    ProfileAnalyzer& p = *static_cast<ProfileAnalyzer*>(context);
    if (p._context->ttdReplayActive && !p._settings.countReplay) [[unlikely]]
    {
        // a replay pass re-runs history: not the CPU's time now
        if (p._started)
        {
            p._started = false;
            p.Discontinuity();
        }
        return;
    }
    const uint32_t t = cpu->t;
    if (!p._started) [[unlikely]]
    {
        p._started = true;
        p._lastT = t;
        p._ackSeen = cpu->acknowledges;
        p._lastFrameCounter = p._context->emulatorState.frame_counter;
        p._prevFlow = FlowNone;
        p._prevSp = static_cast<uint16_t>(cpu->sp + 1);  // no transfer matches it: the opcode before is not ours
        p._topSince.store(Load(p._now), std::memory_order_relaxed);
    }
    const uint32_t lastStart = p._lastT;
    uint32_t d;
    bool wrapped = false;
    if (t < lastStart) [[unlikely]]
    {
        // the frame ended between the two starts (Core::AdjustFrameCounters took a frame's T off t)
        d = t + p._frameT - lastStart;
        wrapped = true;
        const uint64_t frame = p._context->emulatorState.frame_counter;
        if (frame != p._lastFrameCounter + 1 || d > p._frameT)
        {
            p.Discontinuity();
            d = 0;
        }
        p._lastFrameCounter = frame;
    }
    else
    {
        d = t - lastStart;
        if (d > p._frameT) [[unlikely]]
        {
            p.Discontinuity();
            d = 0;
        }
    }
    p._lastT = t;
    uint32_t ack = 0;
    const bool interrupted = cpu->acknowledges != p._ackSeen;
    if (interrupted) [[unlikely]]
    {
        p._ackSeen = cpu->acknowledges;
        ack = std::min(cpu->lastAcknowledgeT, d);
        d -= ack;
        Inc(p._acks);
        Add(p._ackT, ack);
    }
    if (p._lastCell) [[likely]]
        Add(p._lastCell->t, d);
    Add(p._now, d);
    if constexpr (kBeam)
        Add(p.RoutinePtr(p.Top())->beam[(uint64_t(lastStart) * p._beamMul) >> 32], d);
    if (wrapped) [[unlikely]]
    {
        Inc(p._frames);
        if constexpr (kFrames)
            p.FlushFrame();
    }

    const uint32_t off = p._memory->ExecPhysOffset(pc);
    Cell* page = p._pages[off >> 14].load(std::memory_order_relaxed);
    if (!page) [[unlikely]]
        page = p.AllocPage(off >> 14, pc);
    Cell* cell = page + (off & 0x3FFF);
    const bool repeat = cpu->halted != 0;  // a halted CPU's idle fetch: the HALT's time, no execution
    uint64_t rid = 0;
    if (!repeat)
    {
        const uint64_t count = Load(cell->count) + 1;
        cell->count.store(count, std::memory_order_relaxed);
        rid = count >> kRidShift;
        Inc(p._instructions);
    }
    p._lastCell = cell;

    if constexpr (kStack)
    {
        if (!repeat)
        {
            bool arrived = false;
            const uint16_t sp = cpu->sp;
            uint16_t retAddr = 0;
            if (interrupted) [[unlikely]]
            {
                // the acknowledge pushed the address it interrupted
                retAddr = static_cast<uint16_t>(p._memory->DirectReadFromZ80Memory(sp) |
                                                (p._memory->DirectReadFromZ80Memory(static_cast<uint16_t>(sp + 1)) << 8));
            }
            if constexpr (kFlowSource != 0)
            {
                // the transfer of the instruction before, decoded now; with an acknowledge between, the previous
                // instruction left SP 2 higher and the PC at the address the acknowledge pushed
                const uint16_t next = interrupted ? retAddr : pc;
                const uint16_t spAfter = interrupted ? static_cast<uint16_t>(sp + 2) : sp;
                uint8_t flow;
                if constexpr (kFlowSource == 1)
                {
                    const uint16_t prefix = cpu->prefix;
                    flow = prefix == 0xED ? kFlow.ed[cpu->opcode]
                           : (prefix == 0 || prefix == 0xDD || prefix == 0xFD) ? kFlow.main[cpu->opcode]
                                                                               : static_cast<uint8_t>(FlowNone);
                }
                else
                {
                    flow = p._prevFlow;
                }
                if (flow) [[unlikely]]
                    arrived = p.ResolveFlow(flow, spAfter, next, pc, off);
            }
            if (interrupted) [[unlikely]]
            {
                p.AbandonAt(sp);
                p.Push(p.RoutineAt(off, pc), sp, retAddr, EntryInterrupt);
                arrived = true;
            }
            if (rid) [[unlikely]]
            {
                // an entry reached by a jump or a fall in: the time stays with the routine that jumped (said so)
                if constexpr (kFlowSource == 0)
                    arrived = arrived || p._arrivedAt + 1 == Load(p._instructions);
                if (!arrived && static_cast<uint32_t>(rid - 1) != p.Top())
                    Inc(p.RoutinePtr(static_cast<uint32_t>(rid - 1))->jumpIns);
            }
            if constexpr (kFlowSource == 2)
            {
                uint8_t flow = kFlow.main[p._memory->PhysByte(off)];
                if (flow == FlowEd) [[unlikely]]
                    flow = kFlow.ed[p._memory->PhysByte(off + 1)];
                p._prevFlow = flow;
            }
            if constexpr (kFlowSource != 0)
            {
                p._prevSp = sp;
                p._prevPc = pc;
            }
        }
    }
    // the acknowledge: the handler's first instruction carries it (after the interrupt's frame was pushed, so the
    // interrupt's routine owns it)
    if (ack) [[unlikely]]
    {
        Add(cell->t, ack);
        Add(p._now, ack);
    }
}

/// endregion

/// region <Marks and reads>

bool ProfileAnalyzer::Mark(const std::string& name, std::string& error)
{
    if (name.empty())
    {
        error = "a mark needs a name";
        return false;
    }
    Baseline b;
    b.frames = Load(_frames);
    b.t = Load(_now);
    b.instructions = Load(_instructions);
    b.acknowledges = Load(_acks);
    b.acknowledgeT = Load(_ackT);
    for (uint32_t page = 0; page < MAX_PAGES; ++page)
        if (const Cell* cells = _pages[page].load(std::memory_order_acquire))
        {
            auto& copy = b.pages[page];
            copy.resize(kPageCells);
            for (uint32_t i = 0; i < kPageCells; ++i)
                copy[i] = {Load(cells[i].count) & kCountMask, Load(cells[i].t)};
        }
    const uint32_t routines = _routineCount.load(std::memory_order_acquire);
    if (_chunks[0].load(std::memory_order_acquire))
    {
        b.routines.resize(routines);
        for (uint32_t id = 0; id < routines; ++id)
        {
            const Routine& r = *RoutinePtr(id);
            auto& row = b.routines[id];
            row[0] = SelfNow(id, r, b.t);
            row[1] = InclusiveNow(r, b.t);
            for (uint32_t k = 0; k < EntryKinds; ++k)
                row[2 + k] = Load(r.entries[k]);
        }
    }
    std::lock_guard<std::mutex> lock(_marksMutex);
    if (!_marks.count(name) && _marks.size() >= kMaxMarks)
    {
        error = "at most " + std::to_string(kMaxMarks) + " marks: drop one first";
        return false;
    }
    _marks[name] = std::move(b);
    return true;
}

bool ProfileAnalyzer::DropMark(const std::string& name)
{
    std::lock_guard<std::mutex> lock(_marksMutex);
    return _marks.erase(name) != 0;
}

std::vector<std::string> ProfileAnalyzer::Marks() const
{
    std::lock_guard<std::mutex> lock(_marksMutex);
    std::vector<std::string> names;
    for (const auto& [name, _] : _marks)
        names.push_back(name);
    return names;
}

bool ProfileAnalyzer::HasMark(const std::string& name) const
{
    std::lock_guard<std::mutex> lock(_marksMutex);
    return _marks.count(name) != 0;
}

Totals ProfileAnalyzer::ReadTotals() const
{
    Totals t;
    t.active = _active.load(std::memory_order_acquire);
    t.settings = _settings;
    t.startFrame = Load(_startFrame);
    t.frames = Load(_frames);
    t.tstates = Load(_now);
    t.instructions = Load(_instructions);
    t.acknowledges = Load(_acks);
    t.acknowledgeT = Load(_ackT);
    t.discontinuities = Load(_discontinuities);
    t.stackOverflows = Load(_stackOverflows);
    t.routineOverflows = Load(_routineOverflows);
    t.frameT = _frameTPublished.load(std::memory_order_relaxed);
    t.pages = _pagesAllocated.load(std::memory_order_relaxed);
    const uint32_t routines = _routineCount.load(std::memory_order_acquire);
    t.routines = routines > 0 ? routines - 1 : 0;
    t.depth = _depth.load(std::memory_order_relaxed);
    const uint64_t head = _indexHead.load(std::memory_order_acquire);
    t.framesKept = _index ? std::min<uint64_t>(head, kIndexSize - 1) : 0;
    t.framesLost = Load(_streamLost);
    t.streamed = Load(_streamed);
    return t;
}

Snapshot ProfileAnalyzer::Read(const std::string& since, bool cells) const
{
    Snapshot s;
    s.totals = ReadTotals();
    const Baseline* base = nullptr;
    std::unique_lock<std::mutex> lock(_marksMutex);
    if (!since.empty())
    {
        auto it = _marks.find(since);
        if (it != _marks.end())
        {
            base = &it->second;
            s.since = since;
        }
    }
    else
        lock.unlock();
    s.sinceFrames = s.totals.frames - (base ? base->frames : 0);
    s.sinceT = s.totals.tstates - (base ? base->t : 0);
    s.sinceInstructions = s.totals.instructions - (base ? base->instructions : 0);
    s.sinceAcknowledges = s.totals.acknowledges - (base ? base->acknowledges : 0);
    s.sinceAcknowledgeT = s.totals.acknowledgeT - (base ? base->acknowledgeT : 0);

    if (_chunks[0].load(std::memory_order_acquire))
    {
        const uint32_t routines = s.totals.routines + 1;
        const uint64_t now = s.totals.tstates;
        s.routines.resize(routines);
        for (uint32_t id = 0; id < routines; ++id)
        {
            const Routine& r = *RoutinePtr(id);
            RoutineView& v = s.routines[id];
            v.id = id;
            v.address = r.address;
            v.page = r.page;
            v.self = SelfNow(id, r, now);
            v.inclusive = InclusiveNow(r, now);
            for (uint32_t k = 0; k < EntryKinds; ++k)
                v.entries[k] = Load(r.entries[k]);
            v.jumpIns = Load(r.jumpIns);
            v.abandoned = Load(r.abandoned);
            v.patched = Load(r.patched);
            v.dispatchesOut = Load(r.dispatchesOut);
            v.maxDepth = r.maxDepth.load(std::memory_order_relaxed);
            v.onStack = r.onStack.load(std::memory_order_relaxed) != 0;
            for (uint32_t i = 0; i < kCallerSlots; ++i)
            {
                const uint32_t caller = r.callerId[i].load(std::memory_order_acquire);
                if (caller == 0)
                    break;
                v.callers.emplace_back(caller - 1, Load(r.callerCount[i]));
            }
            v.callersMore = Load(r.callersMore);
            if (_settings.beam)
            {
                v.beam.resize(kBeamBins);
                for (uint32_t k = 0; k < kBeamBins; ++k)
                    v.beam[k] = Load(r.beam[k]);
            }
            if (base && id < base->routines.size())
            {
                const auto& row = base->routines[id];
                v.self -= std::min(v.self, row[0]);
                v.inclusive -= std::min(v.inclusive, row[1]);
                for (uint32_t k = 0; k < EntryKinds; ++k)
                    v.entries[k] -= std::min(v.entries[k], row[2 + k]);
            }
        }
    }
    if (cells)
    {
        for (uint32_t page = 0; page < MAX_PAGES; ++page)
        {
            const Cell* c = _pages[page].load(std::memory_order_acquire);
            if (!c)
                continue;
            const std::vector<std::pair<uint64_t, uint64_t>>* basePage = nullptr;
            if (base)
                if (auto it = base->pages.find(page); it != base->pages.end())
                    basePage = &it->second;
            const uint16_t window = static_cast<uint16_t>((_pageWindow[page] & 3) << 14);
            for (uint32_t i = 0; i < kPageCells; ++i)
            {
                uint64_t count = Load(c[i].count) & kCountMask;
                uint64_t t = Load(c[i].t);
                if (basePage)
                {
                    count -= std::min(count, (*basePage)[i].first);
                    t -= std::min(t, (*basePage)[i].second);
                }
                if (count || t)
                    s.cells.push_back(CellView{static_cast<uint16_t>(window | i), static_cast<uint16_t>(page), count, t});
            }
        }
    }
    return s;
}

uint8_t ProfileAnalyzer::ByteAt(uint16_t page, uint16_t offset) const
{
    if (!_context || !_context->pMemory || page >= MAX_PAGES)
        return 0;
    return _context->pMemory->PhysByte((static_cast<uint32_t>(page) << 14) | (offset & 0x3FFFu));
}

/// endregion
