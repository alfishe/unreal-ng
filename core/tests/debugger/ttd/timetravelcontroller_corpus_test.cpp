/// @file timetravelcontroller_corpus_test.cpp
/// @brief The fixture corpus recorded by the engine (testdata/ttd/engine, the
/// application's recorder since the Phase 5 switch) loaded the way a user
/// loads a session, into an instance of the recorded model with the engine's
/// controller as its recorder. For every fixture:
///   - the session loads, and every checkpoint the test visits restores with
///     no damage or configuration difference reported;
///   - the loaded session continues: resumed at checkpoint 37, the 25 frames
///     recorded live reproduce the recorded ones - seeking to each new
///     checkpoint shows the CPU, chipset, every device and all of RAM that
///     seeking to the recorded one showed.
/// The ZX-MultiSound sessions (multisound-pentagon, multisound-zxevo) are not
/// stored: the test records them with the engine in its own process first
/// (ttdmultisoundsessions.h) and loads them like the stored ones, on a machine
/// with the card in ZX-bus slot 1 (ttdslotcards.h).
/// Over the 50 ms budget (~8 s): nine sessions (two of them recorded here
/// first), each loaded, seeked 55 times and replayed; this is the engine
/// corpus's only C++ gate.

#include <gtest/gtest.h>

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "_helpers/emulatortesthelper.h"
#include "_helpers/gsslot.h"
#include "_helpers/soundcardscope.h"
#include "_helpers/testpathhelper.h"
#include "_helpers/ttdmultisoundsessions.h"
#include "_helpers/ttdslotcards.h"
#include "base/featuremanager.h"
#include "debugger/ttd/timetravelcontroller.h"
#include "debugger/ttd/timetravelmanager.h"
#include "debugger/ttd/ttdfileinfo.h"
#include "debugger/ttd/ttdperipheralregistry.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/memory/memory.h"
#include "emulator/sound/soundmanager.h"

namespace
{
namespace fs = std::filesystem;

std::vector<fs::path> EngineCorpus()
{
    std::vector<fs::path> files;
    const fs::path dir = TestPathHelper::FindProjectRoot() / "testdata" / "ttd" / "engine";
    if (fs::exists(dir))
        for (const auto& entry : fs::directory_iterator(dir))
            if (entry.path().extension() == ".ttd")
                files.push_back(entry.path());
    std::sort(files.begin(), files.end());
    // The ZX-MultiSound sessions, recorded by the engine in this process
    for (const fs::path& recorded : ttdtest::MultiSoundSessions(ttdtest::SessionRecorder::Engine))
        files.push_back(recorded);
    return files;
}

/// The live machine: CPU, chipset, every device (decoded) and every RAM page's hash
struct LiveMachine
{
    ttd::TTDCpuState cpu;
    ttd::TTDChipsetState chipset;
    std::unordered_map<uint8_t, std::vector<uint8_t>> devices;
    std::vector<uint64_t> ram;
};

uint64_t Fnv1a(const uint8_t* data, size_t size)
{
    uint64_t h = 14695981039346656037ull;
    for (size_t i = 0; i < size; ++i)
        h = (h ^ data[i]) * 1099511628211ull;
    return h;
}
}  // namespace

class TimeTravelControllerCorpus_Test : public ::testing::Test
{
protected:
    // The fixtures were recorded with the shipped configs' cards (see testdata/ttd/README.md)
    SoundCardScope _soundCards;
    Emulator* _emulator = nullptr;
    EmulatorContext* _context = nullptr;
    std::unique_ptr<ttd::TimeTravelController> _controller;

    /// A fresh machine of the fixture's recorded machine: its General Sound card and its slot-built cards
    /// (ttdslotcards.h)
    void StartMachine(const ttd::TTDRecordedMachine& machine)
    {
        StopMachine();
        std::string why;
        _emulator = ttdtest::CreateRecordedMachine(machine, why, /*fitGeneralSound=*/false);
        ASSERT_NE(_emulator, nullptr) << why;
        _context = _emulator->GetContext();
        FeatureManager* features = _emulator->GetFeatureManager();
        features->setFeature(Features::kDebugMode, true);
        features->setFeature(Features::kTimeTravel, true);
        // Replay runs in the recording's modes (testdata/ttd/README.md: what the recorder fixes)
        features->setFeature(Features::kSoundHQ, true);
        features->setFeature(Features::kScreenHQ, true);
        _context->pMemory->UpdateFeatureCache();
        _context->pSoundManager->UpdateFeatureCache();
        ASSERT_TRUE(FitGeneralSoundCard(_context->pSoundManager, machine.generalSound));
        _controller = std::make_unique<ttd::TimeTravelController>(_context);
        _context->pTimeTravelHooks = _controller.get();
        _context->ttdWriteSink = _controller.get();
        _context->pTimeTravelController = _controller.get();
    }

    void StopMachine()
    {
        if (!_emulator)
            return;
        if (_controller)
            _controller->StopRecording();
        _context->pTimeTravelHooks = _context->pTimeTravelManager;
        _context->ttdWriteSink = _context->pTimeTravelManager;
        _context->pTimeTravelController = nullptr;
        _controller.reset();
        EmulatorTestHelper::CleanupEmulator(_emulator);
        _emulator = nullptr;
    }

    void TearDown() override { StopMachine(); }

    LiveMachine Live() const
    {
        LiveMachine m;
        const Z80* z80 = _context->pCore->GetZ80();
        m.cpu = ttd::CaptureCpuState(*static_cast<const Z80State*>(z80));
        m.chipset = ttd::CaptureChipsetState(_context->emulatorState, static_cast<uint32_t>(z80->t));
        std::unordered_map<uint8_t, std::vector<uint8_t>> blobs;
        _controller->GetPeripheralRegistry().CaptureAll(blobs);
        for (const auto& [id, blob] : blobs)
            m.devices[id] = ttd::TTDPeripheralRegistry::DecodeBlob(id, blob);
        const ttd::TTDSessionInfo info = _controller->GetSessionInfo();
        for (uint16_t page = 0; page < info.modelRamPages; ++page)
            if (const uint8_t* bytes = _context->pMemory->RAMPageAddress(page))
                m.ram.push_back(Fnv1a(bytes, 0x4000));
        return m;
    }

    static void ExpectSame(const LiveMachine& actual, const LiveMachine& expected, const std::string& where)
    {
        {
            const auto* a = reinterpret_cast<const uint8_t*>(&actual.cpu);
            const auto* e = reinterpret_cast<const uint8_t*>(&expected.cpu);
            size_t first = 0;
            while (first < sizeof(actual.cpu) && a[first] == e[first])
                ++first;
            EXPECT_EQ(first, sizeof(actual.cpu)) << where << ": CPU differs from byte " << first << " (pc "
                                                 << actual.cpu.pc << " vs " << expected.cpu.pc << ", T in frame "
                                                 << ttd::GetChipsetCpuTInFrame(actual.chipset) << " vs "
                                                 << ttd::GetChipsetCpuTInFrame(expected.chipset) << ")";
        }
        EXPECT_EQ(std::memcmp(&actual.chipset, &expected.chipset, sizeof(actual.chipset)), 0)
            << where << ": chipset (cpu_t_in_frame " << ttd::GetChipsetCpuTInFrame(actual.chipset) << " vs "
            << ttd::GetChipsetCpuTInFrame(expected.chipset) << ")";
        EXPECT_EQ(actual.devices.size(), expected.devices.size()) << where << ": device sets";
        for (const auto& [id, state] : expected.devices)
        {
            const auto it = actual.devices.find(id);
            ASSERT_NE(it, actual.devices.end()) << where << ": device " << int(id) << " missing";
            size_t first = 0;
            while (first < state.size() && first < it->second.size() && it->second[first] == state[first])
                ++first;
            EXPECT_TRUE(it->second == state) << where << ": device " << int(id) << " differs from byte " << first;
        }
        ASSERT_EQ(actual.ram.size(), expected.ram.size()) << where;
        for (size_t page = 0; page < expected.ram.size(); ++page)
            ASSERT_EQ(actual.ram[page], expected.ram[page]) << where << ": RAM page " << page;
    }

    /// Seek to checkpoint @p index's frame start; the restore reports no damage or difference
    void SeekToCheckpoint(size_t index)
    {
        const ttd::TTDCheckpoint* cp = _controller->GetCheckpoint(index);
        ASSERT_NE(cp, nullptr) << "checkpoint " << index;
        ttd::TTDSeekResult result;
        ASSERT_TRUE(_controller->SeekTo({cp->time.frame, 0}, &result)) << "seek to checkpoint " << index;
        const ttd::TTDRestoreResult& check = _controller->LastEngineCheck();
        EXPECT_EQ(check.status, ttd::TTDRestoreStatus::Exact) << "checkpoint " << index << ": " << check.message;
    }
};

TEST_F(TimeTravelControllerCorpus_Test, EveryFixtureLoadsRestoresAndContinuesExactly)
{
    const std::vector<fs::path> files = EngineCorpus();
    ASSERT_GE(files.size(), 9u) << "testdata/ttd/engine/ should hold the engine-recorded corpus";

    for (const fs::path& file : files)
    {
        SCOPED_TRACE(file.filename().string());
        ttd::TTDFileInfo info;
        std::string err;
        ASSERT_TRUE(ttd::ReadTTDFileInfo(file.string(), info, err)) << err;
        ASSERT_EQ(info.schemaVersion, 2u) << "the engine's format";
        ASSERT_NO_FATAL_FAILURE(StartMachine(info.machine));

        std::ifstream in(file, std::ios::binary);
        ASSERT_TRUE(_controller->DeserializeSession(in, err)) << err;
        const size_t count = _controller->GetCheckpointCount();
        ASSERT_GE(count, 63u);

        // Restore: the first, a segment's inside, the last
        for (const size_t index : {size_t(0), size_t(50), size_t(51), count - 1})
            ASSERT_NO_FATAL_FAILURE(SeekToCheckpoint(index));

        // What the recording shows at checkpoints 38..62, then the same frames run live
        // from checkpoint 37 of the loaded session
        constexpr size_t kFrom = 37;
        constexpr size_t kReplay = 25;
        std::vector<LiveMachine> recorded;
        for (size_t step = 1; step <= kReplay; ++step)
        {
            ASSERT_NO_FATAL_FAILURE(SeekToCheckpoint(kFrom + step));
            recorded.push_back(Live());
        }
        const ttd::TTDTimePoint start{_controller->GetCheckpoint(kFrom)->time.frame, 0};
        ttd::TTDSeekResult seek;
        ASSERT_TRUE(_controller->SeekTo(start, &seek));
        ASSERT_TRUE(_controller->ResumeRecordingFrom(start)) << "a loaded session continues";
        _emulator->RunNFrames(kReplay, /*skipBreakpoints=*/true);
        _controller->StopRecording();
        ASSERT_GE(_controller->GetCheckpointCount(), kFrom + 1 + kReplay);
        // The checkpoints captured live restore what the recorded ones did
        for (size_t step = 1; step <= kReplay; ++step)
        {
            ASSERT_NO_FATAL_FAILURE(SeekToCheckpoint(kFrom + step));
            ExpectSame(Live(), recorded[step - 1], "replay " + std::to_string(kFrom) + " + " + std::to_string(step));
            if (HasFailure())
                return;   // the first divergence is the useful one
        }
    }
}
