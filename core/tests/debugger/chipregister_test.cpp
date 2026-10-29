// Sound chip register breakpoints (BRK_CHIPREG, docs/inprogress/2026-10-27-platform-debugging, P3): the chips' host port
// paths report a register write while a breakpoint asks; the stop is the main CPU's, after its OUT

#include "stdafx.h"
#include "pch.h"

#include <gtest/gtest.h>

#include <string>

#include "_helpers/emulatortesthelper.h"
#include "_helpers/soundcardscope.h"
#include "base/featuremanager.h"
#include "debugger/breakpoints/breakpointmanager.h"
#include "debugger/breakpoints/chipregister.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/ports/portdecoder.h"

namespace
{
constexpr uint16_t kPortAySelect = 0xFFFD;
constexpr uint16_t kPortAyData = 0xBFFD;
constexpr uint8_t kRegMixer = 7;
constexpr uint8_t kMixerAllOff = 0x3F;  // tone and noise of the three channels off
constexpr uint8_t kRegToneA = 0;

BreakpointSpec ChipSpec(SoundChipKind chip, uint16_t reg)
{
    BreakpointSpec spec;
    spec.type = BRK_CHIPREG;
    spec.access = 0;
    spec.chip = static_cast<uint8_t>(chip);
    spec.address = reg;
    return spec;
}
}  // namespace

TEST(ChipRegisterBreakpoint_Test, TheSpecIsCheckedAndTheHotPathAnswersByChipUnitRegisterAndValue)
{
    EmulatorContext context(LoggerLevel::LogError);
    BreakpointManager manager(&context);
    std::string error;

    BreakpointSpec bad = ChipSpec(SoundChipKind::Count, 0);
    EXPECT_EQ(manager.AddBreakpoint(bad, error), BRK_INVALID);
    EXPECT_NE(error.find("unknown sound chip"), std::string::npos) << error;
    bad = ChipSpec(SoundChipKind::Ay, 16);
    EXPECT_EQ(manager.AddBreakpoint(bad, error), BRK_INVALID);
    EXPECT_NE(error.find("0..15"), std::string::npos) << error;
    bad = ChipSpec(SoundChipKind::Ay, 0);
    bad.hasPage = true;
    EXPECT_EQ(manager.AddBreakpoint(bad, error), BRK_INVALID);
    BreakpointSpec memory;
    memory.chipUnit = 1;
    EXPECT_EQ(manager.AddBreakpoint(memory, error), BRK_INVALID) << "a unit belongs to chip register breakpoints";
    EXPECT_FALSE(context.chipRegisterWatch) << "nothing asks the chips yet";

    BreakpointSpec mixer = ChipSpec(SoundChipKind::Ay, kRegMixer);
    mixer.nrValue = kMixerAllOff;
    mixer.nrMask = kMixerAllOff;
    const uint16_t silence = manager.AddBreakpoint(mixer, error);
    ASSERT_NE(silence, BRK_INVALID) << error;
    EXPECT_TRUE(context.chipRegisterWatch);
    EXPECT_EQ(manager.HandleChipRegister(SoundChipKind::Ay, 0, kRegMixer, 0xFF), silence) << "the masked bits match";
    EXPECT_EQ(manager.HandleChipRegister(SoundChipKind::Ay, 1, kRegMixer, kMixerAllOff), silence) << "any unit";
    EXPECT_EQ(manager.HandleChipRegister(SoundChipKind::Ay, 0, kRegMixer, 0x38), BRK_INVALID) << "a tone is on";
    EXPECT_EQ(manager.HandleChipRegister(SoundChipKind::Ay, 0, kRegToneA, kMixerAllOff), BRK_INVALID);
    EXPECT_EQ(manager.HandleChipRegister(SoundChipKind::Fm, 0, kRegMixer, kMixerAllOff), BRK_INVALID) << "another chip";

    BreakpointSpec tones = ChipSpec(SoundChipKind::Ay, kRegToneA);
    tones.hasEnd = true;
    tones.addressEnd = 5;
    tones.chipUnit = 1;
    const uint16_t second = manager.AddBreakpoint(tones, error);
    ASSERT_NE(second, BRK_INVALID) << error;
    EXPECT_EQ(manager.HandleChipRegister(SoundChipKind::Ay, 1, 5, 0x12), second);
    EXPECT_EQ(manager.HandleChipRegister(SoundChipKind::Ay, 0, 5, 0x12), BRK_INVALID) << "the first AY";
    EXPECT_EQ(manager.HandleChipRegister(SoundChipKind::Ay, 1, 6, 0x12), BRK_INVALID) << "past the range";

    const uint16_t opl4 = manager.AddBreakpoint(ChipSpec(SoundChipKind::Opl4, 0x105), error);
    ASSERT_NE(opl4, BRK_INVALID) << error;
    EXPECT_EQ(manager.HandleChipRegister(SoundChipKind::Opl4, 0, 0x105, 0x03), opl4) << "bank 1 register 5";
    EXPECT_EQ(manager.HandleChipRegister(SoundChipKind::Opl4, 0, 0x005, 0x03), BRK_INVALID) << "bank 0";

    // A condition (platform-debugging P4): VAL the byte written, PORT the register
    BreakpointSpec loud = ChipSpec(SoundChipKind::Ay, 8);
    loud.hasEnd = true;
    loud.addressEnd = 10;
    loud.condition = "(VAL & 0F) == 0F && PORT != 9";
    const uint16_t volume = manager.AddBreakpoint(loud, error);
    ASSERT_NE(volume, BRK_INVALID) << error;
    EXPECT_EQ(manager.HandleChipRegister(SoundChipKind::Ay, 0, 8, 0x0F), volume);
    EXPECT_EQ(manager.HandleChipRegister(SoundChipKind::Ay, 0, 8, 0x0E), BRK_INVALID);
    EXPECT_EQ(manager.HandleChipRegister(SoundChipKind::Ay, 0, 9, 0x0F), BRK_INVALID);
    EXPECT_EQ(manager.GetBreakpointById(volume)->matchCount, 3u);

    manager.ClearUserBreakpoints();
    EXPECT_FALSE(context.chipRegisterWatch) << "the chips stop reporting";
}

// A Pentagon's AY: the program's OUTs reach the breakpoint, the select latch alone does not, and a direct run ends
// after the OUT that wrote the register
TEST(ChipRegisterBreakpoint_Test, TheProgramsOutToTheAyStopsTheMainCpuAfterIt)
{
    SoundCardScope turboSound(TestSound::TurboSound);  // the AY is the subject
    Emulator* emulator = EmulatorTestHelper::CreateStandardEmulator("PENTAGON", LoggerLevel::LogError);
    ASSERT_NE(emulator, nullptr);
    EmulatorContext* context = emulator->GetContext();
    emulator->DebugOn();
    BreakpointManager* manager = emulator->GetBreakpointManager();
    BreakpointSpec spec = ChipSpec(SoundChipKind::Ay, kRegMixer);
    spec.nrValue = kMixerAllOff;
    spec.nrMask = 0xFF;
    std::string error;
    const uint16_t id = manager->AddBreakpoint(spec, error);
    ASSERT_NE(id, BRK_INVALID) << error;

    PortDecoder* ports = context->pPortDecoder;
    ports->DecodePortOut(kPortAySelect, kRegMixer, 0);
    EXPECT_EQ(manager->GetLastTriggeredBreakpointID(), BRK_INVALID) << "selecting the register writes nothing";
    ports->DecodePortOut(kPortAyData, 0x38, 0);
    EXPECT_EQ(manager->GetLastTriggeredBreakpointID(), BRK_INVALID) << "another value";
    ports->DecodePortOut(kPortAyData, kMixerAllOff, 0);
    EXPECT_EQ(manager->GetLastTriggeredBreakpointID(), id);
    manager->ClearLastTriggeredBreakpoint();

    // LD BC,#FFFD : LD A,7 : OUT (C),A : LD B,#BF : LD A,#3F : OUT (C),A : JR $
    constexpr uint16_t kCode = 0x8000;
    const uint8_t program[] = {0x01, 0xFD, 0xFF, 0x3E, kRegMixer, 0xED, 0x79, 0x06,
                               0xBF, 0x3E, kMixerAllOff, 0xED, 0x79, 0x18, 0xFE};
    Z80* z80 = context->pCore->GetZ80();
    for (size_t i = 0; i < sizeof(program); i++)
        z80->DirectWrite(static_cast<uint16_t>(kCode + i), program[i]);
    z80->pc = kCode;
    z80->iff1 = z80->iff2 = 0;
    emulator->RunNFrames(1, false);
    const Emulator::DebugStop stop = emulator->LastStop();
    EXPECT_EQ(stop.reason, Emulator::DebugStop::Reason::Breakpoint);
    EXPECT_EQ(stop.breakpoint.breakpointId, id);
    EXPECT_EQ(stop.breakpoint.kind, BreakpointHitKind::ChipRegister);
    EXPECT_STREQ(BreakpointHitKindName(stop.breakpoint.kind), "chip_register");
    EXPECT_EQ(stop.breakpoint.address, kRegMixer);
    EXPECT_EQ(z80->pc, kCode + 13) << "after the OUT that wrote the mixer";
    EmulatorTestHelper::CleanupEmulator(emulator);
}
