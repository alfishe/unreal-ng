#include "unrealdisk/flux/flux_pll.h"
#include <algorithm>
#include <cmath>

namespace unrealdisk
{

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

    // An interval runs from one transition to the next: the cells before it, then the cell of the transition
    for (int i = 1; i < cellsInInterval; ++i)
    {
        _bitCells.push_back(0);
    }
    _bitCells.push_back(1);

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

/// Double density at 300 rpm: MFM's 2 us cells give flux intervals of 4, 6 and 8 us (2, 3, 4 cells); FM's 4 us cells
/// give 4 and 8 us only (a clock pulse, with or without a data pulse between). The 6 us interval is MFM's own: a track
/// with a real share of them (gaps of 4E, any data) is MFM, one without is FM. Mostly intervals under 3.5 us: high
/// density, MFM
void FluxPll::addTrailingTime(uint32_t ns)
{
    int cells = static_cast<int>(std::round(static_cast<double>(ns) / static_cast<double>(_currentCellNs)));
    cells = std::clamp(cells, 0, MAX_CELLS_PER_INTERVAL - 1);
    _bitCells.insert(_bitCells.end(), static_cast<size_t>(cells), uint8_t{0});
}

FluxPll::Mode FluxPll::detectMode(const uint32_t* intervalsNs, size_t count)
{
    size_t fast = 0;         // under 3.5 us: high density MFM (2 / 3 / 4 us)
    size_t shortOrLong = 0;  // ~4 us and ~8 us: both encodings
    size_t middle = 0;       // ~6 us: MFM only
    for (size_t i = 0; i < count; ++i)
    {
        const uint32_t ns = intervalsNs[i];
        if (ns >= 1000 && ns < 3500)
            fast++;
        else if (ns >= 5000 && ns < 7000)
            middle++;
        else if ((ns >= 3500 && ns < 5000) || (ns >= 7000 && ns < 9000))
            shortOrLong++;
    }
    if (middle + shortOrLong == 0 || fast > middle + shortOrLong)
        return Mode::MFM;  // nothing double density to go by, or high density flux
    return (middle * 20 >= middle + shortOrLong) ? Mode::MFM : Mode::FM;
}

} // namespace flux
} // namespace fdc
}  // namespace unrealdisk
