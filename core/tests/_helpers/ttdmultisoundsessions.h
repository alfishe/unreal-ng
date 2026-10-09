#pragma once

/// @file ttdmultisoundsessions.h
/// @brief The ZX-MultiSound sessions of the TTD corpus, recorded in the test process instead of stored.
///
/// The corpus tests (TTD_Corpus_Test, TimeTravelControllerCorpus_Test, TTDSessionFile_Test, TTDV1Feeder_Test) read
/// recorded sessions. The two ZX-MultiSound ones are not committed (owner decision 2026-10-07: ~19 MB of fixtures):
/// the tests record them on first use, the way `tools/verification/ttd-analyzer/scripts/record_fixtures.py` recorded
/// the files it replaces, into the process's scratch folder:
///   - a fresh machine of the model with the card in ZX-bus slot 1 (the create request's slot set, which replaces the
///     shipped config's: the socket keeps its plain AY / YM2149) and the shipped default MIDI bank;
///   - the recorder's sound configuration pinned: 44.1 kHz core rate, Sound HQ and Screen HQ on, one frame to apply;
///   - `testdata/sound/multisound/ttd/allsources.sna` (every source of the card), 10 settle frames;
///   - a recording with the write journal, 70 frames, saved to `multisound-<machine>-<recorder>.ttd`.
/// A test then loads the file into a machine built from the file's header (ttdslotcards.h), exactly as it loads a
/// stored fixture. Each session is recorded once per process (shared by every test of the process; test-parallel
/// runs each shard in its own process) and deleted when the process ends. Over the 50 ms budget: about 0.35 s per
/// session (a real machine with the card's GS firmware and synthesizer, the 30 MB default bank loaded, 81 frames), so
/// about 0.7 s per recorder and process, paid by the multi-second corpus tests that use them.
///
/// Example: `for (const auto& file : ttdtest::MultiSoundSessions(ttdtest::SessionRecorder::V1)) Load(file);`

#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "_helpers/emulatortesthelper.h"
#include "_helpers/soundcardscope.h"
#include "_helpers/testpathhelper.h"
#include "_helpers/ttdslotcards.h"
#include "base/featuremanager.h"
#include "debugger/ttd/timetravelcontroller.h"
#include "debugger/ttd/timetravelmanager.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/memory/memory.h"
#include "emulator/sound/soundmanager.h"

namespace ttdtest
{

/// The implementation that records a generated session (its file format: v1's schema 1 or the engine's schema 2)
enum class SessionRecorder
{
    V1,
    Engine,
};

/// The two machines the corpus records the card on: name, model
inline const std::vector<std::pair<std::string, std::string>>& MultiSoundSessionMachines()
{
    static const std::vector<std::pair<std::string, std::string>> machines = {
        {"multisound-pentagon", "PENTAGON"},
        {"multisound-zxevo", "ATM3"},   // the ZX-Evo starts at 7 MHz; the YM2149 leaves its socket for the card
    };
    return machines;
}

/// Records one session (see the file comment) into @p path; false with @p error when a step fails
inline bool RecordMultiSoundSession(const std::string& model, SessionRecorder recorder, const std::filesystem::path& path,
                                    std::string& error)
{
    constexpr int kSettleFrames = 10;     // the synthesizer's 50 ms boot window passes
    constexpr int kRecordFrames = 70;     // 71 checkpoints: the corpus tests need 63
    constexpr uint32_t kCoreRate = 44100; // the recorder's pinned rate (the replay must run at it)

    // The machine a test builds for the session (CreateRecordedMachine) is built from the same description, with
    // every sound device as configured (the corpus tests hold a SoundCardScope for all of them)
    ttd::TTDRecordedMachine machine;
    machine.model = model;
    machine.peripheralMask = uint64_t{1} << static_cast<uint8_t>(ttd::PeripheralId::MultiSound);
    SoundCardScope everySound;

    const Emulator::TimeTravelBackend previous = Emulator::DefaultTimeTravelBackend();
    Emulator::SetDefaultTimeTravelBackend(recorder == SessionRecorder::V1 ? Emulator::TimeTravelBackend::V1
                                                                         : Emulator::TimeTravelBackend::Engine);
    Emulator* emulator = CreateRecordedMachine(machine, error, /*fitGeneralSound=*/false);
    Emulator::SetDefaultTimeTravelBackend(previous);
    if (!emulator)
    {
        error = "cannot build a " + model + " with the ZX-MultiSound: " + error;
        return false;
    }

    bool ok = false;
    EmulatorContext* context = emulator->GetContext();
    FeatureManager* features = emulator->GetFeatureManager();
    features->setFeature(Features::kDebugMode, true);
    features->setFeature(Features::kTimeTravel, true);
    features->setFeature(Features::kSoundHQ, true);
    features->setFeature(Features::kScreenHQ, true);
    context->pMemory->UpdateFeatureCache();
    context->pSoundManager->UpdateFeatureCache();
    context->pSoundManager->setCoreRatePin(kCoreRate);
    emulator->RunNFrames(1);   // the rate applies at a frame boundary

    const std::filesystem::path sna =
        TestPathHelper::FindProjectRoot() / "testdata" / "sound" / "multisound" / "ttd" / "allsources.sna";
    if (context->pSoundManager->getCoreRate() != kCoreRate)
        error = "the core rate did not change to 44100";
    else if (!emulator->LoadSnapshot(sna.string()))
        error = "cannot load " + sna.string();
    else
    {
        emulator->RunNFrames(kSettleFrames);
        std::ofstream out(path, std::ios::binary);
        auto record = [&](auto& session)
        {
            session.SetEnableWriteJournal(true);
            if (!session.StartRecording())
            {
                error = "the recording did not start";
                return false;
            }
            emulator->RunNFrames(kRecordFrames);
            session.StopRecording();
            return out.is_open() && session.SerializeSession(out, error);
        };
        if (recorder == SessionRecorder::V1)
            ok = record(*context->pTimeTravelManager);
        else if (context->pTimeTravelController)
            ok = record(*context->pTimeTravelController);
        else
            error = "the machine has no engine recorder";
        if (!ok && error.empty())
            error = "cannot write " + path.string();
    }
    EmulatorTestHelper::CleanupEmulator(emulator);
    return ok;
}

/// The ZX-MultiSound sessions of @p recorder, recorded on the first call of the process and deleted when it ends.
/// A failed recording fails the calling test (and is not retried by later calls)
inline std::vector<std::filesystem::path> MultiSoundSessions(SessionRecorder recorder)
{
    /// The files of the process, deleted at its end (the process scratch folder goes too; this keeps the rule
    /// explicit when UNREAL_TEST_KEEP_SCRATCH keeps the folder)
    struct Cache
    {
        std::mutex lock;
        std::map<SessionRecorder, std::vector<std::filesystem::path>> files;
        std::map<SessionRecorder, std::string> errors;
        ~Cache()
        {
            for (const auto& [kind, paths] : files)
                for (const std::filesystem::path& path : paths)
                {
                    std::error_code ignored;
                    std::filesystem::remove(path, ignored);
                }
        }
    };
    static Cache cache;

    std::lock_guard<std::mutex> guard(cache.lock);
    if (!cache.files.count(recorder) && !cache.errors.count(recorder))
    {
        std::vector<std::filesystem::path> paths;
        std::string error;
        for (const auto& [name, model] : MultiSoundSessionMachines())
        {
            const std::filesystem::path path = TestPathHelper::GetUniqueTestScratchPath(
                name + (recorder == SessionRecorder::V1 ? "-v1.ttd" : "-engine.ttd"));
            if (!RecordMultiSoundSession(model, recorder, path, error))
            {
                std::error_code ignored;
                std::filesystem::remove(path, ignored);
                break;
            }
            paths.push_back(path);
        }
        if (error.empty())
            cache.files[recorder] = paths;
        else
        {
            for (const std::filesystem::path& path : paths)
            {
                std::error_code ignored;
                std::filesystem::remove(path, ignored);
            }
            cache.errors[recorder] = error;
        }
    }
    if (cache.errors.count(recorder))
    {
        ADD_FAILURE() << "recording the ZX-MultiSound sessions: " << cache.errors[recorder];
        return {};
    }
    return cache.files[recorder];
}

}  // namespace ttdtest
