// The Next's sprite engine and CLK_28 in machine time: the collision flags of #303B are set by the engine's step on the line it fills
// (sprites.vhd), whether the line is drawn or not, and NR #11 moves the system clock (zxnext.vhd 5208-5217, zxnext_top_issue2.vhd 771-783).

#include "stdafx.h"
#include "pch.h"

#include <gtest/gtest.h>

#include <vector>

#include "_helpers/emulatortesthelper.h"
#include "base/featuremanager.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/io/z80n/nextstatecodec.h"
#include "emulator/memory/memory.h"
#include "emulator/ports/models/portdecoder_next.h"
#include "emulator/sound/soundmanager.h"

namespace
{
constexpr uint16_t kPortSprites = 0x303B, kPortAttributes = 0x57, kPortPatterns = 0x5B;
constexpr uint8_t kCollision = 0x01;
/// T-states of the frame: before the grid's first line (the top border) and well inside the sprites' 16 rows at grid line 0
constexpr uint32_t kBeforeSprites = 100, kAfterSprites = 20000;
}  // namespace

class NextMachineTime_Test : public ::testing::Test
{
protected:
    Emulator* _emulator = nullptr;
    EmulatorContext* _context = nullptr;
    PortDecoder_Next* _ports = nullptr;

    void SetUp() override
    {
        _emulator = EmulatorTestHelper::CreateStandardEmulator("NEXT", LoggerLevel::LogError, RamPowerOn::Zero);
        ASSERT_NE(_emulator, nullptr);
        _context = _emulator->GetContext();
        _ports = dynamic_cast<PortDecoder_Next*>(_context->pPortDecoder);
        ASSERT_NE(_ports, nullptr);
        _context->pMemory->DirectWriteToZ80Memory(0xC000, 0xF3);  // DI; JR $
        _context->pMemory->DirectWriteToZ80Memory(0xC001, 0x18);
        _context->pMemory->DirectWriteToZ80Memory(0xC002, 0xFE);
        _context->pCore->GetZ80()->pc = 0xC000;
    }
    void TearDown() override
    {
        if (_emulator)
            EmulatorTestHelper::CleanupEmulator(_emulator);
    }
    void Out(uint16_t port, uint8_t value) { _ports->DecodePortOut(port, value, 0); }
    void Frames(unsigned n)
    {
        for (unsigned i = 0; i < n; i++)
            _emulator->RunFrame(true);
    }
    /// #303B read by the program at T-state t of the frame
    uint8_t StatusAt(uint32_t t)
    {
        _context->pCore->GetZ80()->t = t;
        return _ports->DecodePortIn(kPortSprites, 0);
    }
    /// Sprites 0 and 1 on top of each other at the grid's top left, pattern 0 opaque
    void TwoSpritesOnOnePixel()
    {
        Out(kPortSprites, 0);
        for (int sprite = 0; sprite < 2; sprite++)
            for (uint8_t b : {uint8_t(32), uint8_t(32), uint8_t(0), uint8_t(0x80)})
                Out(kPortAttributes, b);
        for (int i = 0; i < 256; i++)
            Out(kPortPatterns, 0x22);
        _ports->Board().Write(0x15, 0x01);  // sprites visible
    }
    double SoundScale() const { return _context->pSoundManager ? _context->pSoundManager->getMachineClockScale() : 1.0; }
    uint32_t FrameUs(uint32_t hz) const
    {
        return static_cast<uint32_t>((static_cast<uint64_t>(_context->config.frame) * 8'000'000ull + hz - 1) / hz);
    }
};

// sprites.vhd: the collision bit is set by the engine's write on the line before the one shown, not when the picture is drawn: a read in
// the middle of the frame sees it, without the picture (ScreenHQ off, a frame drawn at its end only)
TEST_F(NextMachineTime_Test, ACollisionIsInTheStatusFromItsLineOnAndARead)
{
    _context->pFeatureManager->setFeature(Features::kScreenHQ, false);
    _context->pMemory->UpdateFeatureCache();
    Frames(1);
    TwoSpritesOnOnePixel();
    Frames(1);
    StatusAt(0);  // whatever the frame of the upload left

    EXPECT_EQ(StatusAt(kBeforeSprites) & kCollision, 0) << "the engine has not reached the sprites' lines";
    EXPECT_EQ(StatusAt(kAfterSprites) & kCollision, kCollision) << "past them: set, the frame not drawn yet";
    EXPECT_EQ(StatusAt(kAfterSprites) & kCollision, 0) << "a read clears it";
    EXPECT_EQ(StatusAt(kAfterSprites + 1000) & kCollision, 0) << "no other line collides";
    Frames(1);
    EXPECT_EQ(StatusAt(kBeforeSprites) & kCollision, 0);
    EXPECT_EQ(StatusAt(kAfterSprites) & kCollision, kCollision) << "every frame";

    // A frame nobody reads in (the rest of this one, then a whole one): the next frame's read sees the flag it set
    Frames(2);
    EXPECT_EQ(StatusAt(kBeforeSprites) & kCollision, kCollision);
}

// The picture is the same with ScreenHQ on: the engine's flags do not depend on the drawing
TEST_F(NextMachineTime_Test, TheFlagsAreTheSameWhenTheBeamDraws)
{
    _context->pFeatureManager->setFeature(Features::kScreenHQ, true);
    _context->pMemory->UpdateFeatureCache();
    Frames(1);
    TwoSpritesOnOnePixel();
    Frames(1);
    StatusAt(0);
    EXPECT_EQ(StatusAt(kBeforeSprites) & kCollision, 0);
    EXPECT_EQ(StatusAt(kAfterSprites) & kCollision, kCollision);
    EXPECT_EQ(StatusAt(kAfterSprites) & kCollision, 0);
}

// NR #11 on the issue 2 core: config mode only, 7 is VGA 0, otherwise bit 0. A change is the top level's power-on reset: the core is reset
// as by a soft one, config mode and NR #11 stay; the pacing, the sound's rate and the UARTs follow the new CLK_28
TEST_F(NextMachineTime_Test, Nr11MovesCLK28InConfigModeAndResets)
{
    NextBoard& board = _ports->Board();
    const uint32_t frameUs = _context->config.frame_duration_us;
    ASSERT_EQ(frameUs, FrameUs(28000000));

    board.Write(0x03, 0x01);  // out of config mode
    board.Write(0x11, 0x01);
    EXPECT_EQ(board.Read(0x11), 0) << "outside config mode the write is ignored";

    board.Write(0x03, 0x07);  // config mode
    board.Write(0x11, 0x01);
    EXPECT_EQ(board.Read(0x11), 1);
    _context->pCore->GetZ80()->pc = 0xC000;
    board.AfterInstruction();
    EXPECT_EQ(_context->pCore->GetZ80()->pc, 0) << "the change resets the machine";
    EXPECT_EQ(board.Read(0x11), 1) << "NR #11 stays";
    EXPECT_EQ(board.SystemClockHz(), 28571429u) << "VGA 1";
    EXPECT_EQ(_context->config.frame_duration_us, FrameUs(28571429));
    EXPECT_LT(_context->config.frame_duration_us, frameUs);
    EXPECT_EQ(_ports->Uart().SystemClock(), 28571429u);
    if (_context->pSoundManager)
        EXPECT_DOUBLE_EQ(SoundScale(), 28571429.0 / 28000000.0);

    // the same timing again: nothing happens
    board.Write(0x11, 0x03);
    _context->pCore->GetZ80()->pc = 0xC000;
    board.AfterInstruction();
    EXPECT_EQ(_context->pCore->GetZ80()->pc, 0xC000) << "bit 0 only: still VGA 1, no reset";

    // the state image carries it
    const std::vector<uint8_t> vga1 = NextStateCodec::Save(*_ports);

    board.Write(0x11, 0x07);
    EXPECT_EQ(board.Read(0x11), 0) << "7 (HDMI) is VGA 0 on this board";
    board.AfterInstruction();
    EXPECT_EQ(_context->config.frame_duration_us, frameUs);
    EXPECT_EQ(_ports->Uart().SystemClock(), 28000000u);
    if (_context->pSoundManager)
        EXPECT_DOUBLE_EQ(SoundScale(), 1.0);

    ASSERT_TRUE(NextStateCodec::Load(*_ports, vga1.data(), vga1.size()));
    EXPECT_EQ(board.SystemClockHz(), 28571429u);
    EXPECT_EQ(_context->config.frame_duration_us, FrameUs(28571429));
    EXPECT_EQ(_ports->Uart().SystemClock(), 28571429u);

    // a hard reset loads the configuration: VGA 0
    board.Write(0x02, 0x02);
    board.AfterInstruction();
    EXPECT_EQ(board.Read(0x11), 0);
    EXPECT_EQ(_context->config.frame_duration_us, frameUs);
    EXPECT_EQ(_ports->Uart().SystemClock(), 28000000u);
}
