/// @file gpupasstimes_test.cpp
/// @brief The screen window's GPU pass times (widgets/gpupasstimes.h) on both backends of the GPU layer: a pass
/// measured from the device's timestamps without a wait, a pass begun while every slot waits left unmeasured, the times
/// read back after the GPU finished, and a lost device forgetting its queries. GL needs an OpenGL 3.3 core context
/// (offscreen has none on macOS: UNREAL_QT_TEST_PLATFORM=cocoa or windows) and a clock (Apple's GL over Metal reads 0
/// for every timestamp), Vulkan a 1.3 device with timestamps; each skips without. The CPU times (Record) need neither.

#include <gtest/gtest.h>

#include <QOffscreenSurface>
#include <QOpenGLContext>

#include <memory>
#include <string>

#include "gpu/gldevice.h"
#include "widgets/gpupasstimes.h"
#include "sharedvulkandevice.h"

namespace
{
class GpuPassTimes_Test : public ::testing::TestWithParam<gpu::Device::Backend>
{
protected:
    void SetUp() override
    {
        if (GetParam() == gpu::Device::Backend::GL)
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
            _gl = gpu::GlDevice::Create();
            _device = _gl.get();
        }
        else
        {
#ifdef UNREAL_VULKAN
            std::string error;
            _device = testgpu::SharedVulkanDevice(error);  // the run's: one device made, not one a test
            if (!_device)
                GTEST_SKIP() << "no Vulkan: " << error;
#else
            GTEST_SKIP() << "built without Vulkan";
#endif
        }
        ASSERT_TRUE(_device);
        _target = _device->CreateTexture(64, 64, gpu::Format::RGBA8);
        ASSERT_TRUE(_target);
        if (!_times.Init(_device))
            GTEST_SKIP() << "no GPU timestamps here";
    }

    void TearDown() override
    {
        _times.Release(false);
        _target.reset();
        _device = nullptr;
        _gl.reset();
        if (_context.isValid())
            _context.doneCurrent();
    }

    /// Some GPU work between the marks
    void Work() { _device->Clear(*_target, 0.1f, 0.2f, 0.3f, 1.0f); }
    void Finish() { _device->Finish(); }

    QOpenGLContext _context;
    QOffscreenSurface _surface;
    std::unique_ptr<gpu::Device> _gl;  ///< GL: the test's own (its context)
    gpu::Device* _device = nullptr;   ///< the device the test draws on (Vulkan: the suite's)
    std::unique_ptr<gpu::Texture> _target;
    GpuPassTimes _times;
};
}  // namespace

INSTANTIATE_TEST_SUITE_P(Backends, GpuPassTimes_Test,
                         ::testing::Values(gpu::Device::Backend::GL, gpu::Device::Backend::Vulkan),
                         [](const ::testing::TestParamInfo<gpu::Device::Backend>& info) {
                             return info.param == gpu::Device::Backend::GL ? std::string("GL") : std::string("Vulkan");
                         });

TEST_P(GpuPassTimes_Test, APassIsMeasuredAfterTheGpuFinished)
{
    _times.Begin(GpuPassTimes::Picture);
    Work();
    _times.End(GpuPassTimes::Picture);
    Finish();
    _times.Collect();
    const GpuPassTimes::Times picture = _times.Get(GpuPassTimes::Picture);
    EXPECT_EQ(picture.measured, 1u);
    EXPECT_GE(picture.maxMs, 0.0);
    EXPECT_LT(picture.maxMs, GpuPassTimes::kSlowMs) << "a clear takes no 100 ms";
    EXPECT_EQ(picture.longestMs, picture.maxMs);
    EXPECT_EQ(_times.Get(GpuPassTimes::Smooth).measured, 0u) << "the other pass is apart";
}

TEST_P(GpuPassTimes_Test, APassBegunWhileEverySlotWaitsGoesUnmeasured)
{
    // Twelve passes, never collected: eight slots fill, the next four find theirs waiting and are skipped
    for (int i = 0; i < 12; ++i)
    {
        _times.Begin(GpuPassTimes::Smooth);
        Work();
        _times.End(GpuPassTimes::Smooth);
    }
    Finish();
    _times.Collect();
    EXPECT_EQ(_times.Get(GpuPassTimes::Smooth).measured, 8u);

    // Collected, the slots are free again
    _times.Begin(GpuPassTimes::Smooth);
    Work();
    _times.End(GpuPassTimes::Smooth);
    Finish();
    _times.Collect();
    EXPECT_EQ(_times.Get(GpuPassTimes::Smooth).measured, 9u);
}

TEST_P(GpuPassTimes_Test, ALostContextForgetsItsQueries)
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
    EXPECT_TRUE(_times.Init(_device)) << "made again on the device";
    EXPECT_EQ(_times.Get(GpuPassTimes::Picture).measured, 0u);
}

TEST_P(GpuPassTimes_Test, TheSegmentsBetweenOpenAndCloseAreOneMeasurement)
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
    Finish();
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
