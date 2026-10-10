#include "hostbusoverlay.h"

#include <numeric>

void HostBusOverlayChain::Assign(HostBusOverlay* const* overlays, size_t count)
{
    _count = count < kMaxOverlays ? count : kMaxOverlays;
    observesReads = false;
    for (size_t i = 0; i < kMaxOverlays; i++)
    {
        _members[i] = i < _count ? overlays[i] : nullptr;
        if (_members[i] && _members[i]->observesReads)
            observesReads = true;
    }
}

uint8_t HostBusOverlayChain::onRead(uint16_t addr, uint8_t normal, bool isExecution, bool romPaged)
{
    uint8_t value = normal;
    for (size_t i = 0; i < _count; i++)
    {
        HostBusOverlay* overlay = _members[i];
        if (overlay->observesReads && addr >= overlay->windowStart && addr < overlay->windowEnd)
            value = overlay->onRead(addr, value, isExecution, romPaged);
    }
    return value;
}

uint8_t HostBusOverlayChain::onReadM1(uint16_t addr, uint8_t normal, bool romPaged)
{
    uint8_t value = normal;
    for (size_t i = 0; i < _count; i++)
    {
        HostBusOverlay* overlay = _members[i];
        if (overlay->observesReads && addr >= overlay->windowStart && addr < overlay->windowEnd)
            value = overlay->onReadM1(addr, value, romPaged);
    }
    return value;
}

void HostBusOverlayChain::onInterruptAcknowledge()
{
    for (size_t i = 0; i < _count; i++)
        _members[i]->onInterruptAcknowledge();
}

void HostBusOverlayChain::onWrite(uint16_t addr, uint8_t value, bool romPaged)
{
    for (size_t i = 0; i < _count; i++)
    {
        HostBusOverlay* overlay = _members[i];
        if (addr >= overlay->windowStart && addr < overlay->windowEnd)
            overlay->onWrite(addr, value, romPaged);
    }
}

bool HostBusOverlayChain::RepeatFetchIsPure(uint16_t addr, uint32_t& period) const
{
    uint32_t combined = 1;
    for (size_t i = 0; i < _count; i++)
    {
        const HostBusOverlay* overlay = _members[i];
        if (!overlay->observesReads || addr < overlay->windowStart || addr >= overlay->windowEnd)
            continue;
        uint32_t memberPeriod = 1;
        if (!overlay->RepeatFetchIsPure(addr, memberPeriod) || memberPeriod == 0)
            return false;
        combined = std::lcm(combined, memberPeriod);
    }
    period = combined;
    return true;
}
