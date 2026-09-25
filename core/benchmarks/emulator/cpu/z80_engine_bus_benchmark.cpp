// z80_engine_bus_benchmark.cpp — decomposes the unreal-z80 integration's
// per-instruction cost into its candidate causes: the callback-bus trampoline
// vs the register-file marshalling, and (since library 0.3.0's zero-copy
// Z80CpuAttachRegisterFile) measures what attaching actually recovers.
// See docs/inprogress/2026-09-24-z80-core-upgrade/register-zero-copy-tdd.md
// for the full design and the baseline numbers this was built to check.
//
// Five synthetic cases isolate the costs against an identical workload
// (a 13-byte instruction loop covering LD imm/INC/memory write/memory read/
// register move/JP, looping over a flat 64K buffer - no contention, no host
// breakpoint/TTD/analyzer overhead, so only the CPU-loop architecture differs):
//
//   1. FlatBus            - Z80CpuAttachMemory: the library's zero-overhead
//                            compiled specialization (inline array access,
//                            no callbacks). The ceiling.
//   2. CallbackBus         - Z80CpuSetMemoryBus with trampolines that do the
//                            same array access through a function pointer,
//                            mirroring the *shape* of Z80::rd/wd's role in the
//                            real wrapper (minus contention/tracing, which
//                            would cost the same either way). FlatBus vs
//                            CallbackBus isolates the trampoline/indirect-
//                            call cost alone.
//   3. CallbackBusWithSync - CallbackBus plus Z80CpuGetRegisters/
//                            SetRegisters around every Z80CpuStep - how
//                            core/src/emulator/cpu/z80.cpp worked before the
//                            zero-copy integration. CallbackBus vs
//                            CallbackBusWithSync isolates the register-copy
//                            cost alone.
//   4. RegisterSyncOnly    - Get/SetRegisters in a loop with no Step at all:
//                            the pure copy cost per "instruction slot", as a
//                            cross-check on case 3's delta.
//   5. CallbackBusAttached - CallbackBus plus Z80CpuAttachRegisterFile
//                            (called once, outside the loop) instead of a
//                            per-step copy - how core/src/emulator/cpu/z80.cpp
//                            works today. CallbackBus vs CallbackBusAttached
//                            isolates the residual cost of the attached
//                            indirection itself (one dependent load per
//                            instruction plus a reload after each trampoline
//                            call, per the library's own measurements); it
//                            should sit much closer to CallbackBus than to
//                            CallbackBusWithSync.
//
// A sixth benchmark (RealFrameCost) runs the actual integrated Z80::Step()
// through a live, booted emulator instance in turbo mode (same harness shape
// as turbo_frame_benchmark.cpp), giving the real-world number the synthetic
// cases are decomposing - it also includes ROM-paging/breakpoint/analyzer/
// TTD-hook checks that Z80Step performs every instruction, which the
// synthetic cases deliberately exclude.
//
// Run with: ./core-benchmarks --benchmark_filter="Z80Engine.*|Z80RealFrame.*"

#include <benchmark/benchmark.h>

#include <array>
#include <cstdint>
#include <cstring>

#include "emulator/cpu/core.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/emulatormanager.h"
#include "emulator/mainloop.h"

extern "C"
{
#include "3rdparty/unreal-z80/include/z80cpu.h"
}

namespace
{
// LD A,0 ; INC A ; LD (4000h),A ; LD A,(4000h) ; LD B,A ; JP 0000h
// 13 bytes, no HALT, loops forever - exercises immediate load, ALU, memory
// write, memory read, register-register move and control flow in one pass.
constexpr uint8_t kWorkload[] = {0x3E, 0x00, 0x3C, 0x32, 0x00, 0x40,
                                  0x3A, 0x00, 0x40, 0x47, 0xC3, 0x00, 0x00};

std::array<uint8_t, 0x10000>& WorkloadMemory()
{
    static std::array<uint8_t, 0x10000> memory{};
    static bool initialized = false;
    if (!initialized)
    {
        std::memcpy(memory.data(), kWorkload, sizeof(kWorkload));
        initialized = true;
    }
    return memory;
}

uint8_t TrampolineRead(Z80CPU*, uint16_t addr, int, void* userData)
{
    return static_cast<uint8_t*>(userData)[addr];
}

void TrampolineWrite(Z80CPU*, uint16_t addr, uint8_t value, void* userData)
{
    static_cast<uint8_t*>(userData)[addr] = value;
}

}  // namespace

/// Ceiling: flat 64K bus, inline array access, no callback indirection at all.
static void BM_Z80EngineFlatBus(benchmark::State& state)
{
    Z80CPU* cpu = Z80CpuCreate();
    Z80CpuReset(cpu);
    Z80CpuAttachMemory(cpu, WorkloadMemory().data());

    for (auto _ : state)
    {
        Z80CpuStep(cpu);
    }
    state.SetItemsProcessed(state.iterations());

    Z80CpuDestroy(cpu);
}
BENCHMARK(BM_Z80EngineFlatBus);

/// Callback bus doing the identical array access through a function pointer -
/// isolates the trampoline/indirect-call cost vs the flat bus above.
static void BM_Z80EngineCallbackBus(benchmark::State& state)
{
    Z80CPU* cpu = Z80CpuCreate();
    Z80CpuReset(cpu);
    Z80CpuSetMemoryBus(cpu, TrampolineRead, WorkloadMemory().data(), TrampolineWrite,
                       WorkloadMemory().data());

    for (auto _ : state)
    {
        Z80CpuStep(cpu);
    }
    state.SetItemsProcessed(state.iterations());

    Z80CpuDestroy(cpu);
}
BENCHMARK(BM_Z80EngineCallbackBus);

/// Callback bus PLUS the full register-file bulk copy around every step, as
/// core/src/emulator/cpu/z80.cpp does today - isolates the register-copy cost
/// vs BM_Z80EngineCallbackBus above.
static void BM_Z80EngineCallbackBusWithRegisterSync(benchmark::State& state)
{
    Z80CPU* cpu = Z80CpuCreate();
    Z80CpuReset(cpu);
    Z80CpuSetMemoryBus(cpu, TrampolineRead, WorkloadMemory().data(), TrampolineWrite,
                       WorkloadMemory().data());

    Z80CpuRegisters regs;
    Z80CpuGetRegisters(cpu, &regs);

    for (auto _ : state)
    {
        Z80CpuSetRegisters(cpu, &regs);
        Z80CpuStep(cpu);
        Z80CpuGetRegisters(cpu, &regs);
    }
    state.SetItemsProcessed(state.iterations());

    Z80CpuDestroy(cpu);
}
BENCHMARK(BM_Z80EngineCallbackBusWithRegisterSync);

/// Pure register-file copy cost, no Step at all - a cross-check on the delta
/// between the two benchmarks above.
static void BM_Z80EngineRegisterSyncOnly(benchmark::State& state)
{
    Z80CPU* cpu = Z80CpuCreate();
    Z80CpuReset(cpu);

    Z80CpuRegisters regs;
    Z80CpuGetRegisters(cpu, &regs);

    for (auto _ : state)
    {
        Z80CpuSetRegisters(cpu, &regs);
        Z80CpuGetRegisters(cpu, &regs);
    }
    state.SetItemsProcessed(state.iterations());

    Z80CpuDestroy(cpu);
}
BENCHMARK(BM_Z80EngineRegisterSyncOnly);

/// Callback bus with a zero-copy attached register file (Z80CpuAttachRegisterFile,
/// called once outside the loop) instead of a per-step copy - how
/// core/src/emulator/cpu/z80.cpp integrates today. Isolates the residual cost
/// of the attached indirection vs BM_Z80EngineCallbackBus above.
static void BM_Z80EngineCallbackBusAttached(benchmark::State& state)
{
    Z80CPU* cpu = Z80CpuCreate();
    Z80CpuReset(cpu);
    Z80CpuSetMemoryBus(cpu, TrampolineRead, WorkloadMemory().data(), TrampolineWrite,
                       WorkloadMemory().data());

    Z80CpuRegisterFile regs{};
    Z80CpuAttachRegisterFile(cpu, &regs);

    for (auto _ : state)
    {
        Z80CpuStep(cpu);
    }
    state.SetItemsProcessed(state.iterations());

    Z80CpuAttachRegisterFile(cpu, nullptr);
    Z80CpuDestroy(cpu);
}
BENCHMARK(BM_Z80EngineCallbackBusAttached);

/// Real-world number: the actual integrated Z80::Step(), through a live,
/// booted 48K emulator in turbo mode (same harness shape as
/// turbo_frame_benchmark.cpp). Includes ROM-paging/breakpoint/analyzer/TTD
/// checks that Z80Step performs every instruction - not present in cases 1-4.
static void BM_Z80RealFrameCost(benchmark::State& state)
{
    EmulatorManager* manager = EmulatorManager::GetInstance();
    std::shared_ptr<Emulator> emulator =
        manager->CreateEmulatorWithModel("bench-z80-engine-frame", "48K", LoggerLevel::LogNone);
    if (!emulator)
    {
        state.SkipWithError("emulator creation failed");
        return;
    }

    EmulatorContext* context = emulator->GetContext();
    MainLoopCUT* mainLoop = context ? reinterpret_cast<MainLoopCUT*>(context->pMainLoop) : nullptr;
    Core* core = context ? context->pCore : nullptr;
    if (!mainLoop || !core)
    {
        state.SkipWithError("main loop or core unavailable");
        manager->RemoveEmulator(emulator->GetUUID());
        return;
    }

    for (int i = 0; i < 10; i++)
        mainLoop->RunFramePublic();

    core->EnableTurboMode();

    for (auto _ : state)
    {
        mainLoop->RunFramePublic();
    }

    manager->RemoveEmulator(emulator->GetUUID());
}
BENCHMARK(BM_Z80RealFrameCost);
