#pragma once

/// @file isabusdevicecard.h
/// @brief A shared bus device (IIoBusDevice: the NE2000, later the 16550 cards, the 3C509B) in one of the
/// Sprinter's ISA slots (network tdd §5.1): the one piece of per-bus code. It forwards I/O cycles with the 20-bit ISA
/// address to the device's decoder, maps RESET DRV to the device's Reset, ignores AEN cycles (an I/O card does, unless
/// its decoder leaves AEN out: IgnoresAen) and
/// reports the device's resources. The device is owned by NetworkManager; this wrapper only points at it.

#include "emulator/io/iiobusdevice.h"
#include "emulator/io/sprinter/isa/iisacard.h"

namespace sprinterisa
{

class IsaBusDeviceCard final : public IIsaCard
{
public:
    explicit IsaBusDeviceCard(IIoBusDevice& device) : _device(device) {}
    ~IsaBusDeviceCard() override { _device.SetIrqListener(nullptr); }   // the device outlives the wrapper
    IsaBusDeviceCard(const IsaBusDeviceCard&) = delete;
    IsaBusDeviceCard& operator=(const IsaBusDeviceCard&) = delete;

    IIoBusDevice& Device() { return _device; }

    const char* Kind() const override { return _device.Kind(); }
    bool IoRead(const IsaCycle& cycle, uint8_t& value) override
    {
        uint16_t offset = 0;
        if ((cycle.aen && !_device.IgnoresAen()) || !_device.Decodes(cycle.address, offset))
            return false;
        value = _device.Read(offset);
        return true;
    }
    bool IoWrite(const IsaCycle& cycle, uint8_t value) override
    {
        uint16_t offset = 0;
        if ((cycle.aen && !_device.IgnoresAen()) || !_device.Decodes(cycle.address, offset))
            return false;
        _device.Write(offset, value);
        return true;
    }
    bool IoPeek(uint32_t address, uint8_t& value) const override
    {
        uint16_t offset = 0;
        if (!_device.Decodes(address, offset))
            return false;
        value = _device.Peek(offset);
        return true;
    }
    void SetReset(bool asserted) override
    {
        if (asserted)
            _device.Reset();
    }
    bool Irq() const override { return _device.Irq(); }
    bool IrqDriven() const override { return _device.IrqDriven(); }
    uint64_t NextIrqEventAt() const override { return _device.NextIrqEventAt(); }
    void CatchUp() override { _device.CatchUp(); }
    void SetLinesListener(std::function<void()> changed) override { _device.SetIrqListener(std::move(changed)); }
    std::string IrqCause() const override { return _device.IrqCause(); }
    bool IoRange(uint32_t& first, uint32_t& last) const override { return _device.IoRange(first, last); }
    int IrqLine() const override { return _device.IrqLine(); }
    bool IgnoresAen() const override { return _device.IgnoresAen(); }
    std::string DecodeNote() const override { return _device.DecodeNote(); }
    const char* RegisterName(bool io, uint32_t address, bool write) const override
    {
        uint16_t offset = 0;
        if (!io || !_device.Decodes(address, offset))
            return "";
        return _device.RegisterName(offset, write);
    }
    bool Stalled() const override { return _device.Stalled(); }
    void Describe(StateNode& out) const override { _device.Describe(out); }

private:
    IIoBusDevice& _device;
};

}  // namespace sprinterisa
