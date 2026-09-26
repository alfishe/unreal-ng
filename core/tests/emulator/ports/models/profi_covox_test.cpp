#include "stdafx.h"
#include "pch.h"

#include "emulator/ports/models/portdecoder_profi.h"
#include "emulator/ports/models/profifixture.h"
#include "emulator/sound/covox.h"
#include "emulator/sound/soundmanager.h"

/// @brief Profi Covox/SoundRive DAC: NORMAL-mode #5F/#3F, the CP/M-extended-mode
///        aliases #C7/#A7, and the mono-leak silencing fix (`0a98d677`).
///
/// PortDecoder_Profi routes Covox through `_context->pSoundManager->getCovox()`
/// directly (not the self-decoding tryClaimOut path Pentagon/Scorpion use), so a
/// SoundManager with a live Covox instance is a precondition. `ProfiMachineFixture`
/// builds its machine before any test body runs and doesn't enable sound, so this
/// fixture reconstructs `_context->pSoundManager` with `config.sound.sd = 1` and
/// swaps it in - Core's OWN `_sound` pointer (private, deleted by Core::Release() on
/// teardown) is left untouched, so this is a test-local shadow, not a leak or a
/// double-free: the original (Covox-less) SoundManager Core owns is still the one
/// wired into the AY/FDC port-device map and gets destroyed normally.
class ProfiCovox_Test : public ProfiMachineFixture
{
protected:
    SoundManager* _testSoundManager = nullptr;

    void SetUp() override
    {
        ProfiMachineFixture::SetUp();
        _context->config.sound.sd = 1;
        _testSoundManager = new SoundManager(_context);
        _context->pSoundManager = _testSoundManager;
    }

    void TearDown() override
    {
        delete _testSoundManager;
        _testSoundManager = nullptr;
        // ProfiMachineFixture::TearDown() -> DestroyMachine() -> ~Core() ->
        // Release() nulls _context->pSoundManager and deletes Core's own
        // (Covox-less) SoundManager instance - never the one deleted above.
        ProfiMachineFixture::TearDown();
    }

    PortDecoder_Profi* Decoder() { return dynamic_cast<PortDecoder_Profi*>(_context->pPortDecoder); }

    Covox* GetCovox() { return _context->pSoundManager->getCovox(); }

    /// Channel order matches Covox::Channel: LeftA=0, LeftB=1, RightA=2, RightB=3
    void GetLatches(uint8_t (&out)[4]) { GetCovox()->getDacLatches(out); }

    /// reset() boots with the DOS latch already on (a plain TR-DOS session, where
    /// #5F/#3F are Beta128 FDC ports and Covox has no ports at all) - drop it so
    /// NORMAL-mode tests see the ports they're actually testing
    void DosLatchOff()
    {
        _context->emulatorState.flags &= ~(CF_TRDOS | CF_DOSPORTS);
        _memory->UpdateZ80Banks();
    }
};

/// @brief NORMAL mode: #5F is Left, #3F is Right, each on the A channel
TEST_F(ProfiCovox_Test, NormalModePortsRouteToTheirOwnChannel)
{
    DosLatchOff();
    WritePort(0x5F, 0x11);
    WritePort(0x3F, 0x22);

    uint8_t latches[4];
    GetLatches(latches);
    EXPECT_EQ(latches[static_cast<int>(Covox::Channel::LeftA)], 0x11);
    EXPECT_EQ(latches[static_cast<int>(Covox::Channel::RightA)], 0x22);
}

/// @brief CP/M-extended mode (cpm && rom14): #C7 is Left, #A7 is Right - real Profi
///        hardware moves the DAC here because the FDC has taken #1F..#7F away from it
TEST_F(ProfiCovox_Test, ExtModePortsRouteToTheirOwnChannel)
{
    WritePort(0x7FFD, 0x10);  // rom14=1
    WritePort(0xDFFD, 0x20);  // cpm=1 -> IsExtMode()

    WritePort(0xC7, 0x33);
    WritePort(0xA7, 0x44);

    uint8_t latches[4];
    GetLatches(latches);
    EXPECT_EQ(latches[static_cast<int>(Covox::Channel::LeftA)], 0x33) << "#C7 = Left";
    EXPECT_EQ(latches[static_cast<int>(Covox::Channel::RightA)], 0x44) << "#A7 = Right";
}

/// @brief #87 and #E7 satisfy the (port & 0x9F) == 0x87 mask but are unused 8255
///        control-register addresses on real hardware (UnrealSpeccy io.cpp never
///        maps them to a DAC channel either) - must not reach Covox
TEST_F(ProfiCovox_Test, ExtModeControlAddressesDoNotReachCovox)
{
    WritePort(0x7FFD, 0x10);
    WritePort(0xDFFD, 0x20);

    uint8_t before[4];
    GetLatches(before);

    WritePort(0x87, 0x55);
    WritePort(0xE7, 0x66);

    uint8_t after[4];
    GetLatches(after);
    for (int i = 0; i < 4; i++)
        EXPECT_EQ(after[i], before[i]) << "channel " << i << " must be untouched by #87/#E7";
}

/// @brief Being in a dosPorts session (e.g. a plain TR-DOS session left over from
///        reset()) without also satisfying IsExtMode() (cpm && rom14) must not reach
///        the aliases either - both conditions are required, not just dosPorts
TEST_F(ProfiCovox_Test, ExtAliasesRequireBothDosPortsAndExtMode)
{
    // rom14=1 but cpm=0 (DFFD.5 clear): dosPorts stays on (DOS latch from reset()),
    // but IsExtMode() is false
    WritePort(0x7FFD, 0x10);
    ASSERT_TRUE(_context->emulatorState.flags & CF_DOSPORTS);
    ASSERT_FALSE(Decoder()->IsExtMode());

    uint8_t before[4];
    GetLatches(before);

    WritePort(0xC7, 0x77);
    WritePort(0xA7, 0x88);

    uint8_t after[4];
    GetLatches(after);
    for (int i = 0; i < 4; i++)
        EXPECT_EQ(after[i], before[i]) << "channel " << i << " must be untouched - dosPorts without ExtMode";
}

/// @brief Outside dosPorts entirely (NORMAL mode), #C7/#A7 aren't Covox ports at all
///        (only #5F/#3F are) - confirms the aliases don't leak into NORMAL mode either
TEST_F(ProfiCovox_Test, ExtAliasesAreNotCovoxPortsInNormalMode)
{
    DosLatchOff();

    uint8_t before[4];
    GetLatches(before);

    WritePort(0xC7, 0x77);
    WritePort(0xA7, 0x88);

    uint8_t after[4];
    GetLatches(after);
    for (int i = 0; i < 4; i++)
        EXPECT_EQ(after[i], before[i]) << "channel " << i << " must be untouched in NORMAL mode";
}

/// @brief Mono-leak fix (0a98d677): entering a plain TR-DOS/Beta128 FDC session (no
///        CP/M-extended-mode alias available) silences Covox exactly once instead of
///        leaving its last-written level stuck forever
TEST_F(ProfiCovox_Test, PlainTrdosSessionSilencesCovoxOnce)
{
    DosLatchOff();
    // Play something in NORMAL mode first
    WritePort(0x5F, 0xFF);
    WritePort(0x3F, 0xFF);
    uint8_t latches[4];
    GetLatches(latches);
    ASSERT_EQ(latches[static_cast<int>(Covox::Channel::LeftA)], 0xFF);
    ASSERT_EQ(latches[static_cast<int>(Covox::Channel::RightA)], 0xFF);

    // Enter a plain TR-DOS session (DOS latch on, no CP/M mode): dosPorts on, ExtMode off
    _context->emulatorState.flags |= (CF_TRDOS | CF_DOSPORTS);
    _context->pPortDecoder->DecodePortOut(0x1F, 0x00, 0x0000);  // any port write ticks the check

    GetLatches(latches);
    EXPECT_EQ(latches[static_cast<int>(Covox::Channel::LeftA)], 0x80) << "silenced to midpoint";
    EXPECT_EQ(latches[static_cast<int>(Covox::Channel::RightA)], 0x80) << "silenced to midpoint";
}

/// @brief Entering CP/M-extended mode from NORMAL must NOT silence Covox - the DAC
///        just moves from #5F/#3F to #C7/#A7, it never actually loses the bus, unlike
///        a plain TR-DOS/Beta128 session where Covox has no ports at all
TEST_F(ProfiCovox_Test, EnteringExtModeDoesNotSilenceCovox)
{
    DosLatchOff();
    WritePort(0x5F, 0xAB);
    uint8_t latches[4];
    GetLatches(latches);
    ASSERT_EQ(latches[static_cast<int>(Covox::Channel::LeftA)], 0xAB);

    // Transition straight into CP/M-extended mode
    WritePort(0x7FFD, 0x10);  // rom14=1
    WritePort(0xDFFD, 0x20);  // cpm=1

    GetLatches(latches);
    EXPECT_EQ(latches[static_cast<int>(Covox::Channel::LeftA)], 0xAB)
        << "CP/M-extended mode must not silence Covox - it kept a live alias";

    // And the alias is immediately usable
    WritePort(0xC7, 0xCD);
    GetLatches(latches);
    EXPECT_EQ(latches[static_cast<int>(Covox::Channel::LeftA)], 0xCD);
}

/// @brief Leaving CP/M-extended mode for a plain TR-DOS/Beta128 session (rom14 stays
///        1, cpm drops to 0 while the DOS latch is on) DOES silence Covox - the alias
///        it had is gone and no other port set is available
TEST_F(ProfiCovox_Test, LeavingExtModeForPlainTrdosSilencesCovox)
{
    WritePort(0x7FFD, 0x10);  // rom14=1
    WritePort(0xDFFD, 0x20);  // cpm=1 -> ExtMode
    WritePort(0xC7, 0xEE);
    uint8_t latches[4];
    GetLatches(latches);
    ASSERT_EQ(latches[static_cast<int>(Covox::Channel::LeftA)], 0xEE);

    // Drop cpm while entering a TR-DOS session: still dosPorts, but no longer ExtMode
    _context->emulatorState.flags |= CF_TRDOS;
    WritePort(0xDFFD, 0x00);  // cpm=0

    GetLatches(latches);
    EXPECT_EQ(latches[static_cast<int>(Covox::Channel::LeftA)], 0x80)
        << "silenced - no port set reaches Covox in a plain TR-DOS session";
}
