#include <gtest/gtest.h>

#include <cstdint>
#include <filesystem>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "_helpers/emulatortesthelper.h"
#include "_helpers/testpathhelper.h"
#include "_helpers/testtier.h"
#include "base/featuremanager.h"
#include "common/filehelper.h"
#include "debugger/debugmanager.h"
#include "debugger/keyboard/debugkeyboardmanager.h"
#include "debugger/ttd/machinestatehash.h"
#include "debugger/ttd/timetravelcontroller.h"
#include "debugger/ttd/timetravelengine.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/memory/memory.h"

namespace fs = std::filesystem;

/// The two tiers (docs/inprogress/2026-11-08-ttd-branches/architecture.md §5.3-5.4, test-strategy.md T-C8..T-C10):
/// with a recording folder the history limit moves segments out of memory, their files keep them, and a restore
/// reads an evicted segment back. Nothing is released: every checkpoint, journal and bookmark stays, and a seek
/// anywhere gives the machine state it gave before the eviction.
///
/// The program folds the keyboard port into RAM #6000-#7FFF on every pass: memory changes every frame, and a key
/// held on a branch makes its memory differ from the trunk's.
///
/// Runtime justification: each test boots a Pentagon and records 30-45 frames into a folder, then seeks every
/// frame twice (~0.2-0.4 s); the oracle over every mid-frame offset runs behind TestTier::Long().
class TimeTravelController_TwoTiers_Test : public ::testing::Test
{
protected:
    Emulator* _emulator = nullptr;
    EmulatorContext* _context = nullptr;
    ttd::TimeTravelController* _ttd = nullptr;
    uint64_t _start = 0;
    std::string _root;

    void SetUp() override
    {
        _emulator = EmulatorTestHelper::CreateStandardEmulator("PENTAGON", LoggerLevel::LogError);
        ASSERT_NE(_emulator, nullptr);
        _context = _emulator->GetContext();
        _ttd = _context->pTimeTravelController;
        FeatureManager* features = _emulator->GetFeatureManager();
        features->setFeature(Features::kDebugMode, true);
        features->setFeature(Features::kTimeTravel, true);
        _context->pMemory->UpdateFeatureCache();
        _root = TestPathHelper::GetUniqueTestScratchPath("ttd-two-tiers");

        static const uint8_t program[] = {
            0xF3,              // 8000 DI
            0x21, 0x00, 0x60,  // 8001 LD HL,#6000
            0xDB, 0xFE,        // 8004 loop: IN A,(#FE)
            0x86,              // 8006 ADD A,(HL)
            0x77,              // 8007 LD (HL),A
            0x23,              // 8008 INC HL
            0x7C,              // 8009 LD A,H  -- wrap #7FFF -> #6000
            0xE6, 0x1F,        // 800A AND #1F
            0xF6, 0x60,        // 800C OR #60
            0x67,              // 800E LD H,A
            0x18, 0xF3,        // 800F JR loop
        };
        _emulator->RunNFrames(5);
        Z80* z80 = _context->pCore->GetZ80();
        for (size_t i = 0; i < sizeof(program); i++)
            z80->DirectWrite(static_cast<uint16_t>(0x8000 + i), program[i]);
        z80->pc = 0x8000;
    }

    void TearDown() override
    {
        if (_emulator)
            EmulatorTestHelper::CleanupEmulator(_emulator);
    }

    /// The machine state where it stands: CPU, the emulator's latches and counters, all RAM
    uint64_t StateHash() const
    {
        Z80* z80 = _context->pCore->GetZ80();
        const uint32_t ramBytes = static_cast<uint32_t>(_context->config.ramsize) * 1024u;
        const uint64_t ram = ttd::HashBytes(_context->pMemory->RAMBase(), ramBytes);
        return ttd::HashSnapshot(ttd::CaptureSnapshot(*static_cast<Z80State*>(z80), _context->emulatorState, ram));
    }

    void HoldKey(int frames)
    {
        auto* keys = _context->pDebugManager->GetKeyboardManager();
        keys->PressKey(ZXKEY_A);
        _emulator->RunNFrames(frames);
        keys->ReleaseKey(ZXKEY_A);
    }

    /// Recording into a folder with a window of @p windowFrames (its segments an eighth of it)
    void StartWithWindow(uint64_t windowFrames)
    {
        _ttd->SetShadowRecordingRoot(_root);
        _ttd->SetHistoryLimit(windowFrames, 0);
        ASSERT_TRUE(_ttd->StartRecording());
        _start = _context->emulatorState.frame_counter;
    }

    /// The state at each position (frames from the start, offsets inside them)
    std::map<std::pair<uint64_t, uint32_t>, uint64_t> HashAll(uint64_t frames, const std::vector<uint32_t>& offsets)
    {
        std::map<std::pair<uint64_t, uint32_t>, uint64_t> all;
        for (uint64_t f = 0; f < frames; ++f)
            for (const uint32_t t : offsets)
            {
                const ttd::TTDTimePoint at{_start + f, t};
                EXPECT_TRUE(_ttd->SeekTo(at)) << "frame +" << f << " t " << t;
                all[{f, t}] = StateHash();
            }
        return all;
    }

    /// A branch whose segments left memory: a switch to it reads them back, with the states it had. A trunk of 30
    /// frames, a branch of 30 from +10; the window @p windowFrames (its segments an eighth)
    void SwitchIntoEvictedBranch(uint64_t windowFrames)
    {
        _ttd->HoldSegmentFilesForTesting(true);
        StartWithWindow(windowFrames);
        _emulator->RunNFrames(30);
        _ttd->StopRecording();
        ASSERT_TRUE(_ttd->SeekTo({_start + 10, 0}));
        ASSERT_TRUE(_ttd->ResumeRecordingFrom({_start + 10, 0}));
        HoldKey(1);
        _emulator->RunNFrames(29);
        _ttd->StopRecording();
        const uint32_t branch = _ttd->ActiveBranch();
        ASSERT_NE(branch, ttd::kTTDTrunk);

        std::string error;
        std::map<uint64_t, uint64_t> before;
        for (uint64_t f = 11; f < 39; f += 2)
        {
            ASSERT_TRUE(_ttd->SwitchBranch(branch, ttd::TTDTimePoint{_start + f, 0}, error)) << error;
            before[f] = StateHash();
        }
        const ttd::TTDTimePoint trunkEnd{_start + 29, 0};
        ASSERT_TRUE(_ttd->SwitchBranch(ttd::kTTDTrunk, trunkEnd, error)) << error;

        _ttd->HoldSegmentFilesForTesting(false);
        const ttd::TimeTravelEngine& engine = _ttd->GetEngine();
        size_t evictedOnBranch = 0;
        for (uint32_t line = 1; line < engine.LineIds(); ++line)
            if (const auto* segments = engine.LineSegments(line))
                for (const ttd::TTDSegmentInfo& s : *segments)
                    evictedOnBranch += !s.resident;
        ASSERT_GT(evictedOnBranch, 0u) << "the branch's early segments left memory";

        for (const auto& [f, hash] : before)
        {
            ASSERT_TRUE(_ttd->SwitchBranch(branch, ttd::TTDTimePoint{_start + f, 0}, error)) << error;
            EXPECT_EQ(StateHash(), hash) << "branch frame +" << f;
        }
        EXPECT_EQ(_ttd->GetSessionInfo().readBackFailures, 0u);
    }

    size_t SegmentFiles() const
    {
        size_t files = 0;
        for (const auto& entry : fs::directory_iterator(FileHelper::ToFsPath(_ttd->ShadowRecordingFolder())))
            files += entry.path().extension() == ".ttd";
        return files;
    }
};

/// T-C8: with a folder the limit only evicts. Every checkpoint stays, the session starts where it started, every
/// segment keeps its file, the input journal and an early bookmark are kept
TEST_F(TimeTravelController_TwoTiers_Test, LimitEvictsToDiskAndKeepsTheWholeSession)
{
    StartWithWindow(16);   // segments of two frames
    _emulator->RunNFrames(2);
    HoldKey(1);
    _emulator->RunNFrames(3);
    std::string error;
    ASSERT_TRUE(_ttd->AddBookmark({_start + 1, 0}, "early", &error)) << error;
    const size_t inputs = _ttd->GetSessionInfo().inputEventCount;
    _emulator->RunNFrames(34);
    _ttd->StopRecording();

    const ttd::TTDSessionInfo info = _ttd->GetSessionInfo();
    EXPECT_TRUE(info.diskTierActive);
    EXPECT_EQ(info.evictedCheckpoints, 0u) << "nothing released";
    EXPECT_GT(info.evictedToDiskSegments, 4u) << "the window bounds memory";
    EXPECT_GT(info.evictedToDiskCheckpoints, 8u);
    EXPECT_EQ(info.checkpointCount, _ttd->GetCheckpointCount());
    EXPECT_GE(info.checkpointCount, 40u) << "every checkpoint stays";
    EXPECT_EQ(info.sessionStartFrame, _start) << "the earliest position is the session start";
    EXPECT_EQ(info.inputEventCount, inputs) << "the input journal is not trimmed";
    EXPECT_EQ(info.bookmarkCount, 1u) << "the early bookmark stays";
    EXPECT_EQ(SegmentFiles(), _ttd->GetEngine().Segments().size()) << "every segment keeps its file";
    EXPECT_TRUE(_ttd->SeekTo({_start, 0})) << "the session start is seekable";
    EXPECT_TRUE(_ttd->SeekToBookmark("early", nullptr));
}

/// T-C9 and T-C10: the oracle. The files are held back (T-C10: a segment leaves memory only once its file is
/// complete), every position is hashed, then the files are offered: the segments leave memory, and every seek
/// gives the hash it gave before, read back from disk
TEST_F(TimeTravelController_TwoTiers_Test, SeekAfterEvictionIsBitIdentical)
{
    _ttd->HoldSegmentFilesForTesting(true);
    StartWithWindow(16);
    _emulator->RunNFrames(3);
    HoldKey(2);
    _emulator->RunNFrames(35);
    _ttd->StopRecording();
    EXPECT_EQ(_ttd->GetSessionInfo().evictedToDiskSegments, 0u) << "no complete file: nothing leaves memory";

    const std::vector<uint32_t> offsets = TestTier::Long() ? std::vector<uint32_t>{0, 1000, 20000, 50000, 71000}
                                                      : std::vector<uint32_t>{0, 30000};
    const uint64_t frames = 38;
    const auto before = HashAll(frames, offsets);

    _ttd->HoldSegmentFilesForTesting(false);
    ttd::TTDSessionInfo info = _ttd->GetSessionInfo();
    ASSERT_GT(info.evictedToDiskSegments, 4u) << "complete files: the segments outside the window leave memory";
    EXPECT_EQ(info.evictedCheckpoints, 0u);

    const auto after = HashAll(frames, offsets);
    for (const auto& [at, hash] : before)
        EXPECT_EQ(after.at(at), hash) << "frame +" << at.first << " t " << at.second;

    info = _ttd->GetSessionInfo();
    EXPECT_GT(info.readBackLoads, 4u) << "the evicted segments came back from their files";
    EXPECT_EQ(info.readBackFailures, 0u);
    EXPECT_LE(info.readBackSegments, info.readBackCapacity) << "the read-back cache keeps its capacity";
    EXPECT_GT(info.readBackBytes, 0u);

    // A seek back into a segment the cache still holds is a hit, not a load
    ASSERT_TRUE(_ttd->SeekTo({_start + 2, 0}));
    ASSERT_TRUE(_ttd->SeekTo({_start + 4, 0}));
    const uint64_t loads = _ttd->GetSessionInfo().readBackLoads;
    const uint64_t hits = _ttd->GetSessionInfo().readBackHits;
    ASSERT_TRUE(_ttd->SeekTo({_start + 2, 0}));
    EXPECT_EQ(_ttd->GetSessionInfo().readBackLoads, loads);
    EXPECT_EQ(_ttd->GetSessionInfo().readBackHits, hits + 1);
}

/// A branch whose segments left memory: a switch to it reads them back with the states it had (segments of two
/// frames)
TEST_F(TimeTravelController_TwoTiers_Test, SwitchIntoAnEvictedBranch)
{
    SwitchIntoEvictedBranch(16);
}

/// The same with segments of one frame: the branch's first capture begins its second segment before its files
/// start, which still begin at the fork copy
TEST_F(TimeTravelController_TwoTiers_Test, SwitchIntoAnEvictedBranchOfOneFrameSegments)
{
    SwitchIntoEvictedBranch(8);
}

/// A save reads every evicted segment back (architecture.md §5.4) and writes the whole session: loaded again, every
/// position gives the state it gave live. Found by the P1.S10 soak (2026-11-21): the dump of a Pentagon and a
/// Sprinter recording with segments on disk answered "a version's base is not in the session"
TEST_F(TimeTravelController_TwoTiers_Test, SaveAfterEvictionWritesTheWholeSession)
{
    StartWithWindow(16);
    _emulator->RunNFrames(3);
    HoldKey(2);
    _emulator->RunNFrames(35);
    _ttd->StopRecording();
    ASSERT_GT(_ttd->GetSessionInfo().evictedToDiskSegments, 4u);

    const std::vector<uint32_t> offsets{0, 30000};
    const auto before = HashAll(38, offsets);
    std::stringstream file;
    std::string error;
    ASSERT_TRUE(_ttd->SerializeSession(file, error)) << error;
    ASSERT_TRUE(_ttd->DeserializeSession(file, error)) << error;
    const auto after = HashAll(38, offsets);
    for (const auto& [at, hash] : before)
        EXPECT_EQ(after.at(at), hash) << "frame +" << at.first << " t " << at.second;
}

/// The same with a branch forked inside a segment that has left memory
TEST_F(TimeTravelController_TwoTiers_Test, SaveAfterEvictionWithABranch)
{
    StartWithWindow(16);
    _emulator->RunNFrames(40);
    _ttd->StopRecording();
    ASSERT_GT(_ttd->GetSessionInfo().evictedToDiskSegments, 4u);
    ASSERT_TRUE(_ttd->SeekTo({_start + 5, 0}));
    ASSERT_TRUE(_ttd->ResumeRecordingFrom({_start + 5, 0}));
    HoldKey(1);
    _emulator->RunNFrames(20);
    _ttd->StopRecording();
    const uint32_t branch = _ttd->ActiveBranch();
    ASSERT_NE(branch, ttd::kTTDTrunk);

    // The branch's states, its fork segment out of memory again (the read-back cache holds one segment)
    std::string error;
    _ttd->SetReadBackCapacity(1);
    std::map<uint64_t, uint64_t> onBranch, onTrunk;
    for (uint64_t f = 6; f < 25; f += 3)
    {
        ASSERT_TRUE(_ttd->SwitchBranch(branch, ttd::TTDTimePoint{_start + f, 0}, error)) << error;
        onBranch[f] = StateHash();
    }
    for (uint64_t f = 1; f < 40; f += 5)
    {
        ASSERT_TRUE(_ttd->SwitchBranch(ttd::kTTDTrunk, ttd::TTDTimePoint{_start + f, 0}, error)) << error;
        onTrunk[f] = StateHash();
    }

    std::stringstream file;
    ASSERT_TRUE(_ttd->SerializeSession(file, error)) << error;
    ASSERT_TRUE(_ttd->DeserializeSession(file, error)) << error;
    for (const auto& [f, hash] : onBranch)
    {
        ASSERT_TRUE(_ttd->SwitchBranch(branch, ttd::TTDTimePoint{_start + f, 0}, error)) << error;
        EXPECT_EQ(StateHash(), hash) << "branch frame +" << f;
    }
    for (const auto& [f, hash] : onTrunk)
    {
        ASSERT_TRUE(_ttd->SwitchBranch(ttd::kTTDTrunk, ttd::TTDTimePoint{_start + f, 0}, error)) << error;
        EXPECT_EQ(StateHash(), hash) << "trunk frame +" << f;
    }
}

/// Without a folder the limit drops the oldest history as before (the journals cut to the new start)
TEST_F(TimeTravelController_TwoTiers_Test, WithoutAFolderTheLimitDrops)
{
    _ttd->SetHistoryLimit(16, 0);
    ASSERT_TRUE(_ttd->StartRecording());
    _start = _context->emulatorState.frame_counter;
    _emulator->RunNFrames(40);
    _ttd->StopRecording();
    const ttd::TTDSessionInfo info = _ttd->GetSessionInfo();
    EXPECT_FALSE(info.diskTierActive);
    EXPECT_GT(info.evictedCheckpoints, 0u) << "the oldest checkpoints are released";
    EXPECT_EQ(info.evictedToDiskSegments, 0u);
    EXPECT_GT(info.sessionStartFrame, _start) << "the session starts later";
}


/// A recording resumed at the trunk's end after its last segment left memory (the head was on a branch). The resume
/// cuts there and writes that segment's file again: the segment has to come back first and stay. The P1.S10 soak
/// (2026-11-21) found it forgotten - its file unknown, every later read back of it failing - and a resume in that
/// part of the history then cut the trunk instead of forking a branch
TEST_F(TimeTravelController_TwoTiers_Test, ResumeAtTheEndOfAnEvictedTrunk)
{
    // A byte limit below one segment: every segment the head leaves goes to its file (a segment begins at each resume)
    _ttd->SetShadowRecordingRoot(_root);
    _ttd->SetHistoryLimit(0, 1);
    ASSERT_TRUE(_ttd->StartRecording());
    _start = _context->emulatorState.frame_counter;
    std::string error;
    std::map<uint64_t, uint64_t> trunk;
    for (int cycle = 0; cycle < 7; ++cycle)
    {
        // The trunk records on; a branch forks a third of the way back, then the trunk resumes at its end
        _emulator->RunNFrames(30);
        const ttd::TTDTimePoint end = _ttd->CurrentPosition();
        const uint64_t frames = end.frame - _start;
        for (uint64_t f = frames - 30; f < frames; f += 7)
        {
            ASSERT_TRUE(_ttd->SeekTo({_start + f, 0}));
            trunk[f] = StateHash();
        }
        const ttd::TTDTimePoint fork{_start + frames / 3, 0};
        ASSERT_TRUE(_ttd->SeekTo(fork));
        ASSERT_TRUE(_ttd->ResumeRecordingFrom(fork)) << "cycle " << cycle;
        ASSERT_NE(_ttd->ActiveBranch(), ttd::kTTDTrunk) << "cycle " << cycle << ": the resume cut the trunk";
        HoldKey(1);
        _emulator->RunNFrames(5);
        _ttd->StopRecording();
        ASSERT_TRUE(_ttd->SwitchBranch(ttd::kTTDTrunk, end, error)) << "cycle " << cycle << ": " << error;
        ASSERT_TRUE(_ttd->ResumeRecordingFrom(end)) << "cycle " << cycle;
        ASSERT_EQ(_ttd->ActiveBranch(), ttd::kTTDTrunk) << "at its end the trunk goes on";
    }
    _ttd->StopRecording();
    EXPECT_GT(_ttd->GetSessionInfo().evictedToDiskSegments, 3u);

    // Every part of the trunk reads back as it was
    _ttd->SetReadBackCapacity(1);
    for (const auto& [f, hash] : trunk)
    {
        ASSERT_TRUE(_ttd->SeekTo({_start + f, 0})) << "frame +" << f;
        EXPECT_EQ(StateHash(), hash) << "frame +" << f;
    }
    EXPECT_EQ(_ttd->GetSessionInfo().readBackFailures, 0u);
}
