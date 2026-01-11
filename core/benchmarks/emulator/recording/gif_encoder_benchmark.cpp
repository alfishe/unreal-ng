#include <benchmark/benchmark.h>

#include "3rdparty/gif/gif.h"
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

    GifWriter gifWriter = {};
    GifBegin(&gifWriter, tempFile.c_str(), width, height, 2);

    // Benchmark loop
    for (auto _ : state)
    {
        GifWriteFrame(&gifWriter, reinterpret_cast<uint8_t*>(framebuffer.data()), width, height, 2);
    }

    GifEnd(&gifWriter);

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

    GifWriter gifWriter = {};
    GifBegin(&gifWriter, tempFile.c_str(), width, height, 2);

    // Benchmark loop
    for (auto _ : state)
    {
        GifWriteFrame(&gifWriter, reinterpret_cast<uint8_t*>(framebuffer.data()), width, height, 2);
    }

    GifEnd(&gifWriter);

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

    GifWriter gifWriter = {};
    GifBegin(&gifWriter, tempFile.c_str(), width, height, 2);

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

        GifWriteFrame(&gifWriter, reinterpret_cast<uint8_t*>(framebuffer.data()), width, height, 2);
        frame++;
    }

    GifEnd(&gifWriter);

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

    GifWriter gifWriter = {};
    GifBegin(&gifWriter, tempFile.c_str(), width, height, 2);

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

        GifWriteFrame(&gifWriter, reinterpret_cast<uint8_t*>(framebuffer.data()), width, height, 2);

        // Move highlight to next item (cycling)
        highlightedItem = (highlightedItem + 1) % menuItems;
    }

    GifEnd(&gifWriter);

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

    GifWriter gifWriter = {};
    GifBegin(&gifWriter, tempFile.c_str(), width, height, 2);

    for (auto _ : state)
    {
        // Use fixed palette path - skips palette calculation
        GifWriteFrameFast(&gifWriter, reinterpret_cast<uint8_t*>(framebuffer.data()), width, height, 2, &fixedPalette, false);
    }

    GifEnd(&gifWriter);

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

    GifWriter gifWriter = {};
    GifBegin(&gifWriter, tempFile.c_str(), width, height, 2);

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

        GifWriteFrameFast(&gifWriter, reinterpret_cast<uint8_t*>(framebuffer.data()), width, height, 2, &fixedPalette, false);
        highlightedItem = (highlightedItem + 1) % menuItems;
    }

    GifEnd(&gifWriter);

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

    GifWriter gifWriter = {};
    GifBegin(&gifWriter, tempFile.c_str(), width, height, 2);

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

        GifWriteFrameFast(&gifWriter, reinterpret_cast<uint8_t*>(framebuffer.data()), width, height, 2, &fixedPalette, false);
        frame++;
    }

    GifEnd(&gifWriter);

    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_GIFWriteFrame_FixedZX16_HighChange)->Iterations(100)->Unit(benchmark::kMicrosecond);

// =============================================================================
// OPT-1: Direct ZX Index Lookup Benchmarks
// =============================================================================
// These benchmarks test GifWriteFrameZX which uses direct O(1) palette index
// lookup instead of k-d tree traversal. This is the fastest path for ZX content.

/// @brief Benchmark ZX-optimized path - MainScreen (direct index lookup)
static void BM_GIFWriteFrame_DirectZX_MainScreen(benchmark::State& state)
{
    const uint32_t width = 256;
    const uint32_t height = 192;
    const size_t bufferSize = width * height;

    // Create test framebuffer with exact ZX Spectrum colors
    std::vector<uint32_t> framebuffer(bufferSize);
    for (size_t i = 0; i < bufferSize; i++)
    {
        uint8_t color = (i % 16);
        uint8_t r = (color & 0x02) ? ((color & 0x08) ? 0xFF : 0xCD) : 0x00;
        uint8_t g = (color & 0x04) ? ((color & 0x08) ? 0xFF : 0xCD) : 0x00;
        uint8_t b = (color & 0x01) ? ((color & 0x08) ? 0xFF : 0xCD) : 0x00;
        framebuffer[i] = (0xFF << 24) | (b << 16) | (g << 8) | r;
    }

    // Build fixed palette
    GifPalette fixedPalette = {};
    fixedPalette.bitDepth = 4;  // 16 colors

    // Standard ZX Spectrum 16-color palette
    const uint8_t zxColors[16][3] = {{0x00, 0x00, 0x00}, {0x00, 0x00, 0xCD}, {0xCD, 0x00, 0x00}, {0xCD, 0x00, 0xCD},
                                     {0x00, 0xCD, 0x00}, {0x00, 0xCD, 0xCD}, {0xCD, 0xCD, 0x00}, {0xCD, 0xCD, 0xCD},
                                     {0x00, 0x00, 0x00}, {0x00, 0x00, 0xFF}, {0xFF, 0x00, 0x00}, {0xFF, 0x00, 0xFF},
                                     {0x00, 0xFF, 0x00}, {0x00, 0xFF, 0xFF}, {0xFF, 0xFF, 0x00}, {0xFF, 0xFF, 0xFF}};
    for (int i = 0; i < 16; i++)
    {
        fixedPalette.r[i] = zxColors[i][0];
        fixedPalette.g[i] = zxColors[i][1];
        fixedPalette.b[i] = zxColors[i][2];
    }
    GifBuildPaletteTree(&fixedPalette);

    std::string tempFile = "/tmp/benchmark_direct_zx_mainscreen.gif";

    GifWriter gifWriter = {};
    GifBegin(&gifWriter, tempFile.c_str(), width, height, 2);

    for (auto _ : state)
    {
        // Use ZX-optimized path with direct index lookup
        GifWriteFrameZX(&gifWriter, reinterpret_cast<uint8_t*>(framebuffer.data()), width, height, 2, &fixedPalette);
    }

    GifEnd(&gifWriter);

    state.SetItemsProcessed(state.iterations());
    state.SetBytesProcessed(state.iterations() * bufferSize * 4);
}
BENCHMARK(BM_GIFWriteFrame_DirectZX_MainScreen)->Iterations(250)->Unit(benchmark::kMicrosecond);

/// @brief Benchmark ZX-optimized path - HighChange scenario
static void BM_GIFWriteFrame_DirectZX_HighChange(benchmark::State& state)
{
    const uint32_t width = 256;
    const uint32_t height = 192;
    const size_t bufferSize = width * height;

    std::vector<uint32_t> framebuffer(bufferSize);

    // Build fixed palette
    GifPalette fixedPalette = {};
    fixedPalette.bitDepth = 4;

    const uint8_t zxColors[16][3] = {{0x00, 0x00, 0x00}, {0x00, 0x00, 0xCD}, {0xCD, 0x00, 0x00}, {0xCD, 0x00, 0xCD},
                                     {0x00, 0xCD, 0x00}, {0x00, 0xCD, 0xCD}, {0xCD, 0xCD, 0x00}, {0xCD, 0xCD, 0xCD},
                                     {0x00, 0x00, 0x00}, {0x00, 0x00, 0xFF}, {0xFF, 0x00, 0x00}, {0xFF, 0x00, 0xFF},
                                     {0x00, 0xFF, 0x00}, {0x00, 0xFF, 0xFF}, {0xFF, 0xFF, 0x00}, {0xFF, 0xFF, 0xFF}};
    for (int i = 0; i < 16; i++)
    {
        fixedPalette.r[i] = zxColors[i][0];
        fixedPalette.g[i] = zxColors[i][1];
        fixedPalette.b[i] = zxColors[i][2];
    }
    GifBuildPaletteTree(&fixedPalette);

    std::string tempFile = "/tmp/benchmark_direct_zx_highchange.gif";

    GifWriter gifWriter = {};
    GifBegin(&gifWriter, tempFile.c_str(), width, height, 2);

    int frame = 0;
    for (auto _ : state)
    {
        // Every frame is completely different (worst case - maximum work)
        for (size_t i = 0; i < bufferSize; i++)
        {
            uint8_t color = ((i + frame * 7) % 16);
            uint8_t r = (color & 0x02) ? ((color & 0x08) ? 0xFF : 0xCD) : 0x00;
            uint8_t g = (color & 0x04) ? ((color & 0x08) ? 0xFF : 0xCD) : 0x00;
            uint8_t b = (color & 0x01) ? ((color & 0x08) ? 0xFF : 0xCD) : 0x00;
            framebuffer[i] = (0xFF << 24) | (b << 16) | (g << 8) | r;
        }

        GifWriteFrameZX(&gifWriter, reinterpret_cast<uint8_t*>(framebuffer.data()), width, height, 2, &fixedPalette);
        frame++;
    }

    GifEnd(&gifWriter);

    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_GIFWriteFrame_DirectZX_HighChange)->Iterations(100)->Unit(benchmark::kMicrosecond);

// ================== Hash Table Lookup Benchmarks ==================

/// @brief Benchmark hash table build time for 256 colors
static void BM_HashLookup_Build256Colors(benchmark::State& state)
{
    GifPalette palette = {};
    palette.bitDepth = 8;  // 256 colors
    for (int i = 0; i < 256; i++)
    {
        palette.r[i] = i;
        palette.g[i] = (i * 2) % 256;
        palette.b[i] = (i * 3) % 256;
    }

    for (auto _ : state)
    {
        GifColorLookup lookup = {};
        GifBuildColorLookup(&lookup, &palette);
        benchmark::DoNotOptimize(lookup);
    }

    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_HashLookup_Build256Colors)->Iterations(10000)->Unit(benchmark::kNanosecond);

/// @brief Benchmark hash table lookup time (single color)
static void BM_HashLookup_SingleColor(benchmark::State& state)
{
    GifPalette palette = {};
    palette.bitDepth = 8;
    for (int i = 0; i < 256; i++)
    {
        palette.r[i] = i;
        palette.g[i] = (i * 2) % 256;
        palette.b[i] = (i * 3) % 256;
    }

    GifColorLookup lookup = {};
    GifBuildColorLookup(&lookup, &palette);

    // Test color in middle of hash table
    uint32_t testColor = 0xFF800040;  // Some color

    for (auto _ : state)
    {
        uint8_t index = GifGetColorIndex(&lookup, testColor);
        benchmark::DoNotOptimize(index);
    }

    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_HashLookup_SingleColor)->Iterations(100000)->Unit(benchmark::kNanosecond);

/// @brief Benchmark GifThresholdImageExact (hash lookup) vs GifThresholdImage (k-d tree)
static void BM_ThresholdImage_HashLookup(benchmark::State& state)
{
    const uint32_t width = 352;
    const uint32_t height = 288;
    const size_t numPixels = width * height;

    // Use emulator palette
    const uint32_t emulatorABGR[16] = {
        0xFF000000, 0xFFC72200, 0xFF1628D6, 0xFFC733D4, 0xFF25C500, 0xFFC9C700, 0xFF2AC8CC, 0xFFCACACA,
        0xFF000000, 0xFFFB2B00, 0xFF1C33FF, 0xFFFC40FF, 0xFF2FF900, 0xFFFEFB00, 0xFF36FCFF, 0xFFFFFFFF,
    };

    GifPalette palette = {};
    palette.bitDepth = 4;
    for (int i = 0; i < 16; i++)
    {
        palette.r[i] = emulatorABGR[i] & 0xFF;
        palette.g[i] = (emulatorABGR[i] >> 8) & 0xFF;
        palette.b[i] = (emulatorABGR[i] >> 16) & 0xFF;
    }
    GifBuildPaletteTree(&palette);

    GifColorLookup lookup = {};
    GifBuildColorLookup(&lookup, &palette);

    // Create framebuffer with cycling colors
    std::vector<uint32_t> framebuffer(numPixels);
    for (size_t i = 0; i < numPixels; i++)
    {
        framebuffer[i] = emulatorABGR[i % 16];
    }

    std::vector<uint8_t> output(numPixels * 4);

    for (auto _ : state)
    {
        GifThresholdImageExact(nullptr, reinterpret_cast<uint8_t*>(framebuffer.data()), output.data(), width, height,
                               &lookup, &palette);
    }

    state.SetItemsProcessed(state.iterations());
    state.SetBytesProcessed(state.iterations() * numPixels * 4);
}
BENCHMARK(BM_ThresholdImage_HashLookup)->Iterations(500)->Unit(benchmark::kMicrosecond);

/// @brief Benchmark k-d tree lookup for comparison
static void BM_ThresholdImage_KdTree(benchmark::State& state)
{
    const uint32_t width = 352;
    const uint32_t height = 288;
    const size_t numPixels = width * height;

    // Use emulator palette
    const uint32_t emulatorABGR[16] = {
        0xFF000000, 0xFFC72200, 0xFF1628D6, 0xFFC733D4, 0xFF25C500, 0xFFC9C700, 0xFF2AC8CC, 0xFFCACACA,
        0xFF000000, 0xFFFB2B00, 0xFF1C33FF, 0xFFFC40FF, 0xFF2FF900, 0xFFFEFB00, 0xFF36FCFF, 0xFFFFFFFF,
    };

    GifPalette palette = {};
    palette.bitDepth = 4;
    for (int i = 0; i < 16; i++)
    {
        palette.r[i] = emulatorABGR[i] & 0xFF;
        palette.g[i] = (emulatorABGR[i] >> 8) & 0xFF;
        palette.b[i] = (emulatorABGR[i] >> 16) & 0xFF;
    }
    GifBuildPaletteTree(&palette);

    // Create framebuffer with cycling colors
    std::vector<uint32_t> framebuffer(numPixels);
    for (size_t i = 0; i < numPixels; i++)
    {
        framebuffer[i] = emulatorABGR[i % 16];
    }

    std::vector<uint8_t> output(numPixels * 4);

    for (auto _ : state)
    {
        GifThresholdImage(nullptr, reinterpret_cast<uint8_t*>(framebuffer.data()), output.data(), width, height,
                          &palette);
    }

    state.SetItemsProcessed(state.iterations());
    state.SetBytesProcessed(state.iterations() * numPixels * 4);
}
BENCHMARK(BM_ThresholdImage_KdTree)->Iterations(500)->Unit(benchmark::kMicrosecond);

// ================== End-to-End Comparison Benchmarks ==================
// All benchmarks use the same scenario for fair comparison

/// @brief Benchmark end-to-end: Auto mode (baseline)
static void BM_E2E_AutoMode(benchmark::State& state)
{
    const uint32_t width = 352;
    const uint32_t height = 288;
    const size_t bufferSize = width * height;

    // Emulator palette
    const uint32_t emulatorABGR[16] = {
        0xFF000000, 0xFFC72200, 0xFF1628D6, 0xFFC733D4, 0xFF25C500, 0xFFC9C700, 0xFF2AC8CC, 0xFFCACACA,
        0xFF000000, 0xFFFB2B00, 0xFF1C33FF, 0xFFFC40FF, 0xFF2FF900, 0xFFFEFB00, 0xFF36FCFF, 0xFFFFFFFF,
    };

    std::vector<uint32_t> framebuffer(bufferSize);
    for (size_t i = 0; i < bufferSize; i++)
    {
        framebuffer[i] = emulatorABGR[i % 16];
    }

    std::string tempFile = "/tmp/benchmark_e2e_auto.gif";
    GifWriter gifWriter = {};
    GifBegin(&gifWriter, tempFile.c_str(), width, height, 2);

    for (auto _ : state)
    {
        GifWriteFrame(&gifWriter, reinterpret_cast<uint8_t*>(framebuffer.data()), width, height, 2);
    }

    GifEnd(&gifWriter);
    state.SetItemsProcessed(state.iterations());
    state.SetBytesProcessed(state.iterations() * bufferSize * 4);
}
BENCHMARK(BM_E2E_AutoMode)->Iterations(250)->Unit(benchmark::kMicrosecond);

/// @brief Benchmark end-to-end: Fixed palette with k-d tree
static void BM_E2E_FixedPalette_KdTree(benchmark::State& state)
{
    const uint32_t width = 352;
    const uint32_t height = 288;
    const size_t bufferSize = width * height;

    const uint32_t emulatorABGR[16] = {
        0xFF000000, 0xFFC72200, 0xFF1628D6, 0xFFC733D4, 0xFF25C500, 0xFFC9C700, 0xFF2AC8CC, 0xFFCACACA,
        0xFF000000, 0xFFFB2B00, 0xFF1C33FF, 0xFFFC40FF, 0xFF2FF900, 0xFFFEFB00, 0xFF36FCFF, 0xFFFFFFFF,
    };

    GifPalette palette = {};
    palette.bitDepth = 4;
    for (int i = 0; i < 16; i++)
    {
        palette.r[i] = emulatorABGR[i] & 0xFF;
        palette.g[i] = (emulatorABGR[i] >> 8) & 0xFF;
        palette.b[i] = (emulatorABGR[i] >> 16) & 0xFF;
    }
    GifBuildPaletteTree(&palette);

    std::vector<uint32_t> framebuffer(bufferSize);
    for (size_t i = 0; i < bufferSize; i++)
    {
        framebuffer[i] = emulatorABGR[i % 16];
    }

    std::string tempFile = "/tmp/benchmark_e2e_fixed_kdtree.gif";
    GifWriter gifWriter = {};
    GifBegin(&gifWriter, tempFile.c_str(), width, height, 2);

    for (auto _ : state)
    {
        GifWriteFrameFast(&gifWriter, reinterpret_cast<uint8_t*>(framebuffer.data()), width, height, 2, &palette, false);
    }

    GifEnd(&gifWriter);
    state.SetItemsProcessed(state.iterations());
    state.SetBytesProcessed(state.iterations() * bufferSize * 4);
}
BENCHMARK(BM_E2E_FixedPalette_KdTree)->Iterations(250)->Unit(benchmark::kMicrosecond);

/// @brief Benchmark end-to-end: Hash lookup (exact matching)
static void BM_E2E_HashLookup(benchmark::State& state)
{
    const uint32_t width = 352;
    const uint32_t height = 288;
    const size_t bufferSize = width * height;

    const uint32_t emulatorABGR[16] = {
        0xFF000000, 0xFFC72200, 0xFF1628D6, 0xFFC733D4, 0xFF25C500, 0xFFC9C700, 0xFF2AC8CC, 0xFFCACACA,
        0xFF000000, 0xFFFB2B00, 0xFF1C33FF, 0xFFFC40FF, 0xFF2FF900, 0xFFFEFB00, 0xFF36FCFF, 0xFFFFFFFF,
    };

    GifPalette palette = {};
    palette.bitDepth = 4;
    for (int i = 0; i < 16; i++)
    {
        palette.r[i] = emulatorABGR[i] & 0xFF;
        palette.g[i] = (emulatorABGR[i] >> 8) & 0xFF;
        palette.b[i] = (emulatorABGR[i] >> 16) & 0xFF;
    }
    GifBuildPaletteTree(&palette);

    GifColorLookup lookup = {};
    GifBuildColorLookup(&lookup, &palette);

    std::vector<uint32_t> framebuffer(bufferSize);
    for (size_t i = 0; i < bufferSize; i++)
    {
        framebuffer[i] = emulatorABGR[i % 16];
    }

    std::string tempFile = "/tmp/benchmark_e2e_hash.gif";
    GifWriter gifWriter = {};
    GifBegin(&gifWriter, tempFile.c_str(), width, height, 2);

    for (auto _ : state)
    {
        GifWriteFrameExact(&gifWriter, reinterpret_cast<uint8_t*>(framebuffer.data()), width, height, 2, &palette, &lookup);
    }

    GifEnd(&gifWriter);
    state.SetItemsProcessed(state.iterations());
    state.SetBytesProcessed(state.iterations() * bufferSize * 4);
}
BENCHMARK(BM_E2E_HashLookup)->Iterations(250)->Unit(benchmark::kMicrosecond);

/// @brief Benchmark end-to-end: DirectZX (hardcoded ZX colors)
static void BM_E2E_DirectZX(benchmark::State& state)
{
    const uint32_t width = 352;
    const uint32_t height = 288;
    const size_t bufferSize = width * height;

    // Standard ZX colors for DirectZX (different from emulator palette!)
    const uint8_t zxColors[16][3] = {
        {0x00, 0x00, 0x00}, {0x00, 0x00, 0xCD}, {0xCD, 0x00, 0x00}, {0xCD, 0x00, 0xCD},
        {0x00, 0xCD, 0x00}, {0x00, 0xCD, 0xCD}, {0xCD, 0xCD, 0x00}, {0xCD, 0xCD, 0xCD},
        {0x00, 0x00, 0x00}, {0x00, 0x00, 0xFF}, {0xFF, 0x00, 0x00}, {0xFF, 0x00, 0xFF},
        {0x00, 0xFF, 0x00}, {0x00, 0xFF, 0xFF}, {0xFF, 0xFF, 0x00}, {0xFF, 0xFF, 0xFF},
    };

    GifPalette palette = {};
    palette.bitDepth = 4;
    for (int i = 0; i < 16; i++)
    {
        palette.r[i] = zxColors[i][0];
        palette.g[i] = zxColors[i][1];
        palette.b[i] = zxColors[i][2];
    }
    GifBuildPaletteTree(&palette);

    // Create ZX-compatible framebuffer
    std::vector<uint32_t> framebuffer(bufferSize);
    for (size_t i = 0; i < bufferSize; i++)
    {
        uint8_t color = i % 16;
        uint8_t r = zxColors[color][0];
        uint8_t g = zxColors[color][1];
        uint8_t b = zxColors[color][2];
        framebuffer[i] = (0xFF << 24) | (b << 16) | (g << 8) | r;
    }

    std::string tempFile = "/tmp/benchmark_e2e_directzx.gif";
    GifWriter gifWriter = {};
    GifBegin(&gifWriter, tempFile.c_str(), width, height, 2);

    for (auto _ : state)
    {
        GifWriteFrameZX(&gifWriter, reinterpret_cast<uint8_t*>(framebuffer.data()), width, height, 2, &palette);
    }

    GifEnd(&gifWriter);
    state.SetItemsProcessed(state.iterations());
    state.SetBytesProcessed(state.iterations() * bufferSize * 4);
}
BENCHMARK(BM_E2E_DirectZX)->Iterations(250)->Unit(benchmark::kMicrosecond);
