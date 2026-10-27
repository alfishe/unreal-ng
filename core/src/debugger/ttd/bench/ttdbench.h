#pragma once

/// @file ttdbench.h
/// @brief TTD benchmark harness (PLAN #40 Phase 0, Step 2, TTD v2 requirements §5).
///
/// One harness for every TTD engine version, so v1, v2 and later versions run
/// exactly the same emulation and their numbers compare:
///
///   - Engine: the TTD implementation under test, chosen by name at run time
///     (BR-1). "v1" wraps today's TimeTravelManager.
///   - Configuration: a base model with its default peripherals, optionally
///     with a peripheral set on top (BR-4, BR-5), plus set-up actions such as
///     switching the hardware turbo on.
///   - Workload: a replayable recording (BR-2): a start state (cold boot or a
///     snapshot or an autostarted disk), a number of settle frames, then the
///     measured frames with scripted input applied at fixed frames through the
///     TTD live-input path (journaled like a user's keys).
///   - Metrics BM-1..BM-8 (requirements §5.2), all under stable names; byte
///     counts are deterministic, timings are percentiles.
///
/// Used by core-benchmarks (the matrix, JSON output) and by core-tests (the
/// CI-sized gate). Nothing here runs unless a harness calls it.

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

class Emulator;

namespace ttd
{
namespace bench
{

/// region <Configuration>

/// Peripheral set on top of a base model's defaults (BR-5). Parsed from
/// "none" or a '+'-joined list: ay (alias ts: the TurboSound slot as two
/// AYs), tsfm, gs128, gs512, moon, covox, beta, mouse. Fields left at Keep
/// keep the model config's value
struct PeripheralSet
{
    enum class Slot : uint8_t
    {
        Keep,
        None,
        Ay,    // the TurboSound slot as a two-AY pair (AY and TurboSound music)
        Tsfm,
    };
    Slot turboSound = Slot::Keep;
    int gsRamKB = -1;   // -1 keep, 0 off, 128 / 512 = classic GS card with that RAM
    int moonSound = -1; // -1 keep, 0 off, 1 on
    int covox = -1;     // -1 keep, 0 off, 1 = Covox #FB + SoundDrive
    int beta = -1;      // -1 keep, 0 off, 1 = Beta 128 (TR-DOS)
    int mouse = -1;     // -1 keep, 0 off, 1 = Kempston mouse

    /// "none" clears every device; an empty string keeps the model's defaults
    static bool Parse(const std::string& text, PeripheralSet& out, std::string& error);
    std::string Name() const;  ///< "default" or the canonical '+' list
};

/// A set-up step applied after the start state, before measuring
struct SetupAction
{
    enum class Kind : uint8_t
    {
        PortIn,   // IN (port): the Scorpion turbo flip-flop clocks on reads
        PortOut,  // OUT (port), value
        AtmTurbo, // ATM: rewrite #FF77 as it stands with the turbo bit (3) set
    };
    Kind kind = Kind::PortOut;
    uint16_t port = 0;
    uint8_t value = 0;
};

struct Configuration
{
    std::string name;         ///< matrix name, e.g. "PENTAGON", "ATM3+gs512+moon"
    std::string model;        ///< model short name for EmulatorManager
    unsigned ramKB = 0;       ///< RAM size override (Pentagon 512 / 1024); 0 = model default
    PeripheralSet peripherals;
    std::vector<SetupAction> setup;
    bool turbo = false;       ///< runs with a hardware turbo (PR-5 / V1b question)
};

/// Scripted input at a measured frame (relative to the first measured frame)
struct ScriptedInput
{
    uint32_t frame = 0;
    uint8_t zxKey = 0;        ///< ZXKeysEnum value
    bool pressed = false;
};

struct Workload
{
    enum class Start : uint8_t
    {
        ColdBoot,
        Snapshot,
        DiskAutostart,
    };
    std::string name;         ///< "idle", "game", "demo", "music-tsfm", ...
    Start start = Start::ColdBoot;
    std::string file;         ///< testdata-relative path for Snapshot / DiskAutostart
    uint32_t settleFrames = 0;///< run before measuring (boot, loading)
    uint32_t frames = 0;      ///< measured (recorded) frames
    std::vector<ScriptedInput> input;
};

/// One matrix entry
struct Case
{
    Configuration config;
    Workload workload;
    std::string Name() const { return config.name + "/" + workload.name; }
};

/// The configuration x workload matrix. "ci" = the <= 2 minute subset (BR-7),
/// "full" = every base model, peripheral set and workload (BR-4..BR-6),
/// "turbo" = the turbo / heavy configurations that decide V1b (PR-5)
std::vector<Case> Matrix(const std::string& set);

/// endregion </Configuration>

/// region <Engine>

/// Byte streams of a recording (BM-3), totals over the recorded frames
struct StreamBytes
{
    uint64_t ramPayload = 0;      ///< page store payload (compressed RAM content)
    uint64_t pageRefs = 0;        ///< checkpoint page reference tables
    uint64_t deviceBlobs = 0;     ///< peripheral / model state blobs
    uint64_t checkpointCore = 0;  ///< CPU + chipset state per checkpoint
    uint64_t writeJournal = 0;
    uint64_t inputJournal = 0;
    uint64_t coverage = 0;
    /// Device memory recorded as regions (the time-travel engine; v1 keeps
    /// the General Sound RAM inside deviceBlobs and the rest not at all)
    uint64_t deviceRegions = 0;
    /// The part of deviceRegions v1 does not record at all (NeoGS RAM and
    /// flash, MoonSound wave RAM, the EEPROMs): D33's "memory v1 does not
    /// record" exception. Not added to Total() a second time
    uint64_t deviceRegionsV1Lacks = 0;
    /// Piece versions stored, in all and for memory v1 does not record
    uint64_t versions = 0;
    uint64_t versionsV1Lacks = 0;
    /// Memory writes recorded since the session started, including those the
    /// write journal's ring has already dropped (not part of Total())
    uint64_t journalWritesTotal = 0;
    uint64_t Total() const
    {
        return ramPayload + pageRefs + deviceBlobs + checkpointCore + writeJournal + inputJournal + coverage +
               deviceRegions;
    }
};

/// One region of the engine (memory, or a device's state): its size and what the session stored for it -
/// the state registry's Size and Variability columns come from these (BM-9)
struct RegionStat
{
    std::string name;          ///< "ram", "neogs.ram", "device.zxbus.1.neogs", ...
    uint64_t bytes = 0;        ///< the region's size (a device state: the size it reached)
    uint64_t payload = 0;      ///< stored bytes, every version
    uint64_t versions = 0;     ///< versions stored (pieces that changed, summed over checkpoints)
    uint64_t pieces = 0;       ///< 4 KB pieces
};

/// Restore split of one seek (BM-5 / BM-6), microseconds
struct SeekTiming
{
    double totalUs = 0;
    double restoreUs = 0;         ///< checkpoint restore (sum of the components below)
    double replayUs = 0;          ///< re-execution from the checkpoint to the target
    double presentUs = 0;         ///< building the picture of the position
    double otherUs = 0;           ///< the rest (search, bookkeeping)
    double cpuChipsetUs = 0;
    double devicesUs = 0;
    double memoryUs = 0;
    double screenUs = 0;
    double piecesDecoded = 0;     ///< 4 KB pieces written by the memory restore (engines that count them)
};

/// Counted work of a frame capture (BM-2 work). Unlike the capture time it
/// repeats exactly for a replayable workload on any host and under any load,
/// which makes it the CI gate's check of capture cost
struct CaptureWork
{
    uint64_t pagesVisited = 0;
    uint64_t deltaBaseBytes = 0;
    uint64_t deviceBlobBytes = 0;
    uint64_t bytesScanned = 0;
    uint64_t compressCalls = 0;
    uint64_t compressInputBytes = 0;
    uint64_t slotsDecoded = 0;
    /// Raw device-state bytes serialized (both engines read them every frame)
    uint64_t deviceStateBytes = 0;
    /// Part of bytesScanned spent on memory v1 does not record (NeoGS,
    /// MoonSound wave RAM, the EEPROMs; engine only): D33's exception
    uint64_t bytesScannedV1Lacks = 0;
    uint64_t bytesScannedRam = 0;   ///< part of bytesScanned spent on machine RAM (engine only)

    CaptureWork& operator+=(const CaptureWork& o)
    {
        bytesScannedRam += o.bytesScannedRam;
        deviceStateBytes += o.deviceStateBytes;
        bytesScannedV1Lacks += o.bytesScannedV1Lacks;
        pagesVisited += o.pagesVisited;
        deltaBaseBytes += o.deltaBaseBytes;
        deviceBlobBytes += o.deviceBlobBytes;
        bytesScanned += o.bytesScanned;
        compressCalls += o.compressCalls;
        compressInputBytes += o.compressInputBytes;
        slotsDecoded += o.slotsDecoded;
        return *this;
    }
};

/// Recording modes (BM-1)
enum class Mode : uint8_t
{
    NoJournal,
    Journal,
    JournalCoverage,
};

/// The TTD implementation under test. Engines are stateless between cases:
/// every case attaches one to a fresh emulator
class Engine
{
public:
    /// What an engine can be measured on; the runner skips the rest. The
    /// time-travel engine gains them step by step (Phase 1: memory and bytes
    /// first, live capture in Step 4, seeks in Step 5, files in Phase 4)
    struct Capabilities
    {
        bool liveCapture = true;   ///< BM-1 / BM-2 / BM-8 measure this engine's own capture
        bool seek = true;          ///< BM-5 / BM-6
        bool seekMemory = false;   ///< without full seeks: BM-6 memory restore alone (frame-aligned)
        bool saveLoad = true;      ///< BM-7
        bool save = false;         ///< BM-7 file size alone (a file it cannot load back into a machine yet)
    };

    virtual ~Engine() = default;
    virtual std::string Name() const = 0;
    virtual Capabilities Supports() const { return {}; }
    /// Why the last Stop() left no usable session (empty when it did)
    virtual std::string LastError() const { return {}; }

    /// Start recording on @p emulator (paused, synchronous run mode)
    virtual bool Start(Emulator& emulator, Mode mode, std::string& error) = 0;
    /// Stop recording, keep the history for seeks
    virtual void Stop() = 0;
    /// Capture time of the frame that just ended, nanoseconds (BM-2)
    virtual uint64_t LastCaptureNs() const = 0;
    /// Counted work of the capture of the frame that just ended
    virtual CaptureWork LastCaptureWork() const = 0;
    virtual size_t Checkpoints() const = 0;
    virtual uint64_t FirstFrame() const = 0;
    virtual uint64_t LastFrame() const = 0;
    virtual uint32_t FrameSpan() const = 0;       ///< TTD time units per frame
    virtual StreamBytes Bytes() const = 0;
    virtual uint64_t ResidentBytes() const = 0;   ///< heap of the whole session (BM-4)
    /// ResidentBytes() by part, (name, bytes). The parts sum to ResidentBytes(),
    /// except the "*_slack" ones other than "ram_payload_slack": those are the
    /// unused allocation inside another part (TTDHeapBreakdown)
    virtual std::vector<std::pair<std::string, uint64_t>> HeapParts() const = 0;
    /// Per region of the engine (BM-9); empty for v1
    virtual std::vector<RegionStat> RegionStats() const { return {}; }
    /// Seek to (frame, tInFrame); false when the engine refused
    virtual bool Seek(uint64_t frame, uint32_t tInFrame, SeekTiming& out) = 0;
    /// Write the session to @p path; bytes written
    virtual bool Save(const std::string& path, uint64_t& bytes, std::string& error) = 0;
    /// Load a session written by Save into @p emulator (same configuration)
    virtual bool Load(Emulator& emulator, const std::string& path, std::string& error) = 0;
    /// Drive one capture by hand after writing N 4 KB pieces (BM-8); returns ns
    virtual uint64_t CaptureNow() = 0;
};

std::vector<std::string> EngineNames();
std::unique_ptr<Engine> CreateEngine(const std::string& name);

/// endregion </Engine>

/// region <Runner>

struct Options
{
    uint32_t framesOverride = 0;     ///< replace every workload's measured frames (0 = keep)
    uint32_t seekSamples = 200;      ///< random positions for BM-5
    uint32_t seekSeed = 0x5EEC;
    /// Each position is sought this many times and the minimum per component
    /// kept: the spread between positions (chain length, place in the frame)
    /// stays, a preempted measurement on a loaded host does not (BR-8)
    uint32_t seekRepeats = 3;
    bool overhead = true;            ///< BM-1: run the workload per mode, compare to TTD off
    bool saveLoad = true;            ///< BM-7
    bool dirtySweep = false;         ///< BM-8: 0 / 1 / 4 / 16 / 64 dirty 4 KB pieces
    std::string scratchDir;          ///< where BM-7 writes its session file
    /// Non-empty: BM-7's session file is kept here as <engine>-<case>.ttd
    /// instead of deleted - the recorded matrix workloads as input data for
    /// offline experiments (tools/poc/011-ttd-v2-capture-analysis/experiments)
    std::string keepSessionDir;
    /// Write journal ring in bytes for v1 (0: its default, 64 MB). Large
    /// enough not to wrap gives a session's whole write history (E7)
    size_t journalBytes = 0;
    /// testdata-relative path -> absolute path (the caller knows the tree)
    std::function<std::string(const std::string&)> resolveTestData;
};

/// Metric name -> value. Names: see RunCase(). Deterministic metrics end in
/// "_bytes", "_bpf" (bytes per frame) or "_opf" (operations per frame), plus
/// "frames" and "checkpoints"; everything else is time
using Metrics = std::map<std::string, double>;

struct Result
{
    bool ok = false;
    std::string error;
    Metrics metrics;
};

/// Run one case with one engine and return BM-1..BM-8
Result RunCase(Engine& engine, const Case& c, const Options& options);

/// Experiment E7 (write journal retention, TTD v2 Phase 3, Step 7): on
/// @p sessionFile, a session recorded from case @p c with a write journal that
/// did not wrap (UNREAL_TTD_BENCH_JOURNAL_MB), measures what a find-last
/// query costs when the journal does not answer it:
/// - e7_walk_ns_per_frame(_first): the coverage index walked for an address
///   no frame wrote, per frame (minimum of three walks; the first walk alone);
/// - e7_regen_us_*: one frame's writes regenerated by replay
///   (TimeTravelManager::RegenerateFrameWrites: restore + replay of the whole
///   frame), on up to 200 frames spread over the session;
/// - e7_regen_mismatch_frames: of those, frames whose regenerated writes
///   differ from the journal's (a determinism bug; must be 0);
///   e7_regen_refused_frames: frames a v1 marker keeps from replaying
Result RunE7(const Case& c, const std::string& sessionFile);

/// True for a deterministic metric name (bytes, bytes or operations per frame)
bool IsByteMetric(const std::string& name);

/// endregion </Runner>

}  // namespace bench
}  // namespace ttd
