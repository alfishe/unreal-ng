#pragma once

/// @file ttdvdac4.h
/// @brief TTD devices of the TS-Conf VDAC4 board (docs/inprogress/2026-11-15-tsconf-vdac4/ttd-design.md).
///
/// The Tang's FT812 is the VDAC2 card's devices (Vdac2Memory, Vdac2: the same EveHostCard). Beside them:
///   - Vdac4Tang (78): the Tang's own state (personality, timers, FT_CFG, the LINK window);
///   - Vdac4Esp (79): the ESP's clock, its SPI slave, link, frame transport and firmware;
///   - Vdac4EspCpu (80): the Xtensa app host. Its memory (PSRAM, SRAM, the ROM's RW copy) is recorded only at
///     keyframes (every KeyframeFrames() captures, a rescan, a journal over kJournalLimit), its blob is the host's
///     state at the last keyframe and the input journal since. A restore loads the keyframe and runs the ESP
///     again up to the frame on that journal, then checks the host state's hash.
///   - Vdac4Q2 (81): the Tang's Quake II core (bomzh-3D) while it runs. Every input of the core but the Evo side
///     is a LINK write of the ESP's app, so the core goes with the ESP's keyframes: Vdac4EspCpu's keyframe holds
///     the core's whole state, and its restore feeds the core the LINK writes of its replay. Vdac4Q2's blob at
///     every capture is the rest (the Evo side, the latched picture, a hash of the core's state to check the
///     replay against); its SDRAM is the region Vdac4Q2Sdram, offered whole at the ESP's keyframes only.

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
    /// A keyframe at least this often (captures) while slot 0 or the FT812 runs
    static constexpr uint32_t kKeyframeFrames = 25;
    /// The same while the Quake II core runs: its keyframes store the SDRAM pieces the frame and z buffers rewrote
    /// as well, so they cost more (ttd-design.md §9.5)
    static constexpr uint32_t kKeyframeFramesQ2 = 50;
    /// The interval in use: the one of the personality, or UNREAL_VDAC4_TTD_KEYFRAME for both (A/B runs)
    uint32_t KeyframeFrames(bool q2) const { return q2 ? _keyEveryQ2 : _keyEvery; }
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
    /// A replay point is kept: a load that goes on from one writes the whole ESP memory itself
    bool TTDLoadMayRestoreRegions() const override { return !_points.empty(); }
    bool TTDLoadRestoresRegions(const uint8_t* state, size_t size) override;
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
        uint64_t regionsLeft = 0;      ///< of them: the engine left the ESP memory to the point (not decoded first)
    };
    /// Replay points kept (each the ESP's whole memory, about 9 MB)
    static constexpr size_t kReplayPoints = 3;
    const Stats& GetStats() const { return _stats; }
    /// The last capture was a keyframe of the ESP (Vdac4Q2 offers the SDRAM then)
    bool LastCaptureKeyed() const { return _keyedNow; }

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
        std::vector<uint8_t> q2State, q2Sdram;  ///< the Quake II core, when the keyframe has it running
        uint64_t used = 0;
    };
    ReplayPoint* FindReplayPoint(uint64_t keyHash, const std::vector<uint8_t>& journal);
    void KeepReplayPoint(uint64_t keyHash, const uint8_t* journal, size_t size, bool q2);

    Vdac4Board& _board;
    TTDRegionTracker _trackers[3];
    bool _armed = false;
    std::vector<uint8_t> _keyState;  ///< the host's state at the last keyframe (empty: none yet)
    uint32_t _sinceKey = 0;          ///< captures since the keyframe
    uint32_t _keyEvery = kKeyframeFrames;
    uint32_t _keyEveryQ2 = kKeyframeFramesQ2;
    bool _keyDue = true;
    uint64_t _hashNow = 0;           ///< the host state's hash at this capture
    uint64_t _journalLast = 0;       ///< the journal's length at this capture
    uint64_t _journalPrev = 0;       ///< and at the capture before (0: the keyframe), a replay point on restore
    std::vector<ReplayPoint> _points;
    uint64_t _pointClock = 0;
    mutable std::vector<uint8_t> _scratch;
    Stats _stats;
    std::string _loadDivergence;  ///< the last load's divergence, until the engine takes it (empty: none)
    bool _regionsLeft = false;    ///< TTDLoadRestoresRegions said yes: the next load must write the memory
    bool _keyedNow = false;       ///< LastCaptureKeyed
    std::vector<uint8_t> _q2Key;  ///< the Quake II core's state at the keyframe (Vdac4BomzhCore::Save; empty: not running)
    uint64_t _q2Epoch = 0;        ///< Vdac4Tang::Q2Epoch and Q2OutsideInputs at the last capture: a change keys
    uint64_t _q2Outside = 0;
};

/// The Tang's Quake II core: the engine's blob is Vdac4Tang::SaveQ2 Live (the Evo side, counters, a hash of the
/// core's state, the picture only when the core's state does not give it); the core itself comes from
/// Vdac4EspCpu's keyframe and replay, so this device restores after it and checks the hash. The SDRAM is a region
/// of its own, offered at the ESP's keyframes. The whole state (a live save) carries the core and the SDRAM
class TTDVdac4Q2 : public TTDSerializable, public ITTDRegionSource
{
public:
    TTDVdac4Q2(Vdac4Tang& tang, const TTDVdac4EspCpu& espCpu);

    size_t TTDStateSize() const override;
    void TTDSaveState(uint8_t* dst) const override;
    void TTDLoadState(const uint8_t* src) override;
    bool TTDVariableSize() const override { return true; }
    void TTDSaveStateTo(std::vector<uint8_t>& out) const override;
    std::string TTDDeviceName() const override { return "Vdac4Q2"; }
    TTDDeviceType TTDType() const override { return TTDDeviceType::Vdac4Q2; }
    uint64_t TTDHashState() const override;
    TTDDeviceDescriptor TTDDescribe() const override;

    /// region <ITTDRegionSource>
    void TTDRegions(std::vector<TTDDeviceRegion>& out) override;
    void TTDArmRegions(bool on) override { _armed = on; }
    void TTDBeforeCapture() override;
    void TTDBeforeRestore() override;
    bool TTDStateWithoutRegions(std::vector<uint8_t>& state) const override;
    bool TTDLoadStateWithoutRegions(const uint8_t* state, size_t size) override;
    size_t TTDStateWithoutRegionsLimit() const override;
    bool TTDLoadDiverged(std::string& detail) override;
    /// endregion

private:
    Vdac4Tang& _tang;
    const TTDVdac4EspCpu& _espCpu;
    TTDRegionTracker _tracker;
    bool _armed = false;
    std::string _loadDivergence;  ///< the last load's divergence, until the engine takes it (empty: none)
};

}  // namespace ttd
