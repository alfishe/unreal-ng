#include "z80.h"

#include "3rdparty/message-center/messagecenter.h"
#include "common/modulelogger.h"
#include "common/stringhelper.h"
#include "common/timehelper.h"
#include "debugger/analyzers/analyzermanager.h"
#include "debugger/breakpoints/breakpointmanager.h"
#include "debugger/debugmanager.h"
#include "debugger/ttd/timetravelmanager.h"
#include "emulator/cpu/op_noprefix.h"
#include "emulator/cpu/opcode_profiler.h"
#include "emulator/emulator.h"
#include "emulator/notifications.h"
#include "emulator/ports/portdecoder.h"
#include "emulator/video/screen.h"
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
                // Pause emulator (single source of truth)
                emulator.Pause();

                // Broadcast notification - breakpoint triggered (instance-tagged)
                MessageCenter& messageCenter = MessageCenter::DefaultMessageCenter();
                BreakpointTriggeredPayload* payload =
                    new BreakpointTriggeredPayload(emulator.GetId(), breakpointID, pc);
                messageCenter.Post(NC_EXECUTION_BREAKPOINT, payload);

                // Wait until emulator resumed externally (by debugger or scripting engine)
                emulator.WaitWhilePaused();
            }
        }
    }

    if (cpu.vm1 && cpu.halted)
    {
        // Z80 in HALT state. No further opcode processing will be done until INT or NMI arrives
        cpu.tt += cpu.rate * 1;

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

        // Save F register before opcode execution (for Q register update)
        uint8_t prev_f = cpu.f;

        // Regular Z80 bus cycle
        // 1. Fetch opcode (Z80 M1 bus cycle)
        cpu.prefix = 0x0000;
        cpu.opcode = m1_cycle();

        // 2. Emulate fetched Z80 opcode
        (normal_opcode[opcode])(&cpu);

        // 2a. Opcode profiling hook (after opcode execution)
        if (_feature_opcodeprofiler_enabled && _opcodeProfiler)
        {
            _opcodeProfiler->LogExecution(m1_pc, prefix, opcode, f, a, _context->emulatorState.frame_counter, t);
        }

        // 3. Update Q register based on whether flags were modified
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

/// Execute number of cpu cycles equivalent to full frame screen render
void Z80::Z80FrameCycle()
{
    [[maybe_unused]] const CONFIG& config = _context->config;
    [[maybe_unused]] Z80& cpu = *this;
    [[maybe_unused]] EmulatorState& state = _context->emulatorState;

    // Apply queued speed multiplier change at frame boundary (if any)
    // This prevents mid-frame timing inconsistencies
    if (state.next_z80_frequency_multiplier != state.current_z80_frequency_multiplier)
    {
        uint8_t oldMultiplier = state.current_z80_frequency_multiplier;
        state.current_z80_frequency_multiplier = state.next_z80_frequency_multiplier;
        state.current_z80_frequency = state.base_z80_frequency * state.current_z80_frequency_multiplier;

        // Reset rate to normal - counter represents actual t-states
        // Speed multipliers are handled by adjusting frame duration and timings
        cpu.rate = 256;

        MLOGINFO("Z80::Z80FrameCycle - Applied queued speed multiplier: %dx -> %dx (%.2f MHz, rate=%d)", oldMultiplier,
                 state.current_z80_frequency_multiplier, state.current_z80_frequency / 1'000'000.0, cpu.rate);
    }

    // Scale frame duration by speed multiplier
    uint32_t frameLimit = config.frame * state.current_z80_frequency_multiplier;

    // Video Interrupt position calculation - scale by multiplier for Z80 timing
    bool int_occurred = false;
    unsigned int_start = config.intstart * state.current_z80_frequency_multiplier;
    unsigned int_end = (config.intstart + config.intlen) * state.current_z80_frequency_multiplier;

    cpu.haltpos = 0;

    // INT interrupt handling lasts for more than 1 frame
    if (int_end >= frameLimit)
    {
        int_end -= frameLimit;
        cpu.int_pending = true;
        int_occurred = true;
    }

    // Cover whole frame (control by effective t-states)
    while (cpu.t < frameLimit)
    {
        // Handle interrupts if arrived
        // Returns true if INT was handled - in that case, skip Z80Step for this iteration
        // because INT entry IS the "instruction" that consumes this cycle
        bool intHandled = ProcessInterrupts(int_occurred, int_start, int_end);

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
    {
        m1_pc = cpu.pc;

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

    PortDecoder& portDecoder = *_context->pPortDecoder;

    // Let model-specific decoder to process port output
    portDecoder.DecodePortOut(port, val, m1_pc);

    if (busTraceHook)
        busTraceHook('O', port, val);
}

void Z80::retn() {}

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
void Z80::RequestNonMaskedInterrupt() {}

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

    // NMI processing
    if (_nmi_pending_count > 0)
    {
        /* move to ports logic
        if (config.mem_model == MM_ATM3)
        {
            _nmi_pending_count = 0;
            cpu.nmi_in_progress = true;

            SetBanks();
            HandleNMI(RM_NOCHANGE);
            return;
        }
        else if (config.mem_model == MM_PROFSCORP || config.mem_model == MM_SCORP)
        {
            _nmi_pending_count--;
            if (cpu.pc > 0x4000)
            {
                HandleNMI(RM_DOS);
                _nmi_pending_count = 0;
            }
        }
        else
            _nmi_pending_count = 0;
         */
    }  // end if (nmi_pending)

    // Generate INT
    // TODO: move INT forming logic to Screen class since in reality it's formed by ULA / frame counters
    // Strict sampling (cpu.t > int_start): the ULA registers the INT signal one clock
    // after the raster compare (MiSTer ula.sv: INT <= 1 on the next edge) and the CPU
    // samples INT only at end-of-instruction edges - an instruction boundary landing
    // exactly at int_start still sees INT inactive. Inclusive ">=" accepts 1T early,
    // which shifts interrupt-locked raster effects by one locked state (doc 20).
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