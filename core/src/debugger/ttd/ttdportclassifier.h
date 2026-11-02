#pragma once

/// @file ttdportclassifier.h
/// @brief Which port map device a port belongs to, for the TTD queries that name devices (the timeline summary, the
/// cause chain). Built once per query from the machine's port decoder; a lookup is memoised per port.

#include <bit>
#include <cstdint>
#include <string>
#include <vector>

#include "debugger/ttd/ttdtimeline.h"
#include "emulator/ports/portdecoder.h"

namespace ttd
{

/// The port map's devices (rows grouped by name) and which one a port belongs to: the matching row with the most
/// decoded address bits, so a narrow decode wins over a partial one that overlaps it
class PortClassifier
{
public:
    static constexpr int16_t kNone = -1;

    PortClassifier(const PortDecoder* decoder, std::vector<TTDTimelineDevice>& devices) : _memo(65536, kUnknown)
    {
        if (!decoder)
            return;
        for (const PortMapEntry& e : decoder->getPortMapEntries())
        {
            const std::string name = e.device ? e.device : "";
            size_t d = 0;
            while (d < devices.size() && devices[d].name != name)
                ++d;
            if (d == devices.size())
                devices.push_back({name, 0});
            devices[d].tags |= e.tags;
            _rows.push_back({e.mask, e.match, static_cast<int16_t>(d), e.latch == PagingLatch::PFE});
        }
    }

    /// Device index of @p port, or kNone
    int16_t DeviceOf(uint16_t port)
    {
        int16_t& m = _memo[port];
        if (m == kUnknown)
        {
            m = kNone;
            int best = -1;
            for (const Row& r : _rows)
                if ((port & r.mask) == r.match && std::popcount(r.mask) > best)
                {
                    best = std::popcount(r.mask);
                    m = r.device;
                }
        }
        return m;
    }

    /// The ULA's #FE (keyboard, EAR, border, beeper): the device of the row bound to that latch
    int16_t UlaDevice() const
    {
        for (const Row& r : _rows)
            if (r.ula)
                return r.device;
        return kNone;
    }

private:
    static constexpr int16_t kUnknown = -2;
    struct Row
    {
        uint16_t mask;
        uint16_t match;
        int16_t device;
        bool ula;
    };
    std::vector<Row> _rows;
    std::vector<int16_t> _memo;
};

}  // namespace ttd
