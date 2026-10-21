#pragma once

/// @file executionarbiter.h
/// @brief One place that decides whether a machine's Z80 executes
/// (docs/inprogress/2026-10-09-execution-arbiter/design.md).
///
/// Every request goes into one ordered queue and gets a ticket. The requests come from any thread: GUI, WebAPI, CLI,
/// MCP, scripts, GDB, DeZog, the debugger, TTD, the emulation thread itself.
///
/// - **Intent** (Run / Pause) is a desired state. Consecutive intents coalesce, and the last one wins. A throttle
///   window (`IntentThrottleMs`, default 0) bounds how long they gather while the machine runs.
/// - **Command** is an event with a result: media, reset, snapshot load, TTD work, stepping. A command is a barrier:
///   intents never coalesce across it, and commands run in submission order, each exactly once. While a command
///   is served the CPU is held, which is the "park". A command never changes the intent, except through its own
///   `pauseFirst` / `thenIntent` options, which are applied atomically with it.
/// - **Priority lane**: a pause from the driver's own thread (a breakpoint), the abort of the command being served,
///   and stop. These do not wait in the queue.
///
/// Who executes: the main loop (attached with AttachMainLoop) while `mayRun`, or the body of a command with
/// `drivesZ80` on its submitter's thread. Never both at once.
///
/// mayRun = phase running && intent Run && no command served && the queue head is not a command.
/// The loop reads ShouldYield() (one relaxed atomic load) at every instruction boundary. When it is set, the loop
/// calls LoopWait(), which parks until mayRun again and serves posted commands while parked.
///
/// Worked example (the bug that led here): a thread does Execute("ttd.journal") while the automation submits
/// Pause, then Execute("run_frames 1500"). The queue is [ttd.journal (served), Pause, run_frames]. When the
/// journal command ends, Pause is applied, and then run_frames is served. The main loop stays parked throughout,
/// because the intent is Pause, so only run_frames drives.

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "common/timehelper.h"

namespace execution
{

using Clock = std::chrono::steady_clock;

enum class Intent : uint8_t
{
    Run,
    Pause
};

enum class Origin : uint8_t
{
    Gui,
    WebApi,
    Cli,
    Mcp,
    Lua,
    Python,
    Gdb,
    DeZog,
    Companion,   ///< a paired companion client: the user's hands on another device (no session)
    HostDevice,  ///< a host input device bound to an action (a pad, the Surface Dial): the user's hands, no session
    Debugger,
    Ttd,
    Rzx,
    Lifecycle,
    Internal
};

enum class Outcome : uint8_t
{
    Pending,
    Applied,    ///< done: the intent is in force / the command ran
    Coalesced,  ///< a later intent replaced this one before it was applied
    Refused,    ///< not allowed: run-control claim, not the owner of the pause, stopping
    Busy,       ///< the deadline passed before it could be served (the holder is named)
    Aborted     ///< the command was served and aborted from the priority lane
};

enum class Phase : uint8_t
{
    NotStarted,
    Running,
    Stopping,
    Stopped
};

const char* IntentName(Intent intent);
const char* OriginName(Origin origin);
/// A surface's label ("gdb", "webapi", "qt-timeline", ...) as an origin; unknown labels are Internal
Origin OriginFromName(const std::string& name);
const char* OutcomeName(Outcome outcome);
const char* PhaseName(Phase phase);

/// Who asks: the surface and, for surfaces with several clients, which one
struct Requester
{
    Origin origin = Origin::Internal;
    std::string client;  ///< e.g. a GDB / DeZog connection id; empty for single-client surfaces

    bool operator==(const Requester& other) const { return origin == other.origin && client == other.client; }
};

struct IntentOptions
{
    std::string reason;          ///< "user", "breakpoint", "attach", ...
    bool onlyIfOwner = false;    ///< Run lifts only a pause this requester set (DeZog close, TTD resume)
};

class CommandContext;
using CommandBody = std::function<void(CommandContext&)>;

struct CommandOptions
{
    bool drivesZ80 = false;                ///< the body executes instructions (stepping, replay, RZX seek)
    bool pauseFirst = false;               ///< apply intent Pause before the body ("stepping leaves it paused")
    std::optional<Intent> thenIntent;      ///< applied right after the body, nothing in between (load and run)
};

/// What a command body sees
class CommandContext
{
public:
    bool IsAborted() const { return _aborted && _aborted->load(std::memory_order_acquire); }
    uint64_t Seq() const { return _seq; }

private:
    friend class ExecutionArbiter;
    const std::atomic<bool>* _aborted = nullptr;
    uint64_t _seq = 0;
};

struct Result
{
    Outcome outcome = Outcome::Pending;
    uint64_t seq = 0;
    Intent intent = Intent::Pause;  ///< for intents: the intent in force when this one completed
    std::string detail;             ///< refusal reason or the holder for Busy
};

namespace detail
{
struct Request;
}

class ExecutionArbiter;

/// Handle on a submitted request. Copyable; Wait from several threads is fine.
class Ticket
{
public:
    Ticket() = default;
    bool Valid() const { return _request != nullptr; }
    bool Done() const;
    /// Waits until the request completes or the deadline passes. A request still queued at the deadline stays
    /// queued (an intent is never lost): the result is Pending, with the holder in `detail`.
    /// Waiting on the thread that holds the CPU for a request queued behind its own command would deadlock:
    /// it returns Busy("self-wait") at once instead.
    Result Wait(Clock::time_point deadline);
    Result Wait(std::chrono::milliseconds timeout) { return Wait(Clock::now() + timeout); }
    Result Peek() const;

private:
    friend class ExecutionArbiter;
    Ticket(ExecutionArbiter* arbiter, std::shared_ptr<detail::Request> request)
        : _arbiter(arbiter), _request(std::move(request))
    {
    }
    ExecutionArbiter* _arbiter = nullptr;
    std::shared_ptr<detail::Request> _request;
};

/// A command without a body that holds the CPU until EndHold (the compat form of a silent Pause / Resume pair).
/// Inline (nested) when the thread already held the CPU.
struct HoldToken
{
    bool Valid() const { return request != nullptr; }
    /// Stop (or AbortCurrent) asked the holder to end: a driving holder checks it in its loop
    bool Aborted() const;
    std::shared_ptr<detail::Request> request;
    bool inlined = false;
    std::thread::id thread;
};

/// One applied decision, for inspection and for checking against the reference model
struct Decision
{
    enum class Kind : uint8_t
    {
        IntentApplied,
        IntentCoalesced,
        IntentRefused,
        CommandStarted,
        CommandFinished,
        Busy,
        PriorityPause,
        Stop
    };
    Kind kind = Kind::IntentApplied;
    uint64_t seq = 0;
    Outcome outcome = Outcome::Pending;
    Intent intentBefore = Intent::Pause;
    Intent intentAfter = Intent::Pause;
    bool edge = false;     ///< the intent changed (one broadcast)
    bool inlined = false;   ///< a command run inline by the thread holding the CPU
    bool command = false;   ///< the request is a command (an IntentApplied with it is the command's own effect)
    bool priority = false;  ///< decided by the priority lane (PauseFromDriver), out of queue order by design
    uint64_t batch = 0;                ///< queued intents decided together carry one batch number (0 = none)
    Intent requested = Intent::Pause;  ///< for intents: what was asked
    bool onlyIfOwner = false;
    Requester requester;
    std::string tag;
    std::string detail;  ///< refusal reason
};

struct SnapshotEntry
{
    uint64_t seq = 0;
    bool command = false;
    Intent intent = Intent::Pause;
    Requester requester;
    std::string tag;
    double ageMs = 0;
};

struct Snapshot
{
    Phase phase = Phase::NotStarted;
    Intent intent = Intent::Pause;
    Requester intentSetBy;
    std::string intentReason;
    uint64_t intentSeq = 0;
    double intentAgeMs = 0;
    bool loopAttached = false;
    bool loopParked = false;
    bool mayRun = false;
    std::optional<SnapshotEntry> serving;
    bool servingDrivesZ80 = false;
    std::vector<SnapshotEntry> queue;
    std::optional<Requester> claim;
    uint64_t applied = 0, coalesced = 0, refused = 0, busy = 0, aborted = 0, edges = 0;
};

/// Broadcast of an applied intent edge. Delivered in application order, outside the arbiter's lock.
struct IntentEdge
{
    Intent intent = Intent::Pause;
    Requester requester;
    std::string reason;
    uint64_t seq = 0;
};

class ExecutionArbiter
{
public:
    ExecutionArbiter();
    ~ExecutionArbiter();
    ExecutionArbiter(const ExecutionArbiter&) = delete;
    ExecutionArbiter& operator=(const ExecutionArbiter&) = delete;

    /// region <Configuration>
    void SetThrottle(std::chrono::milliseconds window);
    void SetEdgeListener(std::function<void(const IntentEdge&)> listener);
    /// Called synchronously, under the arbiter's lock, at the moment an intent edge is applied: before the loop can
    /// act on it. `parked`: nothing executes right now. Must not call back into the arbiter (state that has to change
    /// before the machine runs again: the registers at the stop, host output holds)
    void SetEdgeGate(std::function<void(const IntentEdge&, bool parked)> gate);
    void SetDecisionLog(bool enabled);
    std::vector<Decision> TakeDecisions();
    /// Called at every lock release inside the arbiter (tests inject yields there to vary interleavings)
    void SetInterleaveHook(std::function<void()> hook);
    /// endregion </Configuration>

    /// region <Requests>
    Ticket SubmitIntent(Intent intent, const Requester& requester, IntentOptions options = {});
    /// Synchronous command: the body runs on this thread with the CPU held. Inline (at once) when this thread
    /// already holds the CPU: inside another command's body, or the main loop's thread in a hook.
    Result Execute(const Requester& requester, std::string tag, const CommandBody& body, CommandOptions options = {},
                   Clock::time_point deadline = Clock::time_point::max());
    /// Asynchronous command: the body runs on the parked main loop's thread, or on the thread that pumps the queue
    /// when no loop runs.
    Ticket Post(const Requester& requester, std::string tag, CommandBody body, CommandOptions options = {});
    /// Hold the CPU from here until EndHold: a command whose body is the caller's own code. Waits until served or
    /// the deadline (then an invalid token, `result` says why). EndHold may come from another thread.
    /// `options`: drivesZ80 (the holder executes instructions: IsParked is false for everybody else meanwhile);
    /// pauseFirst (applies intent Pause when served; not when nested - the outer holder decides)
    HoldToken BeginHold(const Requester& requester, std::string tag, Clock::time_point deadline,
                        Result* result = nullptr, const CommandOptions& options = {});
    void EndHold(HoldToken& token);
    /// A long driving command (a direct run) lends the CPU between two frames: the commands queued behind it that do
    /// not drive the Z80 and whose submitter waits for them (holds, Execute: TTD operations, saves, media, readers)
    /// are served now, one after another, while it waits; then it goes on where it stood. Intents stay queued (a
    /// pause applies at the command's end) and so do driving commands (two direct runs never interleave). Called on
    /// the command's own thread; returns how many it lent to
    size_t LendCpu(const HoldToken& token);
    /// endregion </Requests>

    /// region <Priority lane>
    /// From the driver's own thread (breakpoint, TTD end of a detached session): intent Pause at once. It is newer
    /// than every queued intent, which complete as Coalesced.
    void PauseFromDriver(const Requester& requester, std::string reason);
    /// Abort the command being served (pause during a long run, GDB interrupt). False when nothing is served.
    bool AbortCurrent(const Requester& requester, const std::string& tag);
    /// Abort the command being served only if it drives the Z80 (a direct run): the run ends at its next instruction
    /// boundary. The user taking control back from an automation session. False when no direct run is served
    bool AbortDriving();
    /// The command being served was aborted (read per instruction by the Run* loops: one relaxed load)
    bool ServingAborted() const { return _servingAborted.load(std::memory_order_relaxed); }
    /// Stop: queued requests are Refused("stopping"), the served command is aborted, the loop is told to exit
    void BeginStop();
    /// The machine was removed (Emulator::Retire, after Stop): from now on intents and commands that drive the Z80
    /// are Refused("retired"); commands that do not drive it (readers, saves) still run. One way
    void Retire();
    /// endregion </Priority lane>

    /// region <Run-control claim>
    bool TakeClaim(const Requester& owner);
    void ReleaseClaim(const Requester& owner);
    /// endregion </Run-control claim>

    /// region <Main loop>
    /// A main loop is about to start (StartAsync, before its thread exists): phase Running, the loop counted as
    /// parked until its first LoopWait lets it run
    void ExpectMainLoop();
    /// The calling thread becomes the main loop: phase Running. Its first LoopWait decides whether it may run.
    void AttachMainLoop();
    /// The loop has exited (or never started): phase Stopped. Idempotent, any thread.
    void DetachMainLoop();
    bool IsLoopThread() const;
    /// One relaxed load: the loop must call LoopWait at this instruction boundary
    bool ShouldYield() const { return _yield.load(std::memory_order_relaxed); }
    /// Notified when ShouldYield turns true: the loop's wait between frames ends at once (MainLoop::Run)
    WakeSignal& LoopWake() { return _loopWake; }
    /// Called by the loop at a boundary: pumps the queue, parks while it may not run (serving posted commands).
    /// Returns false when the loop must exit (stopping).
    bool LoopWait();
    /// endregion </Main loop>

    /// region <Queries>
    Intent CurrentIntent() const;
    /// Lock-free read of the intent in force (for hot status reads)
    Intent IntentRelaxed() const { return _intentAtomic.load(std::memory_order_relaxed); }
    Phase CurrentPhase() const;
    /// Nothing executes right now, and that is confirmed (no loop, or the loop parked; no driving command)
    bool IsParked() const;
    bool WaitParked(Clock::time_point deadline);
    /// True when this thread holds the CPU (a command body, or the main loop's thread)
    bool HoldsCpu() const;
    /// The command another thread is serving now (a hold, a direct run, a posted command), described for the user:
    /// "'run_frames' (webapi)"; none: nullopt. A pause asked meanwhile waits for it (Emulator::Pause does not)
    std::optional<std::string> ServingCommand() const;
    Snapshot GetSnapshot() const;
    /// endregion </Queries>

private:
    friend class Ticket;
    using Lock = std::unique_lock<std::mutex>;

    std::shared_ptr<detail::Request> NewRequestLocked(bool command, const Requester& requester, std::string tag);
    void PumpLocked(Lock& lock);
    bool ApplyIntentRunLocked(Clock::time_point now);
    bool GrantedLocked() const;
    void StartServingLocked(const std::shared_ptr<detail::Request>& request);
    void FinishServingLocked(const std::shared_ptr<detail::Request>& request, Outcome outcome);
    void RunBodyLocked(Lock& lock, const std::shared_ptr<detail::Request>& request);
    void SetIntentLocked(Intent intent, const Requester& requester, const std::string& reason, uint64_t seq);
    void CompleteLocked(const std::shared_ptr<detail::Request>& request, Outcome outcome, std::string detail = {});
    void RecomputeLocked();
    bool RefusedByClaimLocked(const detail::Request& request) const;
    /// "stopping", "retired" or nullptr: the machine's lifecycle refuses the request
    const char* LifecycleRefusalLocked(const detail::Request& request) const;
    std::string HolderLocked() const;
    void LogLocked(Decision::Kind kind, const detail::Request* request, Outcome outcome, Intent before, bool edge,
                   bool inlined = false, bool priority = false);
    Clock::time_point NextWakeLocked() const;
    void Unlock(Lock& lock);
    void DeliverEdges();
    void Released();
    Result WaitTicket(const std::shared_ptr<detail::Request>& request, Clock::time_point deadline);
    Result ResultOf(const detail::Request& request) const;
    Result RunInline(const std::shared_ptr<detail::Request>& request);

    mutable std::mutex _mutex;
    std::condition_variable _cv;

    Phase _phase = Phase::NotStarted;
    Intent _intent = Intent::Pause;
    Requester _intentBy{Origin::Lifecycle, {}};
    std::string _intentReason = "created";
    uint64_t _intentSeq = 0;
    Clock::time_point _intentAt = Clock::now();

    uint64_t _nextSeq = 1;
    std::deque<std::shared_ptr<detail::Request>> _queue;
    std::shared_ptr<detail::Request> _serving;
    /// The driving command that lent the CPU (LendCpu): nothing else is served until it has it back
    std::shared_ptr<detail::Request> _lender;
    std::thread::id _servingThread;
    std::vector<std::thread::id> _inlineThreads;  ///< threads running inline bodies (nested commands)

    bool _loopAttached = false;
    bool _loopParked = false;
    bool _retired = false;   ///< Retire(): intents and driving commands are refused for good
    std::thread::id _loopThread;

    std::optional<Requester> _claim;
    std::chrono::milliseconds _throttle{0};

    std::atomic<bool> _yield{false};
    WakeSignal _loopWake;
    std::atomic<bool> _servingAborted{false};
    std::atomic<Intent> _intentAtomic{Intent::Pause};
    std::atomic<std::thread::id> _loopThreadAtomic{};
    bool _mayRun = false;

    std::deque<IntentEdge> _edges;
    std::mutex _deliverMutex;
    std::function<void(const IntentEdge&)> _edgeListener;
    std::function<void(const IntentEdge&, bool)> _edgeGate;
    std::function<void()> _interleaveHook;

    bool _logEnabled = false;
    uint64_t _logBatch = 0;
    uint64_t _nextBatch = 1;
    std::vector<Decision> _log;

    uint64_t _applied = 0, _coalesced = 0, _refused = 0, _busy = 0, _aborted = 0, _edgeCount = 0;
};

}  // namespace execution
