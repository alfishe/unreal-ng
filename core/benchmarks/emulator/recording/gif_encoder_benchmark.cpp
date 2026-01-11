#include <benchmark/benchmark.h>

#include "common/image/gifanimationhelper.h"
#include "emulator/cpu/core.h"
#include "emulator/emulatorcontext.h"
#include "emulator/video/zx/screenzx.h"
#include "stdafx.h"

/// @brief Benchmark GIF frame encoding for ZX Spectrum MainScreen (256x192)
static void BM_GIFWriteFrame_MainScreen(benchmark::State& state)
{
    // Setup
    const uint32_t width = 256;
    const uint32_t height = 192;
    const size_t bufferSize = width * height;

    // Create test framebuffer with ZX Spectrum-like content (16 colors)
    std::vector<uint32_t> framebuffer(bufferSize);
    for (size_t i = 0; i < bufferSize; i++)
    {
        // Simulate ZX Spectrum 16-color palette cycling
        uint8_t color = (i % 16);
        uint8_t r = (color & 0x02) ? ((color & 0x08) ? 0xFF : 0xCD) : 0x00;
        uint8_t g = (color & 0x04) ? ((color & 0x08) ? 0xFF : 0xCD) : 0x00;
        uint8_t b = (color & 0x01) ? ((color & 0x08) ? 0xFF : 0xCD) : 0x00;
        framebuffer[i] = (0xFF << 24) | (b << 16) | (g << 8) | r;
    }

    // Create temp file for GIF output
    std::string tempFile = "/tmp/benchmark_test.gif";

    GIFAnimationHelper gifHelper;
    gifHelper.StartAnimation(tempFile, width, height, 20);

    // Benchmark loop
    for (auto _ : state)
    {
        gifHelper.WriteFrame(framebuffer.data(), bufferSize);
    }

    gifHelper.StopAnimation();

    // Report metrics
    state.SetItemsProcessed(state.iterations());
    state.SetBytesProcessed(state.iterations() * bufferSize * 4);
}
BENCHMARK(BM_GIFWriteFrame_MainScreen)->Iterations(250)->Unit(benchmark::kMicrosecond);

/// @brief Benchmark GIF frame encoding for FullFrame (320x240)
static void BM_GIFWriteFrame_FullFrame(benchmark::State& state)
{
    // Setup
    const uint32_t width = 320;
    const uint32_t height = 240;
    const size_t bufferSize = width * height;

    // Create test framebuffer with ZX Spectrum-like content
    std::vector<uint32_t> framebuffer(bufferSize);
    for (size_t i = 0; i < bufferSize; i++)
    {
        // Simulate ZX Spectrum content with black border
        size_t x = i % width;
        size_t y = i / width;

        // Check if in main screen area (centered within 320x240)
        bool inMainScreen = (x >= 32 && x < 288 && y >= 24 && y < 216);

        if (inMainScreen)
        {
            uint8_t color = (i % 16);
            uint8_t r = (color & 0x02) ? ((color & 0x08) ? 0xFF : 0xCD) : 0x00;
            uint8_t g = (color & 0x04) ? ((color & 0x08) ? 0xFF : 0xCD) : 0x00;
            uint8_t b = (color & 0x01) ? ((color & 0x08) ? 0xFF : 0xCD) : 0x00;
            framebuffer[i] = (0xFF << 24) | (b << 16) | (g << 8) | r;
        }
        else
        {
            // Border - black (or could be any border color)
            framebuffer[i] = 0xFF000000;
        }
    }

    std::string tempFile = "/tmp/benchmark_test_full.gif";

    GIFAnimationHelper gifHelper;
    gifHelper.StartAnimation(tempFile, width, height, 20);

    // Benchmark loop
    for (auto _ : state)
    {
        gifHelper.WriteFrame(framebuffer.data(), bufferSize);
    }

    gifHelper.StopAnimation();

    // Report metrics
    state.SetItemsProcessed(state.iterations());
    state.SetBytesProcessed(state.iterations() * bufferSize * 4);
}
BENCHMARK(BM_GIFWriteFrame_FullFrame)->Iterations(250)->Unit(benchmark::kMicrosecond);

/// @brief Benchmark GIF encoding with high-change content (worst case)
static void BM_GIFWriteFrame_HighChange(benchmark::State& state)
{
    const uint32_t width = 256;
    const uint32_t height = 192;
    const size_t bufferSize = width * height;

    std::vector<uint32_t> framebuffer(bufferSize);
    std::string tempFile = "/tmp/benchmark_test_change.gif";

    GIFAnimationHelper gifHelper;
    gifHelper.StartAnimation(tempFile, width, height, 20);

    uint32_t frame = 0;

    // Benchmark loop - each frame is different
    for (auto _ : state)
    {
        // Change all pixels each frame (worst case for delta encoding)
        for (size_t i = 0; i < bufferSize; i++)
        {
            uint8_t color = ((i + frame) % 16);
            uint8_t r = (color & 0x02) ? ((color & 0x08) ? 0xFF : 0xCD) : 0x00;
            uint8_t g = (color & 0x04) ? ((color & 0x08) ? 0xFF : 0xCD) : 0x00;
            uint8_t b = (color & 0x01) ? ((color & 0x08) ? 0xFF : 0xCD) : 0x00;
            framebuffer[i] = (0xFF << 24) | (b << 16) | (g << 8) | r;
        }

        gifHelper.WriteFrame(framebuffer.data(), bufferSize);
        frame++;
    }

    gifHelper.StopAnimation();

    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_GIFWriteFrame_HighChange)->Iterations(100)->Unit(benchmark::kMicrosecond);

/// @brief Benchmark GIF encoding with menu scrolling (typical ZX Spectrum menu)
/// Simulates a highlight bar moving through menu items - realistic non-game scenario
static void BM_GIFWriteFrame_MenuScroll(benchmark::State& state)
{
    const uint32_t width = 256;
    const uint32_t height = 192;
    const size_t bufferSize = width * height;

    // ZX Spectrum color palette (simplified)
    const uint32_t COLOR_BLACK = 0xFF000000;
    const uint32_t COLOR_WHITE = 0xFFFFFFFF;
    const uint32_t COLOR_CYAN = 0xFFFFFF00;  // BGRA
    const uint32_t COLOR_BLUE = 0xFFCD0000;  // BGRA

    // Menu parameters
    const int menuStartY = 48;      // Menu starts at line 48
    const int menuItemHeight = 16;  // Each menu item is 16 pixels tall (2 char rows)
    const int menuItems = 8;        // 8 menu items

    std::vector<uint32_t> framebuffer(bufferSize);
    std::string tempFile = "/tmp/benchmark_test_menu.gif";

    GIFAnimationHelper gifHelper;
    gifHelper.StartAnimation(tempFile, width, height, 20);

    int highlightedItem = 0;

    // Benchmark loop - simulate scrolling through menu
    for (auto _ : state)
    {
        // Draw static content (background + menu text areas)
        for (size_t i = 0; i < bufferSize; i++)
        {
            size_t y = i / width;

            // Determine which menu item this pixel belongs to
            int menuItemIdx = (static_cast<int>(y) - menuStartY) / menuItemHeight;
            bool inMenuArea = (y >= menuStartY && y < menuStartY + menuItems * menuItemHeight);

            if (inMenuArea && menuItemIdx >= 0 && menuItemIdx < menuItems)
            {
                // This pixel is in menu area
                if (menuItemIdx == highlightedItem)
                {
                    // Highlighted item: inverse video (cyan background)
                    framebuffer[i] = COLOR_CYAN;
                }
                else
                {
                    // Normal item: black background
                    framebuffer[i] = COLOR_BLACK;
                }
            }
            else
            {
                // Outside menu: black background
                framebuffer[i] = COLOR_BLACK;
            }
        }

        gifHelper.WriteFrame(framebuffer.data(), bufferSize);

        // Move highlight to next item (cycling)
        highlightedItem = (highlightedItem + 1) % menuItems;
    }

    gifHelper.StopAnimation();

    state.SetItemsProcessed(state.iterations());
    state.SetBytesProcessed(state.iterations() * bufferSize * 4);
}
BENCHMARK(BM_GIFWriteFrame_MenuScroll)->Iterations(200)->Unit(benchmark::kMicrosecond);

// ================================================================================
// FIXED PALETTE BENCHMARKS (FixedZX16 mode) - for A/B comparison with Auto mode
// ================================================================================

/// Helper: Build ZX Spectrum 16-color palette (same as GIFEncoder::BuildZXSpectrum16Palette)
static void BuildZXPalette(GifPalette* pal)
{
    pal->bitDepth = 4;

    const uint8_t normal = 0xCD;
    const uint8_t bright = 0xFF;

    pal->r[0] = pal->g[0] = pal->b[0] = 0;

    for (int i = 1; i < 16; i++)
    {
        bool isBright = (i >= 8);
        uint8_t intensity = isBright ? bright : normal;
        int colorBits = i % 8;

        if (colorBits == 0)
        {
            pal->r[i] = pal->g[i] = pal->b[i] = 0;
        }
        else
        {
            pal->r[i] = (colorBits & 0x02) ? intensity : 0;
            pal->g[i] = (colorBits & 0x04) ? intensity : 0;
            pal->b[i] = (colorBits & 0x01) ? intensity : 0;
        }
    }

    GifBuildPaletteTree(pal);
}

/// @brief Benchmark FixedZX16 mode - MainScreen (same content as Auto for comparison)
static void BM_GIFWriteFrame_FixedZX16_MainScreen(benchmark::State& state)
{
    const uint32_t width = 256;
    const uint32_t height = 192;
    const size_t bufferSize = width * height;

    // Same content as Auto mode benchmark
    std::vector<uint32_t> framebuffer(bufferSize);
    for (size_t i = 0; i < bufferSize; i++)
    {
        uint8_t color = (i % 16);
        uint8_t r = (color & 0x02) ? ((color & 0x08) ? 0xFF : 0xCD) : 0x00;
        uint8_t g = (color & 0x04) ? ((color & 0x08) ? 0xFF : 0xCD) : 0x00;
        uint8_t b = (color & 0x01) ? ((color & 0x08) ? 0xFF : 0xCD) : 0x00;
        framebuffer[i] = (0xFF << 24) | (b << 16) | (g << 8) | r;
    }

    // Pre-build fixed palette
    GifPalette fixedPalette;
    BuildZXPalette(&fixedPalette);

    std::string tempFile = "/tmp/benchmark_fixed_mainscreen.gif";

    GIFAnimationHelper gifHelper;
    gifHelper.StartAnimation(tempFile, width, height, 20);

    for (auto _ : state)
    {
        // Use fixed palette path - skips palette calculation
        gifHelper.WriteFrameWithPalette(framebuffer.data(), bufferSize, &fixedPalette, false);
    }

    gifHelper.StopAnimation();

    state.SetItemsProcessed(state.iterations());
    state.SetBytesProcessed(state.iterations() * bufferSize * 4);
}
BENCHMARK(BM_GIFWriteFrame_FixedZX16_MainScreen)->Iterations(250)->Unit(benchmark::kMicrosecond);

/// @brief Benchmark FixedZX16 mode - Menu scroll (same content as Auto for comparison)
static void BM_GIFWriteFrame_FixedZX16_MenuScroll(benchmark::State& state)
{
    const uint32_t width = 256;
    const uint32_t height = 192;
    const size_t bufferSize = width * height;

    const uint32_t COLOR_BLACK = 0xFF000000;
    const uint32_t COLOR_CYAN = 0xFFFFFF00;

    const int menuStartY = 48;
    const int menuItemHeight = 16;
    const int menuItems = 8;

    std::vector<uint32_t> framebuffer(bufferSize);

    GifPalette fixedPalette;
    BuildZXPalette(&fixedPalette);

    std::string tempFile = "/tmp/benchmark_fixed_menu.gif";

    GIFAnimationHelper gifHelper;
    gifHelper.StartAnimation(tempFile, width, height, 20);

    int highlightedItem = 0;

    for (auto _ : state)
    {
        for (size_t i = 0; i < bufferSize; i++)
        {
            size_t y = i / width;
            int menuItemIdx = (static_cast<int>(y) - menuStartY) / menuItemHeight;
            bool inMenuArea = (y >= menuStartY && y < menuStartY + menuItems * menuItemHeight);

            if (inMenuArea && menuItemIdx >= 0 && menuItemIdx < menuItems)
            {
                framebuffer[i] = (menuItemIdx == highlightedItem) ? COLOR_CYAN : COLOR_BLACK;
            }
            else
            {
                framebuffer[i] = COLOR_BLACK;
            }
        }

        gifHelper.WriteFrameWithPalette(framebuffer.data(), bufferSize, &fixedPalette, false);
        highlightedItem = (highlightedItem + 1) % menuItems;
    }

    gifHelper.StopAnimation();

    state.SetItemsProcessed(state.iterations());
    state.SetBytesProcessed(state.iterations() * bufferSize * 4);
}
BENCHMARK(BM_GIFWriteFrame_FixedZX16_MenuScroll)->Iterations(200)->Unit(benchmark::kMicrosecond);

/// @brief Benchmark FixedZX16 mode - High change (same content as Auto for comparison)
static void BM_GIFWriteFrame_FixedZX16_HighChange(benchmark::State& state)
{
    const uint32_t width = 256;
    const uint32_t height = 192;
    const size_t bufferSize = width * height;

    std::vector<uint32_t> framebuffer(bufferSize);

    GifPalette fixedPalette;
    BuildZXPalette(&fixedPalette);

    std::string tempFile = "/tmp/benchmark_fixed_change.gif";

    GIFAnimationHelper gifHelper;
    gifHelper.StartAnimation(tempFile, width, height, 20);

    uint32_t frame = 0;

    for (auto _ : state)
    {
        // Change all pixels each frame (worst case)
        for (size_t i = 0; i < bufferSize; i++)
        {
            uint8_t color = ((i + frame) % 16);
            uint8_t r = (color & 0x02) ? ((color & 0x08) ? 0xFF : 0xCD) : 0x00;
            uint8_t g = (color & 0x04) ? ((color & 0x08) ? 0xFF : 0xCD) : 0x00;
            uint8_t b = (color & 0x01) ? ((color & 0x08) ? 0xFF : 0xCD) : 0x00;
            framebuffer[i] = (0xFF << 24) | (b << 16) | (g << 8) | r;
        }

        gifHelper.WriteFrameWithPalette(framebuffer.data(), bufferSize, &fixedPalette, false);
        frame++;
    }

    gifHelper.StopAnimation();

    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_GIFWriteFrame_FixedZX16_HighChange)->Iterations(100)->Unit(benchmark::kMicrosecond);
