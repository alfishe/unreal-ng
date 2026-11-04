#pragma once

/// @file framewatch.h
/// @brief Frame watches (docs/inprogress/2026-11-03-run-until-watches): "tell me when the machine gets there" without
/// a polling loop. One spec - a frame-end condition (the conditional-breakpoint language), a text the ROM-font OCR
/// finds on screen, the screen changing or holding still, a game event firing - is checked at every frame end:
///   - run_until (Emulator::RunFramesUntil): a direct run of at most N frames that stops, paused, at the end of the
///     first frame where it holds. One call instead of run_frames chunks + a read per chunk;
///   - an armed watch (Manager): checked on the running machine; a hit is published (NC_FRAME_WATCH_HIT: the
///     `watch_hit` event of `debug.events`, `debug/wait` wakes) and, by default, pauses there.
/// Off costs one relaxed load per frame (Manager::Armed).

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include "emulator/state/statenode.h"

class EmulatorContext;
class BreakpointConditionProgram;

namespace framewatch
{

enum class ScreenRule : uint8_t
{
    None,
    Changed,  ///< the screen's digest differs from the one at the start
    Stable    ///< the digest stayed the same for stableFrames frame ends in a row
};

/// What to wait for. At least one rule; `all` = every given rule at the same frame end, else any of them
struct Spec
{
    std::string condition;  ///< frame-end expression: M(x), W(x), registers, FRAME, x@prev, labels (no access operands)
    std::string text;       ///< a substring of the screen's text (ROM font, the text layer in text modes)
    ScreenRule screen = ScreenRule::None;
    unsigned stableFrames = 25;
    std::string event;      ///< a game event's name (a profile's `events:`): fires after the start
    bool all = false;
    // Armed watches only
    std::string label;
    bool pause = true;      ///< pause at the hit's frame end; false: report only
    bool once = true;       ///< removed after its first hit

    bool Empty() const { return condition.empty() && text.empty() && screen == ScreenRule::None && event.empty(); }
};

constexpr unsigned kMaxStableFrames = 100000;
constexpr size_t kMaxTextLength = 80;
constexpr size_t kMaxArmed = 16;

/// One frame end where the spec held
struct Hit
{
    uint64_t frame = 0;                ///< the emulator's frame counter at that frame end
    std::vector<std::string> reasons;  ///< "if", "text", "screen", "event": the rules that held
    std::string textLine;              ///< the screen row with the text (trimmed)
    unsigned stableFor = 0;            ///< ScreenRule::Stable: frames the digest held
    uint64_t eventFires = 0;           ///< the event's fires since the start
};

/// A compiled spec and its baseline (the screen digest, the event's fires). Checked on the thread that drives the Z80
class Probe
{
public:
    /// Null with `error` for a bad spec (no rule, a condition that does not compile, an unknown game event).
    /// `dryRun`: check the condition once against the machine now (only while nothing else drives the Z80)
    static std::unique_ptr<Probe> Create(EmulatorContext* context, const Spec& spec, bool dryRun, std::string& error);
    ~Probe();

    /// Takes the baseline now (run_until does it once it holds the CPU; an armed watch at its first frame end)
    void Prime();
    bool Primed() const { return _primed; }
    /// At a frame end: true when the spec holds (`hit` filled). A condition that fails at run time ends the probe:
    /// Failed() then says why and Check never holds again
    bool Check(Hit& hit);
    bool Failed() const { return !_failure.empty(); }
    const std::string& Failure() const { return _failure; }
    const Spec& GetSpec() const { return _spec; }

private:
    Probe(EmulatorContext* context, Spec spec);
    uint64_t Digest() const;
    uint64_t EventFires() const;

    EmulatorContext* _context;
    Spec _spec;
    std::shared_ptr<const BreakpointConditionProgram> _condition;
    bool _primed = false;
    uint64_t _startDigest = 0;
    uint64_t _lastDigest = 0;
    unsigned _sameFrames = 0;
    uint64_t _startFires = 0;
    std::string _failure;
};

/// An armed watch as listed
struct WatchInfo
{
    uint32_t id = 0;
    Spec spec;
    uint64_t armedFrame = 0;
    uint64_t hits = 0;
    Hit lastHit;
    bool hasHit = false;
    std::string error;  ///< the condition failed at run time (the watch is disarmed)
};

/// The emulator's armed watches. Add / Remove / List from any thread; OnFrameEnd on the thread that drives the Z80
class Manager
{
public:
    explicit Manager(EmulatorContext* context);
    ~Manager();

    /// The id (1, 2, ...) or 0 with `error` (a bad spec, kMaxArmed reached)
    uint32_t Add(const Spec& spec, std::string& error);
    bool Remove(uint32_t id);
    void Clear();
    std::vector<WatchInfo> List() const;
    /// The watch whose hit paused the machine last (it may be gone since: a `once` one); false: none yet
    bool LastPause(WatchInfo& info) const;
    /// One relaxed load: any watch armed
    bool Armed() const { return _armed.load(std::memory_order_relaxed); }
    /// MainLoop::CompleteFrame, at the end: checks every watch. A hit is published, a pausing one pauses the machine
    /// (a direct run is not paused: it ends paused by itself), a `once` watch goes
    void OnFrameEnd(uint64_t frame);

private:
    struct Entry;
    void Publish(const WatchInfo& info, bool paused, bool removed) const;

    EmulatorContext* _context;
    mutable std::mutex _mutex;
    std::vector<std::unique_ptr<Entry>> _watches;
    uint32_t _nextId = 1;
    WatchInfo _lastPause;
    bool _hasLastPause = false;
    std::atomic<bool> _armed{false};
};

/// The spec from plane options, the same names everywhere: if (condition), text, screen (changed / stable),
/// stable_frames, event, match (any / all), label, then (pause / notify), once. Unknown names and bad values: false
/// with `error`
bool SpecFromOptions(const std::vector<std::pair<std::string, std::string>>& options, Spec& spec, std::string& error);

/// What a run_until ended with
struct RunResult
{
    bool met = false;
    std::string stop;      ///< "met", "budget", "breakpoint", "aborted", "error", "refused"
    std::string error;
    unsigned frames = 0;   ///< frame ends passed
    uint64_t frame = 0;    ///< the frame counter at the end
    double ms = 0;
    Hit hit;
    uint16_t pc = 0;
    int64_t ttdFrame = -1; ///< the TTD cursor when a history records
};

constexpr unsigned kDefaultRunFrames = 3000;
constexpr unsigned kMaxRunFrames = 200000;

/// Runs the emulator until the spec holds at a frame end or `maxFrames` frame ends passed (a direct run: it leaves
/// the machine paused). `breakpoints`: a breakpoint on the way ends it too
RunResult RunUntil(EmulatorContext* context, const Spec& spec, unsigned maxFrames, bool breakpoints);

/// run_until's options: the spec's plus max_frames (1..kMaxRunFrames, default kDefaultRunFrames) and breakpoints
/// (true: a breakpoint on the way ends the run). The watch-only ones (label, then, once) are refused here
bool RunOptions(const std::vector<std::pair<std::string, std::string>>& options, Spec& spec, unsigned& maxFrames,
                bool& breakpoints, std::string& error);

/// The armed watches' verbs for every plane: the HTTP status (200, 201, 400, 404, 503), the body and the text
struct Outcome
{
    int status = 200;
    StateNode body = StateNode::Object();
    std::string text;
};
/// Arms a watch from options (the spec's plus label, then, once): {id, watch}
Outcome AddWatch(EmulatorContext* context, const std::vector<std::pair<std::string, std::string>>& options);
/// {count, watches: [...]}
Outcome ListWatches(EmulatorContext* context);
/// One by id, or every one ("all"): {removed: [ids]}
Outcome RemoveWatch(EmulatorContext* context, const std::string& id);

/// Reports for every plane (StateNode -> JSON / table / dict) and the one-line text (CLI, MCP)
StateNode RunResultNode(const RunResult& result);
std::string RunResultText(const RunResult& result);
StateNode SpecNode(const Spec& spec);
StateNode WatchNode(const WatchInfo& info);
std::string WatchListText(const std::vector<WatchInfo>& watches);
std::string SpecText(const Spec& spec);

}  // namespace framewatch
