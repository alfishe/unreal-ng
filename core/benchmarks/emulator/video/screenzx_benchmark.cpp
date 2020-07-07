#include <benchmark/benchmark.h>
#include "stdafx.h"

#include "screenzx_benchmark.h"

#include "emulator/cpu/cpu.h"
#include "emulator/emulatorcontext.h"
#include "emulator/video/zx/screenzx.h"

static void BM_CalculateXYAddress(benchmark::State& state)
{
    /// region <SetUp>
    // Instantiate emulator with all peripherals, but no configuration loaded
    EmulatorContext* context = new EmulatorContext();
    CPU* cpu = new CPU(context);
    ScreenZXCUT* screenzx = new ScreenZXCUT(context);
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
    EmulatorContext* context = new EmulatorContext();
    CPU* cpu = new CPU(context);
    ScreenZXCUT* screenzx = new ScreenZXCUT(context);
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
    EmulatorContext* context = new EmulatorContext();
    CPU* cpu = new CPU(context);
    ScreenZXCUT* screenzx = new ScreenZXCUT(context);
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
    EmulatorContext* context = new EmulatorContext();
    CPU* cpu = new CPU(context);
    ScreenZXCUT* screenzx = new ScreenZXCUT(context);
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
    EmulatorContext* context = new EmulatorContext();

    CPU* cpu = new CPU(context);
    // Use Spectrum48K / Pentagon memory layout
    cpu->GetMemory()->InternalSetBanks();

    ScreenZXCUT* screenzx = new ScreenZXCUT(context);
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
BENCHMARK(BM_RenderOnlyMainScreen)->Iterations(100)->Threads(12);