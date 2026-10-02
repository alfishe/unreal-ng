#pragma once

/// @file msserialmouse.h
/// @brief A Microsoft two-button serial mouse as the byte stream on its RS-232
/// line: 1 200 baud, 7 data bits, a 3-byte packet per movement or button change.
///
/// Packet (the Microsoft mouse protocol):
///   byte 1: 1 L R Y7 Y6 X7 X6   (bit 6 set: the start of a packet)
///   byte 2: 0 0 X5 X4 X3 X2 X1 X0
///   byte 3: 0 0 Y5 Y4 Y3 Y2 Y1 Y0
/// X, Y: signed 8-bit movement since the last packet, + = right / down.
///
/// The mouse here is the host mouse the machine already has: the board's mouse
/// counters (Kempston-style: X grows to the right, Y grows upward, buttons
/// active low) sampled through a callback, so the serial view and the Kempston
/// view of the same board stay in step and the input goes through the
/// journaled MouseManager path.
/// A packet starts when the sampled state differs from the state last sent; a
/// move larger than 127 is sent over several packets.
///
/// Time is the caller's clock in base T-states; nothing runs by itself. The
/// board calls Advance(now) before its receiver is read: the mouse looks at the
/// counters then (a packet starts at `now`) and delivers the bytes whose frame
/// has ended. A character takes 9 bits (start, 7 data, stop) at 1 200 baud =
/// 7.5 ms, a packet 22.5 ms.
///
/// Worked example (3.5 MHz base clock, character = 26 250 T): the counters move
/// from X = 31, Y = 85 to X = 36, Y = 88 (5 right, 3 up), left button down,
/// first seen at T = 10 000: dx = 5, dy = -3 (#FD: bits 7-6 = 11, bits 5-0 = #3D)
/// -> #40 | #20 (left) | 11 << 2 = #6C, then #05, #3D; bytes at 36 250, 62 500
/// and 88 750.

#include <cstdint>
#include <functional>
#include <type_traits>

class MsSerialMouse
{
public:
    static constexpr uint32_t kBaud = 1200;
    static constexpr uint32_t kBitsPerCharacter = 9;  ///< start, 7 data, stop

    struct State
    {
        uint8_t packet[3];
        uint8_t sent;          ///< bytes of `packet` delivered (3 = no packet in flight)
        uint8_t lastX;         ///< the counters as last reported
        uint8_t lastY;
        uint8_t lastButtons;   ///< active low, D0 left, D1 right
        uint8_t synced;        ///< lastX / lastY / lastButtons hold a sample
        uint64_t nextByteAt;   ///< end of the frame of packet[sent]
    };
    static_assert(std::is_trivially_copyable_v<State>, "MsSerialMouse::State must stay a plain blob");

    /// Kempston-style counters: X (+ right), Y (+ up), buttons (active low: D0 left, D1 right)
    using Sampler = std::function<void(uint8_t& x, uint8_t& y, uint8_t& buttons)>;
    /// A character arrived at the receiver at `at`
    using ByteSink = std::function<void(uint8_t value, uint64_t at)>;

    MsSerialMouse();

    void SetBaseClock(uint32_t baseHz);
    void SetSampler(Sampler sampler) { _sampler = std::move(sampler); }
    void SetByteSink(ByteSink sink) { _sink = std::move(sink); }

    /// Deliver the characters due by `now`; when no packet is in flight, sample
    /// the mouse and start one at `now` if it moved or a button changed
    void Advance(uint64_t now);

    /// The packet bytes for a move and the buttons (exposed for tests)
    static void BuildPacket(int dx, int dy, bool left, bool right, uint8_t out[3]);

    /// The machine's clock restarted: a packet in flight continues from `now`
    void Rebase(uint64_t now);
    /// Power-on: no packet, the next sample is the reference (sends nothing)
    void Clear();

    uint32_t CharacterTStates() const { return _charT; }
    const State& GetState() const { return _state; }
    void SetState(const State& state) { _state = state; }

private:
    State _state{};
    uint32_t _charT = 0;
    Sampler _sampler;
    ByteSink _sink;
};
