#include <gtest/gtest.h>

#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "_helpers/emulatortesthelper.h"
#include "_helpers/soundcardscope.h"
#include "_helpers/testpathhelper.h"
#include <json/json.h>
#include "base/featuremanager.h"
#include "debugger/debugmanager.h"
#include "debugger/keyboard/debugkeyboardmanager.h"
#include "debugger/ttd/machinestatehash.h"
#include "debugger/ttd/timetravelmanager.h"
#include "debugger/ttd/ttdcheckpoint.h"
#include "debugger/ttd/ttddumpformat.h"
#include "debugger/ttd/ttdportjournal.h"
#include "debugger/ttd/ttdportsearch.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/io/keyboard/keyboard.h"
#include "emulator/memory/memory.h"
#include "emulator/sound/chips/gs/generalsoundcard.h"
#include "emulator/sound/soundmanager.h"

/// The port-read journal on the machine (ttd-port-read-journal.md): a session
/// records every IN result of the main CPU, and a replay hands the CPU the
/// recorded values - so a session replays exactly without the media it was
/// recorded with.
///
/// The central check records a program that polls the tape's EAR bit in a
/// tight loop, loads the file into a fresh instance that has NO tape, and
/// replays it: the machine must arrive at the recorded end in the same state.
/// The same file without the journal must diverge.
///
/// Runtime justification: several emulator instances; the tape session runs
/// ~35 frames and every replay runs it again.
namespace
{
using ttd::TTDPortJournal;
using ttd::TTDPortRecord;

constexpr uint16_t kProgramAddress = 0x8000;
constexpr uint16_t kLogAddress = 0x9000;
constexpr size_t kFlagsOffset = 6;  // magic 4 + schema_version 2

struct Point
{
    uint64_t frame = 0;
    uint32_t tInFrame = 0;
    ttd::TTDCpuState cpu;
    uint64_t ramHash = 0;
};

bool SameMachine(const Point& a, const Point& b)
{
    return a.frame == b.frame && a.tInFrame == b.tInFrame && std::memcmp(&a.cpu, &b.cpu, sizeof(a.cpu)) == 0 &&
           a.ramHash == b.ramHash;
}

struct Machine
{
    Emulator* emulator = nullptr;
    EmulatorContext* context = nullptr;
    ttd::TimeTravelManager* ttd = nullptr;

    bool Create(const char* model = "PENTAGON")
    {
        emulator = EmulatorTestHelper::CreateStandardEmulator(model, LoggerLevel::LogError);
        if (!emulator)
            return false;
        context = emulator->GetContext();
        ttd = context->pTimeTravelManager;
        FeatureManager* features = emulator->GetFeatureManager();
        features->setFeature(Features::kDebugMode, true);
        features->setFeature(Features::kTimeTravel, true);
        // Automatic turbo loading toggles the host turbo mode during a load - a
        // host decision this test is not about
        features->setFeature(Features::kTurboTape, false);
        context->pMemory->UpdateFeatureCache();
        return ttd != nullptr;
    }

    void Destroy()
    {
        if (emulator)
            EmulatorTestHelper::CleanupEmulator(emulator);
        emulator = nullptr;
    }

    Point Observe() const
    {
        const Z80* z80 = context->pCore->GetZ80();
        Point p;
        p.frame = context->emulatorState.frame_counter;
        p.tInFrame = z80->t;
        p.cpu = ttd::CaptureCpuState(*static_cast<const Z80State*>(z80));
        const size_t ramBytes = static_cast<size_t>(context->config.ramsize) * 1024u;
        p.ramHash = ttd::HashBytes(context->pMemory->RAMBase(), ramBytes);
        return p;
    }

    void RunTo(const Point& target)
    {
        const EmulatorState* state = &context->emulatorState;
        const uint64_t frame = target.frame;
        const uint32_t t = target.tInFrame;
        emulator->RunUntilCondition([state, frame, t](const Z80State& z80)
                                    { return state->frame_counter > frame || (state->frame_counter == frame && z80.t >= t); });
    }

    void RunToPc(uint16_t pc)
    {
        emulator->RunUntilCondition([pc](const Z80State& z80) { return z80.pc == pc; });
    }

    void Install(const uint8_t* program, size_t size)
    {
        Z80* z80 = context->pCore->GetZ80();
        for (size_t i = 0; i < size; i++)
            z80->DirectWrite(static_cast<uint16_t>(kProgramAddress + i), program[i]);
        for (uint16_t a = kLogAddress; a < 0xC000; a++)
            z80->DirectWrite(a, 0);
        z80->pc = kProgramAddress;
    }

    std::string Save()
    {
        std::ostringstream out;
        std::string err;
        EXPECT_TRUE(ttd->SerializeSession(out, err)) << err;
        return out.str();
    }

    bool Load(const std::string& file, std::string* errOut = nullptr)
    {
        std::istringstream in(file);
        std::string err;
        const bool ok = ttd->DeserializeSession(in, err);
        if (errOut)
            *errOut = err;
        return ok;
    }

    /// Size of the port-journal section (bit 8) at the end of this session's file
    size_t PortJournalBytes() const
    {
        std::vector<uint64_t> reads;
        std::vector<uint64_t> writes;
        for (size_t i = 0; i < ttd->GetSessionInfo().checkpointCount; i++)
        {
            reads.push_back(ttd->GetCheckpoint(i)->portReadCursor);
            writes.push_back(ttd->GetCheckpoint(i)->portWriteCursor);
        }
        std::ostringstream out;
        std::string err;
        EXPECT_TRUE(ttd->GetPortReadJournal().Serialize(out, reads, err)) << err;
        EXPECT_TRUE(ttd->GetPortWriteJournal().Serialize(out, writes, err)) << err;
        return out.str().size();
    }

    size_t LogEntries() const
    {
        size_t entries = 0;
        for (uint16_t a = kLogAddress; a < 0xC000; a += 2)
        {
            if (context->pMemory->DirectReadFromZ80Memory(a) == 0 && context->pMemory->DirectReadFromZ80Memory(a + 1) == 0)
                break;
            entries++;
        }
        return entries;
    }
};

uint16_t FlagsOf(const std::string& file)
{
    uint16_t flags = 0;
    std::memcpy(&flags, file.data() + kFlagsOffset, sizeof(flags));
    return flags;
}

/// The file an isolating writer would not have produced: the port-read
/// journal section cut off and its bit cleared
std::string StripPortJournal(const std::string& file, size_t sectionBytes)
{
    std::string stripped = file.substr(0, file.size() - sectionBytes);
    const uint16_t flags = static_cast<uint16_t>(FlagsOf(stripped) & ~ttd::dump::kFlagsHasPortJournals);
    std::memcpy(&stripped[kFlagsOffset], &flags, sizeof(flags));
    return stripped;
}

/// Polls EAR (port #FE bit 6) with an iteration counter and logs the counter
/// at every edge (#9000..): any read that differs changes the log and RAM
const uint8_t kEarPoller[] = {
    0xF3,                    // 8000 DI
    0x21, 0x00, 0x00,        // 8001 LD HL,0
    0xDD, 0x21, 0x00, 0x90,  // 8004 LD IX,#9000
    0x0E, 0xFF,              // 8008 LD C,#FF
    0x23,                    // 800A loop: INC HL
    0x3E, 0xFF,              // 800B LD A,#FF        no keyboard row selected
    0xDB, 0xFE,              // 800D IN A,(#FE)
    0xE6, 0x40,              // 800F AND #40         EAR
    0xB9,                    // 8011 CP C
    0x28, 0xF6,              // 8012 JR Z,loop
    0x4F,                    // 8014 LD C,A
    0xDD, 0x75, 0x00,        // 8015 LD (IX+0),L
    0xDD, 0x74, 0x01,        // 8018 LD (IX+1),H
    0xDD, 0x23,              // 801B INC IX
    0xDD, 0x23,              // 801D INC IX
    0x18, 0xE9,              // 801F JR loop
};
constexpr uint16_t kEarPollerLoop = 0x800A;

/// The same shape for the keyboard: all half-rows, keys only
const uint8_t kKeyPoller[] = {
    0xF3,                    // 8000 DI
    0x21, 0x00, 0x00,        // 8001 LD HL,0
    0xDD, 0x21, 0x00, 0x90,  // 8004 LD IX,#9000
    0x0E, 0xFF,              // 8008 LD C,#FF
    0x23,                    // 800A loop: INC HL
    0xAF,                    // 800B XOR A           all half-rows
    0xDB, 0xFE,              // 800C IN A,(#FE)
    0xF6, 0xE0,              // 800E OR #E0          keys only
    0xB9,                    // 8010 CP C
    0x28, 0xF7,              // 8011 JR Z,loop
    0x4F,                    // 8013 LD C,A
    0xDD, 0x75, 0x00,        // 8014 LD (IX+0),L
    0xDD, 0x74, 0x01,        // 8017 LD (IX+1),H
    0xDD, 0x23,              // 801A INC IX
    0xDD, 0x23,              // 801C INC IX
    0x18, 0xEA,              // 801E JR loop
};
/// Scans two half-rows one at a time, as the ROM's KEY-SCAN does: A's (#FD)
/// and Q's (#FB)
const uint8_t kRowPoller[] = {
    0xF3,        // 8000 DI
    0x3E, 0xFD,  // 8001 loop: LD A,#FD
    0xDB, 0xFE,  // 8003 IN A,(#FE)   A S D F G
    0x3E, 0xFB,  // 8005 LD A,#FB
    0xDB, 0xFE,  // 8007 IN A,(#FE)   Q W E R T
    0x18, 0xF6,  // 8009 JR loop
};
}  // namespace

class TimeTravelManager_PortReadJournal_Test : public ::testing::Test
{
protected:
    Machine _rec;   // records
    Machine _play;  // a fresh instance a file is loaded into
    size_t _logAtStart = 0;  // poller log entries when the recording started

    void SetUp() override
    {
        ASSERT_TRUE(_rec.Create());
        ASSERT_TRUE(_play.Create());
    }

    void TearDown() override
    {
        _rec.Destroy();
        _play.Destroy();
    }

    /// The EAR poller over a playing tape: returns the start frame and the end
    void RecordTapeSession(uint64_t& startFrame, Point& end)
    {
        ASSERT_TRUE(_rec.emulator->LoadTape((TestPathHelper::FindProjectRoot() / "testdata/memory/UMT23X.tap").string()));
        _rec.Install(kEarPoller, sizeof(kEarPoller));
        _rec.emulator->RunNFrames(3);  // the poller's EAR polling starts the tape
        _rec.emulator->RunNCPUCycles(4321);
        _logAtStart = _rec.LogEntries();
        ASSERT_TRUE(_rec.ttd->StartRecording());
        startFrame = _rec.ttd->GetCheckpoint(0)->time.frame;
        _rec.emulator->RunNFrames(30);
        _rec.emulator->RunNCPUCycles(999);
        end = _rec.Observe();
        _rec.ttd->StopRecording();
        ASSERT_GE(_rec.LogEntries(), 20u) << "the poller saw no tape edges - the replay check would be vacuous";
    }

    /// The key poller with keys pressed and released inside frames
    void RecordKeySession(uint64_t& startFrame, Point& end, bool rowScan = false)
    {
        DebugKeyboardManager* keys = _rec.context->pDebugManager->GetKeyboardManager();
        _rec.emulator->RunNFrames(2);
        if (rowScan)
            _rec.Install(kRowPoller, sizeof(kRowPoller));
        else
            _rec.Install(kKeyPoller, sizeof(kKeyPoller));
        ASSERT_TRUE(_rec.ttd->StartRecording());
        startFrame = _rec.context->emulatorState.frame_counter;
        _rec.emulator->RunNCPUCycles(3001);
        keys->PressKey(ZXKEY_A);
        _rec.emulator->RunNCPUCycles(4567);
        keys->ReleaseKey(ZXKEY_A);
        _rec.emulator->RunNFrames(2);
        _rec.emulator->RunNCPUCycles(777);
        keys->PressKey(ZXKEY_Q);
        _rec.emulator->RunNFrames(1);
        keys->ReleaseKey(ZXKEY_Q);
        _rec.emulator->RunNFrames(2);
        end = _rec.Observe();
        _rec.ttd->StopRecording();
        if (!rowScan)
            ASSERT_GE(_rec.LogEntries(), 4u) << "the poller never saw the keys";
    }
};

// ---------------------------------------------------------------------------
// Recording
// ---------------------------------------------------------------------------

/// One record per IN: the key poller reads the port once per iteration, so
/// between two visits of the loop head the journal grows by exactly the
/// iteration counter's growth
TEST_F(TimeTravelManager_PortReadJournal_Test, RecordsEveryInOfTheSession)
{
    _rec.Install(kKeyPoller, sizeof(kKeyPoller));
    _rec.RunToPc(0x800A);
    ASSERT_TRUE(_rec.ttd->StartRecording());
    const uint16_t hlStart = _rec.context->pCore->GetZ80()->hl;
    _rec.emulator->RunNFrames(3);
    _rec.RunToPc(0x800A);
    const uint16_t hlEnd = _rec.context->pCore->GetZ80()->hl;
    _rec.ttd->StopRecording();

    const TTDPortJournal& journal = _rec.ttd->GetPortReadJournal();
    ASSERT_GT(hlEnd - hlStart, 1000);
    EXPECT_EQ(journal.Size(), static_cast<uint64_t>(hlEnd - hlStart));
    TTDPortRecord first;
    TTDPortRecord second;
    ASSERT_TRUE(journal.Get(0, first));
    ASSERT_TRUE(journal.Get(1, second));
    EXPECT_EQ(first.port, 0x00FE) << "XOR A; IN A,(#FE): A is the high byte";
    EXPECT_EQ(first.value | 0xE0, 0xFF) << "no key pressed";
    EXPECT_EQ(first.pc, 0x800C) << "the IN instruction";
    EXPECT_EQ(first.frame, _rec.ttd->GetCheckpoint(0)->time.frame);
    EXPECT_EQ(second.tInFrame - first.tInFrame, 44u) << "one loop iteration: INC HL 6, XOR A 4, IN 11, OR 7, CP 4, JR 12 T";

    const auto info = _rec.ttd->GetSessionInfo();
    EXPECT_TRUE(info.portJournalActive);
    EXPECT_TRUE(info.portJournalOffReason.empty());
    EXPECT_EQ(info.portReadCount, journal.Size());
    EXPECT_GT(info.portJournalBytes, 0u);
    EXPECT_LT(info.portJournalBytes, journal.Size() / 20) << "a polling loop compresses";
}

/// Block I/O: INIR reads B times, each from the port BC held at the time
TEST_F(TimeTravelManager_PortReadJournal_Test, BlockInputRecordsEveryIteration)
{
    static const uint8_t program[] = {
        0xF3,              // 8000 DI
        0x21, 0x00, 0xA0,  // 8001 LD HL,#A000
        0x01, 0xFE, 0x05,  // 8004 LD BC,#05FE
        0xED, 0xB2,        // 8007 INIR
        0x18, 0xFE,        // 8009 JR $
    };
    _rec.Install(program, sizeof(program));
    ASSERT_TRUE(_rec.ttd->StartRecording());
    _rec.RunToPc(0x8009);
    _rec.ttd->StopRecording();

    const TTDPortJournal& journal = _rec.ttd->GetPortReadJournal();
    ASSERT_EQ(journal.Size(), 5u);
    for (uint64_t i = 0; i < 5; i++)
    {
        TTDPortRecord r;
        ASSERT_TRUE(journal.Get(i, r));
        EXPECT_EQ(r.port, static_cast<uint16_t>(((5 - i) << 8) | 0xFE)) << "read " << i;
        EXPECT_EQ(r.pc, 0x8007) << "every iteration is the INIR";
        EXPECT_EQ(r.value, _rec.context->pMemory->DirectReadFromZ80Memory(static_cast<uint16_t>(0xA000 + i)))
            << "the value INIR stored is the one recorded";
    }
}

// ---------------------------------------------------------------------------
// Replay is isolated from the outside world
// ---------------------------------------------------------------------------

/// The central check: a tape session saved to a file replays exactly in an
/// instance that has no tape at all - the CPU is fed the recorded EAR reads.
/// The live (tapeless) machine answered differently, and that is counted
TEST_F(TimeTravelManager_PortReadJournal_Test, LoadedSessionReplaysTheTapeWithoutTheTape)
{
    uint64_t startFrame = 0;
    Point recorded;
    RecordTapeSession(startFrame, recorded);
    if (HasFatalFailure())
        return;
    const std::string file = _rec.Save();
    EXPECT_NE(FlagsOf(file) & ttd::dump::kFlagsHasPortJournals, 0);

    std::string err;
    ASSERT_TRUE(_play.Load(file, &err)) << err;
    ASSERT_TRUE(_play.ttd->GetSessionInfo().portJournalActive);
    ASSERT_TRUE(_play.ttd->SeekTo({startFrame, 0}));
    _play.RunTo(recorded);

    EXPECT_TRUE(SameMachine(_play.Observe(), recorded)) << "the replay without the tape diverged";
    EXPECT_EQ(_play.LogEntries(), _rec.LogEntries());
    const auto info = _play.ttd->GetSessionInfo();
    EXPECT_GT(info.portReplayValueMismatches, 0u) << "the tapeless machine must have answered differently";
    EXPECT_EQ(info.portReplayDivergences, 0u) << "execution itself never diverged";
}

/// The negative control: the same file without the journal replays against the
/// live (tapeless) machine and diverges
TEST_F(TimeTravelManager_PortReadJournal_Test, WithoutTheJournalTheTapelessReplayDiverges)
{
    uint64_t startFrame = 0;
    Point recorded;
    RecordTapeSession(startFrame, recorded);
    if (HasFatalFailure())
        return;
    const std::string stripped = StripPortJournal(_rec.Save(), _rec.PortJournalBytes());

    std::string err;
    ASSERT_TRUE(_play.Load(stripped, &err)) << err;
    const auto info = _play.ttd->GetSessionInfo();
    EXPECT_FALSE(info.portJournalActive);
    EXPECT_FALSE(info.portJournalOffReason.empty());
    ASSERT_TRUE(_play.ttd->SeekTo({startFrame, 0}));
    _play.RunTo(recorded);
    EXPECT_FALSE(SameMachine(_play.Observe(), recorded));
}

/// Replayed on the instance that recorded it, with the tape still there, the
/// devices answer as recorded: no mismatch at all, from every kind of start
TEST_F(TimeTravelManager_PortReadJournal_Test, AFaithfulReplayHasNoMismatches)
{
    uint64_t startFrame = 0;
    Point recorded;
    RecordTapeSession(startFrame, recorded);
    if (HasFatalFailure())
        return;

    const ttd::TTDTimePoint starts[] = {{startFrame, 0}, {startFrame + 11, 0}, {startFrame + 19, 30000}};
    for (const ttd::TTDTimePoint& start : starts)
    {
        SCOPED_TRACE(start.frame);
        ASSERT_TRUE(_rec.ttd->SeekTo(start));
        _rec.RunTo(recorded);
        EXPECT_TRUE(SameMachine(_rec.Observe(), recorded));
    }
    const auto info = _rec.ttd->GetSessionInfo();
    EXPECT_EQ(info.portReplayValueMismatches, 0u);
    EXPECT_EQ(info.portReplayDivergences, 0u);
}

/// The keyboard reaches the CPU through IN too: with the input journal cut out
/// of the file, the journal alone replays what the program saw - the keyboard
/// device itself never has a key pressed during the replay
TEST_F(TimeTravelManager_PortReadJournal_Test, TheJournalAloneReplaysTheKeyboard)
{
    uint64_t startFrame = 0;
    Point recorded;
    RecordKeySession(startFrame, recorded);
    if (HasFatalFailure())
        return;

    // Cut the input journal and the markers (bits 6-7) out from before the
    // port-read journal, as a file without them would be
    const std::string file = _rec.Save();
    const size_t portBytes = _rec.PortJournalBytes();
    const size_t inputBytes =
        4 + _rec.ttd->GetInputJournal().Size() * ttd::dump::kInputEventRecordSize + 4;  // no markers recorded
    ASSERT_EQ(_rec.ttd->GetExternalEvents().Size(), 0u);
    std::string cut = file.substr(0, file.size() - portBytes - inputBytes) + file.substr(file.size() - portBytes);
    const uint16_t flags = static_cast<uint16_t>(
        FlagsOf(cut) & ~(ttd::dump::kFlagsHasInputJournal | ttd::dump::kFlagsHasExternalEvents));
    std::memcpy(&cut[kFlagsOffset], &flags, sizeof(flags));

    std::string err;
    ASSERT_TRUE(_play.Load(cut, &err)) << err;
    ASSERT_FALSE(_play.ttd->GetSessionInfo().inputHistoryComplete);
    ASSERT_EQ(_play.ttd->GetInputJournal().Size(), 0u);
    ASSERT_TRUE(_play.ttd->SeekTo({startFrame, 0}));
    _play.RunTo(recorded);

    EXPECT_TRUE(SameMachine(_play.Observe(), recorded));
    EXPECT_EQ(_play.LogEntries(), _rec.LogEntries());
    EXPECT_GT(_play.ttd->GetSessionInfo().portReplayValueMismatches, 0u) << "the unpressed keyboard answered otherwise";
}

// ---------------------------------------------------------------------------
// History edits
// ---------------------------------------------------------------------------

/// Resuming from a point in the past cuts the journal exactly there: the new
/// history's reads follow the kept ones, and the new history replays exactly
TEST_F(TimeTravelManager_PortReadJournal_Test, ResumeFromThePastCutsTheJournalAtTheResumePoint)
{
    uint64_t startFrame = 0;
    Point recorded;
    RecordKeySession(startFrame, recorded);
    if (HasFatalFailure())
        return;

    const ttd::TTDTimePoint resumeAt{startFrame + 1, 30000};
    ASSERT_TRUE(_rec.ttd->SeekTo(resumeAt));
    const uint64_t cursor = _rec.ttd->GetPortReadJournal().Cursor();
    ASSERT_GT(cursor, 0u);
    ASSERT_LT(cursor, _rec.ttd->GetPortReadJournal().Size());
    ASSERT_TRUE(_rec.ttd->ResumeRecordingFrom(resumeAt));
    EXPECT_EQ(_rec.ttd->GetPortReadJournal().Size(), cursor) << "reads past the resume point are dead future";

    DebugKeyboardManager* keys = _rec.context->pDebugManager->GetKeyboardManager();
    _rec.emulator->RunNCPUCycles(2000);
    keys->PressKey(ZXKEY_Z);
    _rec.emulator->RunNFrames(2);
    keys->ReleaseKey(ZXKEY_Z);
    _rec.emulator->RunNFrames(1);
    const Point newEnd = _rec.Observe();
    _rec.ttd->StopRecording();

    // From the file, in a fresh instance
    std::string err;
    ASSERT_TRUE(_play.Load(_rec.Save(), &err)) << err;
    ASSERT_TRUE(_play.ttd->SeekTo({startFrame, 0}));
    _play.RunTo(newEnd);
    EXPECT_TRUE(SameMachine(_play.Observe(), newEnd));
    EXPECT_EQ(_play.ttd->GetSessionInfo().portReplayDivergences, 0u);
}

/// The journal cannot hold a gap: a machine that ran between a stop and a
/// resume made reads nobody recorded, so the session gives the journal up
/// (replay reads the live devices again) and says why
TEST_F(TimeTravelManager_PortReadJournal_Test, AResumeAfterRunningUnrecordedGivesTheJournalUp)
{
    _rec.Install(kKeyPoller, sizeof(kKeyPoller));
    ASSERT_TRUE(_rec.ttd->StartRecording());
    _rec.emulator->RunNFrames(1);
    _rec.emulator->RunNCPUCycles(100);
    _rec.ttd->StopRecording();

    // Straight back: nothing ran, the journal stays whole
    ASSERT_TRUE(_rec.ttd->ResumeRecordingLive());
    EXPECT_TRUE(_rec.ttd->GetSessionInfo().portJournalActive);
    _rec.emulator->RunNCPUCycles(100);
    _rec.ttd->StopRecording();

    _rec.emulator->RunNCPUCycles(50);  // unrecorded reads
    ASSERT_TRUE(_rec.ttd->ResumeRecordingLive());
    const auto info = _rec.ttd->GetSessionInfo();
    EXPECT_FALSE(info.portJournalActive);
    EXPECT_NE(info.portJournalOffReason.find("unrecorded"), std::string::npos) << info.portJournalOffReason;
    EXPECT_EQ(FlagsOf(_rec.Save()) & ttd::dump::kFlagsHasPortJournals, 0) << "a partial journal is never saved";
}

// ---------------------------------------------------------------------------
// "When did the program ..." - searches over the journals
// ---------------------------------------------------------------------------

namespace
{
ttd::TTDPortSearchResult Find(const ttd::TimeTravelManager& ttd, const std::string& event, const std::string& arg = "",
                              size_t limit = 100)
{
    ttd::TTDPortQuery q;
    std::string err;
    EXPECT_TRUE(ttd::BuildPortEventQuery(event, arg, q, err)) << err;
    q.limit = limit;
    ttd::TTDPortSearchResult r = ttd.SearchPortEvents(q);
    EXPECT_TRUE(r.ok) << r.error;
    return r;
}
}  // namespace

/// "When did the program first see A pressed": the first read of A's
/// half-row after the key went down - the read of that row before it still saw
/// the key up. The hit carries the IN instruction's address
TEST_F(TimeTravelManager_PortReadJournal_Test, KeySearchFindsTheReadThatSawThePress)
{
    uint64_t startFrame = 0;
    Point recorded;
    RecordKeySession(startFrame, recorded, /*rowScan=*/true);
    if (HasFatalFailure())
        return;

    ttd::TTDTimePoint pressedA{};
    ttd::TTDTimePoint pressedQ{};
    for (const ttd::TTDInputEvent& ev : _rec.ttd->GetInputJournal().Events())
    {
        if (ev.pressed && ev.key == ZXKEY_A)
            pressedA = ev.time;
        if (ev.pressed && ev.key == ZXKEY_Q)
            pressedQ = ev.time;
    }

    const auto a = Find(*_rec.ttd, "key", "a");
    ASSERT_EQ(a.hits.size(), 1u) << "one press, seen once however long it was held";
    const TTDPortRecord& seen = a.hits[0].record;
    EXPECT_FALSE(seen.Time() < pressedA) << "seen after it was pressed";
    TTDPortRecord before;
    uint64_t i = a.hits[0].index;
    do
        ASSERT_TRUE(_rec.ttd->GetPortReadJournal().Get(--i, before));
    while (before.port != 0xFDFE);
    EXPECT_TRUE(before.Time() < pressedA) << "the previous read of the row came before the press";
    EXPECT_EQ(seen.port, 0xFDFE);
    EXPECT_EQ(seen.pc, 0x8003);
    EXPECT_EQ(seen.value & 0x01, 0) << "A is bit 0 of its half-row";

    const auto q = Find(*_rec.ttd, "key", "q");
    ASSERT_EQ(q.hits.size(), 1u);
    EXPECT_FALSE(q.hits[0].record.Time() < pressedQ);
    EXPECT_EQ(q.hits[0].record.pc, 0x8007);
    EXPECT_TRUE(Find(*_rec.ttd, "key", "z").hits.empty()) << "never pressed";
    EXPECT_EQ(Find(*_rec.ttd, "key").hits.size(), 2u) << "any key: the two presses";
}

/// The journals travel in the file: a loaded session answers the same
/// questions with the same records, without the recording instance
TEST_F(TimeTravelManager_PortReadJournal_Test, ALoadedFileAnswersTheSameSearches)
{
    uint64_t startFrame = 0;
    Point recorded;
    RecordKeySession(startFrame, recorded, /*rowScan=*/true);
    if (HasFatalFailure())
        return;
    std::string err;
    ASSERT_TRUE(_play.Load(_rec.Save(), &err)) << err;

    for (const char* key : {"a", "q", ""})
    {
        SCOPED_TRACE(key);
        const auto live = Find(*_rec.ttd, "key", key);
        const auto loaded = Find(*_play.ttd, "key", key);
        ASSERT_EQ(loaded.hits.size(), live.hits.size());
        for (size_t i = 0; i < live.hits.size(); i++)
        {
            EXPECT_EQ(loaded.hits[i].index, live.hits[i].index);
            EXPECT_TRUE(loaded.hits[i].record.SameAccess(live.hits[i].record));
            EXPECT_EQ(loaded.hits[i].record.value, live.hits[i].record.value);
        }
    }
}

/// Every EAR change the search reports is one the poller logged: the same
/// count, from the IN instruction, alternating levels
TEST_F(TimeTravelManager_PortReadJournal_Test, EarSearchFindsEveryEdgeTheProgramSaw)
{
    uint64_t startFrame = 0;
    Point recorded;
    RecordTapeSession(startFrame, recorded);
    if (HasFatalFailure())
        return;

    const auto r = Find(*_rec.ttd, "ear", "", 100000);
    EXPECT_EQ(r.hits.size(), _rec.LogEntries() - _logAtStart);
    ASSERT_GT(r.hits.size(), 20u);
    for (size_t i = 0; i < r.hits.size(); i++)
    {
        EXPECT_EQ(r.hits[i].record.pc, 0x800D) << "hit " << i;
        if (i > 0)
            EXPECT_NE(r.hits[i].record.value & 0x40, r.hits[i - 1].record.value & 0x40) << "hit " << i;
    }
}

/// OUTs are journaled too: "when did the program write AY register 7", and
/// what did it write
TEST_F(TimeTravelManager_PortReadJournal_Test, AyWriteSearchFindsTheOutWithItsRegister)
{
    static const uint8_t program[] = {
        0xF3,              // 8000 DI
        0x01, 0xFD, 0xFF,  // 8001 LD BC,#FFFD
        0x3E, 0x07,        // 8004 LD A,7
        0xED, 0x79,        // 8006 OUT (C),A     select R7
        0x06, 0xBF,        // 8008 LD B,#BF
        0x3E, 0x38,        // 800A LD A,#38
        0xED, 0x79,        // 800C OUT (C),A     R7 = #38 (mixer: tones on)
        0x06, 0xFF,        // 800E LD B,#FF
        0x3E, 0x08,        // 8010 LD A,8
        0xED, 0x79,        // 8012 OUT (C),A     select R8
        0x06, 0xBF,        // 8014 LD B,#BF
        0x3E, 0x0F,        // 8016 LD A,#0F
        0xED, 0x79,        // 8018 OUT (C),A     R8 = #0F
        0x18, 0xFE,        // 801A JR $
    };
    _rec.Install(program, sizeof(program));
    ASSERT_TRUE(_rec.ttd->StartRecording());
    _rec.RunToPc(0x801A);
    _rec.emulator->RunNFrames(1);
    _rec.ttd->StopRecording();
    EXPECT_GE(_rec.ttd->GetSessionInfo().portWriteCount, 4u);

    const auto r7 = Find(*_rec.ttd, "ay-write", "7");
    ASSERT_EQ(r7.hits.size(), 1u);
    EXPECT_EQ(r7.hits[0].record.value, 0x38);
    EXPECT_EQ(r7.hits[0].record.pc, 0x800C);
    EXPECT_EQ(r7.hits[0].ayRegister, 7);
    const auto r8 = Find(*_rec.ttd, "ay-write", "8");
    ASSERT_EQ(r8.hits.size(), 1u);
    EXPECT_EQ(r8.hits[0].record.value, 0x0F);
    EXPECT_TRUE(r7.hits[0].record.Time() < r8.hits[0].record.Time());
    EXPECT_EQ(Find(*_rec.ttd, "ay-select").hits.size(), 2u);
}

namespace
{
std::string WriteFile(const std::string& name, const std::string& bytes)
{
    const std::filesystem::path path = TestPathHelper::GetProcessScratchDir() / name;
    std::ofstream out(path, std::ios::binary);
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    return path.string();
}

ttd::TTDPortQuery Query(const std::string& event, const std::string& arg = "")
{
    ttd::TTDPortQuery q;
    std::string err;
    EXPECT_TRUE(ttd::BuildPortEventQuery(event, arg, q, err)) << err;
    return q;
}
}  // namespace

/// A saved session is searched on disk without loading it: the answers are the
/// recording's, and the instance keeps its own session - here a recording in
/// progress on another machine model (the file's model and ROM do not matter
/// to the journals)
TEST_F(TimeTravelManager_PortReadJournal_Test, AFileIsSearchedWithoutLoadingIt)
{
    uint64_t startFrame = 0;
    Point recorded;
    RecordKeySession(startFrame, recorded, /*rowScan=*/true);
    if (HasFatalFailure())
        return;
    const std::string path = WriteFile("keys.ttd", _rec.Save());

    Machine other;
    ASSERT_TRUE(other.Create("48K"));
    ASSERT_TRUE(other.ttd->StartRecording());
    other.emulator->RunNFrames(2);
    const auto before = other.ttd->GetSessionInfo();

    for (const char* key : {"a", "q", ""})
    {
        SCOPED_TRACE(key);
        const auto live = Find(*_rec.ttd, "key", key);
        const auto inFile = other.ttd->SearchPortEventsInFile(path, Query("key", key));
        ASSERT_TRUE(inFile.ok) << inFile.error;
        ASSERT_EQ(inFile.hits.size(), live.hits.size());
        for (size_t i = 0; i < live.hits.size(); i++)
        {
            EXPECT_EQ(inFile.hits[i].index, live.hits[i].index);
            EXPECT_TRUE(inFile.hits[i].record.SameAccess(live.hits[i].record));
        }
    }

    const auto after = other.ttd->GetSessionInfo();
    EXPECT_EQ(after.state, ttd::TTDSessionState::Recording) << "the instance's own recording goes on";
    EXPECT_EQ(after.checkpointCount, before.checkpointCount);
    EXPECT_FALSE(after.loadedFromFile);
    other.ttd->StopRecording();
    other.Destroy();
}

TEST_F(TimeTravelManager_PortReadJournal_Test, AFileSearchSaysWhyItCannotAnswer)
{
    uint64_t startFrame = 0;
    Point recorded;
    RecordKeySession(startFrame, recorded, /*rowScan=*/true);
    if (HasFatalFailure())
        return;
    const std::string file = _rec.Save();

    auto r = _play.ttd->SearchPortEventsInFile(
        (TestPathHelper::GetProcessScratchDir() / "missing.ttd").string(), Query("ear"));
    EXPECT_FALSE(r.ok);
    EXPECT_NE(r.error.find("cannot open"), std::string::npos) << r.error;

    r = _play.ttd->SearchPortEventsInFile(WriteFile("stripped.ttd", StripPortJournal(file, _rec.PortJournalBytes())),
                                          Query("ear"));
    EXPECT_FALSE(r.ok);
    EXPECT_NE(r.error.find("no port journals"), std::string::npos) << r.error;

    std::string damaged = file;
    damaged[damaged.size() - _rec.PortJournalBytes() + 60] ^= 0x5A;  // inside the first block's payload
    r = _play.ttd->SearchPortEventsInFile(WriteFile("damaged.ttd", damaged), Query("ear"));
    EXPECT_FALSE(r.ok);
    EXPECT_NE(r.error.find("port-read journal"), std::string::npos) << r.error;
}

/// Without journals a search says why instead of answering "never"
TEST_F(TimeTravelManager_PortReadJournal_Test, ASessionWithoutJournalsRefusesToSearch)
{
    ttd::TTDPortQuery q;
    std::string err;
    ASSERT_TRUE(ttd::BuildPortEventQuery("key", "", q, err));
    const auto r = _rec.ttd->SearchPortEvents(q);
    EXPECT_FALSE(r.ok);
    EXPECT_NE(r.error.find("no port journal"), std::string::npos) << r.error;
}

// ---------------------------------------------------------------------------
// Configurations the first version does not isolate
// ---------------------------------------------------------------------------

class TimeTravelManager_PortReadJournalNeoGS_Test : public ::testing::Test
{
protected:
    // ATM710's shipped config fits NeoGS in the GS slot
    SoundCardScope _soundCards{TestSound::GeneralSound};
    Machine _m;

    void SetUp() override { ASSERT_TRUE(_m.Create("ATM710")); }
    void TearDown() override { _m.Destroy(); }
};

/// NeoGS serves host memory reads through ZX-DMA without an IN: a journal of
/// IN results would not isolate the replay, so none is recorded and the
/// session says why. With the classic card in the slot the journal is back
TEST_F(TimeTravelManager_PortReadJournalNeoGS_Test, NeoGSSessionsRecordNoJournalAndSayWhy)
{
    GeneralSoundCard* card = _m.context->pSoundManager->getGeneralSound();
    ASSERT_NE(card, nullptr);
    ASSERT_EQ(card->implementation(), GSCardImplementation::NGS);

    ASSERT_TRUE(_m.ttd->StartRecording());
    _m.emulator->RunNFrames(2);
    EXPECT_EQ(_m.context->ttdPortReads, nullptr) << "the CPU hook stays off";
    _m.ttd->StopRecording();
    const auto info = _m.ttd->GetSessionInfo();
    EXPECT_FALSE(info.portJournalActive);
    EXPECT_NE(info.portJournalOffReason.find("NeoGS"), std::string::npos) << info.portJournalOffReason;
    EXPECT_EQ(info.portReadCount, 0u);
    EXPECT_EQ(FlagsOf(_m.Save()) & ttd::dump::kFlagsHasPortJournals, 0);

    ASSERT_TRUE(_m.context->pSoundManager->switchGeneralSoundCard(GSTypeKind::Z80));
    _m.Install(kKeyPoller, sizeof(kKeyPoller));
    ASSERT_TRUE(_m.ttd->StartRecording());
    _m.emulator->RunNFrames(2);
    _m.ttd->StopRecording();
    EXPECT_TRUE(_m.ttd->GetSessionInfo().portJournalActive);
    EXPECT_GT(_m.ttd->GetSessionInfo().portReadCount, 0u) << "the key poller's reads";
}

// ---------------------------------------------------------------------------
// Recorded fixtures: the emulator's answers when they were recorded
// ---------------------------------------------------------------------------

/// testdata/ttd/port-journals/: real sessions (Dizzy X with keys pressed, Green
/// Beret loading from tape) and every question the WebAPI answered when they
/// were recorded (expected.json, scripts/record_port_journal_fixtures.py). The
/// file search must answer each the same - and so must the analyzer
/// (tools/verification/ttd-analyzer/tests/test_port_search.py)
TEST(TimeTravelManager_PortJournalFixture_Test, FileSearchesAnswerAsTheEmulatorDidWhenRecording)
{
    const auto dir = TestPathHelper::FindProjectRoot() / "testdata/ttd/port-journals";
    std::ifstream in(dir / "expected.json");
    ASSERT_TRUE(in) << "testdata/ttd/port-journals/expected.json is missing";
    Json::Value expected;
    Json::CharReaderBuilder builder;
    std::string err;
    ASSERT_TRUE(Json::parseFromStream(builder, in, &expected, &err)) << err;

    Machine m;
    ASSERT_TRUE(m.Create());
    size_t checked = 0;
    for (const Json::Value& fixture : expected["fixtures"])
    {
        const std::string path = (dir / fixture["file"].asString()).string();
        for (const Json::Value& item : fixture["answers"])
        {
            const Json::Value& query = item["query"];
            const Json::Value& answer = item["answer"];
            SCOPED_TRACE(fixture["file"].asString() + " " + query.toStyledString());

            ttd::TTDPortQuery q;
            ASSERT_TRUE(ttd::BuildPortEventQuery(query["event"].asString(),
                                                 query.isMember("arg") ? query["arg"].asString() : "", q, err))
                << err;
            for (const std::string& name : query.getMemberNames())
            {
                if (name == "event" || name == "arg")
                    continue;
                const Json::Value& v = query[name];
                const std::string text = v.isBool() ? (v.asBool() ? "true" : "false")
                                         : v.isString() ? v.asString()
                                                        : std::to_string(v.asLargestUInt());
                ASSERT_TRUE(ttd::ApplyPortQueryOption(q, name, text, err)) << err;
            }
            const ttd::TTDPortSearchResult r = m.ttd->SearchPortEventsInFile(path, q);
            ASSERT_TRUE(r.ok) << r.error;
            EXPECT_EQ(std::string(ttd::PortDirectionName(q.direction)), answer["direction"].asString());
            EXPECT_EQ(r.truncated, answer["truncated"].asBool());
            EXPECT_EQ(r.scanned, answer["scanned"].asUInt64());
            ASSERT_EQ(r.hits.size(), answer["hits"].size());
            for (Json::ArrayIndex i = 0; i < answer["hits"].size(); i++)
            {
                const Json::Value& h = answer["hits"][i];
                const ttd::TTDPortHit& hit = r.hits[i];
                EXPECT_EQ(hit.index, h["index"].asUInt64());
                EXPECT_EQ(hit.record.frame, h["frame"].asUInt64());
                EXPECT_EQ(hit.record.tInFrame, h["tinframe"].asUInt());
                EXPECT_EQ(hit.record.port, h["port"].asUInt());
                EXPECT_EQ(hit.record.value, h["value"].asUInt());
                EXPECT_EQ(hit.record.pc, h["pc"].asUInt());
                EXPECT_EQ(hit.ayRegister, h.isMember("ay_register") ? h["ay_register"].asInt() : -1);
            }
            checked++;
        }
    }
    EXPECT_GE(checked, 20u);
    m.Destroy();
}
