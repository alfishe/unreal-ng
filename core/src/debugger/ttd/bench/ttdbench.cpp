#include "stdafx.h"

#include "ttdbench.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>

#include "base/featuremanager.h"
#include "common/filehelper.h"
#include "debugger/ttd/bench/ttdv1feeder.h"
#include "debugger/ttd/timetravelengine.h"
#include "debugger/ttd/timetravelmanager.h"
#include "emulator/config.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/emulatormanager.h"
#include "emulator/io/rtc/ds12887.h"
#include "emulator/io/rtc/rtcaccess.h"
#include "emulator/memory/memory.h"
#include "emulator/ports/portdecoder.h"

namespace ttd
{

namespace
{
/// Memory the engine records and v1 does not at all (D33 exception)
bool V1Lacks(TTDRegionId id)
{
    switch (id)
    {
        case TTDRegionId::MoonSoundWaveMemory:
        case TTDRegionId::NeoGSRam:
        case TTDRegionId::NeoGSFlash:
        case TTDRegionId::EvoAvrEeprom:
        case TTDRegionId::SmucEeprom:
            return true;
        default:
            return false;
    }
}
}  // namespace

namespace bench
{

namespace
{
using Clock = std::chrono::steady_clock;

/// Component-wise minimum of repeated seeks to one position
void KeepMin(SeekTiming& best, const SeekTiming& t, bool first)
{
    if (first)
    {
        best = t;
        return;
    }
    best.totalUs = std::min(best.totalUs, t.totalUs);
    best.restoreUs = std::min(best.restoreUs, t.restoreUs);
    best.replayUs = std::min(best.replayUs, t.replayUs);
    best.presentUs = std::min(best.presentUs, t.presentUs);
    best.otherUs = std::min(best.otherUs, t.otherUs);
    best.cpuChipsetUs = std::min(best.cpuChipsetUs, t.cpuChipsetUs);
    best.devicesUs = std::min(best.devicesUs, t.devicesUs);
    best.memoryUs = std::min(best.memoryUs, t.memoryUs);
    best.screenUs = std::min(best.screenUs, t.screenUs);
}

double ElapsedUs(Clock::time_point from, Clock::time_point to)
{
    return std::chrono::duration<double, std::micro>(to - from).count();
}

/// Nearest-rank percentile of an unsorted sample (p in 0..100)
double Percentile(std::vector<double> values, double p)
{
    if (values.empty())
        return 0.0;
    std::sort(values.begin(), values.end());
    size_t rank = static_cast<size_t>(p / 100.0 * static_cast<double>(values.size()) + 0.5);
    rank = std::clamp<size_t>(rank, 1, values.size());
    return values[rank - 1];
}

void AddPercentiles(Metrics& m, const std::string& prefix, const std::vector<double>& values)
{
    m[prefix + "_p50"] = Percentile(values, 50);
    m[prefix + "_p95"] = Percentile(values, 95);
    m[prefix + "_p99"] = Percentile(values, 99);
    m[prefix + "_max"] = values.empty() ? 0.0 : *std::max_element(values.begin(), values.end());
}

/// region <Machine>

/// A fresh machine for one run of a case; removed on destruction
class Machine
{
public:
    Machine(const Configuration& config, std::string& error)
    {
        // A case is the same machine in every process that runs it (BR-2), so
        // the CI gate and core-benchmarks measure identical bytes: the global
        // config hook (core-tests' sound-card policy) is set aside while the
        // machine is created, and the peripheral set goes on top of the
        // model's config as this instance's override. The override also
        // zeroes power-on RAM ([MISC] RAMPowerOn): the default noise comes
        // from the process-global rand(), so the machine would depend on
        // every case that ran before it in the process
        const Config::ConfigLoadedHook previous = Config::GetConfigLoadedHook();
        Config::SetConfigLoadedHook({});
        const PeripheralSet set = config.peripherals;
        auto configOverride = [set](CONFIG& c) {
            c.ramPowerOn = RamPowerOn::Zero;
            switch (set.turboSound)
            {
                case PeripheralSet::Slot::None: c.sound.turboSoundKind = TurboSoundKind::None; break;
                case PeripheralSet::Slot::Ay: c.sound.turboSoundKind = TurboSoundKind::AY; break;
                case PeripheralSet::Slot::Tsfm: c.sound.turboSoundKind = TurboSoundKind::FM; break;
                case PeripheralSet::Slot::Keep: break;
            }
            if (set.gsRamKB == 0)
                c.sound.gsTypeKind = GSTypeKind::NONE;
            else if (set.gsRamKB > 0)
            {
                c.sound.gsTypeKind = GSTypeKind::Z80;
                c.sound.gsRamKB = static_cast<unsigned>(set.gsRamKB);
            }
            if (set.moonSound >= 0)
                c.sound.moonsound = set.moonSound;
            if (set.covox >= 0)
            {
                c.sound.covoxFB = set.covox;
                c.sound.sd = set.covox;
            }
            if (set.beta >= 0)
                c.trdos_present = set.beta != 0;
            if (set.mouse >= 0)
                c.input.mouse = set.mouse ? MOUSE_TYPE_KEMPSTON : MOUSE_TYPE_NONE;
        };

        EmulatorManager* manager = EmulatorManager::GetInstance();
        _emulator = config.ramKB ? manager->CreateEmulatorWithModelAndRAM("ttd-bench", config.model, config.ramKB,
                                                                          LoggerLevel::LogError, &error, configOverride)
                                 : manager->CreateEmulatorWithModel("ttd-bench", config.model, LoggerLevel::LogError,
                                                                    &error, configOverride);
        Config::SetConfigLoadedHook(previous);
        if (!_emulator && error.empty())
            error = "cannot create " + config.model;

        // A replayable workload (BR-2) cannot read the host clock: firmware
        // that reads the CMOS time while it boots (ZX-Evo BaseConf) would lay
        // out RAM differently on every run
        if (_emulator)
            if (Ds12887* rtc = RtcAccess::Find(_emulator->GetContext()))
                rtc->SetFixedTime(kFrozenTime);
    }

    static constexpr time_t kFrozenTime = 1767268830;  // 2026-01-01 12:00:30 UTC

    ~Machine()
    {
        if (_emulator)
            EmulatorManager::GetInstance()->RemoveEmulator(_emulator->GetUUID());
    }

    Emulator* Get() const { return _emulator.get(); }

private:
    std::shared_ptr<Emulator> _emulator;
};

/// Start state, set-up actions and settle frames of a workload
bool Prepare(Emulator& emulator, const Case& c, const Options& options, std::string& error)
{
    if (options.journalBytes && emulator.GetContext()->pTimeTravelManager)
        emulator.GetContext()->pTimeTravelManager->SetWriteJournalCapacity(options.journalBytes);
    const Workload& w = c.workload;
    const std::string path = w.file.empty() || !options.resolveTestData ? w.file : options.resolveTestData(w.file);
    switch (w.start)
    {
        case Workload::Start::ColdBoot:
emulator.Reset();
            break;
        case Workload::Start::Snapshot:
            if (!emulator.LoadSnapshot(path))
            {
                error = "cannot load snapshot " + path;
                return false;
            }
            break;
        case Workload::Start::DiskAutostart:
        {
            const Emulator::DiskAutostartResult started = emulator.AutostartDisk(path);
            if (!started.mounted)
            {
                error = "cannot autostart " + path + ": " + started.message;
                return false;
            }
            break;
        }
    }

    // Settle first (boot, loading): firmware rewrites the machine latches while
    // it starts, so set-up actions such as a turbo switch go in afterwards
    if (w.settleFrames)
        emulator.RunNFrames(w.settleFrames);

    PortDecoder* ports = emulator.GetContext()->pPortDecoder;
    const EmulatorState& state = emulator.GetContext()->emulatorState;
    for (const SetupAction& a : c.config.setup)
    {
        switch (a.kind)
        {
            case SetupAction::Kind::PortIn:
                ports->DecodePortIn(a.port, 0);
                break;
            case SetupAction::Kind::PortOut:
                ports->DecodePortOut(a.port, a.value, 0);
                break;
            case SetupAction::Kind::AtmTurbo:
                ports->DecodePortOut(static_cast<uint16_t>((state.aFF77 & 0xFF00) | 0x77),
                                     static_cast<uint8_t>(state.pFF77 | 0x08), 0);
                break;
        }
    }
    return true;
}

uint32_t MeasuredFrames(const Case& c, const Options& options)
{
    return options.framesOverride ? options.framesOverride : c.workload.frames;
}

/// Run the measured frames; per-frame wall time (and capture time when an
/// engine records) in microseconds
void RunFrames(Emulator& emulator, const Case& c, const Options& options, Engine* engine,
               std::vector<double>& frameUs, std::vector<double>* captureUs, CaptureWork* work = nullptr)
{
    TimeTravelManager* ttd = emulator.GetContext()->pTimeTravelManager;
    const uint32_t frames = MeasuredFrames(c, options);
    size_t nextInput = 0;
    frameUs.reserve(frames);
    struct Slow
    {
        double us;
        uint64_t frame;
        CaptureWork work;
    };
    std::vector<Slow> slow;
    const char* slowEnv = std::getenv("UNREAL_TTD_BENCH_SLOW_FRAMES");
    const size_t slowFrames = slowEnv ? static_cast<size_t>(std::strtoul(slowEnv, nullptr, 10)) : 0;
    for (uint32_t f = 0; f < frames; f++)
    {
        // Scripted input through the live-input path: applied at once in the
        // synchronous run mode, journaled while recording
        while (nextInput < c.workload.input.size() && c.workload.input[nextInput].frame == f)
        {
            const ScriptedInput& in = c.workload.input[nextInput++];
            TTDInputEvent ev;
            ev.kind = TTDInputKind::Key;
            ev.key = in.zxKey;
            ev.pressed = in.pressed;
            if (ttd)
                ttd->SubmitLiveInput(ev);
        }
        const Clock::time_point start = Clock::now();
        emulator.RunNFrames(1);
        frameUs.push_back(ElapsedUs(start, Clock::now()));
        if (engine && captureUs)
            captureUs->push_back(static_cast<double>(engine->LastCaptureNs()) / 1000.0);
        if (engine && work)
            *work += engine->LastCaptureWork();
        if (engine && slowFrames)
        {
            const CaptureWork w = engine->LastCaptureWork();
            slow.push_back({static_cast<double>(engine->LastCaptureNs()) / 1000.0, f, w});
        }
    }
    // UNREAL_TTD_BENCH_SLOW_FRAMES=N: the N slowest captures with their work (diagnostics, stderr)
    if (slowFrames && !slow.empty())
    {
        std::sort(slow.begin(), slow.end(), [](const Slow& a, const Slow& b) { return a.us > b.us; });
        for (size_t i = 0; i < slow.size() && i < slowFrames; ++i)
            std::fprintf(stderr, "slow capture %.1f us frame %llu: scanned %llu B (ram %llu), compressed %llu B in %llu calls, delta base %llu B, state %llu B\n",
                         slow[i].us, static_cast<unsigned long long>(slow[i].frame),
                         static_cast<unsigned long long>(slow[i].work.bytesScanned),
                         static_cast<unsigned long long>(slow[i].work.bytesScannedRam),
                         static_cast<unsigned long long>(slow[i].work.compressInputBytes),
                         static_cast<unsigned long long>(slow[i].work.compressCalls),
                         static_cast<unsigned long long>(slow[i].work.deltaBaseBytes),
                         static_cast<unsigned long long>(slow[i].work.deviceStateBytes));
    }
}

/// endregion </Machine>

/// region <Engine v1>

class EngineV1 final : public Engine
{
public:
    std::string Name() const override { return "v1"; }

    bool Start(Emulator& emulator, Mode mode, std::string& error) override
    {
        _context = emulator.GetContext();
        _ttd = _context->pTimeTravelManager;
        if (!_ttd)
        {
            error = "no TimeTravelManager";
            return false;
        }
        _context->pFeatureManager->setFeature(Features::kTimeTravel, true);
        _ttd->SetEnableWriteJournal(mode != Mode::NoJournal);
        _ttd->SetEnableCoverageIndex(mode == Mode::JournalCoverage);
        if (!_ttd->StartRecording())
        {
            error = "StartRecording refused";
            return false;
        }
        return true;
    }

    void Stop() override
    {
        if (_ttd)
            _ttd->StopRecording();
    }

    uint64_t LastCaptureNs() const override { return _ttd ? _ttd->GetPerfCounters().lastCaptureNs : 0; }

    CaptureWork LastCaptureWork() const override
    {
        CaptureWork w;
        if (!_ttd)
            return w;
        const TTDCaptureWork& t = _ttd->GetPerfCounters().lastCaptureWork;
        w.pagesVisited = t.pagesVisited;
        w.deltaBaseBytes = t.deltaBaseBytes;
        w.deviceBlobBytes = t.deviceBlobBytes;
        w.deviceStateBytes = t.deviceStateBytes;
        w.bytesScanned = t.bytesScanned;
        w.compressCalls = t.compressCalls;
        w.compressInputBytes = t.compressInputBytes;
        w.slotsDecoded = t.slotsDecoded;
        return w;
    }
    size_t Checkpoints() const override { return _ttd ? _ttd->GetCheckpointCount() : 0; }

    uint64_t FirstFrame() const override
    {
        const TTDCheckpoint* cp = _ttd ? _ttd->GetCheckpoint(0) : nullptr;
        return cp ? cp->time.frame : 0;
    }

    uint64_t LastFrame() const override
    {
        const size_t n = Checkpoints();
        const TTDCheckpoint* cp = n ? _ttd->GetCheckpoint(n - 1) : nullptr;
        return cp ? cp->time.frame : 0;
    }

    uint32_t FrameSpan() const override { return _ttd ? _ttd->FrameSpan() : 0; }

    StreamBytes Bytes() const override
    {
        StreamBytes b;
        if (!_ttd)
            return b;
        b.ramPayload = _ttd->GetPageStore().GetLivePayloadBytes();
        for (size_t i = 0; i < _ttd->GetCheckpointCount(); i++)
        {
            const TTDCheckpoint* cp = _ttd->GetCheckpoint(i);
            b.pageRefs += cp->ramPages.size() * sizeof(TTDPageRef);
            for (const auto& blob : cp->peripheralBlobs)
                b.deviceBlobs += blob.second.size();
            b.checkpointCore += sizeof(TTDCpuState) + sizeof(TTDChipsetState);
        }
        if (const TTDWriteJournal* journal = _ttd->GetWriteJournal())
        {
            b.writeJournal = journal->Size() * sizeof(TTDWriteRecord);
            b.journalWritesTotal = journal->SeqHead();
        }
        b.inputJournal = _ttd->GetInputJournal().Size() * sizeof(TTDInputEvent);
        b.coverage = _ttd->GetCoverageIndex().HeapBytes();
        return b;
    }

    uint64_t ResidentBytes() const override { return _ttd ? _ttd->GetSessionInfo().sessionHeapBytes : 0; }

    std::vector<std::pair<std::string, uint64_t>> HeapParts() const override
    {
        if (!_ttd)
            return {};
        const TTDHeapBreakdown h = _ttd->GetHeapBreakdown();
        return {
            {"page_store_table", h.pageStoreTable}, {"ram_payload", h.ramPayload},
            {"ram_payload_slack", h.ramPayloadSlack}, {"checkpoints", h.checkpoints},
            {"page_refs", h.pageRefs}, {"device_blobs", h.deviceBlobs},
            {"input_journals", h.inputJournals}, {"write_journal", h.writeJournal},
            {"write_journal_slack", h.writeJournalSlack}, {"coverage", h.coverage},
            {"coverage_slack", h.coverageSlack}, {"port_reads", h.portReads},
            {"port_writes", h.portWrites}, {"port_journal_slack", h.portJournalSlack},
            {"frame_cache", h.frameCache},
        };
    }

    bool Seek(uint64_t frame, uint32_t tInFrame, SeekTiming& out) override
    {
        TTDTimePoint target;
        target.frame = frame;
        target.tInFrame = tInFrame;
        const Clock::time_point start = Clock::now();
        const bool ok = _ttd->SeekTo(target);
        out.totalUs = ElapsedUs(start, Clock::now());
        const TTDPerfCounters& perf = _ttd->GetPerfCounters();
        out.cpuChipsetUs = static_cast<double>(perf.lastRestoreCpuChipsetNs) / 1000.0;
        out.devicesUs = static_cast<double>(perf.lastRestoreDevicesNs) / 1000.0;
        out.memoryUs = static_cast<double>(perf.lastRestoreMemoryNs) / 1000.0;
        out.screenUs = static_cast<double>(perf.lastRestoreScreenNs) / 1000.0;
        out.restoreUs = static_cast<double>(perf.lastRestoreTotalNs()) / 1000.0;
        out.replayUs = static_cast<double>(perf.lastReplayNs) / 1000.0;
        out.presentUs = static_cast<double>(perf.lastPresentNs) / 1000.0;
        out.otherUs = std::max(0.0, out.totalUs - out.restoreUs - out.replayUs - out.presentUs);
        return ok;
    }

    bool Save(const std::string& path, uint64_t& bytes, std::string& error) override
    {
        std::ofstream file(FileHelper::ToFsPath(path), std::ios::binary | std::ios::trunc);
        if (!file || !_ttd->SerializeSession(file, error))
            return false;
        file.flush();
        bytes = static_cast<uint64_t>(file.tellp());
        return static_cast<bool>(file);
    }

    bool Load(Emulator& emulator, const std::string& path, std::string& error) override
    {
        _context = emulator.GetContext();
        _ttd = _context->pTimeTravelManager;
        _context->pFeatureManager->setFeature(Features::kTimeTravel, true);
        std::ifstream file(FileHelper::ToFsPath(path), std::ios::binary);
        return file && _ttd->DeserializeSession(file, error);
    }

    uint64_t CaptureNow() override
    {
        _ttd->OnFrameBoundary();
        return _ttd->GetPerfCounters().lastCaptureNs;
    }

    /// The recorded v1 session (the time-travel engine records next to it)
    TimeTravelManager* Manager() const { return _ttd; }

private:
    EmulatorContext* _context = nullptr;
    TimeTravelManager* _ttd = nullptr;
};

/// endregion </Engine v1>

/// region <Engine: TimeTravelEngine>

/// The time-travel engine (docs/inprogress/2026-09-25-ttd-v2-migration/).
/// Phase 1, Step 4: it records the running emulator in shadow mode, next to
/// v1 (TimeTravelManager::SetShadowEngine), so its capture time, counted
/// work, bytes and memory are its own on every case; seek and file metrics
/// come as the engine gains them
class EngineTimeTravel final : public Engine
{
public:
    std::string Name() const override { return "engine"; }

    Capabilities Supports() const override
    {
        Capabilities c;
        c.liveCapture = true;
        c.seek = false;
        c.seekMemory = true;
        c.saveLoad = false;
        return c;
    }

    bool Start(Emulator& emulator, Mode mode, std::string& error) override
    {
        _engine.EndSession();
        // UNREAL_TTD_ENGINE_SNAPSHOT_INTERVAL: full reference table every N checkpoints, to measure alternatives
        if (const char* v = std::getenv("UNREAL_TTD_ENGINE_SNAPSHOT_INTERVAL"); v && *v)
            _engine.SetSnapshotInterval(static_cast<uint32_t>(std::strtoul(v, nullptr, 10)));
        _feedError.clear();
        if (!_recorder.Start(emulator, mode, error))
            return false;
        _recorder.Manager()->SetShadowEngine(&_engine);
        return true;
    }

    void Stop() override
    {
        if (TimeTravelManager* v1 = _recorder.Manager())
            v1->SetShadowEngine(nullptr);
        _recorder.Stop();
        if (!_engine.IsSessionOpen())
            _feedError = "the shadow engine holds no session";
    }

    uint64_t LastCaptureNs() const override { return _engine.LastCaptureNs(); }
    CaptureWork LastCaptureWork() const override
    {
        const TTDEngineCaptureWork& e = _engine.LastCaptureWork();
        CaptureWork w;
        w.pagesVisited = e.piecesOffered / 4;   // pieces handed over, in 16 KB pages like v1's count
        w.deltaBaseBytes = e.deltaBaseBytes;
        w.bytesScanned = e.piecesOffered * kTTDPieceSize;
        w.compressCalls = e.compressCalls;
        w.compressInputBytes = e.compressInputBytes;
        w.deviceBlobBytes = e.deviceBlobBytes;
        w.deviceStateBytes = e.deviceStateBytes;
        w.bytesScannedRam = uint64_t(_engine.LastPiecesOffered(0)) * kTTDPieceSize;
        for (uint32_t r = 1; r < _engine.Regions().size(); ++r)
            if (V1Lacks(_engine.Regions()[r].id))
                w.bytesScannedV1Lacks += uint64_t(_engine.LastPiecesOffered(r)) * kTTDPieceSize;
        return w;
    }
    size_t Checkpoints() const override { return _engine.CheckpointCount(); }
    uint64_t FirstFrame() const override { return _engine.Frames().FirstFrame(); }
    uint64_t LastFrame() const override { return _engine.Frames().LastFrame(); }
    uint32_t FrameSpan() const override { return _recorder.FrameSpan(); }

    StreamBytes Bytes() const override
    {
        StreamBytes b;
        b.ramPayload = _engine.RegionPayloadBytes(0);   // machine RAM, as v1's ramPayload
        b.versions = _engine.RegionVersionCount(0);
        for (uint32_t r = 1; r < _engine.Regions().size(); ++r)
        {
            if (_engine.IsDeviceStateRegion(r))
            {
                // Device states (Phase 2): stored only when they change, as differences
                b.deviceBlobs += _engine.RegionPayloadBytes(r);
                b.versions += _engine.RegionVersionCount(r);
                // UNREAL_TTD_BENCH_DEVICE_BYTES: each device's bytes (diagnostics, stderr)
                if (std::getenv("UNREAL_TTD_BENCH_DEVICE_BYTES"))
                    std::fprintf(stderr, "device bytes %s: %llu in %llu versions\n", _engine.Regions()[r].name.c_str(),
                                 static_cast<unsigned long long>(_engine.RegionPayloadBytes(r)),
                                 static_cast<unsigned long long>(_engine.RegionVersionCount(r)));
                continue;
            }
            b.deviceRegions += _engine.RegionPayloadBytes(r);
            if (V1Lacks(_engine.Regions()[r].id))
            {
                b.deviceRegionsV1Lacks += _engine.RegionPayloadBytes(r);
                b.versionsV1Lacks += _engine.RegionVersionCount(r);
            }
            b.versions += _engine.RegionVersionCount(r);
        }
        b.pageRefs = _engine.ReferenceBytes();
        for (size_t i = 0; i < _engine.CheckpointCount(); i++)
        {
            b.checkpointCore += sizeof(TTDCpuState) + sizeof(TTDChipsetState);
        }
        return b;
    }

    uint64_t ResidentBytes() const override { return _engine.HeapBreakdown().Total(); }

    std::vector<std::pair<std::string, uint64_t>> HeapParts() const override
    {
        const TTDEngineHeapBreakdown h = _engine.HeapBreakdown();
        return {
            {"piece_versions", h.pieceVersions}, {"ram_payload", h.piecePayload},
            {"arena_slack", h.arenaSlack}, {"reference_tables", h.referenceTables},
            {"delta_base", h.deltaBase}, {"checkpoints", h.checkpoints},
            {"device_blobs", h.deviceBlobs}, {"frame_table", h.frameTable},
            {"event_log", h.eventLog}, {"port_reads", h.portReads},
            {"port_writes", h.portWrites}, {"port_journal_slack", h.portJournalSlack},
            {"media_reads", h.mediaReads},   // v1 has no such journal: not in D33's sum
            {"bus_vectors", h.busVectors},   // nor this one
        };
    }

    /// Frame-aligned memory restore only (Phase 1, Step 5): CPU, devices and
    /// the picture come with the switch to the engine
    bool Seek(uint64_t frame, uint32_t tInFrame, SeekTiming& out) override
    {
        const int64_t index = _engine.CheckpointIndexOf({0, frame, 0});
        if (tInFrame != 0 || index < 0)
            return false;
        TTDRestoreStats stats;
        const auto start = std::chrono::steady_clock::now();
        const bool ok = _engine.RestoreToMemory(static_cast<size_t>(index), nullptr, &stats).Ok();
        out = SeekTiming{};
        out.memoryUs = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - start).count();
        out.restoreUs = out.memoryUs;
        out.totalUs = out.memoryUs;
        out.piecesDecoded = static_cast<double>(stats.piecesDecoded);
        return ok;
    }
    bool Save(const std::string&, uint64_t&, std::string& error) override
    {
        error = "the engine has no file before Phase 4";
        return false;
    }
    bool Load(Emulator&, const std::string&, std::string& error) override
    {
        error = "the engine has no file before Phase 4";
        return false;
    }
    uint64_t CaptureNow() override
    {
        _recorder.CaptureNow();
        return _engine.LastCaptureNs();
    }

    std::string LastError() const override { return _feedError; }

private:
    EngineV1 _recorder;
    TimeTravelEngine _engine;
    std::string _feedError;
};

/// endregion </Engine: TimeTravelEngine>

/// region <Measurements>

/// BM-8: capture time with N 4 KB pieces of new content, by hand
void DirtySweep(Emulator& emulator, Engine& engine, Metrics& m)
{
    Memory* memory = emulator.GetContext()->pMemory;
    uint8_t seed = 1;
    for (int pieces : {0, 1, 4, 16, 64})
    {
        std::vector<double> samples;
        for (int round = 0; round < 32; round++)
        {
            for (int piece = 0; piece < pieces; piece++)
            {
                // Bank 1 is RAM on every model: walk 16 KB pages through it and
                // write content into one 4 KB sub-page per piece
                memory->SetRAMPageToBank1(static_cast<uint16_t>(piece / 4));
                const uint16_t base = static_cast<uint16_t>(0x4000 + (piece % 4) * 0x1000);
                for (uint16_t off = 0; off < 0x1000; off += 32)
                    memory->MemoryWriteDebug(static_cast<uint16_t>(base + off), static_cast<uint8_t>(seed + off + piece));
            }
            seed++;
            samples.push_back(static_cast<double>(engine.CaptureNow()) / 1000.0);
        }
        m["bm8_capture_us_dirty" + std::to_string(pieces)] = Percentile(samples, 50);
    }
}

/// BM-1: the workload with TTD off and in each recording mode; median frame time
bool Overhead(Engine& engine, const Case& c, const Options& options, Metrics& m, std::string& error)
{
    std::vector<double> off;
    {
        Machine machine(c.config, error);
        if (!machine.Get() || !Prepare(*machine.Get(), c, options, error))
            return false;
        RunFrames(*machine.Get(), c, options, nullptr, off, nullptr);
    }
    const double offMedian = Percentile(off, 50);
    m["bm1_frame_off_us_p50"] = offMedian;

    const std::pair<Mode, const char*> modes[] = {{Mode::NoJournal, "bm1_overhead_nojournal_pct"},
                                                  {Mode::Journal, "bm1_overhead_journal_pct"},
                                                  {Mode::JournalCoverage, "bm1_overhead_journal_cov_pct"}};
    for (const auto& [mode, name] : modes)
    {
        Machine machine(c.config, error);
        if (!machine.Get() || !Prepare(*machine.Get(), c, options, error) || !engine.Start(*machine.Get(), mode, error))
            return false;
        std::vector<double> frames;
        RunFrames(*machine.Get(), c, options, &engine, frames, nullptr);
        engine.Stop();
        m[name] = offMedian > 0 ? (Percentile(frames, 50) / offMedian - 1.0) * 100.0 : 0.0;
    }
    return true;
}

/// endregion </Measurements>

/// BM-7's session file: moved to Options::keepSessionDir when set, else deleted
void KeepOrRemove(const Options& options, const std::string& file)
{
    if (options.keepSessionDir.empty())
    {
        std::remove(file.c_str());
        return;
    }
    std::error_code ec;
    const std::filesystem::path from = FileHelper::ToFsPath(file);
    const std::filesystem::path dir = FileHelper::ToFsPath(options.keepSessionDir);
    std::filesystem::create_directories(dir, ec);
    std::filesystem::rename(from, dir / from.filename(), ec);
    if (ec)
        std::remove(file.c_str());
}

std::string ScratchFile(const Options& options, const std::string& name)
{
    std::string dir = options.scratchDir.empty() ? std::string(".") : options.scratchDir;
    std::string safe = name;
    for (char& ch : safe)
        if (ch == '/' || ch == '+' || ch == ' ')
            ch = '_';
    return dir + "/ttd-bench-" + safe + ".ttd";
}

}  // namespace

/// region <PeripheralSet>

bool PeripheralSet::Parse(const std::string& text, PeripheralSet& out, std::string& error)
{
    out = PeripheralSet{};
    if (text.empty() || text == "default")
        return true;
    if (text == "none")
    {
        out.turboSound = Slot::None;
        out.gsRamKB = 0;
        out.moonSound = 0;
        out.covox = 0;
        out.mouse = 0;
        return true;
    }
    std::stringstream in(text);
    std::string item;
    while (std::getline(in, item, '+'))
    {
        if (item == "ay" || item == "ts")
            out.turboSound = Slot::Ay;
        else if (item == "tsfm")
            out.turboSound = Slot::Tsfm;
        else if (item == "gs128")
            out.gsRamKB = 128;
        else if (item == "gs512")
            out.gsRamKB = 512;
        else if (item == "moon")
            out.moonSound = 1;
        else if (item == "covox")
            out.covox = 1;
        else if (item == "beta")
            out.beta = 1;
        else if (item == "mouse")
            out.mouse = 1;
        else
        {
            error = "unknown peripheral '" + item + "' (ay, ts, tsfm, gs128, gs512, moon, covox, beta, mouse, none)";
            return false;
        }
    }
    return true;
}

std::string PeripheralSet::Name() const
{
    std::string name;
    auto add = [&name](const char* part) { name += (name.empty() ? "" : "+") + std::string(part); };
    if (turboSound == Slot::None)
        add("noay");
    if (turboSound == Slot::Ay)
        add("ay");
    if (turboSound == Slot::Tsfm)
        add("tsfm");
    if (gsRamKB == 128)
        add("gs128");
    if (gsRamKB == 512)
        add("gs512");
    if (moonSound == 1)
        add("moon");
    if (covox == 1)
        add("covox");
    if (beta == 1)
        add("beta");
    if (mouse == 1)
        add("mouse");
    return name.empty() ? "default" : name;
}

/// endregion </PeripheralSet>

/// region <Engines>

std::vector<std::string> EngineNames()
{
    return {"v1", "engine"};
}

std::unique_ptr<Engine> CreateEngine(const std::string& name)
{
    if (name == "v1")
        return std::make_unique<EngineV1>();
    if (name == "engine")
        return std::make_unique<EngineTimeTravel>();
    return nullptr;
}

/// endregion </Engines>

/// region <Runner>

Result RunE7(const Case& c, const std::string& sessionFile)
{
    Result r;
    Machine machine(c.config, r.error);
    Emulator* emulator = machine.Get();
    std::unique_ptr<Engine> v1 = CreateEngine("v1");
    if (!emulator || !v1 || !v1->Load(*emulator, sessionFile, r.error))
    {
        if (r.error.empty())
            r.error = "cannot load " + sessionFile;
        return r;
    }
    EmulatorContext* context = emulator->GetContext();
    TimeTravelManager* ttd = context->pTimeTravelManager;
    const TTDWriteJournal* journal = ttd->GetWriteJournal();
    const size_t count = ttd->GetCheckpointCount();
    if (!journal || journal->HasEvictedRecords() || count < 3)
    {
        r.error = "the session's write journal does not hold its whole history";
        return r;
    }
    Metrics& m = r.metrics;
    m["e7_frames"] = static_cast<double>(count);
    using Clock = std::chrono::steady_clock;

    // The coverage index walked over the whole session for an address no
    // frame wrote (a page this machine does not have)
    const uint64_t first = ttd->GetCheckpoint(0)->time.frame;
    const uint64_t last = ttd->GetCheckpoint(count - 1)->time.frame;
    double best = 0, firstWalk = 0;
    uint64_t scanned = 0;
    for (int repeat = 0; repeat < 3; ++repeat)
    {
        const Clock::time_point t0 = Clock::now();
        const TTDCoverageScanResult scan =
            ttd->QueryCoverageScan(first, last, TTDCoverageKind::Written, 0xC000, 0xC000, PhysPage(0xFE), 1);
        const double ns = static_cast<double>(std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - t0).count());
        scanned = scan.scannedFrames;
        if (!scan.indexAvailable || scanned == 0)
        {
            r.error = "the session has no coverage index";
            return r;
        }
        best = repeat == 0 ? ns : std::min(best, ns);
        if (repeat == 0)
            firstWalk = ns;
    }
    m["e7_walk_frames"] = static_cast<double>(scanned);
    m["e7_walk_ns_per_frame"] = best / static_cast<double>(scanned);
    m["e7_walk_ns_per_frame_first"] = firstWalk / static_cast<double>(scanned);

    // One frame's writes regenerated by replay, against the journal's records
    // of the frame: (from, to], where its checkpoint's CPU and the next one's stood
    const uint64_t units = ttd->FrameSpan() / std::max(1u, context->config.frame);
    auto at = [&](size_t index) {
        const TTDCheckpoint* cp = ttd->GetCheckpoint(index);
        return ttd->GlobalT(cp->time) + GetChipsetCpuTInFrame(cp->chipset) * units;
    };
    const size_t samples = std::min<size_t>(200, count - 1);
    std::vector<double> regenUs;
    std::vector<TTDSearchResult> hits;
    std::vector<TTDWriteRecord> recorded;
    uint64_t seq = journal->SeqTail();
    size_t mismatches = 0, refused = 0, writes = 0;
    for (size_t s = 0; s < samples; ++s)
    {
        const size_t i = s * (count - 1) / samples;
        const uint64_t from = at(i), to = at(i + 1);
        recorded.clear();
        while (seq < journal->SeqHead() && journal->RecordAt(seq).globalT <= from)
            ++seq;
        for (uint64_t k = seq; k < journal->SeqHead() && journal->RecordAt(k).globalT <= to; ++k)
            if (!journal->RecordAt(k).isIo)
                recorded.push_back(journal->RecordAt(k));

        const Clock::time_point t0 = Clock::now();
        const bool ok = ttd->RegenerateFrameWrites(ttd->GetCheckpoint(i)->time.frame, hits);
        const double us =
            static_cast<double>(std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - t0).count()) / 1000.0;
        if (!ok)
        {
            ++refused;
            continue;
        }
        regenUs.push_back(us);
        writes += hits.size();
        bool same = hits.size() == recorded.size();
        for (size_t k = 0; same && k < hits.size(); ++k)
            same = ttd->GlobalT(hits[k].time) == recorded[k].globalT && hits[k].addr == recorded[k].addr &&
                   hits[k].value == recorded[k].value && hits[k].pc == recorded[k].m1pc &&
                   static_cast<uint8_t>(hits[k].physPage) == recorded[k].physPage;
        mismatches += same ? 0 : 1;
    }
    m["e7_regen_frames"] = static_cast<double>(regenUs.size());
    m["e7_regen_refused_frames"] = static_cast<double>(refused);
    m["e7_regen_mismatch_frames"] = static_cast<double>(mismatches);
    m["e7_regen_writes_per_frame"] = regenUs.empty() ? 0.0 : static_cast<double>(writes) / static_cast<double>(regenUs.size());
    AddPercentiles(m, "e7_regen_us", regenUs);
    r.ok = true;
    return r;
}

bool IsByteMetric(const std::string& name)
{
    auto endsWith = [&name](const char* suffix) {
        const std::string s(suffix);
        return name.size() >= s.size() && name.compare(name.size() - s.size(), s.size(), s) == 0;
    };
    return endsWith("_bytes") || endsWith("_bpf") || endsWith("_opf") || name == "frames" || name == "checkpoints";
}

Result RunCase(Engine& engine, const Case& c, const Options& options)
{
    Result r;
    Metrics& m = r.metrics;

    // Main run: every stream recorded (journal + coverage), per-frame capture
    {
        Machine machine(c.config, r.error);
        Emulator* emulator = machine.Get();
        if (!emulator || !Prepare(*emulator, c, options, r.error))
            return r;
        if (!engine.Start(*emulator, Mode::JournalCoverage, r.error))
            return r;

        std::vector<double> frameUs;
        std::vector<double> captureUs;
        CaptureWork work;
        RunFrames(*emulator, c, options, &engine, frameUs, &captureUs, &work);
        engine.Stop();
        if (!engine.LastError().empty() || engine.Checkpoints() == 0)
        {
            r.error = engine.Name() + ": " + (engine.LastError().empty() ? "no checkpoint recorded" : engine.LastError());
            return r;
        }

        const double frames = static_cast<double>(MeasuredFrames(c, options));
        m["frames"] = frames;
        m["checkpoints"] = static_cast<double>(engine.Checkpoints());
        const Engine::Capabilities caps = engine.Supports();

        // BM-2
        if (caps.liveCapture)
        {
            AddPercentiles(m, "bm2_capture_us", captureUs);
            const double p50 = Percentile(captureUs, 50);
            m["bm2_capture_p99_over_p50"] = p50 > 0 ? Percentile(captureUs, 99) / p50 : 0.0;
            // Capture as a share of the recorded frame. Informational: under heavy
            // host load capture's large copies slow down more than emulation, so
            // the share drifts up; the CI gate checks the counted work below
            const double frameP50 = Percentile(frameUs, 50);
            m["bm2_capture_share_pct"] = frameP50 > 0 ? p50 / frameP50 * 100.0 : 0.0;
            // BM-2 work: what the captures did, counted (deterministic, the CI
            // gate's check of capture cost); means per recorded frame
            const double perFrame = 1.0 / std::max(1.0, frames);
            m["bm2_work_pages_visited_opf"] = static_cast<double>(work.pagesVisited) * perFrame;
            m["bm2_work_delta_base_bpf"] = static_cast<double>(work.deltaBaseBytes) * perFrame;
            m["bm2_work_device_blobs_bpf"] = static_cast<double>(work.deviceBlobBytes) * perFrame;
            m["bm2_work_device_state_bpf"] = static_cast<double>(work.deviceStateBytes) * perFrame;
            m["bm2_work_scanned_v1_lacks_bpf"] = static_cast<double>(work.bytesScannedV1Lacks) * perFrame;
            m["bm2_work_scanned_ram_bpf"] = static_cast<double>(work.bytesScannedRam) * perFrame;
            m["bm2_work_scanned_bpf"] = static_cast<double>(work.bytesScanned) * perFrame;
            m["bm2_work_compress_calls_opf"] = static_cast<double>(work.compressCalls) * perFrame;
            m["bm2_work_compress_input_bpf"] = static_cast<double>(work.compressInputBytes) * perFrame;
            m["bm2_work_decoded_opf"] = static_cast<double>(work.slotsDecoded) * perFrame;
        }

        // BM-3 (bytes per recorded frame, split by stream) and BM-4
        const StreamBytes b = engine.Bytes();
        const double n = std::max(1.0, frames);
        m["bm3_ram_payload_bpf"] = static_cast<double>(b.ramPayload) / n;
        m["bm3_page_refs_bpf"] = static_cast<double>(b.pageRefs) / n;
        m["bm3_device_blobs_bpf"] = static_cast<double>(b.deviceBlobs) / n;
        m["bm3_checkpoint_core_bpf"] = static_cast<double>(b.checkpointCore) / n;
        m["bm3_write_journal_bpf"] = static_cast<double>(b.writeJournal) / n;
        m["bm3_input_journal_bpf"] = static_cast<double>(b.inputJournal) / n;
        m["bm3_coverage_bpf"] = static_cast<double>(b.coverage) / n;
        m["bm3_device_regions_bpf"] = static_cast<double>(b.deviceRegions) / n;
        m["bm3_device_regions_v1_lacks_bpf"] = static_cast<double>(b.deviceRegionsV1Lacks) / n;
        m["bm3_versions_v1_lacks_share"] = b.versions ? static_cast<double>(b.versionsV1Lacks) / b.versions : 0.0;
        m["bm3_total_bpf"] = static_cast<double>(b.Total()) / n;
        // Writes per frame over the whole session: the journal's bytes above
        // stop growing once its ring is full, this count does not
        m["bm3_journal_writes_opf"] = static_cast<double>(b.journalWritesTotal) / n;
        const double resident = static_cast<double>(engine.ResidentBytes());
        m["bm4_resident_bytes"] = resident;
        m["bm4_resident_bpf"] = resident / n;
        // BM-4 split: where the session heap goes, per recorded frame
        for (const auto& [part, bytes] : engine.HeapParts())
            m["bm4_heap_" + part + "_bpf"] = static_cast<double>(bytes) / n;
        // Proof the configuration ran as named: the hardware turbo in effect at the end
        m["turbo_ratio"] = static_cast<double>(emulator->GetContext()->emulatorState.hw_turbo_ratio);

        // BM-5 / BM-6: random positions, frame-aligned and inside a frame
        const uint64_t first = engine.FirstFrame();
        const uint64_t last = engine.LastFrame();
        const uint32_t span = engine.FrameSpan();
        if (caps.seek && last > first && span > 1 && options.seekSamples)
        {
            std::mt19937 rng(options.seekSeed);
            std::uniform_int_distribution<uint64_t> pickFrame(first, last - 1);
            std::uniform_int_distribution<uint32_t> pickT(1, span - 1);
            std::vector<double> aligned, offset, replay, restore, cpu, devices, memory, screen;
            std::vector<double> alignedPresent, offsetPresent, alignedNoPresent, offsetNoPresent;
            for (uint32_t i = 0; i < options.seekSamples; i++)
            {
                const uint64_t frame = pickFrame(rng);
                const uint32_t tInFrame = pickT(rng);
                const uint32_t repeats = std::max<uint32_t>(1, options.seekRepeats);
                SeekTiming t;
                SeekTiming sample;
                for (uint32_t k = 0; k < repeats; k++)
                {
                    if (!engine.Seek(frame, 0, sample))
                    {
                        r.error = "seek to frame " + std::to_string(frame) + " refused";
                        return r;
                    }
                    KeepMin(t, sample, k == 0);
                }
                aligned.push_back(t.totalUs);
                alignedPresent.push_back(t.presentUs);
                alignedNoPresent.push_back(t.totalUs - t.presentUs);
                cpu.push_back(t.cpuChipsetUs);
                devices.push_back(t.devicesUs);
                memory.push_back(t.memoryUs);
                screen.push_back(t.screenUs);
                for (uint32_t k = 0; k < repeats; k++)
                {
                    if (!engine.Seek(frame, tInFrame, sample))
                    {
                        r.error = "seek inside frame " + std::to_string(frame) + " refused";
                        return r;
                    }
                    KeepMin(t, sample, k == 0);
                }
                offset.push_back(t.totalUs);
                offsetPresent.push_back(t.presentUs);
                offsetNoPresent.push_back(t.totalUs - t.presentUs);
                replay.push_back(t.replayUs);
                restore.push_back(t.restoreUs);
            }
            // Totals as a user sees a seek (the picture included), and without
            // the picture: intra-frame checkpoints (V1b) shorten the replay, not
            // the picture, so the V1b question reads the "_nopresent" figures
            AddPercentiles(m, "bm5_aligned_us", aligned);
            AddPercentiles(m, "bm5_offset_us", offset);
            AddPercentiles(m, "bm5_aligned_nopresent_us", alignedNoPresent);
            AddPercentiles(m, "bm5_offset_nopresent_us", offsetNoPresent);
            m["bm5_aligned_present_us_p50"] = Percentile(alignedPresent, 50);
            m["bm5_aligned_present_us_p99"] = Percentile(alignedPresent, 99);
            m["bm5_offset_present_us_p50"] = Percentile(offsetPresent, 50);
            m["bm5_offset_present_us_p99"] = Percentile(offsetPresent, 99);
            m["bm5_offset_restore_us_p50"] = Percentile(restore, 50);
            m["bm5_offset_restore_us_p99"] = Percentile(restore, 99);
            m["bm5_offset_replay_us_p50"] = Percentile(replay, 50);
            m["bm5_offset_replay_us_p99"] = Percentile(replay, 99);
            m["bm6_restore_cpu_chipset_us_p50"] = Percentile(cpu, 50);
            m["bm6_restore_devices_us_p50"] = Percentile(devices, 50);
            m["bm6_restore_memory_us_p50"] = Percentile(memory, 50);
            m["bm6_restore_screen_us_p50"] = Percentile(screen, 50);
        }

        // BM-6 memory restore alone, for an engine that cannot do full seeks yet
        if (!caps.seek && caps.seekMemory && last > first && options.seekSamples)
        {
            std::mt19937 rng(options.seekSeed);
            std::uniform_int_distribution<uint64_t> pickFrame(first, last - 1);
            std::uniform_int_distribution<uint32_t> pickT(1, std::max<uint32_t>(2, span) - 1);
            std::vector<double> memory;
            double decoded = 0;
            for (uint32_t i = 0; i < options.seekSamples; i++)
            {
                const uint64_t frame = pickFrame(rng);
                (void)pickT(rng);   // same random sequence as the full-seek loop
                SeekTiming t;
                if (!engine.Seek(frame, 0, t))
                {
                    r.error = "memory restore of frame " + std::to_string(frame) + " refused";
                    return r;
                }
                memory.push_back(t.memoryUs);
                decoded += t.piecesDecoded;
            }
            m["bm6_restore_memory_us_p50"] = Percentile(memory, 50);
            m["bm6_restore_memory_us_p99"] = Percentile(memory, 99);
            m["bm6_pieces_decoded_mean"] = decoded / options.seekSamples;
        }

        // BM-7: save, then load into a fresh machine and seek once
        if (options.saveLoad && caps.saveLoad && caps.seek)
        {
            const std::string file = ScratchFile(options, engine.Name() + "-" + c.Name());
            uint64_t bytes = 0;
            const Clock::time_point saveStart = Clock::now();
            if (!engine.Save(file, bytes, r.error))
                return r;
            const double saveS = ElapsedUs(saveStart, Clock::now()) / 1e6;
            m["bm7_file_bytes"] = static_cast<double>(bytes);
            m["bm7_file_bpf"] = static_cast<double>(bytes) / n;
            m["bm7_save_s_per_gb"] = bytes ? saveS / (static_cast<double>(bytes) / 1e9) : 0.0;

            Machine target(c.config, r.error);
            if (!target.Get())
                return r;
            const Clock::time_point loadStart = Clock::now();
            if (!engine.Load(*target.Get(), file, r.error))
            {
                std::remove(file.c_str());
                return r;
            }
            const double loadS = ElapsedUs(loadStart, Clock::now()) / 1e6;
            SeekTiming t;
            engine.Seek(engine.FirstFrame() + (engine.LastFrame() - engine.FirstFrame()) / 2, 0, t);
            m["bm7_load_s_per_gb"] = bytes ? loadS / (static_cast<double>(bytes) / 1e9) : 0.0;
            m["bm7_first_seek_ms"] = loadS * 1e3 + t.totalUs / 1e3;
            KeepOrRemove(options, file);
        }
    }

    // BM-8 on a fresh recording of the same start state
    if (options.dirtySweep && engine.Supports().liveCapture)
    {
        Machine machine(c.config, r.error);
        if (!machine.Get() || !Prepare(*machine.Get(), c, options, r.error) ||
            !engine.Start(*machine.Get(), Mode::NoJournal, r.error))
            return r;
        DirtySweep(*machine.Get(), engine, m);
        engine.Stop();
    }

    // BM-1 last: it builds four more machines
    if (options.overhead && engine.Supports().liveCapture && !Overhead(engine, c, options, m, r.error))
        return r;

    r.ok = true;
    return r;
}

/// endregion </Runner>

/// region <Matrix>

namespace
{
Configuration Base(const std::string& name, const std::string& model, unsigned ramKB = 0)
{
    Configuration c;
    c.name = name;
    c.model = model;
    c.ramKB = ramKB;
    return c;
}

Configuration WithPeripherals(Configuration c, const std::string& set)
{
    std::string error;
    PeripheralSet::Parse(set, c.peripherals, error);
    c.name += "+" + c.peripherals.Name();
    return c;
}

Workload Idle(uint32_t frames)
{
    Workload w;
    w.name = "idle";
    w.start = Workload::Start::ColdBoot;
    w.settleFrames = 150;
    w.frames = frames;
    return w;
}

Workload FromSnapshot(const std::string& name, const std::string& file, uint32_t frames)
{
    Workload w;
    w.name = name;
    w.start = Workload::Start::Snapshot;
    w.file = file;
    w.settleFrames = 50;
    w.frames = frames;
    return w;
}

Workload FromDisk(const std::string& name, const std::string& file, uint32_t settle, uint32_t frames)
{
    Workload w;
    w.name = name;
    w.start = Workload::Start::DiskAutostart;
    w.file = file;
    w.settleFrames = settle;
    w.frames = frames;
    return w;
}

/// A game with a player: action.sna with the fire / move keys scripted
Workload GameWithInput(uint32_t frames)
{
    Workload w = FromSnapshot("game", "loaders/sna/action.sna", frames);
    for (uint32_t f = 25; f + 20 < frames; f += 50)
    {
        w.input.push_back({f, static_cast<uint8_t>('P'), true});
        w.input.push_back({f + 20, static_cast<uint8_t>('P'), false});
        w.input.push_back({f + 5, static_cast<uint8_t>(' '), true});
        w.input.push_back({f + 10, static_cast<uint8_t>(' '), false});
    }
    std::sort(w.input.begin(), w.input.end(), [](const ScriptedInput& a, const ScriptedInput& b) { return a.frame < b.frame; });
    return w;
}
}  // namespace

std::vector<Case> Matrix(const std::string& set)
{
    std::vector<Case> cases;
    auto add = [&cases](const Configuration& c, const Workload& w) { cases.push_back({c, w}); };

    // Turbo: ATM #FF77 bit 3 (the rest of the latch as the firmware left it).
    // The Scorpion firmware switches its Turbo+ flip-flop on by itself while
    // it boots (IN #7FFD: the standard ROM's service page at #0419, ProfROM's
    // #04CE on the way to the user program), so plain SCORPION / PROFSCORP
    // run at 7 MHz; the 3.5 MHz Scorpion clears it afterwards with IN #1FFD
    Configuration atm710Turbo = Base("ATM710-turbo", "ATM710");
    atm710Turbo.turbo = true;
    atm710Turbo.setup.push_back({SetupAction::Kind::AtmTurbo, 0, 0});
    Configuration scorpion = Base("SCORPION", "SCORPION");
    scorpion.turbo = true;
    Configuration profScorpion = Base("PROFSCORP", "PROFSCORP");
    profScorpion.turbo = true;
    Configuration scorpion35 = Base("SCORPION-3.5MHz", "SCORPION");
    scorpion35.setup.push_back({SetupAction::Kind::PortIn, 0x1FFD, 0});
    Configuration atm3 = Base("ATM3", "ATM3");  // BaseConf boots with the 2x turbo on
    atm3.turbo = true;

    if (set == "ci")
    {
        add(Base("PENTAGON", "PENTAGON"), Idle(60));
        add(Base("PENTAGON", "PENTAGON"), GameWithInput(60));
        add(Base("48K", "48K"), Idle(60));
        add(atm3, Idle(60));
        return cases;
    }

    if (set == "turbo")
    {
        add(atm3, Idle(1500));
        add(WithPeripherals(atm3, "gs512+moon+tsfm"), Idle(1500));
        add(atm710Turbo, Idle(1500));
        add(scorpion, Idle(1500));
        add(Base("PENTAGON", "PENTAGON"), FromSnapshot("demo", "loaders/sna/7threality.sna", 1500));
        return cases;
    }

    // full: every base model with its default peripherals (BR-4) ...
    const Configuration bases[] = {Base("48K", "48K"),
                                   Base("128K", "128k"),
                                   Base("PLUS2", "PLUS2"),
                                   Base("PLUS2A", "PLUS2A"),
                                   Base("PENTAGON", "PENTAGON"),
                                   Base("PENTAGON512", "PENTAGON", 512),
                                   Base("PENTAGON1024", "PENTAGON", 1024),
                                   Base("PLUS3", "PLUS3"),
                                   scorpion,
                                   scorpion35,
                                   profScorpion,
                                   Base("ATM450", "ATM450"),
                                   Base("ATM710", "ATM710"),
                                   atm710Turbo,
                                   atm3,
                                   Base("TSCONF", "TSL"),
                                   Base("PROFI", "PROFI"),
                                   Base("SPRINTER", "SPRINTER"),
                                   Base("TSL-VDAC2", "TSL-VDAC2")};
    for (const Configuration& c : bases)
        add(c, Idle(3000));

    // ... workloads on the 128K-compatible Pentagon (BR-6) ...
    const Configuration pentagon = Base("PENTAGON", "PENTAGON");
    add(pentagon, GameWithInput(3000));
    add(pentagon, FromSnapshot("demo", "loaders/sna/7threality.sna", 3000));
    add(pentagon, FromSnapshot("demo2", "loaders/sna/across-the-edge-second.sna", 3000));
    add(pentagon, FromSnapshot("demo-eyeache", "loaders/sna/eyeache1.sna", 3000));
    add(WithPeripherals(pentagon, "tsfm"), FromSnapshot("music-tsfm", "sound/tsfm/tech_support.sna", 3000));
    add(WithPeripherals(pentagon, "covox"), FromSnapshot("music-covox", "sound/covox/scroller_by_demarche.sna", 3000));
    add(WithPeripherals(pentagon, "beta"), FromDisk("disk-loading", "sound/The_Viewer1.0.trd", 0, 3000));
    add(WithPeripherals(pentagon, "gs512"),
        FromDisk("gs-upload", "sound/generalsound/soundtracker_20th_anniversary.scl", 0, 3000));
    add(WithPeripherals(pentagon, "moon"), FromDisk("moon-upload", "sound/moonsound/mfm_sample.trd", 0, 3000));

    // ... and the peripheral sets on Pentagon 128 and ZX-Evo (BR-5)
    // ("ts" is not a set of its own: the TurboSound slot holds two AYs or TSFM)
    for (const char* periph : {"none", "ay", "tsfm", "gs128", "gs512", "moon", "covox", "beta", "mouse"})
    {
        add(WithPeripherals(pentagon, periph), Idle(1500));
        add(WithPeripherals(atm3, periph), Idle(1500));
    }
    return cases;
}

/// endregion </Matrix>

}  // namespace bench
}  // namespace ttd
