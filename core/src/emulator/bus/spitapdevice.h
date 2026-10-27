#pragma once

/// @file spitapdevice.h
/// @brief An SPI device that reports to a bus tap and passes everything on to the device behind it
/// (docs/inprogress/2026-10-11-bus-analyzer/technical-design.md §4). For a host whose bytes reach the device by
/// more than one path - a master and a DMA that clocks the device directly (NeoGS) - the tap sits in front of the
/// device instead of in every path. The tap sees the byte when the device does: at the byte's completion on a
/// master that delivers lazily.
///
/// A truncated byte (the master restarted before the eighth bit) is the event `truncated` and the finding
/// `early_access`: the access that restarted the master came too early.

#include <cstdint>

#include "emulator/bus/bustap.h"
#include "emulator/io/spi/spidevice.h"

class SpiTapDevice final : public SpiDevice
{
public:
    SpiTapDevice(BusTap& tap, uint8_t slot) : _tap(tap), _slot(slot) {}

    void SetInner(SpiDevice* inner) { _inner = inner; }
    SpiDevice* Inner() const { return _inner; }

    void select(bool selected) override
    {
        if (_inner)
            _inner->select(selected);
        if (_tap.Active()) [[unlikely]]
            _tap.SpiSelect(_slot, selected);
    }

    uint8_t exchange(uint8_t mosi) override
    {
        const uint8_t miso = _inner ? _inner->exchange(mosi) : 0xFF;
        if (_tap.Active()) [[unlikely]]
            _tap.SpiByte(_slot, mosi, miso, false);
        return miso;
    }

    void truncatedByte() override
    {
        if (_inner)
            _inner->truncatedByte();
        if (_tap.Active()) [[unlikely]]
        {
            _tap.SpiTruncated(_slot);
            _tap.Finding(BusRule::EarlyAccess, _slot);
        }
    }

private:
    BusTap& _tap;
    uint8_t _slot;
    SpiDevice* _inner = nullptr;
};
