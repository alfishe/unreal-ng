#pragma once

/// @file espcpu.h
/// @brief What the VDAC4 ESP HLE needs from the Xtensa interpreter (one core's worth of execution).
///
/// EspAppHost runs every guest task on one interpreter, switching CPU states between them. This interface keeps
/// the host independent of the interpreter's exact API (core/src/3rdparty/unreal-xtensa); espxtensacpu.cpp adapts
/// it, a test fake can stand in.

#include <cstddef>
#include <cstdint>
#include <vector>

namespace vdac4
{

class IEspCpu
{
public:
    enum class Stop : uint8_t
    {
        Budget,         ///< ran the whole budget
        Trap,           ///< PC reached the trap page (before executing there)
        Break,          ///< BREAK / ILL as a guest assertion
        Illegal,        ///< undecodable instruction
        Unimplemented,  ///< decodable, not emulated (EE.*, MAC16, ...)
        MemoryFault,    ///< unmapped access
        Privileged,     ///< privileged instruction or an unsupported SYSCALL
    };

    struct Result
    {
        Stop reason = Stop::Budget;
        uint32_t pc = 0;
        uint64_t executed = 0;
        uint32_t faultAddress = 0;
    };

    virtual ~IEspCpu() = default;

    /// Guest memory: [base, base + size) backed by `host` (aliases allowed)
    virtual void Map(uint32_t base, uint32_t size, uint8_t* host, bool executable) = 0;
    /// PCs in [base, base + size) stop with Stop::Trap
    virtual void SetTrapRange(uint32_t base, uint32_t size) = 0;
    /// Guest code at [address, address + size) was written by the host: drop predecoded instructions
    virtual void InvalidateCode(uint32_t address, uint32_t size) = 0;

    virtual Result Run(uint64_t maxInstructions) = 0;
    /// Translated code (the JIT) or the interpreter, from the next Run on; any thread
    virtual void SetJit(bool /*on*/) {}
#ifdef UNREAL_XTENSA_PROFILE
    /// POC 051: the interpreter's per-block profile to `path`
    virtual void DumpProfile(const char* /*path*/) {}
#endif

    /// At a trap: argument word `index` as the callee sees it (windowed ABI, stack words past the sixth)
    virtual uint32_t Arg(int index) = 0;
    /// At a trap: return like the callee's RETW would, a2 (and a3) to the caller
    virtual void ReturnFromTrap(uint32_t a2, uint32_t a3 = 0) = 0;
    /// Start a CALL8-style call of `pc` with `args`, the stack at `stackPointer`, returning into `sentinel` (a
    /// trap address); the current state is the caller and stays below the new frame
    virtual void BeginCall(uint32_t pc, const uint32_t* args, int count, uint32_t stackPointer, uint32_t sentinel) = 0;
    /// At the sentinel trap after BeginCall: the callee's return value
    virtual uint32_t CallResult() = 0;
    /// A fresh state: windows reset, PS for user code, nothing live
    virtual void ResetState() = 0;

    /// Whole CPU state (registers, windows, loop, FPU, CCOUNT), plain bytes
    virtual void SaveState(std::vector<uint8_t>& out) const = 0;
    virtual void LoadState(const std::vector<uint8_t>& in) = 0;
};

}  // namespace vdac4
