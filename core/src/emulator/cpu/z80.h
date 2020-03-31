#pragma once
#include "stdafx.h"

#include "emulator/cpu/cpulogic.h"
#include "emulator/emulatorcontext.h"
#include "emulator/memory/memory.h"

// Turn on padding to align members within each structure
#pragma pack(push, 1)

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

	// IR - Instruction register
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
			uint8_t iff1;	// Interrupt flip-flop 1
			uint8_t iff2;	// Interrupt flip-flop 2
			uint8_t halted;	// CPU halted
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

	uint32_t cycle_count;	// Counter to track cycle accuracy

	uint8_t opcode;			// Opcode fetched during m1 cycle
	unsigned eipos;
	unsigned haltpos;

	/*------------------------------*/
	uint8_t im;				// Interrupt mode [IM0|IM1|IM2]
	bool nmi_in_progress;


	uint32_t tscache_addr[TS_CACHE_SIZE];
	uint8_t  tscache_data[TS_CACHE_SIZE];
};

#pragma pack(pop)

struct Z80State : public Z80Registers
{
	uint32_t Idx;									// CPU Enumeration index (for multiple Z80 in system, like Spectrum with GS/NGS)

	unsigned rate;									// Rate for Z80 speed recalculatins. 3.5MHz -> 256, 7MHz -> 128
	bool vm1;										// Halt handling type (True - ...; False - ...)
	uint8_t outc0;

	uint8_t tmp0, tmp1, tmp3;
	uint16_t last_branch;
	unsigned trace_curs, trace_top, trace_mode;
	unsigned mem_curs, mem_top, mem_second;
	unsigned pc_trflags;
	unsigned nextpc;

	// Debugger related
	uint8_t dbgchk;									// Active breakpoints
	uint8_t dbgbreak;
	unsigned dbg_stophere;
	unsigned dbg_stopsp;
	unsigned dbg_loop_r1;
	unsigned dbg_loop_r2;
	#define MAX_CBP 16								// Up to 16 conditional breakpoints
	unsigned cbp[MAX_CBP][128];						// Conditional breakpoints data
	unsigned cbpn;
	int64_t debug_last_t;							// Used to find time delta


	// Interrupts / HALT
	bool int_pend;									// INT pending
	bool int_gate;									// External interrupts gate (True - enabled; False - disabled)
	unsigned halt_cycle;

	// CPU cycles counter
	uint32_t tpi;									// Ticks per interrupt (CPU cycles per video frame)
	uint32_t trpc[40];

	// Callbacks to time critical functions
	//CallbackBankNames BankNames;
	//CallbackStep Z80Step;
	//CallbackDelta Delta;
	//CallbackSetLastT SetLastT;

	// Memory interfacing
	uint8_t* _membits;
	const MemoryInterface* FastMemIf;				// Fast memory interface (max performance
	const MemoryInterface* DbgMemIf;				// Debug memory interface (supports memory access breakpoints)
	const MemoryInterface* MemIf;					// Currently selected memory interface (Fast|Debug)
};

class Z80 : public Z80State
{
protected:
	EmulatorContext* _context;

protected:
	int _nmi_pending_count = 0;

public:
	Z80() = delete;					// Disable default constuctor. C++ 11 feature
	Z80(EmulatorContext* context);	// Only constructor with context param is allowed
	virtual ~Z80();


public:
	uint8_t InterruptVector();
	void CheckNextFrame();

	// TODO: convert obsolete naming
	void Z80FrameCycle();
	uint8_t m1_cycle();
	uint8_t in(uint16_t port);
	void out(uint16_t port, uint8_t val);
	void retn();

	// Memory access dispatching methods
	uint8_t rd(uint16_t addr);
	void wd(uint16_t addr, uint8_t val);
	// End of TODO: convert obsolete naming


	// Memory access implementation methods
	uint8_t MemoryReadFast(uint16_t addr);
	uint8_t MemoryReadDebug(uint16_t addr);
	void MemoryWriteFast(uint16_t addr, uint8_t val);
	void MemoryWriteDebug(uint16_t addr, uint8_t val);

	/*
	uint8_t Read(uint16_t addr);
	void Write(uint16_t addr, uint8_t val);
	*/

	uint8_t DirectRead(uint16_t addr);				// Direct emulated memory MemoryRead (For debugger use)
	void DirectWrite(uint16_t addr, uint8_t val);	// Direct emulated memory MemoryWrite (For debugger use)

	void Reset();		// Z80 chip reset
	void Z80Step();		// Single opcode execution

	// Trigger updates
public:
	void SetBanks();								// Reconfigure ROM/RAM banks
	inline void ProcessInterrupts(bool int_occured,	// Take care about incoming interrupts
		unsigned int_start,
		unsigned int_end);
	inline void UpdateScreen();			// Trigger screen update after each command cycle

	// Event handlers
public:
	void HandleNMI(ROMModeEnum mode);
	void HandleINT(uint8_t vector);

	// Debugger trigger updates
public:
	void ProcessDebuggerEvents();

protected:
	__forceinline void IncrementCPUCyclesCounter(uint8_t cycles);	// Increment cycle counters

	// Debug and trace methods
public:
	void DumpCurrentState();

	// TSConf specific
	// TODO: Move to plugin
protected:
	void ts_frame_int(bool vdos);
	void ts_line_int(bool vdos);
	void ts_dma_int(bool vdos);

	//region Debug methods
#ifdef _DEBUG
public:
	void DumpZ80State(char* buffer, size_t len);
#endif
	//endregion
};

