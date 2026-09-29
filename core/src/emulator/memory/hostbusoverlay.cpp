#include "hostbusoverlay.h"

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

void HostBusOverlayChain::onWrite(uint16_t addr, uint8_t value, bool romPaged)
{
    for (size_t i = 0; i < _count; i++)
    {
        HostBusOverlay* overlay = _members[i];
        if (addr >= overlay->windowStart && addr < overlay->windowEnd)
            overlay->onWrite(addr, value, romPaged);
    }
}
