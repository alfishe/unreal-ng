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
class LabelManager;
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
    /// The card CPU's labels (gs-debugger design §6.2): its own store, page-tied by the card's map; the shipped
    /// firmware symbols (data/symbols/gs/manifest.json) join it when the card's ROM page 0 matches a bundle
    LabelManager* GetLabelManager() { return _labels.get(); }
    /// The bundles of the card's ROM into the labels (at each card creation); the bundle sets now there
    std::vector<std::string> ApplySymbolBundles();

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
        Out,   ///< after a return (RET, RET cc, RETI, RETN) run with SP at or above the entry SP
        To     ///< at the next instruction at `target` (run to the cursor); the instruction at PC runs first
    };
    /// Arm a step of the card CPU (the machine paused; the caller resumes it then): the next card run parks the
    /// machine there with the reason step (Emulator::OnTargetStep). A breakpoint on the way ends it first. "" or why
    /// not (no card CPU, a step already armed). The card runs only as the main CPU's run takes it along (at a host
    /// port access or the frame end), so the main CPU moves on to that point. `target`: To's address
    std::string ArmStep(StepKind kind, uint32_t count = 1, uint16_t target = 0);
    /// Disarm a step that did not end (a timeout, a pause of another cause)
    void CancelStep();
    bool StepArmed() const { return _stepKind.load(std::memory_order_acquire) != StepKind::None; }
    /// Debugged with a stop (a stopping breakpoint, an armed step): the machine runs in tight mode for it
    bool WantsTight() const
    {
        return _stopWanted.load(std::memory_order_acquire) || StepArmed() || _scanning.load(std::memory_order_acquire);
    }
    /// The machine is parked inside the card's run (a card breakpoint or step): the next instruction is the
    /// boundary's
    bool ParkedInside() const { return _parkedInside.load(std::memory_order_acquire); }
    /// endregion </Steps>

    /// region <TTD card scans (gs-debugger design §8): a replay that finds a card instruction>
    /// A card's whole state: its TTD blob and the memory it gives the engine as regions (NeoGS RAM and flash are no
    /// part of its blob). A card position keeps one, and so does the scan's capture
    struct CardState
    {
        std::vector<uint8_t> blob;
        std::vector<std::vector<uint8_t>> regions;
        bool valid = false;
    };
    static bool SaveCardState(GeneralSoundCard* card, CardState& out);
    /// Puts @p state back into @p card (the regions' trackers marked written, then the blob); false when it does not fit
    static bool LoadCardState(GeneralSoundCard* card, const CardState& state);

    /// What a scan saw at the card instruction it waited for (the boundary before it: count instructions done)
    struct ScanCapture
    {
        bool captured = false;
        uint64_t count = 0;
        uint16_t pc = 0;
        int64_t cardTime = 0;
        CardState state;
        // The main CPU at that moment, inside the card's run
        uint64_t frameCounter = 0;   ///< emulatorState.frame_counter (the frame-end catch-up: the next frame's)
        uint32_t t = 0;              ///< z80.t
        uint32_t instrStartT = 0;    ///< the start of the instruction the card's moment is in
        uint32_t ttdInstrStart = 0;  ///< instrStartT in TTD units (TtdTInFrame)
        uint16_t m1pc = 0;           ///< that instruction's PC
        bool catchingUp = false;     ///< tight mode's catch-up at a main instruction start (DebugManager::CatchingUp)
        bool frameEnd = false;       ///< the card's frame-end catch-up (GeneralSoundCard::inFrameEndCatchUp)
    };
    /// A breakpoint hit a scan saw (BeginScan with breakpoints): where the card would park for it
    struct ScanHit
    {
        bool hit = false;
        uint64_t count = 0;   ///< the boundary the card parks at: count instructions done
        uint16_t breakpointId = 0xFFFF;
        uint16_t address = 0;
        uint8_t value = 0;
        BreakpointHitKind kind = BreakpointHitKind::Execute;
        uint16_t pc = 0;
    };
    /// Starts a scan of a TTD replay (the machine held): the card runs its debug loop in tight mode, nothing parks
    /// and no step ends. `captureAt`: the count whose boundary is copied (UINT64_MAX: none). `breakpoints`: this
    /// target's breakpoints are evaluated as a live run would (BreakpointManager::BeginReplayEvaluate) and the newest
    /// hit before `hitsBefore` is kept
    void BeginScan(uint64_t captureAt, bool breakpoints = false, uint64_t hitsBefore = UINT64_MAX);
    /// Ends it: the breakpoints' counts back as they were, hooks and tight mode as wanted again
    void EndScan();
    bool Scanning() const { return _scanning.load(std::memory_order_acquire); }
    const ScanCapture& Capture() const { return _capture; }
    /// The newest hit before `hitsBefore` and the number of hits the scan saw
    ScanHit LastScanHit() const { return _scanLastHit; }
    uint64_t ScanHitCount() const { return _scanHits; }
    /// The first boundary the scan saw was past `captureAt` (the count lies before the replay's start)
    bool ScanStartedPast() const { return _scanStartedPast; }
    /// After BeginScan: the scan also keeps the newest boundary before `before` whose SP is at stack level `level`
    /// or above it (the difference taken modulo 64K): a card reverse step over looks for the instruction that
    /// entered a routine (TimeTravelController::ReverseStepOverCard)
    void ScanStackLevel(uint16_t level, uint64_t before);
    /// After BeginScan: @p trace is called at every boundary of the scan with the card and its count (the oracle of
    /// tests and tools that need each card instruction; the card's registers and memory read from it are the
    /// boundary's). Cleared at the scan's start and end
    using ScanTraceFn = std::function<void(GeneralSoundCard* card, uint64_t count, uint16_t pc)>;
    void ScanTrace(ScanTraceFn trace) { _scanTrace = std::move(trace); }
    /// After BeginScan: the scan also finds where a card step of @p kind (Over or Out) taken from the boundary
    /// @p after ends. The card's registers and code at that boundary arm it as ArmStep arms a step from a standing
    /// card; each later boundary is decided as StepBoundary decides it live: Over ends at the first one at the
    /// address after the instruction passed (StepOverTarget; anything else is a step in) with SP at or above the
    /// start's; Out at the first one after a return ran with SP at or above the start's. A card step through the
    /// recorded past (TimeTravelController::StepCardForward)
    void ScanStep(StepKind kind, uint64_t after);
    /// The scan has seen the step's start boundary
    bool ScanStepStarted() const { return _scanStepStarted; }
    /// The boundary the step ends at (false: not seen yet)
    bool ScanStepFound(uint64_t& count) const
    {
        count = _scanStepCount;
        return _scanStepFound;
    }
    /// After BeginScan with breakpoints: the oldest hit after the boundary @p after is kept too (a forward step ends
    /// at it)
    void ScanFirstHitAfter(uint64_t after);
    ScanHit FirstScanHit() const { return _scanFirstHit; }
    /// That boundary's count (false: none seen)
    bool ScanStackFound(uint64_t& count) const
    {
        count = _scanStackCount;
        return _scanStackFound;
    }
    /// An active breakpoint that stops (what a reverse continue looks for)
    bool HasStoppingBreakpoints() const { return _stopWanted.load(std::memory_order_acquire); }
    /// RAII: a scan for one replay
    class ScanScope
    {
    public:
        ScanScope(GSCardTarget& target, uint64_t captureAt, bool breakpoints = false, uint64_t hitsBefore = UINT64_MAX)
            : _target(target)
        {
            _target.BeginScan(captureAt, breakpoints, hitsBefore);
        }
        ~ScanScope() { _target.EndScan(); }
        ScanScope(const ScanScope&) = delete;
        ScanScope& operator=(const ScanScope&) = delete;

    private:
        GSCardTarget& _target;
    };
    /// endregion </TTD card scans>

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

    /// The instruction at `pc` of the card CPU is a return (RET, RET cc, RETI, RETN and their aliases)
    bool IsReturnAt(GeneralSoundCard* card, uint16_t pc) const;
    /// The instruction at `pc` is one a step over passes as a whole (CALL, RST, DJNZ, a block instruction): true and
    /// `target` the address after it; false: a step over is a step in there
    bool StepOverTarget(GeneralSoundCard* card, uint16_t pc, uint16_t& target) const;

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
    /// The waiting edits due at this boundary
    void ApplyEdits(GeneralSoundCard* card);
    /// The ZX frame the card is in (its frame-end catch-up runs after the frame counter moved on)
    uint64_t CardFrame(const GeneralSoundCard* card) const;
    /// A replayed read substitution for this access, if one waits
    bool ReplayRead(ttd::TTDCardEdit::Kind kind, uint16_t address, uint8_t& value);
    /// A scan's boundary (beforeStep while scanning): its breakpoint hits and the capture
    void ScanBoundary(GeneralSoundCard* card, uint16_t pc);
    /// A scan's step (ScanStep) at the boundary with `count` instructions done
    void ScanStepBoundary(GeneralSoundCard* card, uint16_t pc, uint64_t count);
    /// A scan's hit at the boundary with `count` instructions done
    void ScanNoteHit(const Stop& stop, uint64_t count);

    EmulatorContext* _context = nullptr;
    DebugCpuId _id = DebugCpuId::GS;
    std::function<GeneralSoundCard*()> _cardSource;
    std::unique_ptr<BreakpointManager> _breakpoints;
    std::unique_ptr<LabelManager> _labels;
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
    uint16_t _stepTarget = 0;                // Over: the next instruction's address; To: the cursor
    uint16_t _stepSp = 0;                    // Over / Out: SP when armed
    bool _stepReturning = false;             // Out: the instruction now running is a return
    uint16_t _stepReturnSp = 0;              // Out: SP before that return
    std::atomic<bool> _parkedInside{false};  // inside Park's call: the machine waits in the card's run
    // A TTD card scan (BeginScan): set and read with the machine held, on its thread during the replay
    std::atomic<bool> _scanning{false};
    bool _scanBreakpoints = false;
    uint64_t _scanCaptureAt = UINT64_MAX;
    uint64_t _scanHitsBefore = UINT64_MAX;
    bool _scanStartedPast = false;
    bool _scanSawBoundary = false;
    ScanCapture _capture;
    ScanHit _scanLastHit;
    uint64_t _scanHits = 0;
    ScanTraceFn _scanTrace;                  // ScanTrace: called at every boundary of the scan
    StepKind _scanStepKind = StepKind::None; // ScanStep: the step looked for, and its state
    uint64_t _scanStepAfter = 0;
    bool _scanStepStarted = false;
    uint64_t _scanStepLast = 0;
    uint16_t _scanStepTarget = 0;
    uint16_t _scanStepSp = 0;
    bool _scanStepReturning = false;
    uint16_t _scanStepReturnSp = 0;
    bool _scanStepFound = false;
    uint64_t _scanStepCount = 0;
    uint64_t _scanFirstAfter = UINT64_MAX;   // ScanFirstHitAfter: the oldest hit after it
    ScanHit _scanFirstHit;
    bool _scanStack = false;                 // ScanStackLevel: the newest boundary at the stack level is kept
    uint16_t _scanStackLevel = 0;
    uint64_t _scanStackBefore = 0;
    bool _scanStackFound = false;
    uint64_t _scanStackCount = 0;
};

/// The host side of the mailbox (the main CPU's access to #B3 / #BB, inside its instruction): a host-side event
/// breakpoint of the main CPU stops the machine there, as a port breakpoint does. Free without one
void GSReportHostEvent(EmulatorContext* context, GSCardEvent kind, uint8_t value, int arg = -1);
