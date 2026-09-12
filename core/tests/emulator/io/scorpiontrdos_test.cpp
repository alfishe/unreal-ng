#include "stdafx.h"
#include "pch.h"

#include <filesystem>
#include <functional>

#include "emulator/ports/models/scorpionfixture.h"

#include "emulator/emulator.h"
#include "emulator/emulatormanager.h"
#include "emulator/io/fdc/diskimage.h"

/// @brief Scorpion ZS 256 TR-DOS session integration (implementation-plan Task 5):
///        scripted Z80 programs drive the real trap machinery — CF_SETDOSROM arming
///        on the port write, the #3Dxx fetch boundary in Z80Step switching bank0 to
///        the DOS ROM, and the execute-from-RAM close — plus the FDC bus-ownership
///        contract around the session (hardware-reference §12.3).
///
/// Script shape (breakpoints_test.cpp ROMPagingBeforeBreakpointDispatch idiom):
/// the opener arms the trap from RAM at #8000 via a real OUT (C),A and jumps into
/// the #3Dxx hot zone. The synthetic DOS ROM is filled with its tag byte 0xC3, so
/// the trapped fetch executes JP #C3C3 and parks execution in bank3 RAM — a
/// deterministic landing used to observe both the open and the close boundaries.
class ScorpionTrdos_Test : public ScorpionMachineFixture
{
protected:
    /// @brief Hand-assemble a snippet at a Z80 window address
    void WriteScript(uint16_t address, std::initializer_list<uint8_t> bytes)
    {
        for (uint8_t byte : bytes)
            DirectWrite(address++, byte);
    }

    /// @brief Single-step the CPU until the predicate holds (checked before the
    ///        first step and after the last). @return false when the step cap is
    ///        exhausted without the predicate turning true
    bool RunUntil(const std::function<bool()>& predicate, uint64_t maxSteps = 2000)
    {
        Z80* z80 = _core->GetZ80();
        for (uint64_t step = 0; step < maxSteps; step++)
        {
            if (predicate())
                return true;
            z80->Z80Step();
        }
        return predicate();
    }

    /// @brief The opener: OUT (#7FFD),10h (ROM1 slot) then JP #3D2F.
    ///        Leaves PC parked at #C3C3 with the session open when the caller's
    ///        RunUntil predicate observes CF_TRDOS
    void WriteRom1Opener()
    {
        WriteScript(0x8000, { 0x01, 0xFD, 0x7F });   // LD BC,#7FFD
        WriteScript(0x8003, { 0x3E, 0x10 });         // LD A,#10
        WriteScript(0x8005, { 0xED, 0x79 });         // OUT (C),A
        WriteScript(0x8007, { 0xC3, 0x2F, 0x3D });   // JP #3D2F
        _core->GetZ80()->pc = 0x8000;
    }

    EmulatorState& State()
    {
        return _context->emulatorState;
    }
};

/// @brief Script 1: arming from the ROM1 slot (48K ROM selected) opens the session
///        on the #3Dxx fetch — bank0 switches to the DOS ROM and the FDC session
///        flags rise
TEST_F(ScorpionTrdos_Test, ArmFromRom1OpensSession)
{
    WriteRom1Opener();

    EmulatorState& state = State();
    EXPECT_TRUE(RunUntil([&state] { return (state.flags & CF_TRDOS) != 0; }))
        << "the #3D2F fetch must open the TR-DOS session";

    EXPECT_EQ(BankTag(0x0000), 0xC3) << "bank0 switched to the DOS ROM (page 3)";
    EXPECT_TRUE(state.flags & CF_DOSPORTS) << "the FDC joins the bus";
    EXPECT_TRUE(state.flags & CF_LEAVEDOSRAM) << "the close condition is armed";
    EXPECT_EQ(_core->GetZ80()->pc, 0xC3C3) << "the tag-byte redirect parks PC in bank3";
}

/// @brief Script 2: arming from the Shadow Monitor (OUT (#1FFD),02h) opens the
///        session with the monitor still paged (design §3: the monitor outranks
///        ROM3 in the bank0 chain); clearing the latch mid-session hands #0000 to
///        the DOS ROM, and the close then restores ROM0 per p7FFD
TEST_F(ScorpionTrdos_Test, ArmFromShadowMonitorOpensSession)
{
    WriteScript(0x8000, { 0x01, 0xFD, 0x1F });       // LD BC,#1FFD
    WriteScript(0x8003, { 0x3E, 0x02 });            // LD A,#02
    WriteScript(0x8005, { 0xED, 0x79 });            // OUT (C),A
    WriteScript(0x8007, { 0xC3, 0x2F, 0x3D });      // JP #3D2F
    _core->GetZ80()->pc = 0x8000;

    EmulatorState& state = State();
    EXPECT_TRUE(RunUntil([&state] { return (state.flags & CF_TRDOS) != 0; }));
    ReadPort(0x001F);
    EXPECT_TRUE(_context->pPortDecoder->WasLastPortDecoded())
        << "the session is open: the FDC answers";
    EXPECT_EQ(BankTag(0x0000), 0xC2) << "the monitor stays at #0000 (outranks ROM3)";

    // Clear the monitor latch while the session is open: #0000 goes to the DOS ROM
    WritePort(0x1FFD, 0x00);
    EXPECT_EQ(BankTag(0x0000), 0xC3) << "unlatched: the session ROM3 takes over";

    // One more step: PC sits at #C3C3 (bank3 RAM) -> session closes, ROM0 per p7FFD
    _core->GetZ80()->Z80Step();
    EXPECT_FALSE(state.flags & CF_TRDOS);
    EXPECT_EQ(BankTag(0x0000), 0xC0) << "closed: ROM0 returns per p7FFD=#00";
}

/// @brief Script 3: plain ROM0 (48K slot not selected) — a #3D9D fetch never
///        arms anything and bank0 stays on BASIC 128
TEST_F(ScorpionTrdos_Test, NoArmFromRom0KeepsSessionClosed)
{
    WriteScript(0x8000, { 0xC3, 0x9D, 0x3D });       // JP #3D9D
    _core->GetZ80()->pc = 0x8000;

    EmulatorState& state = State();
    EXPECT_FALSE(RunUntil([&state] { return (state.flags & CF_TRDOS) != 0; }, 500))
        << "no arm: the #3Dxx fetch must be inert with the 128K ROM selected";

    EXPECT_EQ(BankTag(0x0000), 0xC0) << "bank0 never left ROM0";
    EXPECT_FALSE(state.flags & CF_SETDOSROM) << "the trap is not armed from ROM0";
}

/// @brief Script 4: RAM latched at #0000 (OUT (#1FFD),01h) — the #3Dxx fetch
///        executes real code but must not open a session (design: RAM at bank0
///        disarms the trap)
TEST_F(ScorpionTrdos_Test, NoArmWithRam0KeepsSessionClosed)
{
    // Latch first so the park bytes land in RAM page 0 (not the ROM trash page)
    WritePort(0x1FFD, 0x01);
    WriteScript(0x3D2F, { 0x18, 0xFE });            // JR -2 (park at #3D2F)

    WriteScript(0x8000, { 0xC3, 0x2F, 0x3D });      // JP #3D2F
    _core->GetZ80()->pc = 0x8000;

    EmulatorState& state = State();
    EXPECT_FALSE(RunUntil([&state] { return (state.flags & CF_TRDOS) != 0; }, 500))
        << "RAM at #0000 must disarm the #3Dxx trap";

    EXPECT_EQ(_core->GetZ80()->pc, 0x3D2F) << "execution parked inside the hot zone";
    EXPECT_EQ(BankTag(0x0000), 0x40) << "bank0 still RAM page 0";
    EXPECT_FALSE(state.flags & CF_SETDOSROM);
}

/// @brief Script 5: execute-from-RAM closes the session and restores bank0 per the
///        latches (HW §4.4: the unpaged ROM follows p7FFD once the session ends)
TEST_F(ScorpionTrdos_Test, ExecuteFromRamClosesSession)
{
    WriteRom1Opener();
    // Bank3 stays RAM page 0 across the whole flow, so these land safely now:
    WriteScript(0xC3C3, { 0xC3, 0x18, 0x80 });      // JP #8018 (bank2 RAM)
    WriteScript(0x8018, { 0x18, 0xFE });            // JR -2 (park)

    EmulatorState& state = State();
    EXPECT_TRUE(RunUntil([&state] { return (state.flags & CF_TRDOS) != 0; }));
    EXPECT_EQ(BankTag(0x0000), 0xC3);

    EXPECT_TRUE(RunUntil([&state] { return (state.flags & CF_TRDOS) == 0; }))
        << "arrival at #C3C3 (RAM bank) must close the session";

    EXPECT_EQ(BankTag(0x0000), 0xC1) << "closed: ROM1 returns per p7FFD=#10";
    EXPECT_FALSE(state.flags & CF_DOSPORTS) << "the FDC leaves the bus";
    EXPECT_EQ(_core->GetZ80()->pc, 0x8018) << "execution parked in bank2 RAM";
}

/// @brief Script 6: while the session is open, OUT (#7FFD),00h changes the latch
///        but bank0 stays on ROM3 (HW §4.4 rule 3); the close then applies the new
///        latch and lands on ROM0
TEST_F(ScorpionTrdos_Test, Rom3StaysPagedDuringSessionDespiteSevenFFD)
{
    WriteRom1Opener();

    EmulatorState& state = State();
    EXPECT_TRUE(RunUntil([&state] { return (state.flags & CF_TRDOS) != 0; }));
    EXPECT_EQ(BankTag(0x0000), 0xC3);

    WritePort(0x7FFD, 0x00);  // ROM0 select while the session is open
    EXPECT_EQ(state.p7FFD, 0x00) << "the latch itself is updated";
    EXPECT_EQ(BankTag(0x0000), 0xC3) << "but the DOS ROM stays at #0000 under CF_TRDOS";

    _core->GetZ80()->Z80Step();  // close at #C3C3
    EXPECT_FALSE(state.flags & CF_TRDOS);
    EXPECT_EQ(BankTag(0x0000), 0xC0) << "the close applies the new p7FFD: ROM0";
}

/// @brief Script 6b: OUT (#1FFD),04h outside a session — bit 2 (RS-232) is not
///        implemented: latched verbatim, no paging change, no trap arm
TEST_F(ScorpionTrdos_Test, OneFFDBusBitDoesNotPageOrArm)
{
    WritePort(0x1FFD, 0x04);

    EmulatorState& state = State();
    EXPECT_EQ(state.p1FFD, 0x04) << "the byte is latched verbatim";
    EXPECT_EQ(BankTag(0x0000), 0xC0) << "bit 2 does not page anything";
    EXPECT_FALSE(state.flags & CF_SETDOSROM) << "bit 2 does not arm the trap";
    EXPECT_FALSE(state.flags & CF_TRDOS);
}

/// @brief Script 7: FDC bus ownership follows the session end to end — IN (#1F)
///        answers only while the session is open, and OUT (#FF) colors the border
///        only once the FDC is off the bus (Task 4 interplay)
TEST_F(ScorpionTrdos_Test, FdcBusOwnershipFollowsSession)
{
    WriteRom1Opener();

    EmulatorState& state = State();
    EXPECT_TRUE(RunUntil([&state] { return (state.flags & CF_TRDOS) != 0; }));

    ReadPort(0x001F);
    EXPECT_TRUE(_context->pPortDecoder->WasLastPortDecoded())
        << "inside the session the FDC answers #1F";

    WritePort(0x00FF, 0x02);
    EXPECT_EQ(_context->pScreen->GetBorderColor(), COLOR_BLACK)
        << "inside the session #FF belongs to the FDC system port, not the border";

    _core->GetZ80()->Z80Step();  // close at #C3C3
    EXPECT_FALSE(state.flags & CF_TRDOS);

    ReadPort(0x001F);
    EXPECT_FALSE(_context->pPortDecoder->WasLastPortDecoded())
        << "outside the session the FDC is off the bus";

    WritePort(0x00FF, 0x02);
    EXPECT_EQ(_context->pScreen->GetBorderColor(), COLOR_RED)
        << "outside the session OUT (#FF) colors the border";
}

/// @brief Script 8: the §12.3 arbitration contract around the monitor — with the
///        Shadow Monitor paged the FDC answers even with the session closed, #FF
///        belongs to the FDC in that state too, and unpaging hands both back
TEST_F(ScorpionTrdos_Test, MonitorPagingKeepsFdcOnBus)
{
    EmulatorState& state = State();

    // Session closed, monitor unpaged: FDC off the bus, #FF is the border
    ReadPort(0x001F);
    EXPECT_FALSE(_context->pPortDecoder->WasLastPortDecoded());

    // Monitor paged, session still closed: the FDC keeps answering (HW 12.3)
    WritePort(0x1FFD, 0x02);
    ReadPort(0x001F);
    EXPECT_TRUE(_context->pPortDecoder->WasLastPortDecoded())
        << "the monitor keeps the FDC on the bus with the session closed";
    WritePort(0x00FF, 0x02);
    EXPECT_EQ(_context->pScreen->GetBorderColor(), COLOR_BLACK)
        << "and #FF belongs to the FDC system port, not the border";

    // Open the session from the monitor, then close it: arbitration persists
    WriteScript(0x8000, { 0xC3, 0x2F, 0x3D });      // JP #3D2F
    _core->GetZ80()->pc = 0x8000;
    EXPECT_TRUE(RunUntil([&state] { return (state.flags & CF_TRDOS) != 0; }));
    _core->GetZ80()->Z80Step();  // close at #C3C3
    EXPECT_FALSE(state.flags & CF_TRDOS);

    ReadPort(0x001F);
    EXPECT_TRUE(_context->pPortDecoder->WasLastPortDecoded())
        << "after the close the monitor still keeps the FDC on the bus";

    // Unpage the monitor: both ports return to their plain roles
    WritePort(0x1FFD, 0x00);
    ReadPort(0x001F);
    EXPECT_FALSE(_context->pPortDecoder->WasLastPortDecoded());
    WritePort(0x00FF, 0x02);
    EXPECT_EQ(_context->pScreen->GetBorderColor(), COLOR_RED)
        << "with the monitor unpaged OUT (#FF) colors the border again";
}

/// @brief Integration smoke: a real TRD mounts into a full SCORPION-model
///        emulator built by the manager (real scorpion.rom, real loader path)
TEST(ScorpionTrdosMount_Test, TrdMountSmoke)
{
    EmulatorManager* manager = EmulatorManager::GetInstance();
    std::shared_ptr<Emulator> emulator = manager->CreateEmulatorWithModel("", "SCORPION", LoggerLevel::LogError);
    if (!emulator)
        GTEST_SKIP() << "SCORPION model emulator could not be created (ROM/config missing)";

    std::string source = TestPathHelper::GetTestDataPath("loaders/trd/EyeAche.trd");
    std::string target = TestPathHelper::GetTestScratchPath("scorpiontrdos-mount.trd");
    std::error_code ec;
    std::filesystem::remove(target, ec);
    std::filesystem::copy_file(source, target, ec);
    ASSERT_FALSE(ec) << "fixture copy failed: " << source;

    ASSERT_TRUE(emulator->LoadDisk(target)) << "the TRD must mount into drive A";

    DiskImage* image = emulator->GetContext()->coreState.diskImages[0];
    ASSERT_NE(image, nullptr);
    EXPECT_EQ(image->getCylinders(), 80);
    EXPECT_EQ(image->getSides(), 2);

    EmulatorManager::GetInstance()->RemoveEmulator(emulator->GetId());
    std::filesystem::remove(target, ec);
}
