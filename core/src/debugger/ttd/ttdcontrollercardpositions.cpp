/// @file ttdcontrollercardpositions.cpp
/// @brief TimeTravelController's positions on a sound card's CPU (gs-debugger design §8; owner decision 2026-11-17,
/// Q2 of the TTD v2 phase-3 TDD): seek to a card instruction, step either way by card instructions, reverse continue
/// to the card's previous breakpoint hit.
///
/// A card position is the card's instruction count (GeneralSoundCard::cardInstructionCount, part of the card's TTD
/// state). Every move is a replay:
///   1. the latest frame whose checkpoint count is at or below the target (restores of the checkpoints only);
///   2. that frame replayed in tight mode with the card's debug loop (GSCardTarget's scan): at the target count the
///      card's whole state is copied, with the main CPU's instruction start at that moment;
///   3. the main CPU seeks to that instruction start, its last boundary at or before the card's moment (a capture in
///      the replay's first catch-up has no instruction start yet: the replay starts a frame earlier);
///   4. the card's copy is loaded. What the seek left in the card (its natural state there) is kept: before the
///      machine runs it goes back, so the main CPU finishes its instruction with the card behind it, as recorded.

#include "stdafx.h"
#include "timetravelcontroller.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <mutex>

#include "common/modulelogger.h"
#include "debugger/debugmanager.h"
#include "debugger/targets/gscardtarget.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulatorcontext.h"
#include "emulator/platform.h"
#include "emulator/sound/chips/gs/generalsoundcard.h"

namespace ttd
{

/// The card position the machine stands at
struct TimeTravelController::CardStand
{
    DebugCpuId cpu = DebugCpuId::GS;
    TTDCardPosition position;
    GSCardTarget::CardState captured;   ///< the card at the position (the scan's copy)
    GSCardTarget::CardState natural;    ///< the card as the seek to the main boundary left it
};

namespace
{
GSCardTarget* CardTargetOf(EmulatorContext* context, DebugCpuId cpu)
{
    if (!context || !context->pDebugManager || !IsCardCpu(cpu))
        return nullptr;
    return context->pDebugManager->GetCardTarget(cpu);
}

/// The card's readout where it stands (no card position)
TTDCardPosition CardReadout(EmulatorContext* context, DebugCpuId cpu, const TTDTimePoint& mainAt)
{
    TTDCardPosition p;
    p.cpu = cpu;
    GSCardTarget* target = CardTargetOf(context, cpu);
    GeneralSoundCard* card = target ? target->Card() : nullptr;
    uint64_t count = 0;
    if (!card || !card->cardInstructionCount(count))
        return p;
    p.known = true;
    p.instruction = count;
    Z80CpuRegisters regs{};
    if (card->cardRegisters(regs))
        p.pc = regs.pc;
    p.cardTime = card->cardTime();
    p.mainBoundary = mainAt;
    const Z80* z80 = context->pCore ? context->pCore->GetZ80() : nullptr;
    p.mainPc = z80 ? z80->pc : 0;
    return p;
}

/// Main CPU T-states per second at the clock the machine runs now
double MainTPerSecond(const EmulatorContext* context)
{
    const CONFIG& config = context->config;
    if (config.frame == 0 || config.frame_duration_us == 0)
        return 3500000.0;
    return static_cast<double>(context->emulatorState.BaseToCpuT(config.frame)) /
           (static_cast<double>(config.frame_duration_us) * 1e-6);
}

TTDCardMoveResult Refused(TTDCardMoveResult::Status status, std::string message)
{
    TTDCardMoveResult result;
    result.status = status;
    result.message = std::move(message);
    return result;
}
}  // namespace

std::vector<TTDCardPosition> TimeTravelController::PublishedCardPositions() const
{
    // The card position as its move published it; the other cards where they stand (read as CurrentPosition reads
    // the main CPU: a poll never parks the machine)
    std::vector<TTDCardPosition> stand;
    {
        std::lock_guard<std::mutex> lock(_cardReadoutMutex);
        stand = _cardReadouts;
    }
    std::vector<TTDCardPosition> readouts;
    if (!_context || !_context->pDebugManager)
        return readouts;
    const TTDTimePoint here = CurrentPosition();
    for (const DebugCpuId cpu : {DebugCpuId::GS, DebugCpuId::MultiSoundGS})
    {
        const auto at = std::find_if(stand.begin(), stand.end(), [cpu](const TTDCardPosition& p) { return p.cpu == cpu; });
        if (at != stand.end())
            readouts.push_back(*at);
        else if (CardTargetOf(_context, cpu) && CardTargetOf(_context, cpu)->Card())
            readouts.push_back(CardReadout(_context, cpu, here));
    }
    return readouts;
}

void TimeTravelController::PublishCardReadouts()
{
    std::vector<TTDCardPosition> stand;
    if (_cardStand)
        stand.push_back(_cardStand->position);
    std::lock_guard<std::mutex> lock(_cardReadoutMutex);
    _cardReadouts = std::move(stand);
}

TTDCardPosition TimeTravelController::CardPositionNow(DebugCpuId cpu) const
{
    if (_cardStand && _cardStand->cpu == cpu)
        return _cardStand->position;
    return CardReadout(_context, cpu, CurrentPosition());
}

bool TimeTravelController::AtCardPosition() const
{
    return _cardStand != nullptr;
}

void TimeTravelController::LeaveCardStand()
{
    if (!_cardStand)
        return;
    const std::shared_ptr<CardStand> stand = std::move(_cardStand);
    _cardStand.reset();
    if (GSCardTarget* target = CardTargetOf(_context, stand->cpu))
        GSCardTarget::LoadCardState(target->Card(), stand->natural);
    _engineMemoryKnown = false;   // the card's memory came back behind the engine
    UpdateInputWorkFlag();
    PublishCardReadouts();
}

void TimeTravelController::ReapplyCardStand()
{
    if (!_cardStand)
        return;
    if (GSCardTarget* target = CardTargetOf(_context, _cardStand->cpu))
        GSCardTarget::LoadCardState(target->Card(), _cardStand->captured);
    _engineMemoryKnown = false;
}

bool TimeTravelController::CardCountAtFrame(DebugCpuId cpu, uint64_t frame, uint64_t& count, bool& before)
{
    before = false;
    TTDSeekResult result;
    if (!SeekToInternal(TTDTimePoint{frame, 0}, &result))
    {
        before = result.beforeEarliest;
        return false;
    }
    GSCardTarget* target = CardTargetOf(_context, cpu);
    GeneralSoundCard* card = target ? target->Card() : nullptr;
    return card && card->cardInstructionCount(count);
}

bool TimeTravelController::CardCountNow(DebugCpuId cpu, uint64_t& count, uint16_t* sp)
{
    GSCardTarget* target = CardTargetOf(_context, cpu);
    GeneralSoundCard* card = target ? target->Card() : nullptr;
    if (_cardStand && _cardStand->cpu == cpu)
    {
        count = _cardStand->position.instruction;
        // The card holds the position's copy
        Z80CpuRegisters regs{};
        if (sp && card && card->cardRegisters(regs))
            *sp = regs.sp;
        return true;
    }
    if (!card)
        return false;
    // The card runs lazily: up to the main CPU's moment for the count, then back as it was (nothing parks)
    GSCardTarget::CardState saved;
    GSCardTarget::SaveCardState(card, saved);
    bool known = false;
    {
        const GSCardTarget::ScanScope scan(*target, UINT64_MAX);
        card->catchUpToHost();
        known = card->cardInstructionCount(count);
        Z80CpuRegisters regs{};
        if (sp && card->cardRegisters(regs))
            *sp = regs.sp;
    }
    GSCardTarget::LoadCardState(card, saved);
    _engineMemoryKnown = false;
    return known;
}

bool TimeTravelController::FindCardStartFrame(DebugCpuId cpu, uint64_t instruction, uint64_t hint, uint64_t& frame,
                                              TTDCardMoveResult& result)
{
    const uint64_t first = GetEarliestRecordedFrame();
    const uint64_t last = TimelineEnd().frame;
    hint = std::clamp<uint64_t>(hint, first, last);

    // The count at a frame's checkpoint; a frame the history does not hold any more counts as "too late" from below
    bool failed = false;
    bool noCount = false;
    uint64_t earliestKept = first;
    auto countAt = [&](uint64_t f, uint64_t& c) -> bool {
        bool before = false;
        if (CardCountAtFrame(cpu, f, c, before))
            return true;
        if (before)
            earliestKept = std::max(earliestKept, f + 1);
        else
        {
            GSCardTarget* target = CardTargetOf(_context, cpu);
            GeneralSoundCard* card = target ? target->Card() : nullptr;
            uint64_t ignored = 0;
            noCount = card && !card->cardInstructionCount(ignored);
            failed = !noCount;
        }
        return false;
    };
    auto refuseCount = [&]() {
        result.status = noCount ? TTDCardMoveResult::Status::NoCount : TTDCardMoveResult::Status::Failed;
        result.message = noCount ? "the recording has no instruction counts of the card's CPU (made before them): "
                                   "card positions need a recording made with them"
                                 : "a checkpoint of the history could not be restored";
        return false;
    };

    uint64_t c = 0;
    if (!countAt(hint, c))
    {
        if (!failed && !noCount)
        {
            // The hint lies before the kept history: from its first kept frame
            hint = earliestKept;
            if (hint > last || !countAt(hint, c))
                return failed || noCount ? refuseCount() : (result.status = TTDCardMoveResult::Status::AtStart, false);
        }
        else
            return refuseCount();
    }

    uint64_t lo = hint;   // C(lo) <= instruction
    uint64_t hi = 0;      // C(hi) > instruction (last + 1: none)
    if (c <= instruction)
    {
        hi = last + 1;
        if (hint < last)
        {
            uint64_t next = 0;
            if (!countAt(hint + 1, next))
                return refuseCount();
            if (next > instruction)
                hi = hint + 1;
            else
                lo = hint + 1;
        }
    }
    else
    {
        hi = hint;
        uint64_t firstCount = 0;
        if (!countAt(earliestKept, firstCount))
            return failed || noCount ? refuseCount() : (result.status = TTDCardMoveResult::Status::AtStart, false);
        if (firstCount > instruction)
        {
            result.status = TTDCardMoveResult::Status::AtStart;
            result.message = "card instruction " + std::to_string(instruction) +
                             " is before the history: it starts at card instruction " + std::to_string(firstCount);
            return false;
        }
        lo = earliestKept;
        if (hint > earliestKept + 1)
        {
            uint64_t previous = 0;
            if (!countAt(hint - 1, previous))
                return refuseCount();
            if (previous <= instruction)
                lo = hint - 1;
            else
                hi = hint - 1;
        }
    }
    // Binary search between them: the latest frame whose count is at or below the instruction
    while (hi - lo > 1)
    {
        const uint64_t mid = lo + (hi - lo) / 2;
        uint64_t m = 0;
        if (!countAt(mid, m))
            return refuseCount();
        if (m <= instruction)
            lo = mid;
        else
            hi = mid;
    }
    frame = lo;
    if (frame == last)
    {
        uint64_t endCount = 0;
        if (!countAt(last, endCount))
            return refuseCount();
        if (endCount < instruction)
        {
            result.status = TTDCardMoveResult::Status::AtEnd;
            result.message = "card instruction " + std::to_string(instruction) +
                             " is past the history: it ends at card instruction " + std::to_string(endCount);
            return false;
        }
    }
    return true;
}

void TimeTravelController::CardSeekInternal(DebugCpuId cpu, uint64_t instruction, uint64_t hint,
                                            TTDCardMoveResult& result)
{
    result.target = instruction;
    GSCardTarget* target = CardTargetOf(_context, cpu);
    Z80* z80 = _context && _context->pCore ? _context->pCore->GetZ80() : nullptr;
    if (!target || !target->Card() || !z80)
    {
        result.status = TTDCardMoveResult::Status::NoCard;
        result.message = std::string("cpu ") + DebugCpuName(cpu) + " not available: no card with a CPU in its slot";
        return;
    }
    uint64_t frame = 0;
    if (!FindCardStartFrame(cpu, instruction, hint, frame, result))
        return;

    const uint64_t firstFrame = GetEarliestRecordedFrame();
    const uint64_t last = TimelineEnd().frame;
    auto stand = std::make_shared<CardStand>();
    stand->cpu = cpu;
    TTDCardPosition& p = stand->position;
    p.known = true;
    p.cpu = cpu;
    p.cardPosition = true;
    p.caughtUp = true;
    p.instruction = instruction;

    // A frame's checkpoint at the history's ends holds the instruction itself: the card stands there as restored
    // (the main CPU at that checkpoint; before the history's first or after its last instruction start nothing else
    // is known)
    uint64_t at = 0;
    bool before = false;
    if (!CardCountAtFrame(cpu, frame, at, before))
    {
        result.status = TTDCardMoveResult::Status::Failed;
        result.message = "a checkpoint of the history could not be restored";
        return;
    }
    GeneralSoundCard* card = target->Card();
    if (at == instruction && (frame == firstFrame || frame == last))
    {
        PresentPosition(true);
        GSCardTarget::SaveCardState(card, stand->natural);
        stand->captured = stand->natural;
        Z80CpuRegisters regs{};
        card->cardRegisters(regs);
        p.pc = regs.pc;
        p.cardTime = card->cardTime();
        p.mainBoundary = CurrentPosition();
        p.mainPc = z80->pc;
        _cardStand = stand;
        UpdateInputWorkFlag();
        PublishCardReadouts();
        result.status = TTDCardMoveResult::Status::Ok;
        result.position = p;
        return;
    }

    // The replay: from the frame, or a frame earlier when the card reaches the instruction in the replay's first
    // catch-up (the main CPU has no instruction start of this replay yet)
    GSCardTarget::ScanCapture capture;
    for (uint64_t from = frame;;)
    {
        if (!CardCountAtFrame(cpu, from, at, before))
        {
            result.status = TTDCardMoveResult::Status::Failed;
            result.message = "a checkpoint of the history could not be restored";
            return;
        }
        const uint64_t startFrame = _context->emulatorState.frame_counter;
        const uint32_t startT = z80->t;
        {
            const GSCardTarget::ScanScope scan(*target, instruction);
            ReplayModeScope replay(*this);
            while (!target->Capture().captured && !target->ScanStartedPast() &&
                   _context->emulatorState.frame_counter < last)
            {
                const uint64_t frameBefore = _context->emulatorState.frame_counter;
                RunToFrameEnd();
                if (_context->emulatorState.frame_counter == frameBefore)
                    break;
            }
            replay.Exit();
        }
        capture = target->Capture();
        if (!capture.captured)
        {
            result.status = TTDCardMoveResult::Status::Failed;
            result.message = "the replay did not reach card instruction " + std::to_string(instruction);
            return;
        }
        const bool firstCatchUp = capture.catchingUp && !capture.frameEnd && capture.frameCounter == startFrame &&
                                  capture.t == startT;
        if (!firstCatchUp)
            break;
        if (from <= firstFrame)
        {
            result.status = TTDCardMoveResult::Status::AtStart;
            result.message = "card instruction " + std::to_string(instruction) +
                             " runs before the main CPU's first instruction of the history";
            return;
        }
        from--;
    }

    // The main CPU at the instruction start the card's moment lies in. The frame-end catch-up runs after the frame
    // counter moved on; the first catch-up of a frame belongs to the previous frame's last instruction
    uint64_t mainFrame = capture.frameCounter;
    if ((capture.frameEnd || capture.t < capture.instrStartT) && mainFrame > 0)
        mainFrame--;
    const TTDTimePoint boundary{mainFrame, capture.ttdInstrStart};
    TTDSeekResult seek;
    if (!SeekToInternal(boundary, &seek) || !(CurrentPosition() == boundary) || z80->pc != capture.m1pc)
    {
        const TTDTimePoint here = CurrentPosition();
        result.status = TTDCardMoveResult::Status::Failed;
        result.message = "the main CPU's instruction start of card instruction " + std::to_string(instruction) +
                         " (frame " + std::to_string(boundary.frame) + " t " + std::to_string(boundary.tInFrame) +
                         ", PC " + std::to_string(capture.m1pc) + ") was not reached: at frame " +
                         std::to_string(here.frame) + " t " + std::to_string(here.tInFrame) + ", PC " +
                         std::to_string(z80->pc);
        return;
    }
    PresentPosition(false);

    // The card as the seek left it (put back before the machine runs), its time at the main CPU's boundary (run up
    // to it, nothing parks), then the copy
    GSCardTarget::SaveCardState(card, stand->natural);
    int64_t cardAtBoundary = 0;
    {
        const GSCardTarget::ScanScope scan(*target, UINT64_MAX);
        card->catchUpToHost();
        cardAtBoundary = card->cardTime();
    }
    stand->captured = std::move(capture.state);
    GSCardTarget::LoadCardState(card, stand->captured);
    _engineMemoryKnown = false;

    p.pc = capture.pc;
    p.cardTime = capture.cardTime;
    p.mainBoundary = boundary;
    p.mainPc = capture.m1pc;
    const double unitsPerSecond = card->cardUnitsPerSecond();
    if (unitsPerSecond > 0.0 && capture.cardTime > cardAtBoundary)
    {
        const double seconds = static_cast<double>(capture.cardTime - cardAtBoundary) / unitsPerSecond;
        p.tOffset = static_cast<uint32_t>(std::llround(seconds * MainTPerSecond(_context)));
    }
    p.inside = p.tOffset > 0;
    _cardStand = stand;
    UpdateInputWorkFlag();
    PublishCardReadouts();
    result.status = TTDCardMoveResult::Status::Ok;
    result.position = p;
}

TTDCardMoveResult TimeTravelController::SeekToCardInstruction(DebugCpuId cpu, uint64_t instruction)
{
    return SeekCardImpl(cpu, instruction, false);
}

TTDCardMoveResult TimeTravelController::SeekCardImpl(DebugCpuId cpu, uint64_t instruction, bool backToHistory)
{
    const SessionOperation op{*this, SessionOperation::Kind::Change};
    const KnownMemoryScope knownMemory{*this};
    GSCardTarget* target = CardTargetOf(_context, cpu);
    if (!target || !target->Card())
        return Refused(TTDCardMoveResult::Status::NoCard,
                       std::string("cpu ") + DebugCpuName(cpu) + " not available: no card with a CPU in its slot");
    if (_line->timeline.empty())
        return Refused(TTDCardMoveResult::Status::NoHistory, "no recorded history");
    if (_state == TTDSessionState::Recording)
        PauseRecordingForBrowsing();

    TTDCardMoveResult result;
    if (!CardCountNow(cpu, result.from))
        return Refused(TTDCardMoveResult::Status::NoCount,
                       "the card's CPU has no instruction count (a recording made before them is loaded)");

    // A refusal leaves the machine exactly where it stood
    const TTDSessionState stateBefore = _state;
    LiveStateSnapshot before;
    SaveLiveState(before, true);
    GSCardTarget::CardState cardBefore;
    GSCardTarget::SaveCardState(target->Card(), cardBefore);
    const std::shared_ptr<CardStand> standBefore = _cardStand;
    const uint64_t hint = _cardStand ? _cardStand->position.mainBoundary.frame : CurrentPosition().frame;

    CardSeekInternal(cpu, instruction, hint, result);
    if (backToHistory && result.status == TTDCardMoveResult::Status::AtEnd && instruction < result.from)
    {
        // The card stands past the history (stepped on live, unrecorded): back to the history's newest card
        // instruction, the end's checkpoint
        const uint64_t last = TimelineEnd().frame;
        uint64_t endCount = 0;
        bool ignored = false;
        if (CardCountAtFrame(cpu, last, endCount, ignored) && endCount < instruction)
        {
            const uint64_t from = result.from;
            result = TTDCardMoveResult{};
            result.from = from;
            CardSeekInternal(cpu, endCount, last, result);
            result.historyEnd = result.Ok();
        }
    }
    if (!result.Ok())
    {
        _cardStand.reset();
        RestoreLiveState(before);
        GSCardTarget::LoadCardState(target->Card(), cardBefore);
        _cardStand = standBefore;
        if (_state != stateBefore)
            SetState(stateBefore);
        UpdateInputWorkFlag();
        PublishCardReadouts();
        result.position = CardPositionNow(cpu);
    }
    return result;
}

TTDCardMoveResult TimeTravelController::StepCardInstructions(DebugCpuId cpu, int64_t delta)
{
    const SessionOperation op{*this, SessionOperation::Kind::Change};
    GSCardTarget* target = CardTargetOf(_context, cpu);
    if (!target || !target->Card())
        return Refused(TTDCardMoveResult::Status::NoCard,
                       std::string("cpu ") + DebugCpuName(cpu) + " not available: no card with a CPU in its slot");
    if (_line->timeline.empty())
        return Refused(TTDCardMoveResult::Status::NoHistory, "no recorded history");
    if (_state == TTDSessionState::Recording)
        PauseRecordingForBrowsing();
    uint64_t base = 0;
    if (!CardCountNow(cpu, base))
        return Refused(TTDCardMoveResult::Status::NoCount,
                       "the card's CPU has no instruction count (a recording made before them is loaded)");
    if (delta < 0 && static_cast<uint64_t>(-delta) > base)
    {
        TTDCardMoveResult result = Refused(TTDCardMoveResult::Status::AtStart,
                                           "the card has run only " + std::to_string(base) + " instructions");
        result.from = base;
        result.position = CardPositionNow(cpu);
        return result;
    }
    const uint64_t targetCount = delta < 0 ? base - static_cast<uint64_t>(-delta) : base + static_cast<uint64_t>(delta);
    TTDCardMoveResult result = SeekCardImpl(cpu, targetCount, delta < 0);
    result.from = base;
    return result;
}

TTDCardMoveResult TimeTravelController::ReverseStepOverCard(DebugCpuId cpu)
{
    const SessionOperation op{*this, SessionOperation::Kind::Change};
    const KnownMemoryScope knownMemory{*this};
    GSCardTarget* target = CardTargetOf(_context, cpu);
    if (!target || !target->Card())
        return Refused(TTDCardMoveResult::Status::NoCard,
                       std::string("cpu ") + DebugCpuName(cpu) + " not available: no card with a CPU in its slot");
    if (_line->timeline.empty())
        return Refused(TTDCardMoveResult::Status::NoHistory, "no recorded history");
    if (_state == TTDSessionState::Recording)
        PauseRecordingForBrowsing();
    // The stack level the step back over returns to: where the card stands now
    uint64_t base = 0;
    uint16_t level = 0;
    if (!CardCountNow(cpu, base, &level))
        return Refused(TTDCardMoveResult::Status::NoCount,
                       "the card's CPU has no instruction count (a recording made before them is loaded)");
    if (base == 0)
    {
        TTDCardMoveResult result = Refused(TTDCardMoveResult::Status::AtStart, "the card has run no instruction yet");
        result.position = CardPositionNow(cpu);
        return result;
    }

    // One card instruction back (a refusal leaves the machine where it stood); from past the history's end (stepped
    // on live) to its end, no routine to pass over there
    TTDCardMoveResult result = SeekCardImpl(cpu, base - 1, true);
    result.from = base;
    if (!result.Ok() || result.historyEnd)
        return result;
    const uint64_t ret = base - 1;
    GeneralSoundCard* card = target->Card();
    Z80CpuRegisters regs{};
    if (!card || !card->cardRegisters(regs))
        return result;
    // Only a return that lifted the stack to this level closes a routine; any other instruction is the step back
    if (!target->IsReturnAt(card, regs.pc) || static_cast<int16_t>(static_cast<uint16_t>(regs.sp - level)) >= 0)
        return result;
    result.overCall = true;
    const TTDCardMoveResult onReturn = result;

    // Walk back a frame at a time, newest first: the routine's instructions run below this level; the newest card
    // boundary before the return at it or above is the CALL / RST that entered the routine, or the instruction an
    // interrupt came after. The card's moment may lie in the frame after the main CPU's boundary: from there (the
    // last frame of the history is its end, nothing to replay)
    const uint64_t firstFrame = GetEarliestRecordedFrame();
    const uint64_t last = TimelineEnd().frame;
    const uint64_t here = _cardStand ? _cardStand->position.mainBoundary.frame : CurrentPosition().frame;
    uint64_t frame = std::min<uint64_t>(here + 1, last > 0 ? last - 1 : 0);
    bool found = false;
    uint64_t foundCount = 0;
    uint64_t foundFrame = 0;
    for (uint64_t searched = 0; searched < kCardReverseMaxFrames; frame--)
    {
        uint64_t at = 0;
        bool beforeHistory = false;
        if (!CardCountAtFrame(cpu, frame, at, beforeHistory))
            break;   // before the kept history
        if (at < ret)
        {
            const GSCardTarget::ScanScope scan(*target, UINT64_MAX);
            target->ScanStackLevel(level, ret);
            ReplayModeScope replay(*this);
            RunToFrameEnd();
            replay.Exit();
            searched++;
            found = target->ScanStackFound(foundCount);
        }
        if (found)
        {
            foundFrame = frame;
            break;
        }
        if (frame == 0 || frame <= firstFrame)
            break;
    }

    if (found)
    {
        TTDCardMoveResult over;
        over.from = base;
        over.overCall = true;
        CardSeekInternal(cpu, foundCount, foundFrame, over);
        if (over.Ok())
        {
            over.skipped = ret - foundCount;
            return over;
        }
        MLOGWARNING("TimeTravelController::ReverseStepOverCard — the seek to the routine's caller failed: %s",
                    over.message.c_str());
    }
    // Out of reach: the card stays one instruction back, on the return
    TTDCardMoveResult back;
    CardSeekInternal(cpu, ret, onReturn.position.mainBoundary.frame, back);
    result = back.Ok() ? back : onReturn;
    result.from = base;
    result.overCall = true;
    result.callOutOfReach = true;
    if (!back.Ok())
    {
        result.status = TTDCardMoveResult::Status::Failed;
        result.message = "the card's return could not be reached again: " + back.message;
        result.position = CardPositionNow(cpu);
    }
    return result;
}

TTDCardMoveResult TimeTravelController::StepCardForward(DebugCpuId cpu, bool over)
{
    return CardForwardScan(cpu, over ? CardForward::Over : CardForward::Out, 0, kCardReverseMaxFrames);
}

TTDCardMoveResult TimeTravelController::RunCardToCursor(DebugCpuId cpu, uint16_t address, uint64_t maxFrames)
{
    return CardForwardScan(cpu, CardForward::To, address, std::max<uint64_t>(maxFrames, 1));
}

TTDCardMoveResult TimeTravelController::CardForwardScan(DebugCpuId cpu, CardForward kind, uint16_t address,
                                                        uint64_t maxFrames)
{
    const SessionOperation op{*this, SessionOperation::Kind::Change};
    const KnownMemoryScope knownMemory{*this};
    GSCardTarget* target = CardTargetOf(_context, cpu);
    if (!target || !target->Card())
        return Refused(TTDCardMoveResult::Status::NoCard,
                       std::string("cpu ") + DebugCpuName(cpu) + " not available: no card with a CPU in its slot");
    if (_line->timeline.empty())
        return Refused(TTDCardMoveResult::Status::NoHistory, "no recorded history");
    if (_state == TTDSessionState::Recording)
        PauseRecordingForBrowsing();
    TTDCardMoveResult result;
    if (!CardCountNow(cpu, result.from))
        return Refused(TTDCardMoveResult::Status::NoCount,
                       "the card's CPU has no instruction count (a recording made before them is loaded)");
    const uint64_t base = result.from;
    const bool toCursor = kind == CardForward::To;
    char cursor[8];
    std::snprintf(cursor, sizeof(cursor), "#%04X", static_cast<unsigned>(address));

    const TTDSessionState stateBefore = _state;
    LiveStateSnapshot before;
    SaveLiveState(before, true);
    GSCardTarget::CardState cardBefore;
    GSCardTarget::SaveCardState(target->Card(), cardBefore);
    const std::shared_ptr<CardStand> standBefore = _cardStand;
    auto putBack = [&]() {
        _cardStand.reset();
        RestoreLiveState(before);
        GSCardTarget::LoadCardState(target->Card(), cardBefore);
        _cardStand = standBefore;
        if (_state != stateBefore)
            SetState(stateBefore);
        UpdateInputWorkFlag();
        PublishCardReadouts();
        result.position = CardPositionNow(cpu);
    };

    // The frame whose checkpoint the card has not passed `base` at; the history's last frame: the card stands at
    // the history's end, nothing recorded to step through
    const uint64_t last = TimelineEnd().frame;
    const uint64_t hint = _cardStand ? _cardStand->position.mainBoundary.frame : CurrentPosition().frame;
    uint64_t frame = 0;
    const bool inHistory = FindCardStartFrame(cpu, base, hint, frame, result);
    if (inHistory && frame >= last)
    {
        result.status = TTDCardMoveResult::Status::AtEnd;
        result.message = "card instruction " + std::to_string(base) + " is the history's end: the next " +
                         (toCursor ? "run to the cursor" : "step") + " runs on live";
    }
    if (!inHistory || frame >= last)
    {
        putBack();
        return result;
    }

    // The replay forward from there: the step's end (the cursor's next instruction), or the oldest hit of the card's
    // breakpoints after `base`
    const bool breakpoints = target->HasStoppingBreakpoints();
    Z80* z80 = _context && _context->pCore ? _context->pCore->GetZ80() : nullptr;
    uint64_t at = 0;
    bool beforeHistory = false;
    if (!z80 || !CardCountAtFrame(cpu, frame, at, beforeHistory))
    {
        putBack();
        result.status = TTDCardMoveResult::Status::Failed;
        result.message = "a checkpoint of the history could not be restored";
        return result;
    }
    const GSCardTarget::StepKind step = kind == CardForward::Over  ? GSCardTarget::StepKind::Over
                                        : kind == CardForward::Out ? GSCardTarget::StepKind::Out
                                                                   : GSCardTarget::StepKind::To;
    bool started = false;
    bool stepped = false;
    uint64_t stepCount = 0;
    GSCardTarget::ScanHit hit;
    uint64_t stopFrame = frame;
    uint64_t frames = 0;
    {
        const GSCardTarget::ScanScope scan(*target, UINT64_MAX, breakpoints);
        target->ScanStep(step, base, address);
        if (breakpoints)
            target->ScanFirstHitAfter(base);
        ReplayModeScope replay(*this);
        while (_context->emulatorState.frame_counter < last && frames < maxFrames)
        {
            const uint64_t frameBefore = _context->emulatorState.frame_counter;
            RunToFrameEnd();
            frames++;
            stopFrame = frameBefore;
            started = target->ScanStepStarted();
            stepped = target->ScanStepFound(stepCount);
            hit = target->FirstScanHit();
            if (!started || stepped || hit.hit || _context->emulatorState.frame_counter == frameBefore)
                break;
        }
        replay.Exit();
    }
    result.searchedFrames = frames;
    if (!started)
    {
        putBack();
        result.status = TTDCardMoveResult::Status::Failed;
        result.message = "the replay did not reach card instruction " + std::to_string(base);
        return result;
    }

    uint64_t endCount = 0;
    if (hit.hit && (!stepped || hit.count < stepCount))
    {
        endCount = hit.count;
        result.stoppedByBreakpoint = true;
        result.breakpointId = hit.breakpointId;
        result.hitKind = static_cast<uint8_t>(hit.kind);
        result.hitAddress = hit.address;
    }
    else if (stepped)
        endCount = stepCount;
    else if (toCursor)
    {
        // Not reached: the answer, nothing moves (a run to the cursor never stops halfway)
        const bool historyLeft = _context->emulatorState.frame_counter < last && frames >= maxFrames;
        putBack();
        result.status = TTDCardMoveResult::Status::NoHit;
        result.truncated = historyLeft;
        result.message = historyLeft ? "the card does not reach " + std::string(cursor) + " in the " +
                                           std::to_string(frames) + " frames searched (max_frames)"
                                     : "the card does not reach " + std::string(cursor) + " after card instruction " +
                                           std::to_string(base) + " in the history";
        return result;
    }
    else
    {
        // Not ended in the frames replayed: the card goes as far as they reach (the history's end, or the limit)
        stopFrame = std::min<uint64_t>(stopFrame + 1, last);
        bool ignored = false;
        if (!CardCountAtFrame(cpu, stopFrame, endCount, ignored) || endCount <= base)
        {
            putBack();
            result.status = TTDCardMoveResult::Status::AtEnd;
            result.message = "the step does not end before the history's end: the next step runs on live";
            return result;
        }
        result.historyEnd = stopFrame >= last;
        result.truncated = !result.historyEnd;
    }
    CardSeekInternal(cpu, endCount, stopFrame, result);
    if (!result.Ok())
        putBack();
    return result;
}

TTDCardMoveResult TimeTravelController::ReverseContinueCard(DebugCpuId cpu, uint64_t maxFrames)
{
    const SessionOperation op{*this, SessionOperation::Kind::Change};
    const KnownMemoryScope knownMemory{*this};
    GSCardTarget* target = CardTargetOf(_context, cpu);
    if (!target || !target->Card())
        return Refused(TTDCardMoveResult::Status::NoCard,
                       std::string("cpu ") + DebugCpuName(cpu) + " not available: no card with a CPU in its slot");
    if (_line->timeline.empty())
        return Refused(TTDCardMoveResult::Status::NoHistory, "no recorded history");
    if (!target->HasStoppingBreakpoints())
        return Refused(TTDCardMoveResult::Status::NoBreakpoints,
                       std::string("cpu ") + DebugCpuName(cpu) + " has no active breakpoint to run back to");
    if (_state == TTDSessionState::Recording)
        PauseRecordingForBrowsing();
    TTDCardMoveResult result;
    if (!CardCountNow(cpu, result.from))
        return Refused(TTDCardMoveResult::Status::NoCount,
                       "the card's CPU has no instruction count (a recording made before them is loaded)");
    const uint64_t base = result.from;

    const TTDSessionState stateBefore = _state;
    LiveStateSnapshot before;
    SaveLiveState(before, true);
    GSCardTarget::CardState cardBefore;
    GSCardTarget::SaveCardState(target->Card(), cardBefore);
    const std::shared_ptr<CardStand> standBefore = _cardStand;
    auto putBack = [&]() {
        _cardStand.reset();
        RestoreLiveState(before);
        GSCardTarget::LoadCardState(target->Card(), cardBefore);
        _cardStand = standBefore;
        if (_state != stateBefore)
            SetState(stateBefore);
        UpdateInputWorkFlag();
        PublishCardReadouts();
    };

    const uint64_t firstFrame = GetEarliestRecordedFrame();
    const uint64_t last = TimelineEnd().frame;
    const uint64_t here = _cardStand ? _cardStand->position.mainBoundary.frame : CurrentPosition().frame;
    // The card's moment may lie in the frame after the main CPU's boundary: from there, newest frame first (the
    // last frame of the history is its end, nothing to replay)
    uint64_t frame = std::min<uint64_t>(here + 1, last > 0 ? last - 1 : 0);
    GSCardTarget::ScanHit hit;
    uint64_t hitFrame = 0;
    for (;; frame--)
    {
        if (result.searchedFrames >= maxFrames)
        {
            result.truncated = frame >= firstFrame;
            break;
        }
        uint64_t at = 0;
        bool beforeHistory = false;
        if (!CardCountAtFrame(cpu, frame, at, beforeHistory))
            break;   // before the kept history
        if (at < base)
        {
            const GSCardTarget::ScanScope scan(*target, UINT64_MAX, true, base);
            ReplayModeScope replay(*this);
            RunToFrameEnd();
            replay.Exit();
            result.searchedFrames++;
            hit = target->LastScanHit();
        }
        if (hit.hit)
        {
            hitFrame = frame;
            break;
        }
        if (frame == 0 || frame <= firstFrame)
            break;
    }
    if (!hit.hit)
    {
        putBack();
        result.status = TTDCardMoveResult::Status::NoHit;
        result.message = result.truncated
                             ? "no hit of cpu " + std::string(DebugCpuName(cpu)) + "'s breakpoints in the last " +
                                   std::to_string(result.searchedFrames) + " frames searched (max_frames)"
                             : "no hit of cpu " + std::string(DebugCpuName(cpu)) +
                                   "'s breakpoints before card instruction " + std::to_string(base) +
                                   " in the history";
        result.position = CardPositionNow(cpu);
        return result;
    }
    result.breakpointId = hit.breakpointId;
    result.hitKind = static_cast<uint8_t>(hit.kind);
    result.hitAddress = hit.address;
    CardSeekInternal(cpu, hit.count, hitFrame, result);
    if (!result.Ok())
    {
        putBack();
        result.position = CardPositionNow(cpu);
    }
    return result;
}

}  // namespace ttd
