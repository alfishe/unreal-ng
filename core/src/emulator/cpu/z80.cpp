#include "z80.h"

#include "3rdparty/message-center/messagecenter.h"
#include "common/modulelogger.h"
#include "common/stringhelper.h"
#include "common/timehelper.h"
#include "debugger/analyzers/analyzermanager.h"
#include "debugger/breakpoints/breakpointmanager.h"
#include "debugger/debugmanager.h"
#include "emulator/cpu/op_noprefix.h"
#include "emulator/emulator.h"
#include "emulator/ports/portdecoder.h"
#include "emulator/video/screen.h"
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

    _context = nullptr;

    MLOGDEBUG("Z80::~Z80()");
}

/// endregion </Constructors / Destructors>

/// region <Properties>

bool Z80::IsPaused()
{
    return _pauseRequested;
}

/// endregion </Properties

/// region <Methods>

/// Handle Z80 reset signal
void Z80::Reset()
{
    // Emulation state
    last_branch = 0x0000;  // Address of last branch (in Z80 address space)
    int_pending = false;   // No interrupts pending
    int_gate = true;       // Allow external interrupts

    tt = 0;  // Scaled to CPU frequency multiplier cycle count

    // Z80 chip reset sequence. See: http://www.z80.info/interrup.htm (Reset Timing section)
    int_flags = 0;  // Set interrupt mode 0
    ir_ = 0;        // Reset IR (Instruction Register)
    pc = 0x0000;    // Reset PC (Program Counter)
    im = 0;         // IM0 mode is set by default
    sp = 0xFFFF;    // Stack pointer set to the end of memory address space
    af = 0xFFFF;    // Real chip behavior
    q = 0;          // Q register (undocumented) reset

    // All that takes 3 clock cycles
    IncrementCPUCyclesCounter(3);
}

void Z80::Pause()
{
    _pauseRequested = true;
}

void Z80::Resume()
{
    _pauseRequested = false;
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

    // Let debugger process step event
    if (cpu.isDebugMode && skipBreakpoints == false && _context->pDebugManager != nullptr)
    {
        BreakpointManager& brk = *_context->pDebugManager->GetBreakpointsManager();
        uint16_t breakpointID = brk.HandlePCChange(pc);
        if (breakpointID != BRK_INVALID)
        {
            // Request to pause emulator
            // Important note: Emulator.Pause() is needed, not Core.Pause() or Z80.Pause() for successful resume later
            emulator.Pause();

            // Broadcast notification - breakpoint triggered
            MessageCenter& messageCenter = MessageCenter::DefaultMessageCenter();
            SimpleNumberPayload* payload = new SimpleNumberPayload(breakpointID);
            messageCenter.Post(NC_EXECUTION_BREAKPOINT, payload);
            
            // Dispatch breakpoint hit event to AnalyzerManager
            if (_context->pDebugManager && _context->pDebugManager->GetAnalyzerManager())
            {
                _context->pDebugManager->GetAnalyzerManager()->dispatchBreakpointHit(pc, breakpointID, this);
            }

            // Wait until emulator resumed externally (by debugger or scripting engine)
            WaitUntilResumed();
        }
    }

    /// region  <Ports logic>

    // Execution address is within range [0x3D00 .. 0x3DFF] => Beta Disk Interface (TR-DOS) ROM must be activated
    if (!(state.flags & CF_TRDOS) && (cpu.pch == 0x3D))
    {
        state.flags |= CF_TRDOS;

        // Apply ROM page changes
        memory.UpdateZ80Banks();

        // TODO: remove Debug
        /// region <Debug>
        // emulator.Pause();
        //  Wait until emulator resumed externally (by debugger or scripting engine)
        // WaitUntilResumed();
        /// endregion </Debug
    }
    else if ((state.flags & CF_TRDOS) &&
             (cpu.pch >= 0x40))  // When execution leaves ROM area (>= 0x4000) - DOS must be disabled
    {
        state.flags &= ~CF_TRDOS;

        // Apply ROM page changes
        memory.UpdateZ80Banks();
    }

    /* TODO: move to Ports class
    if (state.flags & CF_SETDOSROM)
    {
        if (cpu.pch == 0x3D)
        {
            state.flags |= CF_TRDOS;  // !!! add here TS memconf behaviour !!!
            SetBanks();
        }
    }
    else if (state.flags & CF_LEAVEDOSADR)
    {
        if (cpu.pch & 0xC0) // PC > 3FFF closes TR-DOS
        {
            state.flags &= ~CF_TRDOS;
            SetBanks();
        }

        //if (config.trdos_traps)
        //	state.wd.trdos_traps();
    }
    else if (state.flags & CF_LEAVEDOSRAM)
    {
        // Execution code from RAM address - disables TR-DOS ROM
        uint8_t bank = (cpu.pc >> 14) & 3;
        if (memory.GetMemoryBankMode(bank) == MemoryBankModeEnum::BANK_RAM)
        {
            state.flags &= ~CF_TRDOS;
            SetBanks();
        }

        // WD93 logic
        //if (config.trdos_traps)
        //	state.wd.trdos_traps();
    }
     */
    /// endregion  </Ports logic>

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
        ProcessInterrupts(int_occurred, int_start, int_end);

        // Perform single Z80 command cycle
        Z80Step();

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
        m1_pc = cpu.pc;

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
    IncrementCPUCyclesCounter(3);

    return (_memory->*MemIf->MemoryRead)(addr, isExecution);
}

/// Dispatching memory write method. Used directly from Z80 microcode (CPULogic and opcode)
/// Write access to memory takes 3 clock cycles
/// \param addr
/// \param val
void Z80::wd(uint16_t addr, uint8_t val)
{
    IncrementCPUCyclesCounter(3);

    (_memory->*MemIf->MemoryWrite)(addr, val);
}

uint8_t Z80::in(uint16_t port)
{
    PortDecoder& portDecoder = *_context->pPortDecoder;

    // Let model-specific decoder to process port output
    uint8_t result = portDecoder.DecodePortIn(port, m1_pc);

    return result;
}

void Z80::out(uint16_t port, uint8_t val)
{
    PortDecoder& portDecoder = *_context->pPortDecoder;

    // Let model-specific decoder to process port output
    portDecoder.DecodePortOut(port, val, m1_pc);
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
void Z80::ProcessInterrupts(bool int_occurred, unsigned int_start, unsigned int_end)
{
    Z80& cpu = *this;
    VideoControl& video = _context->pScreen->_vid;

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
    if (!int_occurred && cpu.t >= int_start)
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
    }

    /// endregion </INT (Non-masked interrupt)>
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
        uint16_t vectorAddress = vector + cpu.i * 0x100;
        interruptHandlerAddress = rd(vectorAddress) + 0x100 * rd(vectorAddress + 1);
    }
    /// endregion </Determine interrupt handler address>

    /// region <Calculate INT duration>

    int interruptDuration = 0;

    switch (cpu.im)
    {
        case 0:
        case 1:
            interruptDuration = 13 - 3;  // M1 cycle (3 cycles) is already counted
            break;
        case 2:
            interruptDuration = 19 - 3;  // M1 cycle (3 cycles) is already counted
            break;
        default:
            throw std::logic_error("Unknown interrupt mode detected");
            break;
    }

    IncrementCPUCyclesCounter(interruptDuration);

    /// endregion </Calculate INT duration>

    // Push return address to stack
    uint16_t sp = cpu.sp;
    wd(--sp, cpu.pch);
    wd(--sp, cpu.pcl);
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

void Z80::WaitUntilResumed()
{
    // Pause emulation until upper-level controller (emulator / scripting) resumes execution
    if (_pauseRequested)
    {
        while (_pauseRequested)
        {
            sleep_ms(20);
        }
    }
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