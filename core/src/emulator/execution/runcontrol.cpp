#include "emulator/execution/runcontrol.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <exception>

#include "common/uuid.h"
#include "debugger/debugerror.h"
#include "debugger/debugmanager.h"
#include "debugger/listing/listingparser.h"
#include "debugger/targets/gscardtarget.h"
#include "debugger/watch/framewatch.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/emulatormanager.h"
#include "emulator/notifications.h"
#include "emulator/sound/chips/gs/generalsoundcard.h"

namespace execution
{

std::string MainPositionText(const MainPosition& main)
{
    char text[192];
    const std::string gap = std::to_string(main.gapT) + " T past the card";
    if (main.frameEnd)
        return "main between frames; " + gap;
    // Device /WAIT main got in the instruction (ZX-DMA, gui-card-debugger S3): "+ 3 T wait"
    const std::string wait = main.waitT ? " + " + std::to_string(main.waitT) + " T wait" : std::string();
    if (!main.timed)
    {
        std::snprintf(text, sizeof(text), "main #%04X%s; %s", main.pc, wait.c_str(), gap.c_str());
        return text;
    }
    std::string total = std::to_string(main.totalT);
    if (main.totalTAlt)
        total += "/" + std::to_string(main.totalTAlt);
    std::snprintf(text, sizeof(text), "main #%04X%s: T %u of %s%s%s%s%s; %s", main.pc,
                  main.onBoundary ? " (finished)" : "", main.elapsedT, total.c_str(), wait.c_str(),
                  main.contended ? " contended" : "", main.busCycle >= 0 ? ", " : "",
                  main.busCycle >= 0 ? Z80MCycleDecoder::Description(main.cycles.cycles[main.busCycle].kind) : "",
                  gap.c_str());
    return text;
}

StateNode PositionsNode(const StopPositions& positions, const MainPosition& main)
{
    StateNode list = StateNode::Array();
    if (!positions.known)
        return list;
    StateNode lane = StateNode::Object();
    lane["cpu"] = std::string("main");
    lane["frame"] = positions.frame;
    lane["t"] = static_cast<int64_t>(positions.mainT);
    lane["pc"] = static_cast<int>(main.known ? main.pc : positions.mainPc);
    if (main.known && main.timed)
    {
        lane["elapsed_t"] = static_cast<int>(main.elapsedT);
        lane["total_t"] = static_cast<int>(main.totalT);
        if (main.busCycle >= 0)
            lane["bus_cycle"] = std::string(Z80MCycleDecoder::Description(main.cycles.cycles[main.busCycle].kind));
    }
    if (main.known)
        lane["text"] = MainPositionText(main);
    list.push(std::move(lane));
    static const char* const kCards[2] = {"gs", "ms"};
    for (size_t i = 0; i < 2; i++)
    {
        const CardLanePosition& card = positions.cards[i];
        if (!card.known)
            continue;
        StateNode node = StateNode::Object();
        node["cpu"] = std::string(kCards[i]);
        node["frame"] = positions.frame - (card.frameEnd && positions.frame ? 1 : 0);
        node["units"] = card.units;
        node["cycle"] = card.cycle;
        node["clock_hz"] = static_cast<int64_t>(card.clockHz);
        node["units_per_t"] = std::round(card.unitsPerT * 1000.0) / 1000.0;
        node["pc"] = static_cast<int>(card.pc);
        node["gap_t"] = std::round(card.gapT * 1000.0) / 1000.0;
        // NOW on main's clock: main's T in the card's frame (the frame before in the frame-end catch-up) minus the gap
        const double mainT = static_cast<double>(positions.mainT) + (card.frameEnd ? positions.frameT : 0);
        node["now_t"] = std::round((mainT - card.gapT) * 1000.0) / 1000.0;
        node["frame_end"] = card.frameEnd;
        list.push(std::move(node));
    }
    return list;
}

StateNode MainPositionNode(const MainPosition& main)
{
    StateNode node = StateNode::Object();
    node["text"] = MainPositionText(main);
    node["pc"] = static_cast<int>(main.pc);
    node["t_in_instruction"] = static_cast<int>(main.tInInstruction);
    node["wait_t"] = static_cast<int>(main.waitT);
    node["gap_t"] = static_cast<int>(main.gapT);
    node["frame_end"] = main.frameEnd;
    node["on_boundary"] = main.onBoundary;
    if (!main.timed)
        return node;
    node["elapsed_t"] = static_cast<int>(main.elapsedT);
    node["total_t"] = static_cast<int>(main.totalT);
    if (main.totalTAlt)
        node["total_t_alt"] = static_cast<int>(main.totalTAlt);
    node["contended"] = main.contended;
    StateNode cycles = StateNode::Array();
    for (uint8_t i = 0; i < main.cycles.count; i++)
    {
        StateNode cycle = StateNode::Object();
        cycle["kind"] = std::string(Z80MCycleDecoder::Name(main.cycles.cycles[i].kind));
        cycle["t"] = static_cast<int>(main.cycles.cycles[i].t);
        cycles.push(std::move(cycle));
    }
    node["bus_cycles"] = std::move(cycles);
    if (main.busCycle >= 0)
    {
        node["bus_cycle"] = std::string(Z80MCycleDecoder::Name(main.cycles.cycles[main.busCycle].kind));
        node["bus_cycle_index"] = static_cast<int>(main.busCycle);
    }
    return node;
}

namespace
{
/// A card's stop: where the main CPU parked with it (gs-debugger design §5.2)
void AddMainPosition(StateNode& stop, const MainPosition& main)
{
    if (main.known)
        stop["main"] = MainPositionNode(main);
}
}  // namespace

const char* RunReply::Code() const
{
    if (!code.empty())
        return code.c_str();
    switch (status)
    {
        case Status::Ok:
        case Status::Queued: return "";
        case Status::NotFound: return DebugError::kNotFound;
        case Status::Held: return DebugError::kRunControlHeld;
        case Status::Revoked: return DebugError::kRevoked;
        case Status::Refused: return DebugError::kRefused;
        case Status::BadRequest: return DebugError::kInvalidArgument;
        case Status::Failed: return DebugError::kInternal;
    }
    return DebugError::kInternal;
}

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
    stopNode["cpu"] = DebugCpuName(stop.cpu);
    stopNode["reason"] = stop.hit ? "breakpoint" : "step";
    if (stop.hit)
    {
        stopNode["breakpoint_id"] = static_cast<int>(stop.breakpointId);
        stopNode["address"] = static_cast<int>(stop.address);
        stopNode["access"] = BreakpointHitKindName(stop.kind);
        if (stop.cpu != DebugCpuId::Main)
            stopNode["cpu_pc"] = static_cast<int>(stop.cpuPc);
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

namespace
{
/// A card reply's refusal
RunReply CardRefusal(RunReply::Status status, const std::string& message, const char* code = "")
{
    RunReply reply;
    reply.status = status;
    reply.code = code;
    reply.error = status == RunReply::Status::BadRequest ? "Bad Request" : "Conflict";
    reply.message = message;
    reply.body["status"] = "error";
    reply.body["message"] = message;
    return reply;
}

std::string Hex4(unsigned value)
{
    char text[16];
    std::snprintf(text, sizeof(text), "#%04X", value & 0xFFFF);
    return text;
}
}  // namespace

RunReply RunControl::CardStep(const Requester& requester, DebugCpuId cpu, const std::string& kind, unsigned count,
                              uint16_t address)
{
    if (!_emulator)
        return Missing();
    EmulatorContext* context = _emulator->GetContext();
    if (HeldByOther(context, requester))
        return HeldReply("step");
    GSCardTarget::StepKind step = GSCardTarget::StepKind::None;
    if (kind == "in")
        step = GSCardTarget::StepKind::In;
    else if (kind == "over")
        step = GSCardTarget::StepKind::Over;
    else if (kind == "out")
        step = GSCardTarget::StepKind::Out;
    else if (kind == "to")
        step = GSCardTarget::StepKind::To;
    else
        return CardRefusal(RunReply::Status::BadRequest, "Unknown step '" + kind + "' (in, over, out, to)");
    if (count < 1 || count > 10000)
        return CardRefusal(RunReply::Status::BadRequest, "count must be 1-10000");
    GSCardTarget* target =
        context && context->pDebugManager ? context->pDebugManager->GetCardTarget(cpu) : nullptr;
    if (!target)
        return CardRefusal(RunReply::Status::Refused, std::string("cpu ") + DebugCpuName(cpu) + " not available",
                           DebugError::kCpuUnavailable);
    if (!_emulator->IsPaused())
        return CardRefusal(RunReply::Status::Refused, "Emulator is not paused: pause it before a step",
                           DebugError::kNotPaused);
    if (const std::string error = target->ArmStep(step, count, address); !error.empty())
        return CardRefusal(RunReply::Status::Refused, error);

    const std::string name = DebugCpuName(cpu);
    const bool to = step == GSCardTarget::StepKind::To;
    CardRunTexts texts;
    texts.done = to ? "Ran cpu " + name + " to " + Hex4(address) : "Step " + kind + " of cpu " + name + " completed";
    texts.timeout = to ? "cpu " + name + " did not reach " + Hex4(address) + " in time; paused"
                       : "The step of cpu " + name + " did not end in time; paused";
    texts.other = "Stopped before the step of cpu " + name + " ended";
    bool ended = false;
    RunReply reply = RunCard(requester, cpu, *target,
                             std::chrono::milliseconds(step == GSCardTarget::StepKind::In ? 2000 : 5000), texts, ended);
    if (reply.Ok() && to)
        reply.body["hit"] = ended;
    return reply;
}

RunReply RunControl::CardRunTo(const Requester& requester, DebugCpuId cpu, const std::string& until, uint64_t value)
{
    if (!_emulator)
        return Missing();
    EmulatorContext* context = _emulator->GetContext();
    if (HeldByOther(context, requester))
        return HeldReply("step");
    GSCardTarget::StepKind step = GSCardTarget::StepKind::None;
    const bool mainT = until == "main_t";
    if (until == "cycle")
        step = GSCardTarget::StepKind::Cycle;
    else if (until == "int")
        step = GSCardTarget::StepKind::Int;
    else if (until == "command")
        step = GSCardTarget::StepKind::Command;
    else if (!mainT)
        return CardRefusal(RunReply::Status::BadRequest,
                           "Unknown run '" + until + "' (cycle, int, command, main_t)");
    if (!IsCardCpu(cpu))
        return CardRefusal(RunReply::Status::BadRequest,
                           std::string("cpu ") + DebugCpuName(cpu) + ": card_run_to takes a card CPU (gs, ms)");
    GSCardTarget* target =
        context && context->pDebugManager ? context->pDebugManager->GetCardTarget(cpu) : nullptr;
    GeneralSoundCard* card = target ? target->Card() : nullptr;
    if (!card)
        return CardRefusal(RunReply::Status::Refused, std::string("cpu ") + DebugCpuName(cpu) +
                                                          " not available: no card with a CPU in its slot",
                           DebugError::kCpuUnavailable);
    if (!_emulator->IsPaused())
        return CardRefusal(RunReply::Status::Refused, "Emulator is not paused: pause it before a run",
                           DebugError::kNotPaused);
    const std::string name = DebugCpuName(cpu);

    if (mainT)
        return CardRunToMainT(cpu, *target, value);

    if (step == GSCardTarget::StepKind::Cycle)
    {
        const int64_t frameCycles = card->cardCycles(card->cardFrameLength());
        if (frameCycles > 0 && value >= static_cast<uint64_t>(frameCycles))
            return CardRefusal(RunReply::Status::BadRequest,
                               "value " + std::to_string(value) + ": a card cycle of the frame is 0.." +
                                   std::to_string(frameCycles - 1));
        if (value > UINT32_MAX)
            return CardRefusal(RunReply::Status::BadRequest, "value " + std::to_string(value) + " out of range");
    }
    if (const std::string error = target->ArmUntil(step, static_cast<uint32_t>(value)); !error.empty())
        return CardRefusal(RunReply::Status::Refused, error);

    CardRunTexts texts;
    if (step == GSCardTarget::StepKind::Cycle)
    {
        texts.done = "Ran cpu " + name + " to card cycle " + std::to_string(value);
        texts.timeout = "cpu " + name + " did not reach card cycle " + std::to_string(value) + " in time; paused";
    }
    else if (step == GSCardTarget::StepKind::Int)
    {
        texts.done = "cpu " + name + " accepted an interrupt";
        texts.timeout = "cpu " + name + " accepted no interrupt in time; paused";
    }
    else
    {
        texts.done = "The host wrote a command to cpu " + name;
        texts.timeout = "The host wrote no command to cpu " + name + " in time; paused";
    }
    texts.other = "Stopped before the run of cpu " + name + " to its " +
                  (step == GSCardTarget::StepKind::Cycle ? "cycle" : step == GSCardTarget::StepKind::Int
                                                                         ? "next interrupt"
                                                                         : "next host command") +
                  " ended";
    bool ended = false;
    RunReply reply = RunCard(requester, cpu, *target, std::chrono::milliseconds(5000), texts, ended);
    if (!reply.Ok())
        return reply;
    reply.body["until"] = until;
    reply.body["hit"] = ended;
    if (step == GSCardTarget::StepKind::Cycle)
        reply.body["target_cycle"] = static_cast<int64_t>(value);
    StateNode& stop = reply.body["stop"];
    if (ended && step == GSCardTarget::StepKind::Command)
    {
        const GSCardTarget::Stop last = target->LastStop();
        reply.body["command"] = static_cast<int>(last.value);
        stop["command"] = static_cast<int>(last.value);
    }
    // The message says where it stopped
    std::string message = reply.message;
    if (ended && step == GSCardTarget::StepKind::Cycle)
    {
        if (const StateNode* reached = reply.body.find("card_cycle"))
            message += ": stopped at card cycle " + std::to_string(reached->i) + ", " + Hex4(static_cast<unsigned>(reply.body["pc"].i));
    }
    else if (ended && step == GSCardTarget::StepKind::Int)
        message += ": parked at the handler's first instruction " + Hex4(static_cast<unsigned>(reply.body["pc"].i));
    else if (ended)
    {
        const StateNode* mainPc = reply.body.find("main_pc");
        char command[8];
        std::snprintf(command, sizeof(command), "#%02X", static_cast<unsigned>(target->LastStop().value));
        message += std::string(" ") + command + "; paused after the main instruction (main at " +
                   (mainPc ? Hex4(static_cast<unsigned>(mainPc->i)) : std::string("?")) +
                   "), the card has not taken it yet";
    }
    reply.message = message;
    reply.body["message"] = message;
    return reply;
}

RunReply RunControl::CardRunToMainT(DebugCpuId cpu, GSCardTarget& target, uint64_t value)
{
    EmulatorContext* context = _emulator->GetContext();
    Z80* z80 = context && context->pCore ? context->pCore->GetZ80() : nullptr;
    if (!z80)
        return CardRefusal(RunReply::Status::Failed, "No main CPU");
    const uint32_t frame = z80->_frameLimit;
    if (frame == 0 || value >= frame)
        return CardRefusal(RunReply::Status::BadRequest, "value " + std::to_string(value) + ": a main T-state of the "
                                                         "frame is 0.." + std::to_string(frame ? frame - 1 : 0));
    // T `value` of this frame, or the next frame's when the main CPU stands at it or past it
    const uint32_t now = z80->t;
    const uint64_t delta = value > now ? value - now : static_cast<uint64_t>(frame > now ? frame - now : 0) + value;
    const Emulator::DirectRunResult result = _emulator->RunTStates(std::max<uint64_t>(delta, 1), false);
    const Emulator::BreakpointStop stop = _emulator->LastDirectStop();
    const bool hit = result.reached && !stop.hit;
    const std::string name = DebugCpuName(cpu);
    RunReply reply = Finish(hit ? "Ran the main CPU to T " + std::to_string(z80->t) + " (asked " + std::to_string(value) +
                                      ")"
                                : "Stopped before the main CPU reached T " + std::to_string(value));
    if (!reply.Ok())
        return reply;
    StateNode stopNode = StateNode::Object();
    if (stop.hit)
    {
        stopNode["cpu"] = DebugCpuName(stop.cpu);
        stopNode["reason"] = "breakpoint";
        stopNode["breakpoint_id"] = static_cast<int>(stop.breakpointId);
        stopNode["address"] = static_cast<int>(stop.address);
        stopNode["access"] = BreakpointHitKindName(stop.kind);
        if (stop.cpu != DebugCpuId::Main)
            stopNode["cpu_pc"] = static_cast<int>(stop.cpuPc);
    }
    else
    {
        stopNode["cpu"] = "main";
        stopNode["reason"] = hit ? "step" : "pause";
    }
    reply.body = CardBody(reply, std::move(stopNode), cpu, target);
    reply.body["until"] = "main_t";
    reply.body["hit"] = hit;
    reply.body["target_t"] = static_cast<int64_t>(value);
    return reply;
}

RunReply RunControl::RunCard(const Requester& requester, DebugCpuId cpu, GSCardTarget& target,
                             std::chrono::milliseconds limit, const CardRunTexts& texts, bool& ended)
{
    const uint64_t stopsBefore = target.StopCount();
    const std::string id = _emulator->GetId();
    if (!EmulatorManager::GetInstance()->ResumeEmulator(id, requester))
    {
        target.CancelStep();
        return CardRefusal(RunReply::Status::Failed, "The machine did not resume for the step");
    }
    // Until the machine pauses again: the step's park, a breakpoint (any CPU's) or somebody's pause
    const auto deadline = std::chrono::steady_clock::now() + limit;
    bool timedOut = false;
    uint64_t seq = _emulator->DebugSeq();
    while (!_emulator->IsPaused())
    {
        const auto now = std::chrono::steady_clock::now();
        if (now >= deadline)
        {
            timedOut = true;
            break;
        }
        const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now).count();
        seq = _emulator->WaitDebugChange(seq, static_cast<uint32_t>(std::min<int64_t>(left, 50) + 1));
    }
    if (timedOut)
    {
        target.CancelStep();
        EmulatorManager::GetInstance()->PauseEmulatorWithResult(id, requester, std::chrono::milliseconds(1000));
    }
    // A pause from the host side (a command write) is applied at the main CPU's next instruction boundary
    _emulator->WaitForPauseConfirmation(1000);
    ended = target.StopCount() > stopsBefore && target.LastStop().step;
    target.CancelStep();  // another cause paused it first: the step is over too

    RunReply reply = Finish(timedOut ? texts.timeout : ended ? texts.done : texts.other);
    if (!reply.Ok())
        return reply;
    StateNode stopNode = StateNode::Object();
    const Emulator::DebugStop last = _emulator->LastStop();
    const Emulator::BreakpointStop& cause = last.breakpoint;
    if (timedOut)
    {
        stopNode["cpu"] = DebugCpuName(cpu);
        stopNode["reason"] = "timeout";
    }
    else if (ended)
    {
        stopNode["cpu"] = DebugCpuName(cpu);
        stopNode["reason"] = "step";
        stopNode["cpu_pc"] = static_cast<int>(target.LastStop().boundaryPc);
        AddMainPosition(stopNode, cause.main);
    }
    else if (last.reason == Emulator::DebugStop::Reason::Breakpoint && cause.hit)
    {
        stopNode["cpu"] = DebugCpuName(cause.cpu);
        stopNode["reason"] = "breakpoint";
        stopNode["breakpoint_id"] = static_cast<int>(cause.breakpointId);
        stopNode["address"] = static_cast<int>(cause.address);
        stopNode["access"] = BreakpointHitKindName(cause.kind);
        if (cause.cpu != DebugCpuId::Main)
            stopNode["cpu_pc"] = static_cast<int>(cause.cpuPc);
        AddMainPosition(stopNode, cause.main);
    }
    else
    {
        stopNode["cpu"] = "main";
        stopNode["reason"] = "pause";
    }
    reply.body = CardBody(reply, std::move(stopNode), cpu, target);
    return reply;
}

StateNode RunControl::CardBody(const RunReply& reply, StateNode stop, DebugCpuId cpu, GSCardTarget& target) const
{
    StateNode body = StateNode::Object();
    body["status"] = "success";
    body["message"] = reply.message;
    body["stop"] = std::move(stop);
    body["cpu"] = DebugCpuName(cpu);
    if (const StateNode* pc = reply.body.find("pc"))
        body["main_pc"] = *pc;
    if (const StateNode* sp = reply.body.find("sp"))
        body["main_sp"] = *sp;
    Z80Registers registers{};
    if (target.Registers(registers))
    {
        body["pc"] = static_cast<int>(registers.pc);
        body["sp"] = static_cast<int>(registers.sp);
    }
    // Where both CPUs stand in the frame: the card's cycle (the `gs` report's cpu.frame_cycle) and the main CPU's T
    if (GeneralSoundCard* card = target.Card(); card && card->cardCpuClockHz() != 0 && card->cardUnitsPerSecond() > 0.0)
        body["card_cycle"] = static_cast<int64_t>(card->cardCycles(card->cardFrameTime()));
    if (Z80State* z80 = _emulator->GetZ80State())
        body["main_t"] = static_cast<int64_t>(z80->t);
    if (const EmulatorContext* context = _emulator->GetContext())
        body["frame"] = static_cast<int64_t>(context->emulatorState.frame_counter);
    if (const StateNode* state = reply.body.find("state"))
        body["state"] = *state;
    return body;
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

RunReply RunControl::Steps(const Requester& requester, uint64_t count)
{
    if (!_emulator)
        return Missing();
    if (count < 1 || count > kMaxRunSteps)
        return CountRefusal("instructions", count, kMaxRunSteps);
    if (HeldByOther(_emulator->GetContext(), requester))
        return HeldReply("step");
    const Position before = Where();
    const unsigned executed = _emulator->RunNCPUCycles(static_cast<unsigned>(count), false);
    RunOutcome outcome;
    outcome.unit = "instructions";
    outcome.requested = count;
    outcome.instructionsRun = executed;
    outcome.reached = executed == count && !_emulator->LastDirectStop().hit;
    Since(before, outcome);
    return FinishRun(outcome);
}

RunReply RunControl::RunFrames(const Requester& requester, uint64_t count, bool breakpoints)
{
    if (!_emulator)
        return Missing();
    if (count < 1 || count > kMaxRunFrames)
        return CountRefusal("frames", count, kMaxRunFrames);
    if (HeldByOther(_emulator->GetContext(), requester))
        return HeldReply("step");
    const Emulator::DirectRunResult result = _emulator->RunNFrames(static_cast<unsigned>(count), !breakpoints);
    RunOutcome outcome;
    outcome.unit = "frames";
    outcome.requested = count;
    outcome.framesRun = result.frames;
    outcome.tstatesRun = result.tstates;
    outcome.reached = result.reached;
    return FinishRun(outcome);
}

RunReply RunControl::RunTStates(const Requester& requester, uint64_t count, bool breakpoints)
{
    if (!_emulator)
        return Missing();
    if (count < 1 || count > kMaxRunTStates)
        return CountRefusal("tstates", count, kMaxRunTStates);
    if (HeldByOther(_emulator->GetContext(), requester))
        return HeldReply("step");
    const Emulator::DirectRunResult result = _emulator->RunTStates(count, !breakpoints, /*onTimeline=*/true);
    RunOutcome outcome;
    outcome.unit = "tstates";
    outcome.requested = count;
    outcome.framesRun = result.frames;
    outcome.tstatesRun = result.tstates;
    outcome.reached = result.reached;
    return FinishRun(outcome);
}

RunControl::Position RunControl::Where() const
{
    Position position;
    if (const EmulatorContext* context = _emulator->GetContext())
        position.frame = context->emulatorState.frame_counter;
    if (Z80State* z80 = _emulator->GetZ80State())
        position.t = z80->t;
    return position;
}

void RunControl::Since(const Position& before, RunOutcome& outcome) const
{
    const Position after = Where();
    const EmulatorContext* context = _emulator->GetContext();
    const uint64_t frameLength =
        context != nullptr && context->pCore != nullptr ? context->pCore->GetZ80()->_frameLimit : 0;
    outcome.framesRun = after.frame >= before.frame ? after.frame - before.frame : 0;
    const int64_t tstates = static_cast<int64_t>(outcome.framesRun * frameLength) + static_cast<int64_t>(after.t) -
                            static_cast<int64_t>(before.t);
    outcome.tstatesRun = tstates > 0 ? static_cast<uint64_t>(tstates) : 0;
}

RunReply RunControl::CountRefusal(const char* unit, uint64_t count, uint64_t max) const
{
    RunReply reply;
    reply.status = RunReply::Status::BadRequest;
    reply.error = "Bad Request";
    reply.message = RunCountRefusal(unit, count, max);
    reply.body["emulator_id"] = _emulator->GetId();
    reply.body["count"] = static_cast<int64_t>(count);
    reply.body["max"] = static_cast<int64_t>(max);
    return reply;
}

RunReply RunControl::FinishRun(const RunOutcome& outcome)
{
    const std::string text = RunOutcomeText(*_emulator, outcome);
    RunReply reply = Finish(text);
    if (!reply.Ok())
        return reply;
    const StateNode node = RunOutcomeNode(*_emulator, outcome);
    for (const auto& [key, value] : node.members)
        reply.body[key] = value;
    return reply;
}

namespace
{
const char* UnitName(const char* unit, uint64_t count)
{
    const std::string u = unit;
    if (u == "tstates")
        return count == 1 ? "t-state" : "t-states";
    if (u == "instructions")
        return count == 1 ? "instruction" : "instructions";
    return count == 1 ? "frame" : "frames";
}

uint64_t RanOf(const RunOutcome& outcome)
{
    const std::string u = outcome.unit;
    return u == "tstates" ? outcome.tstatesRun : u == "instructions" ? outcome.instructionsRun : outcome.framesRun;
}
}  // namespace

std::string RunCountRefusal(const char* unit, uint64_t count, uint64_t max)
{
    return "count " + std::to_string(count) + ": 1.." + std::to_string(max) + " " + UnitName(unit, max) +
           " in one call; split the run into calls (they end where one call would)";
}

std::string RunOutcomeText(Emulator& emulator, const RunOutcome& outcome)
{
    const uint64_t ran = RanOf(outcome);
    std::string text = outcome.reached ? "Ran " + std::to_string(outcome.requested) + " " +
                                             UnitName(outcome.unit, outcome.requested)
                                       : "Ran " + std::to_string(ran) + " of " + std::to_string(outcome.requested) +
                                             " " + UnitName(outcome.unit, outcome.requested);
    const Emulator::BreakpointStop& stop = emulator.LastDirectStop();
    if (stop.hit)
    {
        char where[128];
        std::snprintf(where, sizeof(where), ": stopped at breakpoint #%u (%s $%04X%s)",
                      static_cast<unsigned>(stop.breakpointId), BreakpointHitKindName(stop.kind),
                      static_cast<unsigned>(stop.address), stop.cpu != DebugCpuId::Main ? ", card CPU" : "");
        text += where;
    }
    else if (!outcome.reached)
        text += ": stopped";
    const EmulatorContext* context = emulator.GetContext();
    const Z80State* z80 = emulator.GetZ80State();
    if (context != nullptr && z80 != nullptr)
        text += " (frame " + std::to_string(context->emulatorState.frame_counter) + ", t " + std::to_string(z80->t) + ")";
    return text;
}

StateNode RunOutcomeNode(Emulator& emulator, const RunOutcome& outcome)
{
    StateNode node = StateNode::Object();
    const std::string unit = outcome.unit;
    node["count"] = static_cast<int64_t>(outcome.requested);
    node["unit"] = unit;
    node["frames_run"] = static_cast<int64_t>(outcome.framesRun);
    node["tstates_run"] = static_cast<int64_t>(outcome.tstatesRun);
    if (unit == "instructions")
    {
        node["instructions_run"] = static_cast<int64_t>(outcome.instructionsRun);
        node["executed"] = static_cast<int64_t>(outcome.instructionsRun);
    }
    if (const EmulatorContext* context = emulator.GetContext())
        node["frame"] = static_cast<int64_t>(context->emulatorState.frame_counter);
    if (const Z80State* z80 = emulator.GetZ80State())
        node["t"] = static_cast<int64_t>(z80->t);

    const Emulator::BreakpointStop& stop = emulator.LastDirectStop();
    StateNode stopNode = StateNode::Object();
    stopNode["cpu"] = DebugCpuName(stop.cpu);
    // Steps keep their reason "step" for a run that did it all (the debuggers read it)
    stopNode["reason"] = stop.hit ? "breakpoint" : !outcome.reached ? "stopped" : unit == "instructions" ? "step" : "done";
    if (stop.hit)
    {
        stopNode["breakpoint_id"] = static_cast<int>(stop.breakpointId);
        stopNode["address"] = static_cast<int>(stop.address);
        stopNode["access"] = BreakpointHitKindName(stop.kind);
        if (stop.cpu != DebugCpuId::Main)
            stopNode["cpu_pc"] = static_cast<int>(stop.cpuPc);
    }
    node["stop"] = std::move(stopNode);
    node["message"] = RunOutcomeText(emulator, outcome);
    return node;
}

void RunControl::AddStop(RunReply& reply) const
{
    const Emulator::BreakpointStop& stop = _emulator->LastDirectStop();
    StateNode stopNode = StateNode::Object();
    stopNode["cpu"] = DebugCpuName(stop.cpu);
    stopNode["reason"] = stop.hit ? "breakpoint" : "step";
    if (stop.hit)
    {
        stopNode["breakpoint_id"] = static_cast<int>(stop.breakpointId);
        stopNode["address"] = static_cast<int>(stop.address);
        stopNode["access"] = BreakpointHitKindName(stop.kind);
        if (stop.cpu != DebugCpuId::Main)
            stopNode["cpu_pc"] = static_cast<int>(stop.cpuPc);
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

RunReply RunControl::RunUntil(const Requester& requester, const framewatch::Spec& spec, unsigned maxFrames,
                              bool breakpoints)
{
    if (!_emulator)
        return Missing();
    if (HeldByOther(_emulator->GetContext(), requester))
        return HeldReply("step");
    const framewatch::RunResult result = framewatch::RunUntil(_emulator->GetContext(), spec, maxFrames, breakpoints);
    if (result.stop == "error")
    {
        RunReply refusal;
        refusal.status = RunReply::Status::BadRequest;
        refusal.error = "Bad Request";
        refusal.message = result.error;
        refusal.body["emulator_id"] = _emulator->GetId();
        return refusal;
    }
    const std::string text = framewatch::RunResultText(result);
    RunReply reply = Finish(text);
    if (!reply.Ok())
        return reply;
    const StateNode node = framewatch::RunResultNode(result);
    for (const auto& [key, value] : node.members)
        reply.body[key] = value;
    reply.body["text"] = text;
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
