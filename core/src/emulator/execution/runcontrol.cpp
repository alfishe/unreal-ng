#include "emulator/execution/runcontrol.h"

#include <algorithm>
#include <exception>

#include "common/uuid.h"
#include "debugger/debugmanager.h"
#include "debugger/listing/listingparser.h"
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

RunReply RunControl::Steps(const Requester& requester, unsigned count)
{
    if (!_emulator)
        return Missing();
    if (HeldByOther(_emulator->GetContext(), requester))
        return HeldReply("step");
    count = std::clamp(count, 1u, 100000u);  // safety limit, as POST /steps
    const unsigned executed = _emulator->RunNCPUCycles(count, false);
    RunReply reply = Finish("Executed " + std::to_string(executed) + " of " + std::to_string(count) + " instructions");
    if (reply.Ok())
    {
        reply.body["count"] = count;
        reply.body["executed"] = executed;
    }
    return reply;
}

RunReply RunControl::RunFrames(const Requester& requester, unsigned count)
{
    if (!_emulator)
        return Missing();
    if (HeldByOther(_emulator->GetContext(), requester))
        return HeldReply("step");
    count = std::clamp(count, 1u, 10000u);  // safety limit, as POST /run_frames
    _emulator->RunNFrames(count);
    RunReply reply = Finish("Ran " + std::to_string(count) + " frames");
    if (reply.Ok())
        reply.body["count"] = count;
    return reply;
}

RunReply RunControl::RunTStates(const Requester& requester, uint64_t count)
{
    if (!_emulator)
        return Missing();
    if (HeldByOther(_emulator->GetContext(), requester))
        return HeldReply("step");
    count = std::clamp<uint64_t>(count, 1, 10000000);  // safety limit, as POST /run_tstates
    _emulator->RunTStates(count);
    RunReply reply = Finish("Ran " + std::to_string(count) + " t-states");
    if (reply.Ok())
        reply.body["tstates"] = static_cast<int64_t>(count);
    return reply;
}

void RunControl::AddStop(RunReply& reply) const
{
    const Emulator::BreakpointStop& stop = _emulator->LastDirectStop();
    StateNode stopNode = StateNode::Object();
    stopNode["cpu"] = "main";
    stopNode["reason"] = stop.hit ? "breakpoint" : "step";
    if (stop.hit)
    {
        stopNode["breakpoint_id"] = static_cast<int>(stop.breakpointId);
        stopNode["address"] = static_cast<int>(stop.address);
        stopNode["access"] = BreakpointHitKindName(stop.kind);
    }
    reply.body["stop"] = std::move(stopNode);
}

RunReply RunControl::RunToPixel(const Requester& requester)
{
    if (!_emulator)
        return Missing();
    if (HeldByOther(_emulator->GetContext(), requester))
        return HeldReply("step");
    _emulator->RunUntilNextScreenPixel();
    return Finish("Ran to next screen pixel");
}

namespace
{
/// The run budget of a run-until: `maxTStates`, else `frames` frames of the machine; at most 700000000
uint64_t Budget(Emulator& emulator, uint64_t maxTStates, unsigned frames)
{
    if (maxTStates == 0)
    {
        const EmulatorContext* context = emulator.GetContext();
        maxTStates = context != nullptr && context->config.frame != 0 ? static_cast<uint64_t>(context->config.frame) * frames
                                                                     : 69888ull * frames;
    }
    return std::min<uint64_t>(maxTStates, 700000000ull);
}

StateNode ListingLineNode(const ListingLine& line)
{
    StateNode node = StateNode::Object();
    node["line"] = line.lineNumber;
    node["has_code"] = line.hasCode;
    if (line.hasCode)
    {
        node["address"] = static_cast<int>(line.addressStart);
        node["address_end"] = static_cast<int>(line.addressEnd);
        node["size"] = static_cast<int64_t>(line.bytes.size());
    }
    node["source"] = line.source;
    return node;
}

/// The loaded listing, or the refusal: Failed without a debug manager, Conflict "No listing loaded"
ListingParser* Listing(Emulator& emulator, RunReply& refusal)
{
    EmulatorContext* context = emulator.GetContext();
    ListingParser* parser =
        context != nullptr && context->pDebugManager != nullptr ? context->pDebugManager->GetListingParser() : nullptr;
    if (parser == nullptr)
    {
        refusal.status = RunReply::Status::Failed;
        refusal.error = "Internal Error";
        refusal.message = "Listing parser not available";
        return nullptr;
    }
    if (!parser->IsLoaded())
    {
        refusal.status = RunReply::Status::Refused;
        refusal.error = "Conflict";
        refusal.message = "No listing loaded";
        refusal.body["hint"] = "POST /api/v1/emulator/{id}/listing/load first";
        return nullptr;
    }
    return parser;
}
}  // namespace

RunReply RunControl::RunUntilPc(const Requester& requester, uint16_t pc, uint64_t maxTStates, bool breakpoints)
{
    if (!_emulator)
        return Missing();
    if (HeldByOther(_emulator->GetContext(), requester))
        return HeldReply("step");
    const uint64_t budget = Budget(*_emulator, maxTStates, 100);
    _emulator->RunUntilCondition([pc](const Z80State& state) { return state.pc == pc; }, budget, true, !breakpoints);
    Z80State* z80 = _emulator->GetZ80State();
    const bool hit = z80 != nullptr && z80->pc == pc;
    const bool stopped = _emulator->LastDirectStop().hit;
    RunReply reply = Finish(hit ? "Reached target address"
                                : stopped ? "Stopped at a breakpoint before the target address"
                                          : "T-state budget exhausted before reaching target");
    if (!reply.Ok())
        return reply;
    reply.body["hit"] = hit;
    reply.body["target"] = static_cast<int>(pc);
    reply.body["max_tstates"] = static_cast<int64_t>(budget);
    AddStop(reply);
    return reply;
}

RunReply RunControl::StepLine(const Requester& requester, uint64_t maxTStates)
{
    if (!_emulator)
        return Missing();
    if (HeldByOther(_emulator->GetContext(), requester))
        return HeldReply("step");
    RunReply refusal;
    ListingParser* parser = Listing(*_emulator, refusal);
    Z80State* z80 = _emulator->GetZ80State();
    if (parser == nullptr)
        return refusal;
    if (z80 == nullptr)
    {
        refusal.status = RunReply::Status::Failed;
        refusal.error = "Internal Error";
        refusal.message = "Z80 state not available";
        return refusal;
    }
    const ListingLine* startLine = parser->FindLineByAddress(z80->pc);
    const int startLineNumber = startLine != nullptr ? startLine->lineNumber : -1;
    // The first instruction on another listing line; from an uncovered PC, the first covered one
    _emulator->RunUntilCondition(
        [parser, startLineNumber](const Z80State& state) {
            const ListingLine* line = parser->FindLineByAddress(state.pc);
            return line != nullptr && line->lineNumber != startLineNumber;
        },
        Budget(*_emulator, maxTStates, 100));
    z80 = _emulator->GetZ80State();
    const ListingLine* endLine = z80 != nullptr ? parser->FindLineByAddress(z80->pc) : nullptr;
    const bool lineChanged = endLine != nullptr && endLine->lineNumber != startLineNumber;
    RunReply reply =
        Finish(lineChanged ? "Stepped to a different source line" : "Stopped without reaching a different source line");
    if (!reply.Ok())
        return reply;
    reply.body["line_changed"] = lineChanged;
    if (startLine != nullptr)
        reply.body["from_line"] = ListingLineNode(*startLine);
    if (endLine != nullptr)
        reply.body["to_line"] = ListingLineNode(*endLine);
    return reply;
}

RunReply RunControl::RunToLine(const Requester& requester, int line, uint64_t maxTStates)
{
    if (!_emulator)
        return Missing();
    if (HeldByOther(_emulator->GetContext(), requester))
        return HeldReply("step");
    RunReply refusal;
    ListingParser* parser = Listing(*_emulator, refusal);
    if (parser == nullptr)
        return refusal;
    const ListingLine* target = parser->FindNextCodeLine(line);
    if (target == nullptr)
    {
        refusal.status = RunReply::Status::NotFound;
        refusal.error = "Not Found";
        refusal.message = "No code line at or after line " + std::to_string(line) + " in loaded listing";
        return refusal;
    }
    Z80State* z80 = _emulator->GetZ80State();
    if (z80 == nullptr)
    {
        refusal.status = RunReply::Status::Failed;
        refusal.error = "Internal Error";
        refusal.message = "Z80 state not available";
        return refusal;
    }
    const uint16_t address = target->addressStart;
    const bool alreadyAt = z80->pc == address;
    if (!alreadyAt)
        _emulator->RunUntilCondition([address](const Z80State& state) { return state.pc == address; },
                                     Budget(*_emulator, maxTStates, 500));
    z80 = _emulator->GetZ80State();
    const bool reached = z80 != nullptr && z80->pc == address;
    RunReply reply = Finish(alreadyAt ? "Already at target line"
                                      : reached ? "Reached target line" : "Safety limit reached before target line");
    if (!reply.Ok())
        return reply;
    reply.body["status"] = reached ? "success" : "timeout";
    reply.body["reached"] = reached;
    reply.body["already_at"] = alreadyAt;
    reply.body["target_line"] = ListingLineNode(*target);
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
