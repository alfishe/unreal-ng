#include <gtest/gtest.h>

#include <cstring>
#include <random>
#include <sstream>
#include <string>

#include "_helpers/emulatortesthelper.h"
#include "_helpers/gsslot.h"
#include "_helpers/soundcardscope.h"
#include "base/featuremanager.h"
#include "debugger/debugmanager.h"
#include "debugger/keyboard/debugkeyboardmanager.h"
#include "debugger/ttd/ttdprobe.h"
#include "emulator/io/keyboard/keyboard.h"
#include "debugger/ttd/timetravelmanager.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/memory/memory.h"
#include "emulator/sound/chips/neogs/soundchip_neogs.h"
#include "emulator/sound/soundmanager.h"

/// A resume from the past (ResumeRecordingFrom: seek back, keep recording, the
/// future after the point is discarded) and saving a session: defects found
/// while documenting TTD v1 (ttd-v1-architecture-and-format.md §9.3), each
/// confirmed by its test failing before the fix:
///   - a session saved after a resume did not load (page-store slots reused
///     newest-first broke the file's "prev_slot before the slot" order);
///   - resuming a loaded session collected no coverage;
///   - the coverage index kept the discarded future, so a reverse search
///     skipped a read of the new history;
///   - a NeoGS session with incompressible card RAM saved but did not load
///     (the reader's 1 MiB blob cap, unchecked by the writer).
///
/// Runtime justification: each test records 30-60 frames of a small program
/// and replays some of them (~0.1-0.2 s).
class TimeTravelManager_ResumeSave_Test : public ::testing::Test
{
protected:
    SoundCardScope _gs{TestSound::GeneralSound};
    Emulator* _emulator = nullptr;
    EmulatorContext* _context = nullptr;
    ttd::TimeTravelManager* _ttd = nullptr;

    void SetUp() override
    {
        _emulator = EmulatorTestHelper::CreateStandardEmulator("PENTAGON", LoggerLevel::LogError);
        ASSERT_NE(_emulator, nullptr);
        _context = _emulator->GetContext();
        _ttd = _context->pTimeTravelManager;
        FeatureManager* features = _emulator->GetFeatureManager();
        features->setFeature(Features::kDebugMode, true);
        features->setFeature(Features::kTimeTravel, true);
        _context->pMemory->UpdateFeatureCache();
    }

    void TearDown() override
    {
        if (_emulator)
            EmulatorTestHelper::CleanupEmulator(_emulator);
    }

    /// A program that rewrites a different part of RAM every pass: every
    /// frame dirties several sub-pages with new contents, so every delta frame
    /// creates XorPrev slots chained to earlier ones
    void InstallScribbler()
    {
        static const uint8_t program[] = {
            0xF3,              // 8000 DI
            0x21, 0x00, 0x60,  // 8001 LD HL,#6000
            0x3C,              // 8004 loop: INC A
            0x77,              // 8005 LD (HL),A
            0x23,              // 8006 INC HL
            0x7C,              // 8007 LD A,H  -- wrap #7FFF -> #6000
            0xE6, 0x1F,        // 8008 AND #1F
            0xF6, 0x60,        // 800A OR #60
            0x67,              // 800C LD H,A
            0x7D,              // 800D LD A,L
            0x86,              // 800E ADD A,(HL)
            0x18, 0xF3,        // 800F JR loop
        };
        Z80* z80 = _context->pCore->GetZ80();
        for (size_t i = 0; i < sizeof(program); i++)
            z80->DirectWrite(static_cast<uint16_t>(0x8000 + i), program[i]);
        z80->pc = 0x8000;
    }

    std::string Save()
    {
        std::ostringstream out;
        std::string err;
        EXPECT_TRUE(_ttd->SerializeSession(out, err)) << err;
        return out.str();
    }

    bool Load(const std::string& file, std::string& err)
    {
        std::istringstream in(file);
        return _ttd->DeserializeSession(in, err);
    }
};

/// After a resume from the past frees slots, the page store reuses
/// them newest-first, so a later XorPrev slot may get a lower index than the
/// slot it depends on - and the reader refuses prev_slot >= index. A session
/// saved after a resume must load.
TEST_F(TimeTravelManager_ResumeSave_Test, SessionSavedAfterAResumeFromThePastLoads)
{
    _emulator->RunNFrames(5);
    InstallScribbler();
    ASSERT_TRUE(_ttd->StartRecording());
    const uint64_t start = _context->emulatorState.frame_counter;
    _emulator->RunNFrames(30);
    _ttd->StopRecording();

    ASSERT_TRUE(_ttd->SeekTo({start + 8, 0}));
    ASSERT_TRUE(_ttd->ResumeRecordingFrom({start + 8, 0}));
    _emulator->RunNFrames(40);
    _ttd->StopRecording();

    const std::string file = Save();
    std::string err;
    EXPECT_TRUE(Load(file, err)) << "a session saved after a resume from the past does not load: " << err;
}

/// Loading switches coverage collection off; resuming a loaded
/// session from the past must switch it back on (as StartRecording and
/// ResumeRecordingLive do), or the new history has no coverage index
TEST_F(TimeTravelManager_ResumeSave_Test, ResumingALoadedSessionCollectsCoverageAgain)
{
    ASSERT_TRUE(_ttd->StartRecording());
    _emulator->RunNFrames(10);
    _ttd->StopRecording();
    const uint64_t end = _ttd->GetSessionInfo().currentEndFrame;
    const std::string file = Save();

    std::string err;
    ASSERT_TRUE(Load(file, err)) << err;
    ASSERT_TRUE(_ttd->SeekTo({end - 5, 0}));
    ASSERT_TRUE(_ttd->ResumeRecordingFrom({end - 5, 0}));
    EXPECT_TRUE(_context->ttdCoverageActive) << "the resumed recording collects no coverage";
    _ttd->StopRecording();
}

/// NeoGS stores its RAM (up to 4 MB) and flash in every blob until TTD v2
/// memory regions: with incompressible RAM contents (MP3 data, a random fill
/// here) the blob exceeds 1 MiB, which the reader used to refuse while the
/// writer wrote it. The reader and the writer now share one cap
/// (kMaxPeripheralBlobBytes), and a saved session loads.
TEST_F(TimeTravelManager_ResumeSave_Test, NeoGSSessionWithIncompressibleRamLoads)
{
    ASSERT_TRUE(FitGeneralSoundCard(_context->pSoundManager, GSTypeKind::NGS));
    auto* ngs = dynamic_cast<SoundChip_NeoGS*>(_context->pSoundManager->getGeneralSound());
    ASSERT_NE(ngs, nullptr);
    std::mt19937 rng(1234);
    uint8_t* ram = ngs->memory().ram();
    for (size_t i = 0; i < ngs->memory().ramSize(); i++)
        ram[i] = static_cast<uint8_t>(rng());

    ASSERT_TRUE(_ttd->StartRecording());
    _emulator->RunNFrames(2);
    _ttd->StopRecording();

    const std::string file = Save();
    std::string err;
    EXPECT_TRUE(Load(file, err)) << "a NeoGS session saves but does not load: " << err;
}

/// A resume from the past truncates the timeline and the journals
/// but not the coverage index, so the index keeps describing the discarded
/// future. A reverse search prunes frames by the index; a stale "this frame
/// never read X" hides a read the new history made in that frame.
/// The program reads #9500 only while a key is held. The first history holds
/// no key; after the resume a key is pressed, and the read must be found.
TEST_F(TimeTravelManager_ResumeSave_Test, ReverseSearchAfterAResumeFindsTheNewHistory)
{
    static const uint8_t program[] = {
        0xF3,              // 8000 DI
        0xAF,              // 8001 loop: XOR A
        0xDB, 0xFE,        // 8002 IN A,(#FE)
        0xE6, 0x1F,        // 8004 AND #1F
        0xFE, 0x1F,        // 8006 CP #1F
        0x28, 0xF7,        // 8008 JR Z,loop      no key
        0x3A, 0x00, 0x95,  // 800A LD A,(#9500)   the read being searched for
        0x18, 0xF2,        // 800D JR loop
    };
    _emulator->RunNFrames(5);
    Z80* z80 = _context->pCore->GetZ80();
    for (size_t i = 0; i < sizeof(program); i++)
        z80->DirectWrite(static_cast<uint16_t>(0x8000 + i), program[i]);
    z80->pc = 0x8000;

    ASSERT_TRUE(_ttd->StartRecording());
    const uint64_t start = _context->emulatorState.frame_counter;
    _emulator->RunNFrames(30);  // no key: #9500 never read
    _ttd->StopRecording();

    ASSERT_TRUE(_ttd->SeekTo({start + 4, 0}));
    ASSERT_TRUE(_ttd->ResumeRecordingFrom({start + 4, 0}));
    _emulator->RunNFrames(6);
    auto* keys = _context->pDebugManager->GetKeyboardManager();
    keys->PressKey(ZXKEY_A);
    _emulator->RunNFrames(1);  // reads #9500 in frame start+11
    keys->ReleaseKey(ZXKEY_A);
    _emulator->RunNFrames(20);
    _ttd->StopRecording();

    ttd::TTDSearchQuery query;
    query.addrFrom = 0x9500;
    query.addrTo = 0x9500;
    query.access = ttd::TTDAccessType::Read;
    ASSERT_TRUE(_ttd->SeekTo({_ttd->GetSessionInfo().currentEndFrame, 0}));
    const auto hit = _ttd->FindLastAccess(query);
    ASSERT_TRUE(hit.has_value()) << "the read in the new history was not found (stale coverage index)";
    EXPECT_GE(hit->time.frame, start + 10);
}

/// Control for the test above: the same program and key in one straight
/// recording - the read is found. Separates "stale index" from "the search
/// cannot see this read at all"
TEST_F(TimeTravelManager_ResumeSave_Test, ReverseSearchFindsTheReadInAStraightRecording)
{
    static const uint8_t program[] = {
        0xF3, 0xAF, 0xDB, 0xFE, 0xE6, 0x1F, 0xFE, 0x1F, 0x28, 0xF7, 0x3A, 0x00, 0x95, 0x18, 0xF2,
    };
    _emulator->RunNFrames(5);
    Z80* z80 = _context->pCore->GetZ80();
    for (size_t i = 0; i < sizeof(program); i++)
        z80->DirectWrite(static_cast<uint16_t>(0x8000 + i), program[i]);
    z80->pc = 0x8000;

    ASSERT_TRUE(_ttd->StartRecording());
    const uint64_t start = _context->emulatorState.frame_counter;
    _emulator->RunNFrames(10);
    auto* keys = _context->pDebugManager->GetKeyboardManager();
    keys->PressKey(ZXKEY_A);
    _emulator->RunNFrames(1);
    keys->ReleaseKey(ZXKEY_A);
    _emulator->RunNFrames(20);
    _ttd->StopRecording();

    ttd::TTDSearchQuery query;
    query.addrFrom = 0x9500;
    query.addrTo = 0x9500;
    query.access = ttd::TTDAccessType::Read;
    ASSERT_TRUE(_ttd->SeekTo({_ttd->GetSessionInfo().currentEndFrame, 0}));
    const auto hit = _ttd->FindLastAccess(query);
    ASSERT_TRUE(hit.has_value());
    EXPECT_GE(hit->time.frame, start + 10);
}

/// A mid-frame resume keeps the part of the resume frame before the resume
/// point: a read made there must still be found by a reverse search (the
/// frame is a hole in the coverage index, so the search replays it)
TEST_F(TimeTravelManager_ResumeSave_Test, MidFrameResumeKeepsTheRetainedPartOfTheFrameSearchable)
{
    static const uint8_t program[] = {
        0xF3, 0xAF, 0xDB, 0xFE, 0xE6, 0x1F, 0xFE, 0x1F, 0x28, 0xF7, 0x3A, 0x00, 0x95, 0x18, 0xF2,
    };
    _emulator->RunNFrames(5);
    Z80* z80 = _context->pCore->GetZ80();
    for (size_t i = 0; i < sizeof(program); i++)
        z80->DirectWrite(static_cast<uint16_t>(0x8000 + i), program[i]);
    z80->pc = 0x8000;

    ASSERT_TRUE(_ttd->StartRecording());
    const uint64_t start = _context->emulatorState.frame_counter;
    _emulator->RunNFrames(10);
    auto* keys = _context->pDebugManager->GetKeyboardManager();
    _emulator->RunNCPUCycles(2000);
    keys->PressKey(ZXKEY_A);
    _emulator->RunNCPUCycles(3000);  // reads #9500 early in frame start+10
    keys->ReleaseKey(ZXKEY_A);
    _emulator->RunNFrames(20);
    _ttd->StopRecording();

    // After the key's release (the key must not stay held in the new history)
    const ttd::TTDTimePoint resumeAt{start + 10, 60000};
    ASSERT_LT(_ttd->GetInputJournal().Events().back().time, resumeAt);
    ASSERT_TRUE(_ttd->SeekTo(resumeAt));
    ASSERT_TRUE(_ttd->ResumeRecordingFrom(resumeAt));
    _emulator->RunNFrames(10);  // no key: no more reads
    _ttd->StopRecording();

    ttd::TTDSearchQuery query;
    query.addrFrom = 0x9500;
    query.addrTo = 0x9500;
    query.access = ttd::TTDAccessType::Read;
    ASSERT_TRUE(_ttd->SeekTo({_ttd->GetSessionInfo().currentEndFrame, 0}));
    const auto hit = _ttd->FindLastAccess(query);
    ASSERT_TRUE(hit.has_value()) << "the read before the resume point was lost";
    EXPECT_EQ(hit->time.frame, start + 10);
}
