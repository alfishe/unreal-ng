#include "espxtensacpu.h"

#include <atomic>
#include <cstdio>
#include <cstring>

#include "3rdparty/unreal-xtensa/xtensa-cpu.h"

namespace vdac4
{

namespace
{

class EspXtensaCpu final : public IEspCpu
{
public:
#if UNREAL_XTENSA_JIT
    ~EspXtensaCpu() override
    {
        // POC 053: what the block JIT did
        const xtensa::Cpu::JitStats& s = _cpu.GetJitStats();
        if (s.compiles)
            std::fprintf(stderr, "xtensa-jit: compiles %llu rejected %llu flushes %llu bytes %llu calls %llu insns %llu\n",
                         static_cast<unsigned long long>(s.compiles), static_cast<unsigned long long>(s.rejected),
                         static_cast<unsigned long long>(s.flushes), static_cast<unsigned long long>(s.bytes),
                         static_cast<unsigned long long>(s.calls), static_cast<unsigned long long>(s.insns));
    }
#endif
    void Map(uint32_t base, uint32_t size, uint8_t* host, bool executable) override
    {
        const uint32_t flags = xtensa::RegionRead | xtensa::RegionWrite | (executable ? xtensa::RegionExec : 0u);
        _cpu.AddRegion(base, size, host, flags);
    }

    void SetTrapRange(uint32_t base, uint32_t size) override { _cpu.SetTrapRange(base, size); }

    void InvalidateCode(uint32_t /*address*/, uint32_t /*size*/) override
    {
        // The interpreter drops every predecoded block at once; code loads are rare (app start, reset)
        _cpu.InvalidateCode();
    }

    void HoldCode() override
    {
        _cpu.HoldCode();
    }
    bool KeepHeldCode() override
    {
        return _cpu.KeepHeldCode();
    }

#ifdef UNREAL_XTENSA_PROFILE
    void DumpProfile(const char* path) override { _cpu.DumpProfile(path); }
#endif
#if UNREAL_XTENSA_JIT
    void SetJit(bool on) override { _jit.store(on ? 1 : 0, std::memory_order_relaxed); }
#endif

    Result Run(uint64_t maxInstructions) override
    {
#if UNREAL_XTENSA_JIT
        if (const int jit = _jit.load(std::memory_order_relaxed); jit != _jitApplied)
        {
            _cpu.SetJit(jit != 0, 0);
            _jitApplied = jit;
        }
#endif
        const xtensa::RunResult run = _cpu.Run(maxInstructions);
        Result result;
        result.pc = run.pc;
        result.executed = run.executed;
        result.faultAddress = run.faultAddress;
        switch (run.reason)
        {
            case xtensa::StopReason::None:
            case xtensa::StopReason::Budget:
                result.reason = Stop::Budget;
                break;
            case xtensa::StopReason::Trap:
                result.reason = Stop::Trap;
                break;
            case xtensa::StopReason::Break:
                result.reason = Stop::Break;
                break;
            case xtensa::StopReason::IllegalInstruction:
            case xtensa::StopReason::DivideByZero:
                result.reason = Stop::Illegal;
                break;
            case xtensa::StopReason::Unimplemented:
                result.reason = Stop::Unimplemented;
                break;
            case xtensa::StopReason::MemoryFault:
                result.reason = Stop::MemoryFault;
                break;
            case xtensa::StopReason::Privileged:
            case xtensa::StopReason::Syscall:
                result.reason = Stop::Privileged;
                break;
        }
        return result;
    }

    uint32_t Arg(int index) override { return _cpu.Arg(index); }
    void ReturnFromTrap(uint32_t a2, uint32_t a3) override { _cpu.ReturnFromTrap(a2, a3); }

    void BeginCall(uint32_t pc, const uint32_t* args, int count, uint32_t stackPointer, uint32_t sentinel) override
    {
        _cpu.SetCallSentinel(sentinel);
        _cpu.BeginCall(pc, args, count, stackPointer);
    }

    uint32_t CallResult() override { return _cpu.EndCall(); }
    void ResetState() override { _cpu.Reset(); }

    void SaveState(std::vector<uint8_t>& out) const override
    {
        out.resize(sizeof(xtensa::CpuState));
        std::memcpy(out.data(), &_cpu.State(), sizeof(xtensa::CpuState));
    }

    void LoadState(const std::vector<uint8_t>& in) override
    {
        if (in.size() != sizeof(xtensa::CpuState))
            return;
        xtensa::CpuState state;
        std::memcpy(&state, in.data(), sizeof(state));
        _cpu.SetState(state);
    }

private:
    xtensa::Cpu _cpu;
#if UNREAL_XTENSA_JIT
    std::atomic<int> _jit{-1}; ///< SetJit: -1 none yet, 0 interpreter, 1 JIT
    int _jitApplied = -1;
#endif
};

}  // namespace

std::unique_ptr<IEspCpu> CreateEspXtensaCpu()
{
    return std::make_unique<EspXtensaCpu>();
}

}  // namespace vdac4
