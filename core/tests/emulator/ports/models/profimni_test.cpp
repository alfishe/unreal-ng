#include "stdafx.h"
#include "pch.h"

#include "emulator/ports/models/profifixture.h"
#include "emulator/ports/models/portdecoder_profi.h"

#include "emulator/emulator.h"
#include "emulator/emulatormanager.h"

/// @brief Profi "magic button" (NMI -> DOS latch), gap G5 from the reconciliation report
///        (docs/inprogress/2026-09-21-profi/2026-09-25-profi-reconciliation.md section 4.1).
///        Karabas `video.vhd`/`TOP:1197-1199`: `dos_act` is set on NMI (`cpu_nmi_n=0`) when
///        `DS80=0`. Unlike Scorpion's DD50 flip-flop hardware, Profi has no separate trigger
///        register: the button raises the SAME `CF_TRDOS` latch the #3Dxx M1 trap sets, so
///        `Memory::UpdateZ80Banks()` re-derives `CF_LEAVEDOSADR`/`CF_DOSPORTS` from it and the
///        session closes on the ordinary PC>=#4000 opcode fetch - no bespoke release logic.
///        Deliberately NOT gated on DFFD.4 (WOROM): the M1 trap itself doesn't gate on it
///        either (design section 4.2/Q7 - Karabas blocks entry there too, but that gate was
///        "not adopted" to match the Unreal/ZXMAK2 consensus); WOROM's RAM-at-#0000 override
///        already hides the ROM regardless of the latch, so a second gate here would be both
///        redundant and inconsistent with the M1 trap.
class ProfiMni_Test : public ProfiMachineFixture
{
protected:
    /// The Emulator::RequestMNI() sequence on the Core-level fixture (no Emulator wrapper here)
    void PressMagicButton()
    {
        EmulatorState& state = _context->emulatorState;
        if (!(state.pDFFD & 0x80))
        {
            state.flags |= CF_TRDOS;
            _memory->UpdateZ80Banks();
        }
        _core->GetZ80()->RequestNonMaskedInterrupt();
    }

    bool AcceptNmi()
    {
        Z80* z80 = _core->GetZ80();
        z80->t = 100;  // away from the INT window edges; NMI ignores the window
        z80->eipos = 0;
        return z80->ProcessInterrupts(false, 0, 0);
    }

    void OutDFFD(uint8_t value) { WritePort(0xDFFD, value); }
    void Out7FFD(uint8_t value) { WritePort(0x7FFD, value); }

    /// reset() boots with the DOS latch already on (SYS ROM); drop it so a test's own
    /// button press is the only thing that can raise it
    void DosLatchOff()
    {
        _context->emulatorState.flags &= ~(CF_TRDOS | CF_DOSPORTS);
        _memory->UpdateZ80Banks();
    }

    EmulatorState& State() { return _context->emulatorState; }
};

/// @brief Standard case: DS80=0 - the button raises CF_TRDOS and vectors into the SYS
///        ROM (ROM14=0) exactly like the #3Dxx M1 trap would
TEST_F(ProfiMni_Test, MagicButtonRaisesDosLatchAndVectors)
{
    Out7FFD(0x00);  // ROM14=0 -> SYS ROM once the latch is up
    DosLatchOff();

    EXPECT_EQ(BankTag(0x0000), 0xC2) << "precondition: SYS/DOS latch off, ROM14=0 -> 128K ROM";

    PressMagicButton();

    EXPECT_TRUE(State().flags & CF_TRDOS) << "NMI raised the DOS latch";
    EXPECT_EQ(BankTag(0x0000), 0xC0) << "SYS ROM (page 0) now mapped at #0000";
    EXPECT_TRUE(State().flags & CF_DOSPORTS) << "FDC ports on the bus while the session is active";

    ASSERT_TRUE(AcceptNmi());
    EXPECT_EQ(_core->GetZ80()->pc, 0x0066u) << "NMI vector";
    EXPECT_EQ(BankTag(0x0066), 0xC0) << "the vector fetch comes from the SYS ROM the button just paged in";
}

/// @brief ROM14=1 at press time lands on TR-DOS instead of SYS, same as the M1 trap
TEST_F(ProfiMni_Test, MagicButtonWithRom14SelectsTrDos)
{
    Out7FFD(0x10);  // ROM14=1
    DosLatchOff();

    PressMagicButton();

    EXPECT_TRUE(State().flags & CF_TRDOS);
    EXPECT_EQ(BankTag(0x0000), 0xC1) << "TR-DOS ROM (page 1)";
}

/// @brief DS80=1 (hi-res) blocks the button entirely (Karabas: NMI only sets dos_act
///        when DS80=0) - the latch, banks and pending NMI must all stay untouched
TEST_F(ProfiMni_Test, Ds80BlocksTheButton)
{
    OutDFFD(0x80);  // DS80=1
    Out7FFD(0x00);
    DosLatchOff();

    PressMagicButton();

    EXPECT_FALSE(State().flags & CF_TRDOS) << "DS80=1 blocks the DOS latch";
    EXPECT_NE(BankTag(0x0000), 0xC0) << "SYS ROM not paged in";
    EXPECT_FALSE(State().flags & CF_DOSPORTS);
}

/// @brief WOROM (DFFD.4=1) does NOT gate the button (same as the M1 trap, design Q7) -
///        the latch still raises, but WOROM's RAM-at-#0000 override hides the ROM anyway,
///        so #0000 shows RAM regardless of CF_TRDOS
TEST_F(ProfiMni_Test, WoromDoesNotGateTheLatchButHidesTheRom)
{
    OutDFFD(0x10);  // WOROM=1, DS80=0
    Out7FFD(0x00);
    DosLatchOff();

    PressMagicButton();

    EXPECT_TRUE(State().flags & CF_TRDOS) << "the latch still raises, unlike the DS80 gate";
    EXPECT_NE(BankTag(0x0000), 0xC0) << "WOROM's RAM override hides the ROM at #0000 either way";
    EXPECT_TRUE(ProfiIsRamTag(BankTag(0x0000))) << "RAM (not ROM) is mapped at #0000 under WOROM";
}

/// @brief The session closes on the ordinary PC>=#4000 fetch rule (CF_LEAVEDOSADR),
///        exactly like a session opened by the #3Dxx M1 trap - no bespoke release path
TEST_F(ProfiMni_Test, SessionClosesOnUpperHalfFetchLikeTheM1Trap)
{
    Out7FFD(0x00);
    DosLatchOff();
    PressMagicButton();
    ASSERT_TRUE(State().flags & CF_TRDOS);
    ASSERT_EQ(BankTag(0x0000), 0xC0);

    // CF_LEAVEDOSADR is checked inside Z80Step() itself (z80.cpp), not by a raw memory
    // read - park PC at #4000 (RAM page 5, tag 0x45 = a harmless one-byte LD B,L) and
    // step once to fetch the opcode that closes the session
    ASSERT_TRUE(ProfiIsRamTag(BankTag(0x4000)));
    _core->GetZ80()->pc = 0x4000;
    _core->GetZ80()->Z80Step();

    EXPECT_FALSE(State().flags & CF_TRDOS) << "PC>=#4000 fetch closed the session";
    EXPECT_EQ(BankTag(0x0000), 0xC2) << "back to the plain ROM14=0 -> 128K selection";
}

/// @brief Pressing the button while a DOS session is already open (ROM14=1, latch already
///        up from a prior #3Dxx entry) is a harmless no-op: CF_TRDOS was already set
TEST_F(ProfiMni_Test, ButtonWhileSessionAlreadyOpenIsNoOp)
{
    Out7FFD(0x10);
    DosLatchOff();
    PressMagicButton();
    ASSERT_TRUE(State().flags & CF_TRDOS);
    ASSERT_EQ(BankTag(0x0000), 0xC1);

    PressMagicButton();  // second press, session still open

    EXPECT_TRUE(State().flags & CF_TRDOS);
    EXPECT_EQ(BankTag(0x0000), 0xC1) << "still TR-DOS - nothing regressed";
}

/// @brief Emulator::RequestMNI() end to end on a real PROFI model instance
TEST(ProfiMniEmulator_Test, RequestMniRaisesDosLatch)
{
    EmulatorManager* manager = EmulatorManager::GetInstance();
    std::shared_ptr<Emulator> emulator = manager->CreateEmulatorWithModelAndRAM("", "PROFI", 1024, LoggerLevel::LogError);
    if (!emulator)
        GTEST_SKIP() << "PROFI model emulator could not be created (ROM/config missing)";

    EmulatorContext* context = emulator->GetContext();
    EmulatorState& state = context->emulatorState;

    // Reset boots with the DOS latch already on (SYS ROM) - drop it first so the
    // button's own effect is observable
    state.flags &= ~(CF_TRDOS | CF_DOSPORTS);
    context->pMemory->UpdateZ80Banks();
    ASSERT_FALSE(state.flags & CF_TRDOS);

    emulator->RequestMNI();

    EXPECT_TRUE(state.flags & CF_TRDOS) << "the button raised the DOS latch";
    EXPECT_TRUE(state.flags & CF_DOSPORTS);

    Z80* z80 = context->pCore->GetZ80();
    z80->t = 100;
    z80->eipos = 0;
    EXPECT_TRUE(z80->ProcessInterrupts(false, 0, 0));
    EXPECT_EQ(z80->pc, 0x0066u) << "the CPU lands on the NMI vector";

    EmulatorManager::GetInstance()->RemoveEmulator(emulator->GetId());
}

/// @brief Emulator::RequestMNI() is a no-op on the DOS latch while DS80=1 (hi-res)
TEST(ProfiMniEmulator_Test, RequestMniBlockedInHiRes)
{
    EmulatorManager* manager = EmulatorManager::GetInstance();
    std::shared_ptr<Emulator> emulator = manager->CreateEmulatorWithModelAndRAM("", "PROFI", 1024, LoggerLevel::LogError);
    if (!emulator)
        GTEST_SKIP() << "PROFI model emulator could not be created (ROM/config missing)";

    EmulatorContext* context = emulator->GetContext();
    EmulatorState& state = context->emulatorState;

    state.flags &= ~(CF_TRDOS | CF_DOSPORTS);
    context->pPortDecoder->DecodePortOut(0xDFFD, 0x80, 0x0000);  // DS80=1
    context->pMemory->UpdateZ80Banks();
    ASSERT_FALSE(state.flags & CF_TRDOS);

    emulator->RequestMNI();

    EXPECT_FALSE(state.flags & CF_TRDOS) << "DS80=1 blocks the DOS latch";

    EmulatorManager::GetInstance()->RemoveEmulator(emulator->GetId());
}
