/// @file gpupasstimes_test.cpp
/// @brief The screen window's GPU pass times (widgets/gpupasstimes.h): a pass measured from GL timestamps without a
/// wait, a pass begun while every slot waits left unmeasured, the times read back after the GPU finished, and a lost
/// context forgetting its queries. Needs an OpenGL 3.3 core context: offscreen has none on macOS
/// (UNREAL_QT_TEST_PLATFORM=cocoa or windows; only an offscreen surface, no window); skips without one, and where the
/// GL has no clock (Apple's GL over Metal reads 0 for every timestamp). The CPU times (Record) need no context.

#include <gtest/gtest.h>

#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QOpenGLFunctions>

#include "widgets/gpupasstimes.h"

namespace
{
class GpuPassTimes_Test : public ::testing::Test
{
protected:
    void SetUp() override
    {
        QSurfaceFormat format;
        format.setRenderableType(QSurfaceFormat::OpenGL);
        format.setVersion(3, 3);
        format.setProfile(QSurfaceFormat::CoreProfile);
        _context.setFormat(format);
        if (!_context.create() || _context.isOpenGLES() || _context.format().version() < qMakePair(3, 3))
            GTEST_SKIP() << "no OpenGL 3.3 core context on this platform";
        _surface.setFormat(_context.format());
        _surface.create();
        if (!_context.makeCurrent(&_surface))
            GTEST_SKIP() << "the GL context is not made current";
        if (!_times.Init(&_context))
            GTEST_SKIP() << "no GL timer queries here";
    }

    void TearDown() override
    {
        _times.Release(false);
        _context.doneCurrent();
    }

    /// Some GL work between the marks
    void Work()
    {
        QOpenGLFunctions* f = _context.functions();
        f->glClearColor(0.1f, 0.2f, 0.3f, 1.0f);
        f->glClear(GL_COLOR_BUFFER_BIT);
    }

    QOpenGLContext _context;
    QOffscreenSurface _surface;
    GpuPassTimes _times;
};
}  // namespace

TEST_F(GpuPassTimes_Test, APassIsMeasuredAfterTheGpuFinished)
{
    _times.Begin(GpuPassTimes::Picture);
    Work();
    _times.End(GpuPassTimes::Picture);
    _context.functions()->glFinish();
    _times.Collect();
    const GpuPassTimes::Times picture = _times.Get(GpuPassTimes::Picture);
    EXPECT_EQ(picture.measured, 1u);
    EXPECT_GE(picture.maxMs, 0.0);
    EXPECT_LT(picture.maxMs, GpuPassTimes::kSlowMs) << "a clear takes no 100 ms";
    EXPECT_EQ(picture.longestMs, picture.maxMs);
    EXPECT_EQ(_times.Get(GpuPassTimes::Smooth).measured, 0u) << "the other pass is apart";
}

TEST_F(GpuPassTimes_Test, APassBegunWhileEverySlotWaitsGoesUnmeasured)
{
    // Twelve passes, never collected: eight slots fill, the next four find theirs waiting and are skipped
    for (int i = 0; i < 12; ++i)
    {
        _times.Begin(GpuPassTimes::Smooth);
        Work();
        _times.End(GpuPassTimes::Smooth);
    }
    _context.functions()->glFinish();
    _times.Collect();
    EXPECT_EQ(_times.Get(GpuPassTimes::Smooth).measured, 8u);

    // Collected, the slots are free again
    _times.Begin(GpuPassTimes::Smooth);
    Work();
    _times.End(GpuPassTimes::Smooth);
    _context.functions()->glFinish();
    _times.Collect();
    EXPECT_EQ(_times.Get(GpuPassTimes::Smooth).measured, 9u);
}

TEST_F(GpuPassTimes_Test, ALostContextForgetsItsQueries)
{
    _times.Begin(GpuPassTimes::Picture);
    Work();
    _times.End(GpuPassTimes::Picture);
    _times.Release(true);
    EXPECT_FALSE(_times.Ready());
    // Nothing measured from here, nothing touched
    _times.Begin(GpuPassTimes::Picture);
    _times.End(GpuPassTimes::Picture);
    _times.Collect();
    EXPECT_TRUE(_times.Init(&_context)) << "made again in a context";
    EXPECT_EQ(_times.Get(GpuPassTimes::Picture).measured, 0u);
}

TEST_F(GpuPassTimes_Test, TheSegmentsBetweenOpenAndCloseAreOneMeasurement)
{
    // Smooth motion's GPU stretches with the CPU's work between them: one measurement, their sum; past kSegments
    // the rest goes unmeasured, still one
    _times.Open(GpuPassTimes::Smooth);
    for (size_t i = 0; i < GpuPassTimes::kSegments + 3; ++i)
    {
        _times.Begin(GpuPassTimes::Smooth);
        Work();
        _times.End(GpuPassTimes::Smooth);
    }
    _times.Close(GpuPassTimes::Smooth);
    // A Begin without its End is closed by Close
    _times.Open(GpuPassTimes::Smooth);
    _times.Begin(GpuPassTimes::Smooth);
    Work();
    _times.Close(GpuPassTimes::Smooth);
    // Nothing begun: nothing measured
    _times.Open(GpuPassTimes::Smooth);
    _times.Close(GpuPassTimes::Smooth);
    _context.functions()->glFinish();
    _times.Collect();
    const GpuPassTimes::Times smooth = _times.Get(GpuPassTimes::Smooth);
    EXPECT_EQ(smooth.measured, 2u);
    EXPECT_LT(smooth.maxMs, GpuPassTimes::kSlowMs);
    EXPECT_EQ(_times.Get(GpuPassTimes::Picture).measured, 0u);
}

TEST(GpuPassTimesRecord_Test, ACpuTimeIsRecordedWithoutAContext)
{
    // SmoothCpu: measured on the CPU, kept whatever the GL has
    GpuPassTimes times;
    for (double ms : {3.0, 1.0, 2.0})
        times.Record(GpuPassTimes::SmoothCpu, ms);
    const GpuPassTimes::Times cpu = times.Get(GpuPassTimes::SmoothCpu);
    EXPECT_EQ(cpu.measured, 3u);
    EXPECT_EQ(cpu.p50Ms, 2.0);
    EXPECT_EQ(cpu.maxMs, 3.0);
    EXPECT_EQ(cpu.longestMs, 3.0);
    EXPECT_EQ(times.Get(GpuPassTimes::Smooth).measured, 0u);
}
