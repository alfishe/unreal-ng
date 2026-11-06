#pragma once

/// @file pic8259.h
/// @brief Intel 8259A programmable interrupt controller (Intel datasheet 231468), single chip, no cascade. Shared by
/// every machine that wires one; the machine keeps how the Z80 reaches it and what drives its IR inputs.
///
/// Users:
///   - Scorpion SMUC: #7EBE (A0 = 0) / #7FBE (A0 = 1); IR0 = the DS1685's /IRQ, INT to the Z80 through #FFBA D3, the
///     vector read in the Z80's IM 2 acknowledge (ports/models/smucinterrupts.h)
///
/// Programming (datasheet "Programming the 8259A"):
///   - ICW1 (A0 = 0, D4 = 1): starts the init sequence: clears IMR and ISR, IR7 lowest priority, read IRR next,
///     the edge detectors re-armed. D0 IC4 (ICW4 follows), D1 SNGL (no ICW3), D3 LTIM (level triggered)
///   - ICW2, ICW3 (unless SNGL), ICW4 (if IC4) at A0 = 1, in that order. ICW4 D0 uPM (8086 vectors), D1 AEOI
///   - after the sequence: OCW1 (A0 = 1) = IMR; OCW2 (A0 = 0, D4 D3 = 00) = EOI / rotation; OCW3 (A0 = 0,
///     D4 D3 = 01) = read IRR / ISR (D1 RR, D0 RIS), poll (D2), special mask (D6 ESMM, D5 SMM)
///   - read A0 = 1: IMR; A0 = 0: IRR or ISR as OCW3 chose, or the poll word after an OCW3 poll
///
/// The vector. A Z80 acknowledge is one cycle, the 8259A's 8080 sequence is three (CALL, low, high) and its 8086 one
/// two: the bridge's byte is not documented. The firmware the SMUC serves expects the 8086 form for both modes,
/// ICW2 T7-T3 with the level in D2-D0: the ProfROM and GMX 6.44 interrupt tests program ICW1 #1A (8080 mode, no ICW4)
/// and ICW2 #F8, unmask IR0 only, set I = 0 and keep handlers for the vectors #C8, #D0 ... #F8 at #00C8-#00FF; IR0
/// gives #F8, the word at #00F8 points at the handler. Worked example: after ICW1 #1A, ICW2 #F8, OCW1 #FE and the
/// RTC's IRQ on IR0, INT rises; the acknowledge returns #F8 and sets ISR bit 0; the handler's OUT #7EBE, #20 (a
/// non-specific EOI) clears it.
///
/// Simplifications: no cascade (ICW3 stored only), no special fully nested mode (ICW4 SFNM stored only).

#include <cstdint>

class Pic8259
{
public:
    /// The chip's registers and sequence position: fixed layout, no padding (TTD blobs copy it byte-wise)
    struct State
    {
        uint8_t irr;        ///< interrupt request register
        uint8_t isr;        ///< in-service register
        uint8_t imr;        ///< interrupt mask register
        uint8_t icw1;
        uint8_t icw2;
        uint8_t icw3;
        uint8_t icw4;
        uint8_t initStep;   ///< 0 = ready; 2 / 3 / 4 = the ICW the next A0 = 1 write is
        uint8_t lines;      ///< IR input levels (bit = IR pin high), the edge detectors' memory
        uint8_t lowest;     ///< the lowest-priority level (rotation); 7 after ICW1
        uint8_t mode;       ///< bit 0 read ISR (else IRR), 1 poll pending, 2 special mask, 3 rotate in AEOI
        uint8_t reserved;
    };
    static_assert(sizeof(State) == 12, "Pic8259::State layout drift");

    static constexpr uint8_t kModeReadIsr = 0x01;
    static constexpr uint8_t kModePoll = 0x02;
    static constexpr uint8_t kModeSpecialMask = 0x04;
    static constexpr uint8_t kModeRotateAeoi = 0x08;

    Pic8259() { PowerOn(); }

    /// Power-on: the registers hold no defined values on the part; zeroed, waiting for ICW1, every request masked
    void PowerOn();

    /// The Z80's port write and read; `a0` = the chip's A0 pin
    void Write(bool a0, uint8_t value);
    uint8_t Read(bool a0);
    /// A read without side effects (no poll acknowledge), for reports
    uint8_t Peek(bool a0) const;

    /// An IR pin's level (true = high = requesting); edge mode latches the rising edge into IRR
    void SetInput(uint8_t level, bool high);

    /// The INT pin: an unmasked request of higher priority than every level in service
    bool IntAsserted() const { return PendingLevel() >= 0; }
    /// The acknowledge: the vector byte the Z80 reads; the level goes in service (unless AEOI)
    uint8_t Acknowledge();
    /// A request is unmasked (in IRR or held high on its pin): INT may rise without a CPU access
    bool HasUnmaskedRequest() const { return ((_s.irr | _s.lines) & static_cast<uint8_t>(~_s.imr)) != 0; }
    bool Initialized() const { return _s.initStep == 0 && _s.icw1 != 0; }

    const State& GetState() const { return _s; }
    void SetState(const State& state) { _s = state; }

private:
    /// The level INT asserts for, or -1
    int PendingLevel() const;
    /// Levels from the highest priority down
    uint8_t LevelAt(uint8_t rank) const { return static_cast<uint8_t>((_s.lowest + 1 + rank) & 7); }
    /// The highest-priority level in service, or -1
    int HighestInService() const;
    void Eoi(uint8_t level);
    void Ocw2(uint8_t value);
    bool LevelTriggered() const { return (_s.icw1 & 0x08) != 0; }
    bool AutoEoi() const { return (_s.icw4 & 0x02) != 0; }

    State _s{};
};
