#pragma once

/// @file wildsoundcore.h
/// @brief Robus's Wild Sound card ("AYX-32 / Wild Sound II" firmware, STM32F405) as the host sees it: the AY bus
/// cycles, the register file, the unlock, the identification registers and the upload streams
/// (docs/inprogress/2026-10-09-wildsound/design.md §2-§4; every rule from reference-firmware.md §A-§B).
///
/// The core knows nothing of the board it sits on: the host gives it three bus events (address write, data write,
/// read), each with the host time. AY register writes and the mode switches (AY <-> XM) go into a queue with that
/// time; the card's audio side drains it when it renders a frame.
///
/// **The read pipeline.** The firmware answers a read with a byte it prepared at the end of the previous bus event,
/// then runs the read's side effect and prepares the next one (reference-firmware.md §A.2). Worked example, the
/// status frame: an address write of #F3 snapshots the frame and prepares byte 0; read 1 returns byte 0 and
/// prepares byte 1; ...; read 32 returns the CRC (byte 31) and snapshots the next frame.
///
/// **Streams.** `#F1` takes a command (u16) and a size (u32); `#F2` takes packets (a length byte, then that many
/// bytes into a 127-byte FIFO); a completed packet runs the command's step at once (design §4.4: the card's busy
/// time is not modelled, so the busy bit is never seen set); `#F3` reads the status frame.

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "wildsoundstorage.h"

namespace wildsound
{

/// What the card plays through its DAC: the two AY chips, or the uploaded XM (never both, reference-firmware §D.5)
enum class WildSoundMode : uint8_t
{
    Ay = 0,
    Xm = 1,
};

/// An event for the audio side, at host time `t`
struct WildSoundEvent
{
    enum Kind : uint8_t
    {
        AyWrite = 0,    ///< chip, reg, value
        PlayXm = 1,     ///< DIGPLAY: start the XM from its first order (mode = XM)
        StopXm = 2,     ///< CLEAR / ERASE / RESET: back to the AY chips
        ChipReset = 3,  ///< RESET: both AY chips to their power-on state
    };
    uint64_t t = 0;
    Kind kind = AyWrite;
    uint8_t chip = 0;
    uint8_t reg = 0;
    uint8_t value = 0;
};

class WildSoundCore
{
public:
    /// Stream commands (reference-firmware.md §B.2)
    static constexpr uint16_t kCmdNop = 0x0000;
    static constexpr uint16_t kCmdClear = 0x0001;
    static constexpr uint16_t kCmdErase = 0x0002;
    static constexpr uint16_t kCmdXmAStart = 0x0010;  ///< track pack
    static constexpr uint16_t kCmdXmAData = 0x0011;   ///< no-op
    static constexpr uint16_t kCmdXmBStart = 0x0020;  ///< instrument table
    static constexpr uint16_t kCmdXmBData = 0x0021;   ///< sample bytes
    static constexpr uint16_t kCmdDigPlay = 0x0030;

    static constexpr size_t kFifoCapacity = 127;
    static constexpr size_t kWorkSize = 32;
    static constexpr uint32_t kCoreClockHz = 220000000;  ///< #EF: SYSCLK of the shipped build
    static constexpr const char* kLockString = "Wild Sound Enable ";
    static constexpr const char* kVersionString = "165931.23042021";
    static constexpr const char* kCopyrightString = "Wild Sound II By Rob F.";
    static constexpr const char* kBuildString = "16:59:31 23.04.2021";

    WildSoundCore();

    /// Power-on or RESET: the firmware restarts (registers, unlock, streams, storage allocation; the flash bytes stay)
    void Reset(uint64_t t);

    /// region <Bus>
    void WriteAddress(uint8_t value, uint64_t t);
    void WriteData(uint8_t value, uint64_t t);
    uint8_t Read(uint64_t t);
    /// The byte the next read returns, without the read's side effect (debugger)
    uint8_t Peek() const { return _latch; }
    /// endregion </Bus>

    /// Milliseconds since the card's reset, for #EB (the host knows the absolute time; `t` is frame-relative)
    void SetUptimeMs(uint32_t ms) { _uptimeMs = ms; }

    /// region <Audio side>
    /// The events since the last call, oldest first
    std::vector<WildSoundEvent>& Events() { return _events; }
    WildSoundMode Mode() const { return _mode; }
    const WildSoundStorage& Storage() const { return _storage; }
    WildSoundStorage& Storage() { return _storage; }
    /// The AY registers as the card holds them (raw bytes, chip 0 / 1, 0..#1F)
    uint8_t ChipRegister(int chip, int reg) const { return _chips[chip & 1].r[reg & 0x1F]; }
    /// endregion </Audio side>

    /// region <Report>
    int SelectedChip() const { return _chip; }
    uint8_t SelectedRegister() const { return _chips[_chip].addr; }
    bool Unlocked() const { return _lockBit == 0; }
    uint16_t Command() const { return _cmd; }
    uint32_t StreamSize() const { return _size; }
    uint32_t StreamPos() const { return _pos; }
    size_t FifoSize() const { return _fifoCount; }
    bool Busy() const { return _busy; }
    const std::string& LastError() const { return _lastError; }
    uint64_t Commands() const { return _commandsRun; }
    /// endregion </Report>

    /// region <State>
    /// Everything but the flash bytes (WildSoundStorage::Flash: a TTD region) and the event queue (drained per frame)
    static constexpr size_t kStateSize = 640;
    void SaveState(uint8_t* dst) const;
    void LoadState(const uint8_t* src);
    /// endregion </State>

private:
    struct Chip
    {
        std::array<uint8_t, 32> r{};  ///< #00-#0F AY, #10-#1F the card's extension registers
        uint8_t addr = 0;
    };

    /// The byte a read at register `reg` would return now (chip registers below #20, the shared area above)
    uint8_t Lookup(uint8_t reg) const;
    void PushEvent(const WildSoundEvent& e);
    static constexpr size_t kMaxEvents = 65536;
    void SpecialAddress(uint8_t reg);
    void SpecialData(uint8_t reg, uint8_t value, uint64_t t);
    void SpecialRead(uint8_t reg);

    uint8_t ReadStopString(const char* s);
    void SnapshotStatus();
    uint8_t NextStatusByte();
    static uint8_t CrcStep(uint8_t b, uint8_t r, uint8_t p);

    void StreamClear();
    void PacketDone(uint64_t t);
    void RunStep(uint64_t t);
    void Fail(const std::string& why);
    void SetMode(WildSoundMode mode, uint64_t t);

    std::array<Chip, 2> _chips{};
    int _chip = 0;
    std::array<uint8_t, 256> _ws{};  ///< the shared register area (echo store and the special registers' bytes)
    uint8_t _latch = 0xFF;           ///< what the next read returns

    // The firmware's WORK_* fields
    uint8_t _lockBit = 0xFF;   ///< #FF locked, #FE waiting for the zero byte, 0 unlocked
    uint8_t _workMode = 0xFF;
    uint8_t _workCmd = 0xFF;
    uint16_t _workPos = 0;     ///< string / buffer index; for the status frame low byte = index, high byte = CRC
    uint8_t _workStatus = 0x80;
    uint8_t _workError = 0;
    uint16_t _workFwSize = 0;  ///< also the #F2 packet's remaining length
    std::array<uint8_t, kWorkSize> _work{};

    // The stream engine
    uint16_t _cmd = 0;
    uint32_t _size = 0;
    uint32_t _pos = 0;
    bool _busy = false;
    bool _started = false;  ///< the command's first data step ran (XMAST / XMBST allocate there)
    std::array<uint8_t, 128> _fifo{};
    size_t _fifoCount = 0;

    WildSoundMode _mode = WildSoundMode::Ay;
    WildSoundStorage _storage;
    std::vector<WildSoundEvent> _events;
    std::string _lastError;
    uint64_t _commandsRun = 0;

    uint32_t _uptimeMs = 0;  ///< set by the host before each bus event (not state)
};

}  // namespace wildsound
