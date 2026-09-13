#include "stdafx.h"
#include "gtest/gtest.h"

#include "_helpers/testwaithelper.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/emulatormanager.h"
#include "emulator/io/mouse/mouse.h"
#include "emulator/ports/portdecoder.h"

/// @brief End-to-end delivery of host mouse input into the emulated machine.
///
/// The desktop front end never touches the Mouse object: DeviceScreen posts
/// MC_MOUSE_MOVE / MC_MOUSE_BUTTON / MC_MOUSE_WHEEL to the default MessageCenter,
/// tagged with the target emulator's UUID. These tests post exactly those
/// messages to real emulator instances (created through EmulatorManager, the
/// same way the app creates them) and assert the result where the guest sees
/// it - IN from the Kempston Mouse ports through the model's port decoder.
///
/// MessageCenter dispatches on its own worker thread, one message at a time in
/// post order. "Was NOT applied" is therefore proven with a marker: post the
/// message that must be ignored, then a message that must be applied, wait for
/// the marker to land, and check the ignored one left no trace.
class MouseMessageCenter_Test : public ::testing::TestWithParam<const char*>
{
protected:
    static constexpr uint16_t PORT_BUTTONS = 0xFADF;
    static constexpr uint16_t PORT_X = 0xFBDF;
    static constexpr uint16_t PORT_Y = 0xFFDF;

    // Reset state (hardware-reference: non-equal axes so software detects presence)
    static constexpr uint8_t RESET_X = 31;
    static constexpr uint8_t RESET_Y = 85;

    void TearDown() override
    {
        EmulatorManager* manager = EmulatorManager::GetInstance();
        for (auto& emulator : _emulators)
        {
            if (emulator)
                manager->RemoveEmulator(emulator->GetId());
        }
        _emulators.clear();
    }

    std::shared_ptr<Emulator> CreateInstance()
    {
        std::shared_ptr<Emulator> emulator =
            EmulatorManager::GetInstance()->CreateEmulatorWithModel("", GetParam(), LoggerLevel::LogError);
        if (emulator)
            _emulators.push_back(emulator);
        return emulator;
    }

    static std::string TargetOf(const std::shared_ptr<Emulator>& emulator)
    {
        // The UUID string DeviceScreen tags events with
        return std::string(emulator->GetUUID());
    }

    static uint8_t In(const std::shared_ptr<Emulator>& emulator, uint16_t port)
    {
        return emulator->GetContext()->pPortDecoder->DecodePortIn(port, 0x0000);
    }

    static void Post(const char* topic, MouseEvent* event)
    {
        MessageCenter::DefaultMessageCenter().Post(topic, event);
    }

    std::vector<std::shared_ptr<Emulator>> _emulators;
};

/// The tag DeviceScreen uses and the id Mouse filters on must be the same string
TEST_P(MouseMessageCenter_Test, UuidTagMatchesEmulatorId)
{
    auto emulator = CreateInstance();
    ASSERT_TRUE(emulator);
    ASSERT_NE(emulator->GetContext()->pMouse, nullptr) << "Core::Init must create the Mouse device";

    EXPECT_EQ(TargetOf(emulator), emulator->GetId());
}

/// MC_MOUSE_MOVE with the emulator UUID moves the X/Y counters read at #FBDF / #FFDF
TEST_P(MouseMessageCenter_Test, MoveWithOwnUuidReachesAxisPorts)
{
    auto emulator = CreateInstance();
    ASSERT_TRUE(emulator);
    const std::string target = TargetOf(emulator);

    ASSERT_EQ(In(emulator, PORT_X), RESET_X);
    ASSERT_EQ(In(emulator, PORT_Y), RESET_Y);

    Post(MC_MOUSE_MOVE, MouseEvent::Move(5, -3, target));
    ASSERT_TRUE(TestWait::For([&] { return In(emulator, PORT_X) == RESET_X + 5; }))
        << "move event with the emulator UUID was not delivered";
    EXPECT_EQ(In(emulator, PORT_X), RESET_X + 5);
    EXPECT_EQ(In(emulator, PORT_Y), RESET_Y - 3);

    // Deltas accumulate across events
    Post(MC_MOUSE_MOVE, MouseEvent::Move(-10, 7, target));
    ASSERT_TRUE(TestWait::For([&] { return In(emulator, PORT_X) == RESET_X + 5 - 10; }));
    EXPECT_EQ(In(emulator, PORT_Y), RESET_Y - 3 + 7);
}

/// The axis counters are 8-bit and wrap, as on the hardware
TEST_P(MouseMessageCenter_Test, MoveWrapsAxisCountersAt8Bits)
{
    auto emulator = CreateInstance();
    ASSERT_TRUE(emulator);
    const std::string target = TargetOf(emulator);

    Post(MC_MOUSE_MOVE, MouseEvent::Move(250, -100, target));  // 31+250 = 281 -> 25; 85-100 = -15 -> 241
    ASSERT_TRUE(TestWait::For([&] { return In(emulator, PORT_X) == 25; }));
    EXPECT_EQ(In(emulator, PORT_Y), 241);
}

/// MC_MOUSE_BUTTON with the emulator UUID drives the active-low button bits at #FADF
TEST_P(MouseMessageCenter_Test, ButtonsWithOwnUuidReachButtonPort)
{
    auto emulator = CreateInstance();
    ASSERT_TRUE(emulator);
    const std::string target = TargetOf(emulator);

    ASSERT_EQ(In(emulator, PORT_BUTTONS) & 0x07, 0x07) << "all buttons released after reset";
    EXPECT_EQ(In(emulator, PORT_BUTTONS) & 0x08, 0x08) << "bit 3 is constant 1";

    // D0 = Left, D1 = Right, D2 = Middle, active low
    Post(MC_MOUSE_BUTTON, MouseEvent::Buttons(0xFE, target));  // left down
    ASSERT_TRUE(TestWait::For([&] { return (In(emulator, PORT_BUTTONS) & 0x07) == 0x06; }))
        << "button event with the emulator UUID was not delivered";

    Post(MC_MOUSE_BUTTON, MouseEvent::Buttons(0xF9, target));  // right + middle down
    ASSERT_TRUE(TestWait::For([&] { return (In(emulator, PORT_BUTTONS) & 0x07) == 0x01; }));

    Post(MC_MOUSE_BUTTON, MouseEvent::Buttons(0xFF, target));  // all released
    ASSERT_TRUE(TestWait::For([&] { return (In(emulator, PORT_BUTTONS) & 0x07) == 0x07; }));
}

/// MC_MOUSE_WHEEL with the emulator UUID accumulates in the upper nibble of #FADF (wheel fitted)
TEST_P(MouseMessageCenter_Test, WheelWithOwnUuidReachesButtonPortNibble)
{
    auto emulator = CreateInstance();
    ASSERT_TRUE(emulator);
    const std::string target = TargetOf(emulator);

    // The nibble is only visible on a wheel-equipped interface (default: classic, D7-D3 = 1)
    ASSERT_EQ(In(emulator, PORT_BUTTONS), 0xFF);
    emulator->GetContext()->pMouse->SetWheelEnabled(true);
    ASSERT_EQ(In(emulator, PORT_BUTTONS), 0x0F);

    Post(MC_MOUSE_WHEEL, MouseEvent::Wheel(2, target));
    ASSERT_TRUE(TestWait::For([&] { return (In(emulator, PORT_BUTTONS) >> 4) == 2; }))
        << "wheel event with the emulator UUID was not delivered";

    Post(MC_MOUSE_WHEEL, MouseEvent::Wheel(-3, target));  // 2-3 = -1 -> 0xF
    ASSERT_TRUE(TestWait::For([&] { return (In(emulator, PORT_BUTTONS) >> 4) == 0x0F; }));
    EXPECT_EQ(In(emulator, PORT_BUTTONS) & 0x0F, 0x0F) << "wheel must not disturb buttons / bit 3";
}

/// Events tagged with a different UUID are ignored by this instance
TEST_P(MouseMessageCenter_Test, EventsWithForeignUuidAreIgnored)
{
    auto emulator = CreateInstance();
    ASSERT_TRUE(emulator);
    const std::string target = TargetOf(emulator);
    const std::string foreign = std::string(unreal::UUID::Generate());
    ASSERT_NE(foreign, target);
    emulator->GetContext()->pMouse->SetWheelEnabled(true);  // make a leaked wheel event visible on #FADF

    Post(MC_MOUSE_MOVE, MouseEvent::Move(40, 40, foreign));
    Post(MC_MOUSE_BUTTON, MouseEvent::Buttons(0xF8, foreign));
    Post(MC_MOUSE_WHEEL, MouseEvent::Wheel(5, foreign));

    // Marker addressed to us: once it lands, everything posted before it was dispatched
    Post(MC_MOUSE_MOVE, MouseEvent::Move(1, 0, target));
    ASSERT_TRUE(TestWait::For([&] { return In(emulator, PORT_X) != RESET_X; }));

    EXPECT_EQ(In(emulator, PORT_X), RESET_X + 1) << "foreign move leaked into this instance";
    EXPECT_EQ(In(emulator, PORT_Y), RESET_Y) << "foreign move leaked into this instance";
    EXPECT_EQ(In(emulator, PORT_BUTTONS), 0x0F) << "foreign button/wheel leaked into this instance";
}

/// A payload of the wrong kind on a topic is ignored, never reinterpreted. The old
/// overloaded constructors built MouseEvent(0xFF, id) as a *wheel* event; a handler
/// reading whichever field it expects would silently apply garbage
TEST_P(MouseMessageCenter_Test, WrongKindOnTopicIsIgnored)
{
    auto emulator = CreateInstance();
    ASSERT_TRUE(emulator);
    const std::string target = TargetOf(emulator);

    emulator->GetContext()->pMouse->SetWheelEnabled(true);  // make a misread wheel payload visible on #FADF

    // Each mismatched payload also carries non-default values in the fields the
    // receiving topic's handler would read if it ignored the kind
    MouseEvent* wheelOnButtonTopic = MouseEvent::Wheel(3, target);
    wheelOnButtonTopic->buttonMask = 0xF8;
    Post(MC_MOUSE_BUTTON, wheelOnButtonTopic);

    MouseEvent* buttonsOnWheelTopic = MouseEvent::Buttons(0xF8, target);
    buttonsOnWheelTopic->wheelSteps = 5;
    Post(MC_MOUSE_WHEEL, buttonsOnWheelTopic);

    MouseEvent* buttonsOnMoveTopic = MouseEvent::Buttons(0x00, target);
    buttonsOnMoveTopic->dx = 40;
    buttonsOnMoveTopic->dy = 40;
    Post(MC_MOUSE_MOVE, buttonsOnMoveTopic);

    Post(MC_MOUSE_MOVE, MouseEvent::Move(1, 0, target));        // marker
    ASSERT_TRUE(TestWait::For([&] { return In(emulator, PORT_X) != RESET_X; }));

    EXPECT_EQ(In(emulator, PORT_X), RESET_X + 1);
    EXPECT_EQ(In(emulator, PORT_Y), RESET_Y);
    EXPECT_EQ(In(emulator, PORT_BUTTONS), 0x0F) << "mismatched payload changed buttons or wheel";
}

/// Two live instances: an event tagged for one moves only that one
TEST_P(MouseMessageCenter_Test, TwoInstancesRouteByUuid)
{
    auto first = CreateInstance();
    auto second = CreateInstance();
    ASSERT_TRUE(first);
    ASSERT_TRUE(second);
    ASSERT_NE(TargetOf(first), TargetOf(second));

    Post(MC_MOUSE_MOVE, MouseEvent::Move(10, 0, TargetOf(first)));
    Post(MC_MOUSE_BUTTON, MouseEvent::Buttons(0xFE, TargetOf(first)));

    // Marker for the second instance, posted after the first instance's events
    Post(MC_MOUSE_MOVE, MouseEvent::Move(0, 2, TargetOf(second)));

    ASSERT_TRUE(TestWait::For([&] { return In(second, PORT_Y) == RESET_Y + 2; }));
    ASSERT_TRUE(TestWait::For([&] { return In(first, PORT_X) == RESET_X + 10; }));

    EXPECT_EQ(In(first, PORT_Y), RESET_Y);
    EXPECT_EQ(In(first, PORT_BUTTONS) & 0x07, 0x06);

    EXPECT_EQ(In(second, PORT_X), RESET_X) << "first instance's move reached the second";
    EXPECT_EQ(In(second, PORT_BUTTONS) & 0x07, 0x07) << "first instance's button reached the second";
}

// Models whose port decoder answers the Kempston Mouse ports
INSTANTIATE_TEST_SUITE_P(KempstonMouseModels, MouseMessageCenter_Test,
                         ::testing::Values("PENTAGON", "SCORPION", "PROFSCORP"),
                         [](const ::testing::TestParamInfo<const char*>& info) { return std::string(info.param); });
