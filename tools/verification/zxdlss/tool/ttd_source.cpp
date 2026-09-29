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
#include "debugger/ttd/timetravelmanager.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/emulatormanager.h"

namespace zxdlss
{

std::string readTtd(const std::string& path, const std::string& model, uint64_t from, uint64_t to, const FrameCallback& cb,
                    bool overscan)
{
    std::ifstream in(path, std::ios::binary);
    if (!in)
        return "cannot open " + path;

    EmulatorManager* manager = EmulatorManager::GetInstance();
    std::string error;
    std::shared_ptr<Emulator> emulator = manager->CreateEmulatorWithModel("zxdlss-render", model, LoggerLevel::LogError, &error);
    if (!emulator)
        return "cannot create a " + model + " emulator: " + error;
    struct Cleanup
    {
        EmulatorManager* m;
        std::string id;
        ~Cleanup() { m->RemoveEmulator(id); }
    } cleanup{manager, emulator->GetId()};

    // Plane B needs the per-T renderer (screenhq) and the zxdlss feature; the
    // TTD session needs time travel + debug mode (as ttdclipexport_test)
    FeatureManager* fm = emulator->GetFeatureManager();
    fm->setFeature(Features::kDebugMode, true);
    fm->setFeature(Features::kTimeTravel, true);
    fm->setFeature(Features::kScreenHQ, true);
    fm->setFeature(Features::kZXDLSS, true);

    ttd::TimeTravelManager* ttd = emulator->GetContext()->pTimeTravelManager;
    if (!ttd)
        return "the emulator has no time travel manager";
    ttd->SetSessionSourcePath(path);
    std::string err;
    if (!ttd->DeserializeSession(in, err))
        return "cannot load " + path + ": " + err;
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

/// The machine's sound of frames [from, to], played CONTINUOUSLY from `from`:
/// composing frame by frame restarts the sound path at every checkpoint
/// (clicks at frame boundaries - steps 3x the in-frame average). Position
/// once, then run the machine forward, as it ran when recorded. Exact for
/// recordings without input after `from` (demos, test programs); recorded
/// key presses are not re-applied by a plain run.
std::string readTtdAudio(const std::string& path, const std::string& model, uint64_t from, uint64_t to,
                         std::vector<int16_t>& samples, size_t& minPerFrame, size_t& maxPerFrame)
{
    std::ifstream in(path, std::ios::binary);
    if (!in)
        return "cannot open " + path;
    EmulatorManager* manager = EmulatorManager::GetInstance();
    std::string error;
    std::shared_ptr<Emulator> emulator = manager->CreateEmulatorWithModel("zxdlss-audio", model, LoggerLevel::LogError, &error);
    if (!emulator)
        return "cannot create a " + model + " emulator: " + error;
    struct Cleanup
    {
        EmulatorManager* m;
        std::string id;
        ~Cleanup() { m->RemoveEmulator(id); }
    } cleanup{manager, emulator->GetId()};
    FeatureManager* fm = emulator->GetFeatureManager();
    fm->setFeature(Features::kDebugMode, true);
    fm->setFeature(Features::kTimeTravel, true);

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

    ttd::TimeTravelManager* ttd = emulator->GetContext()->pTimeTravelManager;
    if (!ttd)
        return "the emulator has no time travel manager";
    ttd->SetSessionSourcePath(path);
    std::string err;
    if (!ttd->DeserializeSession(in, err))
        return "cannot load " + path + ": " + err;
    if (!ttd->SeekTo(ttd::TTDTimePoint{from, 0}))
        return "cannot position at frame " + std::to_string(from);
    pending.clear();
    minPerFrame = SIZE_MAX;
    maxPerFrame = 0;
    for (uint64_t f = from; f <= to; ++f)
    {
        emulator->RunNFrames(1, /*skipBreakpoints=*/true);
        if (pending.size() / 2 > 1200)           // ~903 per Pentagon frame
            std::fprintf(stderr, "audio: frame %llu gave %zu samples\n", static_cast<unsigned long long>(f),
                         pending.size() / 2);
        minPerFrame = std::min(minPerFrame, pending.size() / 2);
        maxPerFrame = std::max(maxPerFrame, pending.size() / 2);
        samples.insert(samples.end(), pending.begin(), pending.end());
        pending.clear();
    }
    return {};
}

/// Frame range of a TTD file (loads it once).
bool ttdRange(const std::string& path, const std::string& model, uint64_t& first, uint64_t& last, std::string& error)
{
    std::ifstream in(path, std::ios::binary);
    if (!in)
    {
        error = "cannot open " + path;
        return false;
    }
    EmulatorManager* manager = EmulatorManager::GetInstance();
    std::shared_ptr<Emulator> emulator = manager->CreateEmulatorWithModel("zxdlss-range", model, LoggerLevel::LogError, &error);
    if (!emulator)
        return false;
    FeatureManager* fm = emulator->GetFeatureManager();
    fm->setFeature(Features::kDebugMode, true);
    fm->setFeature(Features::kTimeTravel, true);
    ttd::TimeTravelManager* ttd = emulator->GetContext()->pTimeTravelManager;
    const bool ok = ttd && ttd->DeserializeSession(in, error);
    if (ok)
    {
        const ttd::TTDSessionInfo info = ttd->GetSessionInfo();
        first = info.sessionStartFrame;
        last = info.currentEndFrame;
    }
    manager->RemoveEmulator(emulator->GetId());
    return ok;
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

std::string readTtdAudio(const std::string&, const std::string&, uint64_t, uint64_t, std::vector<int16_t>&, size_t&, size_t&)
{
    return "built without the emulator core: no TTD audio";
}

}  // namespace zxdlss

#endif
