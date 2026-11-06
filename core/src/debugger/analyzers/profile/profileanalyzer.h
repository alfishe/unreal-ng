#pragma once

/// @file profileanalyzer.h
/// @brief The CPU time profile (docs/emulator/analysis/cpu-profile.md): where the CPU's T-states go, per executed
/// byte and per routine found at run time, live, without a recording.
///
/// Off it costs nothing: no CPU step subscriber exists. On, one step function per instruction start (the variant the
/// settings pick, no option branches in it) books the T-states since the previous start to the previous instruction:
///   - cells: per physical byte (page, offset) the executions and the T-states, pages allocated at their first
///     execution (256 KiB a 16 KiB page);
///   - the shadow stack (stack=true, the default): CALL / RST / an interrupt push a frame, RET / RETI / RETN pop it by
///     the stack slot, so each routine has self (top of the stack) and inclusive (on the stack) T-states; JP entries,
///     tail calls, PUSH + RET dispatch, discarded return addresses and patched returns are counted, not hidden;
///   - beam bins (beam=true): a routine's self T-states per 1/64 of the frame (where in the frame it runs);
///   - the per-frame series (frames=true, the default): every frame's self / inclusive per active routine, in a ring
///     (the last few thousand frames) and optionally streamed to a JSONL file by a writer thread.
/// The interrupt acknowledge is booked to the handler's first instruction (and so to the interrupt's routine); a halted
/// CPU's idle stretch to its HALT, which lets the CPU run the stretch in one go.
///
/// Threads: the step runs on the emulation thread and is the only writer of the counters; every counter is a relaxed
/// atomic written by a plain load + store, so a reader on another thread never parks the emulation thread and never
/// sees a torn value (a report is a few instructions skewed at most). Activation, reset and settings changes need the
/// CPU held (ProfileControl pauses the instance for them); marks and reads do not.

#include <array>
#include <atomic>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "debugger/analyzers/ianalyzer.h"
#include "emulator/cpu/z80.h"
#include "emulator/platform.h"

class AnalyzerManager;
class EmulatorContext;
class Memory;
class Z80;

namespace profile
{

/// What the profile keeps besides the cells (ProfileAnalyzer::Configure)
struct Settings
{
    bool stack = true;         ///< the shadow call stack: routines with self / inclusive
    bool beam = false;         ///< self T-states per 1/64 of the frame per routine (needs stack)
    bool frames = true;        ///< the per-frame series ring (needs stack)
    uint32_t ringKiB = 4096;   ///< the series ring's size (64 .. 262144 KiB)
    std::string stream;        ///< a JSONL file the series is appended to while the profile runs (empty: none)
    bool countReplay = false;  ///< book TTD replay passes too: the profile of a recording on an analysis clone
                               ///< (analysis::ReplayWindow); a live instance skips them (not the CPU's time now)
    /// How the call stack learns of calls and returns. 0: the CPU's flow events (Z80::FlowHook, raised only by CALL /
    /// RST / RET-family instructions); 1: the previous instruction's opcode (Z80::opcode / prefix) decoded at every
    /// step; 2: the instruction's byte re-read from memory at every step (the first version). 1 and 2 are kept for
    /// the A/B of POC 046 only
    uint8_t flowSource = 0;
};

constexpr uint32_t kBeamBins = 64;
constexpr uint32_t kMaxRoutines = 65536;
constexpr uint32_t kCallerSlots = 8;
constexpr uint32_t kMaxMarks = 8;
constexpr uint32_t kMaxDepth = 1024;  ///< shadow stack frames kept (deeper: the oldest is dropped and counted)

/// How a routine was entered (Routine::entries)
enum EntryKind : uint8_t
{
    EntryCall = 0,
    EntryRst,
    EntryInterrupt,
    EntryDispatch,  ///< a RET to an address no frame pushed (PUSH + RET dispatch, a RET chain)
    EntryKinds
};

/// One routine's counters as a reader sees them (a copy)
struct RoutineView
{
    uint32_t id = 0;
    uint16_t address = 0;  ///< the Z80 address the entry ran at first
    uint16_t page = 0;     ///< absolute page (RAM 0-255, cache, misc, ROM from 261: Memory::GetPageForBank)
    uint64_t self = 0;
    uint64_t inclusive = 0;
    uint64_t entries[EntryKinds] = {};
    uint64_t jumpIns = 0;    ///< arrivals at the entry not by a call, a return or an interrupt (JP, a fall in)
    uint64_t abandoned = 0;  ///< frames left without a return (SP moved past them, a discarded return address)
    uint64_t patched = 0;    ///< returns to another address than the one pushed (the slot rewritten)
    uint64_t dispatchesOut = 0;  ///< RETs from this routine that no frame owned (it dispatched by PUSH + RET)
    uint32_t maxDepth = 0;
    bool onStack = false;
    std::vector<std::pair<uint32_t, uint64_t>> callers;  ///< caller routine id (0: none), entries from it
    uint64_t callersMore = 0;  ///< entries from callers past the first kCallerSlots
    std::vector<uint64_t> beam;  ///< kBeamBins self T-states (empty without beam)
};

/// The profile's totals as a reader sees them
struct Totals
{
    bool active = false;
    Settings settings;
    uint64_t startFrame = 0;      ///< the machine's frame counter when the profile (or its reset) started
    uint64_t frames = 0;          ///< frame ends seen
    uint64_t tstates = 0;         ///< T-states booked
    uint64_t instructions = 0;    ///< instructions executed (a halted CPU's idle fetches not counted)
    uint64_t acknowledges = 0;    ///< INT / NMI acknowledges
    uint64_t acknowledgeT = 0;    ///< their T-states (in the handlers' first instructions)
    uint64_t discontinuities = 0; ///< time jumps not booked (a snapshot load, a reset, a seek, a replay pass)
    uint64_t stackOverflows = 0;  ///< frames dropped at kMaxDepth
    uint64_t routineOverflows = 0;///< entries past kMaxRoutines (booked to the caller)
    uint32_t frameT = 0;          ///< CPU T-states in a frame now
    uint32_t pages = 0;           ///< cell pages allocated
    uint32_t routines = 0;        ///< routines found (the root not counted)
    uint32_t depth = 0;           ///< shadow stack depth now
    uint64_t framesKept = 0;      ///< frames in the series ring now
    uint64_t framesLost = 0;      ///< series frames the stream could not keep up with
    uint64_t streamed = 0;        ///< series frames written to the stream
};

/// One executed byte's counters
struct CellView
{
    uint16_t address;  ///< Z80 address (the window the page ran in first)
    uint16_t page;
    uint64_t count;
    uint64_t tstates;
};

/// One frame of the per-frame series
struct FrameView
{
    uint64_t frame = 0;  ///< the machine's frame number
    uint32_t total = 0;  ///< T-states booked in the frame
    std::vector<std::array<uint32_t, 3>> routines;  ///< routine id, self, inclusive (active routines only)
};

/// A reader's copy of the profile, optionally since a mark (counters minus the mark's baseline)
struct Snapshot
{
    Totals totals;
    std::string since;  ///< "" (since the start) or the mark's name
    uint64_t sinceFrames = 0, sinceT = 0, sinceInstructions = 0, sinceAcknowledges = 0, sinceAcknowledgeT = 0;
    std::vector<RoutineView> routines;  ///< index = id; [0] is the root (code outside any routine found)
    std::vector<CellView> cells;        ///< only when asked for
};

}  // namespace profile

class ProfileAnalyzer : public IAnalyzer
{
public:
    static constexpr const char* kId = "profile";

    explicit ProfileAnalyzer(EmulatorContext* context);
    ~ProfileAnalyzer() override;

    std::string getName() const override { return "ProfileAnalyzer"; }
    std::string getUUID() const override { return "profileanalyzer"; }

    /// Subscribes the step the settings pick (the CPU held by the caller); a first activation or one after Reset
    /// starts the window
    void onActivate(AnalyzerManager* manager) override;
    void onDeactivate() override;

    /// region <Control (the CPU held by the caller)>
    /// The settings of the next activation (while inactive)
    bool Configure(const profile::Settings& settings, std::string& error);
    /// The checks of Configure that do not depend on the state: a refused start leaves a running profile running
    static bool Validate(const profile::Settings& settings, std::string& error);
    /// Zero every counter, forget the routines, the marks and the series: a fresh window from now
    void Reset();
    /// endregion

    /// region <Marks and reads (any thread, never parks the emulation thread)>
    /// Copy the counters as the baseline @p name (replaces one of the name). False past kMaxMarks
    bool Mark(const std::string& name, std::string& error);
    bool DropMark(const std::string& name);
    std::vector<std::string> Marks() const;
    bool HasMark(const std::string& name) const;

    profile::Totals ReadTotals() const;
    /// The counters, minus the baseline of @p since when it names a mark. @p cells: the executed bytes too
    profile::Snapshot Read(const std::string& since, bool cells) const;
    /// The last @p last frames of the series (oldest first), at most what the ring holds
    std::vector<profile::FrameView> ReadFrames(uint32_t last) const;
    /// The first byte of an executed instruction (the export's code column): the memory now
    uint8_t ByteAt(uint16_t page, uint16_t offset) const;
    /// endregion

private:
    struct Cell
    {
        std::atomic<uint64_t> count{0};  ///< bits 0-39 executions, 40-63 the routine id + 1 of an entry here
        std::atomic<uint64_t> t{0};
    };
    struct Routine
    {
        uint32_t phys = 0;
        uint16_t address = 0;
        uint16_t page = 0;
        std::atomic<uint64_t> self{0};
        std::atomic<uint64_t> inclusive{0};
        std::atomic<uint64_t> outerEnter{0};
        std::atomic<uint32_t> onStack{0};
        std::atomic<uint32_t> maxDepth{0};
        std::atomic<uint64_t> entries[profile::EntryKinds] = {};
        std::atomic<uint64_t> jumpIns{0};
        std::atomic<uint64_t> abandoned{0};
        std::atomic<uint64_t> patched{0};
        std::atomic<uint64_t> dispatchesOut{0};
        std::atomic<uint32_t> callerId[profile::kCallerSlots] = {};  ///< id + 1, 0: free
        std::atomic<uint64_t> callerCount[profile::kCallerSlots] = {};
        std::atomic<uint64_t> callersMore{0};
        std::atomic<uint64_t> beam[profile::kBeamBins] = {};
        // the series' previous values (emulation thread only)
        uint64_t prevSelf = 0;
        uint64_t prevInclusive = 0;
    };
    static constexpr uint32_t kChunk = 1024;
    static constexpr uint16_t kRootSlot = 0xFFFF;  ///< the slot of a frame a dispatch from the root pushed (none)
    struct Frame
    {
        uint32_t rid;
        uint16_t slot;
        uint16_t ret;
        uint8_t kind;  ///< profile::EntryKind: how the routine was entered
    };
    struct Baseline
    {
        uint64_t frames = 0, t = 0, instructions = 0, acknowledges = 0, acknowledgeT = 0;
        std::map<uint32_t, std::vector<std::pair<uint64_t, uint64_t>>> pages;  ///< page -> count, t per byte
        std::vector<std::array<uint64_t, 2 + profile::EntryKinds>> routines;   ///< self, inclusive, entries
    };

    using StepFn = void (*)(void*, Z80*, uint16_t);
    template <bool kStack, bool kBeam, bool kFrames, int kFlowSource>
    static void Step(void* context, Z80* cpu, uint16_t pc);
    template <bool kStack, bool kBeam, bool kFrames>
    StepFn PickFlow() const;
    StepFn PickStep() const;
    /// Z80::FlowHook: a CALL / RST pushed or a RET popped (flowSource 0)
    static void OnFlow(void* context, Z80* cpu, Z80::FlowEvent event, uint16_t target, uint16_t slot, uint16_t ret);

    static void Add(std::atomic<uint64_t>& a, uint64_t d)
    {
        a.store(a.load(std::memory_order_relaxed) + d, std::memory_order_relaxed);
    }
    static void Inc(std::atomic<uint64_t>& a) { Add(a, 1); }

    Cell* AllocPage(uint32_t page, uint16_t pc);
    Routine* RoutinePtr(uint32_t id) const
    {
        return &_chunks[id / kChunk].load(std::memory_order_acquire)[id % kChunk];
    }
    uint32_t RoutineAt(uint32_t phys, uint16_t pc);
    uint32_t Top() const { return _stack.empty() ? 0 : _stack.back().rid; }
    void CloseSelf();
    void PublishTop();
    uint64_t SelfNow(uint32_t id, const Routine& r, uint64_t now) const;
    void Push(uint32_t rid, uint16_t slot, uint16_t ret, profile::EntryKind kind);
    void DropFrame(size_t index, bool abandoned);
    void PopTop(bool abandoned);
    void ClearStack();
    void AbandonAt(uint16_t slot);
    bool ResolveFlow(uint8_t flow, uint16_t spAfter, uint16_t next, uint16_t pc, uint32_t phys);
    void Called(uint16_t target, uint32_t phys, uint16_t slot, uint16_t ret, profile::EntryKind kind);
    void Returned(uint16_t target, uint32_t phys, uint16_t slot);
    void Settle();
    void Discontinuity();
    void FlushFrame();
    uint64_t InclusiveNow(const Routine& r, uint64_t now) const;

    // the series ring (single writer, readers validate against the heads)
    struct FrameIndex
    {
        std::atomic<uint64_t> frame{0};
        std::atomic<uint64_t> pos{0};
        std::atomic<uint32_t> count{0};
        std::atomic<uint32_t> total{0};
    };
    static constexpr uint32_t kIndexSize = 16384;
    void StreamLoop();
    void StopStream();

    EmulatorContext* _context;
    AnalyzerManager* _manager = nullptr;
    Memory* _memory = nullptr;
    profile::Settings _settings;
    std::atomic<bool> _active{false};
    bool _fresh = true;  ///< the next activation starts the window

    std::atomic<Cell*> _pages[MAX_PAGES] = {};
    int8_t _pageWindow[MAX_PAGES];
    std::atomic<uint32_t> _pagesAllocated{0};
    std::atomic<Routine*> _chunks[profile::kMaxRoutines / kChunk] = {};
    std::atomic<uint32_t> _routineCount{1};  ///< [0] the root

    // the step's state (emulation thread)
    Cell* _lastCell = nullptr;
    bool _started = false;
    uint32_t _lastT = 0;
    uint64_t _lastFrameCounter = 0;
    uint64_t _ackSeen = 0;
    uint32_t _frameT = 69888;
    uint32_t _beamMul = 0;
    std::vector<Frame> _stack;
    std::atomic<uint64_t> _topSince{0};  ///< when the routine on top last became the top
    std::atomic<uint32_t> _top{0};       ///< the routine on top (readers add its open stretch)
    uint64_t _arrivedAt = ~uint64_t(0);  ///< _instructions when the last flow event moved the stack (flowSource 0)
    Z80* _hookedCpu = nullptr;            ///< the CPU whose flow hook is ours
    uint8_t _prevFlow = 0;
    uint16_t _prevSp = 0;
    uint16_t _prevPc = 0;
    std::vector<std::array<uint32_t, 3>> _flushScratch;

    // totals
    std::atomic<uint64_t> _now{0};
    std::atomic<uint64_t> _instructions{0};
    std::atomic<uint64_t> _acks{0};
    std::atomic<uint64_t> _ackT{0};
    std::atomic<uint64_t> _frames{0};
    std::atomic<uint64_t> _discontinuities{0};
    std::atomic<uint64_t> _stackOverflows{0};
    std::atomic<uint64_t> _routineOverflows{0};
    std::atomic<uint64_t> _startFrame{0};
    std::atomic<uint32_t> _depth{0};
    std::atomic<uint32_t> _frameTPublished{0};

    // the series
    std::unique_ptr<std::atomic<uint32_t>[]> _ring;
    uint64_t _ringWords = 0;  ///< a power of two
    std::atomic<uint64_t> _ringHead{0};
    std::unique_ptr<FrameIndex[]> _index;
    std::atomic<uint64_t> _indexHead{0};

    // the stream
    std::thread _streamThread;
    std::atomic<bool> _streamStop{false};
    std::atomic<uint64_t> _streamed{0};
    std::atomic<uint64_t> _streamLost{0};

    mutable std::mutex _marksMutex;
    std::map<std::string, Baseline> _marks;
};
