// TTD replay check (docs/inprogress/2026-11-05-ttd-replay-check/README.md): each recorded frame replayed from its
// checkpoint and compared with the next checkpoint, the state the live machine had there.

#include <gtest/gtest.h>

#include <sstream>
#include <algorithm>
#include <string>
#include <vector>

#include "_helpers/emulatortesthelper.h"
#include "base/featuremanager.h"
#include "debugger/ttd/timetravelcontroller.h"
#include "debugger/ttd/ttdcontrol.h"
#include "debugger/ttd/ttdreplaycheck.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/io/keyboard/keyboard.h"
#include "emulator/memory/memory.h"
#include "emulator/memory/next/nextmemory.h"

namespace
{

class TTDReplayCheck_Test : public ::testing::Test
{
protected:
    Emulator* _emulator = nullptr;
    EmulatorContext* _context = nullptr;
    ttd::TimeTravelController* _ttd = nullptr;

    void TearDown() override
    {
        if (_emulator)
            EmulatorTestHelper::CleanupEmulator(_emulator);
    }

    /// The model boots its ROM for @p frames frames while recording
    void Record(const std::string& model, unsigned frames)
    {
        _emulator = EmulatorTestHelper::CreateStandardEmulator(model, LoggerLevel::LogError);
        ASSERT_NE(_emulator, nullptr);
        _context = _emulator->GetContext();
        _context->pFeatureManager->setFeature(Features::kDebugMode, true);
        _context->pFeatureManager->setFeature(Features::kTimeTravel, true);
        _context->pMemory->UpdateFeatureCache();
        _emulator->EnableTurboMode();
        _ttd = _context->pTimeTravelController;
        ASSERT_NE(_ttd, nullptr);
        ASSERT_TRUE(_ttd->StartRecording());
        _emulator->RunNFrames(frames, /*skipBreakpoints=*/true);
        _ttd->StopRecording();
    }

    /// The check over the whole history; the machine must stay where it was
    ttd::TTDReplayCheckResult Check(ttd::TTDReplayCheckQuery q = {})
    {
        const ttd::TTDTimePoint before = _ttd->CurrentPosition();
        const uint16_t pc = _context->pCore->GetZ80()->pc;
        ttd::TTDReplayCheckResult r = _ttd->QueryReplayCheck(q);
        EXPECT_EQ(_ttd->CurrentPosition().frame, before.frame);
        EXPECT_EQ(_ttd->CurrentPosition().tInFrame, before.tInFrame);
        EXPECT_EQ(_context->pCore->GetZ80()->pc, pc);
        return r;
    }
};

/// R1: a ROM boot replays exactly: every frame checked matches its next checkpoint
TEST_F(TTDReplayCheck_Test, R1_RomBootReplaysExactly)
{
    Record("PENTAGON", 12);
    const ttd::TTDReplayCheckResult r = Check();
    ASSERT_TRUE(r.error.empty()) << r.error;
    EXPECT_GT(r.checked, 8u);
    EXPECT_EQ(r.diverged, 0u) << ttd::TTDReplayCheckText(r);
    EXPECT_EQ(r.matched, r.checked);
    EXPECT_EQ(r.checked + r.skippedBarrier + r.skippedCut + r.skippedNoNext, r.toFrame - r.fromFrame + 1);
}

/// R2: a stored checkpoint damaged on purpose (one bit of one RAM piece): the frame before it diverges, and the check
/// names the RAM page and the offset; the other frames still match
TEST_F(TTDReplayCheck_Test, R2_ADamagedCheckpointIsNamed)
{
    Record("PENTAGON", 12);
    ttd::TimeTravelEngine& engine = _ttd->GetEngine();
    const uint64_t first = _ttd->GetSessionInfo().sessionStartFrame;
    const uint64_t damagedFrame = first + 6;
    const int64_t index = engine.CheckpointIndexOf({0, damagedFrame, 0});
    ASSERT_GE(index, 0);
    // Page 0 piece 0 holds the ROM's system variables area on no machine: take any piece the session knows there
    uint32_t piece = UINT32_MAX;
    for (uint32_t p = 0; p < 8 * 4 && piece == UINT32_MAX; ++p)
        if (engine.VersionAt(size_t(index), 0, p) != ttd::TimeTravelEngine::kAbsent)
            piece = p;
    ASSERT_NE(piece, UINT32_MAX);
    ASSERT_TRUE(engine.DamageForTesting(size_t(index), 0, piece));

    const ttd::TTDReplayCheckResult r = Check();
    ASSERT_TRUE(r.error.empty()) << r.error;
    ASSERT_GE(r.diverged, 1u) << ttd::TTDReplayCheckText(r);
    bool named = false;
    for (const ttd::TTDReplayCheckFrame& f : r.frames)
        for (const ttd::TTDReplayCheckItem& i : f.items)
            if (i.section == "ram" && i.name == "page " + std::to_string(piece / 4))
                named = true;
    EXPECT_TRUE(named) << ttd::TTDReplayCheckText(r);
    EXPECT_EQ(r.frames.front().frame, damagedFrame - 1) << ttd::TTDReplayCheckText(r);
}

/// R3: range, stride and limit
TEST_F(TTDReplayCheck_Test, R3_RangeStrideLimit)
{
    Record("PENTAGON", 12);
    const uint64_t first = _ttd->GetSessionInfo().sessionStartFrame;
    ttd::TTDReplayCheckQuery q;
    q.hasFrom = true;
    q.fromFrame = first + 2;
    q.hasTo = true;
    q.toFrame = first + 8;
    q.stride = 3;
    const ttd::TTDReplayCheckResult r = Check(q);
    ASSERT_TRUE(r.error.empty()) << r.error;
    EXPECT_EQ(r.checked + r.skippedBarrier + r.skippedCut + r.skippedNoNext, 3u) << "frames +2, +5, +8";

    q.toFrame = _ttd->SessionEndPosition().frame;   // no next checkpoint
    EXPECT_FALSE(Check(q).error.empty());
}

/// R4: the verb on the shared control layer (every plane forwards to it)
TEST_F(TTDReplayCheck_Test, R4_TheVerb)
{
    Record("PENTAGON", 6);
    ttd::TTDControl control(_context);
    const ttd::TTDReply ok = control.Execute({"replay-check", {{"text", "true"}}});
    ASSERT_TRUE(ok.Ok()) << ok.message;
    ASSERT_NE(ok.body.find("text"), nullptr);
    EXPECT_NE(ok.body.find("text")->s.find("replay check f"), std::string::npos) << ok.body.find("text")->s;
    EXPECT_EQ(ok.body.find("diverged")->i, 0);
    EXPECT_GT(ok.body.find("checked")->i, 0);
    ASSERT_NE(ok.body.find("skipped"), nullptr);

    EXPECT_FALSE(control.Execute({"replay-check", {{"stride", "0"}}}).Ok());
    EXPECT_FALSE(control.Execute({"replay-check", {{"limit", "0"}}}).Ok());
    EXPECT_FALSE(control.Execute({"replay-check", {{"from_frame", "999999"}}}).Ok()) << "outside the history";
}

/// S1: check_seeks on: a seek into a frame checks that frame's replay; a damaged checkpoint after it is counted as a
/// divergence, with its text; off, a seek checks nothing
TEST_F(TTDReplayCheck_Test, S1_SeeksCheckTheirFrame)
{
    Record("PENTAGON", 12);
    const uint64_t first = _ttd->GetSessionInfo().sessionStartFrame;
    ttd::TTDSeekResult plain;
    ASSERT_TRUE(_ttd->SeekTo({first + 3, 100}, &plain));
    EXPECT_FALSE(plain.checked) << "off by default";

    _ttd->SetCheckSeeks(true);
    ttd::TTDSeekResult ok;
    ASSERT_TRUE(_ttd->SeekTo({first + 4, 100}, &ok));
    EXPECT_TRUE(ok.checked);
    EXPECT_FALSE(ok.checkDiverged) << ok.checkText;
    EXPECT_EQ(ok.arrivedAt.frame, first + 4);
    const uint16_t pc = _context->pCore->GetZ80()->pc;

    // Damage checkpoint +7: the replay of frame +6 no longer reaches it
    ttd::TimeTravelEngine& engine = _ttd->GetEngine();
    const int64_t index = engine.CheckpointIndexOf({0, first + 7, 0});
    ASSERT_GE(index, 0);
    uint32_t piece = UINT32_MAX;
    for (uint32_t p = 0; p < 8 * 4 && piece == UINT32_MAX; ++p)
        if (engine.VersionAt(size_t(index), 0, p) != ttd::TimeTravelEngine::kAbsent)
            piece = p;
    ASSERT_NE(piece, UINT32_MAX);
    ASSERT_TRUE(engine.DamageForTesting(size_t(index), 0, piece));
    ttd::TTDSeekResult bad;
    ASSERT_TRUE(_ttd->SeekTo({first + 6, 100}, &bad));
    EXPECT_TRUE(bad.checked);
    EXPECT_TRUE(bad.checkDiverged);
    EXPECT_NE(bad.checkText.find("ram page"), std::string::npos) << bad.checkText;
    EXPECT_EQ(_ttd->CurrentPosition().frame, first + 6) << "the check puts the machine back where the seek landed";

    const ttd::TTDSessionInfo info = _ttd->GetSessionInfo();
    EXPECT_TRUE(info.checkSeeks);
    EXPECT_EQ(info.seekChecks, 2u);
    EXPECT_EQ(info.seekCheckDivergences, 1u);
    EXPECT_EQ(info.lastSeekDivergence, bad.checkText);

    // The same seek as before the damage lands on the same machine
    ASSERT_TRUE(_ttd->SeekTo({first + 4, 100}, &ok));
    EXPECT_EQ(_context->pCore->GetZ80()->pc, pc);
}

/// S2: the verb and the seek's answer on the shared control layer
TEST_F(TTDReplayCheck_Test, S2_SeekCheckVerb)
{
    Record("PENTAGON", 8);
    ttd::TTDControl control(_context);
    ttd::TTDReply r = control.Execute({"seek-check", {}});
    ASSERT_TRUE(r.Ok()) << r.message;
    EXPECT_FALSE(r.body.find("enabled")->b);
    r = control.Execute({"seek-check", {{"enabled", "true"}}});
    ASSERT_TRUE(r.Ok()) << r.message;
    EXPECT_TRUE(r.body.find("enabled")->b);
    EXPECT_FALSE(control.Execute({"seek-check", {{"enabled", "maybe"}}}).Ok());

    const uint64_t first = _ttd->GetSessionInfo().sessionStartFrame;
    const ttd::TTDReply seek =
        control.Execute({"seek", {{"frame", std::to_string(first + 3)}, {"tinframe", "200"}}});
    ASSERT_TRUE(seek.Ok()) << seek.message;
    const StateNode* check = seek.body.find("replay_check");
    ASSERT_NE(check, nullptr);
    EXPECT_FALSE(check->find("diverged")->b) << check->find("text")->s;

    const ttd::TTDReply status = control.Execute({"status", {}});
    ASSERT_TRUE(status.Ok());
    EXPECT_TRUE(status.body.find("check_seeks")->b);
    EXPECT_EQ(status.body.find("seek_checks")->i, 1);
    EXPECT_EQ(status.body.find("seek_check_divergences")->i, 0);
}

/// R6: the ZX Next DMA reads a port on its own, past the CPU's IN and so past the port journal: every frame a
/// port-to-memory block of 64 reads of #FE while a key goes down and up through the journaled input. The replayed
/// DMA reads the live keyboard, which the replay drives from the same input journal: the frames still match
TEST_F(TTDReplayCheck_Test, R6_NextDmaReadingAPortReplaysExactly)
{
    _emulator = EmulatorTestHelper::CreateStandardEmulator("NEXT", LoggerLevel::LogError);
    ASSERT_NE(_emulator, nullptr);
    _context = _emulator->GetContext();
    _context->pFeatureManager->setFeature(Features::kDebugMode, true);
    _context->pFeatureManager->setFeature(Features::kTimeTravel, true);
    _context->pMemory->UpdateFeatureCache();
    _emulator->EnableTurboMode();
    _ttd = _context->pTimeTravelController;
    _emulator->RunNFrames(4, /*skipBreakpoints=*/true);   // the boot sets the board up

    constexpr uint16_t kCode = 0x8000, kDmaProgram = 0x8100, kBlock = 0xC000;
    // DI; loop: LD HL,dma; LD BC,#0D6B (13 bytes to port #6B); OTIR; LD B,0; DJNZ $; JR loop
    const uint8_t code[] = {0xF3, 0x21, 0x00, 0x81, 0x01, 0x6B, 0x0D, 0xED, 0xB3, 0x06, 0x00, 0x10, 0xFE, 0x18, 0xF2};
    const uint8_t dma[] = {
        0x83,                          // WR6: disable
        0x7D, 0xFE, 0x00, 0x40, 0x00,  // WR0: A -> B, port A #00FE, length 64
        0x2C,                          // WR1: port A is I/O, fixed
        0x10,                          // WR2: port B is memory, increment
        0xAD, 0x00, 0xC0,              // WR4: continuous, port B #C000
        0xCF,                          // WR6: load
        0x87,                          // WR6: enable
    };
    static_assert(sizeof(dma) == 13);
    for (size_t i = 0; i < sizeof(code); ++i)
        _context->pMemory->DirectWriteToZ80Memory(static_cast<uint16_t>(kCode + i), code[i]);
    for (size_t i = 0; i < sizeof(dma); ++i)
        _context->pMemory->DirectWriteToZ80Memory(static_cast<uint16_t>(kDmaProgram + i), dma[i]);
    _context->pCore->GetZ80()->pc = kCode;

    ASSERT_TRUE(_ttd->StartRecording());
    _emulator->RunNFrames(3, /*skipBreakpoints=*/true);
    ttd::TTDInputEvent key;
    key.kind = ttd::TTDInputKind::Key;
    key.key = ZXKEY_SPACE;
    key.pressed = true;
    ASSERT_TRUE(_ttd->SubmitLiveInput(key));
    _emulator->RunNFrames(3, /*skipBreakpoints=*/true);
    bool keySeen = false;  // the DMA moved the keyboard's answer: a row with a key down
    for (uint16_t i = 0; i < 64; ++i)
        keySeen |= (_context->pMemory->DirectReadFromZ80Memory(static_cast<uint16_t>(kBlock + i)) & 0x1F) != 0x1F;
    key.pressed = false;
    ASSERT_TRUE(_ttd->SubmitLiveInput(key));
    _emulator->RunNFrames(3, /*skipBreakpoints=*/true);
    _ttd->StopRecording();
    ASSERT_TRUE(keySeen) << "the DMA block at #C000 holds no pressed key: the transfer did not run";

    const ttd::TTDReplayCheckResult r = Check();
    ASSERT_TRUE(r.error.empty()) << r.error;
    EXPECT_GE(r.checked, 7u) << ttd::TTDReplayCheckText(r);
    EXPECT_EQ(r.diverged, 0u) << ttd::TTDReplayCheckText(r);
}

/// R7 (088897e5b): on the Next the MMU is the paging, #7FFD only one way to set it. A program pages RAM into slots
/// 0, 1, 6 and 7 by NEXTREG (slot 6 an odd 8K page: the other half of its 16K page); a seek to any recorded moment
/// shows the MMU and the bytes behind it as they were, not slots re-derived from #7FFD; the frames replay exactly
TEST_F(TTDReplayCheck_Test, R7_NextSeekKeepsTheRestoredMmu)
{
    _emulator = EmulatorTestHelper::CreateStandardEmulator("NEXT", LoggerLevel::LogError);
    ASSERT_NE(_emulator, nullptr);
    _context = _emulator->GetContext();
    _context->pFeatureManager->setFeature(Features::kDebugMode, true);
    _context->pFeatureManager->setFeature(Features::kTimeTravel, true);
    _context->pMemory->UpdateFeatureCache();
    _emulator->EnableTurboMode();
    _ttd = _context->pTimeTravelController;
    auto* next = dynamic_cast<NextMemory*>(_context->pMemory);
    ASSERT_NE(next, nullptr);
    _emulator->RunNFrames(4, /*skipBreakpoints=*/true);   // the boot sets the board up

    constexpr uint8_t kMmu[8] = {20, 21, 10, 11, 4, 5, 33, 40};
    // DI; NEXTREG #50,20 / #51,21 / #56,33 / #57,40; markers in slots 0, 6, 7; loop: INC (#E001); JR loop
    const uint8_t code[] = {0xF3,
                            0xED, 0x91, 0x50, kMmu[0], 0xED, 0x91, 0x51, kMmu[1],
                            0xED, 0x91, 0x56, kMmu[6], 0xED, 0x91, 0x57, kMmu[7],
                            0x3E, 0xA1, 0x32, 0x00, 0x00,    // LD A,#A1 : LD (#0000),A
                            0x3E, 0xA6, 0x32, 0x00, 0xC0,    // LD A,#A6 : LD (#C000),A
                            0x3E, 0xA7, 0x32, 0x00, 0xE0,    // LD A,#A7 : LD (#E000),A
                            0x21, 0x01, 0xE0, 0x34, 0x18, 0xFD};   // LD HL,#E001 : INC (HL) : JR $-1
    for (size_t i = 0; i < sizeof(code); ++i)
        _context->pMemory->DirectWriteToZ80Memory(static_cast<uint16_t>(0x8000 + i), code[i]);
    _context->pCore->GetZ80()->pc = 0x8000;

    struct Seen
    {
        ttd::TTDTimePoint at;
        uint8_t mmu[8] = {};
        uint8_t bytes[4] = {};
    };
    auto see = [&]() {
        Seen s;
        s.at = _ttd->CurrentPosition();
        for (unsigned slot = 0; slot < 8; ++slot)
            s.mmu[slot] = next->GetMmu(slot);
        const uint16_t at[4] = {0x0000, 0xC000, 0xE000, 0xE001};
        for (int i = 0; i < 4; ++i)
            s.bytes[i] = _context->pMemory->DirectReadFromZ80Memory(at[i]);
        return s;
    };
    ASSERT_TRUE(_ttd->StartRecording());
    std::vector<Seen> seen;
    for (int f = 0; f < 6; ++f)
    {
        _emulator->RunNFrames(1, /*skipBreakpoints=*/true);
        seen.push_back(see());
    }
    _ttd->StopRecording();
    for (unsigned slot = 0; slot < 8; ++slot)
        ASSERT_EQ(seen.back().mmu[slot], kMmu[slot]) << "the program paged slot " << slot;
    EXPECT_EQ(seen.back().bytes[0], 0xA1);
    EXPECT_EQ(seen.back().bytes[2], 0xA7);

    // The live machine pages back to the reset MMU: a seek must bring the program's MMU back
    for (unsigned slot = 0; slot < 8; ++slot)
        next->SetMmu(slot, NextMemory::kResetMmu[slot]);
    for (size_t i : {size_t(1), size_t(3), seen.size() - 1})
    {
        SCOPED_TRACE("frame " + std::to_string(seen[i].at.frame));
        ASSERT_TRUE(_ttd->SeekTo(seen[i].at));
        const Seen now = see();
        for (unsigned slot = 0; slot < 8; ++slot)
            EXPECT_EQ(now.mmu[slot], seen[i].mmu[slot]) << "slot " << slot;
        for (int b = 0; b < 4; ++b)
            EXPECT_EQ(now.bytes[b], seen[i].bytes[b]) << "byte " << b;
    }

    const ttd::TTDReplayCheckResult r = Check();
    ASSERT_TRUE(r.error.empty()) << r.error;
    EXPECT_GE(r.checked, 4u) << ttd::TTDReplayCheckText(r);
    EXPECT_EQ(r.diverged, 0u) << ttd::TTDReplayCheckText(r);
}

/// R8: the Next's system area (the ROMs its firmware loads, the DivMMC RAM) is machine state, not the ROM set. A
/// session recorded after the boot loads into a machine that has not booted, and a seek there shows the area as
/// recorded
TEST_F(TTDReplayCheck_Test, R8_NextSessionLoadsIntoAFreshMachine)
{
    _emulator = EmulatorTestHelper::CreateStandardEmulator("NEXT", LoggerLevel::LogError);
    ASSERT_NE(_emulator, nullptr);
    _context = _emulator->GetContext();
    _context->pFeatureManager->setFeature(Features::kDebugMode, true);
    _context->pFeatureManager->setFeature(Features::kTimeTravel, true);
    _context->pMemory->UpdateFeatureCache();
    _emulator->EnableTurboMode();
    _ttd = _context->pTimeTravelController;
    _emulator->RunNFrames(4, /*skipBreakpoints=*/true);   // the firmware fills the system area
    const size_t areaBytes = static_cast<size_t>(NextMemory::kSystemAreaPages) * PAGE_SIZE;
    ASSERT_TRUE(_ttd->StartRecording());
    _emulator->RunNFrames(3, /*skipBreakpoints=*/true);
    const ttd::TTDTimePoint at = _ttd->CurrentPosition();
    const std::vector<uint8_t> area(_context->pMemory->ROMBase(), _context->pMemory->ROMBase() + areaBytes);
    _ttd->StopRecording();
    std::stringstream session;
    std::string error;
    ASSERT_TRUE(_ttd->SerializeSession(session, error)) << error;

    Emulator* fresh = EmulatorTestHelper::CreateStandardEmulator("NEXT", LoggerLevel::LogError);
    ASSERT_NE(fresh, nullptr);
    EmulatorContext* freshContext = fresh->GetContext();
    freshContext->pFeatureManager->setFeature(Features::kDebugMode, true);
    freshContext->pFeatureManager->setFeature(Features::kTimeTravel, true);
    freshContext->pMemory->UpdateFeatureCache();
    ttd::TimeTravelController* freshTtd = freshContext->pTimeTravelController;
    session.seekg(0);
    EXPECT_TRUE(freshTtd->DeserializeSession(session, error)) << error;
    if (error.empty())
    {
        EXPECT_TRUE(freshTtd->SeekTo(at));
        const uint8_t* now = freshContext->pMemory->ROMBase();
        EXPECT_TRUE(std::equal(area.begin(), area.end(), now)) << "the system area as recorded";
    }
    EmulatorTestHelper::CleanupEmulator(fresh);
}

/// R5: every model the TTD tests boot: the ROM boot's replay matches each next checkpoint
class TTDReplayCheckModels_Test : public TTDReplayCheck_Test, public ::testing::WithParamInterface<const char*>
{
};

TEST_P(TTDReplayCheckModels_Test, R5_RomBootReplaysExactly)
{
    ASSERT_NO_FATAL_FAILURE(Record(GetParam(), 10));
    const ttd::TTDReplayCheckResult r = Check();
    ASSERT_TRUE(r.error.empty()) << r.error;
    EXPECT_GT(r.checked, 0u);
    EXPECT_EQ(r.diverged, 0u) << GetParam() << "\n" << ttd::TTDReplayCheckText(r);
    RecordProperty("checked", static_cast<int>(r.checked));
    RecordProperty("ms", static_cast<int>(r.ms));
}

INSTANTIATE_TEST_SUITE_P(AcrossModels, TTDReplayCheckModels_Test,
                         ::testing::Values("48K", "128k", "PENTAGON", "PLUS2A", "PLUS3", "SCORPION", "PROFSCORP", "GMX",
                                           "PROFI", "PROFI3", "ATM710", "ATM450", "ATM3", "TSL", "SPRINTER", "NEXT",
                                           "PLUS2"),
                         [](const ::testing::TestParamInfo<const char*>& info) { return std::string(info.param); });

}  // namespace
