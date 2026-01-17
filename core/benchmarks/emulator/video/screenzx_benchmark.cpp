#include "screenzx_benchmark.h"

#include <benchmark/benchmark.h>

#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulatorcontext.h"
#include "emulator/video/zx/screenzx.h"
#include "stdafx.h"

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
    EmulatorContext* context =
        new EmulatorContext(LoggerLevel::LogError);  // Filter out all messages with level below error
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
    EmulatorContext* context =
        new EmulatorContext(LoggerLevel::LogError);  // Filter out all messages with level below error
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
    EmulatorContext* context =
        new EmulatorContext(LoggerLevel::LogError);  // Filter out all messages with level below error
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
    EmulatorContext* context =
        new EmulatorContext(LoggerLevel::LogError);  // Filter out all messages with level below error
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

/// @brief Benchmark for original Draw implementation (with runtime division/modulo)
/// @deprecated CLEANUP: Remove after optimization phases verified
/// Draws entire frame using DrawOriginal method
static void BM_DrawFrame_Original(benchmark::State& state)
{
    /// region <SetUp>
    EmulatorContext* context = new EmulatorContext(LoggerLevel::LogError);
    Core* cpu = new Core(context);
    bool initResult = cpu->Init();
    cpu->GetMemory()->DefaultBanksFor48k();

    ScreenZXCUT* screenzx = new ScreenZXCUT(context);
    screenzx->InitFrame();

    const uint32_t maxTstates = screenzx->_rasterState.maxFrameTiming;
    /// endregion </Setup>

    for (auto _ : state)
    {
        // Draw entire frame using original method
        for (uint32_t t = 0; t < maxTstates; t++)
        {
            screenzx->DrawOriginal(t);
        }
    }

    /// region <TearDown>
    delete screenzx;
    delete cpu;
    delete context;
    /// endregion </Teardown>
}
BENCHMARK(BM_DrawFrame_Original)->Iterations(100);

/// @brief Benchmark for LUT + branch-free Draw implementation (Phase 3)
/// Draws entire frame using Draw method with pre-computed coordinates and branch-free color selection
static void BM_DrawFrame_LUT(benchmark::State& state)
{
    /// region <SetUp>
    EmulatorContext* context = new EmulatorContext(LoggerLevel::LogError);
    Core* cpu = new Core(context);
    bool initResult = cpu->Init();
    cpu->GetMemory()->DefaultBanksFor48k();

    ScreenZXCUT* screenzx = new ScreenZXCUT(context);
    screenzx->InitFrame();

    const uint32_t maxTstates = screenzx->_rasterState.maxFrameTiming;
    /// endregion </Setup>

    for (auto _ : state)
    {
        // Draw entire frame using LUT-optimized method
        for (uint32_t t = 0; t < maxTstates; t++)
        {
            screenzx->Draw(t);
        }
    }

    /// region <TearDown>
    delete screenzx;
    delete cpu;
    delete context;
    /// endregion </Teardown>
}
BENCHMARK(BM_DrawFrame_LUT)->Iterations(100);

/// @brief Benchmark for LUT + ternary color selection (Phase 2 baseline)
/// @deprecated CLEANUP: Remove after Phase 3 verification complete
/// Draws entire frame using DrawLUT_Ternary method for comparison with branch-free version
static void BM_DrawFrame_LUT_Ternary(benchmark::State& state)
{
    /// region <SetUp>
    EmulatorContext* context = new EmulatorContext(LoggerLevel::LogError);
    Core* cpu = new Core(context);
    bool initResult = cpu->Init();
    cpu->GetMemory()->DefaultBanksFor48k();

    ScreenZXCUT* screenzx = new ScreenZXCUT(context);
    screenzx->InitFrame();

    const uint32_t maxTstates = screenzx->_rasterState.maxFrameTiming;
    /// endregion </Setup>

    for (auto _ : state)
    {
        // Draw entire frame using LUT + ternary (Phase 2 baseline)
        for (uint32_t t = 0; t < maxTstates; t++)
        {
            screenzx->DrawLUT_Ternary(t);
        }
    }

    /// region <TearDown>
    delete screenzx;
    delete cpu;
    delete context;
    /// endregion </Teardown>
}
BENCHMARK(BM_DrawFrame_LUT_Ternary)->Iterations(100);

/// region <Phase 4-5: Batch 8-Pixel Benchmarks - ScreenHQ=OFF only>

/// @brief Benchmark for batch 8-pixel scalar rendering (Phase 4)
/// Renders entire screen using DrawBatch8_Scalar - ScreenHQ=OFF only
static void BM_RenderScreen_Batch8_Scalar(benchmark::State& state)
{
    /// region <SetUp>
    EmulatorContext* context = new EmulatorContext(LoggerLevel::LogError);
    Core* cpu = new Core(context);
    bool initResult = cpu->Init();
    cpu->GetMemory()->DefaultBanksFor48k();

    ScreenZXCUT* screenzx = new ScreenZXCUT(context);
    screenzx->InitFrame();
    /// endregion </Setup>

    for (auto _ : state)
    {
        // Render screen using scalar batch 8-pixel method
        const RasterDescriptor& rd = screenzx->rasterDescriptors[screenzx->_mode];
        uint32_t* fb = reinterpret_cast<uint32_t*>(screenzx->_framebuffer.memoryBuffer);
        const uint32_t startX = rd.screenOffsetLeft;
        const uint32_t startY = rd.screenOffsetTop;

        for (uint8_t y = 0; y < 192; y++)
        {
            uint32_t* linePtr = fb + (startY + y) * rd.fullFrameWidth + startX;
            for (uint8_t symbolX = 0; symbolX < 32; symbolX++)
            {
                screenzx->DrawBatch8_Scalar(y, symbolX, linePtr + symbolX * 8);
            }
        }
    }

    /// region <TearDown>
    delete screenzx;
    delete cpu;
    delete context;
    /// endregion </Teardown>
}
BENCHMARK(BM_RenderScreen_Batch8_Scalar)->Iterations(100);

#ifdef __ARM_NEON
/// @brief Benchmark for batch 8-pixel NEON rendering (Phase 5)
/// Renders entire screen using DrawBatch8_NEON - ScreenHQ=OFF only
static void BM_RenderScreen_Batch8_NEON(benchmark::State& state)
{
    /// region <SetUp>
    EmulatorContext* context = new EmulatorContext(LoggerLevel::LogError);
    Core* cpu = new Core(context);
    bool initResult = cpu->Init();
    cpu->GetMemory()->DefaultBanksFor48k();

    ScreenZXCUT* screenzx = new ScreenZXCUT(context);
    screenzx->InitFrame();
    /// endregion </Setup>

    for (auto _ : state)
    {
        // Render screen using NEON batch 8-pixel method
        const RasterDescriptor& rd = screenzx->rasterDescriptors[screenzx->_mode];
        uint32_t* fb = reinterpret_cast<uint32_t*>(screenzx->_framebuffer.memoryBuffer);
        const uint32_t startX = rd.screenOffsetLeft;
        const uint32_t startY = rd.screenOffsetTop;

        for (uint8_t y = 0; y < 192; y++)
        {
            uint32_t* linePtr = fb + (startY + y) * rd.fullFrameWidth + startX;
            for (uint8_t symbolX = 0; symbolX < 32; symbolX++)
            {
                screenzx->DrawBatch8_NEON(y, symbolX, linePtr + symbolX * 8);
            }
        }
    }

    /// region <TearDown>
    delete screenzx;
    delete cpu;
    delete context;
    /// endregion </Teardown>
}
BENCHMARK(BM_RenderScreen_Batch8_NEON)->Iterations(100);
#endif

/// @brief Benchmark for RenderScreen_Batch8 (auto-selects NEON or scalar)
/// This is the method that would be called when ScreenHQ=OFF
static void BM_RenderScreen_Batch8(benchmark::State& state)
{
    /// region <SetUp>
    EmulatorContext* context = new EmulatorContext(LoggerLevel::LogError);
    Core* cpu = new Core(context);
    bool initResult = cpu->Init();
    cpu->GetMemory()->DefaultBanksFor48k();

    ScreenZXCUT* screenzx = new ScreenZXCUT(context);
    screenzx->InitFrame();
    /// endregion </Setup>

    for (auto _ : state)
    {
        screenzx->RenderScreen_Batch8();
    }

    /// region <TearDown>
    delete screenzx;
    delete cpu;
    delete context;
    /// endregion </Teardown>
}
BENCHMARK(BM_RenderScreen_Batch8)->Iterations(100);

/// @brief Benchmark baseline: RenderOnlyMainScreen (existing per-pixel method)
/// For comparison with batch methods
static void BM_RenderScreen_PerPixel(benchmark::State& state)
{
    /// region <SetUp>
    EmulatorContext* context = new EmulatorContext(LoggerLevel::LogError);
    Core* cpu = new Core(context);
    bool initResult = cpu->Init();
    cpu->GetMemory()->DefaultBanksFor48k();

    ScreenZXCUT* screenzx = new ScreenZXCUT(context);
    screenzx->InitFrame();
    /// endregion </Setup>

    for (auto _ : state)
    {
        screenzx->RenderOnlyMainScreen();
    }

    /// region <TearDown>
    delete screenzx;
    delete cpu;
    delete context;
    /// endregion </Teardown>
}
BENCHMARK(BM_RenderScreen_PerPixel)->Iterations(100);

/// endregion </Phase 4-5: Batch 8-Pixel Benchmarks>

/// region <FillBorderWithColor Benchmarks>

/// @brief Benchmark for original FillBorderWithColor (pixel-by-pixel loops)
static void BM_FillBorderWithColor_Original(benchmark::State& state)
{
    /// region <SetUp>
    EmulatorContext* context = new EmulatorContext(LoggerLevel::LogError);
    Core* cpu = new Core(context);
    [[maybe_unused]] bool initResult = cpu->Init();
    cpu->GetMemory()->DefaultBanksFor48k();

    ScreenZXCUT* screenzx = new ScreenZXCUT(context);
    screenzx->InitFrame();
    /// endregion </Setup>

    for (auto _ : state)
    {
        for (uint8_t color = 0; color < 8; color++)
        {
            screenzx->FillBorderWithColor_Original(color);
        }
    }

    /// region <TearDown>
    delete screenzx;
    delete cpu;
    delete context;
    /// endregion </Teardown>
}
BENCHMARK(BM_FillBorderWithColor_Original)->Iterations(100);

/// @brief Benchmark for optimized FillBorderWithColor (row-based std::fill_n)
static void BM_FillBorderWithColor_Optimized(benchmark::State& state)
{
    /// region <SetUp>
    EmulatorContext* context = new EmulatorContext(LoggerLevel::LogError);
    Core* cpu = new Core(context);
    [[maybe_unused]] bool initResult = cpu->Init();
    cpu->GetMemory()->DefaultBanksFor48k();

    ScreenZXCUT* screenzx = new ScreenZXCUT(context);
    screenzx->InitFrame();
    /// endregion </Setup>

    for (auto _ : state)
    {
        for (uint8_t color = 0; color < 8; color++)
        {
            screenzx->FillBorderWithColor_Optimized(color);
        }
    }

    /// region <TearDown>
    delete screenzx;
    delete cpu;
    delete context;
    /// endregion </Teardown>
}
BENCHMARK(BM_FillBorderWithColor_Optimized)->Iterations(100);

/// endregion </FillBorderWithColor Benchmarks>

/// region <RenderOnlyMainScreen Comparison Benchmarks>

/// @brief Benchmark for original RenderOnlyMainScreen (per-pixel with offset calculation)
static void BM_RenderOnlyMainScreen_Original(benchmark::State& state)
{
    /// region <SetUp>
    EmulatorContext* context = new EmulatorContext(LoggerLevel::LogError);
    Core* cpu = new Core(context);
    [[maybe_unused]] bool initResult = cpu->Init();
    cpu->GetMemory()->DefaultBanksFor48k();

    ScreenZXCUT* screenzx = new ScreenZXCUT(context);
    screenzx->InitFrame();
    /// endregion </Setup>

    for (auto _ : state)
    {
        screenzx->RenderOnlyMainScreen_Original();
    }

    /// region <TearDown>
    delete screenzx;
    delete cpu;
    delete context;
    /// endregion </Teardown>
}
BENCHMARK(BM_RenderOnlyMainScreen_Original)->Iterations(100);

/// @brief Benchmark for optimized RenderOnlyMainScreen (uses RenderScreen_Batch8)
static void BM_RenderOnlyMainScreen_Optimized(benchmark::State& state)
{
    /// region <SetUp>
    EmulatorContext* context = new EmulatorContext(LoggerLevel::LogError);
    Core* cpu = new Core(context);
    [[maybe_unused]] bool initResult = cpu->Init();
    cpu->GetMemory()->DefaultBanksFor48k();

    ScreenZXCUT* screenzx = new ScreenZXCUT(context);
    screenzx->InitFrame();
    /// endregion </Setup>

    for (auto _ : state)
    {
        screenzx->RenderOnlyMainScreen_Optimized();
    }

    /// region <TearDown>
    delete screenzx;
    delete cpu;
    delete context;
    /// endregion </Teardown>
}
BENCHMARK(BM_RenderOnlyMainScreen_Optimized)->Iterations(100);

/// endregion </RenderOnlyMainScreen Comparison Benchmarks>