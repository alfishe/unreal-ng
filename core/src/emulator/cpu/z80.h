#pragma once
#include "stdafx.h"

#include "emulator/cpu/cpulogic.h"
#include "emulator/emulatorcontext.h"
#include "emulator/memory/memory.h"

// Defined in /emulator/cpu/op_ddcb.cpp - pointers to registers in Z80 state
extern uint8_t* direct_registers[8];

/// region <Structures>

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

	unsigned eipos;
	unsigned haltpos;

	/*------------------------------*/
	uint8_t im;				// Interrupt mode [IM0|IM1|IM2]
	bool nmi_in_progress;
};

#pragma pack(pop)

struct Z80DecodedOperation
{
    union                                           // Opcode prefix (if available)
    {
        uint16_t prefix;
        struct
        {
            uint8_t prefix1;
            uint8_t prefix2;
        };
    };
	uint8_t opcode;                                 // Opcode fetched during Z80 M1 cycle

	union
	{
		uint16_t address;
		struct
		{
			uint8_t operand1;
			uint8_t operand2;
		};
	};
};

struct Z80State : public Z80Registers, public Z80DecodedOperation
{
	uint32_t z80_index; 							// CPU Enumeration index (for multiple Z80 in system, like Spectrum with GS/NGS)

	uint16_t prev_pc;                               // PC on previous cycle
	uint16_t m1_pc;                                 // PC when M1 cycle started


	unsigned rate;									// Rate for Z80 speed recalculations. 3.5MHz -> 256, 7MHz -> 128
	bool vm1;										// Halt handling type (True - ...; False - ...)
	uint8_t outc0;                                  // What to use when 'out (c), 0' is called

	uint16_t last_branch;
	unsigned trace_curs, trace_top, trace_mode;
	unsigned mem_curs, mem_top, mem_second;
	unsigned pc_trflags;
	uint16_t nextpc;

	// Debugger related
	uint8_t isDebugMode;									// Active breakpoints
	uint8_t dbgbreak;
	unsigned dbg_stophere;
	unsigned dbg_stopsp;
	unsigned dbg_loop_r1;
	unsigned dbg_loop_r2;

	#define MAX_CBP 16								// Up to 16 conditional breakpoints
	unsigned cbp[MAX_CBP][128];						// Conditional breakpoints data
	unsigned cbpn;
	int64_t debug_last_t;							// Used to find time delta

	int cycles_to_capture;                          // [NEW] Number of cycles to capture after trigger


	// Interrupts / HALT
	bool int_pending;							    // INT pending
	bool int_gate;									// External interrupts gate (True - enabled; False - disabled)
	unsigned halt_cycle;

	// CPU cycles counter
	uint32_t tpi;									// Ticks per interrupt (CPU cycles per video frame)
	uint32_t trpc[40];

	// Memory interfacing
	const MemoryInterface* FastMemIf;				// Fast memory interface (max performance)
	const MemoryInterface* DbgMemIf;				// Debug memory interface (supports memory access breakpoints)
	const MemoryInterface* MemIf;					// Currently selected memory interface (Fast|Debug)

	// TS-Conf specific
	// TODO: move to appropriate module
    uint32_t tscache_addr[TS_CACHE_SIZE];
    uint8_t  tscache_data[TS_CACHE_SIZE];
};

/// endregion </Structures>

class Z80 : public Z80State
{
    /// region <ModuleLogger definitions for Module/Submodule>
public:
    const PlatformModulesEnum _MODULE = PlatformModulesEnum::MODULE_Z80;
    const uint16_t _SUBMODULE = PlatformZ80SubmodulesEnum::SUBMODULE_Z80_GENERIC;
    ModuleLogger* _logger;
    /// endregion </ModuleLogger definitions for Module/Submodule>

    /// region <Fields>
protected:
	EmulatorContext* _context;
	Memory* _memory;

	uint8_t _trashRegister;        // Redirect DDCB operation writes with no destination registers here (related to op_ddcb.cpp and direct_registers[6] unused pointer)

    bool _pauseRequested = false;

protected:
	int _nmi_pending_count = 0;
    /// endregion </Fields>

    /// region <Properties>
public:
    bool IsPaused();
    /// endregion </Properties>

	/// region <Constructors / Destructors>
public:
	Z80() = delete;					// Disable default constructor. C++ 11 feature
	Z80(EmulatorContext* context);	// Only constructor with context param is allowed
	virtual ~Z80();
    /// endregion </Constructors / Destructors>

    /// region <Z80 lifecycle>
public:
	uint8_t m1_cycle();
	uint8_t in(uint16_t port);
	void out(uint16_t port, uint8_t val);
	void retn();

	// Memory access dispatching methods
	uint8_t rd(uint16_t addr, bool isExecution = false);
	void wd(uint16_t addr, uint8_t val);
    /// endregion </Z80 lifecycle>


    // Direct memory access methods
	uint8_t DirectRead(uint16_t addr);				// Direct emulated memory MemoryRead (For debugger use)
	void DirectWrite(uint16_t addr, uint8_t val);	// Direct emulated memory MemoryWrite (For debugger use)

	// Z80 CPU control methods
	void Reset();		// Z80 chip reset
	void Pause();
	void Resume();

	void Z80Step(bool skipBreakpoints = false);		// Single opcode execution

public:
    void CheckNextFrame();
    void Z80FrameCycle();

	// Trigger updates
public:
    void RequestMaskedInterrupt();
    void RequestNonMaskedInterrupt();
	inline void ProcessInterrupts(bool int_occured,	// Take care about incoming interrupts
    unsigned int_start,
    unsigned int_end);
	void UpdateScreen();			                // Trigger screen update after each command cycle

	// Event handlers
public:
	void HandleNMI(ROMModeEnum mode);
	void HandleINT(uint8_t vector = 0);

	// Debugger interfacing
public:
	void ProcessDebuggerEvents();
	void WaitUntilResumed();
	void (* callbackM1_Prefetch)();
	void (* callbackM1_Postfetch());

	void (* callbackCPUCycleFinished());

protected:
	__forceinline void IncrementCPUCyclesCounter(uint8_t cycles);	// Increment cycle counters

	// TSConf specific
	// TODO: Move to plugin
protected:
    uint8_t GetTSConfInterruptVector();
	void ts_frame_int(bool vdos);
	void ts_line_int(bool vdos);
	void ts_dma_int(bool vdos);

	/// region <Debug methods>
public:
    void DumpCurrentState();
    std::string DumpZ80State();
	void DumpZ80State(char* buffer, size_t len);

	std::string DumpCurrentFlags();
	static std::string DumpFlags(uint8_t flags);

	/// endregion </Debug methods>
};

