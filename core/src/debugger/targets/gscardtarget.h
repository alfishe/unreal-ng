#pragma once

/// @file gscardtarget.h
/// @brief The CPU of the card in the General Sound slot as a debug target (gs-debugger design §3.1, §4): its own
/// breakpoints, and the card hooks that report their hits at the card's instruction boundaries.
///
/// The target holds only the context: every call finds the card the slot has now (SoundManager::getGeneralSound),
/// so a personality switch never leaves a stale pointer. The breakpoints stay while the card has no CPU (the
/// lightweight player) and fire again after a switch back. The hooks are on the card only while an active
/// breakpoint asks for them; otherwise the card runs its fast loop.

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "3rdparty/unreal-z80/z80cpu.h"
#include "debugger/callbacks/scripthookhost.h"
#include "debugger/targets/debugtarget.h"
#include "debugger/ttd/timetravelhooks.h"
#include "emulator/notifications.h"
#include "emulator/sound/chips/gs/gsdebughooks.h"

class BreakpointManager;
class GeneralSoundCard;
class EmulatorContext;

class GSCardTarget final : public IDebugTarget, public IGSDebugHooks
{
public:
    explicit GSCardTarget(EmulatorContext* context);
    /// `cardSource` finds the card instead of the context's sound manager (tests: a card of their own)
    GSCardTarget(EmulatorContext* context, std::function<GeneralSoundCard*()> cardSource);
    /// Another card's CPU (`ms`: the MultiSound's General Sound), found by `cardSource`
    GSCardTarget(EmulatorContext* context, DebugCpuId id, std::function<GeneralSoundCard*()> cardSource);
    ~GSCardTarget() override;

    GSCardTarget(const GSCardTarget&) = delete;
    GSCardTarget& operator=(const GSCardTarget&) = delete;

    /// The card CPU's breakpoints (memory, ports, card events)
    BreakpointManager* GetBreakpointManager() { return _breakpoints.get(); }

    /// The slot's card was created or replaced (SoundManager): the hooks go to the new card when they are wanted
    void OnCardCreated();
    /// The card the slot has now, when it has a CPU
    GeneralSoundCard* Card() const;

    /// The last stop on the card CPU (hit = false: none yet)
    struct Stop
    {
        bool hit = false;
        uint16_t breakpointId = 0xFFFF;
        uint16_t address = 0;     ///< the PC (execute), the memory address, the port, or the event kind
        uint8_t value = 0;        ///< the byte read / written, the event's byte
        BreakpointHitKind kind = BreakpointHitKind::Execute;
        uint16_t pc = 0;          ///< the instruction the access belonged to (execute: the one about to run)
        uint16_t boundaryPc = 0;  ///< where the card stood when it parked (the next instruction)
        int64_t cardTime = 0;     ///< in the card's unit (GeneralSoundCard::cardTime)
        uint8_t master = 0;       ///< DmaWrite: the bus master (NeoGS DMA module: 0 ZX-DMA, 1 SD)
        uint8_t page = 0;         ///< DmaWrite: the RAM page written (`address` the offset in it)
        bool step = false;        ///< no breakpoint: a step ended at `boundaryPc`
    };
    Stop LastStop() const { return _lastStop; }
    /// Stops since creation (tests, diagnosis)
    uint64_t StopCount() const { return _stopCount; }
    /// The card runs its debug loop now
    bool HooksAttached() const { return _attached != nullptr; }

    /// region <Steps (gs-debugger design §5.3): a one-shot park at a card instruction boundary>
    enum class StepKind : uint8_t
    {
        None,
        In,    ///< after `count` instructions
        Over,  ///< CALL, RST, DJNZ, a block instruction: at the next instruction with SP at or above the entry SP;
               ///< another instruction: as In
        Out    ///< after a return (RET, RET cc, RETI, RETN) run with SP at or above the entry SP
    };
    /// Arm a step of the card CPU (the machine paused; the caller resumes it then): the next card run parks the
    /// machine there with the reason step (Emulator::OnTargetStep). A breakpoint on the way ends it first. "" or why
    /// not (no card CPU, a step already armed). The card runs only as the main CPU's run takes it along (at a host
    /// port access or the frame end), so the main CPU moves on to that point
    std::string ArmStep(StepKind kind, uint32_t count = 1);
    /// Disarm a step that did not end (a timeout, a pause of another cause)
    void CancelStep();
    bool StepArmed() const { return _stepKind.load(std::memory_order_acquire) != StepKind::None; }
    /// Debugged with a stop (a stopping breakpoint, an armed step): the machine runs in tight mode for it
    bool WantsTight() const { return _stopWanted.load(std::memory_order_acquire) || StepArmed(); }
    /// The machine is parked inside the card's run (a card breakpoint or step): the next instruction is the
    /// boundary's
    bool ParkedInside() const { return _parkedInside.load(std::memory_order_acquire); }
    /// endregion </Steps>

    // IDebugTarget
    DebugCpuId Id() const override { return _id; }
    const char* Name() const override { return DebugCpuName(_id); }
    bool Available() const override { return Card() != nullptr; }
    MemoryPageDescriptor MapAddress(uint16_t address) const override;
    uint32_t PageCount(MemoryBankModeEnum kind) const override;
    bool Registers(Z80Registers& out) const override;
    bool Peek(uint16_t address, uint8_t& out) const override;
    void OnBreakpointDemand(const BreakpointTapDemand& demand) override;

    /// A card hook's writes (gs-debugger design §8A.7, Q3), made at the card's next instruction boundary: `view`
    /// ones only while no time travel session is open, `logic` ones while none or a recording one is, which keeps
    /// them as an input of the track
    void QueueHookEdits(uint32_t callbackId, bool view, HookEdits edits);
    /// The byte a card read sees, from a `logic` hook while a session records: an input of the track
    void RecordReadEdit(uint32_t callbackId, bool port, uint16_t address, uint8_t value);
    /// Sets the card CPU register @p name (the debugger's names, any case) in @p regs; false for an unknown one or a
    /// value out of its range
    static bool SetRegister(Z80CpuRegisters& regs, std::string name, uint16_t value);

    /// A replay's edits of the card's current frame (TimeTravelController::DeliverCardEdits): cleared, added, then
    /// committed, which takes the debug loop while any wait
    void ClearReplayEdits();
    void AddReplayEdit(ttd::TTDCardEdit edit);
    void CommitReplayEdits();
    /// Edits waiting (tests)
    size_t PendingEdits() const { return _live.size() + _replay.size(); }

    // IGSDebugHooks (the emulation thread, inside the card's run)
    void beforeStep(uint16_t pc) override;
    void endRun(uint16_t pc) override;
    uint8_t onMemoryRead(uint16_t pc, uint16_t addr, uint8_t value) override;
    void onMemoryWrite(uint16_t pc, uint16_t addr, uint8_t value) override;
    uint8_t onPortIn(uint16_t pc, uint16_t port, uint8_t value) override;
    void onPortOut(uint16_t pc, uint16_t port, uint8_t value) override;
    void onEvent(GSCardEvent kind, uint16_t port, uint8_t value) override;
    void onBusMasterWrite(uint8_t master, uint8_t ramPage, uint16_t offset, uint8_t value) override;

private:
    /// The hooks on the current card when wanted, off it otherwise
    void Attach();
    void UpdateTight();  ///< DebugManager::UpdateCardTight: tight mode follows WantsTight
    /// A hit of the instruction just done, reported at the next boundary (the first one wins)
    void Note(uint16_t id, uint16_t address, uint8_t value, BreakpointHitKind kind, uint16_t pc);
    /// Stop the machine with the card at `boundaryPc`
    void Park(Stop stop, uint16_t boundaryPc);
    /// An armed step at this boundary: true when it parked
    bool StepBoundary(uint16_t pc);
    /// The instruction at `pc` of the card CPU is a return (RET, RET cc, RETI, RETN and their aliases)
    bool IsReturnAt(GeneralSoundCard* card, uint16_t pc) const;
    /// The waiting edits due at this boundary
    void ApplyEdits(GeneralSoundCard* card);
    /// The ZX frame the card is in (its frame-end catch-up runs after the frame counter moved on)
    uint64_t CardFrame(const GeneralSoundCard* card) const;
    /// A replayed read substitution for this access, if one waits
    bool ReplayRead(ttd::TTDCardEdit::Kind kind, uint16_t address, uint8_t& value);

    EmulatorContext* _context = nullptr;
    DebugCpuId _id = DebugCpuId::GS;
    std::function<GeneralSoundCard*()> _cardSource;
    std::unique_ptr<BreakpointManager> _breakpoints;
    bool _wanted = false;                    // an active breakpoint asks for the hooks
    std::atomic<bool> _stopWanted{false};    // an active breakpoint stops (tight mode)
    struct LiveEdit
    {
        uint32_t callbackId = 0;
        bool view = false;
        HookEdits edits;
    };
    std::vector<LiveEdit> _live;              // hook writes for the next boundary
    std::vector<ttd::TTDCardEdit> _replay;    // the replay's edits of this frame, in order
    GeneralSoundCard* _attached = nullptr;   // the card the hooks are on (compared, never called once replaced)
    Stop _pending;                           // a bus or event hit waiting for the boundary
    Stop _lastStop;
    uint64_t _stopCount = 0;
    // An armed step: set by the caller with the machine paused, read by the emulation thread after the resume
    std::atomic<StepKind> _stepKind{StepKind::None};
    uint32_t _stepSkip = 0;                  // boundaries passed before the step may park (In: count - 1, ...)
    uint16_t _stepTarget = 0;                // Over: the next instruction's address
    uint16_t _stepSp = 0;                    // Over / Out: SP when armed
    bool _stepReturning = false;             // Out: the instruction now running is a return
    uint16_t _stepReturnSp = 0;              // Out: SP before that return
    std::atomic<bool> _parkedInside{false};  // inside Park's call: the machine waits in the card's run
};

/// The host side of the mailbox (the main CPU's access to #B3 / #BB, inside its instruction): a host-side event
/// breakpoint of the main CPU stops the machine there, as a port breakpoint does. Free without one
void GSReportHostEvent(EmulatorContext* context, GSCardEvent kind, uint8_t value, int arg = -1);
