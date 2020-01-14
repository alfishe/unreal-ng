#include "stdafx.h"

#include "z80.h"
#include "emulator/cpu/op_noprefix.h"

Z80::Z80(EmulatorContext* context)
{
	_context = context;
}

uint8_t Z80::m1_cycle()
{
	Z80& _cpu_state = *this;
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
	uint8_t opcode = _cpu_state.MemIf->read(_cpu_state.pc++);

	// Align 14MHz CPU memory request to 7MHz DRAM cycle
	// request can be satisfied only in the next DRAM cycle
	if (state.ts.cache_miss && _cpu_state.rate == 0x40)
		_cpu_state.tt = (_cpu_state.tt + 0x40 * 7) & 0xFFFFFF80;
	else
		_cpu_state.tt += _cpu_state.rate * 4;

	_cpu_state.opcode = opcode;
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
uint8_t Z80::rd(unsigned addr)
{
	Z80& _cpu_state = *this;

	_cpu_state.tt += rate * 3;
	return MemIf->read(addr);
}

// TODO: Obsolete method - refactor
void Z80::wd(unsigned addr, uint8_t val)
{
	Z80& _cpu_state = *this;

	tt += rate * 3;
	MemIf->write(addr, val);
}

uint8_t Z80::Read(uint16_t addr)
{
	Z80& _cpu_state = *this;
	COMPUTER& state = _context->state;

	uint8_t result = _cpu_state.MemIf->read(addr);

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

uint8_t Z80::DirectRead(unsigned addr)
{
	uint8_t* remap_addr = _context->pMemory->RemapAddressToCurrentBank(addr);
	return *remap_addr;
}

void Z80::DirectWrite(unsigned addr, uint8_t val)
{
	Z80& _cpu_state = *this;
	uint8_t* remap_addr = _context->pMemory->RemapAddressToCurrentBank(addr);
	*remap_addr = val;

	// Update TSConf cache data
	// TODO: move to plugin
	uint16_t cache_pointer = addr & 0x1FF;
	_cpu_state.tscache_addr[cache_pointer] = -1; // write invalidates flag
}

void Z80::z80loop()
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

			Step();
			UpdateScreen(); // update screen, TSU, DMA
		}

		return;
	}


	// Video Interrupt position calculation
	bool int_occurred = false;
	unsigned int_start = config.intstart;
	unsigned int_end = config.intstart + config.intlen;

	_cpu_state.haltpos = 0;

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

		video.memcyc_lcmd = 0; // new command, start accumulate number of busy memcycles

		// INT
		if (_cpu_state.int_pend && _cpu_state.iff1 &&	// INT enabled in CPU
			(_cpu_state.t != _cpu_state.eipos) &&		// INT disabled after EI
			_cpu_state.int_gate)						// INT enabled by ATM hardware (no INT disabling in PentEvo)
		{
			HandleINT(InterruptVector());
		}

		Step();
;
		UpdateScreen(); // update screen, TSU, DMA
	} // end while (cpu.t < conf.intlen)
}

void Z80::Step()
{
	Z80& _cpu_state = *this;
	CONFIG& config = _context->config;
	COMPUTER& state = _context->state;
	TEMP& temporary = _context->temporary;

	// Ports logic
	// TODO: move to Ports class
	if (state.flags & CF_SETDOSROM)
	{
		if (_cpu_state.pch == 0x3D)
		{
			state.flags |= CF_TRDOS;  // !!! add here TS memconf behaviour !!!
			SetBanks();
		}
	}
	else if (state.flags & CF_LEAVEDOSADR)
	{
		if (_cpu_state.pch & 0xC0) // PC > 3FFF closes TR-DOS
		{
			state.flags &= ~CF_TRDOS;
			SetBanks();
		}

		//if (config.trdos_traps)
		//	state.wd.trdos_traps();
	}
	else if (state.flags & CF_LEAVEDOSRAM)
	{
		// Executing code from RAM address - disables TR-DOS ROM
		/*
		if (bankr[(_cpu_state.pc >> 14) & 3] < RAM_BASE_M + PAGE * MAX_RAM_PAGES)
		{
			state.flags &= ~CF_TRDOS;
			SetBanks();
		}

		if (config.trdos_traps)
			state.wd.trdos_traps();
		*/
	}

	// Tape related IO
	// TODO: Move to io/tape
	/*
	if (state.tape.play_pointer && config.tape_traps && (_cpu_state.pc & 0xFFFF) == 0x056B)
		tape_traps();

	if (state.tape.play_pointer && !config.sound.enabled)
		fast_tape();
	*/

	if (_cpu_state.vm1 && _cpu_state.halted)
	{
		// Z80 in HALT state. No processing will be done until INT or NMI arrives
		_cpu_state.tt += _cpu_state.rate * 1;
		if (++_cpu_state.halt_cycle == 4)
		{
			_cpu_state.r_low += 1;
			_cpu_state.halt_cycle = 0;
		}
	}
	else
	{
		if (_cpu_state.pch & temporary.evenM1_C0)
			_cpu_state.tt += (_cpu_state.tt & _cpu_state.rate);

		// 1. Fetch opcode (Z80 M1 bus cycle)
		uint8_t opcode = m1_cycle();

		// 2. Emulate fetched Z80 opcode
		(normal_opcode[opcode])(&_cpu_state);
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
	Z80& _cpu_state = *this;
}

void Z80::UpdateScreen()
{
	Z80& _cpu_state = *this;
}

void Z80::HandleNMI(ROMModeEnum mode)
{
	Z80& _cpu_state = *this;
}

void Z80::HandleINT(uint8_t vector)
{
	Z80& _cpu_state = *this;
	CONFIG& config = _context->config;
	COMPUTER& state = _context->state;

	unsigned interrupt_handler_address;
	if (_cpu_state.im < 2)
	{
		interrupt_handler_address = 0x38;
	}
	else
	{
		// im2
		unsigned vec = vector + _cpu_state.i * 0x100;
		interrupt_handler_address = _cpu_state.MemIf->read(vec) + 0x100 * _cpu_state.MemIf->read(vec + 1);
	}

	if (DirectRead(_cpu_state.pc) == 0x76) // If interrupt occurs on HALT command (opcode 0x76)
		_cpu_state.pc++;

	IncrementCPUCyclesCounter(((_cpu_state.im < 2) ? 13 : 19) - 3);

	_cpu_state.MemIf->write(--_cpu_state.sp, _cpu_state.pch);
	_cpu_state.MemIf->write(--_cpu_state.sp, _cpu_state.pcl);
	_cpu_state.pc = interrupt_handler_address;
	_cpu_state.memptr = interrupt_handler_address;
	_cpu_state.halted = 0;
	_cpu_state.iff1 = _cpu_state.iff2 = 0;
	_cpu_state.int_pend = false;
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

void Z80::IncrementCPUCyclesCounter(uint8_t cycles)
{
	Z80& _cpu_state = *this;

	_cpu_state.tt += cycles * _cpu_state.rate;
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