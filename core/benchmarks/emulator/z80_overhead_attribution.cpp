/// @file z80_overhead_attribution.cpp
/// @brief Micro-benchmarks to attribute Z80Step overhead by component
///
/// Isolates costs of:
/// - Pointer-to-member function calls (MemIf->MemoryRead)
/// - std::function checks (hooks)
/// - Multiple pointer dereferences
/// - ULA contention checks
/// - TR-DOS/debug mode checks

#include <benchmark/benchmark.h>
#include <functional>

#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/emulatormanager.h"
#include "emulator/memory/memory.h"
#include "emulator/video/ulacontention.h"

/// Baseline: direct array access (what optimal memory read looks like)
static void BM_Overhead_DirectArrayAccess(benchmark::State& state)
{
    alignas(64) uint8_t memory[65536];
    for (int i = 0; i < 65536; i++) memory[i] = i & 0xFF;

    uint64_t sum = 0;
    uint16_t addr = 0;

    for (auto _ : state)
    {
        for (int i = 0; i < 7000; i++)
        {
            sum += memory[addr++];
        }
    }

    benchmark::DoNotOptimize(sum);
    state.SetItemsProcessed(state.iterations() * 7000);
    state.SetLabel("direct array[addr]");
}

/// Cost of pointer-to-member function call
static void BM_Overhead_PointerToMember(benchmark::State& state)
{
    struct Obj {
        uint8_t data[65536];
        uint8_t read(uint16_t addr) { return data[addr]; }
    };

    using ReadFunc = uint8_t (Obj::*)(uint16_t);

    struct Interface {
        ReadFunc reader;
    };

    Obj obj;
    for (int i = 0; i < 65536; i++) obj.data[i] = i & 0xFF;

    Interface iface{&Obj::read};
    Interface* ifacePtr = &iface;

    uint64_t sum = 0;
    uint16_t addr = 0;

    for (auto _ : state)
    {
        for (int i = 0; i < 7000; i++)
        {
            sum += (obj.*ifacePtr->reader)(addr++);  // Same pattern as rd()
        }
    }

    benchmark::DoNotOptimize(sum);
    state.SetItemsProcessed(state.iterations() * 7000);
    state.SetLabel("(obj.*ptr->method)(addr)");
}

/// Cost of virtual function call
static void BM_Overhead_VirtualCall(benchmark::State& state)
{
    struct Base {
        virtual uint8_t read(uint16_t addr) = 0;
        virtual ~Base() = default;
    };

    struct Derived : Base {
        uint8_t data[65536];
        uint8_t read(uint16_t addr) override { return data[addr]; }
    };

    Derived obj;
    for (int i = 0; i < 65536; i++) obj.data[i] = i & 0xFF;

    Base* basePtr = &obj;

    uint64_t sum = 0;
    uint16_t addr = 0;

    for (auto _ : state)
    {
        for (int i = 0; i < 7000; i++)
        {
            sum += basePtr->read(addr++);
        }
    }

    benchmark::DoNotOptimize(sum);
    state.SetItemsProcessed(state.iterations() * 7000);
    state.SetLabel("virtual call");
}

/// Cost of std::function check (even when empty)
static void BM_Overhead_StdFunctionCheck(benchmark::State& state)
{
    std::function<void(uint16_t)> hook;  // Empty

    uint64_t count = 0;
    uint16_t addr = 0;

    for (auto _ : state)
    {
        for (int i = 0; i < 7000; i++)
        {
            if (hook)
                hook(addr);
            addr++;
            count++;
        }
    }

    benchmark::DoNotOptimize(count);
    state.SetItemsProcessed(state.iterations() * 7000);
    state.SetLabel("if(std::function) - empty");
}

/// Cost of std::function check with function set
static void BM_Overhead_StdFunctionCall(benchmark::State& state)
{
    uint64_t called = 0;
    std::function<void(uint16_t)> hook = [&called](uint16_t) { called++; };

    uint16_t addr = 0;

    for (auto _ : state)
    {
        for (int i = 0; i < 7000; i++)
        {
            if (hook)
                hook(addr);
            addr++;
        }
    }

    benchmark::DoNotOptimize(called);
    state.SetItemsProcessed(state.iterations() * 7000);
    state.SetLabel("if(std::function) + call");
}

/// Cost of multiple pointer dereferences (context->member->method pattern)
static void BM_Overhead_PointerChain(benchmark::State& state)
{
    struct Inner {
        uint8_t data[65536];
        uint8_t read(uint16_t addr) { return data[addr]; }
    };

    struct Context {
        Inner* pInner;
    };

    Inner inner;
    for (int i = 0; i < 65536; i++) inner.data[i] = i & 0xFF;

    Context ctx{&inner};
    Context* _context = &ctx;

    uint64_t sum = 0;
    uint16_t addr = 0;

    for (auto _ : state)
    {
        for (int i = 0; i < 7000; i++)
        {
            sum += _context->pInner->read(addr++);
        }
    }

    benchmark::DoNotOptimize(sum);
    state.SetItemsProcessed(state.iterations() * 7000);
    state.SetLabel("ctx->ptr->method()");
}

/// Cost of bool check + branch (simulates isDebugMode, ttdCoverageActive, etc.)
static void BM_Overhead_BoolChecks(benchmark::State& state)
{
    bool flag1 = false;
    bool flag2 = false;
    bool flag3 = false;

    uint64_t count = 0;
    uint16_t addr = 0;

    for (auto _ : state)
    {
        for (int i = 0; i < 7000; i++)
        {
            if (flag1) count++;
            if (flag2) count++;
            if (flag3) count++;
            addr++;
        }
    }

    benchmark::DoNotOptimize(count);
    benchmark::DoNotOptimize(addr);
    state.SetItemsProcessed(state.iterations() * 7000);
    state.SetLabel("3x if(bool) - all false");
}

/// Real Memory::rd() path through the emulator
static void BM_Overhead_RealMemoryRead(benchmark::State& state)
{
    EmulatorManager* manager = EmulatorManager::GetInstance();
    auto emulator = manager->CreateEmulatorWithModel("overhead-mem", "48K", LoggerLevel::LogNone);
    if (!emulator)
    {
        state.SkipWithError("emulator creation failed");
        return;
    }

    EmulatorContext* ctx = emulator->GetContext();
    Z80* cpu = ctx && ctx->pCore ? ctx->pCore->GetZ80() : nullptr;

    if (!cpu)
    {
        state.SkipWithError("cpu unavailable");
        manager->RemoveEmulator(emulator->GetUUID());
        return;
    }

    uint64_t sum = 0;
    uint16_t addr = 0x8000;

    for (auto _ : state)
    {
        for (int i = 0; i < 7000; i++)
        {
            sum += cpu->rd(addr++);
            if (addr >= 0xC000) addr = 0x8000;
        }
    }

    benchmark::DoNotOptimize(sum);
    state.SetItemsProcessed(state.iterations() * 7000);
    state.SetLabel("real Z80::rd()");

    manager->RemoveEmulator(emulator->GetUUID());
}

/// Real m1_cycle() through the emulator
static void BM_Overhead_RealM1Cycle(benchmark::State& state)
{
    EmulatorManager* manager = EmulatorManager::GetInstance();
    auto emulator = manager->CreateEmulatorWithModel("overhead-m1", "48K", LoggerLevel::LogNone);
    if (!emulator)
    {
        state.SkipWithError("emulator creation failed");
        return;
    }

    EmulatorContext* ctx = emulator->GetContext();
    Memory* mem = ctx ? ctx->pMemory : nullptr;
    Z80* cpu = ctx && ctx->pCore ? ctx->pCore->GetZ80() : nullptr;

    if (!cpu || !mem)
    {
        state.SkipWithError("components unavailable");
        manager->RemoveEmulator(emulator->GetUUID());
        return;
    }

    // Fill with NOPs
    for (int i = 0; i < 0x4000; i++)
        mem->DirectWriteToZ80Memory(0x8000 + i, 0x00);

    cpu->pc = 0x8000;

    for (auto _ : state)
    {
        cpu->pc = 0x8000;
        for (int i = 0; i < 7000; i++)
        {
            cpu->m1_cycle();
        }
    }

    state.SetItemsProcessed(state.iterations() * 7000);
    state.SetLabel("real Z80::m1_cycle()");

    manager->RemoveEmulator(emulator->GetUUID());
}

/// Measure ULA contention check cost
static void BM_Overhead_ULAContention(benchmark::State& state)
{
    EmulatorManager* manager = EmulatorManager::GetInstance();
    auto emulator = manager->CreateEmulatorWithModel("overhead-ula", "48K", LoggerLevel::LogNone);
    if (!emulator)
    {
        state.SkipWithError("emulator creation failed");
        return;
    }

    EmulatorContext* ctx = emulator->GetContext();
    UlaContention* ula = ctx ? ctx->pUlaContention : nullptr;

    if (!ula)
    {
        state.SkipWithError("ULA unavailable");
        manager->RemoveEmulator(emulator->GetUUID());
        return;
    }

    uint64_t delays = 0;
    uint16_t addr = 0x4000;  // Contended region

    for (auto _ : state)
    {
        for (int i = 0; i < 7000; i++)
        {
            if (ula->IsAddressContended(addr))
            {
                delays += ula->GetContentionDelay();
            }
            addr++;
            if (addr >= 0x8000) addr = 0x4000;
        }
    }

    benchmark::DoNotOptimize(delays);
    state.SetItemsProcessed(state.iterations() * 7000);
    state.SetLabel("ULA contention check");

    manager->RemoveEmulator(emulator->GetUUID());
}

/// Compare: direct memory vs full Z80Step
static void BM_Overhead_FullComparison(benchmark::State& state)
{
    EmulatorManager* manager = EmulatorManager::GetInstance();
    auto emulator = manager->CreateEmulatorWithModel("overhead-full", "48K", LoggerLevel::LogNone);
    if (!emulator)
    {
        state.SkipWithError("emulator creation failed");
        return;
    }

    EmulatorContext* ctx = emulator->GetContext();
    Memory* mem = ctx ? ctx->pMemory : nullptr;
    Z80* cpu = ctx && ctx->pCore ? ctx->pCore->GetZ80() : nullptr;

    if (!cpu || !mem)
    {
        state.SkipWithError("components unavailable");
        manager->RemoveEmulator(emulator->GetUUID());
        return;
    }

    int mode = state.range(0);

    // Fill RAM with NOPs
    for (int i = 0; i < 0x4000; i++)
        mem->DirectWriteToZ80Memory(0x8000 + i, 0x00);

    if (mode == 0)
    {
        // Mode 0: Direct memory read
        uint64_t sum = 0;
        uint16_t addr = 0x8000;

        for (auto _ : state)
        {
            for (int i = 0; i < 7000; i++)
            {
                sum += mem->DirectReadFromZ80Memory(addr++);
                if (addr >= 0xC000) addr = 0x8000;
            }
        }
        benchmark::DoNotOptimize(sum);
        state.SetLabel("DirectRead (baseline)");
    }
    else if (mode == 1)
    {
        // Mode 1: Z80::rd() only
        uint64_t sum = 0;
        uint16_t addr = 0x8000;

        for (auto _ : state)
        {
            for (int i = 0; i < 7000; i++)
            {
                sum += cpu->rd(addr++);
                if (addr >= 0xC000) addr = 0x8000;
            }
        }
        benchmark::DoNotOptimize(sum);
        state.SetLabel("Z80::rd() only");
    }
    else
    {
        // Mode 2: Full Z80Step (NOPs)
        cpu->pc = 0x8000;

        for (auto _ : state)
        {
            cpu->pc = 0x8000;
            for (int i = 0; i < 7000; i++)
            {
                cpu->Z80Step();
            }
        }
        state.SetLabel("full Z80Step (NOP)");
    }

    state.SetItemsProcessed(state.iterations() * 7000);
    manager->RemoveEmulator(emulator->GetUUID());
}

BENCHMARK(BM_Overhead_DirectArrayAccess)->Iterations(10000)->Unit(benchmark::kNanosecond);
BENCHMARK(BM_Overhead_PointerToMember)->Iterations(10000)->Unit(benchmark::kNanosecond);
BENCHMARK(BM_Overhead_VirtualCall)->Iterations(10000)->Unit(benchmark::kNanosecond);
BENCHMARK(BM_Overhead_StdFunctionCheck)->Iterations(10000)->Unit(benchmark::kNanosecond);
BENCHMARK(BM_Overhead_StdFunctionCall)->Iterations(10000)->Unit(benchmark::kNanosecond);
BENCHMARK(BM_Overhead_PointerChain)->Iterations(10000)->Unit(benchmark::kNanosecond);
BENCHMARK(BM_Overhead_BoolChecks)->Iterations(10000)->Unit(benchmark::kNanosecond);
BENCHMARK(BM_Overhead_RealMemoryRead)->Iterations(5000)->Unit(benchmark::kNanosecond);
BENCHMARK(BM_Overhead_RealM1Cycle)->Iterations(5000)->Unit(benchmark::kNanosecond);
BENCHMARK(BM_Overhead_ULAContention)->Iterations(5000)->Unit(benchmark::kNanosecond);
BENCHMARK(BM_Overhead_FullComparison)->Arg(0)->Arg(1)->Arg(2)->Iterations(5000)->Unit(benchmark::kNanosecond);
