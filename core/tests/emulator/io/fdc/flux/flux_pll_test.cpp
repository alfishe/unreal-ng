#include <gtest/gtest.h>
#include <emulator/io/fdc/flux/flux_pll.h>
#include <vector>
#include <cmath>
#include <random>

using namespace fdc::flux;

class FluxPll_Test : public ::testing::Test
{
protected:
    FluxPll pll{FluxPll::Mode::MFM};

    // Add jitter to intervals
    std::vector<uint32_t> addJitter(const std::vector<uint32_t>& intervals, int percentJitter)
    {
        std::mt19937 rng(42);  // Fixed seed for reproducibility
        std::vector<uint32_t> jittered;

        for (uint32_t interval : intervals)
        {
            int maxOffset = static_cast<int>(interval * percentJitter / 100);
            std::uniform_int_distribution<int> dist(-maxOffset, maxOffset);
            int32_t jitter = dist(rng);
            jittered.push_back(static_cast<uint32_t>(std::max(500, static_cast<int>(interval) + jitter)));
        }

        return jittered;
    }
};

TEST_F(FluxPll_Test, IdealTiming_MFM_SingleCells)
{
    std::vector<uint32_t> intervals = {2000, 2000, 2000, 2000};
    pll.addIntervals(intervals);

    auto& cells = pll.bitCells();
    ASSERT_EQ(cells.size(), 4);
    for (size_t i = 0; i < 4; i++)
    {
        EXPECT_EQ(cells[i], 1) << "Cell " << i << " should be 1";
    }
}

TEST_F(FluxPll_Test, IdealTiming_MFM_DoubleCell)
{
    std::vector<uint32_t> intervals = {4000, 2000};
    pll.addIntervals(intervals);

    auto& cells = pll.bitCells();
    ASSERT_EQ(cells.size(), 3);
    EXPECT_EQ(cells[0], 1);
    EXPECT_EQ(cells[1], 0);
    EXPECT_EQ(cells[2], 1);
}

TEST_F(FluxPll_Test, IdealTiming_MFM_TripleCell)
{
    std::vector<uint32_t> intervals = {6000, 2000};
    pll.addIntervals(intervals);

    auto& cells = pll.bitCells();
    ASSERT_EQ(cells.size(), 4);
    EXPECT_EQ(cells[0], 1);
    EXPECT_EQ(cells[1], 0);
    EXPECT_EQ(cells[2], 0);
    EXPECT_EQ(cells[3], 1);
}

TEST_F(FluxPll_Test, IdealTiming_FM)
{
    FluxPll fmPll{FluxPll::Mode::FM};

    std::vector<uint32_t> intervals = {4000, 8000, 4000};
    fmPll.addIntervals(intervals);

    auto& cells = fmPll.bitCells();
    ASSERT_EQ(cells.size(), 4);
    EXPECT_EQ(cells[0], 1);
    EXPECT_EQ(cells[1], 1);
    EXPECT_EQ(cells[2], 0);
    EXPECT_EQ(cells[3], 1);
}

TEST_F(FluxPll_Test, JitteredTiming_10Percent)
{
    std::vector<uint32_t> idealIntervals = {4000, 4000, 4000, 4000};
    auto jitteredIntervals = addJitter(idealIntervals, 10);

    pll.addIntervals(jitteredIntervals);

    auto& cells = pll.bitCells();
    ASSERT_EQ(cells.size(), 8);
    for (size_t i = 0; i < 8; i++)
    {
        uint8_t expected = (i % 2 == 0) ? 1 : 0;
        EXPECT_EQ(cells[i], expected) << "Cell " << i << " mismatch with 10% jitter";
    }
}

TEST_F(FluxPll_Test, JitteredTiming_20Percent)
{
    std::vector<uint32_t> idealIntervals = {2000, 4000, 2000, 6000};
    auto jitteredIntervals = addJitter(idealIntervals, 20);

    pll.addIntervals(jitteredIntervals);

    auto& cells = pll.bitCells();
    ASSERT_EQ(cells.size(), 7);
    EXPECT_EQ(cells[0], 1);
    EXPECT_EQ(cells[1], 1);
    EXPECT_EQ(cells[2], 0);
    EXPECT_EQ(cells[3], 1);
    EXPECT_EQ(cells[4], 1);
    EXPECT_EQ(cells[5], 0);
    EXPECT_EQ(cells[6], 0);
}

TEST_F(FluxPll_Test, Reset_ClearsState)
{
    pll.addIntervals({2000, 2000, 2000});
    EXPECT_GT(pll.bitCells().size(), 0);

    pll.reset();
    EXPECT_EQ(pll.bitCells().size(), 0);
}

TEST_F(FluxPll_Test, DetectMode_MFM)
{
    std::vector<uint32_t> mfmIntervals;
    for (int i = 0; i < 100; i++)
    {
        mfmIntervals.push_back(2000);
        mfmIntervals.push_back(4000);
    }

    auto mode = FluxPll::detectMode(mfmIntervals.data(), mfmIntervals.size());
    EXPECT_EQ(mode, FluxPll::Mode::MFM);
}

TEST_F(FluxPll_Test, DetectMode_FM)
{
    std::vector<uint32_t> fmIntervals;
    for (int i = 0; i < 100; i++)
    {
        fmIntervals.push_back(4000);
        fmIntervals.push_back(8000);
    }

    auto mode = FluxPll::detectMode(fmIntervals.data(), fmIntervals.size());
    EXPECT_EQ(mode, FluxPll::Mode::FM);
}

TEST_F(FluxPll_Test, LongGap_MaxCells)
{
    std::vector<uint32_t> intervals = {20000};
    pll.addIntervals(intervals);

    auto& cells = pll.bitCells();
    EXPECT_LE(cells.size(), 8);
    EXPECT_EQ(cells[0], 1);
}

TEST_F(FluxPll_Test, ShortPulse_MinOneCell)
{
    std::vector<uint32_t> intervals = {500};
    pll.addIntervals(intervals);

    auto& cells = pll.bitCells();
    EXPECT_GE(cells.size(), 1);
    EXPECT_EQ(cells[0], 1);
}

TEST_F(FluxPll_Test, NominalCellWidth_MFM)
{
    EXPECT_EQ(pll.nominalCellNs(), 2000);
}

TEST_F(FluxPll_Test, NominalCellWidth_FM)
{
    FluxPll fmPll{FluxPll::Mode::FM};
    EXPECT_EQ(fmPll.nominalCellNs(), 4000);
}
