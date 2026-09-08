#include <gtest/gtest.h>
#include <chrono>
#include <iostream>
#include <iomanip>

#include "hud/core/hudmodel.h"
#include "hud/core/hudtiming.h"

class HudPerformanceBenchmark : public ::testing::Test
{
protected:
    void SetUp() override
    {
        _model = std::make_shared<HudModel>(nullptr);
        _model->onFeatureChanged(true);  // Enable HUD
    }

    std::shared_ptr<HudModel> _model;

    // Measure execution time of a callable
    template<typename Func>
    double measureMs(Func&& func, int iterations)
    {
        auto start = std::chrono::high_resolution_clock::now();
        for (int i = 0; i < iterations; ++i)
        {
            func();
        }
        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
        return duration.count() / 1000.0; // Return ms
    }
};

// Benchmark: Indicator updates with SAME content (should be fast with dirty tracking)
TEST_F(HudPerformanceBenchmark, IndicatorUpdateSameContent)
{
    constexpr int kIterations = 10000;
    constexpr int kFrames = 50; // Simulate 1 second at 50fps

    // Initial indicator
    _model->setIndicatorAt("beeper", HudTilePosition::TopLeft, HudState::Active,
                           "BEEP", "", "speaker", std::chrono::milliseconds(1000), true);

    // Measure repeated updates with SAME content (simulates beeper firing every frame)
    double timeMs = measureMs([this]() {
        _model->setIndicatorAt("beeper", HudTilePosition::TopLeft, HudState::Active,
                               "BEEP", "", "speaker", std::chrono::milliseconds(1000), true);
    }, kIterations);

    double perCallUs = (timeMs * 1000.0) / kIterations;
    double perFrameUs = perCallUs * 1; // 1 indicator per frame

    std::cout << "\n=== Indicator Update (Same Content) ===" << std::endl;
    std::cout << "  Iterations: " << kIterations << std::endl;
    std::cout << "  Total time: " << std::fixed << std::setprecision(2) << timeMs << " ms" << std::endl;
    std::cout << "  Per call:   " << std::fixed << std::setprecision(3) << perCallUs << " us" << std::endl;
    std::cout << "  Per frame (50fps): " << std::fixed << std::setprecision(3) << perFrameUs << " us" << std::endl;
    std::cout << "  Frame budget used: " << std::fixed << std::setprecision(4)
              << (perFrameUs / 20000.0) * 100 << "% (of 20ms)" << std::endl;

    // Should be very fast with dirty tracking - under 1us per call
    EXPECT_LT(perCallUs, 10.0) << "Same-content update should be < 10us";
}

// Benchmark: Indicator updates with DIFFERENT content (triggers full publish)
TEST_F(HudPerformanceBenchmark, IndicatorUpdateDifferentContent)
{
    constexpr int kIterations = 10000;

    double timeMs = measureMs([this, i = 0]() mutable {
        char buf[16];
        snprintf(buf, sizeof(buf), "RAM %d", i % 8);
        _model->setIndicatorAt("mem", HudTilePosition::TopLeft, HudState::Active,
                               buf, "", "ram", std::chrono::milliseconds(1200), true);
        ++i;
    }, kIterations);

    double perCallUs = (timeMs * 1000.0) / kIterations;

    std::cout << "\n=== Indicator Update (Different Content) ===" << std::endl;
    std::cout << "  Iterations: " << kIterations << std::endl;
    std::cout << "  Total time: " << std::fixed << std::setprecision(2) << timeMs << " ms" << std::endl;
    std::cout << "  Per call:   " << std::fixed << std::setprecision(3) << perCallUs << " us" << std::endl;
    std::cout << "  Frame budget used: " << std::fixed << std::setprecision(4)
              << (perCallUs / 20000.0) * 100 << "% (of 20ms)" << std::endl;

    EXPECT_LT(perCallUs, 100.0) << "Different-content update should be < 100us";
}

// Benchmark: Snapshot generation
TEST_F(HudPerformanceBenchmark, SnapshotGeneration)
{
    constexpr int kIterations = 10000;

    // Add several indicators
    _model->setIndicatorAt("mem", HudTilePosition::TopLeft, HudState::Active, "RAM 3", "", "ram",
                           std::chrono::milliseconds(1200), true);
    _model->setIndicatorAt("rom", HudTilePosition::TopLeft, HudState::Active, "ROM 0", "", "rom",
                           std::chrono::milliseconds(1200), true);
    _model->setIndicatorAt("beeper", HudTilePosition::TopLeft, HudState::Active, "BEEP", "", "speaker",
                           std::chrono::milliseconds(1000), true);

    double timeMs = measureMs([this]() {
        auto snap = _model->snapshot();
        (void)snap; // Prevent optimization
    }, kIterations);

    double perCallUs = (timeMs * 1000.0) / kIterations;

    std::cout << "\n=== Snapshot Read ===" << std::endl;
    std::cout << "  Iterations: " << kIterations << std::endl;
    std::cout << "  Total time: " << std::fixed << std::setprecision(2) << timeMs << " ms" << std::endl;
    std::cout << "  Per call:   " << std::fixed << std::setprecision(3) << perCallUs << " us" << std::endl;

    // Snapshot read should be very fast (atomic load)
    EXPECT_LT(perCallUs, 1.0) << "Snapshot read should be < 1us";
}

// Benchmark: Simulated frame with multiple notifications
TEST_F(HudPerformanceBenchmark, SimulatedFrameWorkload)
{
    constexpr int kFrames = 1000;

    double timeMs = measureMs([this, frame = 0]() mutable {
        // Simulate typical frame with beeper active + occasional page switch
        _model->setIndicatorAt("beeper", HudTilePosition::TopLeft, HudState::Active,
                               "BEEP", "", "speaker", std::chrono::milliseconds(1000), true);

        if (frame % 10 == 0) // Page switch every 10 frames
        {
            char buf[16];
            snprintf(buf, sizeof(buf), "RAM %d", frame % 8);
            _model->setIndicatorAt("mem", HudTilePosition::TopLeft, HudState::Active,
                                   buf, "", "ram", std::chrono::milliseconds(1200), true);
        }

        // Expire check (like animation timer would do)
        _model->expire(HudClock::now());

        // Snapshot read (like paintEvent would do)
        auto snap = _model->snapshot();
        (void)snap;

        ++frame;
    }, kFrames);

    double perFrameUs = (timeMs * 1000.0) / kFrames;
    double fps50Budget = 20000.0; // 20ms = 20000us

    std::cout << "\n=== Simulated Frame Workload ===" << std::endl;
    std::cout << "  Frames: " << kFrames << std::endl;
    std::cout << "  Total time: " << std::fixed << std::setprecision(2) << timeMs << " ms" << std::endl;
    std::cout << "  Per frame:  " << std::fixed << std::setprecision(3) << perFrameUs << " us" << std::endl;
    std::cout << "  Frame budget used: " << std::fixed << std::setprecision(4)
              << (perFrameUs / fps50Budget) * 100 << "% (of 20ms @ 50fps)" << std::endl;

    // HUD should use < 1% of frame budget
    EXPECT_LT(perFrameUs / fps50Budget * 100, 1.0) << "HUD should use < 1% of frame budget";
}

// Summary output
TEST_F(HudPerformanceBenchmark, PrintSummary)
{
    std::cout << "\n========================================" << std::endl;
    std::cout << "HUD Performance Benchmark Summary" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "Target: < 1% of 20ms frame budget (< 200us/frame)" << std::endl;
    std::cout << "========================================\n" << std::endl;
}
