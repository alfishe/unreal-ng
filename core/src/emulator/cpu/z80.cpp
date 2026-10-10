#include "z80.h"

#include "3rdparty/message-center/messagecenter.h"
#include "common/modulelogger.h"
#include "common/stringhelper.h"
#include "common/timehelper.h"
#include "debugger/pchistory/pchistory.h"
#include "debugger/analyzers/analyzermanager.h"
#include "debugger/breakpoints/breakpointmanager.h"
#include "debugger/debugmanager.h"
#include "debugger/ttd/timetravelhooks.h"
#include "debugger/ttd/timetravelmanager.h"
#include "debugger/ttd/ttdportjournal.h"
#include "emulator/cpu/op_noprefix.h"
#include "emulator/cpu/opcode_profiler.h"
#include "emulator/emulator.h"
#include "emulator/io/fdc/diskautostart.h"
#include "emulator/io/fdc/diskfastload.h"
#include "emulator/io/fdc/wd1793.h"
#include "emulator/io/tape/tape.h"
#include "emulator/io/tape/tapefastload.h"
#include "emulator/memory/memoryaccesstracker.h"
#include "emulator/notifications.h"
#include "emulator/platform.h"
#include "emulator/ports/portdecoder.h"
#include "emulator/rzx/rzxplayer.h"
#include "emulator/spectrumconstants.h"
#include "emulator/video/screen.h"
#include "emulator/memory/hostbusoverlay.h"
#include "emulator/video/ulacontention.h"
#include "stdafx.h"

/// region <Constructors / Destructors>

Z80::Z80(EmulatorContext* context) : Z80State{}
{
    _context = context;
    _logger = context->pModuleLogger;
    _memory = context->pMemory;

    // Initialize memory access interfaces
    FastMemIf = Memory::GetFastMemoryInterface();
    DbgMemIf = Memory::GetDebugMemoryInterface();
    FastContendedMemIf = Memory::GetFastContendedMemoryInterface();
    DbgContendedMemIf = Memory::GetDebugContendedMemoryInterface();
    OverlayFastMemIf = Memory::GetOverlayMemoryInterface(false, false);
    OverlayDbgMemIf = Memory::GetOverlayMemoryInterface(true, false);
    OverlayFastContendedMemIf = Memory::GetOverlayMemoryInterface(false, true);
    OverlayDbgContendedMemIf = Memory::GetOverlayMemoryInterface(true, true);
    MemIf = FastMemIf;  // Use fast memory access interface by default (Core::SelectMemoryInterface decides)

    // Ensure register memory and unions do not contain garbage
    Z80State::tt = 0;
    t = 0;      // Initialize t-state counter
    eipos = 0;  // Unused layout slot (see z80.h)
    boundary = Z80_BOUNDARY_NONE;
    pc = 0;
    sp = 0;
    ir_ = 0;
    int_flags = 0;
    af = 0;
    bc = 0;
    de = 0;
    hl = 0;
    ix = 0;
    iy = 0;
    alt.af = 0;
    alt.bc = 0;
    alt.de = 0;
    alt.hl = 0;
    memptr = 0;
    q = 0;  // Initialize undocumented Q register

    tpi = 0;
    rate = (1 << 8);
    isDebugMode = false;
    trace_curs = trace_top = (unsigned)-1;
    trace_mode = 0;
    mem_curs = mem_top = 0;
    pc_trflags = nextpc = 0;
    int_pending = false;
    int_gate = true;
    nmi_in_progress = false;

    // Supply direct references to registers for DDCB prefix operation results
    // Indexes:
    // [0] - b
    // [1] - c
    // [2] - d
    // [3] - e
    // [4] - h
    // [5] - l
    // [6] - <unused>
    // [7] - a
    direct_registers[0] = &b;
    direct_registers[1] = &c;
    direct_registers[2] = &d;
    direct_registers[3] = &e;
    direct_registers[4] = &h;
    direct_registers[5] = &l;
    direct_registers[6] =
        &_trashRegister;  // Redirect DDCB operation writes with no destination registers to unused register variable
    direct_registers[7] = &a;

    // Create opcode profiler
    _opcodeProfiler = new OpcodeProfiler(context);
}

Z80::~Z80()
{
    if (FastMemIf)
    {
        delete FastMemIf;
        FastMemIf = nullptr;
    }

    if (DbgMemIf)
    {
        delete DbgMemIf;
        DbgMemIf = nullptr;
    }

    delete FastContendedMemIf;
    FastContendedMemIf = nullptr;
    delete DbgContendedMemIf;
    DbgContendedMemIf = nullptr;
    delete OverlayFastMemIf;
    OverlayFastMemIf = nullptr;
    delete OverlayDbgMemIf;
    OverlayDbgMemIf = nullptr;
    delete OverlayFastContendedMemIf;
    OverlayFastContendedMemIf = nullptr;
    delete OverlayDbgContendedMemIf;
    OverlayDbgContendedMemIf = nullptr;

    if (_opcodeProfiler)
    {
        delete _opcodeProfiler;
        _opcodeProfiler = nullptr;
    }

    _context = nullptr;

    MLOGDEBUG("Z80::~Z80()");
}

/// endregion </Constructors / Destructors>

/// region <Methods>

/// Handle Z80 reset signal
void Z80::Reset()
{
    // Emulation state
    last_branch = 0x0000;  // Address of last branch (in Z80 address space)
    int_pending = false;   // No interrupts pending
    int_acked_in_pulse = 0;
    int_gate = true;       // Allow external interrupts
    nmi_in_progress = false;  // Clear NMI flag
    _nmi_pending_count = 0;   // No NMI requested

    tt = 0;  // Scaled to CPU frequency multiplier cycle count
    t = 0;   // Reset cycle counter for deterministic state

    // Z80 chip reset sequence. See: http://www.z80.info/interrup.htm (Reset Timing section)
    int_flags = 0;  // Set interrupt mode 0 (also clears iff1, iff2, halted via union)
    ir_ = 0;        // Reset IR (Instruction Register)
    pc = 0x0000;    // Reset PC (Program Counter)
    im = 0;         // IM0 mode is set by default
    sp = 0xFFFF;    // Stack pointer set to the end of memory address space
    af = 0xFFFF;    // Real chip behavior
    q = 0;          // Q register (undocumented) reset

    // Clear general-purpose registers for deterministic reset state
    // Real Z80 hardware leaves these undefined after reset, but for consistent
    // emulation they must be cleared to prevent stale values from previous session
    bc = 0;
    de = 0;
    hl = 0;
    ix = 0;
    iy = 0;

    // Clear alternate register set
    alt.af = 0;
    alt.bc = 0;
    alt.de = 0;
    alt.hl = 0;

    // Clear undocumented internal registers
    memptr = 0;     // MEMPTR (WZ) internal address buffer
    eipos = 0;      // Unused layout slot (see z80.h)
    boundary = Z80_BOUNDARY_NONE;  // No INT shadow / pending prefix after reset
    haltpos = 0;    // HALT position

    // All that takes 3 clock cycles
    IncrementCPUCyclesCounter(3);
}

/// Instruction-start work that belongs to the first byte of an instruction:
/// TR-DOS ROM session paging, debugger breakpoints, analyzer step events and
/// the ROM traps (fast tape/disk loading, disk autostart). Returns true when
/// a trap consumed the instruction (it must not execute).
// Forced inline: this runs before every instruction, and as an out-of-line
// call it cost ~1 ns per Z80Step (+11% on BM_Z80_DecodeOverhead_NOP).
__forceinline bool Z80::RunInstructionStartHooks(bool skipBreakpoints)
{
    [[maybe_unused]] Z80& cpu = *this;
    [[maybe_unused]] const CONFIG& config = _context->config;
    [[maybe_unused]] EmulatorState& state = _context->emulatorState;
    [[maybe_unused]] TEMP& temporary = _context->temporary;
    [[maybe_unused]] Memory& memory = *_context->pMemory;
    [[maybe_unused]] Emulator& emulator = *_context->pEmulator;

    /// region  <Ports logic>

    // ROM paging MUST happen BEFORE breakpoint dispatch so that page-specific breakpoints
    // (e.g., TR-DOS ROM at $1EDD) can match the correct memory page.
    // Previously this was after breakpoint dispatch, causing page-specific breakpoints to fail.

    // TR-DOS ROM session tracking (port of the original UnrealSpeccy step() logic).
    // Session flags are (re)armed by Memory::UpdateZ80Banks() on every paging change:
    // - CF_SETDOSROM: armed while the 48K ROM slot is selected (p7FFD bit 4) with
    //   Beta128 present. First opcode fetch in $3Dxx activates the TR-DOS session:
    //   bank0 switches to the DOS ROM (bit 4 set) or service ROM (bit 4 clear).
    // - CF_LEAVEDOSADR (Pentagon/Profi): active while in a TR-DOS session; closes it
    //   once PC leaves the ROM area (pc >= $4000), restoring the regular
    //   128K/48K ROM selected by p7FFD bit 4.
    // - CF_LEAVEDOSRAM (other models): closes the session once code executes from a
    //   RAM-mapped bank instead.
    if (state.flags & CF_SETDOSROM)
    {
        if (cpu.pch == 0x3D)  // Execution enters $3D00-$3DFF => activate TR-DOS ROM
        {
            if (_context->pPortDecoder)
                _context->pPortDecoder->OnDosRomFetch(cpu.pc);
            state.flags |= CF_TRDOS;

            // Apply ROM page changes
            memory.UpdateZ80Banks();
        }
    }
    else if (state.flags & CF_LEAVEDOSADR)
    {
        if (cpu.pch & 0xC0)  // PC > $3FFF closes TR-DOS
        {
            state.flags &= ~CF_TRDOS;

            // Apply ROM page changes
            memory.UpdateZ80Banks();
        }
    }
    else if (state.flags & CF_LEAVEDOSRAM)
    {
        // Execution code from RAM address - disables TR-DOS ROM. The model
        // decides what "RAM" means: by default the bank's current mapping; the
        // ZX-Evo looks at the programmed window type, so its NMI page (RAM over
        // a ROM window) keeps the DOS signal on
        uint8_t bank = (cpu.pc >> 14) & 3;
        if ((cpu.pch & 0x3F) == 0x3D)  // #3Dxx of any window: the FPGA's DOS entry strobe looks at A13:A8 only
            _context->pPortDecoder->OnDosRomFetch(cpu.pc);
        if (_context->pPortDecoder->IsDosLeavingBank(bank))
        {
            state.flags &= ~CF_TRDOS;

            // Apply ROM page changes
            memory.UpdateZ80Banks();
        }
    }

    /// endregion  </Ports logic>

    // Let debugger process step event
    if (cpu.isDebugMode && skipBreakpoints == false && _context->pDebugManager != nullptr)
    {
        BreakpointManager& brk = *_context->pDebugManager->GetBreakpointsManager();
        uint16_t breakpointID = brk.HandlePCChange(pc);
        if (breakpointID != BRK_INVALID)
        {
            AnalyzerManager* analyzerMgr = _context->pDebugManager->GetAnalyzerManager();

            // Get current memory page information for page-specific breakpoint matching
            Memory& mem = *_context->pMemory;
            MemoryPageDescriptor pageInfo = mem.MapZ80AddressToPhysicalPage(pc);

            // Check if this is an analyzer-owned breakpoint (should not pause)
            // Must check both address-only AND page-specific ownership
            bool isAnalyzerBreakpoint = false;
            if (analyzerMgr)
            {
                // First check page-specific match (for breakpoints like TR-DOS ROM)
                isAnalyzerBreakpoint = analyzerMgr->ownsBreakpointAtAddress(pc, pageInfo.page, pageInfo.mode);

                // Fall back to address-only match (for non-page-specific breakpoints)
                if (!isAnalyzerBreakpoint)
                {
                    isAnalyzerBreakpoint = analyzerMgr->ownsBreakpointAtAddress(pc);
                }
            }

            // Always dispatch breakpoint hit to analyzer manager for notification
            // Note: No MessageCenter notification is sent for analyzer breakpoints
            if (analyzerMgr)
            {
                analyzerMgr->dispatchBreakpointHit(pc, breakpointID, this);
            }

            // Only pause for debugger breakpoints (not analyzer-owned)
            if (!isAnalyzerBreakpoint)
            {
                // Pause and park, or during a direct run stop before this instruction
                if (emulator.OnBreakpointHit(breakpointID, pc, BreakpointHitKind::Execute))
                    return true;
            }
        }
    }

    // Dispatch CPU step event to analyzers (coverage / tracing). The guard
    // keeps the no-subscriber path down to a null and an empty check per
    // instruction; dispatchCPUStep itself early-returns when the analyzer
    // master toggle is off. Runs outside the debug-mode guard so coverage
    // sessions work without full debug mode, and before the fast-tape trap
    // so LD_BYTES invocations are recorded even when the trap consumes them.
    if (_context->pDebugManager != nullptr)
    {
        AnalyzerManager* analyzerMgr = _context->pDebugManager->GetAnalyzerManager();
        if (analyzerMgr != nullptr && analyzerMgr->hasCPUStepSubscribers())
        {
            analyzerMgr->dispatchCPUStep(this, pc);
        }
    }

    // Fast tape loading trap (design: docs/inprogress/2026-08-30-fast-tape-loading).
    // A ROM LD-BYTES ($0556) invocation is replaced wholesale when armed: the
    // block payload is copied straight from the tape image and the routine's
    // documented exit state is emulated. Any decline is fully inert — the CPU
    // just proceeds into the real ROM code. Runs after breakpoint dispatch so
    // user breakpoints at $0556 keep firing, and outside the debug-mode guard
    // so the trap is active in both debug and release sessions.
    if (pc == ROMAddresses::LD_BYTES && _context->pTapeFastLoad != nullptr)
    {
        // While TTD records (a black box: an explicit recording masks the
        // shortcut) the trap is an edit with what it did - the block's bytes,
        // the registers, the tape cursor and its time - so a replay repeats it
        ttd::ITimeTravelHooks* ttd = _context->pTimeTravelHooks;
        const bool recorded = ttd && ttd->IsRecording();
        if (recorded)
            ttd->BeginToolEdit();
        const bool consumed = _context->pTapeFastLoad->HandleLDBytesTrap(*this);
        if (recorded)
            ttd->EndToolEdit(consumed ? "fast tape" : nullptr);
        if (consumed)
        {
            // Trap consumed the invocation — the routine never executes
            return true;
        }
    }

    // TR-DOS disk autostart: one-shot rewrite of the cold-start RUN "boot" line into RUN "<name>".
    // Only armed for a single-BASIC-program autostart; one compare per instruction otherwise. While
    // TTD records (a black box restarted after the autostart's reset) the rewrite is an edit with
    // its bytes, so a replay repeats it; a replay never runs the hook itself (state-registry gap 16)
    if (pc == DiskAutostart::COMMAND_LOOP_ENTRY && _context->pDiskAutostart != nullptr &&
        _context->pDiskAutostart->IsArmed())
    {
        ttd::ITimeTravelHooks* ttd = _context->pTimeTravelHooks;
        if (!ttd || !ttd->IsReplayActive())
        {
            const bool recorded = ttd && ttd->IsRecording();
            if (recorded)
                ttd->BeginToolEdit();
            const bool rewritten = _context->pDiskAutostart->HandleCommandLoopHook(*this);
            if (recorded)
                ttd->EndToolEdit(rewritten ? "disk autostart" : nullptr);
        }
    }

    // Fast disk loading trap (Layer B ROM $3FEC INI sector drain loop trap).
    // Drains pending sector bytes directly into memory via Z80::wd() when armed.
    if (pc == 0x3FEC && _context->pDiskFastLoad != nullptr)
    {
        // Recorded as an edit like the tape trap above (the FDC's state is in it)
        ttd::ITimeTravelHooks* ttd = _context->pTimeTravelHooks;
        const bool recorded = ttd && ttd->IsRecording();
        if (recorded)
            ttd->BeginToolEdit();
        const bool consumed = _context->pDiskFastLoad->HandleSectorDrainTrap(*this);
        if (recorded)
            ttd->EndToolEdit(consumed ? "fast disk" : nullptr);
        if (consumed)
        {
            // Trap consumed the sector drain loop invocation
            return true;
        }
    }

    return false;
}

bool Z80::IdleStepsInert() const
{
    // The work gate: only the engine and the machine's interrupt source (TTD input, a machine step hook, RZX, a
    // device INT line, the PC history all act per step)
    constexpr uint32_t kIdleWork = EmulatorContext::kStepWorkEngine | EmulatorContext::kStepWorkInterruptSource;
    if (_context->stepWork.load(std::memory_order_relaxed) & ~kIdleWork)
        return false;
    if (_nmi_pending_count > 0 || _context->emulatorState.nmiAtIntStartPending)
        return false;

    // The instruction-start work (RunInstructionStartHooks, EngineStep) and the bus observers
    if (isDebugMode || busTraceHook || InstructionStartObserved() || cycles_to_capture > 0)
        return false;
    if (_feature_opcodeprofiler_enabled && _opcodeProfiler)
        return false;
    if (_feature_calltrace_enabled && _memory && _memory->GetAccessTracker().IsCalltraceCapturing())
        return false;
    if (DebugManager* debug = _context->pDebugManager)
    {
        const AnalyzerManager* analyzers = debug->GetAnalyzerManager();
        if (analyzers && analyzers->hasCPUStepSubscribers())
            return false;
    }
    if (_context->emulatorState.flags & (CF_SETDOSROM | CF_LEAVEDOSADR | CF_LEAVEDOSRAM))
        return false;
    if ((pc == ROMAddresses::LD_BYTES && _context->pTapeFastLoad) || (pc == 0x3FEC && _context->pDiskFastLoad) ||
        (pc == DiskAutostart::COMMAND_LOOP_ENTRY && _context->pDiskAutostart))
        return false;
    if (const ttd::ITimeTravelHooks* ttd = _context->pTimeTravelHooks; ttd && (ttd->IsRecording() || ttd->IsReplayActive()))
        return false;

    // The peripherals MainLoop::OnCPUStep steps: the screen and the sound catch up by time; the tape writes
    // its edges into the sound per step, the floppy controller runs its state machine per step
    if (_context->pTape && _context->pTape->IsPlaying())
        return false;
    if (_context->pBetaDisk && !_context->pBetaDisk->IsStepInert())
        return false;
    return true;
}

/// Single CPU command cycle (non-interruptable)
// CPU-LIBRARY-MIGRATION(native-step): the fetch / execute / Q block below moves into the engine; the work around
// it is EngineStep's
void Z80::Z80Step(bool skipBreakpoints)
{
    [[maybe_unused]] Z80& cpu = *this;
    [[maybe_unused]] const CONFIG& config = _context->config;
    [[maybe_unused]] EmulatorState& state = _context->emulatorState;
    [[maybe_unused]] TEMP& temporary = _context->temporary;
    [[maybe_unused]] Memory& memory = *_context->pMemory;
    [[maybe_unused]] Emulator& emulator = *_context->pEmulator;

    // Boundary state describes the boundary BEFORE this step (it already
    // decided INT/NMI acceptance in ProcessInterrupts); this step's own
    // instruction sets a new one (EI, RETN/RETI, LD A,I/R, redundant prefix).
    // Usually there is none: the common path pays one load and one test
    const uint8_t entryBoundary = cpu.boundary;
    bool prefixPending = false;
    if (entryBoundary != Z80_BOUNDARY_NONE) [[unlikely]]
    {
        cpu.boundary = Z80_BOUNDARY_NONE;
        prefixPending = entryBoundary == Z80_BOUNDARY_PREFIX_DD || entryBoundary == Z80_BOUNDARY_PREFIX_FD;
    }

    // A pending prefix continues an instruction whose start (first prefix
    // byte) already went through the instruction-start work in the previous
    // step - breakpoints and traps must not fire mid-instruction
    if (!prefixPending && RunInstructionStartHooks(skipBreakpoints))
        return;

    // The halted CPU: tested first, so the normal path pays the same one test as before (the HALT latch instead of
    // the vm1 flag)
    if (cpu.halted && !prefixPending) [[unlikely]]
    {
        if (cpu.vm1)
        {
            // Z80 in HALT state. No further opcode processing will be done until INT or NMI arrives
            cpu.tt += cpu.rate * 1;

            // Frame cost accounting: one halted step burns exactly one t-state
            // (rate is fixed at 256 — speed multipliers scale frameLimit instead)
            state.tstates_halted_current++;

            if (++cpu.halt_cycle == 4)
            {
                // The refresh counter's 7 bits, bit 7 kept (as m1_cycle). Only on the vm1 HALT model, which
                // nothing selects today
                cpu.r_low = ((cpu.r_low + 1) & 0x7F) | (cpu.r_low & 0x80);
                cpu.halt_cycle = 0;
            }
        }
        else
        {
            HaltedM1();
        }
    }
    else
    {
        // Save F register before opcode execution (for Q register update)
        uint8_t prev_f = cpu.f;
        cpu.prefix = 0x0000;

        if (prefixPending)
        {
            // The previous step fetched this prefix (its M1, R and T are
            // spent) and stopped because the prefix before it was redundant:
            // run the instruction the pending prefix introduces, from the
            // byte after it. The instruction starts at the prefix: its start
            // was recorded when it was fetched (ddfd_prefixes); m1_pc is set
            // again because it is host-side state a TTD restore at this
            // boundary does not bring back
            cpu.opcode = (entryBoundary == Z80_BOUNDARY_PREFIX_DD) ? 0xDD : 0xFD;
            m1_pc = static_cast<uint16_t>(cpu.pc - 1);
        }
        else
        {
            // Scorpion "Even M1" (config EvenM1): the CPU's DRAM slot is tied to one phase of the video counter,
            // and an opcode fetch - which samples the bus half a T-state earlier than a data read - only fits it
            // on an even T-state. The board's WAIT logic stretches an opcode fetch from RAM that would start on
            // an odd T-state by one T-state (SC15.1 EPLD equations: M1 & RAM select & phase, normal mode only).
            // Fetches from ROM, data accesses, I/O and the interrupt acknowledge never wait. RAM select, not the
            // address: RAM paged in at #0000 counts. Only the instruction's first M1 is checked: every prefix M1
            // is 4 T, so a later M1 inherits the parity. docs/inprogress/2026-09-28-m1-contention/
            // contention-by-machine.md section 6
            // CPU-LIBRARY-MIGRATION(even-m1): a contention-hook rule on the engine's M1 access kind
            if (config.even_M1 && (cpu.tt & cpu.rate) && state.hw_turbo_ratio <= 1 &&
                (cpu.pch >= 0x40 || !memory.IsBank0ROM())) [[unlikely]]
                cpu.tt += cpu.rate;

            // Preserve previous PC register state
            cpu.prev_pc = m1_pc;

            // Regular Z80 bus cycle
            // 1. Fetch opcode (Z80 M1 bus cycle)
            cpu.opcode = m1_cycle();
        }

        // 1a. Call trace hook (pre-execution) — appends control-flow events
        // while a calltrace session is capturing; the decoder wants the
        // register state the instruction acts on (SP before CALL pushes /
        // RET pops). The cached feature flag keeps this to a single bool check
        // when calltrace is off
        if (_feature_calltrace_enabled && _memory != nullptr)
        {
            MemoryAccessTracker& tracker = _memory->GetAccessTracker();
            if (tracker.IsCalltraceCapturing())
            {
                tracker.GetCallTraceBuffer()->LogIfControlFlow(_context, _memory, m1_pc,
                                                               _context->emulatorState.frame_counter);
            }
        }

        // 2. Emulate fetched Z80 opcode
        (normal_opcode[opcode])(&cpu);

        // 2a. Opcode profiling hook (after opcode execution)
        // CPU-LIBRARY-MIGRATION(opcode-profiler): prefix / opcode come from the engine's decoded opcode word
        if (_feature_opcodeprofiler_enabled && _opcodeProfiler)
        {
            _opcodeProfiler->LogExecution(m1_pc, prefix, opcode, f, a, _context->emulatorState.frame_counter, t);
        }

        // 3. Update Q register based on whether flags were modified
        // CPU-LIBRARY-MIGRATION(q-register): the engine keeps Q per instruction class
        // Q captures YF/XF from flag-modifying instructions only
        // SCF/CCF update Q internally even if F doesn't numerically change
        if (cpu.f != prev_f)
        {
            cpu.q = cpu.f & 0x28;  // Flags changed: capture YF/XF
        }
        else if (cpu.opcode == 0x37 || cpu.opcode == 0x3F)
        {
            // SCF/CCF set Q internally, preserve their value
        }
        else
        {
            cpu.q = 0;  // Non-flag-modifying instruction: Q=0
        }
    }

    /// region <Debug trace capture>

    // Trace CPU for all duration of cycles requested
    if (cycles_to_capture > 0)
    {
        static char buffer[1024];
        DumpZ80State(buffer, sizeof(buffer) / sizeof(buffer[0]));
        LOGINFO(buffer);
    }

    /// endregion </Debug trace capture>
}

/// Z80Step for a machine with an instruction engine (ICpuEngine): the same
/// work around the instruction, in the same order, the instruction from the
/// engine. The engine owns the boundary state, HALT and the prefixes; a step
/// behind a pending prefix continues an instruction whose start already ran
/// the instruction-start work. Its start observers run here, one step later
/// than the native core runs them for a redundant prefix (inside ddfd_prefixes)
void Z80::EngineStep(bool skipBreakpoints)
{
    const bool prefixPending = boundary == Z80_BOUNDARY_PREFIX_DD || boundary == Z80_BOUNDARY_PREFIX_FD;
    if (!prefixPending && RunInstructionStartHooks(skipBreakpoints))
        return;

    // m1_pc and the start observers at the instruction's first M1, as m1_cycle does it (prefix == 0)
    if (!prefixPending)
        prev_pc = m1_pc;
    RecordInstructionStart(prefixPending ? static_cast<uint16_t>(pc - 1) : pc);

    // Call trace (pre-execution): decodes the instruction at m1_pc with the registers it acts on
    if (_feature_calltrace_enabled && _memory != nullptr)
    {
        MemoryAccessTracker& tracker = _memory->GetAccessTracker();
        if (tracker.IsCalltraceCapturing())
            tracker.GetCallTraceBuffer()->LogIfControlFlow(_context, _memory, m1_pc, _context->emulatorState.frame_counter);
    }

    _engine->ExecuteStep();

    if (_feature_opcodeprofiler_enabled && _opcodeProfiler)
        _opcodeProfiler->LogExecution(m1_pc, prefix, opcode, f, a, _context->emulatorState.frame_counter, t);

    if (cycles_to_capture > 0)
    {
        static char buffer[1024];
        DumpZ80State(buffer, sizeof(buffer) / sizeof(buffer[0]));
        LOGINFO(buffer);
    }
}

/// @brief Apply the queued frequency multiplier change, if any.
///
/// The effective multiplier is the host speed control (next_) times the
/// model-neutral hardware clock ratio (hw_turbo_ratio, 1..8), e.g. the
/// Scorpion ZS-256 Turbo+ hardware turbo flip-flop (hardware-reference 13):
/// guest code toggles it mid-frame with IN from the #7FFD / #1FFD register
/// families, but the real GAL re-aligns the clock to a cycle boundary anyway,
/// so applying at the frame boundary preserves the software-visible contract
/// (2x T-states per 50 Hz frame) without mid-frame rescaling of frameLimit /
/// the INT window. Every consumer (screen descale, sound pacing, tape timing,
/// INT position) already divides by the effective multiplier, so composition
/// needs no further changes.
void Z80::ApplyQueuedFrequencyMultiplier()
{
    [[maybe_unused]] Z80& cpu = *this;
    EmulatorState& state = _context->emulatorState;

    // The one place per frame the product is formed: every per-access consumer
    // reads the composed multiplier (or the applied ratio) and pays nothing extra
    const uint8_t ratio = state.hw_turbo_ratio ? state.hw_turbo_ratio : 1;
    const uint8_t desiredMultiplier = static_cast<uint8_t>(state.next_z80_frequency_multiplier * ratio);

    // Always taken over, also when the product did not change (host 2x at ratio 1
    // -> host 1x at ratio 2): the audio descale must follow the ratio in effect
    state.hw_turbo_ratio_applied = ratio;
    const uint8_t den = state.hw_clock_den > 1 ? state.hw_clock_den : 1;
    const bool denChanged = den != (state.hw_clock_den_applied > 1 ? state.hw_clock_den_applied : 1);
    state.hw_clock_den_applied = den;

    // Checked every frame regardless of whether this frame itself changed
    // anything: a guest that was oscillating and then settled needs a nudge
    // even on a frame where nothing happens, or the UI would be stuck
    // showing a stale "lo<->hi" range forever once the flips stop
    SettleCpuFreqOscillationIfQuiet();

    if (desiredMultiplier != state.current_z80_frequency_multiplier || denChanged)
    {
        uint8_t oldMultiplier = state.current_z80_frequency_multiplier;
        state.current_z80_frequency_multiplier = desiredMultiplier;
        state.current_z80_frequency = static_cast<uint32_t>(static_cast<uint64_t>(state.base_z80_frequency) * desiredMultiplier / den);

        // Reset rate to normal - counter represents actual t-states
        // Speed multipliers are handled by adjusting frame duration and timings
        cpu.rate = 256;

        // Debug level: a guest can flip its clock several times a frame (TS-Conf
        // software toggles SYS_CONFIG around SD I/O), and an info line per flip
        // floods the log window during playback
        MLOGDEBUG("Z80::ApplyQueuedFrequencyMultiplier - Applied speed multiplier: %dx -> %dx (%.2f MHz, rate=%d, hw_turbo_ratio=%u)", oldMultiplier,
                 state.current_z80_frequency_multiplier, state.current_z80_frequency / 1'000'000.0, cpu.rate, ratio);

        NotifyCPUFrequencyChanged();
    }
}

/// Post NC_CPU_FREQ_CHANGED, classifying the flip for display at the source
/// instead of leaving it to whichever consumer happens to be watching.
///
/// Background: TS-Conf's Wild Commander flips SYS_CONFIG's clock bits [1:0]
/// from the same PC once every single frame while its AY module player runs
/// (verified live via porttrace: 0x6BD5 writes 0x00/0x02 alternately, one
/// frame apart - a software "turbo during the heavy part of the frame, back
/// to normal for the rest" trick; NeoGS playback never does this, since the
/// hardware card needs no CPU-side mixing help). Reporting the bare current
/// value on every one of those flips is correct but useless to a human: two
/// independent GUI timers used to read it at their own cadence (one
/// instantaneous, one accumulating) and stomp on each other's text, which is
/// what actually produced the "random long text that appears and vanishes"
/// symptom - not a port-decode bug.
///
/// The fix lives here, once, instead of in every consumer: classify a flip as
/// "oscillating" the moment two flips land within 1s of each other, track the
/// band of values it bounces between, and only drop back to reporting a
/// single value after the clock has sat still for 3s (SettleCpuFreqOscillationIfQuiet,
/// called every frame so a guest going quiet is noticed even without a new
/// flip to trigger it). _frequencyHz/_freqMultiplier in the payload are
/// unaffected by any of this - they are always the true current value, so
/// ScorpionTurbo_Test.TurboStrobePostsCpuFreqChanged and any other consumer
/// that only reads those two fields keeps working unchanged. _oscillating
/// plus the _oscLowHz/_oscHighHz band are purely additive display hints.
void Z80::NotifyCPUFrequencyChanged()
{
    const uint32_t freqHz = _context->emulatorState.current_z80_frequency;
    const auto now = std::chrono::steady_clock::now();

    constexpr auto kOscillationGap = std::chrono::seconds(1);
    const bool hadPriorChange = _freqLastChangeTime.time_since_epoch().count() != 0;
    const bool rapidFlip = hadPriorChange && (now - _freqLastChangeTime) <= kOscillationGap;

    if (rapidFlip)
    {
        const uint32_t lo = _freqOscillating ? _freqOscLowHz : _freqPrevNotifiedHz;
        const uint32_t hi = _freqOscillating ? _freqOscHighHz : _freqPrevNotifiedHz;
        _freqOscLowHz = std::min({lo, hi, freqHz});
        _freqOscHighHz = std::max({lo, hi, freqHz});
        _freqOscillating = true;
    }
    // else: an isolated change, or a slow beat inside an already-open band -
    // SettleCpuFreqOscillationIfQuiet is the only thing that closes the band

    _freqLastChangeTime = now;
    _freqPrevNotifiedHz = freqHz;

    PostCpuFreqNotification();
}

void Z80::SettleCpuFreqOscillationIfQuiet()
{
    if (!_freqOscillating)
        return;

    constexpr auto kSettleAfter = std::chrono::seconds(3);
    if (std::chrono::steady_clock::now() - _freqLastChangeTime < kSettleAfter)
        return;

    _freqOscillating = false;
    PostCpuFreqNotification();
}

void Z80::PostCpuFreqNotification()
{
    MessageCenter& messageCenter = MessageCenter::DefaultMessageCenter();
    std::string emulatorId = _context->pEmulator ? _context->pEmulator->GetId() : "";
    messageCenter.Post(NC_CPU_FREQ_CHANGED,
                       new CPUFreqPayload(emulatorId,
                                          _context->emulatorState.current_z80_frequency,
                                          _context->emulatorState.current_z80_frequency_multiplier,
                                          _freqOscillating, _freqOscLowHz, _freqOscHighHz));
}

void Z80::RecomputeFrameTiming()
{
    const CONFIG& config = _context->config;
    const EmulatorState& state = _context->emulatorState;

    // EmulatorState::BaseToCpuT: base T x the composed clock (a fraction for the Profi's hi-res clock)
    _frameLimit = state.BaseToCpuT(config.frame);
    _intStart = state.BaseToCpuT(config.intstart);
    _intEnd = state.BaseToCpuT(config.intstart + config.intlen);

    // INT window crossing the frame end: its tail lives at the start of the
    // next frame (raised there by BeginFrame), so the in-frame end wraps
    _intWraps = _intEnd >= _frameLimit;
    if (_intWraps)
        _intEnd -= _frameLimit;
}

void Z80::BeginFrame()
{
    // Apply queued HOST speed multiplier change at the frame boundary (if any).
    // Hardware turbo strobes are applied immediately by ApplyHardwareTurboNow
    ApplyQueuedFrequencyMultiplier();

    // Scaled frame length and INT window - members, so a mid-frame hardware
    // turbo switch (ApplyHardwareTurboNow) is picked up by the next step
    RecomputeFrameTiming();

    haltpos = 0;

    // INT interrupt handling lasts for more than 1 frame (unless the pulse was
    // already acknowledged on a machine that clears INT at the acknowledge)
    if (_intWraps && !int_acked_in_pulse && !frameIntMasked)
        int_pending = true;
}

// CPU-LIBRARY-MIGRATION(step-routing): the plain step calls the engine; the kStepWorkEngine bit goes
Z80::StepResult Z80::StepInstruction(bool skipBreakpoints)
{
    // A CPU never put through a frame start (bare Core fixtures driving the
    // CPU directly) has no frame geometry yet
    if (_frameLimit == 0)
        RecomputeFrameTiming();

    // The per-step work gate (EmulatorContext::stepWork): one relaxed load and
    // one branch per instruction for every rare job together - TTD input, a
    // machine's own INT logic, a machine engine. With none the step below is
    // exactly the classic machine's step
    if (const uint32_t work = _context->stepWork.load(std::memory_order_relaxed)) [[unlikely]]
        return StepInstructionWithWork(work, skipBreakpoints);

    StepResult result;

    // Handle interrupts if arrived. Returns true if an interrupt was accepted -
    // in that case the acceptance IS the "instruction" that consumes this step
    const bool nmiPending = _nmi_pending_count > 0;
    if (ProcessInterruptsImpl<false>(_intWraps, _intStart, _intEnd))
    {
        if (nmiPending)
            result.nmiAccepted = true;
        else
            result.intAccepted = true;
    }
    else
    {
        // Perform single Z80 command cycle
        Z80Step(skipBreakpoints);
    }

    // Update peripheral states after CPU cycle
    OnCPUStep();

    return result;
}

/// The step with rare work around it (StepInstruction's gate is non-zero).
/// Out of line so the plain step stays small; the order is the contract:
/// input first, then the interrupt decision, the instruction, the machine
/// engine, the peripherals
Z80::StepResult Z80::StepInstructionWithWork(uint32_t work, bool skipBreakpoints)
{
    // Input takes effect before this instruction: recorded journal events due
    // at or before now (TTD playback) and live input queued by other threads.
    // An event stamped T is first visible to the instruction starting at T -
    // the machine state AT T (a seek target, a pause) does not include it yet
    if ((work & EmulatorContext::kStepWorkTtdInput) && _context->pTimeTravelHooks)
        _context->pTimeTravelHooks->ServiceInput();

    StepResult result;

    // RZX playback (design §5): the frame's fetch count reached at an
    // instruction boundary ends the RZX frame, and the interrupt that closes
    // it is this step (the machine's own frame INT is masked meanwhile).
    // Otherwise the step's R increments are counted (see RzxCountFetches)
    rzx::RzxPlayer* rzxPlayer = (work & EmulatorContext::kStepWorkRzx) ? _context->rzxPlayer : nullptr;
    RzxBoundary rzxBoundary = RzxBoundary::None;
    uint8_t rzxR0 = 0;
    uint64_t rzxFramesBefore = 0;
    if (rzxPlayer) [[unlikely]]
    {
        rzxFramesBefore = rzxPlayer->FramesDone();
        rzxBoundary = RzxFrameEnd(*rzxPlayer);
        rzxR0 = r_low;
        rLoadAdjust = 0;
    }

    if (rzxBoundary != RzxBoundary::None)
    {
        // The frame end is this step: the forced interrupt, or the machine
        // replaced by a snapshot block
        result.intAccepted = rzxBoundary == RzxBoundary::Interrupt;
    }
    else
    {
        const bool nmiPending = _nmi_pending_count > 0;
        const bool accepted = ProcessInterruptsSelect((work & EmulatorContext::kStepWorkInterruptSource) && _interruptSource,
                                                      (work & EmulatorContext::kStepWorkDeviceInt) && _deviceIntLines,
                                                      _intWraps, _intStart, _intEnd);
        if (accepted)
        {
            if (nmiPending)
                result.nmiAccepted = true;
            else
                result.intAccepted = true;
        }
        else
        {
            // A debugger's PC history (PcHistory::Arm): the instruction about to run and its window's page
            if ((work & EmulatorContext::kStepWorkPcHistory) && _context->pDebugManager) [[unlikely]]
                _context->pDebugManager->GetPcHistory()->Record(pc);
            if ((work & EmulatorContext::kStepWorkEngine) && _engine)
            {
                // CPU-LIBRARY-MIGRATION(step-routing): the machine runs on its own engine (Z80::SetEngine)
                EngineStep(skipBreakpoints);
            }
            else
            {
                Z80Step(skipBreakpoints);
            }
        }
    }

    // The machine engine first (IMachineStepHook): the screen and the sound
    // then see the state this step produced
    if ((work & EmulatorContext::kStepWorkMachineStep) && _machineStepHook)
        _machineStepHook->OnMachineStep(t);

    OnCPUStep();

    if (rzxPlayer) [[unlikely]]
    {
        // The step's fetches: the R delta (7 bits); the acknowledge of an
        // accepted INT / NMI is not a fetch; LD R,A reports the R it replaced
        // (rLoadAdjust). Worked example: R = #7E before `DD 21 nn nn` (LD
        // IX,nn), #00 after: (#00 - #7E) & #7F = 2 fetches
        if (rzxBoundary == RzxBoundary::None && rzxPlayer->IsPlaying())
        {
            uint8_t fetches = static_cast<uint8_t>((r_low - rzxR0 + rLoadAdjust) & 0x7F);
            if (result.intAccepted || result.nmiAccepted)
                fetches = static_cast<uint8_t>(fetches - 1);
            rzxPlayer->AddFetches(fetches);
        }
        // A frame end (with its interrupt or, IFF1 clear, without) while TTD
        // records: a fact at the end of this step, where a seek to "RZX
        // frame N" lands (Phase 3, Step 2)
        if (rzxPlayer->FramesDone() != rzxFramesBefore && _context->pTimeTravelHooks)
            _context->pTimeTravelHooks->NoteRzxFrameEnd(rzxPlayer->FramesDone(),
                                                          rzxBoundary == RzxBoundary::Interrupt);
        if (rzxPlayer->EndPending())
            rzxPlayer->NotifyEnded();
    }

    return result;
}

/// The RZX frame end at this boundary, when the frame's fetch count is
/// reached: the player checks the frame and moves on; true when the
/// interrupt that ends it was taken (HandleINT) as this step. A redundant-
/// prefix boundary is inside an instruction: the frame then ends at the next
/// real boundary (within the player's overrun tolerance)
Z80::RzxBoundary Z80::RzxFrameEnd(rzx::RzxPlayer& player)
{
    const bool prefixPending = boundary == Z80_BOUNDARY_PREFIX_DD || boundary == Z80_BOUNDARY_PREFIX_FD;
    if (prefixPending || !player.FrameDue())
        return RzxBoundary::None;

    // Distance from the machine's own INT position, for the drift statistic
    int32_t drift = static_cast<int32_t>(t) - static_cast<int32_t>(_intStart + 1);
    const int32_t frame = static_cast<int32_t>(_frameLimit);
    if (frame > 0)
    {
        if (drift > frame / 2)
            drift -= frame;
        else if (drift <= -frame / 2)
            drift += frame;
    }

    // A keyframe for seeking back, taken at the boundary before the frame ends
    player.MaybeKeyframe();

    const rzx::FrameEnd end = player.EndFrame(pc, iff1 != 0, boundary == Z80_BOUNDARY_INT_SHADOW, drift);
    if (end == rzx::FrameEnd::Snapshot)
    {
        // Multiload / rollback: the recording's snapshot block replaces the machine
        player.ApplyPendingSnapshot();
        return RzxBoundary::Replaced;
    }
    if (end != rzx::FrameEnd::Interrupt)
        return RzxBoundary::None;

    // The NMOS LD A,I / LD A,R parity quirk only by option (SkoolKit flag 1)
    // CPU-LIBRARY-MIGRATION(rzx-boundary): reads / writes the engine's boundary register
    if (!player.Options().ldAirParityQuirk && boundary == Z80_BOUNDARY_LD_A_IR)
        boundary = Z80_BOUNDARY_NONE;
    HandleINT(0xFF);
    return RzxBoundary::Interrupt;
}

void Z80::SetInterruptSource(IInterruptSource* source)
{
    _interruptSource = source;
    _waitObserver = source && source->ObservesWaits() ? source : nullptr;
    _context->SetStepWork(EmulatorContext::kStepWorkInterruptSource, source != nullptr);
}

void Z80::SetDeviceIntLine(uint32_t line, bool asserted)
{
    const uint32_t lines = asserted ? (_deviceIntLines | line) : (_deviceIntLines & ~line);
    if (lines == _deviceIntLines)
        return;
    _deviceIntLines = lines;
    _context->SetStepWork(EmulatorContext::kStepWorkDeviceInt, lines != 0);
}

void Z80::SetMachineStepHook(IMachineStepHook* hook)
{
    _machineStepHook = hook;
    _context->SetStepWork(EmulatorContext::kStepWorkMachineStep, hook != nullptr);
}

void Z80::SetMachineStepWork(bool on)
{
    _context->SetStepWork(EmulatorContext::kStepWorkMachineStep, on && _machineStepHook != nullptr);
}

void Z80::SetEngine(ICpuEngine* engine)
{
    _engine = engine;
    _context->SetStepWork(EmulatorContext::kStepWorkEngine, engine != nullptr);
}

void Z80::ApplyHardwareTurboNow()
{
    Z80& cpu = *this;
    EmulatorState& state = _context->emulatorState;

    const uint8_t ratio = state.hw_turbo_ratio ? state.hw_turbo_ratio : 1;
    uint8_t desiredMultiplier = static_cast<uint8_t>(state.next_z80_frequency_multiplier * ratio);
    uint8_t oldMultiplier = state.current_z80_frequency_multiplier;
    const uint32_t desiredDen = state.hw_clock_den > 1 ? state.hw_clock_den : 1u;
    const uint32_t oldDen = state.ClockDen();
    if ((desiredMultiplier == oldMultiplier && desiredDen == oldDen) || oldMultiplier == 0)
        return;

    // Preserve the raster instant: the in-frame position is expressed in
    // scaled T-states, so it must be rescaled together with the multiplier
    // (the same instant is 2x further into a 2x longer frame; 10/7 x for the
    // Profi's 5 MHz hi-res clock). haltpos is a frame position too
    auto rescale = [&](uint32_t v) {
        return static_cast<uint32_t>(static_cast<uint64_t>(v) * desiredMultiplier * oldDen / (static_cast<uint64_t>(oldMultiplier) * desiredDen));
    };
    cpu.t = rescale(cpu.t);
    cpu.haltpos = static_cast<uint16_t>(rescale(cpu.haltpos));

    state.current_z80_frequency_multiplier = desiredMultiplier;
    state.current_z80_frequency = static_cast<uint32_t>(static_cast<uint64_t>(state.base_z80_frequency) * desiredMultiplier / desiredDen);
    state.hw_turbo_ratio_applied = ratio;
    state.hw_clock_den_applied = static_cast<uint8_t>(desiredDen);
    cpu.rate = 256;

    // The running Z80FrameCycle loop reads these every iteration
    RecomputeFrameTiming();

    // Mid-frame hardware strobes must notify too: they never pass through a
    // frame boundary, so ApplyQueuedFrequencyMultiplier will see
    // desiredMultiplier == current and post nothing on the next frame
    NotifyCPUFrequencyChanged();
}

/// Execute number of cpu cycles equivalent to full frame screen render
void Z80::Z80FrameCycle()
{
    // A CPU never put through a frame start (bare Core fixtures) has no
    // frame geometry yet
    if (_frameLimit == 0)
        RecomputeFrameTiming();

    // A halted CPU's idle cycles may run in one go up to the frame end (an engine's fast-forward)
    IdleSkipScope idleSkip(*this, UINT32_MAX);

    // Cover whole frame (control by effective t-states)
    while (t < _frameLimit)
    {
        // Mid-frame pause park. Pause() is otherwise observed only at frame
        // boundaries (MainLoop::Run), so an in-flight frame - which under
        // turbo/debug-mode or TTD recording can take hundreds of milliseconds -
        // would delay the park and its confirmation until the frame completes.
        // One volatile read per instruction keeps the unpaused hot path cheap;
        // WaitWhilePaused parks + confirms and wakes on Resume()/Stop() via CV.
        // Everything the loop reads lives in members, so work done while parked
        // (a TTD restore, stepping from another thread) is picked up here
        if (Emulator* emulator = _context->pEmulator; emulator && emulator->IsPaused())
            emulator->WaitWhilePaused();

        StepInstruction();
    }
}

/// endregion </Methods>

/// region <Z80 lifecycle>

/// True while any instruction-start observer is armed
__forceinline bool Z80::InstructionStartObserved() const
{
    return m1TraceHook || _context->ttdCoverageActive || _context->ttdProbe.IsArmed();
}

void Z80::NotifyMachineM1Before(uint16_t address)
{
    machineM1Hook->BeforeMachineM1(address);
}

void Z80::NoteAcknowledgeRefresh(uint32_t t3)
{
    // As Memory::MemoryReadM1Snow: R before the increment; the Ferranti ULA machines only (ioContention)
    if (ioContention && ioContention->IsSlotContended(static_cast<uint8_t>(i >> 6)))
        ioContention->NoteRefresh(t3, static_cast<uint8_t>(r_low - 1));
}

void Z80::NotifyMachineM1(uint16_t address)
{
    machineM1Hook->OnMachineM1(address);
}

// CPU-LIBRARY-MIGRATION(m1-cycle): the engine's M1 bus callback (start observers, machineM1Hook, ULA snow)
uint8_t Z80::m1_cycle()
{
    /// region <Overriding submodule for module logger>
    [[maybe_unused]]
    const uint16_t _SUBMODULE = PlatformZ80SubmodulesEnum::SUBMODULE_Z80_M1;
    /// endregion </Overriding submodule for module logger>

    [[maybe_unused]] Z80& cpu = *this;
    [[maybe_unused]] const CONFIG& config = _context->config;
    [[maybe_unused]] EmulatorState& state = _context->emulatorState;
    [[maybe_unused]] const TEMP& temporary = _context->temporary;
    [[maybe_unused]] const PortDecoder& portDecoder = *_context->pPortDecoder;

    // Record PC for current opcode (prefixes should not alter original PC):
    // only the M1 that starts an instruction runs the instruction-start work.
    // Prefix handlers set a non-zero prefix BEFORE fetching the next byte, so
    // the M1s inside an instruction (after CB/ED/DD/FD) do not run it
    // The fast path stays inline (one store and one observer test); the
    // observers run out of line only while one is armed
    if (prefix == 0x0000)
    {
        m1_pc = cpu.pc;
        if (InstructionStartObserved())
            NotifyInstructionStart();
    }

    // Z80 CPU M1 cycle logic
    r_low = ((r_low + 1) & 0x7f) | (r_low & 0x80);  // Keep memory refresh register ticking

    // Board logic that decides what this fetch sees (ZX-Evo trdemu page swap)
    if (machineM1Hook) [[unlikely]]
        NotifyMachineM1Before(cpu.pc);

    // The opcode read; the contended interfaces also handle the refresh that follows it (ULA snow)
    opcode = rdM1(cpu.pc);  // Keep opcode copy for trace / debug purposes

    // Board logic clocked by the M1 refresh (ZX-Evo NMI exit / breakpoint).
    // Out of line like NotifyInstructionStart: the hot path is one pointer test
    if (machineM1Hook) [[unlikely]]
        NotifyMachineM1(cpu.pc);

    // Point PC to next byte
    cpu.pc++;

    // M1 cycle is always 4 CPU clocks (3 for memory read and 1 for decoding)
    // +3 will be done in rd() (Memory read) method
    // +1 will be done here
    IncrementCPUCyclesCounter(1);

    return opcode;
}

/// One idle opcode fetch of the halted CPU (docs/inprogress/2026-10-02-halt-fetch-address). The Z80's program
/// counter points past the HALT, so the fetch goes to the byte after it, which is read and discarded (HALT2INT v3
/// on a real 48K, MAME). This emulator keeps PC on the HALT while halted (the INT / NMI acknowledge steps past it;
/// the snapshot loaders, the debugger and TTD rely on that), so the bus address is PC + 1. Everything else is a
/// repeat of the HALT, as before: the instruction start at the HALT (m1_pc, the observers), one refresh, 4 T, Q = 0,
/// opcode #76 for the trace and the profiler. Out of line: the normal path never comes here
void Z80::HaltedM1()
{
    Z80& cpu = *this;
    const CONFIG& config = _context->config;
    const EmulatorState& state = _context->emulatorState;
    const uint16_t fetch = static_cast<uint16_t>(cpu.pc + 1);

    // Scorpion Even M1: the board looks at the fetch, so at the RAM select of the byte after the HALT
    if (config.even_M1 && (cpu.tt & cpu.rate) && state.hw_turbo_ratio <= 1 &&
        (fetch >= 0x4000 || !_context->pMemory->IsBank0ROM()))
        cpu.tt += cpu.rate;

    cpu.prev_pc = m1_pc;
    cpu.prefix = 0x0000;
    RecordInstructionStart(cpu.pc);  // the HALT repeating: one instruction start per idle fetch, as before

    cpu.r_low = ((cpu.r_low + 1) & 0x7f) | (cpu.r_low & 0x80);
    if (machineM1Hook) [[unlikely]]
        NotifyMachineM1Before(fetch);
    (void)rdM1(fetch);  // contention, the +2A / +3 latch, snow, the host bus overlays, the access tracker
    if (machineM1Hook) [[unlikely]]
        NotifyMachineM1(fetch);
    IncrementCPUCyclesCounter(1);

    cpu.opcode = 0x76;
    cpu.halt_cycle = 0;
    cpu.q = 0;  // the HALT writes no flags

    if (_feature_opcodeprofiler_enabled && _opcodeProfiler)
        _opcodeProfiler->LogExecution(m1_pc, 0, 0x76, f, a, _context->emulatorState.frame_counter, t);
}

/// Instruction-start bookkeeping: m1_pc (the address every access of the
/// instruction is attributed to - memory tracker, TTD write journal and
/// probes, calltrace) and the start observers (NotifyInstructionStart). Runs
/// once per instruction: inline in m1_cycle for the first byte, and from
/// ddfd_prefixes for a prefix that turned out to start an instruction of its
/// own (after a redundant one).
void Z80::RecordInstructionStart(uint16_t addr)
{
    m1_pc = addr;
    if (InstructionStartObserved())
        NotifyInstructionStart();
}

/// Instruction-start observers, for the instruction starting at m1_pc: the
/// M1 trace hook (TTD per-frame instruction capture), execution coverage and
/// the TTD execute probe
void Z80::NotifyInstructionStart()
{
    if (m1TraceHook)
        m1TraceHook(m1_pc);

    // Per-frame execution coverage for reverse search. One predictable
    // branch on a plain bool when recording is off, which is the common
    // case; the page lookup and the append only happen while a session is
    // actually capturing. This is the only record that a frame executed a
    // given address - instruction fetches are not journalled - so without
    // it a reverse breakpoint has no choice but to replay every frame.
    if (_context->ttdCoverageActive && _context->ttdCoverage != nullptr)
    {
        _context->ttdCoverage->Record(ttd::TTDCoverageKind::Executed,
                                      ttd::MakeCoverageKey(_memory->GetPhysPageForZ80Address(m1_pc), m1_pc));
    }

    // Phase 4 - access probe for Execute access type (TDD 9.2).
    // Fires once per instruction, at its start.
    if (_context->ttdProbe.IsArmed())
    {
        // Resolve the bank the opcode was fetched from, so a reverse
        // breakpoint can distinguish "PC 0xC000 in page 3" from the same
        // address reached with a different page banked in. Code executing
        // from ROM reports kPhysPageNone.
        const ttd::PhysPage execPhysPage = _memory->GetPhysPageForZ80Address(m1_pc);
        if (_context->ttdProbe.Matches(m1_pc, ttd::TTDAccessType::Execute, 0, m1_pc, execPhysPage))
        {
            const auto& st = _context->emulatorState;
            const ttd::TTDTimePoint tp{st.frame_counter, st.TtdTInFrame(t)};
            _context->ttdProbe.RecordHit(tp, m1_pc, m1_pc, /*value=*/0, execPhysPage,
                                          ttd::TTDAccessType::Execute);
        }
    }
}

/// Dispatching memory read method. Used directly from Z80 microcode (CPULogic and opcode)
/// Read access to memory takes 3 clock cycles
/// \param addr
/// \return
// CPU-LIBRARY-MIGRATION(memory-bus): a memory read callback; the 3 T and contention come from the engine
uint8_t Z80::rd(uint16_t addr, bool isExecution)
{
    // Video memory contention, where the machine has it, is part of the selected interface
    // (Memory::MemoryReadContended): it waits before the access, with these 3 T already counted
    IncrementCPUCyclesCounter(3);

    uint8_t value = (_memory->*MemIf->MemoryRead)(addr, isExecution);

    if (busTraceHook)
        busTraceHook('R', addr, value);

    return value;
}

/// The opcode fetch: rd through MemoryReadM1, which on the contended interfaces also notes the refresh that
/// follows (ULA snow); on every other interface it is the plain read, so nothing is added there
// CPU-LIBRARY-MIGRATION(m1-cycle): the engine's M1 read (kind M1) through MemoryReadM1
uint8_t Z80::rdM1(uint16_t addr)
{
    IncrementCPUCyclesCounter(3);

    uint8_t value = (_memory->*MemIf->MemoryReadM1)(addr, true);

    if (busTraceHook)
        busTraceHook('R', addr, value);

    return value;
}

/// Dispatching memory write method. Used directly from Z80 microcode (CPULogic and opcode)
/// Write access to memory takes 3 clock cycles
/// \param addr
/// \param val
// CPU-LIBRARY-MIGRATION(memory-bus): a memory write callback; the 3 T and contention come from the engine
void Z80::wd(uint16_t addr, uint8_t val)
{
    // Video memory contention: see rd (Memory::MemoryWriteContended)
    IncrementCPUCyclesCounter(3);

    (_memory->*MemIf->MemoryWrite)(addr, val);

    if (busTraceHook)
        busTraceHook('W', addr, val);
}

// CPU-LIBRARY-MIGRATION(io-bus): the engine's port-in callback (the Z84C15 engine already calls this)
uint8_t Z80::in(uint16_t port)
{
    // TTD port journal: while a session records, every IN result is appended
    // with its time and PC; while one replays, the CPU gets the recorded value
    // instead of the live device's answer, so the replay depends on nothing
    // outside the session - media files, host devices
    // (ttd-port-read-journal.md). The devices still see the read and its side
    // effects. Time and PC are taken at the start of the I/O cycle
    if (ttd::TTDPortJournal* journal = _context->ttdPortReads) [[unlikely]]
    {
        const EmulatorState& st = _context->emulatorState;
        const uint64_t frame = st.frame_counter;
        const uint32_t tInFrame = st.TtdTInFrame(t);
        const uint16_t pc = m1_pc;
        uint8_t value = inFromBus(port);
        if (rzx::RzxPlayer* player = _context->rzxPlayer) [[unlikely]]
            value = player->OnIn(port, value, pc);
        return journal->OnRead(port, value, frame, tInFrame, pc);
    }
    // RZX playback: the CPU gets the recorded value (emulator/rzx/, design
    // §4); the devices still see the read. Below the TTD journal, so a TTD
    // recording during playback stores the RZX-fed values
    if (rzx::RzxPlayer* player = _context->rzxPlayer) [[unlikely]]
        return player->OnIn(port, inFromBus(port), m1_pc);
    return inFromBus(port);
}

/// The read as the bus answers it: interceptor, model decoder, observer cards,
/// floating bus, I/O contention
// CPU-LIBRARY-MIGRATION(io-bus): ULA I/O contention moves to the engine's pre / post IORQ hook kinds
uint8_t Z80::inFromBus(uint16_t port)
{
    // ULA I/O contention (48K / 128K / +2), first part: the wait at the cycle's first T, before IORQ. The
    // handler has already counted that T (IORQ is at T2), so the cycle started 1 T ago. ioContention is
    // null on machines without it (Core::SelectMemoryInterface)
    uint8_t ioWait = 0;
    if (ioContention)
    {
        ioWait = ioContention->IoWaitBeforeIorq(port, (tt - rate) >> 8);
        IncrementCPUCyclesCounter(ioWait);
    }

    // Pre-decode interceptor (ZX-Poly platform ports): a consumed read never
    // reaches the model decoder, the observer cards or the floating bus
    if (portInterceptor) [[unlikely]]
    {
        uint8_t intercepted = 0xFF;
        if (portInterceptor->InterceptIn(port, intercepted))
        {
            if (busTraceHook)
                busTraceHook('I', port, intercepted);
            if (ioContention)
                IoWaitAfterIorq(port, ioWait);
            return intercepted;
        }
    }

    PortDecoder& portDecoder = *_context->pPortDecoder;

    // One bus cycle: the cards claiming the port (ZX-bus slots claim table) and
    // the model decode, resolved in one pass (PortDecoder::ReadCycle: the
    // shared-bus rule R6 lives there). A port no card claims costs one bit test
    bool cardDrove = false;
    uint8_t result = portDecoder.ReadCycle(port, m1_pc, cardDrove);

    if (busTraceHook)
        busTraceHook('I', port, result);

    // Floating bus: if no hardware device decoded the port, the ULA returns
    // the video byte currently on the data bus.
    // On ZX-48K/128K, any port with A0=1 (odd port) that isn't handled
    // by a specific device returns the floating bus value.
    // IMPORTANT: ports decoded by real hardware (WD1793, Kempston, etc.)
    // must NOT get the floating bus override even if they return 0xFF.
    // Full-decode observer cards are real hardware too - a handled observer
    // port always has a driver on the bus, floating bus must not apply.
    bool fromFloatingBus = false;
    bool lateWaitsCounted = false;
    if (!portDecoder.WasLastPortDecoded() && (port & 0x0001) && !cardDrove)
    {
        UlaContention* ula = _context->pUlaContention;
        if (ula)
        {
            // The +2A/+3 gate array drives only #0FFD-type ports (GetGateArrayFloatingBus). A port whose high
            // byte is in contended memory has ULA waits after IORQ: FloatingBusAfterLateWaits
            uint8_t floatVal;
            if (ula->IsGateArray())
                floatVal = ula->GetGateArrayFloatingBus(port);
            else if (ioContention && ioContention->IsSlotContended(static_cast<uint8_t>(port >> 14))) [[unlikely]]
            {
                floatVal = FloatingBusAfterLateWaits(port, ioWait);
                lateWaitsCounted = true;
            }
            else
                floatVal = ula->GetFloatingBus();
            if (floatVal != 0xFF)
            {
                result = floatVal;
                fromFloatingBus = true;
            }
        }
    }

    if (ioContention && !lateWaitsCounted)
        IoWaitAfterIorq(port, ioWait);

    // One flag for both observers: machines with neither test it once
    if (_inResultHooks) [[unlikely]]
    {
        if (portInterceptor)
            portInterceptor->OnInResult(port, result, fromFloatingBus);
        // A latch clocked by read cycles takes the same byte the CPU took (the 128K's #7FFD)
        if (readCycleLatch && (port & readCycleLatchMask) == readCycleLatchMatch)
            readCycleLatch->OnReadCycle(port, result);
    }

    return result;
}

// CPU-LIBRARY-MIGRATION(io-bus): the engine's port-out callback; ULA I/O contention via the hook kinds
void Z80::out(uint16_t port, uint8_t val)
{
    // TTD port journal: every OUT is logged as a fact of the machine's output
    // while a session records, and checked against the record while it
    // replays (ttd-port-read-journal.md)
    if (ttd::TTDPortJournal* journal = _context->ttdPortWrites) [[unlikely]]
        journal->OnWrite(port, val, _context->emulatorState.frame_counter, _context->emulatorState.TtdTInFrame(t),
                         m1_pc);

    // ULA I/O contention, as in in(): the wait before IORQ goes before the port write, so the device (the
    // border latch) sees the delayed T; the waits after IORQ follow the write
    uint8_t ioWait = 0;
    if (ioContention)
    {
        ioWait = ioContention->IoWaitBeforeIorq(port, (tt - rate) >> 8);
        IncrementCPUCyclesCounter(ioWait);
    }

    // Pre-decode interceptor (ZX-Poly platform ports): a consumed write never
    // reaches the model decoder or the observer cards
    if (portInterceptor && portInterceptor->InterceptOut(port, val)) [[unlikely]]
    {
        if (busTraceHook)
            busTraceHook('O', port, val);
        if (ioContention)
            IoWaitAfterIorq(port, ioWait);
        return;
    }

    PortDecoder& portDecoder = *_context->pPortDecoder;

    // One bus cycle: the cards claiming the port see the write first, then the
    // model decode (PortDecoder::WriteCycle). A port no card claims costs one
    // bit test
    portDecoder.WriteCycle(port, val, m1_pc);

    if (busTraceHook)
        busTraceHook('O', port, val);

    if (ioContention)
        IoWaitAfterIorq(port, ioWait);
}

void Z80::IoWaitAfterIorq(uint16_t port, uint8_t ioWait)
{
    const uint8_t after = ioContention->IoWaitAfterIorq(port, t);
    IncrementCPUCyclesCounter(after);
    if (isDebugMode)
        ioContention->CountAccess(CONTENTION_IO, static_cast<uint8_t>(ioWait + after));
}

/// The floating bus of an IN from a port whose high byte is in contended memory. The CPU takes the data at the
/// end of T3, after the ULA's waits after IORQ (C:1 before TW and T3), so the byte on the bus is the one the ULA
/// fetches then, not at IORQ. The lookup (UlaContention::GetFloatingBus) is calibrated to the uncontended cycle,
/// T3 two T after IORQ; with the waits counted first the clock is again two T before T3. The caller then skips its
/// own IoWaitAfterIorq (docs/inprogress/2026-09-30-fusetest-core-defects)
uint8_t Z80::FloatingBusAfterLateWaits(uint16_t port, uint8_t ioWait)
{
    IoWaitAfterIorq(port, ioWait);
    return _context->pUlaContention->GetFloatingBus();
}

/// The slow half of Idle: taken only while the ULA contends internal cycles or a bus trace hook listens.
/// Each T-state is its own check, like FUSE's contend_read_no_mreq(addr, 1)
// CPU-LIBRARY-MIGRATION(idle-cycles): the engine's Internal access kind on the contention hook
void Z80::IdleSlow(uint16_t addr, uint8_t cycles)
{
    const bool contended = idleContention && idleContention->IsSlotContended(static_cast<uint8_t>(addr >> 14));
    for (uint8_t i = 0; i < cycles; i++)
    {
        if (busTraceHook)
            busTraceHook('N', addr, 0);
        if (contended)
        {
            const uint8_t wait = idleContention->DelayAt(t);
            IncrementCPUCyclesCounter(wait);
            if (isDebugMode)
                idleContention->CountAccess(CONTENTION_IDLE, wait);
        }
        IncrementCPUCyclesCounter(1);
    }
}

void Z80::retn()
{
    // Called by the ED45 RETN handler after iff1 = iff2: leaving the NMI handler
    // ends the NMI session. Checking nmi_in_progress (not just restoring IFF1)
    // keeps a plain RET executed deep inside an NMI handler from silently
    // ending it - only RETN does that, per the Z80 interrupt architecture
    nmi_in_progress = false;
}

/// endregion </Z80 lifecycle>

/// Read byte directly from ZX-Spectrum memory (current memory bank setup used)
/// No cycle counters will be incremented
uint8_t Z80::DirectRead(uint16_t addr)
{
    uint8_t* remap_addr = _context->pMemory->MapZ80AddressToPhysicalAddress(addr);

    return *remap_addr;
}

//
// Write byte directly to RAM memory buffer
// No checks for ROM write access flags
// No cycle counters will be incremented
//
void Z80::DirectWrite(uint16_t addr, uint8_t val)
{
    uint8_t* remap_addr = _context->pMemory->MapZ80AddressToPhysicalAddress(addr);
    *remap_addr = val;
}

void Z80::RaiseLocalInt(unsigned lengthT)
{
    // End position in (frame, T): the frame counter advances and t is rebased
    // by the frame length at every frame boundary
    const uint32_t frameLength = _frameLimit != 0 ? _frameLimit : _context->config.frame;
    uint64_t endFrame = _context->emulatorState.frame_counter;
    uint32_t endT = t + lengthT;
    while (frameLength != 0 && endT >= frameLength)
    {
        endT -= frameLength;
        endFrame++;
    }
    _localIntEndFrame = endFrame;
    _localIntEndT = endT;
    _localIntArmed = true;
}

/// Simulate Z80 INT pin signal raising
/// Interrupt request will be processed before next CPU cycle in
void Z80::RequestMaskedInterrupt()
{
    Z80& cpu = *this;

    cpu.int_pending = true;
}

///
/// Simulate Z80 NMI pin signal raising
///
void Z80::RequestNonMaskedInterrupt()
{
    // Coalesce: the pin is level-less in the model - one pending bit regardless
    // of how many times the host pressed the magic button before the boundary
    _nmi_pending_count = 1;
}

///
/// See: http://www.z80.info/interrup.htm
/// \param int_occurred
/// \param int_start
/// \param int_end
bool Z80::ProcessInterrupts(bool int_occurred, unsigned int_start, unsigned int_end)
{
    // Direct callers (tests, tools) get the machine's INT logic when it has
    // one, and the device INT lines
    return ProcessInterruptsSelect(_interruptSource != nullptr, _deviceIntLines != 0, int_occurred, int_start, int_end);
}

/// The instantiation for this step: the machine's own INT logic or not, the
/// device lines or not. Only the work path (StepInstructionWithWork) and
/// direct callers come here; the plain step stays ProcessInterruptsImpl<false>
bool Z80::ProcessInterruptsSelect(bool useSource, bool deviceInt, bool int_occurred, unsigned int_start,
                                  unsigned int_end)
{
    if (useSource)
        return deviceInt ? ProcessInterruptsImpl<true, true>(int_occurred, int_start, int_end)
                         : ProcessInterruptsImpl<true, false>(int_occurred, int_start, int_end);
    return deviceInt ? ProcessInterruptsImpl<false, true>(int_occurred, int_start, int_end)
                     : ProcessInterruptsImpl<false, false>(int_occurred, int_start, int_end);
}

/// UseSource: the machine owns INT (IInterruptSource). DeviceInt: a device
/// holds /INT low (SetDeviceIntLine). Templates so the classic machines' step
/// carries no test for either (StepInstruction)
template <bool UseSource, bool DeviceInt>
bool Z80::ProcessInterruptsImpl(bool int_occurred, unsigned int_start, unsigned int_end)
{
    Z80& cpu = *this;
    bool intHandled = false;

    // A pending prefix is the middle of an instruction: neither INT nor NMI
    // is accepted until the instruction it introduces has run
    const bool prefixPending =
        cpu.boundary == Z80_BOUNDARY_PREFIX_DD || cpu.boundary == Z80_BOUNDARY_PREFIX_FD;

    // NMI processing (accepted at the instruction boundary, priority over INT).
    // Requested via RequestNonMaskedInterrupt(); on Scorpion models the MNI
    // "magic button" orchestration (Emulator::RequestMNI) pages the Shadow
    // Monitor BEFORE requesting, so only the architecture-defined CPU dance
    // lives here. The model-specific block from the original UnrealSpeccy
    // (ATM3 bank switching / Scorpion pc>0x4000 guard) moved to that layer -
    // the MNI latch already owns the ROM selection.
    // The request stays pending while it is refused: inside an instruction
    // (pending prefix), and right after an NMI acknowledge - the chip takes
    // no second NMI response without an instruction between (Sainz de
    // Baranda 2022, Visual Z80). The EI shadow does not block NMI.
    // A board NMI that waits for the frame INT (ZX-Evo znmi.v: pending_nmi is
    // released at int_start) reaches the /NMI pin at the first boundary inside
    // the INT pulse - also while halted, since this runs at every boundary.
    // The board may still veto it (ZX-Evo: no new NMI while its NMI page is in)
    EmulatorState& machineState = _context->emulatorState;
    if (machineState.nmiAtIntStartPending)
    {
        const bool inPulse = _intWraps ? (cpu.t > int_start || cpu.t < int_end)
                                       : (cpu.t > int_start && cpu.t < int_end);
        if (inPulse)
        {
            machineState.nmiAtIntStartPending = false;
            if (_context->pPortDecoder == nullptr || _context->pPortDecoder->OnFrameIntStartNmi())
                _nmi_pending_count = 1;
        }
    }

    if (_nmi_pending_count > 0 && !prefixPending && cpu.boundary != Z80_BOUNDARY_NMI_ACK)
    {
        _nmi_pending_count = 0;

        // CPU-LIBRARY-MIGRATION(nmi-ack): the engine's NMI acknowledge. A machine on its own engine
        // (ICpuEngine) already takes it there: the restart fetch, the pushes, PC = #0066, IFF1 = 0
        if (_engine) [[unlikely]]
        {
            _engine->AcknowledgeNmi();
            cpu.int_pending = false;
            return true;
        }
        cpu.nmi_in_progress = true;

        // If CPU halted - unblock it by moving PC forward (return lands past
        // the HALT). Keyed on the HALT latch, not on the byte at PC: an NMI
        // at the boundary before a not-yet-executed HALT must return to it
        if (cpu.halted)
            cpu.pc++;

        // The acknowledge M1 is a refresh cycle like any M1: R advances
        cpu.r_low = ((cpu.r_low + 1) & 0x7f) | (cpu.r_low & 0x80);
        NoteAcknowledgeRefresh(cpu.t + 2);  // the opcode-fetch M1 of the restart: T3 is the third tick

        // NMI timing per Z80 manual: 11T (M1=5T restart fetch, M2=3T push PCH, M3=3T push PCL).
        // The accept IS the cycle for this iteration: ProcessInterrupts returns true and
        // the caller skips Z80Step (same contract as the INT acceptance below).
        IncrementCPUCyclesCounter(11);

        // Push return address (raw write: both stack cycles are included in the 11T above)
        uint16_t sp = cpu.sp;
        (_memory->*MemIf->MemoryWrite)(--sp, cpu.pch);
        (_memory->*MemIf->MemoryWrite)(--sp, cpu.pcl);
        cpu.sp = sp;

        // Restart at the NMI vector #0066
        cpu.pc = 0x0066;
        cpu.memptr = 0x0066;
        cpu.halted = 0;

        // Maskable interrupts disabled in the handler; IFF2 is left alone and
        // keeps the pre-NMI state for RETN (UM0080 table 1 "Accept NMI:
        // IFF1 0, IFF2 unchanged"; Sean Young's nested-NMI hardware test) -
        // copying IFF1 into it lost the outer state on a nested NMI
        cpu.iff1 = 0;
        cpu.int_pending = false;
        cpu.boundary = Z80_BOUNDARY_NMI_ACK;

        // A board that owns this NMI may drive #00 (NOP) onto the bus for the
        // #0066 fetch and page its handler RAM in at that fetch's refresh
        // (ZX-Evo znmi.v drive_00 / in_nmi): the CPU spends one M1 on the forced
        // NOP and continues at #0067 from the board's page
        if (_context->pPortDecoder != nullptr && _context->pPortDecoder->OnNmiAccepted())
        {
            cpu.r_low = ((cpu.r_low + 1) & 0x7f) | (cpu.r_low & 0x80);
            IncrementCPUCyclesCounter(4);
            cpu.pc = 0x0067;
        }

        return true;  // NMI accepted: skip Z80Step this iteration
    }

    // A machine that owns its INT logic decides the pin alone (IInterruptSource)
    if constexpr (UseSource)
    {
        const bool sourceInt = _interruptSource->IsIntAsserted(cpu.t);
        cpu.int_pending = sourceInt;
        if constexpr (DeviceInt)
            cpu.int_pending = sourceInt || _deviceIntLines != 0;
        if (cpu.int_pending && cpu.iff1 && cpu.boundary != Z80_BOUNDARY_INT_SHADOW && !prefixPending)
        {
            // Wired-OR: the machine's logic drives its vector when it asserts;
            // a device alone drives none, the bus reads #FF
            uint8_t vector = sourceInt ? _interruptSource->AcknowledgeInterrupt(cpu.t) : 0xFF;
            // TTD (Phase 3): every vector the CPU took, recorded (a check and a
            // search key); a replay from the engine hands back the recorded one
            if (ttd::TTDPortJournal* journal = _context->ttdVectors) [[unlikely]]
            {
                const EmulatorState& st = _context->emulatorState;
                vector = journal->OnRead(kTtdVectorPort, vector, st.frame_counter, st.TtdTInFrame(cpu.t), cpu.pc);
            }
            HandleINT(vector);
            return true;
        }
        return false;
    }

    // Generate INT
    // TODO: move INT forming logic to Screen class since in reality it's formed by ULA / frame counters
    // Strict sampling (cpu.t > int_start): the ULA registers the INT signal one clock
    // after the raster compare (MiSTer ula.sv: INT <= 1 on the next edge) and the CPU
    // samples INT only at end-of-instruction edges - an instruction boundary landing
    // exactly at int_start still sees INT inactive. Inclusive ">=" accepts 1T early,
    // which shifts interrupt-locked raster effects by one T-state.
    // Note: HALT quantizes INT detection to 4T boundaries; fine 2-pixel adjustments
    // are handled in ScreenZX::SetBorderColor. See: docs/timing/pentagon-border-timing.md
    // A pulse the CPU already acknowledged stays down on machines whose INT
    // is cleared by the acknowledge (see HandleINT); once the pulse window is
    // over the flag re-arms for the next one
    if (cpu.int_acked_in_pulse)
    {
        const bool inPulse = _intWraps ? (cpu.t > int_start || cpu.t < int_end)
                                       : (cpu.t > int_start && cpu.t < int_end);
        if (!inPulse)
            cpu.int_acked_in_pulse = 0;
    }

    if (!int_occurred && cpu.t > int_start && !cpu.int_acked_in_pulse && !frameIntMasked)
    {
        int_occurred = true;
        cpu.int_pending = true;
    }

    if (cpu.int_pending && (cpu.t >= int_end))
        cpu.int_pending = false;

    // Board-level local INT (RaiseLocalInt): held for its own length
    if (_localIntArmed) [[unlikely]]
    {
        const uint64_t frame = machineState.frame_counter;
        if (frame < _localIntEndFrame || (frame == _localIntEndFrame && cpu.t < _localIntEndT))
            cpu.int_pending = true;
        else
            _localIntArmed = false;
    }

    /// region <INT (Non-masked interrupt)>

    // If INT signal raised and IFF1 flag is set allowing interrupts handling (set by EI command)
    // Important! Interrupts are in fact enabled only after command executed after EI (delay to 1 command)
    // See: https://floooh.github.io/2021/12/06/z80-instruction-timing.html
    // See: https://www.msx.org/forum/development/msx-development/question-about-z80r800-irqs-and-eidi-behaviour
    // Device INT lines (SetDeviceIntLine) join the pin as a level: not stored
    // in int_pending, which tracks the machine's own pulse
    bool intLine = cpu.int_pending;
    if constexpr (DeviceInt)
        intLine = intLine || _deviceIntLines != 0;

    if (intLine && cpu.iff1 && cpu.boundary != Z80_BOUNDARY_INT_SHADOW && !prefixPending)
    {
        HandleINT();
        intHandled = true;  // Signal caller to skip Z80Step this iteration
    }

    /// endregion </INT (Non-masked interrupt)>

    return intHandled;
}

void Z80::HandleNMI(ROMModeEnum mode)
{
    (void)mode;

    [[maybe_unused]] Z80& cpu = *this;
}

/// Machines whose INT pulse ends at the acknowledge (IORQ with M1 low)
/// instead of lasting its full length. ZX-Evo (ATM3 model): the baseconf
/// zint.v generator ends int_n on the counter OR at the acknowledge. A plain
/// Pentagon/ULA pulse is fixed-length: an EI;RET handler shorter than the
/// pulse is taken twice there, on the real machine too.
bool Z80::IntClearedByAcknowledge() const
{
    return _context->config.mem_model == MM_ATM3;
}

// CPU-LIBRARY-MIGRATION(int-ack): the engine's INT acknowledge, its bus cycles through the callbacks
void Z80::HandleINT(uint8_t vector)
{
    Z80& cpu = *this;

    // A machine on its own engine: the acknowledge cycle, the pushes and the vector read are the engine's
    // (ICpuEngine); one test per accepted INT, not per instruction
    if (_engine) [[unlikely]]
    {
        if (HostBusOverlay* overlay = _memory->GetBusOverlay())
            overlay->onInterruptAcknowledge();
        _engine->AcknowledgeInterrupt(vector);
        cpu.int_pending = false;
        if (IntClearedByAcknowledge())
            cpu.int_acked_in_pulse = 1;
        return;
    }

    /// region <CPU is stopped on HALT (opcode 0x76) command>

    // If CPU halted - unblock it by moving PC forward. Keyed on the HALT
    // latch, not on the byte at PC: an INT at the boundary before a
    // not-yet-executed HALT must return to the HALT, not skip it
    if (cpu.halted)
        cpu.pc++;

    /// endregion </CPU is stopped on HALT (opcode 0x76) command>

    // CPU-LIBRARY-MIGRATION(cmos-variant): the quirk becomes an engine variant setting (the Z84C15 has none)
    // NMOS quirk: LD A,I / LD A,R copy IFF2 into P/V late in the instruction,
    // and an INT accepted at the very next boundary clears IFF2 before that
    // copy settles - P/V reads 0 (Zilog Z80 Family Q&A, Data Book 1989
    // pp. 412-413; FUSE, redcode Z80, z80ex, openMSX). NMI does not do this
    if (cpu.boundary == Z80_BOUNDARY_LD_A_IR)
        cpu.f &= ~PV;
    cpu.boundary = Z80_BOUNDARY_NONE;

    // The acknowledge M1 is a refresh cycle like any M1: R advances
    cpu.r_low = ((cpu.r_low + 1) & 0x7f) | (cpu.r_low & 0x80);
    NoteAcknowledgeRefresh(cpu.t + 4);  // T1 T2 Tw Tw T3 T4: the refresh's T3 is the fifth tick
    // The acknowledge is an I/O cycle: bus overlays that track bus cycles see it (a ZX-Evo's cache)
    if (HostBusOverlay* overlay = _memory->GetBusOverlay()) [[unlikely]]
        overlay->onInterruptAcknowledge();

    /// region <Calculate INT duration>

    // INT timing per Z80 manual:
    // IM0/IM1: 13T total (M1=7T for INT ack, M2=3T push PCH, M3=3T push PCL)
    // IM2: 19T total (M1=7T INT ack, M2=3T push PCH, M3=3T push PCL, M4=3T read VL, M5=3T read VH)
    // Note: Since ProcessInterrupts() returns true and Z80Step() is skipped,
    // we add the full INT duration here (no M1 subtraction needed).
    int interruptDuration = 0;

    switch (cpu.im)
    {
        case 0:
        case 1:
            interruptDuration = 13;  // Full IM0/IM1 timing
            break;
        case 2:
            interruptDuration = 19;  // Full IM2 timing
            break;
        default:
            throw std::logic_error("Unknown interrupt mode detected");
            break;
    }

    IncrementCPUCyclesCounter(interruptDuration);

    /// endregion </Calculate INT duration>

    // Push return address to stack
    // Raw memory access without T-state accounting: both stack write cycles
    // are already included in interruptDuration above (wd() would add +3T per byte).
    // Same approach as the original Unreal Speccy handle_int (MemIf->wm() without t increment)
    uint16_t sp = cpu.sp;
    (_memory->*MemIf->MemoryWrite)(--sp, cpu.pch);
    (_memory->*MemIf->MemoryWrite)(--sp, cpu.pcl);
    cpu.sp = sp;

    /// region <Determine interrupt handler address>
    uint16_t interruptHandlerAddress;
    if (cpu.im < 2)
    {
        // IM0, IM1
        interruptHandlerAddress = 0x38;
    }
    else
    {
        // IM2, in machine-cycle order: the PC push (M2/M3, above) comes
        // before the vector-table read (M4/M5), so a stack that overlaps the
        // table supplies the freshly pushed bytes, as on the chip (FUSE,
        // MAME, z80ex, redcode Z80).
        // Raw memory access without T-state accounting: the vector fetch time
        // is already included in interruptDuration above (rd() would add +3T per byte)
        uint16_t vectorAddress = vector + cpu.i * 0x100;
        interruptHandlerAddress = (_memory->*MemIf->MemoryRead)(vectorAddress, false) +
                                  0x100 * (_memory->*MemIf->MemoryRead)(vectorAddress + 1, false);
    }
    /// endregion </Determine interrupt handler address>

    // Jump to interrupt handler
    cpu.pc = interruptHandlerAddress;
    cpu.memptr = interruptHandlerAddress;
    cpu.halted = 0;

    // Block potential interrupt double handling
    cpu.iff1 = 0;
    cpu.iff2 = 0;
    cpu.int_pending = false;

    // Where the acknowledge ends the pulse, INT stays down for the rest of
    // this pulse window (ProcessInterrupts re-arms after it)
    if (IntClearedByAcknowledge())
        cpu.int_acked_in_pulse = 1;
}

void Z80::OnCPUStep()
{
    // Q register update is now handled in Z80Step() based on flag changes

    // MainLoop will dispatch the call to all peripherals
    _context->pMainLoop->OnCPUStep();
}

//
// Increment CPU cycles counter by specified number of cycles.
// Required to keep exact timings for Z80 commands
// Note: same as '#define cputact(a) cpu->tt += ((a) * cpu->rate)' macro defined in cpulogic.h
//
// CPU-LIBRARY-MIGRATION(t-model): the engine counts plain T; the host maps frame T to it (Z84C15Engine)
void Z80::IncrementCPUCyclesCounter(uint8_t cycles)
{
    tt += cycles * rate;
}

/// region <Debug methods>
#include <cstdio>

void Z80::DumpCurrentState()
{
    static char dumpBuffer[512];

    int pos = 0;
    pos += snprintf(dumpBuffer + pos, sizeof dumpBuffer, "t:%d\r\n", t);
    pos += snprintf(dumpBuffer + pos, sizeof dumpBuffer, "Op:%02X    IR:%04X\r\n", opcode, ir_);
    pos += snprintf(dumpBuffer + pos, sizeof dumpBuffer, "PC:%04X  SP:%04X\r\n", pc, sp);
    pos += snprintf(dumpBuffer + pos, sizeof dumpBuffer, "AF:%04X 'AF:%04X\r\n", af, alt.af);
    pos += snprintf(dumpBuffer + pos, sizeof dumpBuffer, "BC:%04X 'BC:%04X\r\n", bc, alt.bc);
    pos += snprintf(dumpBuffer + pos, sizeof dumpBuffer, "DE:%04X 'DE:%04X\r\n", de, alt.de);
    pos += snprintf(dumpBuffer + pos, sizeof dumpBuffer, "HL:%04X 'HL:%04X\r\n", hl, alt.hl);
    pos += snprintf(dumpBuffer + pos, sizeof dumpBuffer, "IX:%04X  IY:%04X\r\n", ix, iy);
    pos += snprintf(dumpBuffer + pos, sizeof dumpBuffer, "\r\n");

#ifdef _WIN32
#ifdef _UNICODE
    wstring message = StringHelper::StringToWideString(dumpBuffer);
    OutputDebugString(message.c_str());
#else
    string message = dumpBuffer;
    OutputDebugString(message.c_str());
#endif
#endif
}

std::string Z80::DumpZ80State()
{
    static char buffer[512];

    DumpZ80State(buffer, sizeof(buffer));

    std::string result(buffer);

    return result;
}

void Z80::DumpZ80State(char* buffer, size_t len)
{
    std::string annotation;

    // If we were executing in memory and jumped to ROM - we need to highlight what ROM page was used
    if (prev_pc >= 0x4000 && m1_pc < 0x4000)
    {
        annotation = StringHelper::Format(" <-- ROM%d", _context->pMemory->GetROMPage());
    }

    // If we were executing in ROM or fixed RAM pages and jumped to RAM Bank 3 - we need to highlight what RAM page was
    // used
    if (prev_pc < 0xC000 && m1_pc >= 0xC000)
    {
        annotation = StringHelper::Format(" <-- RAM%d", _context->pMemory->GetRAMPageForBank3());
    }
    else if (prev_pc < 0x4000 && m1_pc >= 0x4000)
    {
        annotation = StringHelper::Format(" <-- RAM%d", _context->pMemory->GetRAMPageFromAddress(
                                                            _context->pMemory->MapZ80AddressToPhysicalAddress(m1_pc)));
    }

    if (prefix > 0)
    {
        snprintf(buffer, len,
                 "Pr: 0x%04X Op: 0x%02X PC: 0x%04X AF: 0x%04X BC: 0x%04X DE: 0x%04X HL: 0x%04X IX: %04X IY: %04X SP: "
                 "%04X IR: %04X clock: %04X%s",
                 prefix, opcode, m1_pc, af, bc, de, hl, ix, iy, sp, ir_, t, annotation.c_str());
    }
    else
    {
        snprintf(buffer, len,
                 "           Op: 0x%02X PC: 0x%04X AF: 0x%04X BC: 0x%04X DE: 0x%04X HL: 0x%04X IX: %04X IY: %04X SP: "
                 "%04X IR: %04X clock: %04X%s",
                 opcode, m1_pc, af, bc, de, hl, ix, iy, sp, ir_, t, annotation.c_str());
    }
}

std::string Z80::DumpCurrentFlags()
{
    return DumpFlags(Z80Registers::f);
}

std::string Z80::DumpFlags(uint8_t flags)
{
    const char flagNames[8] = {
        'C',  // Carry
        'N',  // Subtract
        'P',  // P/V - parity / overflow
        '3',  // Undocumented F3
        'H',  // Half-carry
        '5',  // Undocumented F5
        'Z',  // Zero
        'S'   // Sign
    };

    std::string result;
    std::stringstream ss;

    for (int i = 7; i >= 0; i--)
    {
        bool flagSet = flags & (1 << i);

        if (flagSet)
        {
            ss << flagNames[i];
        }
        else
        {
            ss << "_";
        }
    }

    result = ss.str();

    return result;
}

/// endregion </Debug methods>

/// region <Feature Cache>

void Z80::UpdateFeatureCache()
{
    if (_context && _context->pFeatureManager)
    {
        _feature_opcodeprofiler_enabled = _context->pFeatureManager->isEnabled(Features::kOpcodeProfiler);
        _feature_calltrace_enabled = _context->pFeatureManager->isEnabled(Features::kCallTrace);
    }
}

/// endregion </Feature Cache>

/// region <Register Access API>

// Static register metadata table
static const Z80::RegisterInfo s_registers[] = {
    // 8-bit main registers
    {"A", false, false, [](const Z80State* s) -> uint16_t { return s->a; }, [](Z80State* s, uint16_t v) { s->a = static_cast<uint8_t>(v); }},
    {"B", false, false, [](const Z80State* s) -> uint16_t { return s->b; }, [](Z80State* s, uint16_t v) { s->b = static_cast<uint8_t>(v); }},
    {"C", false, false, [](const Z80State* s) -> uint16_t { return s->c; }, [](Z80State* s, uint16_t v) { s->c = static_cast<uint8_t>(v); }},
    {"D", false, false, [](const Z80State* s) -> uint16_t { return s->d; }, [](Z80State* s, uint16_t v) { s->d = static_cast<uint8_t>(v); }},
    {"E", false, false, [](const Z80State* s) -> uint16_t { return s->e; }, [](Z80State* s, uint16_t v) { s->e = static_cast<uint8_t>(v); }},
    {"H", false, false, [](const Z80State* s) -> uint16_t { return s->h; }, [](Z80State* s, uint16_t v) { s->h = static_cast<uint8_t>(v); }},
    {"L", false, false, [](const Z80State* s) -> uint16_t { return s->l; }, [](Z80State* s, uint16_t v) { s->l = static_cast<uint8_t>(v); }},
    {"F", false, false, [](const Z80State* s) -> uint16_t { return s->f; }, [](Z80State* s, uint16_t v) { s->f = static_cast<uint8_t>(v); }},
    {"I", false, false, [](const Z80State* s) -> uint16_t { return s->i; }, [](Z80State* s, uint16_t v) { s->i = static_cast<uint8_t>(v); }},
    // R: bit 7 is kept apart (r_hi) from the counting bits 6:0 (LD R,A / LD A,R)
    {"R", false, false, [](const Z80State* s) -> uint16_t { return Z80::RegisterR(s); },
     [](Z80State* s, uint16_t v) { s->r_low = static_cast<uint8_t>(v); s->r_hi = static_cast<uint8_t>(v & 0x80); }},
    // 8-bit alternate registers
    {"A'", false, true, [](const Z80State* s) -> uint16_t { return s->alt.a; }, [](Z80State* s, uint16_t v) { s->alt.a = static_cast<uint8_t>(v); }},
    {"B'", false, true, [](const Z80State* s) -> uint16_t { return s->alt.b; }, [](Z80State* s, uint16_t v) { s->alt.b = static_cast<uint8_t>(v); }},
    {"C'", false, true, [](const Z80State* s) -> uint16_t { return s->alt.c; }, [](Z80State* s, uint16_t v) { s->alt.c = static_cast<uint8_t>(v); }},
    {"D'", false, true, [](const Z80State* s) -> uint16_t { return s->alt.d; }, [](Z80State* s, uint16_t v) { s->alt.d = static_cast<uint8_t>(v); }},
    {"E'", false, true, [](const Z80State* s) -> uint16_t { return s->alt.e; }, [](Z80State* s, uint16_t v) { s->alt.e = static_cast<uint8_t>(v); }},
    {"H'", false, true, [](const Z80State* s) -> uint16_t { return s->alt.h; }, [](Z80State* s, uint16_t v) { s->alt.h = static_cast<uint8_t>(v); }},
    {"L'", false, true, [](const Z80State* s) -> uint16_t { return s->alt.l; }, [](Z80State* s, uint16_t v) { s->alt.l = static_cast<uint8_t>(v); }},
    {"F'", false, true, [](const Z80State* s) -> uint16_t { return s->alt.f; }, [](Z80State* s, uint16_t v) { s->alt.f = static_cast<uint8_t>(v); }},
    // 8-bit index register halves
    {"IXH", false, false, [](const Z80State* s) -> uint16_t { return s->xh; }, [](Z80State* s, uint16_t v) { s->xh = static_cast<uint8_t>(v); }},
    {"IXL", false, false, [](const Z80State* s) -> uint16_t { return s->xl; }, [](Z80State* s, uint16_t v) { s->xl = static_cast<uint8_t>(v); }},
    {"IYH", false, false, [](const Z80State* s) -> uint16_t { return s->yh; }, [](Z80State* s, uint16_t v) { s->yh = static_cast<uint8_t>(v); }},
    {"IYL", false, false, [](const Z80State* s) -> uint16_t { return s->yl; }, [](Z80State* s, uint16_t v) { s->yl = static_cast<uint8_t>(v); }},
    // 16-bit main registers
    {"AF", true, false, [](const Z80State* s) -> uint16_t { return s->af; }, [](Z80State* s, uint16_t v) { s->af = v; }},
    {"BC", true, false, [](const Z80State* s) -> uint16_t { return s->bc; }, [](Z80State* s, uint16_t v) { s->bc = v; }},
    {"DE", true, false, [](const Z80State* s) -> uint16_t { return s->de; }, [](Z80State* s, uint16_t v) { s->de = v; }},
    {"HL", true, false, [](const Z80State* s) -> uint16_t { return s->hl; }, [](Z80State* s, uint16_t v) { s->hl = v; }},
    {"IX", true, false, [](const Z80State* s) -> uint16_t { return s->ix; }, [](Z80State* s, uint16_t v) { s->ix = v; }},
    {"IY", true, false, [](const Z80State* s) -> uint16_t { return s->iy; }, [](Z80State* s, uint16_t v) { s->iy = v; }},
    {"SP", true, false, [](const Z80State* s) -> uint16_t { return s->sp; }, [](Z80State* s, uint16_t v) { s->sp = v; }},
    {"PC", true, false, [](const Z80State* s) -> uint16_t { return s->pc; }, [](Z80State* s, uint16_t v) { s->pc = v; }},
    {"IR", true, false, [](const Z80State* s) -> uint16_t { return static_cast<uint16_t>((s->i << 8) | Z80::RegisterR(s)); },
     [](Z80State* s, uint16_t v) { s->i = static_cast<uint8_t>(v >> 8); s->r_low = static_cast<uint8_t>(v); s->r_hi = static_cast<uint8_t>(v & 0x80); }},
    // Internal: MEMPTR (WZ), the address latch behind the undocumented flags of BIT n,(HL)
    {"MEMPTR", true, false, [](const Z80State* s) -> uint16_t { return s->memptr; }, [](Z80State* s, uint16_t v) { s->memptr = v; }},
    // Interrupt state: mode 0-2 and the two enable flip-flops
    {"IM", false, false, [](const Z80State* s) -> uint16_t { return s->im; }, [](Z80State* s, uint16_t v) { s->im = static_cast<uint8_t>(v); }, 2},
    {"IFF1", false, false, [](const Z80State* s) -> uint16_t { return s->iff1 ? 1 : 0; }, [](Z80State* s, uint16_t v) { s->iff1 = static_cast<uint8_t>(v); }, 1},
    {"IFF2", false, false, [](const Z80State* s) -> uint16_t { return s->iff2 ? 1 : 0; }, [](Z80State* s, uint16_t v) { s->iff2 = static_cast<uint8_t>(v); }, 1},
    // 16-bit alternate registers
    {"AF'", true, true, [](const Z80State* s) -> uint16_t { return s->alt.af; }, [](Z80State* s, uint16_t v) { s->alt.af = v; }},
    {"BC'", true, true, [](const Z80State* s) -> uint16_t { return s->alt.bc; }, [](Z80State* s, uint16_t v) { s->alt.bc = v; }},
    {"DE'", true, true, [](const Z80State* s) -> uint16_t { return s->alt.de; }, [](Z80State* s, uint16_t v) { s->alt.de = v; }},
    {"HL'", true, true, [](const Z80State* s) -> uint16_t { return s->alt.hl; }, [](Z80State* s, uint16_t v) { s->alt.hl = v; }},
};

static constexpr size_t s_registerCount = sizeof(s_registers) / sizeof(s_registers[0]);

const Z80::RegisterInfo* Z80::GetRegisterInfo()
{
    return s_registers;
}

size_t Z80::GetRegisterCount()
{
    return s_registerCount;
}

const Z80::RegisterInfo* Z80::FindRegister(const std::string& name)
{
    std::string normalized = name;
    std::transform(normalized.begin(), normalized.end(), normalized.begin(), ::toupper);

    for (size_t i = 0; i < s_registerCount; i++)
    {
        if (normalized == s_registers[i].name)
            return &s_registers[i];
    }

    // Handle aliases
    if (normalized == "XH") return FindRegister("IXH");
    if (normalized == "XL") return FindRegister("IXL");
    if (normalized == "YH") return FindRegister("IYH");
    if (normalized == "YL") return FindRegister("IYL");
    if (normalized == "WZ") return FindRegister("MEMPTR");

    return nullptr;
}

bool Z80::GetRegisterValue(Z80State* state, const std::string& name, uint16_t& value, bool& is16bit)
{
    const RegisterInfo* info = FindRegister(name);
    if (!info) return false;

    value = info->getter(state);
    is16bit = info->is16bit;
    return true;
}

bool Z80::SetRegisterValue(Z80State* state, const std::string& name, uint16_t value)
{
    const RegisterInfo* info = FindRegister(name);
    if (!info) return false;
    if (info->maxValue && value > info->maxValue) return false;  // IM 3 is no mode; 8-bit registers truncate

    info->setter(state, value);
    return true;
}

const char* Z80::BoundaryName(uint8_t boundary)
{
    switch (boundary)
    {
        case Z80_BOUNDARY_NONE: return "none";
        case Z80_BOUNDARY_PREFIX_DD: return "prefix_dd";
        case Z80_BOUNDARY_PREFIX_FD: return "prefix_fd";
        case Z80_BOUNDARY_INT_SHADOW: return "int_shadow";
        case Z80_BOUNDARY_LD_A_IR: return "ld_a_ir";
        case Z80_BOUNDARY_NMI_ACK: return "nmi_ack";
        default: return "unknown";
    }
}

uint8_t Z80::RegisterR(const Z80Registers* state)
{
    return static_cast<uint8_t>((state->r_low & 0x7F) | (state->r_hi & 0x80));
}

/// endregion </Register Access API>