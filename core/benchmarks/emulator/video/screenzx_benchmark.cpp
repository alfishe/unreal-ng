#include <benchmark/benchmark.h>
#include "stdafx.h"

#include "screenzx_benchmark.h"

#include "emulator/cpu/core.h"
#include "emulator/emulatorcontext.h"
#include "emulator/video/zx/screenzx.h"
#include "emulator/cpu/z80.h"

static void BM_CalculateXYAddress(benchmark::State& state)
{
    /// region <SetUp>
    // Instantiate emulator with all peripherals, but no configuration loaded
    EmulatorContext* context = new EmulatorContext();
    Core* cpu = new Core(context);
    bool initResult = cpu->Init();
    ScreenZX* screenzx = new ScreenZX(context);
    /// endregion </Setup>

    for (auto _ : state)
    {
        for (uint16_t x = 0; x <= 255; x++)
        {
            for (uint8_t y = 0; y < 192; y++)
            {
                screenzx->CalculateXYScreenAddress(x, y, 0x4000);
            }
        }
    }

    /// region <TearDown>
    if (screenzx != nullptr)
    {
        delete screenzx;
    }

    if (cpu != nullptr)
    {
        delete cpu;
    }

    if (context != nullptr)
    {
        delete context;
    }
    /// endregion </Teardown>
}

BENCHMARK(BM_CalculateXYAddress)->Iterations(100);

static void BM_CalculateXYAddressOptimized(benchmark::State& state)
{
    /// region <SetUp>
    // Instantiate emulator with all peripherals, but no configuration loaded
    EmulatorContext* context = new EmulatorContext(LoggerLevel::LogError); // Filter out all messages with level below error
    Core* cpu = new Core(context);
    bool initResult = cpu->Init();
    ScreenZX* screenzx = new ScreenZX(context);
    /// endregion </Setup>

    for (auto _ : state)
    {
        for (uint16_t x = 0; x <= 255; x++)
        {
            for (uint8_t y = 0; y < 192; y++)
            {
                screenzx->CalculateXYScreenAddressOptimized(x, y, 0x4000);
            }
        }
    }

    /// region <TearDown>
    if (screenzx != nullptr)
    {
        delete screenzx;
    }

    if (cpu != nullptr)
    {
        delete cpu;
    }

    if (context != nullptr)
    {
        delete context;
    }
    /// endregion </Teardown>
}

BENCHMARK(BM_CalculateXYAddressOptimized)->Iterations(100);

static void BM_CalculateXYColorAttrAddress(benchmark::State& state)
{
    /// region <SetUp>
    // Instantiate emulator with all peripherals, but no configuration loaded
    EmulatorContext* context = new EmulatorContext(LoggerLevel::LogError); // Filter out all messages with level below error
    Core* cpu = new Core(context);
    bool initResult = cpu->Init();
    ScreenZX* screenzx = new ScreenZX(context);
    /// endregion </Setup>

    for (auto _ : state)
    {
        for (uint16_t x = 0; x <= 255; x++)
        {
            for (uint8_t y = 0; y < 192; y++)
            {
                screenzx->CalculateXYColorAttrAddress(x, y, 0x4000);
            }
        }
    }

    /// region <TearDown>
    if (screenzx != nullptr)
    {
        delete screenzx;
    }

    if (cpu != nullptr)
    {
        delete cpu;
    }

    if (context != nullptr)
    {
        delete context;
    }
    /// endregion </Teardown>
}

BENCHMARK(BM_CalculateXYColorAttrAddress)->Iterations(100);


static void BM_CalculateXYColorAttrAddressOptimized(benchmark::State& state)
{
    /// region <SetUp>
    // Instantiate emulator with all peripherals, but no configuration loaded
    EmulatorContext* context = new EmulatorContext(LoggerLevel::LogError); // Filter out all messages with level below error
    Core* cpu = new Core(context);
    bool initResult = cpu->Init();
    ScreenZX* screenzx = new ScreenZX(context);
    /// endregion </Setup>

    for (auto _ : state)
    {
        for (uint16_t x = 0; x <= 255; x++)
        {
            for (uint8_t y = 0; y < 192; y++)
            {
                screenzx->CalculateXYColorAttrAddressOptimized(x, y, 0x4000);
            }
        }
    }

    /// region <TearDown>
    if (screenzx != nullptr)
    {
        delete screenzx;
    }

    if (cpu != nullptr)
    {
        delete cpu;
    }

    if (context != nullptr)
    {
        delete context;
    }
    /// endregion </Teardown>
}

BENCHMARK(BM_CalculateXYColorAttrAddressOptimized)->Iterations(100);

static void BM_RenderOnlyMainScreen(benchmark::State& state)
{
    /// region <SetUp>
    // Instantiate emulator with all peripherals, but no configuration loaded
    EmulatorContext* context = new EmulatorContext(LoggerLevel::LogError); // Filter out all messages with level below error
    Core* cpu = new Core(context);
    bool initResult = cpu->Init();
    // Use Spectrum48K / Pentagon memory layout
    cpu->GetMemory()->DefaultBanksFor48k();

    ScreenZX* screenzx = new ScreenZX(context);
    screenzx->InitFrame();

    /// endregion </Setup>

    for (auto _ : state)
    {
        screenzx->RenderOnlyMainScreen();
    }

    /// region <TearDown>
    if (screenzx != nullptr)
    {
        delete screenzx;
    }

    if (cpu != nullptr)
    {
        delete cpu;
    }

    if (context != nullptr)
    {
        delete context;
    }
    /// endregion </Teardown>
}
BENCHMARK(BM_RenderOnlyMainScreen)->Iterations(100);

// Multi-threaded benchmark - each thread gets its own emulator instance
static void BM_RenderOnlyMainScreen_MT(benchmark::State& state)
{
    // Thread-local setup - each thread creates its own instance
    // This avoids conflicts with shared memory and global resources
    EmulatorContext* context = nullptr;
    Core* cpu = nullptr;
    ScreenZX* screenzx = nullptr;

    // Setup phase - pause timing for setup
    state.PauseTiming();
    context = new EmulatorContext(LoggerLevel::LogError);
    cpu = new Core(context);
    bool initResult = cpu->Init();
    cpu->GetMemory()->DefaultBanksFor48k();
    screenzx = new ScreenZX(context);
    screenzx->InitFrame();
    state.ResumeTiming();

    // Benchmark loop
    for (auto _ : state)
    {
        screenzx->RenderOnlyMainScreen();
    }

    // Teardown phase
    state.PauseTiming();
    if (screenzx != nullptr)
    {
        delete screenzx;
    }
    if (cpu != nullptr)
    {
        delete cpu;
    }
    if (context != nullptr)
    {
        delete context;
    }
    state.ResumeTiming();
}
BENCHMARK(BM_RenderOnlyMainScreen_MT)->Iterations(100)->Threads(12)->UseRealTime();