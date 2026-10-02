#pragma once

/// @file ps2keyboardstream.h
/// @brief A PC AT (PS/2) keyboard as the byte stream on its wire: physical key
/// events become scan code set 2 bytes that arrive one by one in emulated time.
///
/// For boards that receive the keyboard with a plain serial receiver (the
/// Sprinter: the keyboard's clock and data lines go to the Z84C15 SIO channel A
/// and the PLD; nothing can hold the keyboard off), not for boards with a
/// keyboard controller that runs its own firmware (ATM Turbo 2+: Atm2Kbc
/// models the wire bit by bit for its MCU).
///
/// Model:
///   - a key change queues its set-2 bytes (pckey::Ps2Set2Bytes); a key already
///     in the state the event asks for queues nothing (Pause excepted: it has
///     no break code);
///   - a byte takes one 11-bit frame (start, 8 data, parity, stop) at a 12 kHz
///     keyboard clock, kByteMicros; bytes follow each other without a gap. A
///     byte is delivered to the sink at the end of its frame;
///   - typematic: the last key made repeats its make bytes while held, after
///     500 ms, 10.9 times per second (the AT keyboard's power-on defaults);
///   - the keyboard's own buffer holds kQueueSize bytes; an overflow drops the
///     event and queues the overflow code #00 once there is room;
///   - commands from the host (typematic rate, LEDs) are not modeled.
///
/// Time is the caller's clock in base T-states (SetClock). Nothing runs by
/// itself: Advance(now) delivers every byte due by `now`, in order - a board
/// calls it before the receiver is read (lazy) or after every instruction while
/// Busy() when an arrival must raise an interrupt on time.
///
/// Worked example (3.5 MHz base clock, byte = 3 210 T): Up pressed at T = 1 000
/// queues E0 75; E0 arrives at 4 210, 75 at 7 420. Released at 100 000: E0 F0 75
/// at 103 210, 106 420, 109 630. Held from 1 000 to 2 000 000: the make bytes
/// again at 1 751 000 (+ 1 750 000 = 500 ms) and every 321 101 T after.
///
/// Called on the thread executing the machine (the journaled input path), so
/// record and replay see the same stream.

#include <cstdint>
#include <functional>
#include <type_traits>

#include "emulator/io/keyboard/pckey.h"

class Ps2KeyboardStream : public IPs2KeySink
{
public:
    static constexpr uint32_t kByteMicros = 917;               ///< 11 bits at 12 kHz
    static constexpr uint32_t kTypematicDelayMicros = 500000;  ///< AT power-on default
    static constexpr uint32_t kTypematicPeriodMicros = 91743;  ///< 10.9 characters per second
    static constexpr uint8_t kQueueSize = 16;                  ///< an AT keyboard's buffer
    static constexpr uint8_t kOverflowCode = 0x00;             ///< set 2 "buffer overrun"

    /// Everything that runs (fixed layout for a TTD blob)
    struct State
    {
        uint8_t queue[kQueueSize];
        uint8_t head;
        uint8_t count;
        uint8_t overflow;    ///< the buffer overflowed: #00 is due once there is room
        uint8_t repeatKey;   ///< PcKey of the typematic key, None = no repeat
        uint8_t held[16];    ///< bitmap of the keys held down, by PcKey
        uint64_t nextByteAt; ///< end of the frame of queue[head]
        uint64_t lastByteAt; ///< end of the last frame sent (the wire is busy until then)
        uint64_t repeatAt;   ///< next typematic make
    };
    static_assert(std::is_trivially_copyable_v<State>, "Ps2KeyboardStream::State must stay a plain blob");

    /// A byte arrived at the receiver: `at` is the end of its frame (<= now)
    using ByteSink = std::function<void(uint8_t value, uint64_t at)>;

    Ps2KeyboardStream();

    /// Base clock in Hz (the unit of the times); converts the microsecond constants
    void SetBaseClock(uint32_t baseHz);
    /// The current time in base T-states (monotonic between Rebase calls)
    void SetClock(std::function<uint64_t()> now) { _now = std::move(now); }
    void SetByteSink(ByteSink sink) { _sink = std::move(sink); }

    /// region <IPs2KeySink>
    void OnPcKey(PcKey key, bool pressed) override;
    void ReleaseAllPcKeys() override;
    /// endregion </IPs2KeySink>

    /// Deliver every byte due by `now` (and the typematic repeats due by then)
    void Advance(uint64_t now);
    /// Advance to the clock's now
    void Advance();

    /// Bytes on the way or a key repeating: the stream has a future event
    bool Busy() const { return _state.count != 0 || _state.overflow || _state.repeatKey != 0; }
    bool IsHeld(PcKey key) const;
    /// The time of the next delivery or repeat (UINT64_MAX when idle)
    uint64_t NextEventAt() const;

    /// The machine's clock restarted at `now` (machine reset): the key state and
    /// the queued bytes are kept (the keyboard is not reset), the times move to `now`
    void Rebase(uint64_t now);
    /// Power-on: nothing held, nothing queued
    void Clear();

    uint32_t ByteTStates() const { return _byteT; }
    uint32_t TypematicDelayTStates() const { return _delayT; }
    uint32_t TypematicPeriodTStates() const { return _periodT; }

    const State& GetState() const { return _state; }
    void SetState(const State& state) { _state = state; }

private:
    uint64_t Now() const { return _now ? _now() : 0; }
    /// Queue `bytes` sent from time `at` on
    void Enqueue(const std::vector<uint8_t>& bytes, uint64_t at);
    void Push(uint8_t value, uint64_t at);

    State _state{};
    uint32_t _byteT = 0;
    uint32_t _delayT = 0;
    uint32_t _periodT = 0;
    std::function<uint64_t()> _now;
    ByteSink _sink;
};
