#pragma once

/// @file twintimeline.h
/// @brief The GS debugger's twin timeline, phase 1: the data behind it on every automation plane (gui-card-debugger
/// §A5.1, gs-debugger design §5.4, §7.2). Both CPUs on one clock: the main CPU's lane and a sound card's lane, the
/// shared moment NOW, the recent instructions of each lane as blocks whose width is their measured duration, the next
/// instruction, markers (breakpoints, host port accesses, INT / NMI, the NeoGS clock change) and, while the machine
/// runs, a density per frame.
///
/// Where the data comes from (no ring of its own):
/// - the blocks: the execution histories (exechistory.h) `cpu` and `<slot>.cpu`, whose records carry their start in
///   the frame. A block lasts until the next record of its lane starts, so contention, /WAIT, an interrupt's
///   acknowledge and the NeoGS clock are in it exactly;
/// - the host port markers: the GS command log `<slot>.commands` (frame, main T, main PC of every access);
/// - the density: the card's always-on activity counters (and the profiler's cycles when it is on), sampled at each
///   frame end into a small ring (Timeline::OnFrameEnd).
///
/// Zero cost off. Nothing records until the timeline is armed: a lease per client (Arm, with a timeout for remote
/// clients that stop asking; 0 = until released). Armed, the histories it needs that were off are switched to a ring
/// (and back off when the last lease ends); the frame-end sampling runs. Off: one relaxed load per frame end.
///
/// Times in the node are main T relative to NOW (`at`, negative in the past): main records by their T, card records
/// by their card units divided by the units per main T (GSHostClock::unitsPerZxTact, the inverse of the card's own
/// catch-up), so a card block's width is exact in its own units (`duration_units`) and converts back exactly.
///
/// Threads: Arm / Release / reads from any thread; OnFrameEnd on the emulation thread. Read builds the paused detail
/// at a coherent moment and never parks a running machine: while it runs the node carries the published density only.

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <vector>

#include "debugger/history/exechistory.h"
#include "debugger/targets/debugcpuid.h"
#include "emulator/state/statenode.h"

class Emulator;
class EmulatorContext;
class GeneralSoundCard;

namespace timeline
{

constexpr uint32_t kDefaultLeaseMs = 30000;  ///< a remote client's lease without a renewal
constexpr size_t kDensityFrames = 64;        ///< the density ring: a bit over a second at 50 Hz
constexpr double kDefaultSpanT = 60.0;       ///< ± main T around NOW
constexpr double kMaxSpanT = 100000.0;
constexpr size_t kMaxRecent = 512;           ///< blocks per lane at most
constexpr uint32_t kDefaultWaitMs = 500;     ///< a paused read's wait for a coherent moment (Query::waitMs)

/// One frame of activity (the running density): the main lane's GS port accesses and the card lane's work
struct DensityTick
{
    uint64_t frame = 0;
    // Main lane: the game's accesses to the card's host ports in the frame
    uint32_t hostCommands = 0;    ///< OUT #BB
    uint32_t hostDataWrites = 0;  ///< OUT #B3
    uint32_t hostDataReads = 0;   ///< IN #B3
    uint32_t hostStatusPolls = 0; ///< IN #BB
    // Card lane
    uint32_t cardSteps = 0;       ///< instructions (the idle sleep's included)
    uint32_t cardIdleSteps = 0;   ///< of those, slept through in the firmware's idle poll loop
    uint32_t cardInterrupts = 0;
    uint32_t cardNmis = 0;
    uint32_t cardCommandsTaken = 0;
    bool profiled = false;        ///< the cycle fields are the card profiler's (GSProfileActivity)
    uint32_t cycles = 0;
    uint32_t isrCycles = 0;
    uint32_t idleCycles = 0;
    uint32_t haltCycles = 0;

    void ToState(StateNode& node) const;
};

/// One block of a lane: a record of an execution history with its start and duration in the lane's units (main T,
/// card units), the start relative to the start of NOW's frame
struct Block
{
    size_t index = 0;       ///< the record in the list given
    int64_t start = 0;
    int64_t duration = 0;   ///< the next record's start minus this one's; the newest: up to `end` (open)
    bool open = false;      ///< the newest record: its end is the lane's time now, not a successor's start
    uint32_t clockHz = 0;   ///< a card: its CPU clock while the record ran (NeoGS clock changes walked back)
};

/// The blocks of a lane's records (oldest first). A record's start is `t` + (its frame - `frame`) x `frameLength`;
/// clock-change records take no time (the instruction before keeps the time up to the next instruction). Records past
/// `end` (a TTD seek back left them) are left out. `clockNowHz`: the card's clock now (0: main)
std::vector<Block> Blocks(const std::vector<ExecRecord>& records, uint32_t frame, int64_t frameLength, int64_t end,
                          uint32_t clockNowHz = 0);

/// The leases, the histories armed for them and the density ring of one machine (DebugManager::GetTimeline)
class Timeline
{
public:
    using Clock = std::function<std::chrono::steady_clock::time_point()>;

    explicit Timeline(EmulatorContext* context);
    ~Timeline();
    Timeline(const Timeline&) = delete;
    Timeline& operator=(const Timeline&) = delete;

    /// Take or renew `owner`'s lease for `leaseMs` (0: until Release). The first lease switches on the histories the
    /// timeline reads that are off (a ring each); a renewal switches on those of a card fitted since. True: this call
    /// armed it
    bool Arm(const std::string& owner, uint32_t leaseMs);
    /// End `owner`'s lease; the last one switches the histories it switched on back off
    void Release(const std::string& owner);
    bool Armed() const { return _armed.load(std::memory_order_relaxed); }
    /// Leases whose time ran out end now (any thread; the frame end checks too)
    void Expire();
    /// [{owner, left_ms (0: no timeout)}]
    StateNode LeasesNode() const;

    /// A frame ended (the emulation thread; MainLoop calls it only while armed): each card's counters into the ring
    void OnFrameEnd(uint64_t frame);
    /// The newest `last` ticks of card `cpu` (0: all kept), oldest first
    std::vector<DensityTick> Density(DebugCpuId cpu, size_t last = 0) const;
    /// One more at every published tick and every arm / release
    uint64_t Revision() const { return _revision.load(std::memory_order_acquire); }

    /// Tests move the lease clock instead of waiting
    void SetClockForTest(Clock clock);

private:
    struct Lease
    {
        std::chrono::steady_clock::time_point until{};
        bool timed = false;
    };
    struct CardSample
    {
        const GeneralSoundCard* card = nullptr;  ///< the card the baseline is of (a new card starts a new baseline)
        uint64_t steps = 0, slept = 0, ints = 0, nmis = 0, taken = 0;
        uint64_t commands = 0, dataWrites = 0, dataReads = 0, polls = 0;
        uint64_t profiledFrame = UINT64_MAX;
    };
    struct Ring
    {
        std::array<DensityTick, kDensityFrames> ticks{};
        size_t head = 0;
        size_t count = 0;
    };

    std::chrono::steady_clock::time_point Now() const;
    /// Under _mutex: drop the leases that ran out; true when one was dropped
    bool ExpireLocked(std::chrono::steady_clock::time_point now);
    /// Under _mutex: switch the histories on (each off one to a ring, remembered by name) or the remembered ones off
    void ApplyLocked(bool on);
    void UpdateDeadlineLocked();

    EmulatorContext* _context;
    mutable std::mutex _mutex;
    std::map<std::string, Lease> _leases;
    std::vector<std::string> _switchedOn;  ///< registry ids of the histories this timeline turned on
    std::atomic<bool> _armed{false};
    std::atomic<int64_t> _deadline{0};     ///< the soonest lease end (steady ticks); 0: none timed
    std::atomic<uint64_t> _revision{0};
    Clock _clock;

    std::atomic<uint64_t> _epoch{0};       ///< one more at every arming: the frame end starts new baselines
    // The emulation thread's samples; the rings under _mutex
    uint64_t _sampleEpoch = UINT64_MAX;
    std::array<CardSample, 2> _samples{};
    std::array<Ring, 2> _rings{};
};

/// What a read asks
struct Query
{
    std::string cpu;                    ///< the card lane: "gs", "ms"; "": the first card CPU there
    double spanT = kDefaultSpanT;       ///< the window: ± main T around NOW
    size_t recent = 64;                 ///< blocks per lane at most (newest kept)
    size_t density = 50;                ///< running: ticks at most
    std::string client = "local";       ///< the lease's owner
    uint32_t leaseMs = kDefaultLeaseMs; ///< the lease a read takes or renews (0: until released)
    bool arm = true;                    ///< the read takes / renews the lease
    /// Paused: how long the read waits for a coherent moment (another client's command holding the CPU, a stop not
    /// parked yet). 0: one try and a 503 at once - a GUI thread's read (it keeps its last picture and tries again
    /// later); it never waits on the emulation thread
    uint32_t waitMs = kDefaultWaitMs;
};

struct Reply
{
    int status = 200;      ///< 200; 400 a bad query; 404 no such CPU; 503 no coherent moment within Query::waitMs
    std::string error;
    StateNode body;
};

/// The node from the machine as it stands. The caller makes sure nothing runs it meanwhile (a coherent moment, the
/// debugger snapshot); a running machine gets the published density only. Fields: available, state, armed, cpu,
/// lanes[], now, position, window, recent{main, <cpu>}, next{main, <cpu>}, markers[], density[], text
StateNode BuildNow(Emulator& emulator, const Query& query);

/// Every plane's read: takes / renews the lease (query.arm), then BuildNow at a coherent moment (paused, stopped) or
/// the density (running). The in-process front-ends call it as well
Reply Read(Emulator* emulator, const Query& query);
/// Arm (a lease of `leaseMs`, 0 = until released) or release `client`'s lease. Body: {armed, started_now, leases}
Reply SetArmed(Emulator* emulator, const std::string& client, bool on, uint32_t leaseMs);
/// The `debug.timeline` monitoring value (a WebSocket state topic, coalesced): published state only, never a pause.
/// {available, armed, state, revision, cpu, frame, density [the newest 10 ticks of the first card]}
StateNode MonitorNode(Emulator& emulator);

/// The node as text for a console or an agent (the node's `text`): the summary, then each lane's blocks
/// ("  -12.0 +4    #8003 out (#bb),a") and the markers
std::string TextOf(const StateNode& node);

/// The card CPU a query names ("": the first one there); false: unknown or not fitted
bool CardCpuOf(EmulatorContext* context, const std::string& name, DebugCpuId& out);

}  // namespace timeline
