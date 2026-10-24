#pragma once

// The AMX mouse (Advanced Memory Systems, 1985) in a slot: a Z80 PIO interface on the Sinclair edge and a three-button
// quadrature mouse (controller manager retro-devices.md §1, research-retro-hardware.md §2.2). Sources: velesoft's
// redrawn schematic (velesoft.speccy.cz/othermouse/amx_mouse/schematic), the Sinclair Wiki "AMX Mouse" page, Gerton
// Lunter's "Z80" emulator notes (amx_mouse.txt), the z88dk driver (in_MouseAMXInit), AMX Art 1.1 disassembled, Zilog
// UM0081 (the PIO).
//
// The interface (schematic):
//   - PIO /CE = A7, B/A = A5, C/D = A6: the PIO answers every port with A7 = 0 (A0-A4, A8-A15 not decoded). The
//     documented ports are #1F data A (X), #3F data B (Y), #5F control A, #7F control B;
//   - the buttons: a 74LS00 / 74LS32 select on A7 = 1, A6 = 1, A5 = 0 (the documented #DF) with /IORQ and /RD (no M1,
//     so the acknowledge cycle does not see them); a held button pulls its line low through a diode: D7 left, D6
//     middle, D5 right (schematic and Sinclair Wiki; z88dk reads D6 as right and D5 as middle). A released button
//     drives nothing: those bits read the idle bus;
//   - X: phase XA to /ASTB, phase XB to A0; Y: YA to /BSTB, YB to B0; ARDY / BRDY unconnected; the PIO's /INT on
//     the edge's /INT (open drain, wired with the ULA's), IEI tied high, IEO open: the only device of the chain.
//
// What the software does (AMX Art 1.1, AMX Control, z88dk): both ports in mode 1 (input), a vector each (AMX Art #F0
// and #F2 with I = #EE), the interrupt control word #87 (enabled), IM 2; the handlers read the data port, test bit 0
// and end with EI : RETI. X: bit 0 = 1 moves left; Y: bit 0 = 0 moves up (BASIC PLOT coordinates). z88dk takes any
// non-zero byte as the negative direction, so the other lines read 0 (the PIO connector's A1-A7 / B1-B7, the printer
// port of AMX Control, have nothing on them here).
//
// The emulation. One mickey (an emulated pixel of the host motion) is one strobe: the direction on bit 0, /STB low
// then high, so the port's interrupt is pending at once (Z80 PIO mode 1). A strobe is generated only while the port
// has no interrupt pending or under service - the program took the last one (its RETI) - and at most one per
// kStrobeDivider-th of a frame per axis, Lunter's 32 per frame and axis; motion beyond that waits in the pending
// counters (clamped to kMaxPending mickeys), so no host motion is lost while a program reads the mouse. A program that
// leaves the interrupts off gets one pending interrupt per axis and nothing more until it enables them (the hardware
// would lose the later mickeys; the counters keep them). The strobe lands at the first instruction boundary at or
// after its time (Im2DaisyChain::IsIntAsserted runs the card's timed events).
//
// Time: (frame_counter, T-state of the frame), compared lexicographically; the strobe interval is the frame's CPU
// T-states / kStrobeDivider. Reset (bus /RESET): the PIO is reset (the PIO has no /RESET pin; the Spectrum's reset
// line is taken as power-on, as Lunter's emulator does), the pending motion dropped.
//
// Cost: the card is on the CPU's daisy chain (Z80::Im2Chain); the chain holds the CPU's device INT bit only while the
// card is armed (motion pending on a port that can strobe, or an enabled interrupt pending). A machine without the
// card, or with the mouse still, steps as without it.
//
// Input: the card is a mouse of the machine (IMouseSink on MouseManager): its motion and buttons are the mouse input
// kinds of the TTD journal; the connector `amx` (ICard::Connectors) only shows the socket. Time travel: one blob
// (AmxMouseTtd): the PIO, the pending motion, the buttons and the next strobe times.

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include "emulator/cpu/im2daisychain.h"
#include "emulator/io/mouse/imousesink.h"
#include "emulator/io/z80pio/z80pio.h"
#include "emulator/slots/card.h"

class EmulatorContext;
class Z80;
class AmxMouseTtd;

class AmxMouseCard : public ICard, public IMouseSink, public IIm2DaisyDevice
{
public:
    static constexpr const char* kCardId = "amx-mouse";
    static constexpr uint8_t kPortDataX = 0x1F;
    static constexpr uint8_t kPortDataY = 0x3F;
    static constexpr uint8_t kPortControlX = 0x5F;
    static constexpr uint8_t kPortControlY = 0x7F;
    static constexpr uint8_t kPortButtons = 0xDF;
    /// IN #DF bits, active low (schematic)
    static constexpr uint8_t kButtonLeft = 0x80;
    static constexpr uint8_t kButtonMiddle = 0x40;
    static constexpr uint8_t kButtonRight = 0x20;
    /// At most one strobe per axis every frame / kStrobeDivider T-states (Lunter: 32 interrupts per frame and axis)
    static constexpr uint32_t kStrobeDivider = 32;
    static constexpr int kMaxPending = 1024;

    /// CardType::create
    static std::unique_ptr<ICard> Create(const CardContext& context);

    explicit AmxMouseCard(const CardContext& context);
    ~AmxMouseCard() override;

    /// The host buttons (IMouseSink: D0 left, D1 right, D2 middle, active low) as the IN #DF bits pulled low
    static uint8_t ButtonLines(uint8_t activeLowMask);

    /// region <PortDevice>
    uint8_t portDeviceInMethod(uint16_t port) override;
    void portDeviceOutMethod(uint16_t port, uint8_t value) override;
    uint8_t portDeviceReadCycle(uint16_t port, bool& drives) override;
    /// endregion </PortDevice>

    /// region <ICard>
    uint8_t Peek(uint16_t port, bool& drives) const override;
    void BusReset() override;
    void FrameStart() override;
    void Connectors(std::vector<CardConnector>& out) const override;
    void CollectTtdDevices(std::vector<CardTtdDevice>& out) override;
    /// endregion </ICard>

    /// region <IMouseSink>
    bool IsMouseFitted() const override
    {
        return true;
    }
    /// A program has a port in mode 1 with its interrupt enabled
    bool IsMouseInUse() const override;
    void OnMouseMotion(int dx, int dy) override;
    void OnMouseButtons(uint8_t activeLowMask) override;
    void OnMouseWheel(int steps) override
    {
        (void)steps;  // the AMX mouse has no wheel
    }
    MouseDeviceStatus DescribeMouse() const override;
    bool HasUnreadMotion() const override;
    /// endregion </IMouseSink>

    /// region <IIm2DaisyDevice>
    uint8_t DaisyState(uint32_t t) override;
    uint8_t DaisyAcknowledge() override;
    void DaisyReti() override;
    bool DaisyArmed() const override;
    /// endregion </IIm2DaisyDevice>

    Z80Pio& Pio()
    {
        return _pio;
    }
    const Z80Pio& Pio() const
    {
        return _pio;
    }
    int Pending(uint8_t axis) const
    {
        return _pending[axis & 1];
    }
    uint64_t Strobes() const
    {
        return _strobes;
    }

    /// region <Time travel state (AmxMouseTtd)>
    static constexpr size_t kStateSize = 1 + Z80Pio::kStateSize + 2 * 2 + 1 + 2 * (8 + 4);
    void SaveState(uint8_t* dst) const;
    void LoadState(const uint8_t* src);
    /// endregion </Time travel state>

private:
    struct Due
    {
        uint64_t frame = 0;
        uint32_t t = 0;
    };

    /// Join the CPU's daisy chain once the core is reachable (the cards are built before EmulatorContext::pCore is set)
    void Attach();
    /// The CPU's device line follows DaisyArmed
    void UpdateChain();
    /// The frame's CPU T-states (the frame length at the host speed)
    uint32_t FrameT() const;
    /// Strobe the axes whose time has come at frame T-state t
    void RunTo(uint32_t t);
    bool CanStrobe(uint8_t axis) const;

    EmulatorContext* _context;
    Z80* _cpu = nullptr;
    std::unique_ptr<AmxMouseTtd> _ttd;
    Z80Pio _pio;
    int _pending[2] = {0, 0};      ///< mickeys to strobe: X + right, Y + up
    uint8_t _buttons = 0xFF;       ///< IMouseSink mask, active low
    Due _due[2];                   ///< the earliest time of the next strobe per axis
    uint64_t _strobes = 0;         ///< statistics
};
