#include "stdafx.h"

#include "common/logger.h"

#include "z80.h"
#include "common/stringhelper.h"
#include "emulator/cpu/op_noprefix.h"
#include "emulator/video/screen.h"

Z80::Z80(EmulatorContext* context)
{
	_context = context;

	MemIf = FastMemIf;
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

	// Initialize memory access interfaces
	FastMemIf = new MemoryInterface(&Z80::MemoryReadFast, &Z80::MemoryWriteFast);
	DbgMemIf = new MemoryInterface(&Z80::MemoryReadDebug, &Z80::MemoryWriteDebug);
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

uint8_t Z80::m1_cycle()
{
	Z80& cpu = *this;
	CONFIG& config = _context->config;
	COMPUTER& state = _context->state;
	TEMP& temporary = _context->temporary;

	// TODO: move to Ports class
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
	// End of TODO: move to Ports class

	// Z80 CPU M1 cycle logic
	r_low++;
	opcode = rd(cpu.pc++);

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

// Note: Only TSConf supports interrupt vectors
uint8_t Z80::InterruptVector()
{
	Z80& _cpu_state = *this;
	CONFIG& config = _context->config;
	COMPUTER& state = _context->state;

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

// TODO: Obsolete method - refactor
//
// Dispatching memory read method. Used directly from Z80 microcode (CPULogic and opcode)
// Read access to memory takes 3 clock cycles
//
uint8_t Z80::rd(uint16_t addr)
{
	Z80& cpu = *this;

	IncrementCPUCyclesCounter(3);

	return (cpu.*MemIf->MemoryRead)(addr);
}

// TODO: Obsolete method - refactor
//
// Dispatching memory write method. Used directly from Z80 microcode (CPULogic and opcode)
// Write access to memory takes 3 clock cycles
//
void Z80::wd(uint16_t addr, uint8_t val)
{
	Z80& cpu = *this;

	IncrementCPUCyclesCounter(3);

	(cpu.*MemIf->MemoryWrite)(addr, val);
}

uint8_t Z80::in(uint16_t port)
{
	uint8_t result = 0xFF;

	return result;
}

void Z80::out(uint16_t port, uint8_t val)
{
}

void Z80::retn()
{
}

//
// Implementation memory read method. Used from FastMemIf.
//
uint8_t Z80::MemoryReadFast(uint16_t addr)
{
	CONFIG& config = _context->config;
	COMPUTER& state = _context->state;
	VideoControl& video = _context->pScreen->_vid;
	Memory& memory = *_context->pMemory;

	uint8_t result = 0xFF;

	// Determine CPU bank (from address bits 14 and 15)
	uint8_t bank = (addr >> 14) & 0x03;

	// TSConf caching logic
	// TODO: move to TSConf plugin
	if (config.mem_model == MM_TSL)
	{
		if (memory._bank_mode[bank] == MemoryBankModeEnum::BANK_RAM)
		{
			// Pentevo version for 16 bit DRAM/cache
			unsigned cached_address = (state.ts.page[bank] << 5) | ((addr >> 9) & 0x1F);  // {page[7:0], addr[13:9]}
			uint16_t cache_pointer = addr & 0x1FF;  // addr[8:0]
			state.ts.cache_miss = !(state.ts.cacheconf & (1 << bank)) || (tscache_addr[cache_pointer] != cached_address);

			if (state.ts.cache_miss)
			{
				// Read two sequential bytes with address bits xxxxxx00 and xxxxxx01 and cache them
				uint16_t cache_addr = addr & 0xFFFE;
				tscache_data[cache_pointer & 0xFFFE] = *(memory._bank_read[bank] + (unsigned)(cache_addr & (PAGE - 1)));

				cache_addr = addr | 0x0001;
				tscache_data[cache_pointer | 0x0001] = *(memory._bank_read[bank] + (unsigned)(cache_addr & (PAGE - 1)));

				tscache_addr[cache_pointer & 0xFFFE] = tscache_addr[cache_pointer | 0x0001] = cached_address;     // Set cache tags for two subsequent 8-bit addresses

				// Update counters
				video.memcpucyc[t / 224]++;
				video.memcyc_lcmd++;
			}

			result = tscache_data[cache_pointer];
		}
		else
		{
			state.ts.cache_miss = false;
		}
	}
	else
	{
		// Read byte from correspondent memory bank mapped to global memory buffer
		result = *(memory._bank_read[bank] + (unsigned)(addr & (PAGE - 1)));

		// Update RAM access counters
		if (memory._bank_mode[bank] == MemoryBankModeEnum::BANK_RAM)
		{
			video.memcpucyc[t % 224]++;
		}
	}

	return result;
}

//
// Implementation memory read method. Used from DbgMemIf.
//
uint8_t Z80::MemoryReadDebug(uint16_t addr)
{
	// Mark memory cell as accessed on read
	uint8_t* membit = _membits + (unsigned)addr;
	*membit |= MEMBITS_R;
	dbgbreak |= (*membit & MEMBITS_BPR);

	// Fetch data from memory
	uint8_t result = MemoryReadFast(addr);

	// Check for breakpoint conditions
	//brk_mem_rd = addr;
	//brk_mem_val = result;
	//debug_cond_check(&cpu);		// Debug conditions check is very slow

	return result;
}

//
// Implementation memory write method. Used from FastMemIf.
//
void Z80::MemoryWriteFast(uint16_t addr, uint8_t val)
{
	CONFIG& config = _context->config;
	COMPUTER& state = _context->state;
	TEMP& temporary = _context->temporary;
	VideoControl& video = _context->pScreen->_vid;
	Memory& mem = *_context->pMemory;

	// Determine CPU bank (from address bits 14 and 15)
	uint8_t bank = (addr >> 14) & 0x03;

	// TSConf caching logic
	// TODO: move to TSConf plugin
	if (config.mem_model == MM_TSL)
	{
		if (state.ts.fm_en && (((addr >> 12) & 0x0F) == state.ts.fm_addr))
		{
			// 256 byte arrays
			// if (((addr >> 8) & 0x0F) == TSF_REGS)
			//		ts_ext_port_wr(addr & 0xFF, val);
		}
		else
		{
			// 512 byte arrays
			if (addr & 1)
			{
				switch ((addr >> 9) & 0x07)
				{
					case TSF_CRAM:
					{
						state.cram[(addr >> 1) & 0xFF] = ((val << 8) | temporary.fm_tmp);
						//update_clut((addr >> 1) & 0xFF);
						break;
					}

					// 
					case TSF_SFILE:
					{
						state.sfile[(addr >> 1) & 0xFF] = (val << 8) | temporary.fm_tmp;
						break;
					}
				}
			}
			else
				// remember temp value
				temporary.fm_tmp = val;
		}

		// Pentevo version for 16 bit DRAM/cache
		uint16_t cache_pointer = addr & 0x1FE;
		tscache_addr[cache_pointer] = tscache_addr[cache_pointer + 1] = -1;    // invalidate two 8-bit addresses

		// Update RAM access counters
		video.memcpucyc[t / 224]++;
	}
	else
	{
		// ATM3 specific
		// Intercept writing to font area
		// TODO: Move to ATM3 plugin
		if ((config.mem_model == MM_ATM3) && (state.pBF & 0x04) /*&& ((addr & 0xF800) == 0)*/) // Font loading enabled for ATM3 // lvd: any addr is possible in ZXEVO
		{
			unsigned idx = ((addr & 0x07F8) >> 3) | ((addr & 7) << 8);
			//fontatm2[idx] = val;
			return;
		}
	}

	// Write byte to correspondent memory bank cell
	uint8_t* bank_addr = mem._bank_write[bank];
	*(bank_addr + (unsigned)addr) = val;
}

//
// Implementation memory write method. Used from DbgMemIf.
//
void Z80::MemoryWriteDebug(uint16_t addr, uint8_t val)
{
	// Mark memory cell as accessed on write
	uint8_t* membit = _membits + (addr & 0xFFFF);
	*membit |= MEMBITS_W;
	dbgbreak |= (*membit & MEMBITS_BPW);

	// Write data to memory
	MemoryWriteFast(addr, val);

	// Check for breakpoint conditions
	//brk_mem_wr = addr;
	//brk_mem_val = val;
	//debug_cond_check(&cpu);		// Debug conditions check is very slow
}

/*
uint8_t Z80::Read(uint16_t addr)
{
	Z80& _cpu_state = *this;
	COMPUTER& state = _context->state;

	uint8_t result = rd(addr);

	// Align 14MHz CPU memory request to 7MHz DRAM cycle
	// request can be satisfied only in the next DRAM cycle
	if (state.ts.cache_miss && _cpu_state.rate == 0x40)
		_cpu_state.tt += (_cpu_state.tt & 0x40) ? 0x40 * 6 : 0x40 * 5;
	else
		_cpu_state.tt += _cpu_state.rate * 3;

	return result;
}

void Z80::Write(uint16_t addr, uint8_t val)
{

}
*/

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
	Z80& _cpu_state = *this;
	uint8_t* remap_addr = _context->pMemory->RemapAddressToCurrentBank(addr);
	*remap_addr = val;

	// Update TSConf cache data
	// TODO: move to plugin
	uint16_t cache_pointer = addr & 0x1FF;
	_cpu_state.tscache_addr[cache_pointer] = -1; // MemoryWrite invalidates flag
}

void Z80::Z80FrameCycle()
{
	Z80& _cpu_state = *this;
	COMPUTER& state = _context->state;
	CONFIG& config = _context->config;
	VideoControl& video = _context->pScreen->_vid;

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
		_cpu_state.haltpos = 0;
		// comp.ts.intctrl.line_t = comp.ts.intline ? 0 : VID_TACTS;
		state.ts.intctrl.line_t = 0;

		while (_cpu_state.t < config.frame)
		{
			bool vdos = state.ts.vdos || state.ts.vdos_m1;

			ts_frame_int(vdos);
			ts_line_int(vdos);
			ts_dma_int(vdos);

			video.memcyc_lcmd = 0; // new command, start accumulate number of busy memcycles

			if (state.ts.intctrl.pend && _cpu_state.iff1 && (_cpu_state.t != _cpu_state.eipos) && !vdos) // int disabled in vdos after r/w vg ports
			{
				HandleINT(InterruptVector());
			}

			Z80Step();
			UpdateScreen(); // update screen, TSU, DMA
		}

		return;
	}

	// All non-TSConf platforms behavior

	// Video Interrupt position calculation
	bool int_occurred = false;
	unsigned int_start = config.intstart;
	unsigned int_end = config.intstart + config.intlen;

	_cpu_state.haltpos = 0;

	// INT interrupt handling lasts for more than 1 frame
	if (int_end >= config.frame)
	{
		int_end -= config.frame;
		_cpu_state.int_pend = true;
		int_occurred = true;
	}

	while (_cpu_state.t < config.frame)
	{
		// Baseconf NMI trap
		if (config.mem_model == MM_ATM3 && (state.pBF & 0x10) && (_cpu_state.pc == state.pBD))
			_nmi_pending_count = 1;

		// NMI processing
		if (_nmi_pending_count > 0)
		{
			if (config.mem_model == MM_ATM3)
			{
				_nmi_pending_count = 0;
				_cpu_state.nmi_in_progress = true;

				SetBanks();
				HandleNMI(RM_NOCHANGE);
				continue;
			}
			else if (config.mem_model == MM_PROFSCORP || config.mem_model == MM_SCORP)
			{
				_nmi_pending_count--;
				if (_cpu_state.pc > 0x4000)
				{
					HandleNMI(RM_DOS);
					_nmi_pending_count = 0;
				}
			}
			else
				_nmi_pending_count = 0;
		} // end if (nmi_pending)

		// Baseconf NMI
		if (state.pBE)
		{
			if (config.mem_model == MM_ATM3 && state.pBE == 1)
			{
				_cpu_state.nmi_in_progress = false;
				SetBanks();
			}
			state.pBE--;
		}

		// Reset INT
		if (!int_occurred && _cpu_state.t >= int_start)
		{
			int_occurred = true;
			_cpu_state.int_pend = true;
		}

		if (_cpu_state.int_pend && (_cpu_state.t >= int_end))
			_cpu_state.int_pend = false;

		video.memcyc_lcmd = 0;							// new command, start accumulate number of busy memcycles

		// INT
		if (_cpu_state.int_pend && _cpu_state.iff1 &&	// INT enabled in CPU
			(_cpu_state.t != _cpu_state.eipos) &&		// INT disabled after EI
			_cpu_state.int_gate)						// INT enabled by ATM hardware (no INT disabling in PentEvo)
		{
			HandleINT(InterruptVector());
		}

		Z80Step();

		UpdateScreen(); // update screen, TSU, DMA
	} // end while (cpu.t < conf.intlen)
}

//
// Handle Z80 reset signal
//
void Z80::Reset()
{
	Z80* cpu = this;

	// Emulation state
	last_branch = 0x0000;	// Address of last branch (in Z80 address space)
	int_pend = false;		// No interrupts pending
	int_gate = true;		// Allow external interrupts
	cycle_count = 0;		// Cycle counter
	tt = 0;					// Scaled to CPU frequency multiplier cycle count

	// Z80 chip reset sequence. See: http://www.z80.info/interrup.htm (Reset Timing section)
	int_flags = 0;					// Set interrupt mode 0
	ir_ = 0;						// Reset IR (Instruction Register)
	pc = 0x0000;					// Reset PC (Program Counter)
	im = 0;							// IM0 mode is set by defaule
	sp = 0xFFFF;					// Stack pointer set to the end of memory address space
	af = 0xFFFF;					// Real chip behavior
	IncrementCPUCyclesCounter(3);	// All that takes 3 clock cycles
}

//
// Single CPU command cycle (non-interruptable)
//
void Z80::Z80Step()
{
	Z80& cpu = *this;
	CONFIG& config = _context->config;
	COMPUTER& state = _context->state;
	TEMP& temporary = _context->temporary;
	Memory& memory = *_context->pMemory;

	// Let debugger process step event
	ProcessDebuggerEvents();

	// Ports logic
	// TODO: move to Ports class
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

	// Tape related IO
	// TODO: Move to io/tape
	/*
	if (state.tape.play_pointer && config.tape_traps && (cpu.pc & 0xFFFF) == 0x056B)
		tape_traps();

	if (state.tape.play_pointer && !config.sound.enabled)
		fast_tape();
	*/

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

		// Regular Z80 bus cycle
		// 1. Fetch opcode (Z80 M1 bus cycle)
		uint8_t opcode = m1_cycle();

		// Debug
		//DumpCurrentState();

		// 2. Emulate fetched Z80 opcode
		(normal_opcode[opcode])(&cpu);
	}

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

void Z80::SetBanks()
{
	// Let memory manager set it up
	_context->pMemory->SetBanks();
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
	Z80& cpu = *this;
	CONFIG& config = _context->config;
	COMPUTER& state = _context->state;

	unsigned interrupt_handler_address;
	if (cpu.im < 2)
	{
		interrupt_handler_address = 0x38;
	}
	else
	{
		// IM2
		unsigned vec = vector + cpu.i * 0x100;
		interrupt_handler_address = rd(vec) + 0x100 * rd(vec + 1);
	}

	if (DirectRead(cpu.pc) == 0x76) // If interrupt occurs on HALT command (opcode 0x76)
		cpu.pc++;

	IncrementCPUCyclesCounter(((cpu.im < 2) ? 13 : 19) - 3);

	// Push return address to stack
	wd(--cpu.sp, cpu.pch);
	wd(--cpu.sp, cpu.pcl);

	cpu.pc = interrupt_handler_address;
	cpu.memptr = interrupt_handler_address;
	cpu.halted = 0;
	cpu.iff1 = cpu.iff2 = 0;
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

	wstring message = StringHelper::StringToWideString(dumpBuffer);
#ifdef _WIN32
	OutputDebugString(message.c_str());
#endif
}

// TSConf specific
// TODO: Move to adapter
void Z80::ts_frame_int(bool vdos)
{
	Z80& _cpu_state = *this;
	CONFIG& config = _context->config;
	COMPUTER& state = _context->state;

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
	COMPUTER& state = _context->state;

	if (state.ts.intctrl.new_dma)
	{
		state.ts.intctrl.new_dma = false;
		state.ts.intctrl.dma_pend = state.ts.intdma;   // !!! possibly incorrect behaviour (VDOS)
	}
}