/// @file hostinputmachine_test.cpp
/// @brief The controllers library on a real machine (C2a): a fake pad through HostInputService reaches the
/// Pentagon's Kempston port at the frame boundary, and a release releases it (architecture.md §7).

#include <gtest/gtest.h>

#include <memory>
#include <string>

#include "_helpers/emulatortesthelper.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/io/controllers/controllermanager.h"
#include "emulator/io/joystick/joystick.h"
#include "fake/fakebackend.h"
#include "hostinputservice.h"

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
