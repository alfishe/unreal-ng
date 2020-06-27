#include <benchmark/benchmark.h>
#include "stdafx.h"

#include "screenzx_benchmark.h"

#include "emulator/cpu/cpu.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulatorcontext.h"
#include "emulator/video/zx/screenzx.h"

static void BM_CalculateXYAddress(benchmark::State& state)
{
    // Instantiate emulator with all peripherals, but no configuration loaded
    EmulatorContext* context = new EmulatorContext();
    CPU* cpu = new CPU(context);
    ScreenZXCUT* screenzx = new ScreenZXCUT(context);

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
}

BENCHMARK(BM_CalculateXYAddress)->Iterations(100);
