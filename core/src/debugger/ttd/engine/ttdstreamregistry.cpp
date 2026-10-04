#include "ttdstreamregistry.h"

namespace ttd
{

bool TTDStreamRegistry::Register(uint32_t id, const std::string& name, CaptureFn capture)
{
    if (id >= kMaxStreams || !capture)
        return false;
    if (_streams.size() <= id)
        _streams.resize(id + 1);
    if (_streams[id].capture)
        return false;
    _streams[id] = {name, std::move(capture)};
    return true;
}

bool TTDStreamRegistry::SetEnabled(uint32_t id, bool on)
{
    if (!IsRegistered(id))
        return false;
    const uint64_t bit = uint64_t(1) << id;
    _enabledMask = on ? (_enabledMask | bit) : (_enabledMask & ~bit);
    return true;
}

void TTDStreamRegistry::CaptureEnabledSlow(const TTDPosition& at)
{
    for (uint64_t mask = _enabledMask; mask != 0; mask &= mask - 1)
    {
        uint32_t id = 0;
        while (((mask >> id) & 1u) == 0)
            ++id;
        _captureCalls++;
        _streams[id].capture(at);
    }
}

}  // namespace ttd
