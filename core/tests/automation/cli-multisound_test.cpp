/// @file cli-multisound_test.cpp
/// @brief CLI `multisound` / `midi` (cli-multisound.h): the card's report and the MIDI side as text, on a real machine
/// with the card and on one without (ZX-MultiSound tdd-integration.md §5). No CLI socket.

#include <gtest/gtest.h>

#include <string>

#include "../../automation/cli/src/commands/cli-multisound.h"
#include "../emulator/slots/cards/multisound/multisoundstagedmachine.h"

using namespace multisoundtest;

TEST(CliMultiSound_Test, SummaryAndMidiText)
{
    StagedMachine m("pentagon128k", "zxbus.1 = multisound\nzxbus.1.dip = ym,gs");
    ASSERT_TRUE(m.Ok());
    const std::string summary = CliMultiSound::Summary(DeviceState::MultiSound(m.Context()));
    EXPECT_EQ(summary.rfind("ZX-MultiSound (UzixLS) in zxbus.1, fit real\n", 0), 0u) << summary;
    EXPECT_NE(summary.find("  options: dip=ym,gs gsRam=1m ctrlMask=pro\n"), std::string::npos) << summary;
    EXPECT_NE(summary.find("  built-in ay: shadowed by zxbus.1\n"), std::string::npos) << summary;
    EXPECT_NE(summary.find("  YM2203 0 (U4 (MIDI pin)): FM keyed 0, sounding 0\n"), std::string::npos) << summary;
    EXPECT_NE(summary.find("  DAC: 0="), std::string::npos) << summary;

    const std::string midi = CliMultiSound::MidiText(DeviceState::Midi(m.Context()));
    EXPECT_EQ(midi.rfind("SAM2695 (General MIDI) in zxbus.1, bank ", 0), 0u) << midi;
    EXPECT_NE(midi.find("  ch  1  prog   1"), std::string::npos) << midi;
    EXPECT_NE(midi.find("  ch 16  prog   1"), std::string::npos) << midi;

    StagedMachine none("pentagon128k", "ay-socket = tsfm");
    ASSERT_TRUE(none.Ok());
    EXPECT_EQ(CliMultiSound::Summary(DeviceState::MultiSound(none.Context())).rfind("multisound: no ZX-MultiSound card fitted", 0), 0u);
    EXPECT_EQ(CliMultiSound::MidiText(DeviceState::Midi(none.Context())).rfind("midi: no MIDI synthesizer fitted", 0), 0u);
}
