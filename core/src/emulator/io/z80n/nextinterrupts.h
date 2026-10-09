#pragma once

/// @file nextinterrupts.h
/// @brief The Next's /INT: the ULA frame interrupt, the line interrupt and the 14-source hardware IM2 controller
/// (research-fpga-vhdl.md section 12, design-peripherals.md section 2).
///
/// Two modes (NR #C0 bit 0): pulse mode, the classic ULA-style INT of intlen T at the frame position (reset
/// default); hardware IM2 mode, where every source latches a request that stays until the CPU acknowledges it, the
/// vector is NR #C0 bits 7:5 + the source number x 2, and RETI ends the service of the highest-priority source in
/// service (the daisy chain, bit 0 highest). Sources: 0 line, 1 UART0 RX, 2 UART1 RX, 3-10 CTC 0-7, 11 ULA,
/// 12 UART0 TX, 13 UART1 TX.

#include <cstdint>
#include <functional>

#include "emulator/cpu/z80.h"
#include "emulator/io/z80n/z80nengine.h"
#include "emulator/io/z80n/nexttiming.h"

class EmulatorContext;

class NextInterruptSource final : public IInterruptSource, public INmiReturnStore
{
public:
    /// NR #C0 bit 3 changed (also at a reset: off): the engine switches the stackless NMI
    std::function<void(bool)> onStacklessNmi;
    void StoreNmiReturn(uint8_t low, uint8_t high) override
    {
        _nmiReturn[0] = low;
        _nmiReturn[1] = high;
    }
    uint16_t LoadNmiReturn() const override { return static_cast<uint16_t>(_nmiReturn[0] | (_nmiReturn[1] << 8)); }

    enum Source : uint8_t
    {
        kLine = 0,
        kUart0Rx = 1,
        kUart1Rx = 2,
        kCtc0 = 3,  ///< .. kCtc0 + 7
        kUla = 11,
        kUart0Tx = 12,
        kUart1Tx = 13,
        kSourceCount = 14
    };

    static constexpr uint8_t kRegLineControl = 0x22;
    static constexpr uint8_t kRegLineValue = 0x23;
    static constexpr uint8_t kRegControl = 0xC0;
    static constexpr uint8_t kRegNmiReturnLow = 0xC2;
    static constexpr uint8_t kRegNmiReturnHigh = 0xC3;
    static constexpr uint8_t kRegEnable0 = 0xC4;
    static constexpr uint8_t kRegEnableCtc = 0xC5;
    static constexpr uint8_t kRegEnableUart = 0xC6;
    static constexpr uint8_t kRegStatus0 = 0xC8;
    static constexpr uint8_t kRegStatusCtc = 0xC9;
    static constexpr uint8_t kRegStatusUart = 0xCA;
    static constexpr uint8_t kRegDma0 = 0xCC;
    static constexpr uint8_t kRegDma2 = 0xCE;

    explicit NextInterruptSource(EmulatorContext* context);

    /// The frame geometry and the ULA interrupt position of the frame family in effect (NextTiming)
    void SetGeometry(const NextTiming& timing);
    void Reset();
    /// Called before the interrupt lines are looked at: devices that run on their own clock (the CTC) catch up
    void SetPoller(std::function<void()> poller) { _poller = std::move(poller); }

    /// region <NextREG>
    bool WriteNr(uint8_t reg, uint8_t value);
    bool ReadNr(uint8_t reg, uint8_t& value) const;
    /// endregion

    /// A device (CTC channel, UART) requests an interrupt. Ignored while its enable bit is clear
    void Raise(Source source);

    /// The counter line the beam is on now (NR #1E / #1F): the frame position from the ULA interrupt's place
    uint16_t CurrentLine() const;
    /// NR #64: the counter starts at this value at the paper's first line
    void SetCounterOffset(uint8_t offset) { _counterOffset = offset; }

    bool HardwareMode() const { return (_control & 1) != 0; }
    uint16_t PendingMask() const { return _pending; }
    uint16_t InServiceMask() const { return _inService; }
    uint8_t VectorOf(Source source) const { return static_cast<uint8_t>((_control & 0xE0) | (static_cast<unsigned>(source) << 1)); }

    /// region <IInterruptSource>
    bool IsIntAsserted(uint32_t t) override;
    uint8_t AcknowledgeInterrupt(uint32_t t) override;
    void OnReti() override;
    /// endregion

private:
    uint32_t BaseT(uint32_t t) const;
    uint32_t LineStartT() const;
    uint32_t CounterOrigin() const;
    bool Enabled(Source source) const;
    void Latch(uint32_t baseT);
    bool PulseAt(uint32_t baseT, uint32_t start) const;
    uint16_t Blocked() const;

    EmulatorContext* _context;
    NextTiming _timing = {70908, 228, 1845, 36, 1, 128, 64, 136};
    uint8_t _control = 0;
    uint8_t _nmiReturn[2] = {};
    uint8_t _enable0 = 0x81;
    uint8_t _enableCtc = 0;
    uint8_t _enableUart = 0;
    uint8_t _dma[3] = {};
    uint8_t _lineControl = 0;
    uint8_t _lineValue = 0;
    uint8_t _counterOffset = 0;
    uint16_t _pending = 0;
    uint16_t _inService = 0;
    uint32_t _lastT = 0;
    std::function<void()> _poller;
};
