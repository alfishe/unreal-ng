/// @file smoothmotiongpu_test.cpp
/// @brief Smooth motion's GPU passes (SmoothMotionGPU) against the CPU reference (core smoothmotion.h): the same motion
/// field block for block, the same blend, interpolation, extrapolation and masked composition bit for bit, LayerFlow's
/// pictures bit for bit (more vectors than the cap, the cover rule's worst content, a picture cut into stripes, frames
/// known by their ids), and the engine on the GPU device the same pictures as on the CPU.
/// The test needs an OpenGL 3.3 core context: the offscreen platform has none on macOS (UNREAL_QT_TEST_PLATFORM=cocoa),
/// it skips without one.

#include <gtest/gtest.h>

#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <cstdint>
#include <vector>

#include "_helpers/testtier.h"
#include "emulator/video/smoothlayers.h"
#include "emulator/video/smoothmotion.h"
#include "emulator/video/smoothscenes.h"
#include "widgets/smoothmotiongpu.h"

using namespace smoothmotion;

namespace
{
/// A pseudo-random opaque colour per (x, y): no two neighbourhoods alike
uint32_t PatternAt(int x, int y)
{
    uint32_t h = static_cast<uint32_t>(x) * 0x9E3779B1u ^ static_cast<uint32_t>(y) * 0x85EBCA77u;
    h ^= h >> 15;
    h *= 0x2C1B3C6Du;
    h ^= h >> 12;
    return h | 0xFF000000u;
}

struct Image
{
    int width;
    int height;
    std::vector<uint32_t> pixels;
    Image(int w, int h) : width(w), height(h), pixels(static_cast<size_t>(w) * h) {}
    uint32_t& At(int x, int y) { return pixels[static_cast<size_t>(y) * width + x]; }
    Picture View() const { return {width, height, pixels.data()}; }
};

Image DotsImage(int width, int height, int k, bool layered)
{
    Image img(width, height);
    img.pixels = smoothscenes::Dots(width, height, k, layered);
    return img;
}

/// A band scrolled by (dx, dy) over a still background, a sprite at (sx, sy), a line that changes unrelated
Image Scene(int width, int height, int dx, int dy, int sx, int sy, int noise)
{
    Image img(width, height);
    for (int y = 0; y < height; y++)
        for (int x = 0; x < width; x++)
            img.At(x, y) = y >= 16 && y < 48 ? PatternAt(x + dx, y + dy) : PatternAt(x + 5000, y + 5000) & 0xFF3F3F3Fu;
    for (int y = 0; y < 12; y++)
        for (int x = 0; x < 12; x++)
            if (sx + x < width && sy + y < height)
                img.At(sx + x, sy + y) = PatternAt(x + 9000, y + 9000);
    for (int x = 0; x < width; x++)
        img.At(x, height - 3) = PatternAt(x, noise);
    return img;
}

/// Frame k of layers LayerFlow's rules work on: a row of dots moving by its period (the still look-alike ties with
/// the motion, the row's ends tell), dashes of period 8 moving by 1 over a still pattern (+1 and -7 alike), a sprite
/// over paper (its holes: cover), a scrolled band and a blinking cell
Image LayerScene(int width, int height, int k)
{
    Image img(width, height);
    for (int y = 0; y < height; y++)
        for (int x = 0; x < width; x++)
            img.At(x, y) = 0xFF202020u;
    for (int y = 4; y < 6; y++)
        for (int i = 0; i < 12; i++)
            img.At(8 + 4 * k + 4 * i, y) = 0xFFE0E0E0u;
    for (int y = 12; y < 20; y++)
        for (int x = 0; x < width; x++)
        {
            if ((x * 7 + y * 3) % 11 == 0)
                img.At(x, y) = 0xFF4080C0u;
            if ((x - k) % 8 < 3 && (x - k) >= 0)
                img.At(x, y) = 0xFFF0C040u;
        }
    for (int y = 0; y < 10; y++)
        for (int x = 0; x < 10; x++)
            if ((x + y) % 3 != 0)
                img.At(30 + 6 * k + x, 30 + 2 * k + y) = PatternAt(x + 9000, y + 9000);
    for (int y = height - 16; y < height - 6; y++)
        for (int x = 0; x < width; x++)
            img.At(x, y) = PatternAt(x + 3 * k, y) & 0xFF7F7F7Fu;
    for (int y = 0; y < 6; y++)
        for (int x = 0; x < 6; x++)
            img.At(width - 10 + x, 30 + y) = k % 2 ? 0xFFFF0000u : 0xFF0000FFu;
    return img;
}

class GLContext
{
public:
    bool create()
    {
        QSurfaceFormat format;
        format.setRenderableType(QSurfaceFormat::OpenGL);
        format.setVersion(3, 3);
        format.setProfile(QSurfaceFormat::CoreProfile);
        _context.setFormat(format);
        if (!_context.create() || _context.isOpenGLES() || _context.format().version() < qMakePair(3, 3))
            return false;
        _surface.setFormat(_context.format());
        _surface.create();
        return _context.makeCurrent(&_surface);
    }

private:
    QOpenGLContext _context;
    QOffscreenSurface _surface;
};

/// The first pixel where two pictures differ, or -1
long FirstDifference(const std::vector<uint32_t>& a, const std::vector<uint32_t>& b)
{
    for (size_t i = 0; i < a.size() && i < b.size(); i++)
        if (a[i] != b[i])
            return static_cast<long>(i);
    return a.size() == b.size() ? -1 : static_cast<long>(std::min(a.size(), b.size()));
}
}  // namespace

TEST(SmoothMotionGPU_Test, TheGpuStepsAreTheCpuSteps)
{
    GLContext context;
    if (!context.create())
        GTEST_SKIP() << "no OpenGL 3.3 core context on this platform";
    SmoothMotionGPU gpu;
    ASSERT_TRUE(gpu.Create()) << gpu.Log().toStdString();
    gpu.SetFieldsOnGpu(true);

    // An odd size (a partial block row and column), motion along both axes, both ranges
    struct Case
    {
        int width, height, dx, dy, range;
    };
    const Case cases[] = {{100, 70, 4, 0, MotionField::kRangeFrames},
                          {100, 70, -3, 2, MotionField::kRangeFrames},
                          {96, 64, 12, -6, MotionField::kRangePictures}};
    for (const Case& c : cases)
    {
        SCOPED_TRACE(testing::Message() << c.width << "x" << c.height << " by " << c.dx << "," << c.dy);
        const Image a = Scene(c.width, c.height, 0, 0, 20, 50, 1);
        const Image b = Scene(c.width, c.height, c.dx, c.dy, 20 + c.dx, 50 - c.dy, 2);

        MotionField cpuField, gpuField;
        cpuField.Estimate(a.View(), b.View(), c.range);
        ASSERT_TRUE(gpu.Estimate(a.View(), b.View(), c.range, gpuField)) << gpu.Log().toStdString();
        ASSERT_EQ(gpuField.BlocksWide(), cpuField.BlocksWide());
        ASSERT_EQ(gpuField.BlocksHigh(), cpuField.BlocksHigh());
        for (int j = 0; j < cpuField.BlocksHigh(); j++)
            for (int i = 0; i < cpuField.BlocksWide(); i++)
            {
                EXPECT_EQ(gpuField.At(i, j).matched, cpuField.At(i, j).matched) << "block " << i << "," << j;
                EXPECT_EQ(gpuField.At(i, j).v, cpuField.At(i, j).v) << "block " << i << "," << j;
            }
        EXPECT_EQ(gpuField.Common(), cpuField.Common());
        EXPECT_GT(cpuField.GetStats().moved, 0) << "the scene moves";

        // Blend, interpolation, extrapolation; and composition inside a mask over what stays
        for (int w : {0, 77, 128, 200, 256, 300, 512})
        {
            Image cpu(c.width, c.height), out(c.width, c.height);
            if (w <= kOne)
            {
                Blend(a.View(), b.View(), w, cpu.pixels.data());
                ASSERT_TRUE(gpu.Blend(a.View(), b.View(), w, out.pixels.data()));
                EXPECT_EQ(FirstDifference(out.pixels, cpu.pixels), -1) << "blend at " << w;
            }
            cpuField.Compose(a.View(), b.View(), w, cpu.pixels.data());
            ASSERT_TRUE(gpu.Compose(cpuField, a.View(), b.View(), w, out.pixels.data(), nullptr));
            EXPECT_EQ(FirstDifference(out.pixels, cpu.pixels), -1) << "compose at " << w;

            std::vector<uint8_t> mask(cpu.pixels.size(), 0);
            for (int y = 8; y < c.height - 8; y++)
                for (int x = 4; x < c.width - 10; x++)
                    mask[static_cast<size_t>(y) * c.width + x] = 1;
            cpu.pixels = a.pixels;
            out.pixels = a.pixels;
            cpuField.Compose(a.View(), b.View(), w, cpu.pixels.data(), mask.data());
            ASSERT_TRUE(gpu.Compose(cpuField, a.View(), b.View(), w, out.pixels.data(), mask.data()));
            EXPECT_EQ(FirstDifference(out.pixels, cpu.pixels), -1) << "masked compose at " << w;
        }
    }
    gpu.Destroy();
}

TEST(SmoothMotionGPU_Test, LayerFlowOnTheGpuIsTheCpus)
{
    GLContext context;
    if (!context.create())
        GTEST_SKIP() << "no OpenGL 3.3 core context on this platform";
    SmoothMotionGPU gpu;
    ASSERT_TRUE(gpu.Create()) << gpu.Log().toStdString();

    // An odd size (a partial block row and column); the first pair after a seek (p = a, everything still before)
    LayerFlow cpu;
    int asks = 0, edges = 0;
    for (const auto& [width, height] : {std::pair{120, 74}, {96, 64}})
        for (int k : {1, 2})
        {
            SCOPED_TRACE(testing::Message() << width << "x" << height << " k " << k);
            const Image p = LayerScene(width, height, k - 1), a = LayerScene(width, height, k),
                        b = LayerScene(width, height, k + 1);
            MotionField prev, next;
            prev.Estimate(p.View(), a.View(), MotionField::kRangeFrames);
            next.Estimate(a.View(), b.View(), MotionField::kRangeFrames);
            for (int w : {0, 43, 85, 128, 171, 213, 256})
            {
                Image expected(width, height), out(width, height);
                cpu.Compose(p.View(), a.View(), b.View(), prev, next, w, expected.pixels.data());
                ASSERT_TRUE(gpu.ComposeLayers(p.View(), a.View(), b.View(), prev, next, w, out.pixels.data()))
                    << gpu.Log().toStdString();
                EXPECT_EQ(FirstDifference(out.pixels, expected.pixels), -1) << "w " << w;
                asks += gpu.LastLayerCounts().ownAsks;
                edges += gpu.LastLayerCounts().edges;
                cpu.Compose(a.View(), a.View(), b.View(), prev, next, w, expected.pixels.data());
                ASSERT_TRUE(gpu.ComposeLayers(a.View(), a.View(), b.View(), prev, next, w, out.pixels.data()));
                EXPECT_EQ(FirstDifference(out.pixels, expected.pixels), -1) << "p = a, w " << w;
            }
        }
    EXPECT_GT(asks, 0) << "the aliases rule asked the GPU";
    EXPECT_GT(edges, 0) << "the regions rule asked the GPU";
    gpu.Destroy();
}

TEST(SmoothMotionGPU_Test, TheEngineOnTheGpuShowsTheCpuPictures)
{
    GLContext context;
    if (!context.create())
        GTEST_SKIP() << "no OpenGL 3.3 core context on this platform";
    SmoothMotionGPU gpu;
    ASSERT_TRUE(gpu.Create()) << gpu.Log().toStdString();

    constexpr int64_t kPeriod = 20'000'000;
    SmoothMotion cpu, onGpu;
    onGpu.SetDevice(&gpu);
    GameVideo game;
    game.picturePeriod = 2;
    game.mode = GameMode::Carry;
    game.window = {0, 0, 96, 56};
    for (SmoothMotion* engine : {&cpu, &onGpu})
    {
        engine->SetMode(Mode::Interpolate);
        for (int f = 0; f < 6; f++)
        {
            const Image img = Scene(96, 64, f * 3, 0, 10 + f * 2, 50, f);
            engine->Push(static_cast<uint64_t>(f + 1), 1000 * kPeriod + f * kPeriod, img.pixels.data(), 96, 64);
        }
    }
    Image a(96, 64), b(96, 64);
    for (bool withGame : {false, true})
    {
        cpu.SetGame(withGame ? &game : nullptr);
        onGpu.SetGame(withGame ? &game : nullptr);
        for (int64_t t = 1000 * kPeriod; t < 1006 * kPeriod; t += kPeriod / 3)
        {
            const SmoothMotion::Result rc = cpu.Render(t, a.pixels.data());
            const SmoothMotion::Result rg = onGpu.Render(t, b.pixels.data());
            EXPECT_EQ(rg, rc) << "t " << t;
            EXPECT_EQ(FirstDifference(b.pixels, a.pixels), -1) << "t " << t << (withGame ? " (game)" : "");
        }
    }
    EXPECT_GT(onGpu.GetStats().onDevice, 0u);
    EXPECT_EQ(cpu.GetStats().onDevice, 0u);
    gpu.Destroy();
}

TEST(SmoothMotionGPU_Test, TheWorstContentIsTheCpusBits)
{
    // More vectors than the cap (a block a vector of its own) and every still pixel asking the cover rule about many
    // moving vectors: the GPU keeps the CPU's vectors and bits. The screen's size in the long tier
    GLContext context;
    if (!context.create())
        GTEST_SKIP() << "no OpenGL 3.3 core context on this platform";
    SmoothMotionGPU gpu;
    ASSERT_TRUE(gpu.Create()) << gpu.Log().toStdString();
    const int width = TestTier::Long() ? 352 : 96, height = TestTier::Long() ? 288 : 64;
    LayerFlow cpu;
    for (bool layered : {false, true})
    {
        SCOPED_TRACE(layered ? "layered" : "a vector a block");
        // layered: every pass cut into stripes of a few rows
        gpu.SetMaxReadsPerDraw(layered ? int64_t{1} << 20 : SmoothMotionGPU::kMaxReadsPerDraw);
        const Image p = DotsImage(width, height, 0, layered), a = DotsImage(width, height, 1, layered),
                    b = DotsImage(width, height, 2, layered);
        MotionField prev, next;
        prev.Estimate(p.View(), a.View(), MotionField::kRangeFrames);
        next.Estimate(a.View(), b.View(), MotionField::kRangeFrames);
        LayerFlow::Layers layers;
        LayerFlow::Prepare(prev, next, width, height, layers);
        for (int w : {43, 128, 213})
        {
            Image expected(width, height), out(width, height);
            cpu.Compose(p.View(), a.View(), b.View(), prev, next, w, expected.pixels.data());
            ASSERT_TRUE(gpu.ComposeLayers(p.View(), a.View(), b.View(), prev, next, w, out.pixels.data()))
                << gpu.Log().toStdString();
            EXPECT_EQ(FirstDifference(out.pixels, expected.pixels), -1) << "w " << w;
            EXPECT_EQ(gpu.LastLayerCounts().vectors, static_cast<int>(layers.all.size()));
        }
        if (!layered)
            EXPECT_EQ(layers.all.size(), LayerFlow::kMaxVectors) << "the scene reaches the cap";
    }
    gpu.Destroy();
}

TEST(SmoothMotionGPU_Test, FramesAreKnownByTheirIds)
{
    // A frame with an id is not compared again: the same id, the texture it is in. A new id uploads; pictures
    // without one compare their pixels. Released buffers come back on the next step
    GLContext context;
    if (!context.create())
        GTEST_SKIP() << "no OpenGL 3.3 core context on this platform";
    SmoothMotionGPU gpu;
    ASSERT_TRUE(gpu.Create()) << gpu.Log().toStdString();
    const Image f0 = LayerScene(96, 64, 0), f1 = LayerScene(96, 64, 1), f2 = LayerScene(96, 64, 2),
                f3 = LayerScene(96, 64, 3);
    const auto withId = [](const Image& img, uint64_t id) { return Picture{img.width, img.height, img.pixels.data(), id}; };
    LayerFlow cpu;
    const Image* frames[4] = {&f0, &f1, &f2, &f3};
    for (int round = 0; round < 3; round++)
    {
        SCOPED_TRACE(testing::Message() << "round " << round);
        for (int k = 1; k + 1 < 4; k++)
        {
            const Image &p = *frames[k - 1], &a = *frames[k], &b = *frames[k + 1];
            MotionField prev, next;
            prev.Estimate(p.View(), a.View(), MotionField::kRangeFrames);
            next.Estimate(a.View(), b.View(), MotionField::kRangeFrames);
            Image expected(96, 64), out(96, 64);
            cpu.Compose(p.View(), a.View(), b.View(), prev, next, 128, expected.pixels.data());
            // round 0: ids, the frames moving on by one; round 1: no ids; round 2: ids again after a release
            const uint64_t base = round == 1 ? 0 : 100 * static_cast<uint64_t>(round + 1);
            const auto view = [&](const Image& img, int index) {
                return base == 0 ? img.View() : withId(img, base + static_cast<uint64_t>(index));
            };
            ASSERT_TRUE(gpu.ComposeLayers(view(p, k - 1), view(a, k), view(b, k + 1), prev, next, 128,
                                          out.pixels.data()));
            EXPECT_EQ(FirstDifference(out.pixels, expected.pixels), -1) << "k " << k;
        }
        if (round == 1)
            gpu.ReleaseBuffers();
    }
    gpu.Destroy();
}
