#include "emulator/execution/runcontrol.h"

#include <algorithm>
#include <exception>

#include "common/uuid.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/emulatormanager.h"
#include "emulator/notifications.h"

namespace execution
{

int RunReply::HttpStatus() const
{
    switch (status)
    {
        case Status::Ok: return 200;
        case Status::Queued: return 202;
        case Status::NotFound: return 404;
        case Status::Held:
        case Status::Revoked: return 409;
        case Status::Refused: return error == "Conflict" ? 409 : 400;  // a claim: 409, any other refusal: 400
        case Status::BadRequest: return 400;
        case Status::Failed: return 500;
    }
    return 500;
}

RunControl::RunControl(std::shared_ptr<Emulator> emulator) : _emulator(std::move(emulator)) {}

const char* RunControl::StateName(EmulatorStateEnum state)
{
    switch (state)
    {
        case StateInitialized: return "initialized";
        case StateRun:
        case StateResumed: return "running";  // Emulator::Resume() leaves the state at Resumed: it runs again
        case StatePaused: return "paused";
        case StateStopped: return "stopped";
        case StateRetired: return "retired";  // removed, kept alive by a holder until it lets go
        default: return "unknown";
    }
}

bool RunControl::Holds(EmulatorContext* context, const Requester& requester)
{
    if (context == nullptr || requester.client.empty() || !context->IsRunControlClaimed())
        return false;
    return context->HasRunControl(unreal::UUID(requester.client));
}

bool RunControl::HeldByOther(EmulatorContext* context, const Requester& requester)
{
    return context != nullptr && context->IsRunControlClaimed() && !Holds(context, requester);
}

RunReply RunControl::Missing() const
{
    RunReply reply;
    reply.status = RunReply::Status::NotFound;
    reply.error = "Not Found";
    reply.message = "Emulator with specified ID not found";
    return reply;
}

RunReply RunControl::HeldReply(const char* verb) const
{
    RunReply reply;
    reply.status = RunReply::Status::Held;
    reply.error = "Run-control held";
    reply.message = "Run-control held by " + _emulator->GetContext()->GetRunControlState().surfaceLabel + ". Use that "
                    "surface to " + verb + ".";
    reply.body["emulator_id"] = _emulator->GetId();
    return reply;
}

RunReply RunControl::Finish(const std::string& message)
{
    RunReply reply;
    if (_emulator->LastDirectRunAborted())
    {
        // The user took control back from the automation session while this ran: final, the client stops
        reply.status = RunReply::Status::Revoked;
        reply.error = "revoked_by_user";
        reply.message = "The user took control back (at the GUI or from a companion); this session has ended. Do not "
                        "retry, and do not take control again without asking the user.";
        reply.body["retry"] = false;
        return reply;
    }
    reply.body["status"] = "success";
    reply.body["message"] = message;
    if (Z80State* z80 = _emulator->GetZ80State())
    {
        reply.body["pc"] = static_cast<int>(z80->pc);
        reply.body["sp"] = static_cast<int>(z80->sp);
    }
    reply.body["state"] = StateName(_emulator->GetState());
    reply.message = message;
    return reply;
}

RunReply RunControl::Pause(const Requester& requester, std::chrono::milliseconds wait)
{
    if (!_emulator)
        return Missing();
    const std::string id = _emulator->GetId();
    wait = std::min(wait, std::chrono::milliseconds(60000));
    const Result result = EmulatorManager::GetInstance()->PauseEmulatorWithResult(id, requester, wait);
    RunReply reply;
    switch (result.outcome)
    {
        case Outcome::Applied:
        case Outcome::Coalesced:
            reply.body["status"] = "success";
            reply.message = "Emulator paused";
            break;
        case Outcome::Pending:
            // Honest: the machine still runs the command ahead; registers read now are not the paused ones
            reply.status = RunReply::Status::Queued;
            reply.body["status"] = "queued";
            reply.message = "The pause is " + result.detail + ". Add ?wait=true to wait for it";
            reply.body["waiting_for"] = result.detail;
            break;
        case Outcome::Busy:
            reply.status = RunReply::Status::Queued;
            reply.body["status"] = "busy";
            reply.message = "The pause is " + result.detail + " (it stays queued and applies when that ends)";
            reply.body["waiting_for"] = result.detail;
            break;
        default:
            reply.status = RunReply::Status::Refused;
            reply.error = result.detail.find("claim") != std::string::npos ? "Conflict" : "Bad Request";
            reply.body["status"] = "error";
            reply.message = "Pause refused: " + result.detail;
            break;
    }
    reply.body["message"] = reply.message;
    reply.body["emulator_id"] = id;
    reply.body["state"] = StateName(_emulator->GetState());
    return reply;
}

RunReply RunControl::Resume(const Requester& requester)
{
    if (!_emulator)
        return Missing();
    if (HeldByOther(_emulator->GetContext(), requester))
        return HeldReply("resume");
    const std::string id = _emulator->GetId();
    // The session holding control resumes as itself; the arbiter refuses anybody else's run while it holds it
    const bool success = EmulatorManager::GetInstance()->ResumeEmulator(id, requester);
    RunReply reply;
    if (!success)
    {
        reply.status = RunReply::Status::BadRequest;
        reply.error = "Bad Request";
    }
    reply.message = success ? "Emulator resumed" : "Failed to resume emulator (not paused or error)";
    reply.body["status"] = success ? "success" : "error";
    reply.body["message"] = reply.message;
    reply.body["emulator_id"] = id;
    reply.body["state"] = StateName(_emulator->GetState());
    return reply;
}

RunReply RunControl::StepIn(const Requester& requester)
{
    if (!_emulator)
        return Missing();
    if (HeldByOther(_emulator->GetContext(), requester))
        return HeldReply("step");
    // Breakpoints are honoured: an execution breakpoint stops the step before its instruction, a memory or port
    // breakpoint after it (the reply's "stop" says which)
    _emulator->RunSingleCPUCycle(false);
    const Emulator::BreakpointStop& stop = _emulator->LastDirectStop();
    const bool executed = !(stop.hit && stop.kind == BreakpointHitKind::Execute);
    RunReply reply = Finish(executed ? "Executed 1 instruction" : "Stopped at a breakpoint before the instruction");
    if (!reply.Ok())
        return reply;
    StateNode body = StateNode::Object();
    body["status"] = "success";
    body["message"] = reply.message;
    body["executed"] = executed ? 1 : 0;
    StateNode stopNode = StateNode::Object();
    stopNode["cpu"] = "main";
    stopNode["reason"] = stop.hit ? "breakpoint" : "step";
    if (stop.hit)
    {
        stopNode["breakpoint_id"] = static_cast<int>(stop.breakpointId);
        stopNode["address"] = static_cast<int>(stop.address);
        stopNode["access"] = BreakpointHitKindName(stop.kind);
    }
    body["stop"] = std::move(stopNode);
    for (const char* key : {"pc", "sp", "state"})
        if (const StateNode* value = reply.body.find(key))
            body[key] = *value;
    reply.body = std::move(body);
    return reply;
}

RunReply RunControl::StepOver(const Requester& requester)
{
    if (!_emulator)
        return Missing();
    if (HeldByOther(_emulator->GetContext(), requester))
        return HeldReply("step");
    _emulator->StepOver();
    return Finish("Step over completed");
}

RunReply RunControl::StepOut(const Requester& requester)
{
    if (!_emulator)
        return Missing();
    if (HeldByOther(_emulator->GetContext(), requester))
        return HeldReply("step");
    _emulator->StepOut();
    return Finish("Step out completed");
}

RunReply RunControl::FrameStep(const Requester& requester)
{
    if (!_emulator)
        return Missing();
    if (HeldByOther(_emulator->GetContext(), requester))
        return HeldReply("step");
    _emulator->RunFrame();
    return Finish("Ran one frame");
}

RunReply RunControl::RunToInterrupt(const Requester& requester)
{
    if (!_emulator)
        return Missing();
    if (HeldByOther(_emulator->GetContext(), requester))
        return HeldReply("step");
    _emulator->RunUntilInterrupt();
    return Finish("Ran to interrupt");
}

RunReply RunControl::RunToScanline(const Requester& requester, unsigned scanline)
{
    if (!_emulator)
        return Missing();
    if (HeldByOther(_emulator->GetContext(), requester))
        return HeldReply("step");
    _emulator->RunUntilScanline(scanline);
    RunReply reply = Finish("Ran to scanline " + std::to_string(scanline));
    if (reply.Ok())
        reply.body["scanline"] = scanline;
    return reply;
}

RunReply RunControl::RunNScanlines(const Requester& requester, unsigned count)
{
    if (!_emulator)
        return Missing();
    if (HeldByOther(_emulator->GetContext(), requester))
        return HeldReply("step");
    count = std::clamp(count, 1u, 1000u);  // safety limit
    _emulator->RunNScanlines(count);
    RunReply reply = Finish("Ran " + std::to_string(count) + " scanlines");
    if (reply.Ok())
        reply.body["count"] = count;
    return reply;
}

RunReply RunControl::Reset()
{
    if (!_emulator)
        return Missing();
    const std::string id = _emulator->GetId();
    const bool success = EmulatorManager::GetInstance()->ResetEmulator(id);
    RunReply reply;
    if (!success)
    {
        reply.status = RunReply::Status::BadRequest;
        reply.error = "Bad Request";
    }
    reply.message = success ? "Emulator reset" : "Failed to reset emulator";
    reply.body["status"] = success ? "success" : "error";
    reply.body["message"] = reply.message;
    reply.body["emulator_id"] = id;
    reply.body["state"] = StateName(_emulator->GetState());
    return reply;
}

RunReply RunControl::Nmi(bool magic)
{
    if (!_emulator)
        return Missing();
    if (magic)
        _emulator->RequestMNI();
    else
        _emulator->RequestNMI();
    RunReply reply;
    reply.message = magic ? "MNI requested (magic button)" : "NMI requested";
    reply.body["status"] = "success";
    reply.body["message"] = reply.message;
    reply.body["emulator_id"] = _emulator->GetId();
    reply.body["magic"] = magic;
    return reply;
}

RunReply RunControl::SetTurbo(bool on)
{
    if (!_emulator)
        return Missing();
    if (on)
        _emulator->EnableTurboMode();
    else
        _emulator->DisableTurboMode();
    RunReply reply;
    reply.message = on ? "Turbo on" : "Turbo off";
    reply.body["status"] = "success";
    reply.body["turbo"] = _emulator->IsTurboMode();
    return reply;
}

RunReply RunControl::ToggleTurbo()
{
    if (!_emulator)
        return Missing();
    return SetTurbo(!_emulator->IsTurboMode());
}

}  // namespace execution
