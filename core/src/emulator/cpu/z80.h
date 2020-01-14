#pragma once
#include "stdafx.h"

#include "emulator/cpu/cpulogic.h"
#include "emulator/emulatorcontext.h"
#include "emulator/memory/memory.h"
#include "emulator/video/screen.h"

struct Z80Registers
{
	union
	{
		unsigned tt;
		struct
		{
			unsigned t_l : 8;
			unsigned t : 24;
		};
	};

	/*------------------------------*/
	union
	{
		unsigned pc;
		struct
		{
			uint8_t pcl;
			uint8_t pch;
		};
	};

	union
	{
		unsigned sp;
		struct
		{
			uint8_t spl;
			uint8_t sph;
		};
	};

	union
	{
		unsigned ir_;
		struct
		{
			uint8_t r_low;
			uint8_t i;
		};
	};

	union
	{
		unsigned int_flags;
		struct
		{
			uint8_t r_hi;
			uint8_t iff1;
			uint8_t iff2;
			uint8_t halted;
		};
	};

	/*------------------------------*/
	union
	{
		unsigned bc;
		uint16_t bc16;
		struct
		{
			uint8_t c;
			uint8_t b;
		};
	};

	union
	{
		unsigned de;
		struct
		{
			uint8_t e;
			uint8_t d;
		};
	};

	union
	{
		unsigned hl;
		struct
		{
			uint8_t l;
			uint8_t h;
		};
	};

	union
	{
		unsigned af;
		struct
		{
			uint8_t f;
			uint8_t a;
		};
	};

	/*------------------------------*/
	union
	{
		unsigned ix;
		struct
		{
			uint8_t xl;
			uint8_t xh;
		};
	};
	union
	{
		unsigned iy;
		struct
		{
			uint8_t yl;
			uint8_t yh;
		};
	};

	/*------------------------------*/
	struct
	{
		union
		{
			unsigned bc;
			struct
			{
				uint8_t c;
				uint8_t b;
			};
		};

		union
		{
			unsigned de;
			struct
			{
				uint8_t e;
				uint8_t d;
			};
		};
		union
		{
			unsigned hl;
			struct
			{
				uint8_t l;
				uint8_t h;
			};
		};
		union
		{
			unsigned af;
			struct
			{
				uint8_t f;
				uint8_t a;
			};
		};
	} alt;

	union
	{
		unsigned memptr; // undocumented register
		struct
		{
			uint8_t meml;
			uint8_t memh;
		};
	};

	uint8_t opcode;
	unsigned eipos, haltpos;

	/*------------------------------*/
	uint8_t im;
	bool nmi_in_progress;
	uint32_t tscache_addr[TS_CACHE_SIZE];
	uint8_t  tscache_data[TS_CACHE_SIZE];
};

struct Z80State : public Z80Registers
{
	uint8_t tmp0, tmp1, tmp3;
	unsigned rate;
	bool vm1;										// Halt handling type (True - ...; False - ...)
	uint8_t outc0;
	uint16_t last_branch;
	unsigned trace_curs, trace_top, trace_mode;
	unsigned mem_curs, mem_top, mem_second;
	unsigned pc_trflags;
	unsigned nextpc;
	unsigned dbg_stophere;
	unsigned dbg_stopsp;
	unsigned dbg_loop_r1;
	unsigned dbg_loop_r2;
	uint8_t dbgchk;									// Active breakpoints
	bool int_pend;									// INT pending
	bool int_gate;									// External interrupts gate (True - enabled; False - disabled)
	unsigned halt_cycle;

	// Conditional breakpoints
	#define MAX_CBP 16								// Up to 16 conditional breakpoints
	unsigned cbp[MAX_CBP][128];						// Conditional breakpoints data
	unsigned cbpn;

	int64_t debug_last_t;							// Used to find time delta
	uint32_t tpi;									// Ticks per interrupt (CPU cycles per video frame)
	uint32_t trpc[40];
	uint32_t Idx;									// CPU Enumeration index (for multiple Z80 in system, like Spectrum with GS/NGS)

	// Callbacks to time critical functions
	//CallbackBankNames BankNames;
	//CallbackStep Step;
	//CallbackDelta Delta;
	//CallbackSetLastT SetLastT;

	uint8_t* membits;
	uint8_t dbgbreak;

	const MemoryInterface* FastMemIf;				// Fast memory interface (max performance
	const MemoryInterface* DbgMemIf;				// Debug memory interface (supports memory access breakpoints)
	const MemoryInterface* MemIf;					// Currently selected memory interface (Fast|Debug)
};

class Z80 : public Z80State
{
protected:
	EmulatorContext* _context;

protected:
	//Z80State _cpu_state;
	int _nmi_pending_count = 0;

public:
	Z80(EmulatorContext* context);
	virtual ~Z80();


public:
	uint8_t m1_cycle();
	uint8_t in(uint16_t port);
	void out(uint16_t port, uint8_t val);
	uint8_t InterruptVector();
	void CheckNextFrame();
	void retn();

	// TODO: convert obsolete naming
	uint8_t rd(unsigned addr);
	void wd(unsigned addr, uint8_t val);
	// End of TODO: convert obsolete naming

	uint8_t Read(uint16_t addr);
	void Write(uint16_t addr, uint8_t val);
	__forceinline uint8_t DirectRead(unsigned addr);				// Direct emulated memory read (For debugger use)
	__forceinline void DirectWrite(unsigned addr, uint8_t val);	// Direct emulated memory write (For debugger use)

	void z80loop();
	void Step();

	// Trigger updates
public:
	void SetBanks();
	void UpdateScreen();

	// Event handlers
public:
	void HandleNMI(ROMModeEnum mode);
	void HandleINT(uint8_t vector);

protected:
	__forceinline void IncrementCPUCyclesCounter(uint8_t cycles);


	// TSConf specific
	// TODO: Move to plugin
protected:
	void ts_frame_int(bool vdos);
	void ts_line_int(bool vdos);
	void ts_dma_int(bool vdos);
};

