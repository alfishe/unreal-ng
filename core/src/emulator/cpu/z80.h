#pragma once
#include <functional>

#include "emulator/cpu/cpulogic.h"
#include "emulator/emulatorcontext.h"
#include "emulator/memory/memory.h"
#include "stdafx.h"

// Defined in /emulator/cpu/op_ddcb.cpp - pointers to registers in Z80 state
extern uint8_t* direct_registers[8];

// Forward declaration
class OpcodeProfiler;

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
            uint8_t iff1;    // Interrupt enable flip-flop 1. Disables interrupts from being accepted
            uint8_t iff2;    // Interrupt enable flip-flop 2. Temporary storage location for IFF1
            uint8_t halted;  // CPU halted
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

    /// region <Undocumented Internal Registers>
    ///
    /// The Z80 contains internal registers not exposed in official documentation but discovered
    /// through reverse engineering and behavioral analysis. These registers affect observable
    /// behavior of certain instructions, particularly undocumented flag bits 3 (XF) and 5 (YF).
    ///
    /// Reference: "The Undocumented Z80 Documented" by Sean Young
    ///            "MEMPTR" research by Boo-boo, Vladimir Kladov (zx.pk.ru, 2006)
    ///            "Z80 XCF Flavor" by Manuel Sainz de Baranda y Goñi (zxe.io, 2022-2024)

    /// MEMPTR (also known as WZ) - 16-bit internal address buffer register
    ///
    /// Used by the Z80 for 16-bit address calculations and temporary storage during
    /// multi-byte memory operations. Its value affects the undocumented YF/XF flags
    /// in the BIT n,(HL) instruction, where bits 11 and 13 of MEMPTR are copied to
    /// flags bits 3 and 5 respectively.
    ///
    /// Various instructions set MEMPTR:
    /// - LD A,(addr) / LD (addr),A: MEMPTR = addr + 1 (low byte), high byte varies
    /// - LD A,(rp) / LD (rp),A: MEMPTR = rp + 1
    /// - ADD/ADC/SBC rp1,rp2: MEMPTR = rp1_before + 1
    /// - JP/CALL/JR/RET/RST: MEMPTR = target address
    /// - IN/OUT instructions: MEMPTR derived from port address
    /// - Block instructions (LDIR, CPIR, etc.): Various behaviors
    /// - Any instruction with (IX+d)/(IY+d): MEMPTR = INDEX + d
    union
    {
        uint16_t memptr;
        struct
        {
            uint8_t meml;
            uint8_t memh;
        };
    };

    /// Q Register - 8-bit internal flag capture register (only bits 3 and 5 are significant)
    ///
    /// Discovered in 2018-2024, Q captures the YF/XF bits (bits 5/3) from the Flags register
    /// after each instruction that modifies flags. It affects the undocumented behavior of
    /// CCF and SCF instructions.
    ///
    /// On genuine Zilog Z80, CCF and SCF compute undocumented flags as:
    ///     YF = A.5 | (F.5 & Q.5)
    ///     XF = A.3 | (F.3 & Q.3)
    /// Or simplified: undoc_flags = (A | (F & Q)) & 0x28
    ///
    /// Different Z80 clones exhibit different behavior:
    /// - Zilog (original): Uses Q register as described above
    /// - NEC NMOS clones: Ignore Q, use only A register: undoc_flags = A & 0x28
    /// - ST CMOS clones: Asymmetric YF/XF behavior (different formulas for each bit)
    ///
    /// The XCF Flavor test (https://zxe.io) can distinguish these variants.
    /// This emulator implements genuine Zilog Z80 behavior.
    uint8_t q;

    /// endregion </Undocumented Internal Registers>

    // Unused layout slot (formerly the t-state of the last EI). The INT shadow
    // after EI is now Z80State::boundary; the slot stays so this packed block
    // keeps the layout unreal-z80's Z80CpuRegisterFile mirrors (reservedEipos).
    int32_t eipos;
    uint16_t haltpos;

    /*------------------------------*/
    uint8_t im;  // Interrupt mode [IM0|IM1|IM2]
    bool nmi_in_progress;
};

#pragma pack(pop)

struct Z80DecodedOperation
{
    union  // Opcode prefix (if available)
    {
        uint16_t prefix;
        struct
        {
            uint8_t prefix1;
            uint8_t prefix2;
        };
    };

    uint8_t opcode;  // Opcode fetched during Z80 M1 cycle

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

/// Instruction-boundary state: what the CPU carries from one instruction
/// boundary to the next beyond the registers, i.e. what decides the next
/// Model-side observer of Z80 M1 cycles (see Z80::machineM1Hook)
class IMachineM1Hook
{
public:
    virtual ~IMachineM1Hook() = default;
    /// Before the opcode read: board logic that changes what the fetch sees
    /// (ZX-Evo trdemu swaps its RAM page in for the next fetch)
    virtual void BeforeMachineM1(uint16_t address) { (void)address; }
    /// After the opcode read (the refresh edge)
    /// @param address the address the opcode byte was fetched from
    virtual void OnMachineM1(uint16_t address) = 0;
};

/// INT/NMI acceptance. One value at a time - each is a property of the last
/// instruction or the last acknowledge. Set by that instruction/acknowledge,
/// cleared when the next Z80Step starts. Same values and meaning as
/// unreal-z80's Z80CpuBoundary.
enum Z80BoundaryEnum : uint8_t
{
    Z80_BOUNDARY_NONE = 0,
    /// A redundant DD/FD prefix (followed by another DD/FD) ended the step:
    /// that next prefix is fetched and its instruction runs in the next
    /// Z80Step. INT and NMI are refused (no boundary inside an instruction).
    Z80_BOUNDARY_PREFIX_DD,
    Z80_BOUNDARY_PREFIX_FD,
    /// After EI, or after a RETN/RETI that set IFF1 (IFF2 reaches IFF1 too
    /// late for this boundary's INT sampling): INT refused, NMI accepted.
    Z80_BOUNDARY_INT_SHADOW,
    /// After LD A,I / LD A,R: an INT accepted here clears P/V (NMOS: the
    /// acknowledge clears IFF2 before the copy settles). NMI does not.
    Z80_BOUNDARY_LD_A_IR,
    /// An NMI was just acknowledged: a second NMI is refused until an
    /// instruction has run.
    Z80_BOUNDARY_NMI_ACK,
};

struct Z80State : public Z80Registers, public Z80DecodedOperation
{
    uint8_t boundary = Z80_BOUNDARY_NONE;  // Z80BoundaryEnum (see above)

    uint32_t z80_index;  // CPU Enumeration index (for multiple Z80 in system, like Spectrum with GS/NGS)

    uint16_t prev_pc;  // PC on previous cycle
    uint16_t m1_pc;    // PC when M1 cycle started

    unsigned rate;  // Rate for Z80 speed recalculations. 3.5MHz -> 256, 7MHz -> 128
    bool vm1;       // Halt handling type (True - ...; False - ...)
    uint8_t outc0;  // What to use when 'out (c), 0' is called

    uint16_t last_branch;
    unsigned trace_curs, trace_top, trace_mode;
    unsigned mem_curs, mem_top, mem_second;
    unsigned pc_trflags;
    uint16_t nextpc;

    // Debugger related - master switch for all features
    bool isDebugMode;

    int cycles_to_capture = 0;  // [NEW] Number of cycles to capture after trigger

    // Interrupts / HALT
    bool int_pending;  // INT pending
    /// The current INT pulse was acknowledged and, on machines whose INT is
    /// cleared by the acknowledge (ZX-Evo: zint.v ends the pulse on IORQ+M1),
    /// stays down until the pulse window ends - one INT per pulse however
    /// short the handler and however long the (turbo-scaled) window
    uint8_t int_acked_in_pulse = 0;
    bool int_gate;     // External interrupts gate (True - enabled; False - disabled)
    unsigned halt_cycle;

    // CPU cycles counter
    uint32_t tpi;  // Ticks per interrupt (CPU cycles per video frame)
    uint32_t trpc[40];

    // Memory interfacing
    const MemoryInterface* FastMemIf;           // Fast memory interface (max performance)
    const MemoryInterface* DbgMemIf;            // Debug memory interface (supports memory access breakpoints)
    const MemoryInterface* FastContendedMemIf;  // Fast + video memory contention (Memory::MemoryReadContended)
    const MemoryInterface* DbgContendedMemIf;   // Debug + video memory contention
    const MemoryInterface* MemIf;               // Currently selected one (Core::SelectMemoryInterface)
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

    uint8_t _trashRegister;  // Redirect DDCB operation writes with no destination registers here (related to
                             // op_ddcb.cpp and direct_registers[6] unused pointer)

protected:
    int _nmi_pending_count = 0;
    
    // Opcode profiling
    OpcodeProfiler* _opcodeProfiler = nullptr;
    bool _feature_opcodeprofiler_enabled = false;

    // Call trace capture — control-flow events go through the memory access
    // tracker's CallTraceBuffer; the session state is polled live, the cached
    // flag only avoids the lookup when the calltrace feature is off
    bool _feature_calltrace_enabled = false;
    /// endregion </Fields>

    /// region <Constructors / Destructors>
public:
    Z80() = delete;                 // Disable default constructor. C++ 11 feature
    Z80(EmulatorContext* context);  // Only constructor with context param is allowed
    virtual ~Z80();
    /// endregion </Constructors / Destructors>

    /// region <Z80 lifecycle>
public:
    uint8_t m1_cycle();
    void RecordInstructionStart(uint16_t addr);  // m1_pc + instruction-start observers (once per instruction)
    bool InstructionStartObserved() const;       // any observer armed (trace hook, TTD coverage/probe)
    void NotifyInstructionStart();               // run the observers for the instruction at m1_pc
    void NotifyMachineM1Before(uint16_t address);  // run machineM1Hook before the opcode read
    void NotifyMachineM1(uint16_t address);      // run machineM1Hook (out of line, see m1_cycle)
    uint8_t in(uint16_t port);
    void out(uint16_t port, uint8_t val);
    void retn();

    // Memory access dispatching methods
    uint8_t rd(uint16_t addr, bool isExecution = false);
    void wd(uint16_t addr, uint8_t val);

    /// Contention wait states inserted by the contended memory interfaces (Memory::MemoryReadContended):
    /// the same counter step as the CPU's own cycles
    inline void InsertWaitStates(uint8_t cycles) { tt += cycles * rate; }

    /// T-state at which the memory access in progress started: rd / wd have already charged its 3 T
    inline uint32_t AccessStartT() const { return (tt - 3u * rate) >> 8; }

    /// Internal (no-MREQ) cycles: `cycles` T-states with `addr` on the address bus (HL, PC, SP, IR... per
    /// instruction). The Ferranti ULA (48K / 128K / +2) contends each of them like the start of a memory
    /// cycle when `addr` is in a contended slot; the +2A/+3 gate array contends MREQ cycles only. Without
    /// that rule (and without a bus trace hook) this is the plain cycle count
    inline void Idle(uint16_t addr, uint8_t cycles)
    {
        if (idleContention || busTraceHook) [[unlikely]]
            IdleSlow(addr, cycles);
        else
            tt += cycles * rate;
    }
    void IdleSlow(uint16_t addr, uint8_t cycles);  // contention per T-state and the 'N' trace events

    /// The refresh address the CPU puts on the bus in internal cycles after M1 (I in the high byte, R low)
    inline uint16_t IR() const { return static_cast<uint16_t>((i << 8) | (r_low & 0x7F) | (r_hi & 0x80)); }

    /// The ULA's rule for internal cycles: set with ioContention (the same machines), null otherwise
    UlaContention* idleContention = nullptr;

    /// I/O contention rule for in / out: the machine's contention component while its ULA contends port
    /// accesses (48K / 128K / +2), null otherwise (no contention, +2A / +3 gate array). Set by
    /// Core::SelectMemoryInterface together with MemIf
    UlaContention* ioContention = nullptr;

    /// Test-only bus trace hook (null in production - a single empty-function
    /// check per bus access when unset). Fired at the access point of each bus
    /// event with cpu.t already advanced to it:
    ///   'R' memory read (data latched at T3 of the cycle - rd() charges first)
    ///   'W' memory write, 'I' port read, 'O' port write (IORQ T-state),
    ///   'N' one internal (no-MREQ) T-state, fired at its start with the address on the bus (value 0)
    /// Used by bus-phase timing tests (io_phase_test / bus_phase tests).
    std::function<void(char type, uint16_t addr, uint8_t value)> busTraceHook;

    /// Test-only instruction-fetch trace hook (null in production - a single
    /// empty-function check per instruction when unset). Fired once per
    /// executed instruction at the M1 cycle, with the PC of the opcode itself;
    /// prefixed opcodes report the PC of the prefix, not of the continuation.
    /// Kept separate from busTraceHook so that adding it does not perturb the
    /// event counts the bus-phase timing tests assert on.
    std::function<void(uint16_t pc)> m1TraceHook;

    /// Machine hook on every M1 cycle, prefix fetches included, called right
    /// after the opcode read (where the refresh cycle starts - the edge board
    /// logic such as the ZX-Evo NMI exit counter and breakpoint compare act on).
    /// Null unless a model decoder needs it: one pointer test per M1
    IMachineM1Hook* machineM1Hook = nullptr;
    /// endregion </Z80 lifecycle>

    // Direct memory access methods
    uint8_t DirectRead(uint16_t addr);             // Direct emulated memory MemoryRead (For debugger use)
    void DirectWrite(uint16_t addr, uint8_t val);  // Direct emulated memory MemoryWrite (For debugger use)

    // Z80 CPU control methods
    void Reset();  // Z80 chip reset
    void Z80Step(bool skipBreakpoints = false);  // Single opcode execution
    bool RunInstructionStartHooks(bool skipBreakpoints);  // true: a trap consumed the instruction

public:
    /// Run instructions until the frame T-state limit (the continuous main
    /// loop's CPU pass). Frame-start work is NOT done here: it belongs to the
    /// frame boundary (BeginFrame, called by MainLoop::CompleteFrame /
    /// RestartFrame), so every run path shares one lifecycle
    void Z80FrameCycle();

    /// What one StepInstruction() did besides executing code
    struct StepResult
    {
        bool nmiAccepted = false;  // the step was an NMI acceptance (no opcode ran)
        bool intAccepted = false;  // the step was a maskable INT acceptance (no opcode ran)
    };

    /// The single CPU stepping primitive every run path uses (main loop,
    /// Emulator::Run*, TTD replay): interrupt sampling against the current
    /// frame geometry, then either the interrupt acceptance or one opcode,
    /// then the per-step peripheral dispatch. An accepted interrupt IS the
    /// step - no opcode runs in the same iteration
    StepResult StepInstruction(bool skipBreakpoints = false);

    /// The CPU is at or past the end of the current frame
    bool IsFrameComplete() const { return t >= _frameLimit; }

    /// CPU half of the frame start (MainLoop::CompleteFrame / RestartFrame):
    /// apply the queued frequency multiplier, derive the frame geometry, and
    /// raise the INT carried over from the previous frame when the INT window
    /// wraps the frame end. Must run exactly once per frame, before its first
    /// instruction
    void BeginFrame();

    /// @brief Apply the queued frequency multiplier change, if any.
    ///
    /// The effective multiplier composes the host speed control
    /// (next_z80_frequency_multiplier) with the Scorpion hardware turbo
    /// state (hw_turbo_shift, driven by the model decoder - Scorpion: scorpion_turbo,
    /// hardware-reference 13). Called once per frame start by BeginFrame, so
    /// the scaled INT window / frame limit are always derived from the
    /// applied value.
    void ApplyQueuedFrequencyMultiplier();

    /// Apply a HARDWARE turbo change immediately, mid-frame (model port decoders
    /// call this right after flipping EmulatorState::hw_turbo_shift). Hardware
    /// switches the clock on the next cycle, and firmware relies on it: the
    /// Scorpion ProfROM monitor strobes IN (#7FFD) and immediately runs an
    /// INT-bounded count loop to detect the 7 MHz clock - deferring the switch
    /// to the frame boundary made that test always fail. Rescales the in-frame
    /// T-state position so the raster/INT instant is preserved, then refreshes
    /// the frame geometry the running Z80FrameCycle loop reads
    void ApplyHardwareTurboNow();

    /// (Re)derive the scaled frame length and INT window from the current
    /// multiplier; read by StepInstruction every step. Pure derivation (no
    /// INT raise): safe mid-frame (hardware turbo) and after a TTD restore
    void RecomputeFrameTiming();

    /// Post NC_CPU_FREQ_CHANGED with the applied frequency/multiplier. Shared
    /// by both apply paths so host speed-menu changes and guest hardware-turbo
    /// strobes notify UI/automation consumers identically (the Scorpion
    /// ProfROM monitor flips the clock mid-frame via IN (#7FFD/#1FFD), which
    /// never passes through a frame boundary)
    void NotifyCPUFrequencyChanged();

    uint32_t _frameLimit = 0;   // config.frame * multiplier
    unsigned _intStart = 0;     // config.intstart * multiplier
    unsigned _intEnd = 0;       // (config.intstart + intlen) * multiplier, wrapped into the frame when _intWraps
    bool _intWraps = false;     // INT window crosses the frame end: raised at frame start (BeginFrame)

    // Trigger updates
public:
    void RequestMaskedInterrupt();
    void RequestNonMaskedInterrupt();
    bool IntClearedByAcknowledge() const;  // machine's INT pulse ends at the acknowledge
    bool ProcessInterrupts(bool int_occured,  // Take care about incoming interrupts
                           unsigned int_start, unsigned int_end);  // Returns true if INT was handled (skip Z80Step)

    // Event handlers
public:
    void HandleNMI(ROMModeEnum mode);
    void HandleINT(uint8_t vector = 0xFF);
    void OnCPUStep();  // Trigger state updates after each CPU command cycle

    // Debugger interfacing
public:
    void ProcessDebuggerEvents();
    void (*callbackM1_Prefetch)();   // Corrected function pointer declaration
    void (*callbackM1_Postfetch)();  // Corrected function pointer declaration

    void (*callbackCPUCycleFinished)();  // Corrected function pointer declaration

protected:
    __forceinline void IncrementCPUCyclesCounter(uint8_t cycles);  // Increment cycle counters

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
    
    /// region <Feature Cache>
    void UpdateFeatureCache();
    OpcodeProfiler* GetOpcodeProfiler() { return _opcodeProfiler; }
    /// endregion </Feature Cache>

    /// region <Register Access API>
public:
    struct RegisterInfo
    {
        const char* name;
        bool is16bit;
        bool isAlternate;
        uint16_t (*getter)(const Z80State*);
        void (*setter)(Z80State*, uint16_t);
    };

    static const RegisterInfo* GetRegisterInfo();
    static size_t GetRegisterCount();
    static const RegisterInfo* FindRegister(const std::string& name);
    static bool GetRegisterValue(Z80State* state, const std::string& name, uint16_t& value, bool& is16bit);
    static bool SetRegisterValue(Z80State* state, const std::string& name, uint16_t value);
    /// endregion </Register Access API>
};
