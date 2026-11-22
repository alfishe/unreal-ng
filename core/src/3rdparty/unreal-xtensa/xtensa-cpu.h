// xtensa-cpu.h - user-mode interpreter of the Xtensa LX7 core as configured in the ESP32-S3 (unreal-xtensa).
//
// Dependency-free (C++17, standard library only), namespace xtensa. It runs application code compiled by GCC for the
// ESP32-S3 (windowed ABI) while the host emulates the firmware (HLE): imported functions point into a trap range and
// stop the interpreter, the host reads the arguments, does the work and returns into the guest.
//
// ISA: core, density, windowed registers (64 physical AR), MUL16/MUL32/MUL32_HIGH/DIV32, zero-overhead loops,
// booleans, MIN/MAX/SEXT/CLAMPS/NSA, SALT/SALTU, S32C1I, the single precision FPU (see "FPU" below), the special and
// user registers application code uses. Everything else stops Run() with a StopReason and the PC (see xtensa-isa.h for
// the decoder, which also knows MAC16 and the EE.* extension by length).
//
// ---- Memory -------------------------------------------------------------------------------------------------------
// AddRegion(guestBase, size, host, flags) maps host memory at a guest address; base and size are multiples of 4 KB.
// The same host buffer may be mapped at several guest addresses (the ESP32-S3 PSRAM is seen at 0x3C000000 on the
// D-bus and at 0x42000000 on the I-bus); every alias can carry its own flags. Little endian.
// Alignment: the ESP32-S3 core handles unaligned L16/L32/S16/S32 (and L32I.N, LSI/SSI, L32E/S32E) in hardware
// (core-isa.h XCHAL_UNALIGNED_LOAD_HW / STORE_HW = 1, no alignment exception), so unaligned accesses read/write the
// bytes at the exact address. S32C1I needs a 4-byte aligned address, as on the board: else MemoryFault.
// Unmapped addresses, writes to pages without Write, fetches from pages without Exec stop Run() with
// StopReason::MemoryFault and RunResult::faultAddress; the faulting instruction has no effect.
// Writes (guest or host Write*()) into bytes the interpreter has decoded invalidate the predecoded code; host code that
// writes guest memory through its own pointers must call InvalidateCode() afterwards if those bytes held code.
//
// ---- Running ------------------------------------------------------------------------------------------------------
// Run(maxInstructions) executes until the budget is spent or a stop. One instruction = one cycle; CCOUNT is the
// cycle counter (CpuState::ccount, settable). A window overflow / underflow / alloca exception is handled natively and
// counts the instructions the ESP-IDF vector would execute (5, 10 or 14 per frame), so a Run() can overshoot the
// budget by up to one handler. Zero-overhead loops follow the ISA: the loop-back happens when an instruction that is
// not a taken branch is followed by PC == LEND and LCOUNT != 0.
//
// ---- Windows ------------------------------------------------------------------------------------------------------
// WindowBase / WindowStart behave as on the board, with the window check done lazily per instruction like the
// hardware. Overflow and underflow do exactly what ESP-IDF's _WindowOverflow4/8/12 and _WindowUnderflow4/8/12 do
// (including the clobber of the handler's scratch registers), so stack bytes match the board. MOVSP with no live
// caller frame performs the alloca handler's underflow. SYSCALL with a2 == 0 spills all live windows (the ESP-IDF
// syscall 0 that newlib's setjmp uses) and continues at PC + 3; other SYSCALLs stop with StopReason::Syscall.
// SpillAllWindows() does the same for the host.
//
// ---- Traps (HLE) --------------------------------------------------------------------------------------------------
// SetTrapRange(base, size): reaching a PC inside it (by CALLn / CALLXn / CALL0 / J / JX / RET...) stops Run() with
// StopReason::Trap before anything there runs. At a trap reached by CALLn the callee has not run ENTRY yet:
//   Arg(i)               i-th argument as the callee sees it (a2 + i for i < 6, then the caller's stack at a1 + 4 * (i - 6)),
//                        for CALL4/8/12 (caller's a(4N + 2 + i)) and CALL0 (a2 + i); the kind comes from the last call
//   ReturnFromTrap(a2, a3)  the effect of the callee's ENTRY + RETW (or RET for CALL0): a2/a3 land in the caller's
//                        a(4N + 2)/a(4N + 3), PC = return address, the caller's window state unchanged.
// Host -> guest calls, nestable (an HLE function may call guest code, e.g. a qsort comparator):
//   BeginCall(pc, args, n, sp)  sets up a CALL4 to `pc` that returns to the call sentinel (SetCallSentinel; default
//                        trap base). Fresh context: the current window becomes the host frame with a1 = sp. At a trap:
//                        the trap callee's frame is entered first (as its ENTRY would, a1 = sp; later nested calls of the
//                        same trap reuse that frame and ignore sp), so read all Arg()s before BeginCall. Arguments
//                        beyond the sixth go to [sp + 4 * (i - 6)].
//   Run() until StopReason::Trap with AtCallReturn() true, then
//   EndCall(&a3)         returns the guest function's a2 (and a3), restores the PC (and the trap context) of the
//                        matching BeginCall.
// Trap and sentinel addresses must lie in 0x40000000..0x7FFFFFFF like code (RETW keeps the PC's top two bits).
//
// ---- State --------------------------------------------------------------------------------------------------------
// CpuState is trivially copyable: GetState()/SetState() for a scheduler's context switch or a snapshot. The
// predecoded code is not state. Mapping and trap configuration are not state either.
//
// ---- FPU ----------------------------------------------------------------------------------------------------------
// Bit exact on every host: each operation is one IEEE single operation (no contraction: the sources are built with
// -ffp-contract=off), MADD.S / MSUB.S / MADDN.S are fused with one rounding (std::fma). Rounding mode: nearest even
// only; FCR is stored and readable but its rounding mode field is not applied (GCC code never changes it). Any NaN
// result of an arithmetic operation is the default quiet NaN 0x7FC00000. FSR flags are not raised (stored only).
// ROUND/TRUNC/FLOOR/CEIL.S saturate to 0x7FFFFFFF / 0x80000000 (NaN -> 0x7FFFFFFF); UTRUNC.S: NaN and >= 2^32 ->
// 0xFFFFFFFF, negative inputs convert as TRUNC.S. FLOAT.S / UFLOAT.S scale by 2^-imm exactly.
// Not implemented (StopReason::Unimplemented): DIVN.S, CONST.S, DIV0.S, RECIP0.S, SQRT0.S, RSQRT0.S, NEXP01.S,
// MKSADJ.S, MKDADJ.S, ADDEXP.S, ADDEXPM.S (GCC calls __divsf3 / sqrtf for these apps), MAC16, EE.*.

#ifndef UNREAL_XTENSA_CPU_H
#define UNREAL_XTENSA_CPU_H

#include "xtensa-isa.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <type_traits>
#include <unordered_map>
#include <vector>

// POC 053: the region JIT (xtensa-jit.inc) on arm64 and x86_64 hosts (macOS, Linux; Windows x86_64; GCC, Clang,
// MSVC); off unless UNREAL_XTENSA_JIT=1. Not on Windows arm64 yet: the arm64 backend has never run there (no such
// host to check it on); the interpreter gives the same results
#if !defined(UNREAL_XTENSA_JIT)
#if defined(_WIN32) && (defined(__aarch64__) || defined(_M_ARM64))
#define UNREAL_XTENSA_JIT 0
#elif defined(__aarch64__) || defined(_M_ARM64) || defined(__x86_64__) || defined(_M_X64)
#define UNREAL_XTENSA_JIT 1
#else
#define UNREAL_XTENSA_JIT 0
#endif
#endif

namespace xtensa
{

struct RegionLoop; // POC 053 (xtensa-jit-a64.inc)
struct RegionBlock;

enum class StopReason : uint8_t
{
    None,
    Budget,             ///< maxInstructions executed
    Trap,               ///< PC entered the trap range (or is the call sentinel)
    Break,              ///< BREAK / BREAK.N
    IllegalInstruction, ///< ILL, unknown encoding, undefined window operation
    Unimplemented,      ///< MAC16, EE.*, FPU approximation steps, unmodelled user registers
    MemoryFault,        ///< unmapped / protected access or fetch, misaligned S32C1I; faultAddress is set
    Privileged,         ///< privileged instruction or special register (RSIL, RFE, WSR.EPS2, ...)
    DivideByZero,       ///< QUOS / QUOU / REMS / REMU by zero
    Syscall,            ///< SYSCALL with a2 != 0, SIMCALL
};

const char* StopReasonName(StopReason reason);

struct RunResult
{
    StopReason reason = StopReason::None;
    uint32_t pc = 0;           ///< next instruction (the stopping one, not executed)
    uint64_t executed = 0;     ///< instructions executed by this Run(), window handlers included
    uint32_t faultAddress = 0; ///< MemoryFault only
};

enum RegionFlags : uint32_t
{
    RegionRead = 1,
    RegionWrite = 2,
    RegionExec = 4,
    RegionAll = 7,
};

/// One level of BeginCall nesting. Part of CpuState so a snapshot can be taken inside a nested call.
struct HostFrame
{
    uint32_t kind = 0;          ///< 0 fresh context, 1 windowed trap, 2 CALL0 trap
    uint32_t savedPc = 0;
    uint32_t savedCallKind = 0;
    uint32_t savedTrapEntered = 0;
    uint32_t savedTrapArgSp = 0;
    uint32_t savedWindow[16] = {}; ///< CALL0 trap: a0..a15 to restore
};

constexpr int kMaxHostFrames = 8;

/// The whole architectural state the interpreter models (plus the host call bookkeeping).
struct CpuState
{
    uint32_t ar[64] = {};       ///< physical address registers
    uint32_t pc = 0;
    uint32_t sar = 0;
    uint32_t lbeg = 0;
    uint32_t lend = 0;
    uint32_t lcount = 0;
    uint32_t windowBase = 0;    ///< 0..15
    uint32_t windowStart = 1;   ///< 16 bits
    uint32_t ps = 0x00040020;   ///< PS: WOE = 1, UM = 1, INTLEVEL = 0, EXCM = 0; CALLINC in bits 16..17
    uint32_t scompare1 = 0;
    uint32_t atomctl = 0x28;
    uint32_t cpEnable = 1;
    uint32_t prid = 0xCDCD;     ///< ESP32-S3: 0xCDCD core 0, 0xABAB core 1
    uint32_t threadPtr = 0;
    uint32_t fcr = 0;
    uint32_t fsr = 0;
    uint32_t br = 0;            ///< b0..b15
    uint32_t fr[16] = {};       ///< FPU registers as bit patterns
    uint64_t ccount = 0;        ///< cycles = instructions executed
    uint32_t callKind = 2;      ///< N of the last CALLn / CALLXn (0 for CALL0 / CALLX0), for the trap helpers
    uint32_t trapEntered = 0;   ///< the host entered the current trap callee's frame
    uint32_t trapArgSp = 0;     ///< caller's a1 when the trap frame was entered (stack arguments)
    uint32_t hostDepth = 0;
    HostFrame hostFrames[kMaxHostFrames] = {};
};
static_assert(std::is_trivially_copyable<CpuState>::value, "CpuState must stay trivially copyable");

class Cpu
{
public:
    Cpu();
    ~Cpu();
    Cpu(const Cpu&) = delete;
    Cpu& operator=(const Cpu&) = delete;

    // ---- memory ----
    bool AddRegion(uint32_t guestBase, uint32_t size, uint8_t* host, uint32_t flags);
    void ClearRegions();
    bool Read8(uint32_t addr, uint8_t& value);
    bool Read16(uint32_t addr, uint16_t& value);
    bool Read32(uint32_t addr, uint32_t& value);
    bool Write8(uint32_t addr, uint8_t value);
    bool Write16(uint32_t addr, uint16_t value);
    bool Write32(uint32_t addr, uint32_t value);
    bool ReadBlock(uint32_t addr, void* dst, size_t size);
    bool WriteBlock(uint32_t addr, const void* src, size_t size);
    /// Host pointer for [addr, addr + size) if it is mapped contiguously with `flags`, else nullptr.
    uint8_t* HostPointer(uint32_t addr, uint32_t size, uint32_t flags = RegionRead);
    void InvalidateCode();

    // ---- traps and host calls ----
    void SetTrapRange(uint32_t base, uint32_t size);
    void SetCallSentinel(uint32_t pc);
    uint32_t CallSentinel() const { return sentinel_; }
    bool InTrapRange(uint32_t pc) const { return pc - trapBase_ < trapSize_; }
    bool AtCallReturn() const { return ((state_.pc ^ sentinel_) & 0x3FFFFFFFu) == 0; }
    uint32_t Arg(int i);
    void ReturnFromTrap(uint32_t a2, uint32_t a3 = 0);
    bool BeginCall(uint32_t pc, const uint32_t* args, int n, uint32_t stackPointer);
    uint32_t EndCall(uint32_t* a3 = nullptr);
    /// Spills every live register window except the current one to the stack (xthal_window_spill). At a windowed trap
    /// that the host has not entered, the trap callee's frame is entered first (a1 = caller's a1 - 16) so the trap's
    /// caller is spilled too. Returns false on a memory fault.
    bool SpillAllWindows();

    // ---- running ----
    RunResult Run(uint64_t maxInstructions);

    // ---- state ----
    CpuState& State() { return state_; }
    const CpuState& State() const { return state_; }
    void GetState(CpuState& out) const { out = state_; }
    void SetState(const CpuState& in);
    void Reset();
    /// Address register of the current window (a0..a15).
    uint32_t& Ar(int n) { return state_.ar[(state_.windowBase * 4 + static_cast<uint32_t>(n)) & 63]; }
    uint32_t Pc() const { return state_.pc; }
    void SetPc(uint32_t pc) { state_.pc = pc; }
    float Fr(int n) const;
    void SetFr(int n, float v);

    // ---- statistics ----
    size_t BlockCount() const { return blockMap_.size(); }
#ifdef UNREAL_XTENSA_PROFILE
    /// POC 053 (compile-time only): per-block entries and executed instructions, as text lines
    /// "pc len entries insns lastop ops..." (ops: the block's mnemonics), retired blocks folded in
    void DumpProfile(const char* path) const;
#endif

#if UNREAL_XTENSA_JIT
    /// POC 053: the block JIT on / off and after how many entries a block is compiled
    void SetJit(bool on, uint32_t hot);
    struct JitStats
    {
        uint64_t compiles = 0;
        uint64_t rejected = 0;
        uint64_t flushes = 0;
        uint64_t bytes = 0;
        uint64_t calls = 0;
        uint64_t insns = 0; ///< instructions the regions ran
        uint64_t regionBlocks = 0;
        uint64_t total = 0;          ///< all instructions run
        uint64_t declineGroup = 0;   ///< region not entered: window check needed
        uint64_t declineLoop = 0;    ///< region not entered: live loop end inside
        uint64_t declineBudget = 0;
        uint64_t noRegion = 0;       ///< block entries without a region (cold or rejected)
        uint64_t runs = 0;
    };
    const JitStats& GetJitStats() const { return jitStats_; }
#endif

private:
    struct PageEntry;
    struct CodePage;
    struct Block;
    struct Region
    {
        uint32_t base;
        uint32_t size;
        uint8_t* host;
        uint32_t flags;
    };
    struct ExecOut
    {
        StopReason stop = StopReason::None;
        uint32_t executed = 0;
        uint32_t faultAddress = 0;
    };

    PageEntry* Entry(uint32_t addr) const;
    uint8_t* Xlate(uint32_t addr, uint32_t flag) const;
    bool Load(uint32_t addr, int size, uint32_t& value);
    bool Store(uint32_t addr, int size, uint32_t value);
    void CodeWriteCheck(PageEntry& e, uint32_t addr, int size);
    void MarkCode(uint32_t addr, int len);
    Block* Lookup(uint32_t pc);
    Block* Build(uint32_t pc);
    ExecOut Exec(Block* b, uint64_t maxInstructions, uint64_t& executed);
    void FreeRetired();

    // windows
    void UpdateSafeGroups();
    bool WindowCheck(int group, uint32_t& faultAddress);
    bool SpillFrame(uint32_t m, uint32_t& faultAddress);
    bool FillFrame(uint32_t m, uint32_t n, uint32_t& faultAddress);
    bool SpillAll(uint32_t& faultAddress);
    void EnterTrapFrame(uint32_t sp);
    uint32_t& Phys(uint32_t group, uint32_t n) { return state_.ar[(group * 4 + n) & 63]; }

    CpuState state_;
    uint32_t safeGroups_ = 3;
    uint64_t handlerCycles_ = 0;
    bool codeModified_ = false;

    std::vector<Region> regions_;
    std::unique_ptr<PageEntry[]> l1_[1024];
    std::unordered_map<uintptr_t, std::unique_ptr<CodePage>> codePages_;
    std::vector<PageEntry*> codeEntries_;

    std::unordered_map<uint32_t, std::unique_ptr<Block>> blockMap_;
    std::vector<std::unique_ptr<Block>> retired_;
    static constexpr uint32_t kLookupSize = 16384; ///< direct-mapped block cache, 32 KB of code without conflicts
    Block* lookup_[kLookupSize] = {};
    static constexpr uint32_t kMaxBlockInsns = 64;
    Insn partial_[kMaxBlockInsns + 1]; ///< a block cut short (budget, loop end inside) plus its BlockEnd

#ifdef UNREAL_XTENSA_PROFILE
    struct ProfileEntry
    {
        uint32_t len = 0;
        uint64_t entries = 0;
        uint64_t insns = 0;
        std::vector<uint16_t> ops;
    };
    void ProfileFold(const Block& b);
    std::unordered_map<uint32_t, ProfileEntry> profileRetired_;
    Block* profileLast_ = nullptr;
    uint64_t profileMark_ = 0;
#endif

#if UNREAL_XTENSA_JIT
    struct JitArena;
    struct JitArenaDelete
    {
        void operator()(JitArena* a) const;
    };
    void JitCompile(Block& head);
    bool Discover(uint32_t headPc, const RegionLoop& loop, std::vector<RegionBlock>& blocks, RegionLoop& found);
    void JitInitFromEnv();
    bool jitOn_ = false;
    uint32_t jitHot_ = 8;
    std::unique_ptr<JitArena, JitArenaDelete> jitArena_;
    JitStats jitStats_;
#endif

    uint32_t trapBase_ = 0;
    uint32_t trapSize_ = 0;
    uint32_t sentinel_ = 0;
};

} // namespace xtensa

#endif // UNREAL_XTENSA_CPU_H
