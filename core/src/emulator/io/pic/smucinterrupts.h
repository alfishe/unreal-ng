#pragma once

/// @file smucinterrupts.h
/// @brief The SMUC board's interrupt path: the DS1685's /IRQ on the 8259's IR0, the 8259's INT on the Z80's /INT
/// while #FFBA D3 is 1, the 8259's vector in the IM 2 acknowledge (SMUC hardware reference §3, §5.2 D3, §5.4).
///
/// The firmware's test (ProfROM page 7 #16CD-#1737, GMX 6.44 page 23 #20E4-#215A): ICW1 #1A, ICW2 #F8, OCW1 #FE; RTC
/// A = #26, B = #D6 then #56 (PIE, UIE); #FFBA D3 = 1, I = 0, IM 2, EI, HALT; the handler at #0160 (vector #F8) sets
/// its flag and sends EOI #20 to #7EBE. The other IR pins (the ISA slot's IRQ lines) are not connected.
///
/// Cost: the Z80 asks the chain at every instruction boundary only while armed: D3 = 1, the 8259 initialized and a
/// request possible (IR0 unmasked with an interrupt enabled in the clock's B, or one already latched).

#include <cstdint>

#include "emulator/cpu/im2daisychain.h"
#include "emulator/io/pic/pic8259.h"

class Ds12887;
class Z80;

class SmucInterrupts final : public IIm2DaisyDevice
{
public:
    static constexpr uint8_t kIntEnable = 0x08;   ///< #FFBA D3: the 8259's INT reaches the Z80

    SmucInterrupts(Ds12887& rtc, const uint8_t& pFFBA) : _rtc(rtc), _pFFBA(pFFBA) {}
    ~SmucInterrupts() override { Detach(); }
    SmucInterrupts(const SmucInterrupts&) = delete;
    SmucInterrupts& operator=(const SmucInterrupts&) = delete;

    /// Join the CPU's daisy chain (the board is fitted) or leave it
    void Attach(Z80* cpu);
    void Detach();

    Pic8259& Pic() { return _pic; }
    const Pic8259& Pic() const { return _pic; }

    /// #7EBE / #7FBE (A8 = the 8259's A0)
    void Write(uint16_t port, uint8_t value);
    uint8_t Read(uint16_t port);
    /// Something the arming depends on changed: #FFBA, the clock's B, a reset, a TTD restore
    void Update();

    // IIm2DaisyDevice
    uint8_t DaisyState(uint32_t t) override;
    uint8_t DaisyAcknowledge() override { return _pic.Acknowledge(); }
    void DaisyReti() override {}
    bool DaisyArmed() const override;

private:
    /// The clock's /IRQ onto IR0
    void SampleRtc();

    Ds12887& _rtc;
    const uint8_t& _pFFBA;
    Pic8259 _pic;
    Z80* _cpu = nullptr;
};
