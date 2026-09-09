#include "stdafx.h"
#include "pch.h"

#include "emulator/ports/models/scorpionfixture.h"

#include "emulator/emulator.h"
#include "emulator/emulatormanager.h"

/// @brief Scorpion MNI - the "magic button" (implementation-plan Task 6):
///        NMI with the Shadow Monitor latched into #0000, so the handler at
///        #0066 executes monitor code. Core-fixture tests drive the
///        Emulator::RequestMNI() orchestration sequence directly on the
///        synthetic ROM bundle; Emulator-level tests exercise the method
///        itself on a real scorpion.rom machine (hardware-reference 12.5).
class ScorpionMni_Test : public ScorpionMachineFixture
{
protected:
    /// The Emulator::RequestMNI() sequence on the Core-level fixture
    /// (the fixture has no Emulator wrapper - Core + decoder only)
    void PressMagicButton()
    {
        _context->emulatorState.p1FFD |= 0x02;
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

/// @brief The magic button latches the monitor and lands the CPU on the #0066
///        vector inside the service ROM - without touching the RAM bank map
TEST_F(ScorpionMni_Test, MagicButtonPagesMonitorAndVectors)
{
    WritePort(0x7FFD, 0x10);  // ROM1 selected - must survive the button press
    WritePort(0x1FFD, 0x10);  // RAM bank 8 at #C000 - must survive too

    PressMagicButton();
    ASSERT_TRUE(AcceptNmi());

    Z80* z80 = _core->GetZ80();
    EXPECT_EQ(z80->pc, 0x0066u) << "NMI vector";
    EXPECT_EQ(_context->emulatorState.p1FFD & 0x02, 0x02) << "monitor latched";
    EXPECT_EQ(_context->emulatorState.p1FFD & 0x10, 0x10) << "#1FFD bank bits preserved";
    EXPECT_EQ(_context->emulatorState.p7FFD & 0x10, 0x10) << "#7FFD untouched";
    EXPECT_EQ(BankTag(0x0000), 0xC2) << "service ROM at #0000";
    EXPECT_EQ(BankTag(0xC000), 0x48) << "RAM bank 8 still at #C000";
    EXPECT_TRUE(z80->nmi_in_progress);
}

/// @brief The vector executes Shadow Monitor code: the synthetic service page
///        is filled with its 0xC2 tag, so the instruction at #0066 is a
///        JP #C2C2 into bank3 RAM - proof the fetch came from the monitor
TEST_F(ScorpionMni_Test, MonitorCodeExecutesAtVector)
{
    WritePort(0x1FFD, 0x10);  // bank 8 at #C000
    PressMagicButton();
    ASSERT_TRUE(AcceptNmi());

    // Park target for the tag redirect (#C2C2 lives in bank3 = RAM page 8 here)
    DirectWrite(0xC2C2, 0x18);
    DirectWrite(0xC2C3, 0xFE);

    _core->GetZ80()->Z80Step();  // execute the instruction at #0066 (monitor)

    EXPECT_EQ(_core->GetZ80()->pc, 0xC2C2u) << "monitor code redirected execution";
    EXPECT_TRUE(_core->GetZ80()->nmi_in_progress) << "still inside the NMI handler";
}

/// @brief The monitor exit (OUT (#1FFD) clearing bit 1) returns #0000 to ROM0
///        with the RAM bank map intact
TEST_F(ScorpionMni_Test, MonitorExitRestoresRom0WithBankIntact)
{
    WritePort(0x1FFD, 0x10);  // bank 8 at #C000
    PressMagicButton();
    ASSERT_TRUE(AcceptNmi());
    ASSERT_EQ(BankTag(0x0000), 0xC2);

    WritePort(0x1FFD, 0x10);  // monitor exit: clear bit 1, keep the bank bits

    EXPECT_EQ(BankTag(0x0000), 0xC0) << "ROM0 (BASIC 128) returns to #0000";
    EXPECT_EQ(BankTag(0xC000), 0x48) << "RAM bank 8 intact at #C000";
    EXPECT_EQ(_context->emulatorState.p1FFD, 0x10);
}

/// @brief Emulator::RequestMNI() on a real scorpion.rom machine: the latch,
///        the bank map and the pending NMI all land together
TEST(ScorpionMniEmulator_Test, RequestMniLatchesMonitorAndVectors)
{
    EmulatorManager* manager = EmulatorManager::GetInstance();
    std::shared_ptr<Emulator> emulator = manager->CreateEmulatorWithModel("", "SCORPION", LoggerLevel::LogError);
    if (!emulator)
        GTEST_SKIP() << "SCORPION model emulator could not be created (ROM/config missing)";

    EmulatorContext* context = emulator->GetContext();
    Memory* memory = context->pMemory;
    Z80* z80 = context->pCore->GetZ80();
    uint8_t monitorFirstByte = memory->base_sys_rom[0];

    EXPECT_FALSE(context->emulatorState.p1FFD & 0x02);

    emulator->RequestMNI();

    EXPECT_TRUE(context->emulatorState.p1FFD & 0x02) << "the button latched the monitor";
    EXPECT_EQ(memory->DirectReadFromZ80Memory(0x0000), monitorFirstByte)
        << "the real service ROM is mapped at #0000";

    z80->t = 100;
    z80->eipos = 0;
    EXPECT_TRUE(z80->ProcessInterrupts(false, 0, 0));
    EXPECT_EQ(z80->pc, 0x0066u) << "the CPU lands on the NMI vector in the monitor";

    EmulatorManager::GetInstance()->RemoveEmulator(emulator->GetId());
}

/// @brief Emulator::RequestNMI() is the plain pulse: no latch side effects
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

    z80->t = 100;
    z80->eipos = 0;
    EXPECT_TRUE(z80->ProcessInterrupts(false, 0, 0));
    EXPECT_EQ(z80->pc, 0x0066u);

    EmulatorManager::GetInstance()->RemoveEmulator(emulator->GetId());
}
