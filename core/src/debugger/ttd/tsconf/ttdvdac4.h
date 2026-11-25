#pragma once

/// @file ttdvdac4.h
/// @brief TTD devices of the TS-Conf VDAC4 board (docs/inprogress/2026-11-15-tsconf-vdac4/ttd-design.md).
///
/// The Tang's FT812 is the VDAC2 card's devices (Vdac2Memory, Vdac2: the same EveHostCard). Beside them:
///   - Vdac4Tang (78): the Tang's own state (personality, timers, FT_CFG, the LINK window);
///   - Vdac4Esp (79): the ESP's clock, its SPI slave, link, frame transport and firmware;
///   - Vdac4EspCpu (80): the Xtensa app host. Its memory (PSRAM, SRAM, the ROM's RW copy) is recorded only at
///     keyframes (every kKeyframeFrames captures, a rescan, a journal over kJournalLimit), its blob is the host's
///     state at the last keyframe and the input journal since. A restore loads the keyframe and runs the ESP
///     again up to the frame on that journal, then checks the host state's hash.

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "debugger/ttd/engine/ttdregiontracker.h"
#include "debugger/ttd/ttdserializable.h"

class Vdac4Board;
class Vdac4Tang;

namespace ttd
{

class TTDVdac4Tang : public TTDSerializable
{
public:
    explicit TTDVdac4Tang(Vdac4Tang& tang) : _tang(tang) {}

    size_t TTDStateSize() const override;
    void TTDSaveState(uint8_t* dst) const override;
    void TTDLoadState(const uint8_t* src) override;
    std::string TTDDeviceName() const override { return "Vdac4Tang"; }
    TTDDeviceType TTDType() const override { return TTDDeviceType::Vdac4Tang; }
    uint64_t TTDHashState() const override;

private:
    Vdac4Tang& _tang;
};

/// The ESP board minus its app host: the clock (the board's monotonic one and the port decoder's time base it
/// reads), then Vdac4Esp::Save. Variable size: a firmware holding resources carries their bytes
class TTDVdac4Esp : public TTDSerializable
{
public:
    TTDVdac4Esp(Vdac4Board& board, uint64_t& baseUs, uint64_t& baseFrames)
        : _board(board), _baseUs(baseUs), _baseFrames(baseFrames)
    {
    }

    size_t TTDStateSize() const override;
    void TTDSaveState(uint8_t* dst) const override;
    void TTDLoadState(const uint8_t* src) override;
    bool TTDVariableSize() const override { return true; }
    void TTDSaveStateTo(std::vector<uint8_t>& out) const override;
    std::string TTDDeviceName() const override { return "Vdac4Esp"; }
    TTDDeviceType TTDType() const override { return TTDDeviceType::Vdac4Esp; }
    uint64_t TTDHashState() const override;
    TTDDeviceDescriptor TTDDescribe() const override;

private:
    Vdac4Board& _board;
    uint64_t& _baseUs;
    uint64_t& _baseFrames;
};

class TTDVdac4EspCpu : public TTDSerializable, public ITTDRegionSource
{
public:
    /// A keyframe at least this often (captures)
    static constexpr uint32_t kKeyframeFrames = 25;
    /// A keyframe once the journal holds this much
    static constexpr size_t kJournalLimit = 256 * 1024;

    explicit TTDVdac4EspCpu(Vdac4Board& board);

    /// Whole state: the host now and its memory, zero runs dropped (a live save, a tool edit)
    size_t TTDStateSize() const override;
    void TTDSaveState(uint8_t* dst) const override;
    void TTDLoadState(const uint8_t* src) override;
    bool TTDVariableSize() const override { return true; }
    void TTDSaveStateTo(std::vector<uint8_t>& out) const override;
    std::string TTDDeviceName() const override { return "Vdac4EspCpu"; }
    TTDDeviceType TTDType() const override { return TTDDeviceType::Vdac4EspCpu; }
    uint64_t TTDHashState() const override;
    TTDDeviceDescriptor TTDDescribe() const override;
    void TTDRecordingStarted() override;
    void TTDRecordingStopped() override;

    /// region <ITTDRegionSource>
    void TTDRegions(std::vector<TTDDeviceRegion>& out) override;
    void TTDArmRegions(bool on) override { _armed = on; }
    void TTDCaptureBegins(bool rescan) override;
    void TTDBeforeRestore() override;
    /// The engine's blob: the keyframe's host state, the journal since, the host state's hash now
    bool TTDStateWithoutRegions(std::vector<uint8_t>& state) const override;
    bool TTDLoadStateWithoutRegions(const uint8_t* state, size_t size) override;
    size_t TTDStateWithoutRegionsLimit() const override;
    bool TTDLoadDiverged(std::string& detail) override;
    /// endregion

    /// Diagnostics (tests, the design's measurements)
    struct Stats
    {
        uint64_t keyframes = 0;
        uint64_t restores = 0;
        uint64_t replayedBytes = 0;
        uint64_t divergences = 0;  ///< a replay that did not fit its journal or ended at another state
        std::string lastDivergence;
        uint64_t replayPointHits = 0;  ///< restores that went on from a replay point, not the keyframe
    };
    /// Replay points kept (each the ESP's whole memory, about 9 MB)
    static constexpr size_t kReplayPoints = 3;
    const Stats& GetStats() const { return _stats; }

private:
    void MarkAllMemory();
    uint64_t HostHash() const;

    /// The ESP as a replay left it at a capture boundary: a restore whose keyframe is the same and whose journal
    /// begins with this one goes on from here (a seek's picture restores the frame before the seek's: once per
    /// seek, not twice; a step forward replays one frame)
    struct ReplayPoint
    {
        uint64_t keyHash = 0;
        std::vector<uint8_t> journal;  ///< the journal replayed up to here
        std::vector<uint8_t> state;    ///< the host's state
        std::map<uint32_t, uint32_t> reads;
        std::vector<uint8_t> psram, sram, rom;
        uint64_t used = 0;
    };
    ReplayPoint* FindReplayPoint(uint64_t keyHash, const std::vector<uint8_t>& journal);
    void KeepReplayPoint(uint64_t keyHash, const uint8_t* journal, size_t size);

    Vdac4Board& _board;
    TTDRegionTracker _trackers[3];
    bool _armed = false;
    std::vector<uint8_t> _keyState;  ///< the host's state at the last keyframe (empty: none yet)
    uint32_t _sinceKey = 0;          ///< captures since the keyframe
    bool _keyDue = true;
    uint64_t _hashNow = 0;           ///< the host state's hash at this capture
    uint64_t _journalLast = 0;       ///< the journal's length at this capture
    uint64_t _journalPrev = 0;       ///< and at the capture before (0: the keyframe), a replay point on restore
    std::vector<ReplayPoint> _points;
    uint64_t _pointClock = 0;
    mutable std::vector<uint8_t> _scratch;
    Stats _stats;
    std::string _loadDivergence;  ///< the last load's divergence, until the engine takes it (empty: none)
};

}  // namespace ttd
