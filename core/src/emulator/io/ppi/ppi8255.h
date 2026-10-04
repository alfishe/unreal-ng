#pragma once
/// @file ppi8255.h
/// @brief Intel 8255 / KR580VV55 programmable peripheral interface, mode 0.
///
/// Three 8-bit ports (A, B, C as two nibbles) and a control register. The control port takes a mode word
/// (bit 7 = 1: directions; it clears every output latch, as the chip does) or a port C bit set / reset
/// (bit 7 = 0). A port (or C nibble) set as output reads back its own latch; one set as input reads what the
/// board drives on it (an input callback, 0xFF when nothing is connected). Modes 1 and 2 (strobed / bidirectional)
/// are not modeled: no Profi software found uses them. After reset every port is an input (control #9B).
/// Used by the ZX Profi (docs/inprogress/2026-10-04-profi-plus/design.md): Kempston joystick on port A, printer /
/// Covox on B and C, at #1F..#7F and, in the extended port map, at #87..#E7.

#include <cstdint>
#include <functional>

class Ppi8255
{
public:
    /// The chip's registers (the TTD blob, PeripheralId::Ppi8255)
    struct State
    {
        uint8_t control = 0x9B;   ///< the last mode word (bit 7 set); #9B = mode 0, all ports inputs
        uint8_t outA = 0;         ///< output latches
        uint8_t outB = 0;
        uint8_t outC = 0;
    };
    static_assert(sizeof(State) == 4, "Ppi8255::State layout changed");

    using Input = std::function<uint8_t()>;

    /// Register offsets (A1 A0)
    static constexpr uint8_t kPortA = 0;
    static constexpr uint8_t kPortB = 1;
    static constexpr uint8_t kPortC = 2;
    static constexpr uint8_t kControl = 3;

    void Reset() { _state = State(); }

    void Write(uint8_t reg, uint8_t value);
    uint8_t Read(uint8_t reg) const;

    /// What the board drives on an input port (not called for a port set as output)
    void SetInputA(Input input) { _inputA = std::move(input); }
    void SetInputB(Input input) { _inputB = std::move(input); }
    void SetInputC(Input input) { _inputC = std::move(input); }

    bool IsInputA() const { return (_state.control & 0x10) != 0; }
    bool IsInputB() const { return (_state.control & 0x02) != 0; }
    bool IsInputCUpper() const { return (_state.control & 0x08) != 0; }
    bool IsInputCLower() const { return (_state.control & 0x01) != 0; }

    const State& GetState() const { return _state; }
    void SetState(const State& state) { _state = state; }

private:
    State _state;
    Input _inputA;
    Input _inputB;
    Input _inputC;
};
