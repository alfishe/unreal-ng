// DeviceState::MultiSound and DeviceState::Midi (core/src/emulator/slots/cards/multisound/multisounddevicestate.cpp):
// the ZX-MultiSound's report every surface renders (ZX-MultiSound tdd-integration.md §5, MS-6). The YM2203 pair, the SSG
// halves and the General Sound use the reports of the TurboSound FM and the GS slot: the same keys for the same chip.
//
// Each test builds a Pentagon with the card from the shipped config (~10-25 ms; the MIDI test runs six frames of it)

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <string>
#include <vector>

#include "debugger/ttd/ttdinputapply.h"
#include "emulator/slots/cards/multisound/multisoundlogic.h"
#include "emulator/sound/midi/midicontrol.h"
#include "emulator/sound/soundmanager.h"
#include "emulator/state/devicestate.h"
#include "emulator/state/statenodejson.h"
#include "multisoundstagedmachine.h"
#include "sam2695/sam2695.h"

using namespace multisoundtest;

namespace
{

const StateNode& Field(const StateNode& node, const std::string& key)
{
    static const StateNode missing;
    const StateNode* found = node.find(key);
    return found != nullptr ? *found : missing;
}

std::vector<std::string> Keys(const StateNode& node)
{
    std::vector<std::string> keys;
    for (const auto& member : node.members)
        keys.push_back(member.first);
    return keys;
}

/// The card's synthesizer receives these MIDI bytes at the machine's now (a whole-byte UART, as the line would deliver)
void SendMidi(MultiSoundSlotCard& card, std::initializer_list<uint8_t> bytes)
{
    const uint64_t t = card.Now();
    for (uint8_t byte : bytes)
        card.Card().Synth().WriteByte(t, byte);
}

} // namespace

/// The card's report: slot, options with the unofficial-mask note, the board AY it shadows, the CPLD latches, both
/// YM2203 with the TSFM report's keys, the SAA, the GS report, the DACs; a machine without the card says how to fit one
TEST(MultiSoundDeviceState_Test, ReportsTheCardWithTheSharedChipReports)
{
    StagedMachine m("pentagon128k", "zxbus.1 = multisound\nzxbus.1.ctrlMask = classic");
    ASSERT_TRUE(m.Ok());
    Out(m, 0xFFFD, 0x07);
    Out(m, 0xBFFD, 0x38);
    Out(m, 0x01FF, 0x1C);
    Out(m, 0x00FF, 0x01);
    Out(m, 0x00B3, 0x5A);
    Out(m, 0x000F, 0xC0);

    const StateNode report = DeviceState::MultiSound(m.Context());
    ASSERT_TRUE(Field(report, "available").b) << Field(report, "description").s;
    EXPECT_EQ(Field(report, "slot").s, "zxbus.1");
    EXPECT_EQ(Field(report, "fit").s, "real");
    const StateNode& options = Field(report, "options");
    EXPECT_EQ(Field(options, "text").s, "dip=ym,saa,gs,sd gsRam=1m ctrlMask=classic");
    EXPECT_TRUE(Field(Field(options, "dip"), "saa").b);
    EXPECT_EQ(Field(options, "ctrl_mask").s, "classic");
    EXPECT_NE(Field(options, "ctrl_mask_note").s.find("unofficial"), std::string::npos);
    const StateNode& shadowed = Field(report, "shadowed_devices");
    ASSERT_EQ(shadowed.size(), 1u);
    EXPECT_EQ(Field(shadowed.items[0], "id").s, "ay");
    EXPECT_EQ(Field(shadowed.items[0], "state").s, "shadowed by zxbus.1");

    const StateNode& ym = Field(report, "ym");
    EXPECT_EQ(Field(ym, "master_clock_hz").i, 3500000);
    ASSERT_EQ(Field(ym, "chips").size(), 2u);
    const StateNode& u4 = Field(ym, "chips").items[0];
    EXPECT_EQ(Field(Field(Field(u4, "ssg"), "mixer"), "register_value").i, 0x38) << "the SSG half: the AY report";
    EXPECT_EQ(Field(Field(u4, "fm"), "chip_type").s, "YM2203");

    EXPECT_TRUE(Field(Field(report, "saa"), "sound_enabled").b);
    EXPECT_EQ(Field(Field(report, "gs"), "data_from_host").i, 0x5A) << "the GS report of the GS slot";
    ASSERT_EQ(Field(report, "dac").size(), 4u);
    EXPECT_EQ(Field(Field(Field(report, "logic"), "dac").items[0], "sample").i, MultiSoundLogic::ConvertSample(0xC0))
        << "the CPLD latched the SounDrive write; the output stage plays it at the frame's end";
    EXPECT_EQ(Field(Field(report, "dac").items[2], "side").s, "right");
    EXPECT_EQ(Field(Field(Field(report, "midi"), "line"), "connected").b, true);

    // The FM half has the keys of the TurboSound FM's report (one builder for both)
    StagedMachine tsfm("pentagon128k", "ay-socket = tsfm");
    ASSERT_TRUE(tsfm.Ok());
    EXPECT_EQ(Keys(Field(u4, "fm")), Keys(DeviceState::FmChip(tsfm.Context(), 0)));
    EXPECT_EQ(Keys(Field(u4, "ssg")), Keys(DeviceState::AyChipReport(*tsfm.Context()->pSoundManager->getAYChip(0), 0, 1750000.0)));

    const StateNode none = DeviceState::MultiSound(tsfm.Context());
    EXPECT_FALSE(Field(none, "available").b);
    EXPECT_NE(Field(none, "description").s.find("slots plug zxbus.next multisound"), std::string::npos);
    EXPECT_FALSE(Field(DeviceState::Midi(tsfm.Context()), "available").b);
}

/// The MIDI report names the parts, their presets and the notes sounding; a panic (MidiControl, a TTD live input applied
/// at once on a machine that is not running) stops every voice and keeps the programs
TEST(MultiSoundDeviceState_Test, MidiReportNamesTheNotesAndPanicStopsThem)
{
    const std::string bank = WriteTestBank();
    StagedMachine m("pentagon128k", "zxbus.1 = multisound", "[MIDI]\nBank = " + bank);
    ASSERT_TRUE(m.Ok());
    MultiSoundSlotCard* card = m.Card();
    ASSERT_NE(card, nullptr);
    ParkCpu(m);
    m.Machine().RunNFrames(4);   // the SAM2695's 50 ms boot window after the reset

    SendMidi(*card, {0x90, 0x3C, 0x64, 0x40, 0x64});   // Note On channel 1: C4, then E4 (running status)
    m.Machine().RunNFrames(1);
    StateNode midi = DeviceState::Midi(m.Context());
    ASSERT_TRUE(Field(midi, "available").b);
    EXPECT_EQ(Field(Field(midi, "bank"), "status").s, "loaded");
    ASSERT_EQ(Field(midi, "parts").size(), 16u);
    const StateNode& part1 = Field(midi, "parts").items[0];
    EXPECT_EQ(Field(part1, "channel").i, 1);
    EXPECT_EQ(Field(part1, "program").i, 1);
    EXPECT_EQ(Field(part1, "preset").s, "Sine");
    EXPECT_EQ(StateNodeToJsonText(Field(part1, "keys")), "[60,64]");
    EXPECT_EQ(StateNodeToJsonText(Field(part1, "notes")), R"(["C4","E4"])");
    EXPECT_EQ(Field(part1, "active_voices").i, 2);

    const MidiControlReply panic = MidiControl::Execute(m.Context(), "panic");
    EXPECT_TRUE(panic.ok) << panic.message;
    m.Machine().RunNFrames(1);
    midi = DeviceState::Midi(m.Context());
    EXPECT_EQ(Field(Field(midi, "parts").items[0], "active_voices").i, 0) << "every voice stopped";
    EXPECT_EQ(Field(Field(midi, "parts").items[0], "preset").s, "Sine") << "the program stays";

    // The running status survived the panic: a data pair alone plays again
    SendMidi(*card, {0x43, 0x64});
    m.Machine().RunNFrames(1);
    midi = DeviceState::Midi(m.Context());
    EXPECT_EQ(StateNodeToJsonText(Field(Field(midi, "parts").items[0], "notes")), R"(["G4"])");

    std::error_code ignored;
    std::filesystem::remove(bank, ignored);
}

/// MidiControl's refusals, and the replay side of the panic: ApplyInputEvent drives every slot card with a synthesizer
TEST(MultiSoundDeviceState_Test, MidiControlRefusalsAndTheReplayApply)
{
    StagedMachine m("pentagon128k", "zxbus.1 = multisound");
    ASSERT_TRUE(m.Ok());
    EXPECT_EQ(MidiControl::Execute(m.Context(), "louder").httpStatus, 400);

    ttd::TTDInputEvent event;
    event.kind = ttd::TTDInputKind::MidiPanic;
    EXPECT_TRUE(ttd::ApplyInputEvent(event, ttd::InputDevicesOf(m.Context()))) << "the card takes it";

    StagedMachine none("pentagon128k", "ay-socket = tsfm");
    ASSERT_TRUE(none.Ok());
    const MidiControlReply refused = MidiControl::Execute(none.Context(), "panic");
    EXPECT_EQ(refused.httpStatus, 404);
    EXPECT_NE(refused.message.find("no MIDI synthesizer"), std::string::npos) << refused.message;
    EXPECT_FALSE(ttd::ApplyInputEvent(event, ttd::InputDevicesOf(none.Context()))) << "no card: not applied";
}
