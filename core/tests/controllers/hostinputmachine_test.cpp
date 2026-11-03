/// @file hostinputmachine_test.cpp
/// @brief The controllers library on a real machine: a fake pad through HostInputService reaches the Pentagon's
/// Kempston port at the frame boundary, and a release releases it (C2a, architecture.md §7); a key target reaches
/// the matrix through the host key overlay, and the library's ZX key names agree with the core's (C2b); the port-read
/// watch counts the guest's INs from the next frame boundary and stops when told, with the row bits of the code
/// after a half-row IN, the AY's R14 reads and the Next's NR #B2 reads (C6).

#include <gtest/gtest.h>

#include <memory>
#include <vector>
#include <string>

#include "_helpers/emulatortesthelper.h"
#include "_helpers/soundcardscope.h"
#include "debugger/keyboard/debugkeyboardmanager.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/memory/memory.h"
#include "emulator/io/controllers/controllermanager.h"
#include "emulator/io/joystick/joystick.h"
#include "emulator/io/keyboard/keyboard.h"
#include "fake/fakebackend.h"
#include "emulator/ports/models/portdecoder_sprinter.h"
#include "emulator/slots/refdata/refdata.h"
#include "emulator/slots/slottypes.h"
#include "hostinputservice.h"
#include "router/zxkeys.h"

using namespace controllers;

// Boot-bound (~20 ms): one Pentagon, three frames
TEST(HostInputMachine_Test, FakePadDrivesTheKempstonPort)
{
    Emulator* emulator = EmulatorTestHelper::CreateStandardEmulator("PENTAGON", LoggerLevel::LogError);
    ASSERT_NE(emulator, nullptr);
    EmulatorContext* context = emulator->GetContext();

    HostInputService service;
    auto fake = std::make_unique<FakeBackend>();
    FakeBackend* pads = fake.get();
    std::string error;
    ASSERT_TRUE(service.AddBackend(std::move(fake), error)) << error;
    ASSERT_TRUE(pads->Connect({"fake:1", "Pad", "xbox", {}}));

    Binding fire;
    fire.sources = {"south"};
    ASSERT_TRUE(ParseRetroTarget("kempston:fire", fire.target, error)) << error;
    Binding stick;
    stick.sources = {"lstick"};
    ASSERT_TRUE(ParseRetroTarget("kempston:dpad", stick.target, error)) << error;
    ASSERT_TRUE(service.Router().SetBindings({}, {fire, stick}, error)) << error;

    context->pControllerManager->SetHostInputSource(service.SourceFor("emu-1"));
    pads->Press("fake:1", "south");
    pads->Set("fake:1", "lstick.y", -1.0f);
    emulator->RunNFrames(1);
    EXPECT_EQ(context->pJoystick->State(), kFire | kUp) << "sampled at the frame boundary";

    pads->Release("fake:1", "south");
    pads->Set("fake:1", "lstick.y", 0.0f);
    emulator->RunNFrames(1);
    EXPECT_EQ(context->pJoystick->State(), 0);

    context->pControllerManager->SetHostInputSource(nullptr);
    EmulatorTestHelper::CleanupEmulator(emulator);
}

// Boot-bound (~20 ms): one 48K, two frames
TEST(HostInputMachine_Test, KeyTargetReachesTheMatrixAndKeyNamesAgree)
{
    Emulator* emulator = EmulatorTestHelper::CreateStandardEmulator("48K", LoggerLevel::LogError);
    ASSERT_NE(emulator, nullptr);
    EmulatorContext* context = emulator->GetContext();

    HostInputService service;
    auto fake = std::make_unique<FakeBackend>();
    FakeBackend* pads = fake.get();
    std::string error;
    ASSERT_TRUE(service.AddBackend(std::move(fake), error)) << error;
    ASSERT_TRUE(pads->Connect({"fake:1", "Pad", "xbox", {}}));
    Binding edit;
    edit.sources = {"north"};
    ASSERT_TRUE(ParseKeysTarget("EDIT", edit.target, error)) << error;
    ASSERT_TRUE(service.Router().SetBindings({}, {edit}, error)) << error;

    context->pControllerManager->SetHostInputSource(service.SourceFor("emu-1"));
    pads->Press("fake:1", "north");
    emulator->RunNFrames(1);
    EXPECT_EQ(context->pKeyboard->HandlePortIn(0xFEFE) & 0x01, 0) << "CAPS SHIFT";
    EXPECT_EQ(context->pKeyboard->HandlePortIn(0xF7FE) & 0x01, 0) << "1";
    pads->Release("fake:1", "north");
    emulator->RunNFrames(1);
    EXPECT_EQ(context->pKeyboard->HandlePortIn(0xFEFE) & 0x1F, 0x1F);
    EXPECT_EQ(context->pKeyboard->HandlePortIn(0xF7FE) & 0x1F, 0x1F);

    context->pControllerManager->SetHostInputSource(nullptr);

    // zxkeys.h carries ZXKeysEnum values without the keyboard header: every name means what ResolveKeyName says
    for (const std::string& name : KnownZxKeyNames())
    {
        std::vector<uint8_t> keys;
        ASSERT_TRUE(ZxKeysFromName(name, keys)) << name;
        const ZXKeysEnum core = DebugKeyboardManager::ResolveKeyName(name);
        ASSERT_NE(core, ZXKEY_NONE) << name;
        if (core < ZXKEY_EXT_CTRL)
        {
            EXPECT_EQ(keys, std::vector<uint8_t>{static_cast<uint8_t>(core)}) << name;
            continue;
        }
        ASSERT_EQ(keys.size(), 2u) << name;
        EXPECT_TRUE(keys[0] == ZXKEY_CAPS_SHIFT || keys[0] == ZXKEY_SYM_SHIFT) << name;
        EXPECT_EQ(keys[1], context->pKeyboard->GetMatrixKey(core)) << name;
    }

    EmulatorTestHelper::CleanupEmulator(emulator);
}

// Boot-bound (~20 ms): one Sprinter, two frames
TEST(HostInputMachine_Test, KeyTargetReachesTheSprinterPs2Keyboard)
{
    Emulator* emulator = EmulatorTestHelper::CreateStandardEmulator("SPRINTER", LoggerLevel::LogError);
    ASSERT_NE(emulator, nullptr);
    EmulatorContext* context = emulator->GetContext();
    auto* decoder = dynamic_cast<PortDecoder_Sprinter*>(context->pPortDecoder);
    ASSERT_NE(decoder, nullptr);

    HostInputService service;
    auto fake = std::make_unique<FakeBackend>();
    FakeBackend* pads = fake.get();
    std::string error;
    ASSERT_TRUE(service.AddBackend(std::move(fake), error)) << error;
    ASSERT_TRUE(pads->Connect({"fake:1", "Pad", "xbox", {}}));
    Binding start;
    start.sources = {"menu"};
    ASSERT_TRUE(ParseKeysTarget("ENTER", start.target, error)) << error;
    Binding up;
    up.sources = {"dpad.up"};
    ASSERT_TRUE(ParseKeysTarget("UP", up.target, error)) << error;
    ASSERT_TRUE(service.Router().SetBindings({}, {start, up}, error)) << error;

    context->pControllerManager->SetHostInputSource(service.SourceFor("emu-1"));
    pads->Press("fake:1", "menu");
    emulator->RunNFrames(1);
    const Ps2KeyboardStream& stream = decoder->GetInput().KeyboardStream();
    EXPECT_TRUE(stream.IsHeld(PcKey::Enter)) << "a pad button with a key target is a PS/2 key on the Sprinter";
    pads->Release("fake:1", "menu");
    pads->Press("fake:1", "dpad.up");
    emulator->RunNFrames(1);
    EXPECT_FALSE(stream.IsHeld(PcKey::Enter));
    EXPECT_TRUE(stream.IsHeld(PcKey::Up)) << "a cursor key is the arrow, not Caps Shift and 7";
    pads->Release("fake:1", "dpad.up");
    emulator->RunNFrames(1);
    EXPECT_FALSE(stream.IsHeld(PcKey::Up));

    context->pControllerManager->SetHostInputSource(nullptr);
    EmulatorTestHelper::CleanupEmulator(emulator);
}

// Boot-bound (~15 ms): one Pentagon, four frames
TEST(HostInputMachine_Test, AccessWatchCountsTheGuestsReads)
{
    Emulator* emulator = EmulatorTestHelper::CreateStandardEmulator("PENTAGON", LoggerLevel::LogError);
    ASSERT_NE(emulator, nullptr);
    EmulatorContext* context = emulator->GetContext();
    // DI; loop: IN A,(#1F); LD A,#EF; IN A,(#FE); JR loop
    const uint8_t program[] = {0xF3, 0xDB, 0x1F, 0x3E, 0xEF, 0xDB, 0xFE, 0x18, 0xF8};
    for (size_t i = 0; i < sizeof(program); ++i)
        context->pMemory->DirectWriteToZ80Memory(static_cast<uint16_t>(0x8000 + i), program[i]);
    context->pCore->GetZ80()->pc = 0x8000;

    ControllerManager* manager = context->pControllerManager;
    EXPECT_FALSE(manager->PortReads().active);
    manager->WatchPortReads(true);
    emulator->RunNFrames(3);  // the first boundary starts the watch, the next two fold a frame each
    DeviceAccessEvidence evidence = manager->PortReads();
    EXPECT_TRUE(evidence.active);
    EXPECT_GE(evidence.framesObserved, 1u);
    const DeviceAccessChannel* kempston = evidence.Find(slots::InputRole::KempstonJoystick);
    ASSERT_NE(kempston, nullptr);
    EXPECT_EQ(kempston->device, "kempston-joystick");
    EXPECT_EQ(kempston->frames, evidence.window);
    EXPECT_GT(kempston->perFrame, 1000.0f) << "a tight loop: thousands of reads per frame";
    ASSERT_FALSE(kempston->sites.empty());
    EXPECT_EQ(kempston->sites[0].pc, 0x8001);
    const DeviceAccessChannel* row = evidence.Find(slots::InputRole::KeyMatrix, 12);
    ASSERT_NE(row, nullptr) << "#EFFE";
    EXPECT_EQ(row->device, "ula");
    EXPECT_EQ(row->frames, evidence.window);
    EXPECT_EQ(evidence.Find(slots::InputRole::KeyMatrix, 8), nullptr);

    manager->WatchPortReads(false);
    emulator->RunNFrames(1);
    EXPECT_FALSE(manager->PortReads().active);
    EmulatorTestHelper::CleanupEmulator(emulator);
}

namespace
{
/// The program at 0x8000, DI first, and the PC there
void RunAt8000(EmulatorContext* context, const std::vector<uint8_t>& program)
{
    for (size_t i = 0; i < program.size(); ++i)
        context->pMemory->DirectWriteToZ80Memory(static_cast<uint16_t>(0x8000 + i), program[i]);
    context->pCore->GetZ80()->pc = 0x8000;
}
}  // namespace

// Boot-bound (~20 ms): one Pentagon, three frames
TEST(HostInputMachine_Test, AccessWatchRowBitsAndAyR14)
{
    SoundCardScope sound(TestSound::TurboSound);  // the AY on #FFFD: the runner's policy leaves the slot empty
    Emulator* emulator = EmulatorTestHelper::CreateStandardEmulator("PENTAGON", LoggerLevel::LogError);
    ASSERT_NE(emulator, nullptr);
    EmulatorContext* context = emulator->GetContext();
    // DI; loop: LD BC,#EFFE; IN A,(C); RRA; JR NC,+0; RRA; JR NC,+0;
    //           LD BC,#FFFD; LD A,14; OUT (C),A; IN A,(C); JR loop
    RunAt8000(context, {0xF3, 0x01, 0xFE, 0xEF, 0xED, 0x78, 0x1F, 0x30, 0x00, 0x1F, 0x30, 0x00,
                        0x01, 0xFD, 0xFF, 0x3E, 0x0E, 0xED, 0x79, 0xED, 0x78, 0x18, 0xEA});
    ControllerManager* manager = context->pControllerManager;
    manager->WatchPortReads(true);
    emulator->RunNFrames(3);
    const DeviceAccessEvidence evidence = manager->PortReads();
    EXPECT_GT(evidence.window, 0u);
    const DeviceAccessChannel* row = evidence.Find(slots::InputRole::KeyMatrix, 12);
    ASSERT_NE(row, nullptr);
    EXPECT_EQ(row->frames, evidence.window);
    EXPECT_EQ(row->tested, 0x03) << "two shifts into a carry test: bits 0 and 1";
    const DeviceAccessChannel* r14 = evidence.Find(slots::InputRole::AyIo, 14);
    ASSERT_NE(r14, nullptr) << "IN #FFFD with R14 selected";
    EXPECT_EQ(r14->frames, evidence.window);
    EXPECT_EQ(r14->channel, "reg.0e");

    manager->WatchPortReads(false);
    emulator->RunNFrames(1);
    EXPECT_FALSE(manager->PortReads().active);
    EmulatorTestHelper::CleanupEmulator(emulator);
}

// Boot-bound (~30 ms): one Next, three frames
TEST(HostInputMachine_Test, AccessWatchNextB2)
{
    Emulator* emulator = EmulatorTestHelper::CreateStandardEmulator("NEXT", LoggerLevel::LogError);
    ASSERT_NE(emulator, nullptr);
    EmulatorContext* context = emulator->GetContext();
    // DI; loop: LD BC,#243B; LD A,#B2; OUT (C),A; INC B; IN A,(C); LD A,#B1; DEC B; OUT (C),A; INC B; IN A,(C);
    //           JR loop (NR #B1 is no evidence)
    RunAt8000(context, {0xF3, 0x01, 0x3B, 0x24, 0x3E, 0xB2, 0xED, 0x79, 0x04, 0xED, 0x78, 0x3E, 0xB1, 0x05,
                        0xED, 0x79, 0x04, 0xED, 0x78, 0x18, 0xEC});
    ControllerManager* manager = context->pControllerManager;
    manager->WatchPortReads(true);
    emulator->RunNFrames(3);
    const DeviceAccessEvidence evidence = manager->PortReads();
    EXPECT_GT(evidence.window, 0u);
    const DeviceAccessChannel* b2 = evidence.Find(slots::InputRole::NextReg, 0xB2);
    ASSERT_NE(b2, nullptr);
    EXPECT_EQ(b2->frames, evidence.window);
    EXPECT_GT(b2->perFrame, 100.0f);
    const DeviceAccessChannel* b1 = evidence.Find(slots::InputRole::NextReg, 0xB1);
    ASSERT_NE(b1, nullptr) << "every register is its own channel";
    EXPECT_EQ(evidence.Find(slots::InputRole::AyIo, 14), nullptr);
    EmulatorTestHelper::CleanupEmulator(emulator);
}

// Boot-bound (~25 ms): one Pentagon with an AY, five frames. The program selects R14 before the watch starts and only
// reads afterwards: the watch takes the selection from the chip when it starts
TEST(HostInputMachine_Test, AccessWatchSeedsTheAySelection)
{
    SoundCardScope sound(TestSound::TurboSound);
    Emulator* emulator = EmulatorTestHelper::CreateStandardEmulator("PENTAGON", LoggerLevel::LogError);
    ASSERT_NE(emulator, nullptr);
    EmulatorContext* context = emulator->GetContext();
    // DI; LD BC,#FFFD; LD A,14; OUT (C),A; loop: IN A,(C); JR loop
    RunAt8000(context, {0xF3, 0x01, 0xFD, 0xFF, 0x3E, 0x0E, 0xED, 0x79, 0xED, 0x78, 0x18, 0xFC});
    emulator->RunNFrames(2);  // the select runs with the watch off
    ControllerManager* manager = context->pControllerManager;
    manager->WatchPortReads(true);
    emulator->RunNFrames(3);
    const DeviceAccessEvidence evidence = manager->PortReads();
    EXPECT_GT(evidence.window, 0u);
    const DeviceAccessChannel* r14 = evidence.Find(slots::InputRole::AyIo, 14);
    ASSERT_NE(r14, nullptr) << "the seed: R14 as the chip holds it";
    EXPECT_EQ(r14->frames, evidence.window);
    EXPECT_EQ(evidence.Find(slots::InputRole::AyIo, DeviceAccessChannel::kNoIndex, true), nullptr)
        << "no select while the watch ran";
    EmulatorTestHelper::CleanupEmulator(emulator);
}

// Boot-bound (~0.4 s): every machine of the slot registry, three frames each. A half-row read reaches the watch
// through each model's own decoder: the hook is the decoder's IN completion, which every model must call
TEST(HostInputMachine_Test, AccessWatchSeesTheKeyboardOnEveryMachine)
{
    for (const slots::MachineDef& machine : slots::refdata::Machines())
    {
        SCOPED_TRACE(machine.name);
        Emulator* emulator = EmulatorTestHelper::CreateStandardEmulator(machine.name, LoggerLevel::LogError);
        ASSERT_NE(emulator, nullptr);
        EmulatorContext* context = emulator->GetContext();
        // DI; loop: LD BC,#EFFE; IN A,(C); OUT (C),A; JR loop
        RunAt8000(context, {0xF3, 0x01, 0xFE, 0xEF, 0xED, 0x78, 0xED, 0x79, 0x18, 0xF7});
        ControllerManager* manager = context->pControllerManager;
        manager->WatchPortReads(true);
        emulator->RunNFrames(3);
        const DeviceAccessEvidence evidence = manager->PortReads();
        const DeviceAccessChannel* row = evidence.Find(slots::InputRole::KeyMatrix, 12);
        ASSERT_NE(row, nullptr) << "IN #EFFE did not reach the watch";
        EXPECT_EQ(row->frames, evidence.window);
        EXPECT_NE(evidence.Find(slots::InputRole::KeyMatrix, DeviceAccessChannel::kNoIndex, true), nullptr)
            << "OUT #EFFE did not reach the watch";
        EmulatorTestHelper::CleanupEmulator(emulator);
    }
}
