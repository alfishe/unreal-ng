#pragma once

/// @file im2daisychain.h
/// @brief The Z80 family's interrupt daisy chain on the machine's bus: peripherals that put their own IM 2 vector on
/// the data bus in the acknowledge cycle and decode RETI (a Z80 PIO on a slot card, the AMX mouse interface).
///
/// The chain (Zilog UM0081, "Interrupt Servicing"; MAME z80daisy.cpp) runs from the device nearest the CPU (IEI
/// tied high) down; each device passes IEI on as IEO while nothing in it is under service. A device whose interrupt
/// is pending (enabled) with IEI high pulls the shared /INT low (open drain: wired-OR with the ULA's frame pulse and
/// the other device lines). In the acknowledge cycle (M1 with IORQ) the highest requesting device drives its vector
/// - in IM 2 the low byte of the table address, the I register the high byte - instead of the idle bus's #FF, and its
/// pending interrupt becomes "under service", which holds every device below it off until it decodes RETI (ED 4D) on
/// the data bus while its IEI is high. The order is the slots' order (the first card built is nearest the CPU).
///
/// Cost. A machine without such a device has an empty chain: nothing is asked on any path. A device on the chain
/// costs the CPU nothing while it is idle: the chain holds the CPU's device INT line kDeviceIntIm2Chain (the per-step
/// work bit) only while some device is armed - it may raise /INT without a CPU access (an enabled interrupt pending,
/// a timed event such as the AMX mouse's next strobe). While armed, the step asks IsIntAsserted at every instruction
/// boundary; the acknowledge asks Acknowledge, op ED 4D calls Reti. Each call first lets the devices run their timed
/// events up to the boundary's T-state, so a strobe due at T lands at the first boundary at or after T.
///
/// Worked example (AMX mouse, one mickey right): the card's PIO port A strobes, IP set with the enable on: the chain
/// is armed, the CPU (EI, IM 2, I = #FE) takes the INT at the next boundary; Acknowledge returns port A's vector #00,
/// so the CPU reads its handler address from #FE00. The handler reads #1F (the direction), ends with EI : RETI; Reti
/// ends port A's service and the chain is disarmed until the next strobe.

#include <cstdint>
#include <vector>

class Z80;

/// One device on the daisy chain
class IIm2DaisyDevice
{
public:
    static constexpr uint8_t kDaisyInt = 0x01;  ///< requests an interrupt (with IEI high)
    static constexpr uint8_t kDaisyIeo = 0x02;  ///< something in it is under service: IEO low, the devices below wait

    virtual ~IIm2DaisyDevice() = default;
    /// Run the device's timed events up to frame T-state `t` (Z80::t), then its kDaisyInt / kDaisyIeo
    virtual uint8_t DaisyState(uint32_t t) = 0;
    /// The acknowledge cycle; called only right after DaisyState returned kDaisyInt: the vector it drives
    virtual uint8_t DaisyAcknowledge() = 0;
    /// RETI decoded with IEI high (it is the highest device under service)
    virtual void DaisyReti() = 0;
    /// The device may assert /INT without a CPU access: an enabled interrupt pending or a timed event scheduled
    virtual bool DaisyArmed() const = 0;
};

class Im2DaisyChain
{
public:
    explicit Im2DaisyChain(Z80& cpu) : _cpu(cpu)
    {
    }
    Im2DaisyChain(const Im2DaisyChain&) = delete;
    Im2DaisyChain& operator=(const Im2DaisyChain&) = delete;

    /// A device joins at the bottom of the chain (lowest priority so far); it leaves before it goes away
    void Add(IIm2DaisyDevice* device);
    void Remove(IIm2DaisyDevice* device);
    bool Empty() const
    {
        return _devices.empty();
    }

    /// The chain pulls /INT at this boundary (IEI high at the top)
    bool IsIntAsserted(uint32_t t);
    /// The acknowledge cycle at `t`: true when a chain device drove the bus, with its vector
    bool Acknowledge(uint32_t t, uint8_t& vector);
    /// RETI (ED 4D) fetched at `t`: the highest device under service ends its service
    void Reti(uint32_t t);
    /// A device's state changed outside the calls above (its ports, its input, a TTD restore, a reset): the CPU's
    /// device line follows DaisyArmed of the devices
    void Update();

private:
    Z80& _cpu;
    std::vector<IIm2DaisyDevice*> _devices;  ///< highest priority first
};
