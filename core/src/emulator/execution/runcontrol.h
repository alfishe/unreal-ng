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

#include "emulator/execution/executionarbiter.h"
#include "emulator/state/statenode.h"

class Emulator;
class EmulatorContext;
enum EmulatorStateEnum : uint8_t;

namespace execution
{

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

    std::shared_ptr<Emulator> _emulator;
};

}  // namespace execution
