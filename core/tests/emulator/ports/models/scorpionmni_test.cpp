#include "stdafx.h"
#include "pch.h"

#include "emulator/ports/models/scorpionfixture.h"

#include "emulator/emulator.h"
#include "emulator/emulatormanager.h"

/// @brief Scorpion MNI - the "magic button" (implementation-plan Task 6, reworked
///        to the DD50 ground truth, hardware-reference §9): the button arms two
///        flip-flops at once. DD50.2 pulses /NMI and DD50.1 ("1-DOS/0-SOS")
///        forces page 3 (TR-DOS) of the CURRENT ProfROM plane over #0000-#3FFF -
///        the same mechanism Beta128 uses for its magic button. Neither the
///        #1FFD latch nor the plane register is touched: the service bit still
///        outranks the trigger (the firmware entry trick runs OUT (#1FFD),#12
///        at TR-DOS #0033 and expects the service page to replace TR-DOS
///        mid-chain), and the plane survives the whole session. The trigger
///        releases on the first instruction FETCH (M1 cycle) from >= #4000 -
///        DD50.1 resets on /M1 & /MREQ & (A15 | A14) - while data reads and
///        writes never release it (the TR-DOS chain reads RAM at #C001 at
///        #0814 before latching the Service page). Core-fixture tests
///        drive the sequence on the synthetic ROM bundle; Emulator-level tests
///        exercise Emulator::RequestMNI() on a real scorpion.rom machine
///        (hardware-reference 12.5).
class ScorpionMni_Test : public ScorpionMachineFixture
{
protected:
    /// The Emulator::RequestMNI() sequence on the Core-level fixture
    /// (the fixture has no Emulator wrapper - Core + decoder only)
    void PressMagicButton()
    {
        _context->emulatorState.scorpionDosTrigger = 1;
        _memory->UpdateZ80Banks();
        _core->GetZ80()->RequestNonMaskedInterrupt();
    }

    bool AcceptNmi()
    {
        Z80* z80 = _core->GetZ80();
        z80->t = 100;  // away from the INT window edges; NMI ignores the window
        z80->eipos = 0;
        return z80->ProcessInterrupts(false, 0, 0);
    }
};

/// @brief The magic button forces TR-DOS (bundle page 3) at #0000 and lands the
///        CPU on the #0066 vector - with every port latch untouched (the button
///        is pure trigger hardware, it never writes #1FFD/#7FFD)
TEST_F(ScorpionMni_Test, MagicButtonForcesTrdDosAndVectors)
{
    WritePort(0x7FFD, 0x10);  // ROM1 selected - must survive the button press
    WritePort(0x1FFD, 0x10);  // RAM bank 8 at #C000 - must survive too

    PressMagicButton();
    ASSERT_TRUE(AcceptNmi());

    Z80* z80 = _core->GetZ80();
    EXPECT_EQ(z80->pc, 0x0066u) << "NMI vector";
    EXPECT_EQ(_context->emulatorState.p1FFD, 0x10) << "#1FFD untouched - bit 1 NOT forced by the button";
    EXPECT_EQ(_context->emulatorState.p7FFD, 0x10) << "#7FFD untouched";
    EXPECT_EQ(_context->emulatorState.scorpionDosTrigger, 1) << "trigger stays armed - the vector fetch is below #4000";
    EXPECT_EQ(BankTag(0x0000), 0xC3) << "TR-DOS (page 3) forced at #0000";
    EXPECT_EQ(BankTag(0xC000), 0x48) << "RAM bank 8 still at #C000";
    EXPECT_TRUE(_context->emulatorState.flags & CF_DOSPORTS) << "FDC ports are on the bus while armed";
    EXPECT_TRUE(z80->nmi_in_progress);
}

/// @brief The vector executes TR-DOS code: the synthetic page 3 is filled with
///        its 0xC3 tag, so the instruction at #0066 is a JP #C3C3 into bank3
///        RAM - proof the fetch came from the forced TR-DOS page, and that
///        ROM-area fetches never release the trigger
TEST_F(ScorpionMni_Test, VectorExecutesTrdDosCode)
{
    WritePort(0x1FFD, 0x10);  // bank 8 at #C000
    PressMagicButton();
    ASSERT_TRUE(AcceptNmi());

    // Park target for the tag redirect (#C3C3 lives in bank3 = RAM page 8 here)
    DirectWrite(0xC3C3, 0x18);
    DirectWrite(0xC3C4, 0xFE);

    _core->GetZ80()->Z80Step();  // execute the instruction at #0066 (TR-DOS tag fill)

    EXPECT_EQ(_core->GetZ80()->pc, 0xC3C3u) << "TR-DOS page code redirected execution";
    EXPECT_TRUE(_core->GetZ80()->nmi_in_progress) << "still inside the NMI handler";
    EXPECT_EQ(_context->emulatorState.scorpionDosTrigger, 1) << "fetches below #4000 keep the trigger armed";
}

/// @brief The trigger releases on the first instruction fetch from >= #4000 -
///        and only on a fetch: a data read at the same address (the exact
///        operand read LD HL,(#C001) the TR-DOS chain executes at #0814, one
///        step before it latches the Service page) and a write both leave it
///        armed. After the release #0000 falls back to the latched ROM selection
TEST_F(ScorpionMni_Test, TriggerReleasesOnUpperHalfRead)
{
    WritePort(0x7FFD, 0x10);  // ROM1 latched - the post-release selection
    WritePort(0x1FFD, 0x10);  // RAM bank 8 at #C000
    PressMagicButton();
    ASSERT_EQ(BankTag(0x0000), 0xC3);

    _memory->MemoryWriteFast(0x4000, 0x5A);  // NMI pushes land in the upper half too
    EXPECT_EQ(_context->emulatorState.scorpionDosTrigger, 1) << "writes never release the trigger";
    EXPECT_EQ(BankTag(0x0000), 0xC3) << "page 3 still forced after the write";

    _memory->MemoryReadFast(0xC001, false);  // the #0814 operand read of LD HL,(#C001)
    EXPECT_EQ(_context->emulatorState.scorpionDosTrigger, 1) << "data reads never release DD50.1 - the NMI chain survives #0814";
    EXPECT_EQ(BankTag(0x0000), 0xC3) << "page 3 still forced after the data read";

    uint8_t served = _memory->MemoryReadFast(0x4000, true);

    EXPECT_EQ(_context->emulatorState.scorpionDosTrigger, 0) << "the >= #4000 instruction fetch released DD50.1";
    EXPECT_EQ(BankTag(0x0000), 0xC1) << "#0000 back to the latched ROM1 selection";
    EXPECT_EQ(served, 0x5A) << "the release fetch read through the plain RAM mapping (the byte written earlier)";
}

/// @brief The service latch (#1FFD bit 1) outranks the armed trigger: pressing
///        the button inside the Shadow Monitor keeps the monitor at #0000, and
///        the firmware entry chain relies on the same ordering after its
///        OUT (#1FFD),#12 at TR-DOS #0033 (the "page the monitor mid-instruction"
///        trick - both pages carry compatible code around #0033)
TEST_F(ScorpionMni_Test, ServiceLatchOutranksTheTrigger)
{
    WritePort(0x1FFD, 0x12);  // service monitor latched (bit 1) + bank 8 at #C000
    PressMagicButton();

    EXPECT_EQ(BankTag(0x0000), 0xC2) << "service page wins over the armed trigger";
    EXPECT_EQ(_context->emulatorState.p1FFD, 0x12) << "latch untouched by the button";
    EXPECT_EQ(_context->emulatorState.scorpionDosTrigger, 1) << "trigger armed regardless";

    ASSERT_TRUE(AcceptNmi());
    EXPECT_EQ(_core->GetZ80()->pc, 0x0066u) << "NMI vectors into the service page";
}

/// @brief While armed, page 3 also overrides RAM-at-#0000 (#1FFD bit 0) - the
///        button must swap the ROM in regardless of what the latch selected,
///        and the RAM mapping returns only after the release read
TEST_F(ScorpionMni_Test, RamAtZeroIsOverriddenWhileArmed)
{
    WritePort(0x1FFD, 0x11);  // RAM bank 0 at #0000 (bit 0) + bank 8 at #C000
    PressMagicButton();

    EXPECT_EQ(BankTag(0x0000), 0xC3) << "the armed trigger overrides RAM-at-#0000";
    EXPECT_EQ(BankTag(0x4000), 0x45) << "bank1 untouched";

    _memory->MemoryReadFast(0x4000, true);  // release strobe (M1 fetch)

    EXPECT_EQ(_context->emulatorState.scorpionDosTrigger, 0);
    EXPECT_EQ(BankTag(0x0000), 0x40) << "RAM bank 0 returns to #0000 after the release";
}

/// @brief Emulator::RequestMNI() on a real scorpion.rom machine: the trigger,
///        the forced TR-DOS mapping and the pending NMI all land together, the
///        release read restores the latched selection
TEST(ScorpionMniEmulator_Test, RequestMniArmsTriggerAndForcesTrdDos)
{
    EmulatorManager* manager = EmulatorManager::GetInstance();
    std::shared_ptr<Emulator> emulator = manager->CreateEmulatorWithModel("", "SCORPION", LoggerLevel::LogError);
    if (!emulator)
        GTEST_SKIP() << "SCORPION model emulator could not be created (ROM/config missing)";

    EmulatorContext* context = emulator->GetContext();
    Memory* memory = context->pMemory;
    Z80* z80 = context->pCore->GetZ80();
    uint8_t trdosFirstByte = memory->base_dos_rom[0];
    uint8_t basic128FirstByte = memory->base_128_rom[0];

    EXPECT_EQ(context->emulatorState.scorpionDosTrigger, 0);

    emulator->RequestMNI();

    EXPECT_EQ(context->emulatorState.scorpionDosTrigger, 1) << "DD50.1 armed";
    EXPECT_FALSE(context->emulatorState.p1FFD & 0x02) << "the #1FFD latch is NOT written by the button";
    EXPECT_EQ(memory->DirectReadFromZ80Memory(0x0000), trdosFirstByte)
        << "the real TR-DOS ROM is mapped at #0000";

    z80->t = 100;
    z80->eipos = 0;
    EXPECT_TRUE(z80->ProcessInterrupts(false, 0, 0));
    EXPECT_EQ(z80->pc, 0x0066u) << "the CPU lands on the NMI vector in TR-DOS";

    memory->MemoryReadFast(0x4000, true);  // the release strobe (M1 fetch)

    EXPECT_EQ(context->emulatorState.scorpionDosTrigger, 0) << "trigger released by the upper-half fetch";
    EXPECT_EQ(memory->DirectReadFromZ80Memory(0x0000), basic128FirstByte)
        << "BASIC 128 returns to #0000 after the release";

    EmulatorManager::GetInstance()->RemoveEmulator(emulator->GetId());
}

/// @brief Emulator::RequestNMI() is the plain pulse: no trigger, no latch side effects
TEST(ScorpionMniEmulator_Test, RequestNmiLeavesLatchesAlone)
{
    EmulatorManager* manager = EmulatorManager::GetInstance();
    std::shared_ptr<Emulator> emulator = manager->CreateEmulatorWithModel("", "SCORPION", LoggerLevel::LogError);
    if (!emulator)
        GTEST_SKIP() << "SCORPION model emulator could not be created (ROM/config missing)";

    EmulatorContext* context = emulator->GetContext();
    Z80* z80 = context->pCore->GetZ80();

    emulator->RequestNMI();

    EXPECT_FALSE(context->emulatorState.p1FFD & 0x02) << "plain NMI does not page the monitor";
    EXPECT_EQ(context->emulatorState.scorpionDosTrigger, 0) << "plain NMI does not arm the DOS trigger";

    z80->t = 100;
    z80->eipos = 0;
    EXPECT_TRUE(z80->ProcessInterrupts(false, 0, 0));
    EXPECT_EQ(z80->pc, 0x0066u);

    EmulatorManager::GetInstance()->RemoveEmulator(emulator->GetId());
}
