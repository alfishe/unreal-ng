#include "stdafx.h"
#include "pch.h"

#include "emulator/ports/models/scorpiongmxfixture.h"
#include "emulator/state/devicestate.h"

/// @brief Scorpion GMX register file, RAM and ROM mapping (GMX design 2.2-2.4, tdd-plan sections 1-3)
class ScorpionGmx_Test : public ScorpionGmxFixture
{
};

TEST_F(ScorpionGmx_Test, ModelIsSupportedAndCreatesTheGmxDecoder)
{
    EXPECT_TRUE(PortDecoder::IsModelSupported(MM_GMX));
    EXPECT_STREQ(Config::GetConfigFolderForModel(MM_GMX).c_str(), "gmx");
}

TEST_F(ScorpionGmx_Test, PowerOnMapsTheLoaderAndTheFixedPages)
{
    EXPECT_EQ(RomPageAt(0x0000), 0) << "plane 0 page 0: the GMX loader";
    EXPECT_EQ(RamPageAt(0x4000), 5);
    EXPECT_EQ(RamPageAt(0x8000), 2) << "#78FD = 0 maps page 0 ^ 2";
    EXPECT_EQ(RamPageAt(0xC000), 0);
}

TEST_F(ScorpionGmx_Test, Port78FDMapsThePageXor2)
{
    for (uint8_t value : {0x00, 0x02, 0x05, 0x40, 0x7F})
    {
        Out(0x78FD, value);
        EXPECT_EQ(RamPageAt(0x8000), (value & 0x7F) ^ 2) << "value " << int(value);
    }
    Out(0x78FD, 0xFF);
    EXPECT_EQ(State().gmx.p78FD, 0x7F) << "bit 7 is not stored";
}

TEST_F(ScorpionGmx_Test, C000PageIsTheSevenBitFormula)
{
    Out(0x7FFD, 0x03);
    Out(0x1FFD, 0x10);
    Out(0xDFFD, 0x05);
    EXPECT_EQ(RamPageAt(0xC000), (5 << 4) | 0x08 | 0x03);
    Out(0xDFFD, 0x07);
    Out(0x7FFD, 0x07);
    EXPECT_EQ(RamPageAt(0xC000), 127);
    Out(0x1FFD, 0x00);
    Out(0xDFFD, 0x00);
    EXPECT_EQ(RamPageAt(0xC000), 7);
}

TEST_F(ScorpionGmx_Test, GmxPortsBeatScorpionMasks)
{
    Out(0x7FFD, 0x01);
    // The host #7FFD mask matches all four #7xFD ports, the AY #FFFD mask matches #DFFD
    Out(0x78FD, 0x05);
    Out(0x7AFD, 0x30);
    Out(0x7CFD, 0x01);
    Out(0x7EFD, 0x00);
    EXPECT_EQ(State().p7FFD, 0x01) << "no #7xFD write may reach #7FFD";
    State().pFFFD = 0x0E;
    Out(0xDFFD, 0x03);
    EXPECT_EQ(State().pFFFD, 0x0E) << "#DFFD must not select an AY register";
    EXPECT_EQ(State().pDFFD, 0x03);
}

TEST_F(ScorpionGmx_Test, Port00HasTheFullLowByteDecode)
{
    Out(0x1200, 0x02);
    EXPECT_EQ(State().gmx.p00, 0x02) << "any high byte";
    Out(0xC000, 0x04);
    EXPECT_EQ(State().gmx.p00, 0x04) << "wins over the AY register-select mirror";
    State().gmx.p00 = 0;
    Out(0x0080, 0x07);
    EXPECT_EQ(State().gmx.p00, 0x00) << "A7 set: not #00";
}

TEST_F(ScorpionGmx_Test, PlaneSelectsTheRomPagesOfTheScorpionChain)
{
    Out(0x7EFD, 0x40);  // plane 4
    EXPECT_EQ(_gmx->Plane(), 4);
    EXPECT_EQ(RomPageAt(0x0000), 16) << "plane 4 page 0: the 128 editor";
    Out(0x7FFD, 0x10);
    EXPECT_EQ(RomPageAt(0x0000), 17) << "#7FFD bit 4: 48 BASIC";
    Out(0x1FFD, 0x02);
    EXPECT_EQ(RomPageAt(0x0000), 18) << "#1FFD bit 1: the service page";
    Out(0x1FFD, 0x04);
    EXPECT_EQ(RomPageAt(0x0000), 19) << "#1FFD bit 2: the plane's TR-DOS page";
    EXPECT_TRUE(State().flags & CF_DOSPORTS) << "with the Beta ports on";
    Out(0x1FFD, 0x05);
    EXPECT_EQ(RomPageAt(0x0000), 19) << "bit 2 wins over RAM 0";
    Out(0x1FFD, 0x01);
    EXPECT_EQ(RamPageAt(0x0000), 0);
    Out(0x1FFD, 0x00);
    Out(0x7EFD, 0x70);
    EXPECT_EQ(RomPageAt(0x0000), 29) << "plane 7, 48 BASIC";
}

TEST_F(ScorpionGmx_Test, NoProfRomReadStrobe)
{
    Out(0x7EFD, 0x40);
    Out(0x1FFD, 0x02);
    for (uint16_t addr : {0x0100, 0x0104, 0x0108, 0x010C})
        (void)_memory->MemoryReadFast(addr, false);
    EXPECT_EQ(_gmx->Plane(), 4) << "reads of #0100-#010C do not move the plane (Q2)";
    EXPECT_EQ(RomPageAt(0x0000), 18);
}

TEST_F(ScorpionGmx_Test, ReadBacks)
{
    Out(0x1FFD, 0x11);
    Out(0xDFFD, 0x06);
    Out(0x7FFD, 0x2D);  // page 5, screen 1, ROM 0, lock (last: it would block #1FFD)
    Out(0x00FE, 0x05);  // BRD0 and BRD2
    EXPECT_EQ(In(0x7AFD), 0x80 | (6 << 4) | 0x08 | 0x05);
    Out(0x7EFD, 0xC8);  // turbo, plane 4, 640x200
    Out(0x0000, 0xA0);  // Pentagon INT, BLKEXT
    // BLKEXT blocks the file: read with it off
    State().gmx.p00 = 0x80;
    EXPECT_EQ(In(0x7EFD), 0x80 | 0x40 | 0x10 | 0x08 | 0x04 | 0x02 | 0x01);
    Out(0x78FD, 0x12);
    EXPECT_EQ(In(0x78FD), 0x12) << "BRD1 = 0";
}

TEST_F(ScorpionGmx_Test, Port7AFDKeepsTheScrollStartWithTheLowNibbleForced)
{
    Out(0x7AFD, 0xFF);
    Out(0x7CFD, 0xFF);
    EXPECT_EQ(State().gmx.p7AFD, 0xF0);
    EXPECT_EQ(State().gmx.p7CFD, 0x3F);
}

TEST_F(ScorpionGmx_Test, ReconfArmsTheMagicByteReadLsbFirst)
{
    Out(0x0000, 0x0A);  // RECONF, scheme 2
    EXPECT_TRUE(_gmx->IsReconfigurationPending());
    const uint8_t expected = 0x8A;
    for (int bit = 0; bit < 8; bit++)
        EXPECT_EQ(In(0x78FD) & 0x01, (expected >> bit) & 0x01) << "bit " << bit;
    EXPECT_EQ(In(0x78FD) & 0x01, 0) << "empty after eight reads";
}

TEST_F(ScorpionGmx_Test, ReconfResetsTheCpuAtTheEndOfTheInstruction)
{
    Out(0x7EFD, 0xC0);
    Out(0x78FD, 0x09);
    Out(0x7FFD, 0x07);
    Z80* z80 = _core->GetZ80();
    z80->pc = 0x5C10;
    z80->iff1 = 1;
    Out(0x0000, 0x0A);
    EXPECT_EQ(z80->pc, 0x5C10) << "not inside the OUT";
    EXPECT_EQ(z80->GetMachineStepHook(), _gmx);
    _gmx->OnMachineStep(0);

    EXPECT_FALSE(_gmx->IsReconfigurationPending());
    EXPECT_EQ(z80->pc, 0x0000);
    EXPECT_EQ(z80->iff1, 0);
    EXPECT_EQ(State().gmx.p00, 0x0A) << "#00 kept";
    EXPECT_EQ(State().gmx.magicShift, 0x8A) << "the loader still has to read it";
    EXPECT_EQ(State().scorpion.p7EFD, 0x00);
    EXPECT_EQ(State().hw_turbo_ratio, 1);
    EXPECT_EQ(RomPageAt(0x0000), 0);
    EXPECT_EQ(RamPageAt(0x8000), 2);
    EXPECT_EQ(RamPageAt(0xC000), 0);
    EXPECT_NE(z80->GetMachineStepHook(), _gmx) << "hook released";
}

TEST_F(ScorpionGmx_Test, Write00WithoutReconfOnlyStores)
{
    Out(0x7EFD, 0x40);
    Out(0x0000, 0x02);
    EXPECT_FALSE(_gmx->IsReconfigurationPending());
    EXPECT_EQ(State().gmx.magicShift, 0);
    EXPECT_EQ(_gmx->Plane(), 4);
}

TEST_F(ScorpionGmx_Test, ResetKeepsPort00AndPowerCycleClearsIt)
{
    Out(0x0000, 0x82);
    Out(0x7EFD, 0x48);
    Out(0x78FD, 0x11);
    Out(0xDFFD, 0x03);
    _gmx->reset();
    EXPECT_EQ(State().gmx.p00, 0x82);
    EXPECT_EQ(State().scorpion.p7EFD, 0x00);
    EXPECT_EQ(State().gmx.p78FD, 0x00);
    EXPECT_EQ(State().pDFFD, 0x00);
    _gmx->PowerCycle();
    EXPECT_EQ(State().gmx.p00, 0x00);
}

TEST_F(ScorpionGmx_Test, FixromFreezesThePlaneAndKeepsItOverReset)
{
    Out(0x7EFD, 0x40);
    Out(0x0000, 0x10);
    Out(0x7EFD, 0x00);
    EXPECT_EQ(_gmx->Plane(), 4);
    _gmx->reset();
    EXPECT_EQ(_gmx->Plane(), 4);
    EXPECT_EQ(RomPageAt(0x0000), 16);
    Out(0x0000, 0x18);  // RECONF with FIXROM: the scheme is not reloaded
    EXPECT_FALSE(_gmx->IsReconfigurationPending());
}

TEST_F(ScorpionGmx_Test, LockPolicy)
{
    Out(0x7FFD, 0x21);
    Out(0x7FFD, 0x03);
    EXPECT_EQ(RamPageAt(0xC000), 1) << "locked";
    Out(0x7EFD, 0x04);
    Out(0x7FFD, 0x03);
    EXPECT_EQ(RamPageAt(0xC000), 3) << "#7EFD bit 2 lifts the lock (manual 3.1)";
}

TEST_F(ScorpionGmx_Test, BlkextHandsTheFileToTheHostBoard)
{
    Out(0x0000, 0x20);
    Out(0x78FD, 0x05);
    EXPECT_EQ(State().gmx.p78FD, 0x00);
    EXPECT_EQ(RamPageAt(0xC000), 5) << "a plain ZS-256: #78FD is a #7FFD mirror";
    Out(0x0000, 0x00);
    Out(0x78FD, 0x05);
    EXPECT_EQ(State().gmx.p78FD, 0x05);
}

TEST_F(ScorpionGmx_Test, TurboFrom7EFDAndNoTurboPlusStrobes)
{
    Out(0x7EFD, 0x80);
    EXPECT_EQ(State().scorpion.turbo, 1);
    EXPECT_EQ(State().hw_turbo_ratio, 2);
    (void)In(0x1FFD);
    EXPECT_EQ(State().hw_turbo_ratio, 2) << "IN from the #1FFD family does not clear turbo (Q5)";
    Out(0x7EFD, 0x00);
    (void)In(0x7FFD);
    EXPECT_EQ(State().hw_turbo_ratio, 1) << "IN from the #7FFD family does not set it";
}

TEST_F(ScorpionGmx_Test, MagicButtonGateAndReadBackLatch)
{
    Out(0x7EFD, 0x04);
    EXPECT_FALSE(_gmx->OnMagicButton()) << "#7EFD bit 2 blocks the button";
    Out(0x7EFD, 0x00);
    Out(0x7FFD, 0x03);
    EXPECT_TRUE(_gmx->OnMagicButton());
    Out(0x7FFD, 0x06);
    EXPECT_EQ(In(0x7AFD) & 0x07, 0x03) << "frozen at the press";
    (void)In(0x00FF);
    EXPECT_EQ(In(0x7AFD) & 0x07, 0x06) << "released by IN #FF";
}

TEST_F(ScorpionGmx_Test, SetRamPageSplitsTheFormula)
{
    _gmx->SetRAMPage(0x5B);
    EXPECT_EQ(RamPageAt(0xC000), 0x5B);
    EXPECT_EQ(State().pDFFD, 0x05);
}

TEST_F(ScorpionGmx_Test, PagingStateReportsTheCardWithoutTouchingTheMagicByte)
{
    Out(0x7EFD, 0xC8);   // turbo, plane 4, 640x200
    Out(0x78FD, 0x25);
    Out(0xDFFD, 0x03);
    Out(0x7AFD, 0xA0);
    Out(0x7CFD, 0x12);
    Out(0x0000, 0x0A);   // RECONF: arms the magic byte
    const uint8_t shift = State().gmx.magicShift;

    const StateNode node = DeviceState::GmxPaging(_context);
    ASSERT_TRUE(node.find("available")->b);
    EXPECT_EQ(node.find("port_7efd")->find("rom_plane")->i, 4);
    EXPECT_TRUE(node.find("port_7efd")->find("turbo")->b);
    EXPECT_TRUE(node.find("port_7efd")->find("video_640x200")->b);
    EXPECT_EQ(node.find("page_8000")->i, 0x27);
    EXPECT_EQ(node.find("page_c000")->i, RamPageAt(0xC000));
    EXPECT_EQ(node.find("scroll_start")->i, 0x12A0);
    EXPECT_EQ(node.find("port_00")->find("scheme")->i, 2);
    EXPECT_TRUE(node.find("port_00")->find("reconf_pending")->b);
    EXPECT_EQ(State().gmx.magicShift, shift) << "a report must not shift the magic byte as IN #78FD does";
}

TEST_F(ScorpionGmx_Test, PortMapCarriesTheCardRows)
{
    bool has00 = false, has78 = false, has7E = false, hasDF = false;
    for (const PortMapEntry& entry : _gmx->getPortMapEntries())
    {
        has00 |= entry.port == 0x0000 && entry.latch == PagingLatch::PGmx00;
        has78 |= entry.port == 0x78FD && entry.latch == PagingLatch::P78FD;
        has7E |= entry.port == 0x7EFD && entry.latch == PagingLatch::P7EFD;
        hasDF |= entry.port == 0xDFFD && entry.latch == PagingLatch::PDFFD;
    }
    EXPECT_TRUE(has00 && has78 && has7E && hasDF);
    EXPECT_STREQ(PagingLatchToString(PagingLatch::P78FD), "p78FD");
}

/// @brief The tracked gmx644.rom on the whole machine: the loader runs its power-on RECONF (the magic byte #8A), then
///        hands over to the Scorpion GMX 128 menu in plane 4 with turbo on (research-2026-10-30-web-unreal.md 4.1)
TEST(ScorpionGmxBoot_Test, LoaderReachesThePlane4Menu)
{
    Emulator* emulator = EmulatorTestHelper::CreateStandardEmulator("GMX", LoggerLevel::LogError, RamPowerOn::Zero);
    ASSERT_NE(emulator, nullptr) << "GMX must be creatable";
    emulator->EnableTurboMode();
    EmulatorContext* context = emulator->GetContext();
    ASSERT_EQ(context->config.mem_model, MM_GMX);

    const int frames = EmulatorTestHelper::RunUntil(
        emulator, [&]() { return (context->emulatorState.scorpion.p7EFD & 0xF0) == 0xC0; }, 1500, 5);
    EXPECT_LT(frames, 1500) << "the loader never reached turbo + plane 4 (p7EFD #"
                            << std::hex << int(context->emulatorState.scorpion.p7EFD) << ")";
    // The boot code the loader reads after its RECONF is #8A: scheme 2 ends up in #00 (power-on reads 0)
    EXPECT_EQ(context->emulatorState.gmx.p00 & 0x07, 2) << "the power-on RECONF (OUT #00,#0A) did not run";

    ROM* rom = context->pCore->GetROM();
    ASSERT_NE(rom, nullptr);
    EXPECT_EQ(context->pMemory->GetROMPage() / 4, 4) << rom->GetROMPageRole(context->pMemory->GetROMPage());

    EmulatorTestHelper::CleanupEmulator(emulator);
}
