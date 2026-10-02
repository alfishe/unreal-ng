#include "stdafx.h"
#include "gtest/gtest.h"

#include "_helpers/testwaithelper.h"
#include "debugger/debugmanager.h"
#include "debugger/mouse/debugmousemanager.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/emulatormanager.h"
#include "emulator/io/mouse/imousesink.h"
#include "emulator/io/mouse/mouse.h"
#include "emulator/io/mouse/mousemanager.h"

/// @brief MouseManager: one input path per emulator, every mouse device of the
/// machine gets every input (docs/inprogress/2026-10-02-mouse-manager/design.md §3.1).

namespace
{
/// A mouse device that records what it got
class RecordingSink : public IMouseSink
{
public:
    explicit RecordingSink(bool fitted = true) : fitted(fitted) {}

    bool IsMouseFitted() const override { return fitted; }
    void OnMouseMotion(int dx, int dy) override
    {
        sumX += dx;
        sumY += dy;
        motions++;
    }
    void OnMouseButtons(uint8_t activeLowMask) override { buttons = activeLowMask; }
    void OnMouseWheel(int steps) override { wheel += steps; }
    void OnMouseCounters(uint8_t x, uint8_t y) override
    {
        counterX = x;
        counterY = y;
    }

    bool fitted;
    int sumX = 0;
    int sumY = 0;
    int motions = 0;
    uint8_t buttons = 0xFF;
    int wheel = 0;
    int counterX = -1;
    int counterY = -1;
};
}  // namespace

class MouseManager_Test : public ::testing::Test
{
protected:
    void SetUp() override { _manager = std::make_unique<MouseManager>(&_context); }

    EmulatorContext _context;
    std::unique_ptr<MouseManager> _manager;
};

/// Every registered device gets every input; a removed one gets nothing more
TEST_F(MouseManager_Test, FansOutToEveryDevice)
{
    RecordingSink kempston, serial;
    _manager->AddSink(&kempston);
    _manager->AddSink(&serial);
    _manager->AddSink(&serial);  // registering twice delivers once

    _manager->ApplyMotion(5, -3);
    _manager->ApplyButtons(0xFE);
    _manager->ApplyWheel(2);
    _manager->ApplyCounters(10, 20);

    for (const RecordingSink* sink : {&kempston, &serial})
    {
        EXPECT_EQ(sink->sumX, 5);
        EXPECT_EQ(sink->sumY, -3);
        EXPECT_EQ(sink->motions, 1);
        EXPECT_EQ(sink->buttons, 0xFE);
        EXPECT_EQ(sink->wheel, 2);
        EXPECT_EQ(sink->counterX, 10);
        EXPECT_EQ(sink->counterY, 20);
    }

    _manager->RemoveSink(&serial);
    _manager->ApplyMotion(1, 1);
    EXPECT_EQ(kempston.motions, 2);
    EXPECT_EQ(serial.motions, 1);
}

/// Capturing makes sense only when some device of the machine can be read
TEST_F(MouseManager_Test, HasMouseDeviceFollowsFittedDevices)
{
    EXPECT_FALSE(_manager->HasMouseDevice()) << "no device";

    RecordingSink unfitted(false);
    _manager->AddSink(&unfitted);
    EXPECT_FALSE(_manager->HasMouseDevice()) << "a device that is not fitted";

    RecordingSink fitted(true);
    _manager->AddSink(&fitted);
    EXPECT_TRUE(_manager->HasMouseDevice());

    _manager->RemoveSink(&fitted);
    EXPECT_FALSE(_manager->HasMouseDevice());
}

/// A sink is reachable as soon as it is fitted, unless it says the machine shadows it
TEST_F(MouseManager_Test, IsMouseInUseDefaultsToFitted)
{
    EXPECT_FALSE(_manager->IsMouseInUse()) << "no device";
    RecordingSink unfitted(false);
    _manager->AddSink(&unfitted);
    EXPECT_FALSE(_manager->IsMouseInUse());
    RecordingSink fitted(true);
    _manager->AddSink(&fitted);
    EXPECT_TRUE(_manager->IsMouseInUse());
}

/// The host mouse is worth capturing only while a program reads the Kempston mouse: the 128K ROM
/// never does, and while TR-DOS is active the ports are Beta Disk's
TEST(MouseManagerMachine_Test, KempstonMouseIsInUseOnlyWhilePolled)
{
    EmulatorManager* emulators = EmulatorManager::GetInstance();
    auto pentagon = emulators->CreateEmulatorWithModel("mouse-poll-pentagon", "PENTAGON", LoggerLevel::LogError);
    ASSERT_NE(pentagon, nullptr);
    EmulatorContext* context = pentagon->GetContext();
    ASSERT_TRUE(context->pMouse->IsPresent());
    MouseManager* manager = context->pMouseManager;
    uint64_t& frame = context->emulatorState.frame_counter;

    EXPECT_TRUE(manager->HasMouseDevice());
    EXPECT_FALSE(manager->IsMouseInUse()) << "fitted, nobody reads it (the 128K ROM)";

    context->pMouse->PeekRegister(1);
    EXPECT_FALSE(manager->IsMouseInUse()) << "a debug read is not a program";

    context->pMouse->ReadRegister(1);
    EXPECT_TRUE(manager->IsMouseInUse()) << "a program read it";

    frame += Mouse::kPolledWithinFrames;
    EXPECT_TRUE(manager->IsMouseInUse());
    frame += 1;
    EXPECT_FALSE(manager->IsMouseInUse()) << "not read for a second";

    context->pMouse->ReadRegister(0);
    EXPECT_TRUE(manager->IsMouseInUse());
    context->emulatorState.flags |= CF_DOSPORTS;
    EXPECT_TRUE(manager->HasMouseDevice()) << "still fitted";
    EXPECT_FALSE(manager->IsMouseInUse()) << "TR-DOS active: only Beta Disk answers";
    context->emulatorState.flags &= static_cast<uint8_t>(~CF_DOSPORTS);
    EXPECT_TRUE(manager->IsMouseInUse());

    context->pMouse->Reset();
    EXPECT_FALSE(manager->IsMouseInUse()) << "a reset program starts over";
    emulators->RemoveEmulator(pentagon->GetId());
}

/// The header's worked example: automation holds left, the host presses and
/// releases right; left stays held until automation lets go
TEST_F(MouseManager_Test, ButtonSourcesDoNotOverwriteEachOther)
{
    using Source = MouseManager::ButtonSource;
    EXPECT_EQ(_manager->ComposeButtons(Source::Automation, 0x01), 0xFE) << "L";
    EXPECT_EQ(_manager->ComposeButtons(Source::Host, 0x02), 0xFC) << "L + R";
    EXPECT_EQ(_manager->ComposeButtons(Source::Host, 0x00), 0xFE) << "L";
    EXPECT_EQ(_manager->ComposeButtons(Source::Automation, 0x00), 0xFF) << "none";

    EXPECT_EQ(_manager->ComposeButtons(Source::Host, 0xFF), 0xF8) << "only the three button bits are kept";
    EXPECT_EQ(_manager->PressedBits(Source::Host), 0x07);
    _manager->ClearButtonSources();
    EXPECT_EQ(_manager->PressedBits(Source::Host), 0x00);
    EXPECT_EQ(_manager->PressedBits(Source::Automation), 0x00);
}

/// Host events reach the devices through the message center; a gated manager
/// (ZX-Poly member) ignores them. Bare context: no DebugManager, applied directly
TEST_F(MouseManager_Test, HostEventsAndGate)
{
    RecordingSink sink;
    _manager->AddSink(&sink);

    MessageCenter::DefaultMessageCenter().Post(MC_MOUSE_MOVE, MouseEvent::Move(4, 2));
    ASSERT_TRUE(TestWait::For([&] { return sink.motions == 1; }));
    EXPECT_EQ(sink.sumX, 4);

    // The gate, checked on the callback itself (no worker-thread timing involved)
    std::unique_ptr<MouseEvent> move(MouseEvent::Move(100, 100));
    Message message(0, move.get(), false);
    _manager->SetHostInputGated(true);
    _manager->OnMouseMove(0, &message);
    EXPECT_EQ(sink.motions, 1) << "gated: dropped";
    _manager->SetHostInputGated(false);
    _manager->OnMouseMove(0, &message);
    EXPECT_EQ(sink.motions, 2);
    EXPECT_EQ(sink.sumX, 104);

    // An event tagged for another emulator is not for this one (a bare context
    // has no emulator id, so only untagged events apply here)
    std::unique_ptr<MouseEvent> wrongKind(MouseEvent::Wheel(3));
    Message wheelOnMoveTopic(0, wrongKind.get(), false);
    _manager->OnMouseMove(0, &wheelOnMoveTopic);
    EXPECT_EQ(sink.motions, 2) << "a wheel payload on the move topic is ignored";
}

/// Through the real machine: automation's press survives the host's button
/// changes, and the host's release does not cancel it (design problem 5)
TEST(MouseManagerMachine_Test, AutomationAndHostButtonsCompose)
{
    EmulatorManager* emulators = EmulatorManager::GetInstance();
    auto emulator = emulators->CreateEmulatorWithModel("mouse-compose", "48K", LoggerLevel::LogError);
    ASSERT_NE(emulator, nullptr);
    EmulatorContext* context = emulator->GetContext();
    ASSERT_NE(context->pMouseManager, nullptr);
    DebugMouseManager* debug = context->pDebugManager->GetMouseManager();
    ASSERT_NE(debug, nullptr);

    ASSERT_TRUE(debug->PressButton(MouseButton::Left).ok());
    EXPECT_EQ(context->pMouse->GetButtons(), 0xFE);
    debug->ApplyHostButtons(0xFD);  // host: right held
    EXPECT_EQ(context->pMouse->GetButtons(), 0xFC);
    debug->ApplyHostButtons(0xFF);  // host: nothing held
    EXPECT_EQ(context->pMouse->GetButtons(), 0xFE) << "automation's left is still held";
    ASSERT_TRUE(debug->ReleaseButton(MouseButton::Left).ok());
    EXPECT_EQ(context->pMouse->GetButtons(), 0xFF);

    emulators->RemoveEmulator(emulator->GetId());
}

/// Mouse=NONE removes the Kempston port. On a 48K nothing else is left; the
/// Sprinter's serial mouse is part of the board and still wants the host mouse
TEST(MouseManagerMachine_Test, MouseNoneLeavesBoardMice)
{
    EmulatorManager* emulators = EmulatorManager::GetInstance();
    auto noMouse = [](CONFIG& config) {
        config.input.mouse = MOUSE_TYPE_NONE;
        config.input.mouseConfigured = true;
    };

    auto spectrum = emulators->CreateEmulatorWithModel("mouse-none-48k", "48K", LoggerLevel::LogError, nullptr, noMouse);
    ASSERT_NE(spectrum, nullptr);
    EXPECT_FALSE(spectrum->GetContext()->pMouse->IsPresent());
    EXPECT_FALSE(spectrum->GetContext()->pMouseManager->HasMouseDevice());
    emulators->RemoveEmulator(spectrum->GetId());

    auto sprinter = emulators->CreateEmulatorWithModel("mouse-none-sp", "SPRINTER", LoggerLevel::LogError, nullptr, noMouse);
    ASSERT_NE(sprinter, nullptr);
    EXPECT_FALSE(sprinter->GetContext()->pMouse->IsPresent());
    EXPECT_TRUE(sprinter->GetContext()->pMouseManager->HasMouseDevice()) << "the serial mouse";
    emulators->RemoveEmulator(sprinter->GetId());
}
