#pragma once

#include <cstdint>
#include <cstddef>
#include <vector>

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
