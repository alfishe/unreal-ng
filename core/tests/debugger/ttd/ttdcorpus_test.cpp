#include <gtest/gtest.h>

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <unordered_map>
#include <vector>

#include "_helpers/soundcardscope.h"
#include "_helpers/emulatortesthelper.h"
#include "_helpers/testpathhelper.h"
#include "base/featuremanager.h"
#include "debugger/ttd/timetravelmanager.h"
#include "debugger/ttd/ttdcheckpoint.h"
#include "debugger/ttd/ttdperipheralregistry.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/memory/memory.h"
#include "emulator/sound/soundmanager.h"

/// The recorded fixture corpus (testdata/ttd/, see its README) loaded the way
/// a user loads a session: into an instance that has its own history - here
/// it has been playing a TurboSound FM tune, so every TSFM field (ymfm engine,
/// render cursor, pending timed SSG writes, output stage) holds live state
/// the restore must replace. For every fixture:
///   - the session loads;
///   - seeking to a checkpoint restores every device exactly: each device
///     re-serializes to the recorded blob, byte for byte (the TSFM blob is
///     the 2008-byte v5 payload);
///   - replay is deterministic: from a restored checkpoint, running forward
///     reproduces the recorded checkpoints - CPU, chipset (including the
///     in-frame T-state) and every device blob.
/// A format change that is not followed by a re-recording fails here first.
/// Over the 50 ms budget (~1 s): five multi-megabyte sessions, each loaded,
/// restored at four points and replayed; this is the corpus's only C++ gate.

namespace
{
namespace fs = std::filesystem;

std::vector<fs::path> CorpusFiles()
{
    std::vector<fs::path> files;
    const fs::path dir = TestPathHelper::FindProjectRoot() / "testdata/ttd";
    if (fs::exists(dir))
        for (const auto& entry : fs::directory_iterator(dir))
            if (entry.path().extension() == ".ttd")
                files.push_back(entry.path());
    std::sort(files.begin(), files.end());
    return files;
}

std::unordered_map<uint8_t, std::vector<uint8_t>> Decoded(const std::unordered_map<uint8_t, std::vector<uint8_t>>& blobs)
{
    std::unordered_map<uint8_t, std::vector<uint8_t>> out;
    for (const auto& [id, blob] : blobs)
        out[id] = ttd::TTDPeripheralRegistry::DecodeBlob(id, blob);
    return out;
}
}  // namespace

class TTD_Corpus_Test : public ::testing::Test
{
protected:
    // Keep the General Sound / MoonSound cards the configs fit (the fixtures were recorded on the shipped configs, cards included):
    // declared first, so it is active before any machine is created
    SoundCardScope _soundCards;

protected:
    Emulator* _emulator = nullptr;
    EmulatorContext* _context = nullptr;
    ttd::TimeTravelManager* _ttd = nullptr;

    void SetUp() override
    {
        _emulator = EmulatorTestHelper::CreateStandardEmulator("PENTAGON", LoggerLevel::LogError);
        ASSERT_NE(_emulator, nullptr);
        _context = _emulator->GetContext();
        _ttd = _context->pTimeTravelManager;
        ASSERT_NE(_ttd, nullptr);
        FeatureManager* features = _emulator->GetFeatureManager();
        features->setFeature(Features::kDebugMode, true);
        features->setFeature(Features::kTimeTravel, true);
        // The corpus is recorded with the product defaults (see its README);
        // the test helper turns the HQ paths off for speed. Replay must run in
        // the recording's sound mode: HQ and LQ schedule the SSG generator
        // ticks differently (FIR decimator phase vs boxcar phase), so the TSFM
        // state diverges from the recording in the other mode
        features->setFeature(Features::kSoundHQ, true);
        features->setFeature(Features::kScreenHQ, true);
        _context->pMemory->UpdateFeatureCache();
        _context->pSoundManager->UpdateFeatureCache();
    }

    void TearDown() override
    {
        if (_emulator)
        {
            EmulatorTestHelper::CleanupEmulator(_emulator);
            _emulator = nullptr;
        }
    }

    /// Live history before the load: a TSFM tune playing for a while
    void PlayTsfmTune(int frames)
    {
        const auto sna = TestPathHelper::FindProjectRoot() / "testdata/sound/tsfm/tech_support.sna";
        ASSERT_TRUE(_emulator->LoadSnapshot(sna.string())) << sna;
        _emulator->RunNFrames(frames, /*skipBreakpoints=*/true);
    }

    /// Every device of the live machine, as the registry serializes it
    std::unordered_map<uint8_t, std::vector<uint8_t>> LiveBlobs() const
    {
        std::unordered_map<uint8_t, std::vector<uint8_t>> blobs;
        _ttd->GetPeripheralRegistry().CaptureAll(blobs);
        return Decoded(blobs);
    }

    /// A checkpoint's machine state: CPU, chipset, every device (decoded)
    struct MachineState
    {
        ttd::TTDCpuState cpu;
        ttd::TTDChipsetState chipset;
        std::unordered_map<uint8_t, std::vector<uint8_t>> devices;
    };

    static MachineState StateOf(const ttd::TTDCheckpoint& cp)
    {
        return {cp.cpu, cp.chipset, Decoded(cp.peripheralBlobs)};
    }

    /// The live machine as a checkpoint would capture it
    MachineState LiveState() const
    {
        const Z80* z80 = _context->pCore->GetZ80();
        std::unordered_map<uint8_t, std::vector<uint8_t>> blobs;
        _ttd->GetPeripheralRegistry().CaptureAll(blobs);
        return {ttd::CaptureCpuState(*static_cast<const Z80State*>(z80)),
                ttd::CaptureChipsetState(_context->emulatorState, static_cast<uint32_t>(z80->t)), Decoded(blobs)};
    }

    static void ExpectSame(const MachineState& actual, const MachineState& expected, const std::string& where)
    {
        EXPECT_EQ(std::memcmp(&actual.cpu, &expected.cpu, sizeof(actual.cpu)), 0) << where << ": CPU state differs";
        EXPECT_EQ(std::memcmp(&actual.chipset, &expected.chipset, sizeof(actual.chipset)), 0)
            << where << ": chipset differs (cpu_t_in_frame " << ttd::GetChipsetCpuTInFrame(actual.chipset)
            << " vs " << ttd::GetChipsetCpuTInFrame(expected.chipset) << ")";
        for (const auto& [id, blob] : expected.devices)
        {
            const auto it = actual.devices.find(id);
            ASSERT_NE(it, actual.devices.end()) << where << ": device " << int(id) << " missing";
            ASSERT_EQ(it->second.size(), blob.size())
                << where << ": device " << int(id) << " blob size differs - the fixture predates a format change, "
                << "re-record it (testdata/ttd/README.md)";
            size_t first = 0;
            while (first < blob.size() && it->second[first] == blob[first])
                first++;
            EXPECT_EQ(first, blob.size()) << where << ": device " << int(id) << " differs from byte " << first;
        }
        EXPECT_EQ(actual.devices.size(), expected.devices.size()) << where << ": device sets differ";
    }
};

TEST_F(TTD_Corpus_Test, EveryFixtureLoadsRestoresAndReplaysExactly)
{
    const auto files = CorpusFiles();
    ASSERT_GE(files.size(), 5u) << "testdata/ttd/ should hold the recorded corpus";

    for (const fs::path& file : files)
    {
        SCOPED_TRACE(file.filename().string());

        // A machine with its own TSFM history, then the user's load
        PlayTsfmTune(60);
        std::ifstream in(file, std::ios::binary);
        std::string err;
        ASSERT_TRUE(_ttd->DeserializeSession(in, err)) << err;
        const size_t count = _ttd->GetCheckpointCount();
        ASSERT_GE(count, 60u);
        EXPECT_NE(_ttd->GetCheckpoint(0)->peripheralBlobs.count(uint8_t(ttd::PeripheralId::TSFM)), 0u)
            << "the Pentagon TurboSound slot is TSFM: its blob must be in every checkpoint";

        // Restore: first, a keyframe, a delta frame, the last
        for (const size_t idx : {size_t(0), size_t(50), size_t(51), count - 1})
        {
            const ttd::TTDCheckpoint* cp = _ttd->GetCheckpoint(idx);
            ASSERT_NE(cp, nullptr);
            ASSERT_TRUE(_ttd->SeekTo({cp->time.frame, 0})) << "seek to checkpoint " << idx;
            ExpectSame(LiveState(), StateOf(*cp), "restore of checkpoint " + std::to_string(idx));
        }

        // Replay: from a restored delta frame, record forward and compare the
        // new checkpoints - captured at the same point of the frame as the
        // original ones - with the recorded 25 that followed it
        const size_t from = 37;
        constexpr size_t kReplay = 25;
        std::vector<MachineState> expected;
        for (size_t step = 1; step <= kReplay; step++)
            expected.push_back(StateOf(*_ttd->GetCheckpoint(from + step)));
        const ttd::TTDTimePoint start{_ttd->GetCheckpoint(from)->time.frame, 0};
        ASSERT_TRUE(_ttd->SeekTo(start));
        ASSERT_TRUE(_ttd->ResumeRecordingFrom(start));
        _emulator->RunNFrames(kReplay, /*skipBreakpoints=*/true);
        _ttd->StopRecording();
        ASSERT_GE(_ttd->GetCheckpointCount(), from + 1 + kReplay);
        for (size_t step = 1; step <= kReplay; step++)
        {
            ExpectSame(StateOf(*_ttd->GetCheckpoint(from + step)), expected[step - 1],
                       "replay " + std::to_string(from) + " + " + std::to_string(step));
            if (HasFailure())
                break;  // the first divergence is the useful one
        }
        if (HasFailure())
            return;
    }
}
