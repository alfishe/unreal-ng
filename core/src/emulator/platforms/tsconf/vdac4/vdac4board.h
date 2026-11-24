#pragma once

/// @file vdac4board.h
/// @brief The VDAC4 board's ESP32-S3 side as the TS-Conf machine holds it: the ESP device (SPI slot 2), the app
/// runtime on the Xtensa interpreter and the ESP's LINK to the Tang.
///
/// The Tang itself is the port decoder's FT812 card (Vdac4Tang, slot 1); this object exists next to it when
/// [MISC] TS_VDAC4=1. Design: docs/inprogress/2026-11-15-tsconf-vdac4/design.md §4, §6, §8.

#include <cstdint>
#include <deque>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "vdac4control.h"

class EmulatorContext;
class SpiDevice;
class Vdac4Audio;
class Vdac4Esp;
class Vdac4FtWorker;
class Vdac4Tang;

namespace vdac4
{
class EspXtensaRuntime;
}

class Vdac4Board
{
public:
    /// The ESP flash folders of boards with none configured (headless, tests): "<temp>/unreal-ng-vdac4-<pid>-<n>"
    /// (ProcessTempFolder; the start-up cleanup removes those of gone processes, startupcleanup.cpp)
    static constexpr const char* kOwnCachePrefix = "unreal-ng-vdac4-";

    /// `nowNs`: the machine's emulated time; `catchUp`: brings the Tang's clock to the CPU before a LINK access
    Vdac4Board(EmulatorContext* context, Vdac4Tang& tang, std::function<uint64_t()> nowNs,
               std::function<void()> catchUp);
    ~Vdac4Board();

    SpiDevice& Spi();
    Vdac4Esp& Esp() { return *_esp; }
    vdac4::EspXtensaRuntime& Runtime() { return *_runtime; }
    /// The ESP's sound on the mixer ("VDAC4 ESP" row)
    Vdac4Audio& Audio() { return *_audio; }
    Vdac4FtWorker& FtWorker() { return *_ft; }

    /// Machine clock restarts (resets) the ESP's clock stepped over, and the ESP time they took back in total
    uint64_t ClockRestarts() const { return _backSteps; }
    uint64_t ClockOffsetNs() const { return _offsetNs; }

    /// The ESP's monotonic clock (TTD: vdac4-esp)
    struct ClockState
    {
        uint64_t lastNs = 0;
        uint64_t offsetNs = 0;
        uint64_t backSteps = 0;
    };
    ClockState GetClock() const { return {_lastNs, _offsetNs, _backSteps}; }
    void SetClock(const ClockState& clock)
    {
        _lastNs = clock.lastNs;
        _offsetNs = clock.offsetNs;
        _backSteps = clock.backSteps;
    }

    /// Power-on (the board shares the machine's supply)
    void PowerOn();
    /// Once per machine frame: the ESP runs up to now (apps draw and play without the Z80)
    void OnFrameEnd();

    /// The ESP's log (app lines and runtime diagnostics), the last kRecentLogLines
    static constexpr size_t kRecentLogLines = 256;
    /// The last `lines` log lines, oldest first (a copy: any thread)
    std::vector<std::string> RecentLog(size_t lines) const;

    /// The Z80's recent packets to the ESP (diagnostics), oldest first
    struct Packet
    {
        uint8_t command = 0;
        uint32_t length = 0;
        uint32_t ms = 0;
    };
    static constexpr size_t kRecentPackets = 64;
    /// The recent packets and the count since power-on (a copy: any thread)
    std::vector<Packet> RecentPackets(uint64_t* count = nullptr) const;

    /// The board's state as of the last frame end or power-on (a copy: any thread; no log, packets or audio)
    Vdac4Control::BoardState Published() const;

private:
    class TangLink;
    void Log(const std::string& line);
    void Publish();
    /// The esp_jit feature (or UNREAL_XTENSA_JIT) to the ESP runtime when it changed; main thread
    void ApplyJitFeature();

    EmulatorContext* _context;
    Vdac4Tang& _tang;
    std::function<void()> _catchUp;
    Vdac4Control::BoardState _published;  ///< guarded by _ringLock
    std::unique_ptr<TangLink> _link;
    std::unique_ptr<Vdac4Audio> _audio;
    bool _audioAttached = false;
    int _jit = -1; ///< what ApplyJitFeature last set: -1 nothing yet
    std::unique_ptr<vdac4::EspXtensaRuntime> _runtime;
    std::unique_ptr<Vdac4FtWorker> _ft;  ///< the Tang's work for the ESP's jobs (vdac4ftworker.h)
    uint64_t _jobStamp = 0;              ///< the running ESP job's stamp (its thread)
    std::unique_ptr<Vdac4Esp> _esp;
    std::filesystem::path _ownCache;  ///< the ESP flash folder made for this board (none configured): removed with it
    std::ofstream _logFile;
    mutable std::mutex _ringLock;  ///< the log and packet rings: written on the emulation thread, read by any
    std::deque<std::string> _recentLog;
    std::deque<Packet> _recentPackets;
    uint64_t _packetCount = 0;
    uint64_t _lastNs = 0;     ///< the ESP's clock (machine time + _offsetNs), monotonic
    uint64_t _offsetNs = 0;   ///< machine resets so far: the ESP time they took back
    uint64_t _backSteps = 0;  ///< machine clock restarts seen
};
