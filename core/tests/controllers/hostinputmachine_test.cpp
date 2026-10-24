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

// Boot-bound (~15 ms): one Pentagon, four frames
TEST(HostInputMachine_Test, PortReadWatchCountsTheGuestsReads)
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
    PortReadEvidence evidence = manager->PortReads();
    EXPECT_TRUE(evidence.active);
    EXPECT_GE(evidence.framesObserved, 1u);
    EXPECT_EQ(evidence.Frames(PortReadClass::Kempston), evidence.window);
    EXPECT_GT(evidence.Reads(PortReadClass::Kempston), 1000.0f) << "a tight loop: thousands of reads per frame";
    EXPECT_EQ(evidence.Frames(PortReadClass::Row4), evidence.window) << "#EFFE";
    EXPECT_EQ(evidence.Frames(PortReadClass::Row0), 0u);

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
TEST(HostInputMachine_Test, PortReadWatchRowBitsAndAyR14)
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
    const PortReadEvidence evidence = manager->PortReads();
    EXPECT_EQ(evidence.Frames(PortReadClass::Row4), evidence.window);
    EXPECT_EQ(evidence.RowBits(PortReadClass::Row4), 0x03) << "two shifts into a carry test: bits 0 and 1";
    EXPECT_EQ(evidence.Frames(PortReadClass::AyR14), evidence.window) << "IN #FFFD with R14 selected";
    EXPECT_GT(evidence.window, 0u);

    manager->WatchPortReads(false);
    emulator->RunNFrames(1);
    EXPECT_FALSE(manager->PortReads().active);
    EmulatorTestHelper::CleanupEmulator(emulator);
}

// Boot-bound (~30 ms): one Next, three frames
TEST(HostInputMachine_Test, PortReadWatchNextB2)
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
    const PortReadEvidence evidence = manager->PortReads();
    EXPECT_GT(evidence.window, 0u);
    EXPECT_EQ(evidence.Frames(PortReadClass::NextB2), evidence.window);
    EXPECT_GT(evidence.Reads(PortReadClass::NextB2), 100.0f);
    EXPECT_EQ(evidence.Frames(PortReadClass::AyR14), 0u);
    EmulatorTestHelper::CleanupEmulator(emulator);
}
