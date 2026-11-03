// The device access watch (core/src/emulator/ports/deviceaccesswatch.h): every access attributed by the slot
// registry's claims of the machine, with the claim's channel (half-row, selected register) and input role, so a port
// number alone never makes a joystick. Built from the machines' declarations without an emulator: microseconds each

#include "stdafx.h"
#include "pch.h"

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "emulator/ports/deviceaccesswatch.h"
#include "emulator/ports/portclaimindex.h"
#include "emulator/config.h"
#include "emulator/platform.h"
#include "emulator/slots/portowners.h"
#include "emulator/slots/refdata/refdata.h"
#include "emulator/slots/slotconfig.h"
#include "emulator/slots/slotmanager.h"
#include "emulator/slots/slotplanner.h"

namespace
{
using slots::InputRole;

/// The claims of @p model as the slot manager plans them, with the @p cards fitted ({slot, card})
slots::PortOwners Owners(MEM_MODEL model, std::vector<std::pair<std::string, std::string>> cards = {})
{
    static const slots::SlotPlanner planner;
    auto config = std::make_unique<CONFIG>();
    config->mem_model = model;
    ParseSlotsSection(cards, config->slotConfig);
    SlotManager::Project(config->slotConfig, *config);
    const SlotManager::Result result = SlotManager::Plan(*config);
    EXPECT_NE(result.machine, nullptr);
    for (const auto& [slot, card] : cards)
    {
        const SlotManager::Slot* fitted = result.FindSlot(slot);
        EXPECT_TRUE(fitted != nullptr && !fitted->entry.disabled && fitted->entry.card == card)
            << slot << " = " << card << " not fitted: " << (fitted ? fitted->entry.disabledReason : "no entry")
            << " " << result.Refusal();
    }
    return slots::PortOwners::Of(result, planner);
}

/// Guest code: @p bytes at 0x8000, #00 elsewhere
struct Code
{
    std::vector<uint8_t> bytes;
    static uint8_t Read(const void* context, uint16_t address)
    {
        const auto& code = static_cast<const Code*>(context)->bytes;
        const unsigned offset = static_cast<uint16_t>(address - 0x8000);
        return offset < code.size() ? code[offset] : uint8_t{0};
    }
};

struct Rig
{
    PortClaimIndex index;
    DeviceAccessWatch watch;
    Code code;

    /// The register each select role holds when the watch starts (the devices' answer); -1 = not known
    int ayRegister = -1;
    int nextRegister = -1;

    explicit Rig(const slots::PortOwners& owners, std::vector<uint8_t> bytes = {}, int ay = -1, int next = -1)
        : ayRegister(ay), nextRegister(next)
    {
        code.bytes = std::move(bytes);
        index.Build(owners);
        watch.Build(index, &Code::Read, &code, &Rig::Selected, this);
        watch.Start();
    }
    static int Selected(const void* context, InputRole role)
    {
        const Rig* rig = static_cast<const Rig*>(context);
        return role == InputRole::AyIo ? rig->ayRegister : role == InputRole::NextReg ? rig->nextRegister : -1;
    }
    void In(uint16_t port, bool dos = false, uint16_t pc = 0x9000)
    {
        watch.Record(port, 0xFF, false, dos, pc);
    }
    void Out(uint16_t port, uint8_t value, uint16_t pc = 0x9000)
    {
        watch.Record(port, value, true, false, pc);
    }
};

/// Every channel, one per line: the failure message
std::string Dump(const DeviceAccessEvidence& evidence)
{
    std::string text;
    for (const DeviceAccessChannel& c : evidence.channels)
        text += c.device + " " + c.channel + (c.out ? " out" : " in") + " index " + std::to_string(c.index) +
                " frames " + std::to_string(c.frames) + " per frame " + std::to_string(c.perFrame) + "\n";
    return text;
}

const DeviceAccessChannel* Named(const DeviceAccessEvidence& evidence, const std::string& device,
                                 const std::string& channel, bool out = false)
{
    for (const DeviceAccessChannel& c : evidence.channels)
        if (c.device == device && c.channel == channel && c.out == out)
            return &c;
    return nullptr;
}
}  // namespace

/// Pentagon: the claims split #1F between the Kempston joystick and the Beta-128 under DOS; the ULA's half-rows are
/// channels by their address line; the AY's data reads are the selected register; an unclaimed port is the board's
TEST(DeviceAccessWatch_Test, PentagonAttributesByTheClaims)
{
    Rig rig(Owners(MM_PENTAGON));
    rig.In(0x001F);
    rig.In(0x001F);
    rig.In(0x001F, true);  // TR-DOS paged: the WD1793's status register, no joystick
    rig.In(0xEFFE);
    rig.In(0x00FE);        // every half-row at once
    rig.In(0xFFFE);        // no half-row: an EAR read
    rig.Out(0xFFFD, 14);
    rig.In(0xFFFD);
    rig.Out(0xFFFD, 7);
    rig.In(0xFFFD);
    rig.In(0x7FFD);
    rig.watch.EndFrame();
    const DeviceAccessEvidence evidence = rig.watch.Evidence();
    EXPECT_TRUE(evidence.active);
    EXPECT_EQ(evidence.window, 1u);

    const DeviceAccessChannel* kempston = Named(evidence, "kempston-joystick", "1f");
    ASSERT_NE(kempston, nullptr);
    EXPECT_EQ(kempston->input, InputRole::KempstonJoystick);
    EXPECT_EQ(kempston->perFrame, 2.0f) << "the DOS read is not the joystick's";
    const DeviceAccessChannel* beta = Named(evidence, "beta128", "1f");
    ASSERT_NE(beta, nullptr);
    EXPECT_EQ(beta->input, InputRole::None);

    const DeviceAccessChannel* row = Named(evidence, "ula", "row.ef");
    ASSERT_NE(row, nullptr);
    EXPECT_EQ(row->index, 12) << "A12 low";
    EXPECT_EQ(evidence.Find(InputRole::KeyMatrix, 12), row);
    EXPECT_NE(Named(evidence, "ula", "row.multi"), nullptr);
    EXPECT_NE(Named(evidence, "ula", "row.none"), nullptr);

    const DeviceAccessChannel* r14 = Named(evidence, "ay", "reg.0e");
    ASSERT_NE(r14, nullptr);
    EXPECT_EQ(r14->input, InputRole::AyIo);
    EXPECT_EQ(evidence.Find(InputRole::AyIo, 14), r14);
    EXPECT_NE(Named(evidence, "ay", "reg.07"), nullptr);
    const DeviceAccessChannel* select = Named(evidence, "ay", "reg.select", true);
    ASSERT_NE(select, nullptr);
    EXPECT_EQ(select->perFrame, 2.0f);

    const DeviceAccessChannel* board = Named(evidence, "board", "7ffd");
    ASSERT_NE(board, nullptr) << "a port no device claims is kept by its own address";
    EXPECT_EQ(board->input, InputRole::None);
}

/// The Next: #1F written is its DAC, read is the left connector; #37 the right one; NR #B2 is a channel of the
/// NextREG device, chosen by the write to #243B
TEST(DeviceAccessWatch_Test, NextDacJoysticksAndRegisters)
{
    Rig rig(Owners(MM_NEXT));
    rig.Out(0x001F, 0x80);
    rig.In(0x001F);
    rig.In(0x0037);
    rig.Out(0x243B, 0xB2);
    rig.In(0x253B);
    rig.watch.EndFrame();
    const DeviceAccessEvidence evidence = rig.watch.Evidence();
    const DeviceAccessChannel* dac = Named(evidence, "dac", "1f", true);
    ASSERT_NE(dac, nullptr);
    EXPECT_EQ(dac->input, InputRole::None) << "a DAC write is no joystick";
    EXPECT_EQ(evidence.Find(InputRole::KempstonJoystick, DeviceAccessChannel::kNoIndex, true), nullptr);
    ASSERT_NE(evidence.Find(InputRole::KempstonJoystick), nullptr);
    ASSERT_NE(evidence.Find(InputRole::KempstonJoystick2), nullptr);
    const DeviceAccessChannel* b2 = evidence.Find(InputRole::NextReg, 0xB2);
    ASSERT_NE(b2, nullptr);
    EXPECT_EQ(b2->device, "nextreg");
    EXPECT_EQ(b2->channel, "reg.b2");
}

/// The Scorpion's keyboard decode needs A1 and A5 high: #EFDE is no half-row there, on a Pentagon (A0 low) it is
TEST(DeviceAccessWatch_Test, KeyboardFollowsTheBoardsDecode)
{
    Rig scorpion(Owners(MM_PROFSCORP));
    scorpion.In(0xEFFE);
    scorpion.In(0xEFDE);
    scorpion.watch.EndFrame();
    const DeviceAccessEvidence onScorpion = scorpion.watch.Evidence();
    const DeviceAccessChannel* row = onScorpion.Find(InputRole::KeyMatrix, 12);
    ASSERT_NE(row, nullptr);
    EXPECT_EQ(row->perFrame, 1.0f) << "#EFDE is not the Scorpion's keyboard\n" << Dump(onScorpion);

    Rig pentagon(Owners(MM_PENTAGON));
    pentagon.In(0xEFFE);
    pentagon.In(0xEFDE);
    pentagon.watch.EndFrame();
    const DeviceAccessEvidence onPentagon = pentagon.watch.Evidence();
    row = onPentagon.Find(InputRole::KeyMatrix, 12);
    ASSERT_NE(row, nullptr);
    EXPECT_EQ(row->perFrame, 2.0f) << Dump(onPentagon);
}

/// An AMX mouse card on a 48K: its PIO answers #1F, so a read there is the mouse, not a Kempston joystick
TEST(DeviceAccessWatch_Test, CardClaimsMakeTheRole)
{
    Rig rig(Owners(MM_SPECTRUM48, { { "edge.1", "amx-mouse" } }));
    rig.In(0x001F);
    rig.watch.EndFrame();
    const DeviceAccessEvidence evidence = rig.watch.Evidence();
    EXPECT_EQ(evidence.Find(InputRole::KempstonJoystick), nullptr);
    const DeviceAccessChannel* amx = evidence.Find(InputRole::AmxMouse);
    ASSERT_NE(amx, nullptr) << Dump(evidence);
    EXPECT_EQ(amx->device, "edge.1");
}

/// The code sites of a channel and the bits the code tests after an input read: one scan per site
TEST(DeviceAccessWatch_Test, SitesAndTestedBits)
{
    // 0x8000: IN A,(#FE); AND #1F
    Rig rig(Owners(MM_PENTAGON), { 0xDB, 0xFE, 0xE6, 0x1F });
    for (int i = 0; i < 3; ++i)
        rig.In(0xEFFE, false, 0x8000);
    for (uint16_t site = 0; site < DeviceAccessWatch::kSites + 2; ++site)
        rig.In(0x001F, false, static_cast<uint16_t>(0xA000 + site * 4));
    rig.watch.EndFrame();
    const DeviceAccessEvidence evidence = rig.watch.Evidence();
    const DeviceAccessChannel* row = evidence.Find(InputRole::KeyMatrix, 12);
    ASSERT_NE(row, nullptr);
    EXPECT_EQ(row->tested, 0x1F);
    ASSERT_EQ(row->sites.size(), 1u);
    EXPECT_EQ(row->sites[0].pc, 0x8000);
    EXPECT_EQ(row->sites[0].count, 3u);
    const DeviceAccessChannel* kempston = evidence.Find(InputRole::KempstonJoystick);
    ASSERT_NE(kempston, nullptr);
    EXPECT_EQ(kempston->sites.size(), DeviceAccessWatch::kSites);
    EXPECT_EQ(kempston->otherSites, 2u);
    EXPECT_EQ(kempston->tested, 0) << "nothing tests the byte at those sites";
}

/// The window slides; a requested window starts at the next boundary, restarts while the media loads and closes
/// itself; the continuous watch keeps counting
TEST(DeviceAccessWatch_Test, WindowAndRequests)
{
    Rig rig(Owners(MM_PENTAGON));
    rig.In(0x001F);
    rig.watch.EndFrame();
    for (uint32_t frame = 0; frame < DeviceAccessWatch::kWindow + 10; ++frame)
        rig.watch.EndFrame();
    DeviceAccessEvidence evidence = rig.watch.Evidence();
    EXPECT_EQ(evidence.framesObserved, DeviceAccessWatch::kWindow + 11);
    EXPECT_EQ(evidence.window, DeviceAccessWatch::kWindow);
    ASSERT_NE(evidence.Find(InputRole::KempstonJoystick), nullptr) << "seen since the start";
    EXPECT_EQ(evidence.Find(InputRole::KempstonJoystick)->frames, 0u) << "the first frame left the window";

    rig.watch.Stop();
    rig.watch.RequestWindow(3);
    rig.watch.OnFrameBoundary(false);  // starts
    EXPECT_TRUE(rig.watch.On());
    rig.In(0x001F);
    rig.watch.OnFrameBoundary(true);   // the tape still plays: from zero again
    EXPECT_EQ(rig.watch.Evidence().framesObserved, 0u);
    for (int frame = 0; frame < 3; ++frame)
    {
        rig.In(0x001F);
        rig.watch.OnFrameBoundary(false);
    }
    evidence = rig.watch.Evidence();
    EXPECT_EQ(evidence.framesObserved, 3u);
    EXPECT_EQ(evidence.Find(InputRole::KempstonJoystick)->frames, 3u);
    rig.watch.OnFrameBoundary(false);  // the window is over
    EXPECT_FALSE(rig.watch.On());
    EXPECT_FALSE(rig.watch.Evidence().active);
    EXPECT_EQ(rig.watch.Evidence().framesObserved, 3u) << "the evidence stays readable";

    rig.watch.SetContinuous(true);
    rig.watch.OnFrameBoundary(false);
    rig.watch.OnFrameBoundary(false);
    rig.watch.OnFrameBoundary(false);
    EXPECT_TRUE(rig.watch.On());
    EXPECT_EQ(rig.watch.Evidence().framesObserved, 2u);
    rig.watch.SetContinuous(false);
    rig.watch.OnFrameBoundary(false);
    EXPECT_FALSE(rig.watch.On());
}

/// Every machine's declaration gives the keyboard and, where it has one, the AY its channels: no machine falls back
/// to the board for them
TEST(DeviceAccessWatch_Test, EveryMachineDeclaresItsKeyboardAndAy)
{
    for (const slots::MachineDef& machine : slots::refdata::Machines())
    {
        Rig rig(Owners(machine.model));
        rig.In(0xEFFE);
        rig.Out(0xFFFD, 14);
        rig.In(0xFFFD);
        rig.watch.EndFrame();
        const DeviceAccessEvidence evidence = rig.watch.Evidence();
        EXPECT_NE(evidence.Find(InputRole::KeyMatrix, 12), nullptr) << machine.name;
        bool hasAy = false;
        for (const slots::BuiltInDef& def : machine.builtIns)
            hasAy = hasAy || std::string(def.id) == "ay";
        if (hasAy)
            EXPECT_NE(evidence.Find(InputRole::AyIo, 14), nullptr) << machine.name;
    }
}

/// The Kempston mouse the emulator fits on every machine by `[INPUT] Mouse=`: the standard decode (A9 high, #xxDF)
/// on the Sinclair, Pentagon, Scorpion and Profi families, the exact low byte #DF on the ATMs. The low byte #1F stays
/// the joystick's even with A9 high (`IN A,(#1F)` with A = #FF): the decoder checks the joystick first
TEST(DeviceAccessWatch_Test, MouseByEachBoardsDecode)
{
    for (MEM_MODEL model : { MM_SPECTRUM48, MM_SPECTRUM128, MM_PLUS3, MM_PENTAGON, MM_PROFSCORP, MM_PROFI, MM_ATM710 })
    {
        Rig rig(Owners(model));
        rig.In(0xFBDF);
        rig.In(0xFFDF);
        rig.In(0xFADF);
        rig.In(0xFF1F);
        rig.watch.EndFrame();
        const DeviceAccessEvidence evidence = rig.watch.Evidence();
        const DeviceAccessChannel* mouse = evidence.Find(InputRole::KempstonMouse);
        ASSERT_NE(mouse, nullptr) << model << "\n" << Dump(evidence);
        EXPECT_EQ(mouse->device, "kempston-mouse") << model;
        EXPECT_EQ(mouse->perFrame, 3.0f) << model << "\n" << Dump(evidence);
        const DeviceAccessChannel* joystick = evidence.Find(InputRole::KempstonJoystick);
        if (model == MM_PENTAGON || model == MM_PROFSCORP)
        {
            ASSERT_NE(joystick, nullptr) << model << "\n" << Dump(evidence);
            EXPECT_EQ(joystick->perFrame, 1.0f) << "#FF1F is the joystick, #xxDF never\n" << Dump(evidence);
        }
    }
}

/// An AY's register select: a value with bits above the register number selects no register (the TurboSound's chip
/// select #FE / #FF, the TSFM's FM addresses) and keeps the last one; the select itself is still counted
TEST(DeviceAccessWatch_Test, ChipSelectKeepsTheRegister)
{
    Rig rig(Owners(MM_PENTAGON));
    rig.Out(0xFFFD, 14);
    rig.Out(0xFFFD, 0xFE);  // chip 1
    rig.In(0xFFFD);
    rig.Out(0xFFFD, 0x2B);  // an FM address
    rig.In(0xFFFD);
    rig.watch.EndFrame();
    const DeviceAccessEvidence evidence = rig.watch.Evidence();
    const DeviceAccessChannel* r14 = evidence.Find(InputRole::AyIo, 14);
    ASSERT_NE(r14, nullptr) << Dump(evidence);
    EXPECT_EQ(r14->perFrame, 2.0f) << Dump(evidence);
    EXPECT_EQ(Named(evidence, "ay", "reg.fe"), nullptr) << Dump(evidence);
    const DeviceAccessChannel* select = Named(evidence, "ay", "reg.select", true);
    ASSERT_NE(select, nullptr);
    EXPECT_EQ(select->perFrame, 3.0f);
}

/// The selects written while the watch was off: the devices' state when it starts. Without one the reads are
/// `reg.unknown`, never another register; a write after the start wins
TEST(DeviceAccessWatch_Test, SelectionSeededAtTheStart)
{
    Rig unknown(Owners(MM_PENTAGON));
    unknown.In(0xFFFD);
    unknown.watch.EndFrame();
    DeviceAccessEvidence evidence = unknown.watch.Evidence();
    EXPECT_NE(Named(evidence, "ay", "reg.unknown"), nullptr) << Dump(evidence);
    EXPECT_EQ(evidence.Find(InputRole::AyIo, 0), nullptr) << Dump(evidence);

    Rig seeded(Owners(MM_PENTAGON), {}, 14);
    seeded.In(0xFFFD);
    seeded.Out(0xFFFD, 7);
    seeded.In(0xFFFD);
    seeded.watch.EndFrame();
    evidence = seeded.watch.Evidence();
    EXPECT_NE(evidence.Find(InputRole::AyIo, 14), nullptr) << Dump(evidence);
    EXPECT_NE(evidence.Find(InputRole::AyIo, 7), nullptr) << Dump(evidence);

    // Every start asks again: the program selected R15 meanwhile
    seeded.ayRegister = 15;
    seeded.watch.Start();
    seeded.In(0xFFFD);
    seeded.watch.EndFrame();
    evidence = seeded.watch.Evidence();
    EXPECT_NE(evidence.Find(InputRole::AyIo, 15), nullptr) << Dump(evidence);

    Rig next(Owners(MM_NEXT), {}, -1, 0xB2);
    next.In(0x253B);
    next.watch.EndFrame();
    EXPECT_NE(next.watch.Evidence().Find(InputRole::NextReg, 0xB2), nullptr) << Dump(next.watch.Evidence());
}
