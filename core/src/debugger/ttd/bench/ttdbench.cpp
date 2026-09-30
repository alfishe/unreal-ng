#include "stdafx.h"

#include "ttdbench.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <random>
#include <sstream>

#include "base/featuremanager.h"
#include "common/filehelper.h"
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
        // Overlay the peripheral set on the model's config. The hook already
        // installed (core-tests' sound-card policy) is set aside, not chained:
        // a case is the same machine in every process that runs it (BR-2), so
        // the CI gate and core-benchmarks measure identical bytes
        const Config::ConfigLoadedHook previous = Config::GetConfigLoadedHook();
        const PeripheralSet set = config.peripherals;
        Config::SetConfigLoadedHook([set](CONFIG& c) {
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
        });

        EmulatorManager* manager = EmulatorManager::GetInstance();
        _emulator = config.ramKB ? manager->CreateEmulatorWithModelAndRAM("ttd-bench", config.model, config.ramKB,
                                                                          LoggerLevel::LogError, &error)
                                 : manager->CreateEmulatorWithModel("ttd-bench", config.model, LoggerLevel::LogError,
                                                                    &error);
        Config::SetConfigLoadedHook(previous);
        if (!_emulator && error.empty())
            error = "cannot create " + config.model;

        // A replayable workload (BR-2) cannot read the host clock: firmware
        // that reads the CMOS time while it boots (ZX-Evo BaseConf) would lay
        // out RAM differently on every run
        if (_emulator)
            if (Ds12887* rtc = RtcAccess::Find(_emulator->GetContext()))
                rtc->SetFixedTime(kFrozenTime);

        // Nor can it start from the power-on RAM noise: Memory fills pages 5
        // and 7 from the process-global rand(), so the machine would depend on
        // every case that ran before it in the process (core/tests/README.md,
        // "Power-on RAM is a hidden global input"). Zero, as the tests pin it
        if (_emulator)
        {
            Memory* memory = _emulator->GetContext()->pMemory;
            std::memset(memory->RAMPageAddress(5), 0, PAGE_SIZE);
            std::memset(memory->RAMPageAddress(7), 0, PAGE_SIZE);
        }
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
               std::vector<double>& frameUs, std::vector<double>* captureUs)
{
    TimeTravelManager* ttd = emulator.GetContext()->pTimeTravelManager;
    const uint32_t frames = MeasuredFrames(c, options);
    size_t nextInput = 0;
    frameUs.reserve(frames);
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
            b.writeJournal = journal->Size() * sizeof(TTDWriteRecord);
        b.inputJournal = _ttd->GetInputJournal().Size() * sizeof(TTDInputEvent);
        b.coverage = _ttd->GetCoverageIndex().HeapBytes();
        return b;
    }

    uint64_t ResidentBytes() const override { return _ttd ? _ttd->GetSessionInfo().sessionHeapBytes : 0; }

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

private:
    EmulatorContext* _context = nullptr;
    TimeTravelManager* _ttd = nullptr;
};

/// endregion </Engine v1>

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
    return {"v1"};
}

std::unique_ptr<Engine> CreateEngine(const std::string& name)
{
    if (name == "v1")
        return std::make_unique<EngineV1>();
    return nullptr;
}

/// endregion </Engines>

/// region <Runner>

bool IsByteMetric(const std::string& name)
{
    auto endsWith = [&name](const char* suffix) {
        const std::string s(suffix);
        return name.size() >= s.size() && name.compare(name.size() - s.size(), s.size(), s) == 0;
    };
    return endsWith("_bytes") || endsWith("_bpf") || name == "frames" || name == "checkpoints";
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
        RunFrames(*emulator, c, options, &engine, frameUs, &captureUs);
        engine.Stop();

        const double frames = static_cast<double>(MeasuredFrames(c, options));
        m["frames"] = frames;
        m["checkpoints"] = static_cast<double>(engine.Checkpoints());

        // BM-2
        AddPercentiles(m, "bm2_capture_us", captureUs);
        const double p50 = Percentile(captureUs, 50);
        m["bm2_capture_p99_over_p50"] = p50 > 0 ? Percentile(captureUs, 99) / p50 : 0.0;
        // Capture as a share of the recorded frame: both halves come from the
        // same frames, so a slow or loaded host moves them together (CI gate)
        const double frameP50 = Percentile(frameUs, 50);
        m["bm2_capture_share_pct"] = frameP50 > 0 ? p50 / frameP50 * 100.0 : 0.0;

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
        m["bm3_total_bpf"] = static_cast<double>(b.Total()) / n;
        const double resident = static_cast<double>(engine.ResidentBytes());
        m["bm4_resident_bytes"] = resident;
        m["bm4_resident_bpf"] = resident / n;
        // Proof the configuration ran as named: the hardware turbo in effect at the end
        m["turbo_ratio"] = static_cast<double>(emulator->GetContext()->emulatorState.hw_turbo_ratio);

        // BM-5 / BM-6: random positions, frame-aligned and inside a frame
        const uint64_t first = engine.FirstFrame();
        const uint64_t last = engine.LastFrame();
        const uint32_t span = engine.FrameSpan();
        if (last > first && span > 1 && options.seekSamples)
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

        // BM-7: save, then load into a fresh machine and seek once
        if (options.saveLoad)
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
            std::remove(file.c_str());
        }
    }

    // BM-8 on a fresh recording of the same start state
    if (options.dirtySweep)
    {
        Machine machine(c.config, r.error);
        if (!machine.Get() || !Prepare(*machine.Get(), c, options, r.error) ||
            !engine.Start(*machine.Get(), Mode::NoJournal, r.error))
            return r;
        DirtySweep(*machine.Get(), engine, m);
        engine.Stop();
    }

    // BM-1 last: it builds four more machines
    if (options.overhead && !Overhead(engine, c, options, m, r.error))
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

    // Turbo: ATM #FF77 bit 3 (the rest of the latch as the firmware left it)
    // and the Scorpion Turbo+ flip-flop (IN #7FFD)
    Configuration atm710Turbo = Base("ATM710-turbo", "ATM710");
    atm710Turbo.turbo = true;
    atm710Turbo.setup.push_back({SetupAction::Kind::AtmTurbo, 0, 0});
    Configuration scorpionTurbo = Base("SCORPION-turbo", "SCORPION");
    scorpionTurbo.turbo = true;
    scorpionTurbo.setup.push_back({SetupAction::Kind::PortIn, 0x7FFD, 0});
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
        add(scorpionTurbo, Idle(1500));
        add(Base("PENTAGON", "PENTAGON"), FromSnapshot("demo", "loaders/sna/7threality.sna", 1500));
        return cases;
    }

    // full: every base model with its default peripherals (BR-4) ...
    const Configuration bases[] = {Base("48K", "48K"),
                                   Base("128K", "128k"),
                                   Base("PENTAGON", "PENTAGON"),
                                   Base("PENTAGON512", "PENTAGON", 512),
                                   Base("PENTAGON1024", "PENTAGON", 1024),
                                   Base("PLUS3", "PLUS3"),
                                   Base("SCORPION", "SCORPION"),
                                   scorpionTurbo,
                                   Base("PROFSCORP", "PROFSCORP"),
                                   Base("ATM710", "ATM710"),
                                   atm710Turbo,
                                   atm3,
                                   Base("PROFI", "PROFI")};
    for (const Configuration& c : bases)
        add(c, Idle(3000));

    // ... workloads on the 128K-compatible Pentagon (BR-6) ...
    const Configuration pentagon = Base("PENTAGON", "PENTAGON");
    add(pentagon, GameWithInput(3000));
    add(pentagon, FromSnapshot("demo", "loaders/sna/7threality.sna", 3000));
    add(pentagon, FromSnapshot("demo2", "loaders/sna/across-the-edge-second.sna", 3000));
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
