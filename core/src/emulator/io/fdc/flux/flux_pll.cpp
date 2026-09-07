#include "flux_pll.h"
#include <algorithm>
#include <cmath>

namespace fdc {
namespace flux {

static constexpr uint32_t MFM_CELL_NS = 2000;
static constexpr uint32_t FM_CELL_NS = 4000;
static constexpr int MIN_CELL_PERCENT = 75;
static constexpr int MAX_CELL_PERCENT = 125;
static constexpr int MAX_CELLS_PER_INTERVAL = 8;

FluxPll::FluxPll(Mode mode, int gainPercent)
    : _nominalCellNs(mode == Mode::MFM ? MFM_CELL_NS : FM_CELL_NS)
    , _gainPercent(gainPercent)
    , _currentCellNs(_nominalCellNs)
    , _phaseErrorNs(0)
{
}

void FluxPll::reset()
{
    _currentCellNs = _nominalCellNs;
    _phaseErrorNs = 0;
    _bitCells.clear();
}

void FluxPll::addIntervals(const uint32_t* intervalsNs, size_t count)
{
    _bitCells.reserve(_bitCells.size() + count * 2);
    for (size_t i = 0; i < count; ++i)
    {
        processInterval(intervalsNs[i]);
    }
}

void FluxPll::addIntervals(const std::vector<uint32_t>& intervalsNs)
{
    addIntervals(intervalsNs.data(), intervalsNs.size());
}

void FluxPll::processInterval(uint32_t intervalNs)
{
    int cellsInInterval = static_cast<int>(std::round(
        static_cast<double>(intervalNs) / static_cast<double>(_currentCellNs)));
    cellsInInterval = std::clamp(cellsInInterval, 1, MAX_CELLS_PER_INTERVAL);

    _bitCells.push_back(1);
    for (int i = 1; i < cellsInInterval; ++i)
    {
        _bitCells.push_back(0);
    }

    int32_t expectedNs = cellsInInterval * static_cast<int32_t>(_currentCellNs);
    int32_t errorNs = static_cast<int32_t>(intervalNs) - expectedNs;
    int32_t adjustment = (errorNs * _gainPercent) / 100;

    uint32_t minCellNs = (_nominalCellNs * MIN_CELL_PERCENT) / 100;
    uint32_t maxCellNs = (_nominalCellNs * MAX_CELL_PERCENT) / 100;

    int32_t newCellNs = static_cast<int32_t>(_currentCellNs) + adjustment;
    _currentCellNs = static_cast<uint32_t>(std::clamp(newCellNs,
                                                       static_cast<int32_t>(minCellNs),
                                                       static_cast<int32_t>(maxCellNs)));
}

FluxPll::Mode FluxPll::detectMode(const uint32_t* intervalsNs, size_t count)
{
    constexpr int BUCKET_SIZE_NS = 500;
    constexpr int NUM_BUCKETS = 20;
    int histogram[NUM_BUCKETS] = {0};

    for (size_t i = 0; i < count; ++i)
    {
        int bucket = static_cast<int>(intervalsNs[i] / BUCKET_SIZE_NS);
        if (bucket >= 0 && bucket < NUM_BUCKETS)
        {
            histogram[bucket]++;
        }
    }

    int mfmScore = histogram[3] + histogram[4] + histogram[5];
    int fmScore = histogram[7] + histogram[8] + histogram[9];

    return (fmScore > mfmScore * 2) ? Mode::FM : Mode::MFM;
}

} // namespace flux
} // namespace fdc
