#include "stdafx.h"

#include "common/logger.h"

#include "memory.h"

#include "emulator/platform.h"
#include <cassert>

/// region <Constructors / Destructors>

Memory::Memory(EmulatorContext* context)
{
	_context = context;

	// Initialize with default (non-platform specific)
	// base_sos_rom should point to ROM Bank 0 (unit tests depend on that)
	base_sos_rom = ROMPageAddress(0);
	base_dos_rom = ROMPageAddress(1);
	base_128_rom = ROMPageAddress(2);
	base_sys_rom = ROMPageAddress(3);
}

Memory::~Memory()
{
	_context = nullptr;

	LOGDEBUG("Memory::~Memory()");
}

/// endregion </Constructors / Destructors>

/// region <Runtime methods>

//
// Switch to certain ROM section RM_SOS | RM_128 | RM_DOS | RM_SYS
//
void Memory::SetROMMode(ROMModeEnum mode)
{
	if (mode == RM_NOCHANGE)
		return;

	static COMPUTER& state = _context->state;
	static CONFIG& config = _context->config;

	if (mode == RM_CACHE)
	{
		state.flags |= CF_CACHEON;
	}
	else
	{
		// No RAM/cache/SERVICE
		state.p1FFD &= ~7;
		state.pDFFD &= ~0x10;
		state.flags &= ~CF_CACHEON;

		// comp.aFF77 |= 0x100; // enable ATM memory

		switch (mode)
		{
			case RM_128:
				state.flags &= ~CF_TRDOS;
				state.p7FFD &= ~0x10;
				break;
			case RM_SOS:
				state.flags &= ~CF_TRDOS;
				state.p7FFD |= 0x10;

				if (config.mem_model == MM_PLUS3) // Disable paging
					state.p7FFD |= 0x20;
				break;
			case RM_SYS:
				state.flags |= CF_TRDOS;
				state.p7FFD &= ~0x10;
				break;
			case RM_DOS:
				state.flags |= CF_TRDOS;
				state.p7FFD |= 0x10;

				if (config.mem_model == MM_ATM710 || config.mem_model == MM_ATM3)
					state.p7FFD &= ~0x10;
				break;
		}
	}

	SetBanks();
}

// input: ports 7FFD, 1FFD, DFFD, FFF7, FF77, EFF7, flags CF_TRDOS,CF_CACHEON
void Memory::SetBanks()
{
	COMPUTER& state = _context->state;
	CONFIG& config = _context->config;
	TEMP& temp = _context->temporary;

	// Initialize according Spectrum 128K standard address space settings
	InternalSetBanks();

	uint8_t* bank0;
	uint8_t* bank3;

	// GMX specific
	if (config.mem_model == MM_GMX)
	{

	}

	// Spectrum 128k / Scorpion and other clones behavior
	if (state.flags & CF_TRDOS)
	{
		// Either TR-DOS or shadow system ROM is mapped to bank 0
		bank0 = (state.p7FFD & 0x10) ? base_dos_rom : base_sys_rom;
	}
	else
	{
		// SOS or SOS128 ROM otherwise
		bank0 = (state.p7FFD & 0x10) ? base_sos_rom : base_128_rom;
	}

	unsigned sel_bank = (state.p7FFD & 7);

	// Model specific behavior
	switch (config.mem_model)
	{
		case MEM_MODEL::MM_PENTAGON:
			if (!(state.pEFF7 & EFF7_LOCKMEM))
				sel_bank |= (state.p7FFD & 0x20) | (state.p7FFD & 0xC0) >> 3; // 7FFD bits 657..210

			bank3 = RAMPageAddress(sel_bank & temp.ram_mask);

			if (state.pEFF7 & EFF7_ROCACHE)
				bank0 = RAMPageAddress(0); // Alone Coder 0.36.4
			break;
		case MM_PROFSCORP:
			// Reset read access flags from ProfROM pages
			_membits[0x0100] &= ~MemoryBitsEnum::MEMBITS_R;
			_membits[0x0104] &= ~MemoryBitsEnum::MEMBITS_R;
			_membits[0x0108] &= ~MemoryBitsEnum::MEMBITS_R;
			_membits[0x010C] &= ~MemoryBitsEnum::MEMBITS_R;
		case MM_SCORP:
			sel_bank += ((state.p1FFD & 0x10) >> 1) + ((state.p1FFD & 0xC0) >> 2);
			bank3 = RAMPageAddress(sel_bank & temp.ram_mask);

			if (state.p1FFD & 4)
				state.flags |= CF_TRDOS;

			if (state.p1FFD & 2)
				bank0 = base_sys_rom;

			if (state.p1FFD & 1)
				bank0 = RAMPageAddress(0);

			if (config.mem_model == MM_PROFSCORP)
			{
				if (bank0 == base_sys_rom)
					state.flags |= CF_PROFROM;
				else
					state.flags &= ~CF_PROFROM;
			}
			break;
		case MM_KAY:
			{
				sel_bank += ((state.p1FFD & 0x10) >> 1) + ((state.p1FFD & 0x80) >> 3) + ((state.p7FFD & 0x80) >> 2);
				bank3 = RAMPageAddress(sel_bank & temp.ram_mask);
				uint8_t rom1 = (state.p1FFD >> 2) & 2;

				if (state.flags & CF_TRDOS)
					rom1 ^= 2;

				switch (rom1 + ((state.p7FFD & 0x10) >> 4))
				{
					case 0: bank0 = base_128_rom; break;
					case 1: bank0 = base_sos_rom; break;
					case 2: bank0 = base_sys_rom; break;
					case 3: bank0 = base_dos_rom; break;
					default: __assume(0);
				}

				if (state.p1FFD & 1)
					bank0 = RAMPageAddress(0);
			}
			break;
		case MM_PROFI:
			sel_bank += ((state.pDFFD & 0x07) << 3);
			bank3 = RAMPageAddress(sel_bank & temp.ram_mask);

			if (state.pDFFD & 0x08)
			{
				_bank_read[1] = _bank_write[1] = bank3;
				bank3 = RAMPageAddress(7);
			}

			if (state.pDFFD & 0x10)
				bank0 = RAMPageAddress(0);

			if (state.pDFFD & 0x20)
				state.flags |= CF_DOSPORTS;

			if (state.pDFFD & 0x40)
				_bank_read[2] = _bank_write[2] = page_ram(6);
			break;
		case MM_ATM450:
			{
				// RAM
				// original ATM uses D2 as ROM address extension, not RAM
				sel_bank += ((state.pFDFD & 0x07) << 3);
				bank3 = RAMPageAddress(sel_bank & temp.ram_mask);

				if (!(state.aFE & 0x80))
				{
					_bank_write[1] = _bank_read[1] = page_ram(4);
					bank0 = RAMPageAddress(0);
					break;
				}

				// ROM
				if (state.p7FFD & 0x20)
					state.aFB &= ~0x80;
				if ((state.flags & CF_TRDOS) && (state.pFDFD & 8))
					state.aFB |= 0x80; // more priority, then 7FFD

				if (state.aFB & 0x80) // CPSYS signal
				{
					bank0 = base_sys_rom;
					break;
				}

				// System rom not used on 7FFD.4=0 and DOS=1
				if (state.flags & CF_TRDOS)
					bank0 = base_dos_rom;
			}
			break;
		case MM_TSL:
			{
				uint8_t tmp;

				if (state.ts.w0_map_n)
				{
					// Linear
					tmp = state.ts.page[0];
				}
				else
				{
					// Mapped
					if (state.flags & CF_TRDOS)
						tmp = (state.p7FFD & 0x10) ? 1 : 0;
					else
						tmp = (state.p7FFD & 0x10) ? 3 : 2;

					tmp |= state.ts.page[0] & 0xFC;
				}

				if (state.ts.w0_ram || state.ts.vdos)
				{
					// RAM at [0x0000-0x3FFF]
					_bank_mode[0] = state.ts.w0_we ? MemoryBankModeEnum::BANK_RAM : MemoryBankModeEnum::BANK_ROM;
					bank0 = RAMPageAddress(state.ts.vdos ? 0xFF : tmp);
				}
				else
				{
					// ROM at[0x0000-0x3FFF]
					_bank_mode[0] = MemoryBankModeEnum::BANK_ROM;
					bank0 = ROMPageAddress(tmp & 0x1F);
				}

				_bank_read[1] = _bank_write[1] = RAMPageAddress(state.ts.page[1]);
				_bank_read[2] = _bank_write[2] = RAMPageAddress(state.ts.page[2]);
				bank3 = RAMPageAddress(state.ts.page[3]);
			}
			break;
		case MM_ATM3:
			if (state.pBF & 1) // shaden
				state.flags |= CF_DOSPORTS;
		case MM_ATM710:
			{
				if (!(state.aFF77 & 0x200)) // ~cpm=0
					state.flags |= CF_TRDOS;

				if (!(state.aFF77 & 0x100))
				{
					// pen=0
					_bank_read[1] = _bank_read[2] = bank3 = bank0 = page_rom(temp.rom_mask);
					break;
				}

				unsigned i = ((state.p7FFD & 0x10) >> 2);
				for (unsigned bank = 0; bank < 4; bank++)
				{
					// lvd added 6 or 3 bits from 7FFD to page number insertion in pentevo (was only 3 as in atm2)
					uint32_t mem7ffd = (state.p7FFD & 7) | ((state.p7FFD & 0xE0) >> 2);
					uint32_t mask7ffd = 0x07;

					if (config.mem_model == MM_ATM3 && (!(state.pEFF7 & EFF7_LOCKMEM)))
						mask7ffd = 0x3F;

					switch (state.pFFF7[i + bank] & 0x300)
					{
						case 0x000: // RAM from 7FFD (lvd patched)
							_bank_read[bank] = _bank_write[bank] = page_ram((mem7ffd & mask7ffd) | (state.pFFF7[i + bank] & (~mask7ffd) & temp.ram_mask));
							break;
						case 0x100: // ROM from 7FFD
							_bank_read[bank] = ROMPageAddress((state.pFFF7[i + bank] & 0xFE & temp.rom_mask) + ((state.flags & CF_TRDOS) ? 1 : 0));
							break;
						case 0x200: // RAM from FFF7
							_bank_read[bank] = _bank_write[bank] = page_ram(state.pFFF7[i + bank] & 0xFF & temp.ram_mask);
							break;
						case 0x300: // ROM from FFF7
							_bank_read[bank] = ROMPageAddress(state.pFFF7[i + bank] & 0xFF & temp.rom_mask);
							break;
					}
				}

				bank0 = _bank_read[0];
				bank3 = _bank_read[3];

				//         if (conf.mem_model == MM_ATM3 && cpu.nmi_in_progress)
				//             bank0 = page_ram(0xFF);
				if (config.mem_model == MM_ATM3) // lvd added pentevo RAM0 to bank0 feature if EFF7_ROCACHE is set
				{
					if (state.nmi_in_progress)
						bank0 = RAMPageAddress(0xFF);
					else if (state.pEFF7 & EFF7_ROCACHE)
						bank0 = RAMPageAddress(0x00);
				}

			}
			break;
		case MM_PLUS3:
			{
				if (state.p7FFD & 0x20) // paging disabled (48k mode)
				{
					bank3 = RAMPageAddress(sel_bank & temp.ram_mask);
					break;
				}

				if (!(state.p1FFD & 1))
				{
					unsigned RomBank = ((state.p1FFD & 4) >> 1) | ((state.p7FFD & 0x10) >> 4);
					switch (RomBank)
					{
						case 0: bank0 = base_128_rom; break;
						case 1: bank0 = base_sys_rom; break;
						case 2: bank0 = base_dos_rom; break;
						case 3: bank0 = base_sos_rom; break;
					}
					bank3 = RAMPageAddress(sel_bank & temp.ram_mask);
				}
				else
				{
					unsigned RamPage = (state.p1FFD >> 1) & 3; // d2,d1
					static const unsigned RamDecoder[4][4] =
					{
						{0, 1, 2, 3},
						{4, 5, 6, 7},
						{4, 5, 6, 3},
						{4, 7, 6, 3}
					};

					for (unsigned i = 0; i < 4; i++)
						_bank_write[i] = _bank_read[i] = RAMPageAddress(RamDecoder[RamPage][i]);

					bank0 = _bank_read[0];
					bank3 = _bank_read[3];
				}
			}
			break;
		case MM_QUORUM:
			{
				if (!(state.p00 & Q_TR_DOS))
					state.flags |= CF_DOSPORTS;

				if (state.p00 & Q_B_ROM)
				{
					if (state.flags & CF_TRDOS)
						bank0 = base_dos_rom;
					else
						bank0 = (state.p7FFD & 0x10) ? base_sos_rom : base_128_rom;
				}
				else
				{
					bank0 = base_sys_rom;
				}

				if (state.p00 & Q_F_RAM)
				{
					unsigned bnk0 = (state.p00 & Q_RAM_8) ? 8 : 0;
					bank0 = RAMPageAddress(bnk0 & temp.ram_mask);
				}

				sel_bank |= ((state.p7FFD & 0xC0) >> 3) | (state.p7FFD & 0x20);
				bank3 = RAMPageAddress(sel_bank & temp.ram_mask);
			}
			break;
		case MM_LSY256:
			// ROM banks: 0 - 128, 1 - 48, 2 - sys, 3 - trd
			{
				switch (state.pLSY256 & (PF_EMUL | PF_BLKROM))
				{
					// #0000: ROM, LSY-Setup
					case 0:
						bank0 = base_sys_rom;
						break;

						// #0000: ROM, default
					case PF_EMUL:
						break;

						// #0000: RAM 12/13 (DV0 - selector), r/w
					case PF_BLKROM:
						bank0 = RAMPageAddress((state.pLSY256 & PF_DV0) ? 13 : 12);
						_bank_mode[0] = MemoryBankModeEnum::BANK_RAM;
						break;

					// #0000: RAM 8-11, r/o
					case PF_EMUL | PF_BLKROM:
					{
						if (state.flags & CF_TRDOS)
							bank0 = RAMPageAddress((state.p7FFD & 0x10) ? 11 : 10);
						else
							bank0 = RAMPageAddress((state.p7FFD & 0x10) ? 9 : 8);
					}
					break;
				}

				bank3 = RAMPageAddress((state.p7FFD & 0x07) | (state.pLSY256 & PF_PA3));
			}
			break;
		case MM_PHOENIX:
			sel_bank = (state.p7FFD & 0x07) |
				((state.p7FFD & 0x80) >> 4) |
				(state.p1FFD & 0x50) |
				((state.p1FFD & 0x80) >> 2);
			bank3 = RAMPageAddress(sel_bank & temp.ram_mask);

			if (state.p1FFD & 2)
			{
				bank0 = base_sys_rom;
			}
			else if (state.p1FFD & 8)
			{
				if (state.flags & CF_TRDOS)
					if (state.p7FFD & 16)
						bank0 = base_sos_rom;
					else
						bank0 = base_128_rom;
				else
					if (state.p7FFD & 16)
						bank0 = base_dos_rom;
					else
						bank0 = base_sys_rom;
			}

			if (state.p1FFD & 1)
				bank0 = RAM_BASE_M + 0 * PAGE;

			if (state.pEFF7 & 0x80)
				state.flags |= CF_DOSPORTS;
			break;
		default:
			bank3 = RAMPageAddress(sel_bank & temp.ram_mask);
			break;
	}


	// Apply changes to the state
	_bank_write[0] = _bank_read[0] = bank0;
	_bank_write[3] = _bank_read[3] = bank3;

	if (_bank_read[0] >= _romBase || (config.mem_model == MM_TSL && !state.ts.w0_we && !state.ts.vdos))
	{
		_bank_write[0] = TRASH_M;
	}

	// Disable MemoryWrite for banks that marked as ROM
	if (_bank_read[1] >= _romBase) _bank_write[1] = TRASH_M;
	if (_bank_read[2] >= _romBase) _bank_write[2] = TRASH_M;
	if (_bank_read[3] >= _romBase) _bank_write[3] = TRASH_M;

	uint8_t dosflags = CF_LEAVEDOSRAM;

	// TS-conf VDOS specific
	if (config.mem_model == MM_TSL && state.ts.vdos)
		dosflags = 0;

	if (config.mem_model == MM_PENTAGON || config.mem_model == MM_PROFI)
		dosflags = CF_LEAVEDOSADR;

	if (state.flags & CF_TRDOS)
	{
		state.flags |= dosflags | CF_DOSPORTS;
	}
	else if ((state.p7FFD & 0x10) && config.trdos_present)
	{
		// B-48, inactive DOS, DOS present
		// for Scorp, ATM-1/2 and KAY, TR-DOS not started on executing RAM 3Dxx
		if (!((dosflags & CF_LEAVEDOSRAM) && _bank_read[0] < page_ram(MAX_RAM_PAGES)))
			state.flags |= CF_SETDOSROM;
	}

	// Cache page update for bank 0 (if enabled)
	if (state.flags & CF_CACHEON)
	{
		uint8_t *cpage = CACHE_M;
		if (config.cache == 32 && !(state.p7FFD & 0x10))
			cpage += PAGE;
		_bank_read[0] = _bank_write[0] = cpage;
		// if (comp.pEFF7 & EFF7_ROCACHE) bankw[0] = TRASH_M; //Alone Coder 0.36.4
	}

	if ((state.flags & CF_DOSPORTS) ? config.floatdos : config.floatbus)
		state.flags |= CF_Z80FBUS;

	// Bank usage trace
	/*
	if (temp.led.osw && (trace_rom | trace_ram))
	{
		for (unsigned i = 0; i < 4; i++)
		{
			unsigned bank = (_bank_read[i] - RAM_BASE_M) / PAGE;
			if (bank < MAX_PAGES)
				used_banks[bank] = 1;
		}
	}
	*/
}

/// endregion </Runtime methods>

/// region <Debug methods>

void Memory::SetROM48k()
{
    // Set all port values accordingly
    SetROMMode(RM_SOS);

    // Switch to 48k ROM page
    _bank_read[0] = base_sos_rom;
    _bank_write[0] = base_sos_rom;
}

void Memory::SetROM128k()
{
    // Set all port values accordingly
    SetROMMode(RM_128);

    // Switch to 128k ROM page
    _bank_read[0] = base_128_rom;
    _bank_write[0] = base_128_rom;
}

void Memory::SetROMDOS()
{
    // Set all port values accordingly
    SetROMMode(RM_DOS);

    // Switch to DOS ROM page
    _bank_read[0] = base_dos_rom;
    _bank_write[0] = base_dos_rom;
}

void Memory::SetROMSystem()
{
    // Set all port values accordingly
    SetROMMode(RM_SYS);

    // Switch to DOS ROM page
    _bank_read[0] = base_sys_rom;
    _bank_write[0] = base_sys_rom;
}

/// endregion </Debug methods>

/// region <Helper methods>

/// Returns pointer in Host memory for the address in Z80 space mapped to current bank configuration
/// \param address Z80 space address
/// \return Pointer to Host memory mapped
uint8_t* Memory::RemapAddressToCurrentBank(uint16_t address)
{
	COMPUTER& state = *(&_context->state);
	CONFIG& config = *(&_context->config);
	TEMP& temp = *(&_context->temporary);

	uint8_t* result = nullptr;

	#if defined MOD_VID_VD
		if (comp.vdbase && (unsigned)((addr & 0xFFFF) - 0x4000) < 0x1800)
			result = comp.vdbase + (addr & 0x1FFF);
    #else
        uint8_t bank = (address >> 14) & 0b0000000000000011;
		result = _bank_read[bank] + address;
	#endif

	return result;
}

//
//
//
MemoryBankModeEnum Memory::GetMemoryBankMode(uint8_t bank)
{
	if (bank >= 4)
	{
		LOGERROR("Memory::GetMemoryBankMode() - Z80 memory bank can only be [0:3]. Found: %d", bank);
		assert("Invalid Z80 bank");
	}

	return _bank_mode[bank];
}

/// Read single byte from address mapped to Z80 address space
/// Note: Direct access method. Not shown in any traces, memory counters are not incremented
/// \param address - Z80 space address
/// \return
uint8_t Memory::ReadFromMappedMemoryAddress(uint16_t address)
{
    uint8_t result = 0x00;

    // Address bits 14 and 15 contain bank number
    uint8_t bank = (address >> 14) & 0b0000000000000011;
    result = *(_bank_read[bank] + address);

    return result;
}

/// Write single byte by address mapped to Z80 address space
/// Note: Direct access method. Not shown in any traces, memory counters are not incremented
/// \param address - Z80 space address
/// \param value - Single byte value to be written by address
void Memory::WriteByMappedMemoryAddress(uint16_t address, uint8_t value)
{
    // Address bits 14 and 15 contain bank number
    uint8_t bank = (address >> 14) & 0b0000000000000011;
    *(_bank_write[bank] + address) = value;
}

//
// Initialize according Spectrum 48K standard address space settings
//
void Memory::InternalSetBanks()
{
	COMPUTER& state = _context->state;
	CONFIG& config = _context->config;

	// Initialize according Spectrum 128K standard address space settings
	_bank_write[0] = _bank_read[0] = base_sos_rom;		// 48K (SOS) ROM					for [0x0000 - 0x3FFF]
	_bank_write[1] = _bank_read[1] = RAMPageAddress(5);	// Set Screen 1 (page 5) as default	for [0x4000 - 0x7FFF]
	_bank_write[2] = _bank_read[2] = RAMPageAddress(2);	// Set page 2 as default			for [0x8000 - 0xBFFF]
	_bank_write[3] = _bank_read[3] = RAMPageAddress(0);	// Set page 0 as default			for [0xC000 - 0xFFFF]

	_bank_mode[0] = MemoryBankModeEnum::BANK_ROM;		// Bank 0 is ROM [0x0000 - 0x3FFF]
	_bank_mode[1] = MemoryBankModeEnum::BANK_RAM;		// Bank 1 is RAM [0x4000 - 0x7FFF]
	_bank_mode[2] = MemoryBankModeEnum::BANK_RAM;		// Bank 2 is RAM [0x8000 - 0xBFFF]
	_bank_mode[3] = MemoryBankModeEnum::BANK_RAM;		// Bank 3 is RAM [0xC000 - 0xFFFF]

	// Reset all address defining flags/signals (they'll be recalculated later in logic)
	state.flags &= ~(CF_DOSPORTS | CF_Z80FBUS | CF_LEAVEDOSRAM | CF_LEAVEDOSADR | CF_SETDOSROM);
}

/// endregion </Helper methods>

/*
void set_banks()
{
   // set default values for memory windows
   bankw[1] = bankr[1] = page_ram(5);
   bankw[2] = bankr[2] = page_ram(2);
   bankm[0] = BANKM_ROM;
   bankm[1] = bankm[2] = bankm[3] = BANKM_RAM;

   // screen begining
   temp.base = memory + comp.ts.vpage * PAGE;

   if (conf.mem_model == MM_QUORUM)
       temp.base = memory + (comp.p80FD & 7) * 0x2000 + 5*PAGE;

   //if (temp.base_2)
       temp.base_2 = temp.base;		// FIX THIS SHIT ANYWHERE YOU MEEEET IT!!!!

   // these flags will be re-calculated
   comp.flags &= ~(CF_DOSPORTS | CF_Z80FBUS | CF_LEAVEDOSRAM | CF_LEAVEDOSADR | CF_SETDOSROM);

   uint8_t *bank0, *bank3;

	if (conf.mem_model == MM_GMX)
	{
		base_128_rom = ROM_BASE_M + ((comp.p7EFD>>4)&7) * 64*1024 + 0*PAGE;
		base_sos_rom = ROM_BASE_M + ((comp.p7EFD>>4)&7) * 64*1024 + 1*PAGE;
		base_sys_rom = ROM_BASE_M + ((comp.p7EFD>>4)&7) * 64*1024 + 2*PAGE;
		base_dos_rom = ROM_BASE_M + ((comp.p7EFD>>4)&7) * 64*1024 + 3*PAGE;
	}

   if (comp.flags & CF_TRDOS)
       bank0 = (comp.p7FFD & 0x10) ? base_dos_rom : base_sys_rom;
   else
       bank0 = (comp.p7FFD & 0x10) ? base_sos_rom : base_128_rom;

   unsigned bank = (comp.p7FFD & 7);

   switch (conf.mem_model)
   {
      case MM_PENTAGON:
         if (!(comp.pEFF7 & EFF7_LOCKMEM))
             bank |= (comp.p7FFD & 0x20) | (comp.p7FFD & 0xC0) >> 3; // 7FFD bits 657..210

         bank3 = page_ram(bank & temp.ram_mask);

         if (comp.pEFF7 & EFF7_ROCACHE)
             bank0 = page_ram(0); //Alone Coder 0.36.4
         break;

      case MM_PROFSCORP:
         membits[0x0100] &= ~MEMBITS_R;
         membits[0x0104] &= ~MEMBITS_R;
         membits[0x0108] &= ~MEMBITS_R;
         membits[0x010C] &= ~MEMBITS_R;

      case MM_SCORP:
         bank += ((comp.p1FFD & 0x10) >> 1) + ((comp.p1FFD & 0xC0) >> 2);
         bank3 = page_ram(bank & temp.ram_mask);

         // ��������� �� ������ gmx (��������� ������ dos �� ���, ������� ������� �� ��� ��� � �������� �����)
         if (comp.p1FFD & 4)
             comp.flags |= CF_TRDOS;
         if (comp.p1FFD & 2)
            bank0 = base_sys_rom;
         if (comp.p1FFD & 1)
            bank0 = page_ram(0);
         if (conf.mem_model == MM_PROFSCORP)
         {
             if (bank0 == base_sys_rom)
                 comp.flags |= CF_PROFROM;
             else
                 comp.flags &= ~CF_PROFROM;
         }
         break;

	  case MM_GMX:
			bank |= ((comp.p1FFD & 0x10) >> 1) | ((comp.pDFFD & 7) << 4);
			bankw[2] = bankr[2] = RAM_BASE_M + ((comp.p78FD ^ 2) & temp.ram_mask)*PAGE;
			bank3 = RAM_BASE_M + (bank & temp.ram_mask)*PAGE;
			if (comp.p1FFD & 2)
				bank0 = base_sys_rom;
			if (comp.p1FFD & 1)
				bank0 = page_ram(0);
			if (comp.p1FFD & 4)
			{
				comp.flags |= CF_DOSPORTS;
				bank0 = base_dos_rom;
			}
			//bank0 += ((comp.p7EFD >> 4) & 7)*PAGE;
			break;

      case MM_KAY:
      {
         bank += ((comp.p1FFD & 0x10) >> 1) + ((comp.p1FFD & 0x80) >> 3) + ((comp.p7FFD & 0x80) >> 2);
         bank3 = page_ram(bank & temp.ram_mask);
		 uint8_t rom1 = (comp.p1FFD >> 2) & 2;
         if (comp.flags & CF_TRDOS) rom1 ^= 2;
         switch (rom1+((comp.p7FFD & 0x10) >> 4))
         {
            case 0: bank0 = base_128_rom; break;
            case 1: bank0 = base_sos_rom; break;
            case 2: bank0 = base_sys_rom; break;
            case 3: bank0 = base_dos_rom; break;
            default: __assume(0);
         }
         if (comp.p1FFD & 1) bank0 = page_ram(0);
         break;
      }

      case MM_PROFI:
         bank += ((comp.pDFFD & 0x07) << 3); bank3 = page_ram(bank & temp.ram_mask);
         if (comp.pDFFD & 0x08) bankr[1] = bankw[1] = bank3, bank3 = page_ram(7);
         if (comp.pDFFD & 0x10) bank0 = page_ram(0);
         if (comp.pDFFD & 0x20) comp.flags |= CF_DOSPORTS;
         if (comp.pDFFD & 0x40) bankr[2] = bankw[2] = page_ram(6);
         break;

      case MM_ATM450:
      {
         // RAM
         // original ATM uses D2 as ROM address extension, not RAM
         bank += ((comp.pFDFD & 0x07) << 3);
         bank3 = page_ram(bank & temp.ram_mask);
         if (!(comp.aFE & 0x80))
         {
            bankw[1] = bankr[1] = page_ram(4);
            bank0 = page_ram(0);
            break;
         }

         // ROM
         if (comp.p7FFD & 0x20)
             comp.aFB &= ~0x80;
         if ((comp.flags & CF_TRDOS) && (comp.pFDFD & 8))
             comp.aFB |= 0x80; // more priority, then 7FFD

         if (comp.aFB & 0x80) // CPSYS signal
         {
             bank0 = base_sys_rom;
             break;
         }
         // system rom not used on 7FFD.4=0 and DOS=1
         if (comp.flags & CF_TRDOS)
             bank0 = base_dos_rom;
         break;
      }

      case MM_TSL:
	  {
		 uint8_t tmp;

		if (comp.ts.w0_map_n)
		{
			// Linear
			tmp = comp.ts.page[0];
		}
		else
		{
			// Mapped
			if (comp.flags & CF_TRDOS)
				tmp = (comp.p7FFD & 0x10) ? 1 : 0;
			else
				tmp = (comp.p7FFD & 0x10) ? 3 : 2;

			tmp |= comp.ts.page[0] & 0xFC;
		}

		if (comp.ts.w0_ram || comp.ts.vdos)
		// RAM at #0000
		{
			bankm[0] = comp.ts.w0_we ? BANKM_RAM : BANKM_ROM;
			bank0 = page_ram(comp.ts.vdos ? 0xFF : tmp);
		}

		else
		{
		// ROM at #0000
			bankm[0] = BANKM_ROM;
			bank0 = page_rom(tmp & 0x1F);
		}

		bankr[1] = bankw[1] = page_ram(comp.ts.page[1]);
		bankr[2] = bankw[2] = page_ram(comp.ts.page[2]);
		bank3  = page_ram(comp.ts.page[3]);
	  }
      break;

      case MM_ATM3:
         if (comp.pBF & 1) // shaden
            comp.flags |= CF_DOSPORTS;

      case MM_ATM710:
      {
         if (!(comp.aFF77 & 0x200)) // ~cpm=0
            comp.flags |= CF_TRDOS;

         if (!(comp.aFF77 & 0x100))
         { // pen=0
            bankr[1] = bankr[2] = bank3 = bank0 = page_rom(temp.rom_mask);
            break;
         }

         unsigned i = ((comp.p7FFD & 0x10) >> 2);
         for (unsigned bank = 0; bank < 4; bank++)
         {
            // lvd added 6 or 3 bits from 7FFD to page number insertion in pentevo (was only 3 as in atm2)
            uint32_t mem7ffd = (comp.p7FFD & 7) | ( (comp.p7FFD & 0xE0)>>2 );
			uint32_t mask7ffd = 0x07;

            if (conf.mem_model==MM_ATM3 && (!(comp.pEFF7 & EFF7_LOCKMEM)))
                mask7ffd = 0x3F;

            switch (comp.pFFF7[i+bank] & 0x300)
            {
               case 0x000: // RAM from 7FFD (lvd patched)
                  bankr[bank] = bankw[bank] = page_ram((mem7ffd & mask7ffd) | (comp.pFFF7[i+bank] & (~mask7ffd) & temp.ram_mask));
                  break;
               case 0x100: // ROM from 7FFD
                  bankr[bank] = page_rom((comp.pFFF7[i+bank] & 0xFE & temp.rom_mask) + ((comp.flags & CF_TRDOS)?1:0));
                  break;
               case 0x200: // RAM from FFF7
                  bankr[bank] = bankw[bank] = page_ram(comp.pFFF7[i+bank] & 0xFF & temp.ram_mask);
                  break;
               case 0x300: // ROM from FFF7
                  bankr[bank] = page_rom(comp.pFFF7[i+bank] & 0xFF & temp.rom_mask);
                  break;
            }
         }
         bank0 = bankr[0]; bank3 = bankr[3];

//         if (conf.mem_model == MM_ATM3 && cpu.nmi_in_progress)
//             bank0 = page_ram(0xFF);
        if ( conf.mem_model==MM_ATM3 ) // lvd added pentevo RAM0 to bank0 feature if EFF7_ROCACHE is set
        {
            if ( cpu.nmi_in_progress )
                bank0 = page_ram(0xFF);
            else if ( comp.pEFF7 & EFF7_ROCACHE )
                bank0 = page_ram(0x00);
        }

      }
      break;

      case MM_PLUS3:
      {
          if (comp.p7FFD & 0x20) // paging disabled (48k mode)
          {
              bank3 = page_ram(bank & temp.ram_mask);
              break;
          }

          if (!(comp.p1FFD & 1))
          {
              unsigned RomBank = ((comp.p1FFD & 4) >> 1) | ((comp.p7FFD & 0x10) >> 4);
              switch(RomBank)
              {
                 case 0: bank0 = base_128_rom; break;
                 case 1: bank0 = base_sys_rom; break;
                 case 2: bank0 = base_dos_rom; break;
                 case 3: bank0 = base_sos_rom; break;
              }
              bank3 = page_ram(bank & temp.ram_mask);
          }
          else
          {
              unsigned RamPage = (comp.p1FFD >> 1) & 3; // d2,d1
              static const unsigned RamDecoder[4][4] =
              { {0, 1, 2, 3}, {4, 5, 6, 7}, {4, 5, 6, 3}, {4, 7, 6, 3} };
              for (unsigned i = 0; i < 4; i++)
                  bankw[i] = bankr[i] = page_ram(RamDecoder[RamPage][i]);
              bank0 = bankr[0];
              bank3 = bankr[3];
          }
      }
      break;

      case MM_QUORUM:
      {
          if (!(comp.p00 & Q_TR_DOS))
              comp.flags |= CF_DOSPORTS;

          if (comp.p00 & Q_B_ROM)
          {
              if (comp.flags & CF_TRDOS)
                  bank0 = base_dos_rom;
              else
                  bank0 = (comp.p7FFD & 0x10) ? base_sos_rom : base_128_rom;
          }
          else
          {
              bank0 = base_sys_rom;
          }

          if (comp.p00 & Q_F_RAM)
          {
              unsigned bnk0 = (comp.p00 & Q_RAM_8) ? 8 : 0;
              bank0 = page_ram(bnk0 & temp.ram_mask);
          }

          bank |= ((comp.p7FFD & 0xC0) >> 3) | (comp.p7FFD & 0x20);
          bank3 = page_ram(bank & temp.ram_mask);
      }
      break;

      case MM_LSY256:
      // ROM banks: 0 - 128, 1 - 48, 2 - sys, 3 - trd
      {
		switch (comp.pLSY256 & (PF_EMUL | PF_BLKROM))
        {
            // #0000: ROM, LSY-Setup
            case 0:
                bank0 = base_sys_rom;
            break;

            // #0000: ROM, default
            case PF_EMUL:
            break;

            // #0000: RAM 12/13 (DV0 - selector), r/w
            case PF_BLKROM:
                bank0 = page_ram((comp.pLSY256 & PF_DV0) ? 13 : 12);
                bankm[0] = BANKM_RAM;
            break;

            // #0000: RAM 8-11, r/o
            case PF_EMUL | PF_BLKROM:
            {
                if (comp.flags & CF_TRDOS)
                    bank0 = page_ram((comp.p7FFD & 0x10) ? 11 : 10);
                else
                    bank0 = page_ram((comp.p7FFD & 0x10) ? 9 : 8);
            }
            break;
        }

        bank3 = page_ram((comp.p7FFD & 0x07) | (comp.pLSY256 & PF_PA3));
      }
      break;

	  case MM_PHOENIX:
         bank = (comp.p7FFD & 0x07) |
				((comp.p7FFD & 0x80) >> 4) |
				(comp.p1FFD & 0x50) |
				((comp.p1FFD & 0x80) >> 2 );
         bank3 = RAM_BASE_M + (bank & temp.ram_mask)*PAGE;

         if (comp.p1FFD & 2) bank0 = base_sys_rom;
		 else if (comp.p1FFD & 8)
		 {
			if (comp.flags & CF_TRDOS)
				if  (comp.p7FFD & 16) bank0 = base_sos_rom;
				else bank0 = base_128_rom;
			else
				if  (comp.p7FFD & 16) bank0 = base_dos_rom;
				else bank0 = base_sys_rom;
		 }
         if (comp.p1FFD & 1) bank0 = RAM_BASE_M + 0*PAGE;

		 if (comp.pEFF7 & 0x80) comp.flags |= CF_DOSPORTS;
         break;

      default: bank3 = page_ram(0);
   }

   bankw[0] = bankr[0] = bank0;
   bankw[3] = bankr[3] = bank3;

   if (bankr[0] >= ROM_BASE_M ||
     (conf.mem_model == MM_TSL && !comp.ts.w0_we && !comp.ts.vdos)) bankw[0] = TRASH_M;

   if (bankr[1] >= ROM_BASE_M) bankw[1] = TRASH_M;
   if (bankr[2] >= ROM_BASE_M) bankw[2] = TRASH_M;
   if (bankr[3] >= ROM_BASE_M) bankw[3] = TRASH_M;

   uint8_t dosflags = CF_LEAVEDOSRAM;
   if (conf.mem_model == MM_TSL && comp.ts.vdos)
       dosflags = 0;
   if (conf.mem_model == MM_PENTAGON || conf.mem_model == MM_PROFI)
       dosflags = CF_LEAVEDOSADR;

   if (comp.flags & CF_TRDOS)
   {
       comp.flags |= dosflags | CF_DOSPORTS;
   }
   else if ((comp.p7FFD & 0x10) && conf.trdos_present)
   { // B-48, inactive DOS, DOS present
      // for Scorp, ATM-1/2 and KAY, TR-DOS not started on executing RAM 3Dxx
      if (!((dosflags & CF_LEAVEDOSRAM) && bankr[0] < page_ram(MAX_RAM_PAGES)))
         comp.flags |= CF_SETDOSROM;
   }

   if (comp.flags & CF_CACHEON)
   {
	   uint8_t *cpage = CACHE_M;
      if (conf.cache == 32 && !(comp.p7FFD & 0x10))
		  cpage += PAGE;
      bankr[0] = bankw[0] = cpage;
      // if (comp.pEFF7 & EFF7_ROCACHE) bankw[0] = TRASH_M; //Alone Coder 0.36.4
   }

   if ((comp.flags & CF_DOSPORTS)? conf.floatdos : conf.floatbus)
       comp.flags |= CF_Z80FBUS;

   if (temp.led.osw && (trace_rom | trace_ram))
   {
      for (unsigned i = 0; i < 4; i++) {
         unsigned bank = (bankr[i] - RAM_BASE_M) / PAGE;
         if (bank < MAX_PAGES)
			 used_banks[bank] = 1;
      }
   }

/*
    if ((unsigned)(bankr[0] - ROM_BASE_M) < PAGE*MAX_ROM_PAGES)
    {
        printf("ROM%2X\n", (bankr[0] - ROM_BASE_M)/PAGE);
        printf("DOS=%p\n",  base_dos_rom);
        printf("SVM=%p\n",  base_sys_rom);
        printf("SOS=%p\n",  base_sos_rom);
        printf("128=%p\n",  base_128_rom);
    }
}
*/

/*
void set_scorp_profrom(unsigned read_address)
{
   static uint8_t switch_table[] =
   {
      0,1,2,3,
      3,3,3,2,
      2,2,0,1,
      1,0,1,0
   };

   comp.profrom_bank = switch_table[read_address*4 + comp.profrom_bank] & temp.profrom_mask;
   uint8_t *base = ROM_BASE_M + (comp.profrom_bank * 64*1024);
   base_128_rom = base + 0*PAGE;
   base_sos_rom = base + 1*PAGE;
   base_sys_rom = base + 2*PAGE;
   base_dos_rom = base + 3*PAGE;
   set_banks();
}
*/

/*
u8 *MemDbg(u32 addr)
{
    return am_r(addr);
}

void wmdbg(u32 addr, u8 val)
{
   *am_r(addr) = val;
}

u8 rmdbg(u32 addr)
{
   return *am_r(addr);
}
*/

/*
void cmos_write(uint8_t val)
{
   if (conf.cmos == 2) comp.cmos_addr &= 0x3F;

   if (((conf.mem_model == MM_ATM3) || (conf.mem_model == MM_TSL)) && comp.cmos_addr == 0x0C)
   {
       BYTE keys[256];
       if (GetKeyboardState(keys) && !keys[VK_CAPITAL] != !(val & 0x02))
       {
           keybd_event(VK_CAPITAL, 0x45, KEYEVENTF_EXTENDEDKEY, 0);
           keybd_event(VK_CAPITAL, 0x45, KEYEVENTF_EXTENDEDKEY | KEYEVENTF_KEYUP, 0);
       }
       if (val & 0x01)
           input.buffer.Empty();
   }

   cmos[comp.cmos_addr] = val;
}



void rand_ram()
{
	uint8_t *mem = page_ram(0);
	for (uint32_t i=0; i<(4096*1024); i++)
		mem[i] = rand();
}

*/