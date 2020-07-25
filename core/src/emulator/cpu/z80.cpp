#include "stdafx.h"

#include "common/logger.h"

#include "z80.h"
#include "common/stringhelper.h"
#include "emulator/cpu/op_noprefix.h"
#include "emulator/video/screen.h"

#include "emulator/ports/portdecoder.h"

/// region <Constructors / Destructors>

Z80::Z80(EmulatorContext* context)
{
	_context = context;
    _memory = context->pMemory;

    //

    // Initialize memory access interfaces
    FastMemIf = Memory::GetFastMemoryInterface();
    DbgMemIf = Memory::GetDebugMemoryInterface();
	MemIf = FastMemIf;	// Use fast memory access interface by default

	// Ensure register memory and unions do not contain garbage
	tt = 0;
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


	tpi = 0;
	rate = (1 << 8);
	dbgbreak = 0;
	dbgchk = 0;
	debug_last_t = 0;
	trace_curs = trace_top = (unsigned)-1;
	trace_mode = 0;
	mem_curs = mem_top = 0;
	pc_trflags = nextpc = 0;
	dbg_stophere = dbg_stopsp = (unsigned)-1;
	dbg_loop_r1 = 0;
	dbg_loop_r2 = 0xFFFF;
	int_pend = false;
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
    direct_registers[6] = &_trashRegister; // Redirect DDCB operation writes with no destination registers to unused register variable
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

	LOGDEBUG("Z80::~Z80()");
}

/// endregion </Constructors / Destructors>

/// region <Z80 lifecycle>

uint8_t Z80::m1_cycle()
{
	static Z80& cpu = *this;
	static CONFIG& config = _context->config;
	static COMPUTER& state = _context->state;
	static TEMP& temporary = _context->temporary;
	static PortDecoder& portDecoder = *_context->pPortDecoder;

	/// region TODO: move to Ports class
	/*
	if ((config.mem_model == MM_PENTAGON) &&
		((state.pEFF7 & (EFF7_CMOS | EFF7_4BPP)) == (EFF7_CMOS | EFF7_4BPP)))
		temporary.offset_vscroll++;
	
	if ((config.mem_model == MM_PENTAGON) &&
		((state.pEFF7 & (EFF7_384 | EFF7_4BPP)) == (EFF7_384 | EFF7_4BPP)))
		temporary.offset_hscroll++;
	
	if (config.mem_model == MM_TSL && state.ts.vdos_m1)
	{
		state.ts.vdos = 1;
		state.ts.vdos_m1 = 0;
		SetBanks();
	}
	 */
	/// endregion TODO: move to Ports class

	/// region <Test>

	/*
    if (cpu.pc == 0x11DC)
    {
        Logger::Unmute();
        LOGINFO("RAM-FILL is executed");
        Logger::Mute();
    }

    if (cpu.pc == 0x11E2)
    {
        Logger::Unmute();
        LOGINFO("RAM-READ is executed");
        Logger::Mute();
    }

    if (cpu.pc == 0x11EF)
    {
        Logger::UnmuteSilent();
        LOGINFO("RAM-DONE is executed. PC: %04X", cpu.pc);
        Logger::MuteSilent();
    }

    if (cpu.pc == 0x1219)
    {
        Logger::UnmuteSilent();
        LOGINFO("RAM-SET is executed. PC: %04X", cpu.pc);
        Logger::MuteSilent();
    }

    if (cpu.pc == 0x12A2)
    {
        Logger::UnmuteSilent();
        LOGINFO("MAIN-EXEC is executed. PC: %04X", cpu.pc);
        Logger::MuteSilent();
    }

    if (cpu.pc == 0x0D6B)
    {
        Logger::UnmuteSilent();
        LOGINFO("CLS is executed. PC: %04X", cpu.pc);
        Logger::MuteSilent();
    }

    if (cpu.pc == 0x0DAF)
    {
        Logger::UnmuteSilent();
        LOGINFO("CL-ALL is executed. PC: %04X", cpu.pc);
        //Logger::MuteSilent();
    }

    if (cpu.pc == 0x0D6E)
    {
        Logger::UnmuteSilent();
        LOGINFO("CLS-LOWER is executed. PC: %04X", cpu.pc);
        Logger::MuteSilent();
    }

    if (cpu.pc == 0x0ADC)
    {
        Logger::UnmuteSilent();
        LOGINFO("PO-STORE is executed. PC: %04X", cpu.pc);
        Logger::MuteSilent();
    }

    if (cpu.pc == 0x0C0A)
    {
        Logger::UnmuteSilent();
        LOGINFO("PO-MSG is executed. PC: %04X", cpu.pc);
        Logger::MuteSilent();
    }

    if (cpu.pc == 0x12A9)
    {
        Logger::UnmuteSilent();
        LOGINFO("MAIN-1 is executed. PC: %04X", cpu.pc);
        Logger::MuteSilent();
    }

    if (cpu.pc == 0x1303)
    {
        Logger::UnmuteSilent();
        LOGINFO("MAIN-4 is executed. PC: %04X", cpu.pc);
        LOGWARNING("HALT!!!");
    }

    if (cpu.pc == 0x1634)
    {
        Logger::UnmuteSilent();
        LOGINFO("CHAN-K is executed. PC: %04X", cpu.pc);
        Logger::MuteSilent();
    }

    if (cpu.pc == 0x0C0A)
    {
        Logger::UnmuteSilent();
        LOGINFO("PO-MSG is executed. PC: %04X", cpu.pc);
        Logger::MuteSilent();
    }
    */

	/// Spectrum 128K names
	if (cpu.pc == 0x0000)
    {
	    Logger::MuteSilent();
        cycles_to_capture = -1;
    }

	if (cpu.pc == 0x1F20)
    {
        Logger::UnmuteSilent();
        LOGINFO("USE_NORMAL_RAM_CONFIG is executed. PC: %04X", cpu.pc);
        Logger::MuteSilent();
    }

    if (cpu.pc == 0x00C7)
    {
        Logger::UnmuteSilent();
        LOGINFO("INIT1 is executed. PC: %04X", cpu.pc);
        Logger::MuteSilent();
    }

    if (cpu.pc == 0x0137)
    {
        Logger::UnmuteSilent();
        LOGINFO("INIT2 is executed. PC: %04X", cpu.pc);
        Logger::MuteSilent();
    }

    if (cpu.pc == 0x019D)
    {
        Logger::UnmuteSilent();
        LOGINFO("NEW is executed. PC: %04X", cpu.pc);
        Logger::MuteSilent();
    }

    if (cpu.pc == 0x1C64)
    {
        Logger::UnmuteSilent();
        LOGINFO("SET_RAM_PAGE is executed. PC: %04X", cpu.pc);
        Logger::MuteSilent();
    }

    if (cpu.pc == 0x018A)
    {
        Logger::UnmuteSilent();
        LOGWARNING("RST #28 call! PC: %04X", cpu.pc);
        LOGWARNING("CPU cycles: %d", cpu.cycle_count);
        Logger::MuteSilent();
    }

    if (cpu.pc == 0x5B00)
    {
        //cycles_to_capture = 400;

        Logger::UnmuteSilent();
        LOGINFO("RAM_SWAP_5B00 is executed. PC: %04X", cpu.pc);
        Logger::MuteSilent();
    }

    if (cpu.pc == 0x259F)
    {
        //cycles_to_capture = 400;

        Logger::UnmuteSilent();
        LOGINFO("MAINMENU is executed. PC: %04X", cpu.pc);
        Logger::MuteSilent();
    }

    if (cpu.pc == 0x36A8)
    {
        //cycles_to_capture = 400;

        Logger::UnmuteSilent();
        LOGINFO("DISPLAY_MENU is executed. PC: %04X", cpu.pc);
        Logger::MuteSilent();
    }

    if (cpu.pc == 0x3719)
    {
        //cycles_to_capture = 400;

        Logger::UnmuteSilent();
        LOGINFO("PLOT_LINE is executed. PC: %04X", cpu.pc);
        Logger::MuteSilent();
    }

    if (cycles_to_capture > 0)
    {
        cycles_to_capture--;
    }
    else if (cycles_to_capture == 0)
    {
        Logger::MuteSilent();
        //exit(1);
    }

	/// endregion </Test>

	// Record PC for current opcode (prefixes should not alter original PC)
	if (prefix == 0x0000)
        m1_pc = cpu.pc;

	// Z80 CPU M1 cycle logic
	r_low++;
	opcode = rd(cpu.pc);                      // Keep opcode copy for trace / debug purposes

	// Point PC to next byte
	cpu.pc++;

	// Align 14MHz CPU memory request to 7MHz DRAM cycle
	// request can be satisfied only in the next DRAM cycle
	if (config.mem_model == MM_TSL && state.ts.cache_miss && cpu.rate == 0x40)
	{
		cpu.tt = (cpu.tt + 0x40 * 7) & 0xFFFFFF80;
		cycle_count += 8; // TODO: verify
	}
	else
		IncrementCPUCyclesCounter(1);	// M1 cycle is always 4 CPU clocks (3 for memory read and 1 for decoding)

	return opcode;
}

/// Dispatching memory read method. Used directly from Z80 microcode (CPULogic and opcode)
/// Read access to memory takes 3 clock cycles
/// \param addr
/// \return
uint8_t Z80::rd(uint16_t addr)
{
	IncrementCPUCyclesCounter(3);

	return (_memory->*MemIf->MemoryRead)(addr);
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
    static PortDecoder& portDecoder = *_context->pPortDecoder;

    // Let model-specific decoder to process port output
	uint8_t result = portDecoder.DecodePortIn(port, m1_pc);

	return result;
}

void Z80::out(uint16_t port, uint8_t val)
{
    static PortDecoder& portDecoder = *_context->pPortDecoder;

    // Let model-specific decoder to process port output
    portDecoder.DecodePortOut(port, val, m1_pc);
}

void Z80::retn()
{
}

/// endregion </Z80 lifecycle>

// Note: Only TSConf supports interrupt vectors
uint8_t Z80::InterruptVector()
{
    static Z80& _cpu_state = *this;
    static CONFIG& config = _context->config;
    static COMPUTER& state = _context->state;

    uint8_t result = 0xFF;

    _cpu_state.tt += _cpu_state.rate * 3; // Skip 3 CPU cycles before reading INT vector

    if (config.mem_model == MM_TSL)
    {
        // Note: Only TSConf supports interrupt vectors

        // check status of frame INT
        //ts_frame_int(state.ts.vdos || state.ts.vdos_m1);

        if (state.ts.intctrl.frame_pend)
            return state.ts.im2vect[INT_FRAME];

        else if (state.ts.intctrl.line_pend)
            return state.ts.im2vect[INT_LINE];

        else if (state.ts.intctrl.dma_pend)
            return state.ts.im2vect[INT_DMA];

        else
            return 0xFF;
    }
    else
    {
        // Simulate random noise on data bus
        // Getting CPU time counter for that
        if (state.flags & CF_Z80FBUS)
            result = (uint8_t)(rdtsc() & 0xFF);
    }

    return result;
}

//
// Read byte directly from ZX-Spectrum memory (current memory bank setup used)
// No cycle counters will be incremented
//
uint8_t Z80::DirectRead(uint16_t addr)
{
	uint8_t* remap_addr = _context->pMemory->RemapAddressToCurrentBank(addr);
	
	return *remap_addr;
}

//
// Write byte directly to RAM memory buffer
// No checks for ROM write access flags
// No cycle counters will be incremented
//
void Z80::DirectWrite(uint16_t addr, uint8_t val)
{
	uint8_t* remap_addr = _context->pMemory->RemapAddressToCurrentBank(addr);
	*remap_addr = val;

	// Update TSConf cache data
	// TODO: move to plugin
    static CONFIG& config = _context->config;
    if (config.mem_model == MM_TSL)
    {
        Z80 &_cpu_state = *this;
        uint16_t cache_pointer = addr & 0x1FF;
        _cpu_state.tscache_addr[cache_pointer] = -1; // MemoryWrite invalidates flag
    }
}

void Z80::Z80FrameCycle()
{
	static Z80& cpu = *this;
	static COMPUTER& state = _context->state;
	static CONFIG& config = _context->config;
	static VideoControl& video = _context->pScreen->_vid;

	// TODO: Move to platform plugin
	// Was:
	// void z80loop()
	// {
	//	if (conf.mem_model == MM_TSL)
	//		z80loop_TSL();
	//	else
	//		z80loop_other();
	// }
	if (config.mem_model == MM_TSL)
	{
		cpu.haltpos = 0;
		// comp.ts.intctrl.line_t = comp.ts.intline ? 0 : VID_TACTS;
		state.ts.intctrl.line_t = 0;

		while (cpu.t < config.frame)
		{
			bool vdos = state.ts.vdos || state.ts.vdos_m1;

			ts_frame_int(vdos);
			ts_line_int(vdos);
			ts_dma_int(vdos);

			video.memcyc_lcmd = 0; // new command, start accumulate number of busy memcycles

			if (state.ts.intctrl.pend && cpu.iff1 && (cpu.t != cpu.eipos) && !vdos) // int disabled in vdos after r/w vg ports
			{
				HandleINT(InterruptVector());
			}

			Z80Step();
			UpdateScreen(); // Reflect state changes on screen, TSU, DMA
		}

		return;
	}

	// All non-TSConf platforms behavior

	// Video Interrupt position calculation
	bool int_occurred = false;
	unsigned int_start = config.intstart;
	unsigned int_end = config.intstart + config.intlen;

	cpu.haltpos = 0;

	// INT interrupt handling lasts for more than 1 frame
	if (int_end >= config.frame)
	{
		int_end -= config.frame;
		cpu.int_pend = true;
		int_occurred = true;
	}

	// Cover whole frame (control by clock cycles)
	while (cpu.t < config.frame)
	{
		// Handle interrupts if arrived
		ProcessInterrupts(int_occurred, int_start, int_end);

		// Perform single Z80 command cycle
		Z80Step();

		// Update screen buffer (including border draw)
		UpdateScreen();
	}
}

//
// Handle Z80 reset signal
//
void Z80::Reset()
{
	// Emulation state
	last_branch = 0x0000;	        // Address of last branch (in Z80 address space)
	int_pend = false;		        // No interrupts pending
	int_gate = true;		        // Allow external interrupts
	cycle_count = 0;		        // Cycle counter
	tt = 0;					        // Scaled to CPU frequency multiplier cycle count

	// Z80 chip reset sequence. See: http://www.z80.info/interrup.htm (Reset Timing section)
	int_flags = 0;					// Set interrupt mode 0
	ir_ = 0;						// Reset IR (Instruction Register)
	pc = 0x0000;					// Reset PC (Program Counter)
	im = 0;							// IM0 mode is set by default
	sp = 0xFFFF;					// Stack pointer set to the end of memory address space
	af = 0xFFFF;					// Real chip behavior

    // All that takes 3 clock cycles
	IncrementCPUCyclesCounter(3);
}

//
// Single CPU command cycle (non-interruptable)
//
void Z80::Z80Step()
{
	static Z80& cpu = *this;
	static CONFIG& config = _context->config;
	static COMPUTER& state = _context->state;
	static TEMP& temporary = _context->temporary;
	static Memory& memory = *_context->pMemory;

	// Let debugger process step event
	ProcessDebuggerEvents();

	/// region  <Ports logic>
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


    /// region <Tape related>
	// Tape related IO
	// TODO: Move to io/tape
	/*
	if (state.tape.play_pointer && config.tape_traps && (cpu.pc & 0xFFFF) == 0x056B)
		tape_traps();

	if (state.tape.play_pointer && !config.sound.enabled)
		fast_tape();
	*/

    /// endregion </Tape related>

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

		// Regular Z80 bus cycle
		// 1. Fetch opcode (Z80 M1 bus cycle)
        cpu.prefix = 0x0000;
        cpu.opcode = m1_cycle();

		// 2. Emulate fetched Z80 opcode
		(normal_opcode[opcode])(&cpu);
	}

	// Trace CPU for all duration of cycles requested
	if (cycles_to_capture > 0)
    {
        static char buffer[1024];
        DumpZ80State(buffer, sizeof (buffer) / sizeof (buffer[0]));
        LOGINFO(buffer);
    }

	/// region <Extra debug validations
#ifdef _DEBUG
	// Sanity check for register corruption
	if (bc > 0xFFFF || de > 0xFFFF || hl > 0xFFFF || ix > 0xFFFF || iy > 0xFFFF || sp > 0xFFFF)
    {
        static char buffer[1024];
        DumpZ80State(buffer, sizeof (buffer) / sizeof (buffer[0]));
        LOGERROR(buffer);
	    LOGERROR("Main register(s) corrupted");
	    exit(1);
    }

	if (alt.bc > 0xFFFF || alt.de > 0xFFFF || alt.hl > 0xFFFF)
    {
        static char buffer[1024];
        DumpZ80State(buffer, sizeof (buffer) / sizeof (buffer[0]));
        LOGERROR(buffer);
        LOGERROR("Alternative register(s) corrupted");
        exit(1);
    }
#endif // _DEBUG
    /// endregion </Extra debug validations

	/* [vv]
	//todo if (comp.turbo)cpu.t-=tbias[cpu.t-oldt]
	   if ( ((conf.mem_model == MM_PENTAGON) && ((comp.pEFF7 & EFF7_GIGASCREEN)==0)) ||
		   ((conf.mem_model == MM_ATM710) && (comp.pFF77 & 8)))
		   cpu.t -= (cpu.t-oldt) >> 1; //0.37
	//~todo
	*/
#ifdef Z80_DBG
	if ((comp.flags & CF_PROFROM) && ((membits[0x100] | membits[0x104] | membits[0x108] | membits[0x10C]) & MEMBITS_R))
	{
		if (membits[0x100] & MEMBITS_R)
			set_scorp_profrom(0);
		if (membits[0x104] & MEMBITS_R)
			set_scorp_profrom(1);
		if (membits[0x108] & MEMBITS_R) 
			set_scorp_profrom(2);
		if (membits[0x10C] & MEMBITS_R)
			set_scorp_profrom(3);
	}
#endif
}

void Z80::ProcessInterrupts(bool int_occurred, unsigned int_start, unsigned int_end)
{
	static Z80& cpu = *this;
	static CONFIG& config = _context->config;
	static COMPUTER& state = _context->state;
	static VideoControl& video = _context->pScreen->_vid;

	// Baseconf NMI trap
	if (config.mem_model == MM_ATM3 && (state.pBF & 0x10) && (cpu.pc == state.pBD))
		_nmi_pending_count = 1;

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
	} // end if (nmi_pending)

	// Baseconf NMI
	if (state.pBE)
	{
	    /*
		if (config.mem_model == MM_ATM3 && state.pBE == 1)
		{
			cpu.nmi_in_progress = false;
			SetBanks();
		}
	     */
		state.pBE--;
	}

	// Reset INT
	if (!int_occurred && cpu.t >= int_start)
	{
		int_occurred = true;
		cpu.int_pend = true;
	}

	if (cpu.int_pend && (cpu.t >= int_end))
		cpu.int_pend = false;

	video.memcyc_lcmd = 0;							// new command, start accumulate number of busy memcycles

	// INT
	if (cpu.int_pend && cpu.iff1 &&	// INT enabled in CPU
		(cpu.t != cpu.eipos) &&		// INT disabled after EI
		cpu.int_gate)						// INT enabled by ATM hardware (no INT disabling in PentEvo)
	{
		HandleINT(InterruptVector());
	}
}

void Z80::UpdateScreen()
{
	_context->pScreen->UpdateScreen();
}

void Z80::HandleNMI(ROMModeEnum mode)
{
	Z80& cpu = *this;
}

void Z80::HandleINT(uint8_t vector)
{
	static Z80& cpu = *this;
	static CONFIG& config = _context->config;
	static COMPUTER& state = _context->state;

	unsigned interrupt_handler_address;
	if (cpu.im < 2)
	{
		interrupt_handler_address = 0x38;
	}
	else
	{
		// IM2
		uint16_t vectorAddress = vector + cpu.i * 0x100;
		interrupt_handler_address = rd(vectorAddress) + 0x100 * rd(vectorAddress + 1);
	}

	if (DirectRead(cpu.pc) == 0x76) // If interrupt occurs on HALT command (opcode 0x76)
		cpu.pc++;

	IncrementCPUCyclesCounter(((cpu.im < 2) ? 13 : 19) - 3);

	// Push return address to stack
	uint16_t sp = cpu.sp;
	wd(--sp, cpu.pch);
	wd(--sp, cpu.pcl);
	cpu.sp = sp;

	cpu.pc = interrupt_handler_address;
	cpu.memptr = interrupt_handler_address;
	cpu.halted = 0;
	cpu.iff1 = 0;
	cpu.iff2 = 0;
	cpu.int_pend = false;

	// TODO: move to TSConf plugin
	if (config.mem_model == MM_TSL)
	{
		if (state.ts.intctrl.frame_pend) state.ts.intctrl.frame_pend = 0;
		else
			if (state.ts.intctrl.line_pend)
				state.ts.intctrl.line_pend = 0;
			else
				if (state.ts.intctrl.dma_pend)
					state.ts.intctrl.dma_pend = 0;
	}
}

// Debugger trigger updates
void Z80::ProcessDebuggerEvents()
{
	// TODO: integrate with CPUDebugger class
	// If in debug session - call it's callback. Debugger should handle all events / breakpoints by itself
}

//
// Increment CPU cycles counter by specified number of cycles.
// Required to keep exact timings for Z80 commands
// Note: same as '#define cputact(a)	cpu->tt += ((a) * cpu->rate); cpu->cycle_count += (a)' macro defined in cpulogic.h
//
void Z80::IncrementCPUCyclesCounter(uint8_t cycles)
{
	tt += cycles * rate;
	cycle_count += cycles;
}

/// region <TSConf specific>

// TODO: Move to adapter
void Z80::ts_frame_int(bool vdos)
{
	static Z80& _cpu_state = *this;
	static CONFIG& config = _context->config;
	static COMPUTER& state = _context->state;

	if (!state.ts.intctrl.frame_pend)
	{
		bool f1 = (_cpu_state.t - state.ts.intctrl.frame_t) < state.ts.intctrl.frame_len; // INT signal in current frame
		bool f2 = (state.ts.intctrl.frame_t + state.ts.intctrl.frame_len) > config.frame; // INT signal is transferred from the previous frame
		bool new_frame = _cpu_state.t < state.ts.intctrl.last_cput; // is it new frame ? - !!! check this !!!

		if (f1 || (f2 && new_frame))
		{
			state.ts.intctrl.frame_pend = state.ts.intframe;
			state.ts.intctrl.frame_cnt = _cpu_state.t - state.ts.intctrl.frame_t + (f1 ? 0 : config.frame);
		}
	}
	else if (vdos) { /* No Operation */ }
	else if (state.ts.intctrl.frame_pend && ((state.ts.intctrl.frame_cnt + (_cpu_state.t - state.ts.intctrl.last_cput)) < state.ts.intctrl.frame_len))
	{
		state.ts.intctrl.frame_cnt += (_cpu_state.t - state.ts.intctrl.last_cput);
	}
	else
		state.ts.intctrl.frame_pend = false;

	state.ts.intctrl.last_cput = _cpu_state.t;
}

void Z80::ts_line_int(bool vdos)
{
	Z80& _cpu_state = *this;
	COMPUTER& state = _context->state;

	if (_cpu_state.t >= state.ts.intctrl.line_t)
	{
		state.ts.intctrl.line_t += VID_TACTS;
		bool pre_pend;

		// Disabled until vdac2 logic restored
		//if (state.ts.vdac2 && state.ts.ft_en)
		//	pre_pend = vdac2::process_line();
		//else
			pre_pend = true;

		if (!vdos)
			state.ts.intctrl.line_pend = pre_pend && state.ts.intline; // !!! incorrect behaviour, pending flag must be processed after VDOS
	}
}

void Z80::ts_dma_int(bool vdos)
{
	static COMPUTER& state = _context->state;

	if (state.ts.intctrl.new_dma)
	{
		state.ts.intctrl.new_dma = false;
		state.ts.intctrl.dma_pend = state.ts.intdma;   // !!! possibly incorrect behaviour (VDOS)
	}
}

/// endregion </TSConf specific>

/// region <Debug methods>
#ifdef _DEBUG
#include <cstdio>

void Z80::DumpCurrentState()
{
    static char dumpBuffer[512];

    int pos = 0;
    pos += snprintf(dumpBuffer + pos, sizeof dumpBuffer, "Cycle:%d\r\n", cycle_count);
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

void Z80::DumpZ80State(char* buffer, size_t len)
{
    std::string annotation;

    // If we were executing in memory and jumped to ROM - we need to highlight what ROM page was used
    if (prev_pc >= 0x4000 && m1_pc < 0x4000)
    {
        annotation = StringHelper::Format(" <-- ROM%d", _context->pMemory->GetROMPage());
    }

    // If we were executing in ROM or fixed RAM pages and jumped to RAM Bank 3 - we need to highlight what RAM page was used
    if (prev_pc < 0xC000 && m1_pc >= 0xC000)
    {
        annotation = StringHelper::Format(" <-- RAM%d", _context->pMemory->GetRAMPageForBank3());
    }
    else if (prev_pc < 0x4000 && m1_pc >= 0x4000)
    {
        annotation = StringHelper::Format(" <-- RAM%d", _context->pMemory->GetRAMPageFromAddress(_context->pMemory->RemapAddressToCurrentBank(m1_pc)));
    }

    if (prefix > 0)
    {
        snprintf(buffer, len,
                 "Pr: 0x%04X Op: 0x%02X PC: 0x%04X AF: 0x%04X BC: 0x%04X DE: 0x%04X HL: 0x%04X IX: %04X IY: %04X SP: %04X IR: %04X clock: %04X%s",
                 prefix, opcode, m1_pc, af, bc, de, hl, ix, iy, sp, ir_, t, annotation.c_str());
    }
    else
    {
        snprintf(buffer, len,
                 "           Op: 0x%02X PC: 0x%04X AF: 0x%04X BC: 0x%04X DE: 0x%04X HL: 0x%04X IX: %04X IY: %04X SP: %04X IR: %04X clock: %04X%s",
                 opcode, m1_pc, af, bc, de, hl, ix, iy, sp, ir_, t, annotation.c_str());
    }
}
#endif
/// endregion </Debug methods>