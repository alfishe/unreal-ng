/// @file ttd_source.cpp
/// @brief Frames of a TTD file, replayed through the emulator core
/// (TimeTravelManager::VisitComposedFrames). Built only with the core.

#include "frames.h"

#ifdef ZXDLSS_WITH_CORE

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>

#include "base/featuremanager.h"
#include "debugger/analyzers/analyzermanager.h"
#include "debugger/debugmanager.h"
#include "common/filehelper.h"
#include "debugger/ttd/timetravelmanager.h"
#include "debugger/ttd/ttdfileinfo.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/emulatormanager.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/memory/memory.h"
#include "emulator/sound/soundmanager.h"

namespace zxdlss
{

namespace
{

/// An emulator for the session in `path`, built as the file says it was
/// recorded (ttd::ReadTTDFileInfo, headers only): its model and its General
/// Sound card, fitted before the load - the loader refuses another card. The
/// session is loaded; `model` non-empty must name the recorded model.
std::string createSessionEmulator(const std::string& path, const std::string& model, const std::string& id,
                                  std::shared_ptr<Emulator>& emulator)
{
    ttd::TTDFileInfo info;
    std::string err;
    if (!ttd::ReadTTDFileInfo(path, info, err))
        return "cannot read " + path + ": " + err;
    const std::string& recorded = info.machine.model;
    if (recorded.empty())
        return path + ": recorded on an unknown model (id " + std::to_string(info.machine.modelId) + ")";
    if (!model.empty() && model != recorded)
        return path + " was recorded on " + recorded + ", not " + model;

    EmulatorManager* manager = EmulatorManager::GetInstance();
    emulator = manager->CreateEmulatorWithModel(id, recorded, LoggerLevel::LogError, &err);
    if (!emulator)
        return "cannot create a " + recorded + " emulator: " + err;

    // Plane B needs the per-T renderer (screenhq) and the zxdlss feature; the
    // TTD session needs time travel + debug mode (as ttdclipexport_test)
    FeatureManager* fm = emulator->GetFeatureManager();
    fm->setFeature(Features::kDebugMode, true);
    fm->setFeature(Features::kTimeTravel, true);

    if (info.machine.generalSound != GSTypeKind::NONE)
    {
        SoundManager* sound = emulator->GetContext()->pSoundManager;
        if (!sound || !sound->switchGeneralSoundCard(info.machine.generalSound))
            return std::string("cannot fit the recorded General Sound card (") +
                   ttd::GeneralSoundName(info.machine.generalSound) + ")";
    }

    ttd::TimeTravelManager* ttd = emulator->GetContext()->pTimeTravelManager;
    if (!ttd)
        return "the emulator has no time travel manager";
    std::ifstream in(FileHelper::ToFsPath(path), std::ios::binary);
    if (!in)
        return "cannot open " + path;
    if (!ttd->DeserializeSession(in, err))
        return "cannot load " + path + ": " + err;
    ttd->SetSessionSourcePath(path);
    return {};
}

}  // namespace

std::string readTtd(const std::string& path, const std::string& model, uint64_t from, uint64_t to, const FrameCallback& cb,
                    bool overscan)
{
    EmulatorManager* manager = EmulatorManager::GetInstance();
    std::shared_ptr<Emulator> emulator;
    struct Cleanup
    {
        EmulatorManager* m;
        std::shared_ptr<Emulator>& e;
        ~Cleanup()
        {
            if (e)
                m->RemoveEmulator(e->GetId());
        }
    } cleanup{manager, emulator};
    std::string err = createSessionEmulator(path, model, "zxdlss-render", emulator);
    if (!err.empty())
        return err;

    FeatureManager* fm = emulator->GetFeatureManager();
    fm->setFeature(Features::kScreenHQ, true);
    fm->setFeature(Features::kZXDLSS, true);
    ttd::TimeTravelManager* ttd = emulator->GetContext()->pTimeTravelManager;
    if (overscan && !emulator->SetOverscanMode(true) && !emulator->IsOverscanMode())
        return "overscan needs a Pentagon session";

    SourceFrame f;
    bool stopped = false;
    const std::string walk = ttd->VisitComposedFrames(from, to, [&](const ttd::TimeTravelManager::TTDComposedFrame& c) {
        if (!c.planeB)
        {
            err = "no plane B from the renderer";
            return false;
        }
        f.frame = c.frame;
        f.width = static_cast<int>(c.width);
        f.height = static_cast<int>(c.height);
        f.paperX = 48;
        f.paperY = 48;
        if (overscan)
        {
            // M_P384: 48 left border, 256 paper, 80 right border; 56 / 192 / 56 lines.
            // Keep 48 px on the right as on the left (Symmetric Horizontal)
            if (c.width != 384 || c.height != 304)
            {
                err = "overscan: the renderer gave " + std::to_string(c.width) + "x" + std::to_string(c.height) +
                      ", expected 384x304";
                return false;
            }
            f.width = 352;
            f.paperY = 56;
            f.planeB.resize(static_cast<size_t>(f.width) * f.height);
            for (int y = 0; y < f.height; ++y)
                std::copy(c.planeB + static_cast<size_t>(y) * c.width, c.planeB + static_cast<size_t>(y) * c.width + f.width,
                          f.planeB.begin() + static_cast<ptrdiff_t>(static_cast<size_t>(y) * f.width));
        }
        else
            f.planeB.assign(c.planeB, c.planeB + c.planeBCount);
        stopped = !cb(f);
        return !stopped;
    });
    if (!err.empty())
        return err;
    return walk;
}

namespace
{

/// An emulator that has loaded a TTD session, removed on destruction.
struct LoadedSession
{
    EmulatorManager* manager = nullptr;
    std::shared_ptr<Emulator> emulator;
    ttd::TimeTravelManager* ttd = nullptr;
    ~LoadedSession()
    {
        if (emulator)
            manager->RemoveEmulator(emulator->GetId());
    }
    std::string load(const std::string& path, const std::string& model, const char* id)
    {
        manager = EmulatorManager::GetInstance();
        const std::string err = createSessionEmulator(path, model, id, emulator);
        if (!err.empty())
            return err;
        ttd = emulator->GetContext()->pTimeTravelManager;
        return {};
    }
};

/// FNV-1a of the 8 RAM pages (128K) and the CPU's PC / SP: equal states of two
/// emulators at the same position.
uint64_t machineHash(Emulator& emulator)
{
    uint64_t h = 1469598103934665603ull;
    Memory* mem = emulator.GetContext()->pMemory;
    for (uint16_t page = 0; page < 8; ++page)
    {
        const uint8_t* p = mem->RAMPageAddress(page);
        for (int i = 0; i < 0x4000; ++i)
            h = (h ^ p[i]) * 1099511628211ull;
    }
    const Z80* z = emulator.GetContext()->pCore->GetZ80();
    h = (h ^ z->pc) * 1099511628211ull;
    h = (h ^ z->sp) * 1099511628211ull;
    return h;
}

}  // namespace

/// The machine's sound of frames [from, to], played CONTINUOUSLY from `from`:
/// composing frame by frame restarts the sound path at every checkpoint
/// (clicks at frame boundaries - steps 3x the in-frame average).
///
/// - Steps are frame-aligned (Emulator::RunFrame keeps the intra-frame
///   position; RunNFrames(1) runs a T-state budget and drifts by the last
///   instruction's overrun - over ~10000 frames a whole frame, which gave one
///   call two frames of sound).
/// - The run is checked against the recording every kCheckEvery frames: a
///   second emulator seeks to the same position and the RAM + PC / SP hashes
///   are compared. A recording can hold outside changes it did not journal
///   (the TR-DOS autostart hook rewrites RUN "boot" into RUN "<name>" in RAM;
///   without it the continuous run boots another program and stays silent).
///   On a mismatch the run is re-positioned onto the recording (one
///   discontinuity in the sound) and the frame is reported in `resyncs`.
std::string readTtdAudio(const std::string& path, const std::string& model, uint64_t from, uint64_t to,
                         std::vector<int16_t>& samples, size_t& minPerFrame, size_t& maxPerFrame,
                         std::vector<uint64_t>* resyncs)
{
    constexpr uint64_t kCheckEvery = 25;
    LoadedSession run, ref;
    std::string e = run.load(path, model, "zxdlss-audio");
    if (e.empty())
        e = ref.load(path, model, "zxdlss-audio-ref");
    if (!e.empty())
        return e;
    Emulator* emulator = run.emulator.get();

    std::vector<int16_t> pending;
    AnalyzerManager* analyzers = emulator->GetDebugManager() ? emulator->GetDebugManager()->GetAnalyzerManager() : nullptr;
    if (!analyzers)
        return "the emulator has no analyzer manager (needed for the audio tap)";
    analyzers->setEnabled(true);
    const auto tap = analyzers->subscribeAudioSample(
        [&pending](int16_t left, int16_t right) {
            pending.push_back(left);
            pending.push_back(right);
        },
        "zxdlss-render-audio");
    struct Unsubscribe
    {
        AnalyzerManager* m;
        CallbackId id;
        ~Unsubscribe() { m->unsubscribe(id); }
    } unsubscribe{analyzers, tap};

    if (!run.ttd->SeekTo(ttd::TTDTimePoint{from, 0}))
        return "cannot position at frame " + std::to_string(from);
    pending.clear();
    minPerFrame = SIZE_MAX;
    maxPerFrame = 0;
    for (uint64_t f = from; f <= to; ++f)
    {
        emulator->RunFrame(/*skipBreakpoints=*/true);
        if (pending.size() / 2 > 1200)           // ~903 per Pentagon frame
            std::fprintf(stderr, "audio: frame %llu gave %zu samples\n", static_cast<unsigned long long>(f),
                         pending.size() / 2);
        minPerFrame = std::min(minPerFrame, pending.size() / 2);
        maxPerFrame = std::max(maxPerFrame, pending.size() / 2);
        samples.insert(samples.end(), pending.begin(), pending.end());
        pending.clear();
        if ((f + 1 - from) % kCheckEvery == 0 && f < to)
        {
            const ttd::TTDTimePoint here = run.ttd->CurrentPosition();
            if (ref.ttd->SeekTo(here) && machineHash(*ref.emulator) != machineHash(*emulator))
            {
                run.ttd->SeekTo(here);
                pending.clear();                     // the seek's own replay is not this frame's sound
                if (resyncs)
                    resyncs->push_back(here.frame);
            }
        }
    }
    return {};
}

/// Frame range of a TTD file, from its header (no emulator).
bool ttdRange(const std::string& path, const std::string& model, uint64_t& first, uint64_t& last, std::string& error)
{
    ttd::TTDFileInfo info;
    if (!ttd::ReadTTDFileInfo(path, info, error))
        return false;
    if (!model.empty() && model != info.machine.model)
    {
        error = path + " was recorded on " + info.machine.model + ", not " + model;
        return false;
    }
    first = info.startFrame;
    last = info.endFrame;
    return true;
}

}  // namespace zxdlss

#else

namespace zxdlss
{

std::string readTtd(const std::string&, const std::string&, uint64_t, uint64_t, const FrameCallback&, bool)
{
    return "built without the emulator core: TTD input is not available (use --clip)";
}

bool ttdRange(const std::string&, const std::string&, uint64_t&, uint64_t&, std::string& error)
{
    error = "built without the emulator core";
    return false;
}

std::string readTtdAudio(const std::string&, const std::string&, uint64_t, uint64_t, std::vector<int16_t>&, size_t&, size_t&,
                         std::vector<uint64_t>*)
{
    return "built without the emulator core: no TTD audio";
}

}  // namespace zxdlss

#endif
