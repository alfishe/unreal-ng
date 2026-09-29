/// @file ttd_source.cpp
/// @brief Frames of a TTD file, replayed through the emulator core
/// (TimeTravelManager::VisitComposedFrames). Built only with the core.

#include "frames.h"

#ifdef ZXDLSS_WITH_CORE

#include <cstring>
#include <fstream>

#include "base/featuremanager.h"
#include "debugger/ttd/timetravelmanager.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/emulatormanager.h"

namespace zxdlss
{

std::string readTtd(const std::string& path, const std::string& model, uint64_t from, uint64_t to, const FrameCallback& cb)
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
        f.planeB.assign(c.planeB, c.planeB + c.planeBCount);
        stopped = !cb(f);
        return !stopped;
    });
    if (!err.empty())
        return err;
    return walk;
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

std::string readTtd(const std::string&, const std::string&, uint64_t, uint64_t, const FrameCallback&)
{
    return "built without the emulator core: TTD input is not available (use --clip)";
}

bool ttdRange(const std::string&, const std::string&, uint64_t&, uint64_t&, std::string& error)
{
    error = "built without the emulator core";
    return false;
}

}  // namespace zxdlss

#endif
