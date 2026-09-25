#include "z80.h"

#include "3rdparty/message-center/messagecenter.h"
#include "3rdparty/unreal-z80/include/z80cpu.h"
#include "common/modulelogger.h"
#include "common/stringhelper.h"
#include "common/timehelper.h"
#include "debugger/analyzers/analyzermanager.h"
#include "debugger/breakpoints/breakpointmanager.h"
#include "debugger/debugmanager.h"
#include "debugger/ttd/timetravelmanager.h"
#include "emulator/cpu/opcode_profiler.h"
#include "emulator/emulator.h"
#include "emulator/io/fdc/diskautostart.h"
#include "emulator/io/fdc/diskfastload.h"
#include "emulator/io/tape/tapefastload.h"
#include "emulator/memory/memoryaccesstracker.h"
#include "emulator/notifications.h"
#include "emulator/platform.h"
#include "emulator/ports/portdecoder.h"
#include "emulator/spectrumconstants.h"
#include "emulator/video/screen.h"
#include "emulator/video/ulacontention.h"
#include "stdafx.h"

/// region <Execution engine bridge>

/// Callback trampolines between the vendored unreal-z80 engine (opaque C API,
/// 3rdparty/unreal-z80) and the Z80 instance that owns it (userData).
///
/// The engine executes opcodes; everything around it stays host logic and is
/// reached through these callbacks with the same observable behavior the
/// in-class interpreter had:
///  - memory and ports go through MemIf / the port decoder / floating bus /
///    busTraceHook (the *NoContention helpers shared with rd/wd/in/out);
///  - ULA contention is returned from the engine's wait-state hook at the
///    first T of each cycle - the same T the interpreter evaluated it at. The
///    hook is installed only while the model has contention enabled
///    (UpdateEngineContention), so uncontended models pay nothing for it;
///  - the opcode-fetch bookkeeping (m1_pc, m1TraceHook, TTD coverage/probe)
///    and the decoded prefix/opcode fields follow the interpreter's rules.
///
/// Opcode fetches are recognized from the instruction byte stream itself:
/// the engine flags every instruction-stream read (m1State != 0 - opcode,
/// prefix, operand and displacement bytes alike, see z80cpu.h), and the
/// prefix structure decides which of those bytes are opcode (M1) fetches -
/// the first byte, the byte after a CB/ED/DD/FD prefix, and the byte after
/// the displacement of DD CB d / FD CB d. So this is known before the read,
/// and the bookkeeping runs ahead of it exactly as in m1_cycle().
///
/// Timing: the engine counts plain T-states, the host counts tt (t << 8 plus
/// a fraction, advanced by cycles * rate). Before any host code runs inside
/// an instruction the host tt is set to the equivalent of the engine's
/// current T, so ULA/screen/sound/port code observes exactly the T it did
/// before. A tt change made by host code inside a callback (hardware turbo
/// rescaling from a port write) is carried over into the rest of the
/// instruction and its end time.
struct Z80EngineBridge
{
    static void Wire(Z80CPU* engine, Z80* cpu)
    {
        Z80CpuSetMemoryBus(engine, &MemRead, cpu, &MemWrite, cpu);
        Z80CpuSetPortBus(engine, &PortIn, cpu, &PortOut, cpu);
        Z80CpuSetRetnFn(engine, &Retn, cpu);

        // Wait-state hook: see Z80::UpdateEngineContention (installed on demand).
        // INT and NMI acceptance stay host-driven (ProcessInterrupts /
        // HandleINT), so the engine's IM2 vector hook is not used. RETI has no
        // host consumer (no Z80 PIO/CTC daisy chain is emulated) - unwired.
    }

    static void SetContention(Z80CPU* engine, Z80* cpu, bool enabled)
    {
        Z80CpuSetContendFn(engine, enabled ? &Contend : nullptr, cpu);
    }

    static uint8_t MemRead(Z80CPU* engine, uint16_t addr, int m1State, void* userData)
    {
        Z80& cpu = *static_cast<Z80*>(userData);
        const uint32_t engineT = Z80CpuTstates(engine);

        // Opcode (M1) fetch: decided by the instruction structure, before the
        // read (see the region comment)
        const bool isOpcodeFetch = m1State != 0 && cpu._engineExpectOpcode;
        if (isOpcodeFetch)
        {
            // Continuation fetches (CB opcode byte, DD/FD chain) are
            // instruction-start fetches for the bookkeeping while the
            // interpreter's prefix was still 0; it ran at the start of the M1
            // cycle, 3 T before the data transfer (instruction-stream reads are
            // never contended). The first fetch was already handled by Z80Step
            if (cpu._engineM1Count > 0 && cpu.prefix == 0x0000)
            {
                cpu.PublishEngineTime(engineT - 3);
                const uint32_t published = cpu.tt;
                cpu.OnInstructionFetch(addr);
                cpu.AbsorbHostTimeChange(published);
            }
        }

        cpu.PublishEngineTime(engineT);
        const uint32_t published = cpu.tt;

        // m1State is set for the opcode fetch and for every instruction-stream
        // byte (operands, displacements) - exactly the reads the interpreter
        // issued as rd(addr, isExecution = true)
        const uint8_t value = cpu.MemoryReadNoContention(addr, m1State != 0);

        if (isOpcodeFetch)
            OnOpcodeByte(cpu, value);
        else if (m1State != 0 && cpu._engineIndexCbDisplacement)
        {
            // DD CB d / FD CB d: the displacement is followed by the opcode byte
            cpu._engineIndexCbDisplacement = false;
            cpu._engineExpectOpcode = true;
        }

        cpu.AbsorbHostTimeChange(published);
        return value;
    }

    static void MemWrite(Z80CPU* engine, uint16_t addr, uint8_t value, void* userData)
    {
        Z80& cpu = *static_cast<Z80*>(userData);
        cpu.PublishEngineTime(Z80CpuTstates(engine));
        const uint32_t published = cpu.tt;

        cpu.MemoryWriteNoContention(addr, value);

        cpu.AbsorbHostTimeChange(published);
    }

    static uint8_t PortIn(Z80CPU* engine, uint16_t port, void* userData)
    {
        Z80& cpu = *static_cast<Z80*>(userData);
        cpu.PublishEngineTime(Z80CpuTstates(engine));
        const uint32_t published = cpu.tt;

        // Port handlers may inspect Z80::pc: expose the mid-instruction value
        // the interpreter had there (past the instruction's fetched bytes)
        cpu.pc = Z80CpuGetReg(engine, Z80CpuRegPc);

        const uint8_t value = cpu.PortInNoContention(port);

        cpu.AbsorbHostTimeChange(published);
        return value;
    }

    static void PortOut(Z80CPU* engine, uint16_t port, uint8_t value, void* userData)
    {
        Z80& cpu = *static_cast<Z80*>(userData);
        cpu.PublishEngineTime(Z80CpuTstates(engine));
        const uint32_t published = cpu.tt;

        cpu.pc = Z80CpuGetReg(engine, Z80CpuRegPc);

        cpu.PortOutNoContention(port, value);

        cpu.AbsorbHostTimeChange(published);
    }

    /// Wait-state hook (ULA contention): called at the first T of every bus
    /// cycle while installed
    static int Contend(Z80CPU* engine, uint16_t addr, Z80CpuAccessKind kind, void* userData)
    {
        Z80& cpu = *static_cast<Z80*>(userData);
        UlaContention* ula = cpu._context->pUlaContention;
        if (ula == nullptr)
            return 0;

        switch (kind)
        {
            case Z80CpuAccessRead:
            case Z80CpuAccessWrite:
                if (!ula->IsAddressContended(addr))
                    return 0;

                // Same T as rd()/wd(): the cycle start, before the access
                cpu.PublishEngineTime(Z80CpuTstates(engine));
                return ula->GetContentionDelay();

            case Z80CpuAccessPortIn:
            case Z80CpuAccessPortOut:
                // in()/out() evaluated IO contention at the IORQ T-state, one T
                // into the IO cycle
                cpu.PublishEngineTime(Z80CpuTstates(engine) + 1);
                return ula->GetIOContentionDelay(addr);

            default:
                // Instruction-stream reads (M1, operands) were never contended
                // by rd(addr, true); the post-IORQ extension is not modeled by
                // UlaContention
                return 0;
        }
    }

    /// RETN ends the NMI service session
    static void Retn(Z80CPU*, void* userData)
    {
        static_cast<Z80*>(userData)->retn();
    }

    /// Decoded-operation fields, the "prefix" state that gates the opcode
    /// fetch bookkeeping, and whether the next instruction-stream byte is an
    /// opcode - reproduced from the interpreter's rules:
    ///  - XX         -> prefix 0,      opcode XX
    ///  - CB XX      -> prefix 0xCB,   opcode XX (the XX fetch still counted
    ///                  as an instruction-start fetch: prefix was set after it)
    ///  - ED XX      -> prefix 0xED,   opcode XX (prefix set before the fetch)
    ///  - DD/FD.. XX -> prefix 0xDD/FD (last one), every fetch counted
    ///  - DD/FD CB d XX -> prefix 0xDDCB/0xFDCB, opcode XX (not counted)
    ///  - DD/FD ED XX   -> prefix 0, opcode XX (all fetches counted)
    static void OnOpcodeByte(Z80& cpu, uint8_t value)
    {
        const uint8_t index = cpu._engineM1Count++;
        const bool afterEd = index > 0 && cpu._engineLastM1Byte == 0xED;
        const bool isIndexPrefix = (value | 0x20) == 0xFD;

        cpu.opcode = value;
        cpu._engineLastM1Byte = value;
        cpu._engineExpectOpcode = false;  // Default: this was the final opcode byte

        // LD R,A (ED 4F) is the only instruction that writes R (see
        // ExecuteEngineInstruction for why the host needs to know)
        if (afterEd && value == 0x4F)
            cpu._engineWroteR = true;

        if (index == 0)
        {
            if (value == 0xED)
                cpu.prefix = 0x00ED;
            else if (value == 0xCB)
                cpu._engineCbPending = true;
            else if (isIndexPrefix)
                cpu._engineIndexPrefix = value;
            else
                return;

            cpu._engineExpectOpcode = true;  // A prefix: the opcode byte follows
            return;
        }

        if (cpu._engineCbPending)
        {
            cpu._engineCbPending = false;
            cpu.prefix = 0x00CB;
            return;
        }

        if (cpu._engineIndexPrefix != 0 && !cpu._engineIndexResolved)
        {
            if (isIndexPrefix)
            {
                cpu._engineIndexPrefix = value;
                cpu._engineExpectOpcode = true;
                return;
            }

            cpu._engineIndexResolved = true;
            if (value == 0xCB)
            {
                cpu.prefix = static_cast<uint16_t>(cpu._engineIndexPrefix * 0x100 + 0xCB);
                cpu._engineIndexCbDisplacement = true;  // d, then the opcode byte
            }
            else if (value == 0xED)
            {
                cpu._engineExpectOpcode = true;  // prefix stays 0 (interpreter quirk)
            }
            else
            {
                cpu.prefix = cpu._engineIndexPrefix;
            }
        }
    }
};

/// endregion </Execution engine bridge>

/// region <Constructors / Destructors>

Z80::Z80(EmulatorContext* context) : Z80State{}
{
    _context = context;
    _logger = context->pModuleLogger;
    _memory = context->pMemory;

    // Initialize memory access interfaces
    FastMemIf = Memory::GetFastMemoryInterface();
    DbgMemIf = Memory::GetDebugMemoryInterface();
    MemIf = FastMemIf;  // Use fast memory access interface by default

    // Ensure register memory and unions do not contain garbage
    Z80State::tt = 0;
    t = 0;      // Initialize t-state counter
    eipos = 0;  // Initialize EI command position
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

    // Opcode execution engine: callback bus wired to this instance (see
    // Z80EngineBridge) - never the flat/paged fast paths, so every access
    // keeps flowing through MemIf, the port decoder, ULA contention and the
    // debug/trace hooks exactly as the in-class interpreter did
    _engine = Z80CpuCreate();
    if (_engine == nullptr)
        throw std::bad_alloc();
    Z80EngineBridge::Wire(_engine, this);
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

    if (_opcodeProfiler)
    {
        delete _opcodeProfiler;
        _opcodeProfiler = nullptr;
    }

    if (_engine)
    {
        Z80CpuDestroy(_engine);
        _engine = nullptr;
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
    eipos = 0;      // EI command position
    haltpos = 0;    // HALT position

    // All that takes 3 clock cycles
    IncrementCPUCyclesCounter(3);
}

/// Single CPU command cycle (non-interruptable)
void Z80::Z80Step(bool skipBreakpoints)
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
        // Execution code from RAM address - disables TR-DOS ROM
        uint8_t bank = (cpu.pc >> 14) & 3;
        if (memory.GetMemoryBankMode(bank) == MemoryBankModeEnum::BANK_RAM)
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
                bool isHidden = false;
                auto* bp = brk.GetBreakpointById(breakpointID);
                if (bp && (bp->hidden || bp->note == "StepOver" || bp->note == "StepOut" || bp->group == "TemporaryBreakpoints"))
                {
                    isHidden = true;
                }

                // Pause emulator (single source of truth)
                emulator.Pause();

                // Broadcast notification - breakpoint triggered (instance-tagged)
                MessageCenter& messageCenter = MessageCenter::DefaultMessageCenter();
                BreakpointTriggeredPayload* payload =
                    new BreakpointTriggeredPayload(emulator.GetId(), breakpointID, pc, isHidden);
                messageCenter.Post(NC_EXECUTION_BREAKPOINT, payload);

                // Wait until emulator resumed externally (by debugger or scripting engine)
                emulator.WaitWhilePaused();
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
        if (_context->pTapeFastLoad->HandleLDBytesTrap(*this))
        {
            // Trap consumed the invocation — the routine never executes
            return;
        }
    }

    // TR-DOS disk autostart: one-shot rewrite of the cold-start RUN "boot" line into RUN "<name>".
    // Only armed for a single-BASIC-program autostart; one compare per instruction otherwise
    if (pc == DiskAutostart::COMMAND_LOOP_ENTRY && _context->pDiskAutostart != nullptr &&
        _context->pDiskAutostart->IsArmed())
    {
        _context->pDiskAutostart->HandleCommandLoopHook(*this);
    }

    // Fast disk loading trap (Layer B ROM $3FEC INI sector drain loop trap).
    // Drains pending sector bytes directly into memory via Z80::wd() when armed.
    if (pc == 0x3FEC && _context->pDiskFastLoad != nullptr)
    {
        if (_context->pDiskFastLoad->HandleSectorDrainTrap(*this))
        {
            // Trap consumed the sector drain loop invocation
            return;
        }
    }

    if (cpu.vm1 && cpu.halted)
    {
        // Z80 in HALT state. No further opcode processing will be done until INT or NMI arrives
        cpu.tt += cpu.rate * 1;

        // Frame cost accounting: one halted step burns exactly one t-state
        // (rate is fixed at 256 — speed multipliers scale frameLimit instead)
        state.tstates_halted_current++;

        if (++cpu.halt_cycle == 4)
        {
            cpu.r_low += 1;
            cpu.halt_cycle = 0;
        }
    }
    else
    {
        // Some counter correction for <???>
        if (cpu.pch & temporary.evenM1_C0)
            cpu.tt += (cpu.tt & cpu.rate);

        // Preserve previous PC register state
        cpu.prev_pc = m1_pc;

        // Regular Z80 bus cycle
        // 1. Opcode fetch bookkeeping (m1_pc, m1TraceHook, TTD coverage /
        // probe) for the instruction's first M1 cycle. Runs before the engine
        // loads the registers, so a hook that patches CPU state at the fetch
        // (e.g. redirects PC) takes effect exactly as with the former in-class
        // m1_cycle(). The fetch itself happens inside the engine step below
        cpu.prefix = 0x0000;
        OnInstructionFetch(cpu.pc);

        // 1a. Call trace hook (pre-execution) — appends control-flow events
        // while a calltrace session is capturing; the decoder wants the
        // register state the instruction acts on (SP before CALL pushes /
        // RET pops). The cached feature flag keeps this to a single bool check
        // when calltrace is off. It decodes the instruction from memory at
        // m1_pc, so it does not depend on the fetch having happened
        if (_feature_calltrace_enabled && _memory != nullptr)
        {
            MemoryAccessTracker& tracker = _memory->GetAccessTracker();
            if (tracker.IsCalltraceCapturing())
            {
                tracker.GetCallTraceBuffer()->LogIfControlFlow(_context, _memory, m1_pc,
                                                               _context->emulatorState.frame_counter);
            }
        }

        // 2. Fetch and execute the instruction (vendored unreal-z80 engine).
        // Leaves prefix/opcode, all registers, Q, MEMPTR, EI shadow and HALT
        // state in the Z80State fields
        ExecuteEngineInstruction();

        // 2a. Opcode profiling hook (after opcode execution)
        if (_feature_opcodeprofiler_enabled && _opcodeProfiler)
        {
            _opcodeProfiler->LogExecution(m1_pc, prefix, opcode, f, a, _context->emulatorState.frame_counter, t);
        }

        // 3. Q register: maintained by the engine per instruction class (a
        // flag-writing instruction loads Q = F & 0x28 even when F is
        // numerically unchanged; POP AF / EX AF,AF' and non-flag
        // instructions clear it) and synced back with the other registers
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

/// @brief Apply the queued frequency multiplier change, if any.
///
/// The effective multiplier composes the host speed control (next_) with the
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

    uint8_t desiredMultiplier = static_cast<uint8_t>(state.next_z80_frequency_multiplier << state.hw_turbo_shift);
    if (desiredMultiplier != state.current_z80_frequency_multiplier)
    {
        uint8_t oldMultiplier = state.current_z80_frequency_multiplier;
        state.current_z80_frequency_multiplier = desiredMultiplier;
        state.current_z80_frequency = state.base_z80_frequency * desiredMultiplier;
        state.hw_turbo_shift_applied = state.hw_turbo_shift;

        // Reset rate to normal - counter represents actual t-states
        // Speed multipliers are handled by adjusting frame duration and timings
        cpu.rate = 256;

        MLOGINFO("Z80::ApplyQueuedFrequencyMultiplier - Applied speed multiplier: %dx -> %dx (%.2f MHz, rate=%d, hw_turbo_shift=%u)", oldMultiplier,
                 state.current_z80_frequency_multiplier, state.current_z80_frequency / 1'000'000.0, cpu.rate, state.hw_turbo_shift);

        NotifyCPUFrequencyChanged();
    }
}

void Z80::NotifyCPUFrequencyChanged()
{
    MessageCenter& messageCenter = MessageCenter::DefaultMessageCenter();
    std::string emulatorId = _context->pEmulator ? _context->pEmulator->GetId() : "";
    messageCenter.Post(NC_CPU_FREQ_CHANGED,
                       new CPUFreqPayload(emulatorId,
                                          _context->emulatorState.current_z80_frequency,
                                          _context->emulatorState.current_z80_frequency_multiplier));
}

void Z80::RecomputeFrameTiming()
{
    const CONFIG& config = _context->config;
    const EmulatorState& state = _context->emulatorState;

    _frameLimit = config.frame * state.current_z80_frequency_multiplier;
    _intStart = config.intstart * state.current_z80_frequency_multiplier;
    _intEnd = (config.intstart + config.intlen) * state.current_z80_frequency_multiplier;
}

void Z80::ApplyHardwareTurboNow()
{
    Z80& cpu = *this;
    EmulatorState& state = _context->emulatorState;

    uint8_t desiredMultiplier = static_cast<uint8_t>(state.next_z80_frequency_multiplier << state.hw_turbo_shift);
    uint8_t oldMultiplier = state.current_z80_frequency_multiplier;
    if (desiredMultiplier == oldMultiplier || oldMultiplier == 0)
        return;

    // Preserve the raster instant: the in-frame position is expressed in
    // scaled T-states, so it must be rescaled together with the multiplier
    // (the same instant is 2x further into a 2x longer frame). eipos/haltpos
    // are frame positions too
    auto rescale = [&](uint32_t v) { return static_cast<uint32_t>(static_cast<uint64_t>(v) * desiredMultiplier / oldMultiplier); };
    cpu.t = rescale(cpu.t);
    if (cpu.eipos >= 0)
        cpu.eipos = static_cast<int32_t>(rescale(static_cast<uint32_t>(cpu.eipos)));
    cpu.haltpos = static_cast<uint16_t>(rescale(cpu.haltpos));

    state.current_z80_frequency_multiplier = desiredMultiplier;
    state.current_z80_frequency = state.base_z80_frequency * desiredMultiplier;
    state.hw_turbo_shift_applied = state.hw_turbo_shift;
    cpu.rate = 256;

    // The running Z80FrameCycle loop reads these every iteration
    RecomputeFrameTiming();

    MLOGINFO("Z80::ApplyHardwareTurboNow - hardware turbo applied mid-frame: %dx -> %dx (%.2f MHz) at t=%u",
             oldMultiplier, desiredMultiplier, state.current_z80_frequency / 1'000'000.0, cpu.t);

    // Mid-frame hardware strobes must notify too: they never pass through a
    // frame boundary, so ApplyQueuedFrequencyMultiplier will see
    // desiredMultiplier == current and post nothing on the next frame
    NotifyCPUFrequencyChanged();
}

/// Execute number of cpu cycles equivalent to full frame screen render
void Z80::Z80FrameCycle()
{
    [[maybe_unused]] const CONFIG& config = _context->config;
    [[maybe_unused]] Z80& cpu = *this;
    [[maybe_unused]] EmulatorState& state = _context->emulatorState;

    // Apply queued HOST speed multiplier change at the frame boundary (if any).
    // Hardware turbo strobes are applied immediately by ApplyHardwareTurboNow
    ApplyQueuedFrequencyMultiplier();

    // Scaled frame length and INT window - members, so a mid-frame hardware
    // turbo switch (ApplyHardwareTurboNow) is picked up by the loop below
    RecomputeFrameTiming();

    bool int_occurred = false;

    cpu.haltpos = 0;

    // INT interrupt handling lasts for more than 1 frame
    if (_intEnd >= _frameLimit)
    {
        _intEnd -= _frameLimit;
        cpu.int_pending = true;
        int_occurred = true;
    }

    // Cover whole frame (control by effective t-states)
    while (cpu.t < _frameLimit)
    {
        // Mid-frame pause park. Pause() is otherwise observed only at frame
        // boundaries (MainLoop::Run), so an in-flight frame - which under
        // turbo/debug-mode or TTD recording can take hundreds of milliseconds -
        // would delay the park and its confirmation until the frame completes.
        // One volatile read per instruction keeps the unpaused hot path cheap;
        // WaitWhilePaused parks + confirms and wakes on Resume()/Stop() via CV.
        if (Emulator* emulator = _context->pEmulator; emulator && emulator->IsPaused())
            emulator->WaitWhilePaused();

        // Handle interrupts if arrived
        // Returns true if INT was handled - in that case, skip Z80Step for this iteration
        // because INT entry IS the "instruction" that consumes this cycle
        bool intHandled = ProcessInterrupts(int_occurred, _intStart, _intEnd);

        if (!intHandled)
        {
            // Perform single Z80 command cycle
            Z80Step();
        }

        // Update peripheral states after CPU cycle
        OnCPUStep();
    }
}

/// endregion </Methods>

/// region <Z80 lifecycle>

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

    // Record PC for current opcode (prefixes should not alter original PC)
    if (prefix == 0x0000)
        OnInstructionFetch(cpu.pc);

    // Z80 CPU M1 cycle logic
    r_low = ((r_low + 1) & 0x7f) | (r_low & 0x80);  // Keep memory refresh register ticking
    opcode = rd(cpu.pc, true);  // Initiate memory read cycle and Keep opcode copy for trace / debug purposes

    // Point PC to next byte
    cpu.pc++;

    // M1 cycle is always 4 CPU clocks (3 for memory read and 1 for decoding)
    // +3 will be done in rd() (Memory read) method
    // +1 will be done here
    IncrementCPUCyclesCounter(1);

    return opcode;
}

/// Opcode fetch bookkeeping, shared by m1_cycle() and the engine bridge.
/// Called at the start of an M1 cycle whose fetch counts as an instruction
/// start (the core's "prefix == 0" fetches - see Z80EngineBridge).
void Z80::OnInstructionFetch(uint16_t fetchPc)
{
    m1_pc = fetchPc;

    if (m1TraceHook)
        m1TraceHook(m1_pc);

    // Per-frame execution coverage for reverse search. One predictable
    // branch on a plain bool when recording is off, which is the common
    // case; the page lookup and the append only happen while a session is
    // actually capturing. This is the only record that a frame executed a
    // given address - instruction fetches are not journalled - so without
    // it a reverse breakpoint has no choice but to replay every frame.
    if (_context->ttdCoverageActive && _context->pTimeTravelManager != nullptr)
    {
        _context->pTimeTravelManager->RecordExecutedCoverage(
            _memory->GetPhysPageForZ80Address(m1_pc), m1_pc);
    }

    // Phase 4 - access probe for Execute access type (TDD 9.2).
    // Fires once per instruction at the M1 (instruction fetch) cycle.
    if (_context->ttdProbe.IsArmed())
    {
        // Resolve the bank the opcode was fetched from, so a reverse
        // breakpoint can distinguish "PC 0xC000 in page 3" from the same
        // address reached with a different page banked in. Code executing
        // from ROM reports kPhysPageNone.
        const uint8_t execPhysPage = _memory->GetPhysPageForZ80Address(m1_pc);
        if (_context->ttdProbe.Matches(m1_pc, ttd::TTDAccessType::Execute, 0, m1_pc, execPhysPage))
        {
            const auto& st = _context->emulatorState;
            const ttd::TTDTimePoint tp{st.frame_counter, t};
            _context->ttdProbe.RecordHit(tp, m1_pc, /*value=*/0, execPhysPage,
                                          ttd::TTDAccessType::Execute);
        }
    }
}

/// Dispatching memory read method. Used directly from Z80 microcode (CPULogic and opcode)
/// Read access to memory takes 3 clock cycles
/// \param addr
/// \return
uint8_t Z80::rd(uint16_t addr, bool isExecution)
{
    // ULA memory contention: accessing contended memory (0x4000-0x7FFF; on
    // 128K also 0xC000+ with an odd page mapped) during screen rendering on
    // ZX-48K/128K stalls the CPU.
    if (!isExecution)
    {
        UlaContention* ula = _context->pUlaContention;
        if (ula && ula->IsAddressContended(addr))
        {
            uint8_t delay = ula->GetContentionDelay();
            if (delay > 0)
                IncrementCPUCyclesCounter(delay);
        }
    }

    IncrementCPUCyclesCounter(3);

    return MemoryReadNoContention(addr, isExecution);
}

/// Memory read access itself (MemIf dispatch + bus trace), without contention
/// or T-state accounting - the timing is owned by the caller (rd() or the
/// execution engine)
uint8_t Z80::MemoryReadNoContention(uint16_t addr, bool isExecution)
{
    uint8_t value = (_memory->*MemIf->MemoryRead)(addr, isExecution);

    if (busTraceHook)
        busTraceHook('R', addr, value);

    return value;
}

/// Dispatching memory write method. Used directly from Z80 microcode (CPULogic and opcode)
/// Write access to memory takes 3 clock cycles
/// \param addr
/// \param val
void Z80::wd(uint16_t addr, uint8_t val)
{
    // ULA memory contention: accessing contended memory (0x4000-0x7FFF; on
    // 128K also 0xC000+ with an odd page mapped) during screen rendering on
    // ZX-48K/128K stalls the CPU.
    {
        UlaContention* ula = _context->pUlaContention;
        if (ula && ula->IsAddressContended(addr))
        {
            uint8_t delay = ula->GetContentionDelay();
            if (delay > 0)
                IncrementCPUCyclesCounter(delay);
        }
    }

    IncrementCPUCyclesCounter(3);

    MemoryWriteNoContention(addr, val);
}

/// Memory write access itself (MemIf dispatch + bus trace), without
/// contention or T-state accounting
void Z80::MemoryWriteNoContention(uint16_t addr, uint8_t val)
{
    (_memory->*MemIf->MemoryWrite)(addr, val);

    if (busTraceHook)
        busTraceHook('W', addr, val);
}

uint8_t Z80::in(uint16_t port)
{
    // ULA IO contention: accessing contended ports during screen rendering
    // on ZX-48K/128K delays the CPU by the contention pattern.
    // This is critical for accurate timing of raster-sync effects.
    {
        UlaContention* ula = _context->pUlaContention;
        if (ula)
        {
            uint8_t delay = ula->GetIOContentionDelay(port);
            if (delay > 0)
                IncrementCPUCyclesCounter(delay);
        }
    }

    return PortInNoContention(port);
}

/// Port read itself (model port decoder, bus trace, floating bus), without
/// IO contention - the timing is owned by the caller (in() or the engine)
uint8_t Z80::PortInNoContention(uint16_t port)
{
    PortDecoder& portDecoder = *_context->pPortDecoder;

    // Let model-specific decoder to process port input
    uint8_t result = portDecoder.DecodePortIn(port, m1_pc);

    if (busTraceHook)
        busTraceHook('I', port, result);

    // Floating bus: if no hardware device decoded the port, the ULA returns
    // the video byte currently on the data bus.
    // On ZX-48K/128K, any port with A0=1 (odd port) that isn't handled
    // by a specific device returns the floating bus value.
    // IMPORTANT: ports decoded by real hardware (WD1793, Kempston, etc.)
    // must NOT get the floating bus override even if they return 0xFF.
    if (!portDecoder.WasLastPortDecoded() && (port & 0x0001))
    {
        UlaContention* ula = _context->pUlaContention;
        if (ula)
        {
            uint8_t floatVal = ula->GetFloatingBus();
            if (floatVal != 0xFF)
                result = floatVal;
        }
    }

    return result;
}

void Z80::out(uint16_t port, uint8_t val)
{
    // ULA IO contention: accessing contended ports during screen rendering
    // on ZX-48K/128K delays the CPU by the contention pattern.
    // This must be applied BEFORE the port write so that SetBorderColor()
    // sees the correct (delayed) t-state.
    {
        UlaContention* ula = _context->pUlaContention;
        if (ula)
        {
            uint8_t delay = ula->GetIOContentionDelay(port);
            if (delay > 0)
                IncrementCPUCyclesCounter(delay);
        }
    }

    PortOutNoContention(port, val);
}

/// Port write itself (model port decoder, bus trace), without IO contention
void Z80::PortOutNoContention(uint16_t port, uint8_t val)
{
    PortDecoder& portDecoder = *_context->pPortDecoder;

    // Let model-specific decoder to process port output
    portDecoder.DecodePortOut(port, val, m1_pc);

    if (busTraceHook)
        busTraceHook('O', port, val);
}

void Z80::retn()
{
    // Called by the ED45 RETN handler after iff1 = iff2: leaving the NMI handler
    // ends the NMI session. Checking nmi_in_progress (not just restoring IFF1)
    // keeps a plain RET executed deep inside an NMI handler from silently
    // ending it - only RETN does that, per the Z80 interrupt architecture
    nmi_in_progress = false;
}

/// Execute exactly one instruction on the engine.
///
/// The Z80Registers fields are the source of truth between instructions (the
/// debugger, TTD restore, snapshot loaders, fast-load traps and tests write
/// them directly), so the whole register file is loaded into the engine
/// before the step (unless it still holds exactly what the previous step
/// stored) and stored back after it.
void Z80::ExecuteEngineInstruction()
{
    Z80CPU* engine = _engine;

    // Wait-state hook only while the model has ULA contention (Pentagon and
    // other uncontended models run without the per-cycle callback)
    UlaContention* ula = _context->pUlaContention;
    const bool contention = ula != nullptr && ula->IsContentionEnabled();
    if (contention != _engineContention)
    {
        Z80EngineBridge::SetContention(engine, this, contention);
        _engineContention = contention;
    }

    /// region <Load: Z80Registers -> engine>
    // The engine still holds what the last step stored into the fields unless
    // something wrote them since (debugger, TTD restore, INT/NMI acceptance,
    // traps, tests) - compare against that copy and load only on a
    // difference. Field-wise, same widths as the stores that produced them
    const Z80CpuRegisters& stored = _engineRegsStored;
    const uint8_t r = static_cast<uint8_t>((r_low & 0x7F) | (r_hi & 0x80));
    const bool engineCurrent = _engineRegsValid && stored.pc == pc && stored.af == af &&
                               stored.bc == bc && stored.de == de && stored.hl == hl && stored.sp == sp &&
                               stored.ix == ix && stored.iy == iy && stored.memptr == memptr && stored.q == q &&
                               stored.r == r && stored.afAlt == alt.af && stored.bcAlt == alt.bc &&
                               stored.deAlt == alt.de && stored.hlAlt == alt.hl && stored.i == i &&
                               stored.im == im && stored.iff1 == iff1 && stored.iff2 == iff2;
    if (!engineCurrent)
    {
        Z80CpuRegisters regs;
        regs.af = af;
        regs.bc = bc;
        regs.de = de;
        regs.hl = hl;
        regs.afAlt = alt.af;
        regs.bcAlt = alt.bc;
        regs.deAlt = alt.de;
        regs.hlAlt = alt.hl;
        regs.ix = ix;
        regs.iy = iy;
        regs.pc = pc;
        regs.sp = sp;
        regs.memptr = memptr;
        regs.i = i;
        regs.r = r;
        regs.im = im;
        regs.iff1 = iff1;
        regs.iff2 = iff2;
        regs.q = q;

        // HALT model (unchanged): a halted CPU re-executes the HALT opcode
        // every step (its M1 fetch included) until INT/NMI moves PC past it,
        // and the latch lives in `halted`. So the engine never sees its latch
        // set - it always performs the fetch, as the interpreter did
        regs.halted = 0;

        Z80CpuSetRegisters(engine, &regs);
    }
    else if (stored.halted)
    {
        Z80CpuSetReg(engine, Z80CpuRegHalted, 0);  // HALT re-executes (see above)
    }

    if (outc0 != _engineOutC0)
    {
        Z80CpuSetOutC0Value(engine, outc0);
        _engineOutC0 = outc0;
    }

    Z80CpuSetTstates(engine, t);
    /// endregion </Load: Z80Registers -> engine>

    _engineTtBase = tt;
    _engineTBase = t;
    _engineTtAdjust = 0;
    _engineM1Count = 0;
    _engineIndexPrefix = 0;
    _engineIndexResolved = false;
    _engineCbPending = false;
    _engineIndexCbDisplacement = false;
    _engineExpectOpcode = true;
    _engineLastM1Byte = 0;
    _engineWroteR = false;

    Z80CpuStep(engine);

    /// region <Store: engine -> Z80Registers>
    tt = EngineHostTt(Z80CpuTstates(engine));

    Z80CpuRegisters& regs = _engineRegsStored;
    Z80CpuGetRegisters(engine, &regs);
    _engineRegsValid = true;
    const bool engineHalted = regs.halted != 0;
    af = regs.af;
    bc = regs.bc;
    de = regs.de;
    hl = regs.hl;
    alt.af = regs.afAlt;
    alt.bc = regs.bcAlt;
    alt.de = regs.deAlt;
    alt.hl = regs.hlAlt;
    ix = regs.ix;
    iy = regs.iy;
    pc = regs.pc;
    sp = regs.sp;
    memptr = regs.memptr;
    i = regs.i;
    im = regs.im;
    iff1 = regs.iff1;
    iff2 = regs.iff2;
    q = regs.q;

    // R: the engine exposes the 7-bit refresh counter plus R7. The host keeps
    // bit 7 of r_low as well (loaders and the register view read r_low as a
    // whole byte): the refresh increment never touches it, only LD R,A
    // rewrites it together with r_hi
    if (_engineWroteR)
    {
        r_low = regs.r;
        r_hi = static_cast<uint8_t>(regs.r & 0x80);
    }
    else
    {
        r_low = static_cast<uint8_t>((regs.r & 0x7F) | (r_low & 0x80));
    }

    // HALT executed: latch it, remember when it was entered (first time only)
    if (engineHalted)
    {
        if (!halted)
            haltpos = static_cast<uint16_t>(t);
        halted = 1;
        halt_cycle = 0;
    }

    // EI shadow: the engine blocks INT right after EI (IFF1 set, INT not yet
    // possible); ProcessInterrupts expresses the same as "t == eipos"
    if (iff1 && !Z80CpuIntPossible(engine))
        eipos = static_cast<int32_t>(t);
    /// endregion </Store: engine -> Z80Registers>
}

/// Host tt that corresponds to an engine T-state of the current instruction
uint32_t Z80::EngineHostTt(uint32_t engineT) const
{
    return _engineTtBase + (engineT - _engineTBase) * rate + _engineTtAdjust;
}

/// Make the host time current before host code runs inside an instruction
void Z80::PublishEngineTime(uint32_t engineT)
{
    tt = EngineHostTt(engineT);
}

/// Host code inside a bus callback may move tt (hardware turbo rescale on a
/// port write): keep that shift for the rest of the instruction
void Z80::AbsorbHostTimeChange(uint32_t publishedTt)
{
    _engineTtAdjust += tt - publishedTt;
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
    Z80& cpu = *this;
    VideoControl& video = _context->pScreen->_vid;
    bool intHandled = false;

    // NMI processing (accepted at the instruction boundary, priority over INT).
    // Requested via RequestNonMaskedInterrupt(); on Scorpion models the MNI
    // "magic button" orchestration (Emulator::RequestMNI) pages the Shadow
    // Monitor BEFORE requesting, so only the architecture-defined CPU dance
    // lives here. The model-specific block from the original UnrealSpeccy
    // (ATM3 bank switching / Scorpion pc>0x4000 guard) moved to that layer -
    // the MNI latch already owns the ROM selection.
    if (_nmi_pending_count > 0)
    {
        _nmi_pending_count = 0;
        cpu.nmi_in_progress = true;

        // If CPU halted - unblock it by moving PC forward (return lands past the HALT)
        if (DirectRead(cpu.pc) == 0x76)
            cpu.pc++;

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

        // IFF2 keeps a copy of IFF1 for RETN; maskable interrupts disabled in the handler
        cpu.iff2 = cpu.iff1;
        cpu.iff1 = 0;
        cpu.int_pending = false;

        video.memcyc_lcmd = 0;  // new command, start accumulate number of busy memcycles

        return true;  // NMI accepted: skip Z80Step this iteration
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
    if (!int_occurred && cpu.t > int_start)
    {
        int_occurred = true;
        cpu.int_pending = true;
    }

    if (cpu.int_pending && (cpu.t >= int_end))
        cpu.int_pending = false;

    video.memcyc_lcmd = 0;  // new command, start accumulate number of busy memcycles

    /// region <INT (Non-masked interrupt)>

    // If INT signal raised and IFF1 flag is set allowing interrupts handling (set by EI command)
    // Important! Interrupts are in fact enabled only after command executed after EI (delay to 1 command)
    // See: https://floooh.github.io/2021/12/06/z80-instruction-timing.html
    // See: https://www.msx.org/forum/development/msx-development/question-about-z80r800-irqs-and-eidi-behaviour
    if (cpu.int_pending && cpu.iff1 && cpu.t != cpu.eipos  // Make delay until command after EI executed
    )
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

void Z80::HandleINT(uint8_t vector)
{
    Z80& cpu = *this;
    CONFIG& config = _context->config;
    EmulatorState& state = _context->emulatorState;

    /// region <CPU is stopped on HALT (opcode 0x76) command>

    // If CPU halted - unblock it by moving PC forward
    if (DirectRead(cpu.pc) == 0x76)
        cpu.pc++;

    /// endregion </CPU is stopped on HALT (opcode 0x76) command>

    /// region <Determine interrupt handler address>
    uint16_t interruptHandlerAddress;
    if (cpu.im < 2)
    {
        // IM0, IM1
        interruptHandlerAddress = 0x38;
    }
    else
    {
        // IM2
        // Raw memory access without T-state accounting: the vector fetch time
        // is already included in interruptDuration below (rd() would add +3T per byte)
        uint16_t vectorAddress = vector + cpu.i * 0x100;
        interruptHandlerAddress = (_memory->*MemIf->MemoryRead)(vectorAddress, false) +
                                  0x100 * (_memory->*MemIf->MemoryRead)(vectorAddress + 1, false);
    }
    /// endregion </Determine interrupt handler address>

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

    // Jump to interrupt handler
    cpu.pc = interruptHandlerAddress;
    cpu.memptr = interruptHandlerAddress;
    cpu.halted = 0;

    // Block potential interrupt double handling
    cpu.iff1 = 0;
    cpu.iff2 = 0;
    cpu.int_pending = false;

    /// region <TSConf>

    // TODO: move to TSConf plugin
    if (config.mem_model == MM_TSL)
    {
        if (state.ts.intctrl.frame_pend)
            state.ts.intctrl.frame_pend = 0;
        else if (state.ts.intctrl.line_pend)
            state.ts.intctrl.line_pend = 0;
        else if (state.ts.intctrl.dma_pend)
            state.ts.intctrl.dma_pend = 0;
    }

    /// endregion </TSConf>
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
    {"R", false, false, [](const Z80State* s) -> uint16_t { return s->r_low; }, [](Z80State* s, uint16_t v) { s->r_low = static_cast<uint8_t>(v); }},
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
    {"IR", true, false, [](const Z80State* s) -> uint16_t { return s->ir_; }, [](Z80State* s, uint16_t v) { s->ir_ = v; }},
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

    info->setter(state, value);
    return true;
}

/// endregion </Register Access API>