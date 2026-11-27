#pragma once

/// @file runcontrol.h
/// @brief Run control of one instance for every surface (control surfaces architecture §3): pause, resume, the
/// debugger's steps and runs, reset, NMI and turbo. The WebAPI lifecycle and debug routes and the actions of the
/// ActionRegistry (core/src/emulator/actions) call it, so the claim rule, the arbiter outcomes and the reply fields
/// live in one place.
///
/// Who asks is an execution::Requester: the session that holds run control (its surface and id) or the surface alone.
/// Every call that advances the CPU is refused "Run-control held" while another surface holds the claim, as the
/// routes did; a pause never is (the arbiter decides it: pending behind a command, refused for a claim).
///
/// Calls block the caller until the machine did it (a step, a frame, a pause up to `wait`): call them from a request
/// worker or the instance's actions worker, never from the emulation thread.

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>

#include "debugger/targets/debugcpuid.h"
#include "emulator/execution/executionarbiter.h"
#include "emulator/state/statenode.h"

class Emulator;
class GSCardTarget;
namespace framewatch { struct Spec; }
class EmulatorContext;
enum EmulatorStateEnum : uint8_t;

struct MainPosition;
struct StopPositions;

namespace execution
{

/// A card's stop: where the main CPU parked with it, its instruction's progress (gs-debugger design §5.2, §5.4).
/// The node every surface sends as `main` (the card step's `stop`, the `debug` topic's `paused` event)
StateNode MainPositionNode(const MainPosition& main);
/// The same in one line, the node's `text` ("main #8123: T 9 of 11, port write; 2 T past the card")
std::string MainPositionText(const MainPosition& main);
/// Both lanes at a stop (gs-debugger design §7.2), the paused event's and the snapshot's `positions`:
/// [{cpu main, frame, t, pc, elapsed_t?, total_t?, bus_cycle?, text?}, {cpu gs | ms, frame, units, cycle, clock_hz,
/// units_per_t, pc, gap_t, now_t, frame_end}]. `main`: a card's stop, main's instruction progress (MainPositionAt)
StateNode PositionsNode(const StopPositions& positions, const MainPosition& main);

/// region <Counted runs: limits and the answer every plane sends (re-api-efficiency F1)>
/// The most one remote call runs (REST, WebSocket, MCP, CLI, the actions). A larger count is refused with this limit
/// named, never cut: runs on the timeline are split-independent (Emulator::RunNFrames), so a client splits a longer
/// run into calls and lands exactly where one call would. The Lua and Python bindings run in-process and take any count
constexpr uint64_t kMaxRunFrames = 200000;            ///< as run_until's max_frames (framewatch::kMaxRunFrames)
constexpr uint64_t kMaxRunTStates = 14000000000ull;   ///< about 200 000 frames of a 3.5 MHz machine
constexpr uint64_t kMaxRunSteps = 10000000;           ///< instructions of POST /steps

/// What a counted run did, as every plane reports it
struct RunOutcome
{
    const char* unit = "frames";   ///< "frames", "tstates" or "instructions": what `requested` counts
    uint64_t requested = 0;
    uint64_t framesRun = 0;        ///< frame ends passed
    uint64_t tstatesRun = 0;       ///< CPU T-states run
    uint64_t instructionsRun = 0;  ///< steps only
    bool reached = false;          ///< all of it ran
};
/// A frames / t-states run's outcome from what Emulator::RunNFrames / RunTStates returned (DirectRunResult)
inline RunOutcome RunOutcomeOf(const char* unit, uint64_t requested, uint64_t framesRun, uint64_t tstatesRun, bool reached)
{
    RunOutcome outcome;
    outcome.unit = unit;
    outcome.requested = requested;
    outcome.framesRun = framesRun;
    outcome.tstatesRun = tstatesRun;
    outcome.reached = reached;
    return outcome;
}
/// The answer's fields: count (as asked), frames_run, tstates_run (instructions_run / executed for steps), frame and t
/// where it ended, stop {cpu, reason done / breakpoint / stopped, breakpoint_id, address, access, cpu_pc} (steps keep
/// their reason "step" for done), message (RunOutcomeText)
StateNode RunOutcomeNode(Emulator& emulator, const RunOutcome& outcome);
/// One line built from what ran, never from the request alone: "Ran 31000 frames (frame 31012, t 1234)", "Ran 12 of
/// 31000 frames: stopped at breakpoint #2 (execute $8000) (frame 24, t 5678)"
std::string RunOutcomeText(Emulator& emulator, const RunOutcome& outcome);
/// The refusal text of a count out of 1..`max` ("count 300000: 1..200000 frames in one call; split the run into calls,
/// they end where one call would")
std::string RunCountRefusal(const char* unit, uint64_t count, uint64_t max);
/// endregion </Counted runs>

/// One call's answer: the route's status, its error phrase and the body every surface converts
struct RunReply
{
    enum class Status : uint8_t
    {
        Ok,
        Queued,      ///< a pause behind a command (or still queued after `wait`): it applies when that ends
        NotFound,    ///< no such instance
        Held,        ///< another surface holds run control ("Run-control held")
        Refused,     ///< the arbiter refused it (a claim, not the owner of the pause)
        Revoked,     ///< the user took control back while the run went on: final, do not retry
        BadRequest,  ///< a value out of range
        Failed       ///< the machine could not do it ("not paused", an exception)
    };

    Status status = Status::Ok;
    std::string error;    ///< "" on success; else "Run-control held", "Not Found", "revoked_by_user", ...
    std::string message;  ///< one sentence a user can act on
    StateNode body = StateNode::Object();

    bool Ok() const { return status == Status::Ok || status == Status::Queued; }
    /// 200, 202 (queued), 404, 409, 400, 500
    int HttpStatus() const;
};

class RunControl
{
public:
    explicit RunControl(std::shared_ptr<Emulator> emulator);

    /// "running", "paused", "stopped", ... as every surface names the state
    static const char* StateName(EmulatorStateEnum state);
    /// Does `requester` hold the claim? False when nobody holds it
    static bool Holds(EmulatorContext* context, const Requester& requester);
    /// Refused "Run-control held" for a CPU-advancing call: someone holds the claim and it is not `requester`
    static bool HeldByOther(EmulatorContext* context, const Requester& requester);

    /// The pause; `wait` > 0 waits behind a running command too (up to 60 s)
    RunReply Pause(const Requester& requester, std::chrono::milliseconds wait = std::chrono::milliseconds(0));
    /// Resume a paused machine; refused while another surface holds the claim
    RunReply Resume(const Requester& requester);

    /// region <Steps and runs (the machine paused); a breakpoint ends them where noted>
    /// One instruction; an execution breakpoint stops it before the instruction, a memory / port one after
    RunReply StepIn(const Requester& requester);
    RunReply StepOver(const Requester& requester);
    RunReply StepOut(const Requester& requester);
    /// One video frame (breakpoints skipped, as the debugger's frame step)
    RunReply FrameStep(const Requester& requester);
    RunReply RunToInterrupt(const Requester& requester);
    RunReply RunToScanline(const Requester& requester, unsigned scanline);
    /// 1..1000 scanlines
    RunReply RunNScanlines(const Requester& requester, unsigned count);
    /// 1..kMaxRunSteps instructions (POST /steps); a breakpoint ends them. Out of range: BadRequest, nothing runs
    RunReply Steps(const Requester& requester, uint64_t count);
    /// 1..kMaxRunFrames video frames (POST /run_frames) on the direct-run timeline (split-independent); breakpoints
    /// skipped unless `breakpoints` (then the first one ends the run). Out of range: BadRequest, nothing runs. The body:
    /// RunOutcomeNode
    RunReply RunFrames(const Requester& requester, uint64_t count, bool breakpoints = false);
    /// 1..kMaxRunTStates t-states (POST /run_tstates) on the direct-run timeline, as RunFrames
    RunReply RunTStates(const Requester& requester, uint64_t count, bool breakpoints = false);
    /// To the next frame's first paper pixel (POST /run_to_pixel), breakpoints skipped
    RunReply RunToPixel(const Requester& requester);
    /// Until PC = `pc` (POST /skip_until; run to the cursor): at most `maxTStates` (0: 100 frames, at most
    /// 700000000). `breakpoints`: a breakpoint ends it first (run to the cursor); false skips them (skip_until).
    /// The body says whether it got there: `hit`
    RunReply RunUntilPc(const Requester& requester, uint16_t pc, uint64_t maxTStates, bool breakpoints);
    /// Whole frames until a frame watch's spec holds at a frame end, at most `maxFrames` (0: the default)
    /// (debugger/watch/framewatch.h): the body is framewatch::RunResultNode with `text`; a bad spec is BadRequest
    RunReply RunUntil(const Requester& requester, const framewatch::Spec& spec, unsigned maxFrames, bool breakpoints);
    /// Source level (the loaded listing, POST /listing/step_line): until PC is on another listing line; at most
    /// `maxTStates` (0: 100 frames). Conflict "No listing loaded" without one
    RunReply StepLine(const Requester& requester, uint64_t maxTStates);
    /// Until PC is at the first code byte of listing line `line` or the next one with code (POST
    /// /listing/run_to_line); at most `maxTStates` (0: 500 frames). Not Found for no such line
    RunReply RunToLine(const Requester& requester, int line, uint64_t maxTStates);
    /// A sound card CPU's step (gs-debugger design §5.3; `kind` "in", "over", "out" or "to" (run to the cursor
    /// `address`; the body adds `hit`), `count` 1..10000 for in): the
    /// machine runs until the card ends it, then pauses. The card follows the main CPU's run (catch-up at a host port
    /// access or the frame end), so the main CPU moves on to that point. A breakpoint of any CPU on the way ends it;
    /// no end within the deadline (2 s, over / out / to 5 s) pauses the machine with the reason timeout. The body: `stop`
    /// ({cpu, reason step / breakpoint / pause / timeout, cpu_pc, breakpoint_id, address, access}), the card CPU's
    /// pc and sp, the main CPU's main_pc and main_sp; card_cycle (the card's cycle of the frame), main_t (the main
    /// CPU's T of the frame) and frame, where both stand now
    RunReply CardStep(const Requester& requester, DebugCpuId cpu, const std::string& kind, unsigned count = 1,
                      uint16_t address = 0);
    /// A card CPU's run to a moment (gs-debugger TODO P5.4, the card debugger's Run to ▾), from the card's view
    /// (`cpu` gs or ms), the machine paused:
    /// - "cycle": until the card CPU reaches card cycle `value` of the ZX frame (the `gs` report's cpu.frame_cycle),
    ///   parked at the first card instruction boundary at or after it; a cycle it already stands at or passed: the
    ///   next frame's. `value` below the frame's card cycles
    /// - "int": until the card CPU accepts its next interrupt, parked at the handler's first instruction (where an
    ///   `int` event breakpoint parks)
    /// - "command": until the host's next command write (OUT #BB, the main CPU): the machine pauses after that main
    ///   instruction, the card has not taken it yet; the body adds `command` (the byte; in `stop` too)
    /// - "main_t": until the main CPU reaches T-state `value` of the frame (z80 t, 0..frame - 1; the next frame's when
    ///   at or past it): a direct run of the main CPU, the card following it
    /// A breakpoint of any CPU on the way ends it; no end in 5 s pauses the machine with the reason timeout (main_t:
    /// at most a frame, no deadline). The body: CardStep's (stop, cpu, pc, sp, main_pc, main_sp, state, card_cycle,
    /// main_t, frame) and `until`, `hit` (it got there), `target_cycle` (cycle) / `target_t` (main_t)
    RunReply CardRunTo(const Requester& requester, DebugCpuId cpu, const std::string& until, uint64_t value = 0);
    /// endregion </Steps and runs>

    /// region <Machine>
    RunReply Reset();
    /// The NMI line; `magic`: the Scorpion's magic button (MNI)
    RunReply Nmi(bool magic = false);
    RunReply SetTurbo(bool on);
    RunReply ToggleTurbo();
    /// endregion </Machine>

private:
    RunReply Missing() const;
    /// The common tail of a step or run: revoked, or "status: success" with pc, sp and the state
    RunReply Finish(const std::string& message);
    RunReply HeldReply(const char* verb) const;
    /// The breakpoint that ended the last direct run, as `stop` ({reason, breakpoint_id, address, access})
    void AddStop(RunReply& reply) const;

    /// Where the machine stands: frame counter and t
    struct Position
    {
        uint64_t frame = 0;
        uint32_t t = 0;
    };
    Position Where() const;
    /// The frames and T-states from `before` to now (steps: RunNCPUCycles reports instructions only)
    void Since(const Position& before, RunOutcome& outcome) const;
    /// BadRequest for a count out of 1..max: RunCountRefusal, with count and max in the body
    RunReply CountRefusal(const char* unit, uint64_t count, uint64_t max) const;
    /// Finish, then the counted run's fields (RunOutcomeNode)
    RunReply FinishRun(const RunOutcome& outcome);

    /// A card step's or run's messages: it ended, its deadline passed, another cause stopped it first
    struct CardRunTexts
    {
        std::string done;
        std::string timeout;
        std::string other;
    };
    /// Resumes the machine for the step armed on `target` and waits until it pauses again (`limit`: the deadline,
    /// then a pause with the reason timeout); `ended`: the step itself ended it. The body: CardBody
    RunReply RunCard(const Requester& requester, DebugCpuId cpu, GSCardTarget& target, std::chrono::milliseconds limit,
                     const CardRunTexts& texts, bool& ended);
    /// CardRunTo "main_t": the main CPU's direct run to T `value` of the frame
    RunReply CardRunToMainT(DebugCpuId cpu, GSCardTarget& target, uint64_t value);
    /// A card reply's body from Finish's (`reply`) and the stop: stop, cpu, the card's pc / sp, main_pc / main_sp,
    /// card_cycle, main_t, frame, state
    StateNode CardBody(const RunReply& reply, StateNode stop, DebugCpuId cpu, GSCardTarget& target) const;

    std::shared_ptr<Emulator> _emulator;
};

}  // namespace execution
