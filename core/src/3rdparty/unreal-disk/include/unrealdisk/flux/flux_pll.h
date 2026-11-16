#pragma once

#include <cstdint>
#include <cstddef>
#include <vector>

namespace unrealdisk
{

namespace fdc {
namespace flux {

class FluxPll
{
public:
    enum class Mode {
        MFM,
        FM
    };

    explicit FluxPll(Mode mode = Mode::MFM, int gainPercent = 5);

    void reset();
    void addIntervals(const uint32_t* intervalsNs, size_t count);
    void addIntervals(const std::vector<uint32_t>& intervalsNs);
    /// Each interval ends with its transition ("001" for three cells). The time from the last transition to the
    /// index has none: its cells are zeros (at most MAX_CELLS_PER_INTERVAL - 1, the rest of a gap)
    void addTrailingTime(uint32_t ns);

    const std::vector<uint8_t>& bitCells() const { return _bitCells; }
    uint32_t nominalCellNs() const { return _nominalCellNs; }

    static Mode detectMode(const uint32_t* intervalsNs, size_t count);

private:
    void processInterval(uint32_t intervalNs);

    uint32_t _nominalCellNs;
    int _gainPercent;
    uint32_t _currentCellNs;
    int32_t _phaseErrorNs;

    std::vector<uint8_t> _bitCells;
};

} // namespace flux
} // namespace fdc
}  // namespace unrealdisk
