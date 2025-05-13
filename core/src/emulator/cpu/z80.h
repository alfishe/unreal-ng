#pragma once
#include "stdafx.h"

#include "emulator/cpu/cpulogic.h"
#include "emulator/emulatorcontext.h"
#include "emulator/memory/memory.h"

// Defined in /emulator/cpu/op_ddcb.cpp - pointers to registers in Z80 state
extern uint8_t* direct_registers[8];

/// region <Structures>

// Disable compiler alignment for packed structures
#pragma pack(push, 1)

struct Z80Registers
{
    union
    {
        uint32_t tt;
        struct
        {
                unsigned t_l : 8;
                unsigned t : 24;
        };
    };

    /*------------------------------*/
    union
    {
        uint16_t pc;
        struct
        {
                uint8_t pcl;
                uint8_t pch;
        };
    };

    union
    {
        uint16_t sp;
        struct
        {
            uint8_t spl;
            uint8_t sph;
        };
    };

    // IR - Instruction register
    union
    {
        uint16_t ir_;
        struct
        {
            uint8_t r_low;
            uint8_t i;
        };
    };

    /// The purpose of IFF2 is to save the status of IFF1 when a nonmaskable interrupt occurs.
    /// When a nonmaskable interrupt is accepted, IFF1 resets to prevent further interrupts until
    /// reenabled by the programmer. Therefore, after a nonmaskable interrupt is accepted, maskable
    /// interrupts are disabled but the previous state of IFF1 is saved so that the complete
    /// state of the CPU just prior to the nonmaskable interrupt can be restored at any time. When
    /// a Load Register A with Register I (LD A, I) instruction or a Load Register A with Register R
    /// (LD A, R) instruction is executed, the state of IFF2 is copied to the parity flag, where it
    /// can be tested or stored.
    /// A second method of restoring the status of IFF1 is through the execution of a Return From
    /// Nonmaskable Interrupt (RETN) instruction. This instruction indicates that the nonmaskable
    /// interrupt service routine is complete and the contents of IFF2 are now copied back
    /// into IFF1 so that the status of IFF1 just prior to the acceptance of the nonmaskable interrupt
    /// is restored automatically.
    /// @see https://www.zilog.com/docs/z80/um0080.pdf
    union
    {
        uint32_t int_flags;
            struct
            {
                uint8_t r_hi;
                uint8_t iff1;	// Interrupt enable flip-flop 1. Disables interrupts from being accepted
                uint8_t iff2;	// Interrupt enable flip-flop 2. Temporary storage location for IFF1
                uint8_t halted;	// CPU halted
            };
	};

    /*------------------------------*/
    union
    {
        uint16_t bc;
            struct
            {
                uint8_t c;
                uint8_t b;
            };
	};

    union
    {
        uint16_t de;
            struct
            {
                uint8_t e;
                uint8_t d;
            };
	};

    union
    {
        uint16_t hl;
        struct
        {
            uint8_t l;
            uint8_t h;
        };
    };

    union
    {
        uint16_t af;
        struct
        {
            uint8_t f;
            uint8_t a;
        };
    };

    /*------------------------------*/
    union
    {
        uint16_t ix;
            struct
            {
                uint8_t xl;
                uint8_t xh;
            };
	};
    union
    {
        uint16_t iy;
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
            uint16_t bc;
            struct
            {
                uint8_t c;
                uint8_t b;
            };
        };

        union
        {
            uint16_t de;
            struct
            {
                uint8_t e;
                uint8_t d;
            };
        };
        union
        {
            uint16_t hl;
            struct
            {
                uint8_t l;
                uint8_t h;
            };
        };
        union
        {
            uint16_t af;
            struct
            {
                uint8_t f;
                uint8_t a;
            };
        };
    } alt;

    union
    {
        uint16_t memptr; // undocumented register
        struct
        {
            uint8_t meml;
            uint8_t memh;
        };
    };

    uint32_t cycle_count;       // Counter to track cycle accuracy
    uint32_t tStatesPerFrame;   // How many t-states already spent during current frame

    uint16_t eipos;
    uint16_t haltpos;

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

    uint64_t clock_count;                           // Monotonically increasing clock edge counter. Doesn't depend on any rates recalculation

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

    int cycles_to_capture = 0;                      // [NEW] Number of cycles to capture after trigger


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
    void Z80FrameCycle();

    // Trigger updates
public:
    void RequestMaskedInterrupt();
    void RequestNonMaskedInterrupt();
    inline void ProcessInterrupts(bool int_occured,	// Take care about incoming interrupts
    unsigned int_start,
    unsigned int_end);

    // Event handlers
public:
    void HandleNMI(ROMModeEnum mode);
    void HandleINT(uint8_t vector = 0xFF);
    void OnCPUStep();			                // Trigger state updates after each CPU command cycle


    // Debugger interfacing
public:
    void ProcessDebuggerEvents();
    void WaitUntilResumed();
    void (*callbackM1_Prefetch)(); // Corrected function pointer declaration
    void (*callbackM1_Postfetch)(); // Corrected function pointer declaration

    void (*callbackCPUCycleFinished)(); // Corrected function pointer declaration

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
