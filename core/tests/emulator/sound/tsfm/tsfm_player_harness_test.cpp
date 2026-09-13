#include "stdafx.h"
#include "pch.h"

#include <utility>

#include "_helpers/emulatortesthelper.h"
#include "_helpers/tsfmplayerharness.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"

/// TurboSound FM player harness validation (TSFM implementation plan, P0).
///
/// The harness pokes the real TFM Music Compiler 1.12 player (TSFM-EL.TAP,
/// block `_tsfmplaye` at 25000) into Pentagon RAM and runs its own main loop.
/// The disassembly (verification/player-entry-points.md) shows the player's
/// init falls through into a chip-reset sequence that polls the TSFM status
/// bit before every register/data pair. On the legacy TurboSound device
/// #FFFD reads return the selected AY register instead of a status word, so
/// the init/reset sequence deterministically parks in the WaitStatus loop
/// once it selects a register whose value has bit 7 set (the mixer register
/// powers up as 0xFF in this emulator). Full-frame playback assertions
/// therefore belong to P4, where the TSFM device answers status reads; the
/// legacy-device tests below pin the memory contract, the init traffic and
/// its determinism.
///
/// Booting the real player into machine RAM is a machine-state test: the frame
/// budgets below keep every test within the suite's timing envelope.
class TsfmPlayerHarness_Test : public ::testing::Test
{
protected:
    Emulator* _emulator = nullptr;
    EmulatorContext* _context = nullptr;
    TsfmPlayerHarness _harness;

    void SetUp() override
    {
        // Legacy-slot contract: the harness tests pin player traffic against
        // the AY device answering #FFFD reads (the shipped default is FM now)
        _emulator = EmulatorTestHelper::CreateEmulatorWithTurboSoundKind(
            "PENTAGON", TurboSoundKind::AY, LoggerLevel::LogError);
        ASSERT_NE(_emulator, nullptr) << "Failed to create emulator";
        _context = _emulator->GetContext();
    }

    void TearDown() override
    {
        _harness.Detach();
        if (_emulator)
        {
            _context->pAudioCallback.store(nullptr, std::memory_order_release);
            _context->pAudioManagerObj.store(nullptr, std::memory_order_release);
            EmulatorTestHelper::CleanupEmulator(_emulator);
            _emulator = nullptr;
        }
    }
};

/// region <Memory contract>

TEST_F(TsfmPlayerHarness_Test, SetupPokesBlocksAndEntersPlayer)
{
    std::string error;
    ASSERT_TRUE(_harness.Setup(_emulator, 0, &error)) << error;

    Z80* z80 = _context->pCore->GetZ80();

    // Player block: `ld hl,08000h` at 25000 (`21 00 80`)
    EXPECT_EQ(z80->DirectRead(25000), 0x21);
    EXPECT_EQ(z80->DirectRead(25001), 0x00);
    EXPECT_EQ(z80->DirectRead(25002), 0x80);

    // lnxdata block at 31000 (loader/graphics code, first byte 0x21)
    EXPECT_EQ(z80->DirectRead(31000), 0x21);

    // Tune block at 32768 with the TFMcom 1.12 signature; the tune was poked
    // last and overwrote the lnxdata tail at the 0x8000 overlap (5819 bytes)
    EXPECT_EQ(z80->DirectRead(32768), 'T');
    EXPECT_EQ(z80->DirectRead(32769), 'F');
    EXPECT_EQ(z80->DirectRead(32770), 'M');
    EXPECT_EQ(z80->DirectRead(32771), 'c');
    EXPECT_EQ(z80->DirectRead(32768 + 5819 - 1), 0x7F) << "tune tail not poked";

    // CPU entered the player main loop with the tune pointer
    EXPECT_EQ(z80->pc, TsfmPlayerHarness::PLAYER_BASE);
    EXPECT_EQ(z80->hl, TsfmPlayerHarness::TUNE_BASE);

    // First tune of the collection (by TAP order, skipping the terminator block)
    EXPECT_EQ(_harness.GetTuneName(), "03 DJ Tepp");
}

/// endregion

/// region <Player traffic on the legacy device>

TEST_F(TsfmPlayerHarness_Test, InitWritesChipResetThenParksInWaitStatus)
{
    ASSERT_TRUE(_harness.Setup(_emulator, 0));

    _harness.RunFrames(8);

    // Frame 0: init relocation + the chip-reset sequence. The reset opens
    // with control word 0xF8 on #FFFD, then drives register/data pairs down
    // from register 0x0D on both chips
    const auto& writes = _harness.GetWrites();
    ASSERT_GE(writes.size(), 14u) << "init/reset sequence did not start";
    EXPECT_EQ(writes[0].port, 0xFFFD);
    EXPECT_EQ(writes[0].value, 0xF8) << "reset sequence must open with control word 0xF8";

    size_t pairs = 0;
    for (size_t i = 1; i + 1 < writes.size(); i += 2)
    {
        if (writes[i].port == 0xFFFD && writes[i + 1].port == 0xBFFD)
            pairs++;
        else
            break;
    }
    EXPECT_GE(pairs, 6u) << "expected descending register/data pairs (0x0D..)";
    EXPECT_EQ(writes[1].value, 0x0D);

    // The legacy device answers the status poll with register data: after the
    // reset sequence selects the mixer register (power-on value 0xFF, bit 7
    // set) the player parks in the WaitStatus loop inside sub_628A
    // (0x628B..0x6295) and no further traffic appears
    EXPECT_GT(_harness.GetChip0WriteCount(), 0u);
    EXPECT_GT(_harness.GetChip1WriteCount(), 0u);
    EXPECT_EQ(writes.back().port, 0xFFFD);
    EXPECT_EQ(writes.back().value, 0x07) << "last write should select the mixer register";

    Z80* z80 = _context->pCore->GetZ80();
    EXPECT_GE(z80->pc, 0x628B) << "player is not parked in the WaitStatus loop";
    EXPECT_LE(z80->pc, 0x6295) << "player is not parked in the WaitStatus loop";

    // Deterministic stop: all later frames stay silent
    const auto& perFrame = _harness.GetPerFrameWriteCounts();
    ASSERT_EQ(perFrame.size(), 8u);
    for (size_t i = 1; i < perFrame.size(); i++)
        EXPECT_EQ(perFrame[i], 0u) << "unexpected traffic after the legacy WaitStatus park";
}

/// endregion

/// region <Determinism>

namespace
{
/// Run tune 0 for frameCount frames on a fresh emulator and return
/// (traffic hash, per-frame write counts)
std::pair<uint64_t, std::vector<uint32_t>> RunFreshPlayerSession(int frameCount)
{
    Emulator* emulator = EmulatorTestHelper::CreateEmulatorWithTurboSoundKind(
        "PENTAGON", TurboSoundKind::AY, LoggerLevel::LogError);
    if (!emulator)
        return {0, {}};

    TsfmPlayerHarness harness;
    if (!harness.Setup(emulator, 0))
    {
        EmulatorTestHelper::CleanupEmulator(emulator);
        return {0, {}};
    }
    harness.RunFrames(frameCount);

    std::pair<uint64_t, std::vector<uint32_t>> result = {harness.GetTrafficHash(), harness.GetPerFrameWriteCounts()};
    harness.Detach();
    EmulatorTestHelper::CleanupEmulator(emulator);
    return result;
}
}  // namespace

/// Port traffic must be identical across fresh emulator instances - even the
/// legacy-device park in the WaitStatus loop is deterministic. The TTD seek
/// tests (P5) and the P2 "recorded capture is bit-identical" gate rely on the
/// device replaying this exact sequence
TEST_F(TsfmPlayerHarness_Test, TrafficIsDeterministicAcrossInstances)
{
    auto first = RunFreshPlayerSession(10);
    auto second = RunFreshPlayerSession(10);

    ASSERT_FALSE(first.second.empty()) << "first session produced no frames";
    ASSERT_EQ(first.second.size(), second.second.size());
    EXPECT_EQ(first.first, second.first) << "port traffic differs between identical sessions";
    EXPECT_EQ(first.second, second.second) << "per-frame write counts differ between identical sessions";
}

/// endregion
