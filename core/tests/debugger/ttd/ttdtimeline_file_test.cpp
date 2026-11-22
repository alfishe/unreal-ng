#include <gtest/gtest.h>

#include <fstream>
#include <memory>
#include <set>
#include <sstream>
#include <string>

#include "_helpers/emulatortesthelper.h"
#include "_helpers/testpathhelper.h"
#include "base/featuremanager.h"
#include "common/filehelper.h"
#include "debugger/debugmanager.h"
#include "debugger/keyboard/debugkeyboardmanager.h"
#include "debugger/ttd/bench/ttdtimelinefixture.h"
#include "debugger/ttd/timetravelcontroller.h"
#include "debugger/ttd/ttdcontrol.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/memory/memory.h"

/// The timeline of a .ttd file without loading it (TD-5 file, docs/inprogress/2026-10-31-ttd-timeline-summary
/// design.md §8): the file read into a controller of its own answers what the session answers after a load, and
/// what the recording answered live except the facts the file does not keep (the HALT waits).
///
/// Runtime justification: the synthetic phase program records 96 frames once for the suite (as ttdtimeline_test);
/// the branch tests record 30 + 15 frames of a small loop each.
namespace
{
/// Every fact of a timeline as text, for an exact comparison; @p halt false leaves out what a file cannot keep
/// (the HALT waits: `halted_frames`, the `halt` tag and its `missing` entry)
std::string DumpTimeline(const ttd::TTDTimelineResult& r, bool halt = true)
{
    std::ostringstream s;
    const uint32_t tagMask = halt ? ~0u : ~uint32_t{ttd::kTimelineHalt};
    auto facts = [&](const ttd::TTDTimelineBucket& b) {
        s << b.frameStart << "-" << b.frameEnd << " t" << (b.tags & tagMask) << " x" << b.executed << " w" << b.written
          << " r" << b.read << " ram" << b.ramPieces << " scr" << b.screenFrames << " h" << (halt ? b.haltedFrames : 0)
          << " bc" << b.borderChanges << " pc" << b.pagingChanges << " lc" << b.lengthChanges << " in" << b.in
          << " out" << b.out << " sec" << b.sectors << " vec" << b.vectors << " ke" << b.keyEvents << " oi"
          << b.otherInput << " net" << b.netEvents << " ula" << b.ulaReads << " bo" << b.borderOuts << " bt"
          << b.beeperToggles << " wr" << b.writes << " tp" << b.tapePlaying << " dw" << b.diskWritten << " keys";
        for (uint8_t k : b.keys)
            s << " " << static_cast<int>(k);
        for (const auto* v : {&b.deviceIn, &b.deviceOut, &b.mediaSectors})
        {
            s << " |";
            for (uint64_t n : *v)
                s << " " << n;
        }
        s << " | regions";
        for (uint32_t n : b.regionFrames)
            s << " " << n;
        s << " | states";
        for (uint32_t n : b.stateFrames)
            s << " " << n;
        s << "\n";
    };
    s << "window " << r.fromFrame << ".." << r.toFrame << " bucket " << r.bucketSize << " fps " << r.fps
      << " truncated " << r.truncated << " port records " << r.portRecords << "\n";
    for (size_t k = 0; k < r.stateEveryFrame.size(); ++k)
        s << (r.stateEveryFrame[k] ? "every " : "some ");
    s << "\n";
    for (const auto* list : {&r.devices, &r.media, &r.regions, &r.states})
    {
        for (const ttd::TTDTimelineDevice& d : *list)
            s << d.name << ":" << d.tags << " ";
        s << "\n";
    }
    for (const ttd::TTDTimelinePhase& p : r.phases)
    {
        s << "phase " << p.frameStart << "-" << p.frameEnd << " tags " << (p.tags & tagMask) << " busiest "
          << p.busiestFrame << ": ";
        facts(p.facts);
    }
    for (const ttd::TTDTimelineBucket& b : r.buckets)
    {
        s << "bucket ";
        facts(b);
    }
    for (const ttd::TTDTimelineEvent& e : r.events)
        s << "event " << e.frame << "+" << e.tInFrame << " " << e.kind << " " << e.text << "\n";
    for (const ttd::TTDTimelineMissing& m : r.missing)
        if (halt || m.fact != "halt")
            s << "missing " << m.fact << ": " << m.reason << "\n";
    return s.str();
}

std::set<std::string> MissingFacts(const ttd::TTDTimelineResult& r)
{
    std::set<std::string> facts;
    for (const ttd::TTDTimelineMissing& m : r.missing)
        facts.insert(m.fact);
    return facts;
}

bool Save(ttd::TimeTravelController& ttd, const std::string& path, std::string& err)
{
    std::ofstream out(FileHelper::ToFsPath(path), std::ios::binary);
    return ttd.SerializeSession(out, err);
}
}  // namespace

class TTDTimelineFile_Test : public ::testing::Test
{
protected:
    static std::unique_ptr<ttd::bench::TimelineSession> _s;
    static std::string _file;

    static void SetUpTestSuite()
    {
        _s = ttd::bench::RecordTimelineProgram(24, 1);
        _file = TestPathHelper::GetUniqueTestScratchPath("timeline-program.ttd");
        std::string err;
        if (_s && _s->Ok() && !Save(*_s->ttd, _file, err))
            _file.clear();
    }
    static void TearDownTestSuite() { _s.reset(); }

    static ttd::TTDTimelineOptions Options()
    {
        ttd::TTDTimelineOptions o;
        o.buckets = true;
        o.bucketSize = 8;
        o.limit = 500;
        return o;
    }
};

std::unique_ptr<ttd::bench::TimelineSession> TTDTimelineFile_Test::_s;
std::string TTDTimelineFile_Test::_file;

/// The file's timeline is the live one, fact for fact, but for the HALT waits a file does not keep; the session
/// that was asked about the file is where it was
TEST_F(TTDTimelineFile_Test, FileAnswersAsTheRecording)
{
    ASSERT_TRUE(_s && _s->Ok());
    ASSERT_FALSE(_file.empty());
    const ttd::TTDTimelineResult live = _s->ttd->QueryTimeline(Options());
    ASSERT_TRUE(live.error.empty()) << live.error;
    const size_t checkpoints = _s->ttd->GetEngine().CheckpointCount();
    const ttd::TTDTimePoint end = _s->ttd->SessionEndPosition();

    const ttd::TTDTimelineResult file = _s->ttd->QueryTimelineInFile(_file, Options());
    ASSERT_TRUE(file.error.empty()) << file.error;
    EXPECT_EQ(file.model, "PENTAGON");
    EXPECT_EQ(file.branch, ttd::kTTDTrunk);
    EXPECT_EQ(_s->ttd->GetEngine().CheckpointCount(), checkpoints);
    EXPECT_TRUE(_s->ttd->SessionEndPosition() == end);

    // The live recording waited in HALT in the idle phase; the file says it cannot tell
    EXPECT_FALSE(MissingFacts(live).count("halt"));
    EXPECT_TRUE(MissingFacts(file).count("halt"));
    uint64_t halted = 0;
    for (const ttd::TTDTimelineBucket& b : live.buckets)
        halted += b.haltedFrames;
    EXPECT_GT(halted, 0u);

    // Every bucket's facts (a bucket does not depend on the phases' tags) and the totals
    ASSERT_EQ(file.buckets.size(), live.buckets.size());
    EXPECT_FALSE(file.devices.empty());
    for (size_t i = 0; i < live.buckets.size(); ++i)
    {
        ttd::TTDTimelineResult a, b;
        a.buckets = {live.buckets[i]};
        b.buckets = {file.buckets[i]};
        a.buckets[0].tags = b.buckets[0].tags = 0;
        EXPECT_EQ(DumpTimeline(b, false), DumpTimeline(a, false)) << "bucket " << i;
    }
    EXPECT_EQ(file.portRecords, live.portRecords);
    EXPECT_EQ(file.events.size(), live.events.size());
}

/// Loading the file and asking the session gives the very answer the file gave unloaded
TEST_F(TTDTimelineFile_Test, FileAnswersAsTheLoadedSession)
{
    ASSERT_TRUE(_s && _s->Ok());
    ASSERT_FALSE(_file.empty());
    const ttd::TTDTimelineResult file = _s->ttd->QueryTimelineInFile(_file, Options());
    ASSERT_TRUE(file.error.empty()) << file.error;
    std::string err;
    ASSERT_TRUE(_s->ttd->DeserializeSessionFile(_file, err)) << err;
    const ttd::TTDTimelineResult loaded = _s->ttd->QueryTimeline(Options());
    ASSERT_TRUE(loaded.error.empty()) << loaded.error;
    EXPECT_EQ(DumpTimeline(file), DumpTimeline(loaded));
    // The defaults (phases only) too
    EXPECT_EQ(DumpTimeline(_s->ttd->QueryTimelineInFile(_file, {})), DumpTimeline(_s->ttd->QueryTimeline({})));
}

/// A reader with no machine of the recorded model has no port map: `ports` is missing with the reason, the rest is
/// there
TEST_F(TTDTimelineFile_Test, AnotherModelNamesThePortsMissing)
{
    ASSERT_FALSE(_file.empty());
    const ttd::TimeTravelController reader(nullptr);
    const ttd::TTDTimelineResult r = reader.QueryTimelineInFile(_file, {});
    ASSERT_TRUE(r.error.empty()) << r.error;
    EXPECT_TRUE(r.devices.empty());
    EXPECT_TRUE(MissingFacts(r).count("ports"));
    for (const ttd::TTDTimelineMissing& m : r.missing)
        if (m.fact == "ports")
            EXPECT_NE(m.reason.find("PENTAGON"), std::string::npos) << m.reason;
    EXPECT_FALSE(r.phases.empty());
    uint64_t in = 0;
    for (const ttd::TTDTimelinePhase& p : r.phases)
        in += p.facts.in;
    EXPECT_GT(in, 0u);

    EXPECT_FALSE(reader.QueryTimelineInFile(_file + ".absent", {}).error.empty());
    EXPECT_NE(reader.QueryTimelineInFile(_file, {}, "no such branch").error.find("Unknown branch"), std::string::npos);
}

/// The verb: `file` and `branch` on every plane go through TTDControl
TEST_F(TTDTimelineFile_Test, VerbReadsTheFile)
{
    ASSERT_TRUE(_s && _s->Ok());
    ASSERT_FALSE(_file.empty());
    ttd::TTDControl control(_s->context);
    ttd::TTDReply reply = control.Execute({"timeline", {{"file", _file}}});
    ASSERT_TRUE(reply.Ok()) << reply.message;
    ASSERT_NE(reply.body.find("file"), nullptr);
    EXPECT_EQ(reply.body.find("file")->s, _file);
    EXPECT_EQ(reply.body.find("model")->s, "PENTAGON");
    EXPECT_EQ(reply.body.find("branch")->i, 0);
    EXPECT_FALSE(reply.body.find("phases")->items.empty());

    reply = control.Execute({"timeline", {{"file", _file}, {"branch", "trunk"}}});
    EXPECT_TRUE(reply.Ok()) << reply.message;
    reply = control.Execute({"timeline", {{"branch", "0"}}});
    EXPECT_EQ(reply.error, ttd::TTDControlError::BadRequest);
    reply = control.Execute({"timeline", {{"file", _file + ".absent"}}});
    EXPECT_EQ(reply.error, ttd::TTDControlError::NotFound);
    reply = control.Execute({"timeline", {{"file", _file}, {"branch", "7"}}});
    EXPECT_EQ(reply.error, ttd::TTDControlError::NotFound);
}

/// A session with branches: the file's timeline of each line is the line's own, the default is the branch the file
/// saved as active; a recording's folder reads as its joined file
class TTDTimelineFileBranches_Test : public ::testing::Test
{
protected:
    Emulator* _emulator = nullptr;
    EmulatorContext* _context = nullptr;
    ttd::TimeTravelController* _ttd = nullptr;
    uint64_t _start = 0;

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
        _ttd->SetShadowRecordingRoot(TestPathHelper::GetUniqueTestScratchPath("ttd-timeline-file"));

        // Folds the keyboard port into RAM #6000-#7FFF: a key held on the branch changes what it writes
        static const uint8_t program[] = {
            0xF3,              // 8000 DI
            0x21, 0x00, 0x60,  // 8001 LD HL,#6000
            0xDB, 0xFE,        // 8004 loop: IN A,(#FE)
            0x86,              // 8006 ADD A,(HL)
            0x77,              // 8007 LD (HL),A
            0x23,              // 8008 INC HL
            0x7C,              // 8009 LD A,H
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

    /// Trunk 30 frames, a branch from +10 with a key held for a frame (15 frames); the branch stays active
    void RecordTree()
    {
        ASSERT_TRUE(_ttd->StartRecording());
        _start = _context->emulatorState.frame_counter;
        _emulator->RunNFrames(30);
        _ttd->StopRecording();
        ASSERT_TRUE(_ttd->SeekTo({_start + 10, 0}));
        ASSERT_TRUE(_ttd->ResumeRecordingFrom({_start + 10, 0}));
        auto* keys = _context->pDebugManager->GetKeyboardManager();
        keys->PressKey(ZXKEY_A);
        _emulator->RunNFrames(1);
        keys->ReleaseKey(ZXKEY_A);
        _emulator->RunNFrames(14);
        _ttd->StopRecording();
        ASSERT_EQ(_ttd->Branches().Count(), 2u);
        ASSERT_EQ(_ttd->ActiveBranch(), 1u);
    }
};

TEST_F(TTDTimelineFileBranches_Test, EachLineAsTheSessionAnswersIt)
{
    ASSERT_NO_FATAL_FAILURE(RecordTree());
    const std::string path = TestPathHelper::GetUniqueTestScratchPath("timeline-branches.ttd");
    std::string err;
    ASSERT_TRUE(Save(*_ttd, path, err)) << err;

    // The branch, live and from the file (the default: the active one when saved)
    const ttd::TTDTimelineResult branchLive = _ttd->QueryTimeline({});
    ASSERT_TRUE(branchLive.error.empty()) << branchLive.error;
    const ttd::TTDTimelineResult branchFile = _ttd->QueryTimelineInFile(path, {});
    ASSERT_TRUE(branchFile.error.empty()) << branchFile.error;
    EXPECT_EQ(branchFile.branch, 1u);
    EXPECT_EQ(DumpTimeline(branchFile, false), DumpTimeline(branchLive, false));
    uint32_t keyEvents = 0;
    for (const ttd::TTDTimelinePhase& p : branchFile.phases)
        keyEvents += p.facts.keyEvents;
    EXPECT_EQ(keyEvents, 1u);

    // The trunk: by id, the session switched to it to compare
    const ttd::TTDTimelineResult trunkFile = _ttd->QueryTimelineInFile(path, {}, "0");
    ASSERT_TRUE(trunkFile.error.empty()) << trunkFile.error;
    EXPECT_EQ(trunkFile.branch, ttd::kTTDTrunk);
    const ttd::TTDBranchInfo* trunk = _ttd->Branches().Get(ttd::kTTDTrunk);
    ASSERT_NE(trunk, nullptr);
    const ttd::TTDTimePoint trunkEnd{trunk->end.frame, static_cast<uint32_t>(trunk->end.tInFrame)};
    ASSERT_TRUE(_ttd->SwitchBranch(ttd::kTTDTrunk, trunkEnd, err)) << err;
    const ttd::TTDTimelineResult trunkLive = _ttd->QueryTimeline({});
    ASSERT_TRUE(trunkLive.error.empty()) << trunkLive.error;
    EXPECT_EQ(DumpTimeline(trunkFile, false), DumpTimeline(trunkLive, false));
    EXPECT_NE(DumpTimeline(trunkFile, false), DumpTimeline(branchFile, false));
    EXPECT_GT(trunkFile.toFrame, branchFile.toFrame - 1);

    // The recording's folder joins into the file a dump writes: the same answer, through the verb
    const std::string folder = _ttd->ShadowRecordingFolder();
    ASSERT_FALSE(folder.empty());
    ttd::TTDControl control(_context);
    const ttd::TTDReply fromFolder = control.Execute({"timeline", {{"file", folder}, {"branch", "1"}}});
    ASSERT_TRUE(fromFolder.Ok()) << fromFolder.message;
    EXPECT_EQ(fromFolder.body.find("to_frame")->i, static_cast<int64_t>(branchFile.toFrame));
    EXPECT_EQ(fromFolder.body.find("phases")->items.size(), branchFile.phases.size());

    // A loaded branch keeps its port records (the load used to drop them: its journals were off), and the loaded
    // session answers what the file answered unloaded
    uint64_t ula = 0;
    for (const ttd::TTDTimelinePhase& p : branchFile.phases)
        ula += p.facts.ulaReads;
    EXPECT_GT(ula, 0u);
    ASSERT_TRUE(_ttd->DeserializeSessionFile(path, err)) << err;
    EXPECT_EQ(_ttd->ActiveBranch(), 1u);
    EXPECT_EQ(DumpTimeline(_ttd->QueryTimeline({})), DumpTimeline(branchFile));
    ttd::TTDPortQuery q;
    q.direction = ttd::TTDPortJournal::Direction::Read;
    q.portMask = 0x00FF;
    q.portValue = 0x00FE;
    const ttd::TTDPortSearchResult loadedPorts = _ttd->SearchPortEvents(q);
    ASSERT_TRUE(loadedPorts.ok) << loadedPorts.error;
    EXPECT_GT(loadedPorts.scanned, 0u);
}

