/// @file controllermanager_test.cpp
/// @brief Controller manager C1 (docs/inprogress/2026-10-17-controller-manager/retro-devices.md): connectors per
/// machine, selectors, plugs, the keyboard-joystick overlay, the host sample hook and ConnectorState through TTD.
///
/// Real machines: the connector list follows the port decoder and the model, and the journal is live. Creating a
/// machine costs a few tens of milliseconds, the price of testing the wiring rather than a bare context.

#include "emulator/io/controllers/controllermanager.h"

#include <iterator>
#include <memory>
#include <vector>

#include "_helpers/emulatortesthelper.h"
#include "base/featuremanager.h"
#include "debugger/ttd/timetravelcontroller.h"
#include "debugger/ttd/ttdinputjournal.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/io/controllers/ihostinputsource.h"
#include "emulator/io/joystick/joystick.h"
#include "emulator/io/keyboard/keyboard.h"
#include "emulator/io/mouse/mouse.h"
#include "emulator/io/mouse/mousemanager.h"
#include "emulator/ports/portdecoder.h"
#include "gtest/gtest.h"
#include "stdafx.h"

using namespace controllers;

namespace
{
/// Hands the manager whatever the test set last
class FakeHostSource : public IHostInputSource
{
public:
    void Sample(uint64_t frame, HostSample& out) override
    {
        (void)frame;
        samples++;
        for (const ConnectorSample& sample : held)
            out.connectors.push_back(sample);
        out.keys = keys;
        out.mouse = mouse;
    }

    std::vector<ConnectorSample> held;
    std::vector<uint8_t> keys;
    MouseSample mouse;
    int samples = 0;
};

constexpr uint16_t kRowSinclair1 = 0xEFFE;  // 0 9 8 7 6 from D0
constexpr uint16_t kRowSinclair2 = 0xF7FE;  // 1 2 3 4 5 from D0
}  // namespace

class ControllerManager_Test : public ::testing::Test
{
protected:
    Emulator* _emulator = nullptr;
    EmulatorContext* _context = nullptr;
    ControllerManager* _manager = nullptr;

    void Init(const char* model)
    {
        _emulator = EmulatorTestHelper::CreateStandardEmulator(model, LoggerLevel::LogError);
        ASSERT_NE(_emulator, nullptr);
        _context = _emulator->GetContext();
        _manager = _context->pControllerManager;
        ASSERT_NE(_manager, nullptr);
    }

    void TearDown() override
    {
        if (_emulator)
            EmulatorTestHelper::CleanupEmulator(_emulator);
    }

    std::vector<std::string> Ids() const
    {
        std::vector<std::string> ids;
        for (const Connector& connector : _manager->Connectors())
            ids.push_back(connector.id);
        return ids;
    }

    size_t Index(const char* selector) const
    {
        const int index = _manager->Find(selector);
        EXPECT_GE(index, 0) << selector;
        return index < 0 ? 0 : static_cast<size_t>(index);
    }

    void PlugOrFail(const char* selector, RetroDeviceKind device, OverlayOptions options)
    {
        const ControllerResult result = _manager->Plug(Index(selector), device, options);
        ASSERT_TRUE(result.Ok()) << result.message;
    }

    /// The five key bits of a half-row as the guest reads them (active low)
    uint8_t Row(uint16_t port) const
    {
        return _context->pKeyboard->HandlePortIn(port) & 0x1F;
    }
};

/// region <Connectors and selectors>

// Build-bound (~80 ms): four machines, the Next alone takes ~50 ms to build
TEST_F(ControllerManager_Test, Connectors_FollowTheMachine)
{
    Init("PENTAGON");
    EXPECT_EQ(Ids(), (std::vector<std::string>{"kempston", "keyjoy.1", "keyjoy.2"}));
    EmulatorTestHelper::CleanupEmulator(_emulator);

    Init("NEXT");
    EXPECT_EQ(Ids(), (std::vector<std::string>{"next.left", "next.right", "keyjoy.1", "keyjoy.2"}));
    EmulatorTestHelper::CleanupEmulator(_emulator);

    Init("PLUS3");
    const std::vector<std::string> plus3 = Ids();
    ASSERT_GE(plus3.size(), 4u);
    EXPECT_EQ(std::vector<std::string>(plus3.end() - 4, plus3.end()),
              (std::vector<std::string>{"sinclair-1", "sinclair-2", "keyjoy.1", "keyjoy.2"}))
        << "the +3 joystick sockets are wired to the keyboard rows";
    EXPECT_EQ(_manager->At(Index("sinclair-2")).options.sinclairPort, 2);
    EmulatorTestHelper::CleanupEmulator(_emulator);

    Init("48K");
    EXPECT_EQ(Ids(), (std::vector<std::string>{"keyjoy.1", "keyjoy.2"})) << "a bare 48K: keys-only joysticks";
    EmulatorTestHelper::CleanupEmulator(_emulator);
    _emulator = nullptr;
}

// Build-bound (~60 ms): the Next is the only machine with two DB9 connectors and an alias
TEST_F(ControllerManager_Test, Find_IdAliasKindIndex)
{
    Init("NEXT");
    EXPECT_EQ(_manager->Find("next.left"), 0);
    EXPECT_EQ(_manager->Find(" RIGHT "), 1) << "alias, trimmed, case-insensitive";
    EXPECT_EQ(_manager->Find("next-db9:1"), 1);
    EXPECT_EQ(_manager->Find("keyboard-joystick:1"), 3);
    EXPECT_EQ(_manager->Find("keyboard-joystick:2"), -1);
    EXPECT_EQ(_manager->Find("kempston"), -1) << "the Next's Kempston is its left DB9";
    EXPECT_EQ(_manager->Find(""), -1);
}

TEST_F(ControllerManager_Test, Plug_RefusesFixedAndForeignDevices)
{
    Init("PENTAGON");
    const uint64_t revision = _manager->Revision();

    EXPECT_EQ(_manager->Plug(Index("kempston"), RetroDeviceKind::SinclairJoystick, {}).error, ControllerError::Fixed);
    EXPECT_EQ(_manager->Unplug(Index("kempston")).error, ControllerError::Fixed);
    EXPECT_EQ(_manager->Plug(Index("keyjoy.1"), RetroDeviceKind::Md6Pad, {}).error, ControllerError::Incompatible);
    EXPECT_EQ(_manager->Revision(), revision) << "nothing changed";

    const ControllerResult plugged = _manager->Plug(Index("keyjoy.1"), RetroDeviceKind::CursorJoystick, {});
    ASSERT_TRUE(plugged.Ok()) << plugged.message;
    EXPECT_EQ(_manager->At(Index("keyjoy.1")).device, RetroDeviceKind::CursorJoystick);
    EXPECT_GT(_manager->Revision(), revision);
    EXPECT_FALSE(_manager->Submit(Index("keyjoy.2"), kFire)) << "an empty socket takes no input";
}

TEST_F(ControllerManager_Test, Rebuild_KeepsPlugsByIdAndRelease)
{
    Init("PENTAGON");
    PlugOrFail("keyjoy.2", RetroDeviceKind::SinclairJoystick, {});
    ASSERT_TRUE(_manager->Submit(Index("keyjoy.2"), kFire));
    _emulator->Reset();  // Core::Reset rebuilds the list
    EXPECT_EQ(_manager->At(Index("keyjoy.2")).device, RetroDeviceKind::SinclairJoystick);
    EXPECT_EQ(_manager->State(Index("keyjoy.2")), kFire) << "a reset does not release a held stick";
    EXPECT_EQ(Row(kRowSinclair1), 0x1E);
}

/// endregion </Connectors and selectors>

/// region <Keyboard-joystick overlay>

TEST_F(ControllerManager_Test, Sinclair1_OverlayAndsWithTheRealKeys)
{
    Init("48K");
    PlugOrFail("keyjoy.1", RetroDeviceKind::SinclairJoystick, {});
    Keyboard* keyboard = _context->pKeyboard;

    ASSERT_TRUE(_manager->Submit(Index("keyjoy.1"), kLeft));
    EXPECT_EQ(Row(kRowSinclair1), 0x0F) << "left = 6 (D4)";

    keyboard->PressKey(ZXKEY_6);
    ASSERT_TRUE(_manager->Submit(Index("keyjoy.1"), 0));
    EXPECT_EQ(Row(kRowSinclair1), 0x0F) << "releasing the stick leaves the real key held";
    keyboard->ReleaseKey(ZXKEY_6);
    EXPECT_EQ(Row(kRowSinclair1), 0x1F);

    ASSERT_TRUE(_manager->Submit(Index("keyjoy.1"), kRight | kDown | kUp | kFire));
    EXPECT_EQ(Row(kRowSinclair1), 0x10) << "7 R (D3), 8 D (D2), 9 U (D1), 0 F (D0)";
    EXPECT_EQ(Row(kRowSinclair2), 0x1F) << "the other half-row is untouched";
}

TEST_F(ControllerManager_Test, Sinclair2CursorAndKeysWirings)
{
    Init("48K");
    OverlayOptions sinclair2;
    sinclair2.sinclairPort = 2;
    PlugOrFail("keyjoy.1", RetroDeviceKind::SinclairJoystick, sinclair2);
    ASSERT_TRUE(_manager->Submit(Index("keyjoy.1"), kLeft | kFire));
    EXPECT_EQ(Row(kRowSinclair2), 0x0E) << "1 L (D0), 5 F (D4)";

    OverlayOptions cursor;
    cursor.cursorShift = true;
    PlugOrFail("keyjoy.1", RetroDeviceKind::CursorJoystick, cursor);
    EXPECT_EQ(Row(kRowSinclair2), 0x1F) << "replacing the device released what the old one held";
    ASSERT_TRUE(_manager->Submit(Index("keyjoy.1"), kLeft | kUp));
    EXPECT_EQ(Row(kRowSinclair2), 0x0F) << "5 L";
    EXPECT_EQ(Row(kRowSinclair1), 0x17) << "7 U";
    EXPECT_EQ(Row(0xFEFE), 0x1E) << "CAPS SHIFT with the shift option";

    PlugOrFail("keyjoy.2", RetroDeviceKind::KeysJoystick,
               ControllerManager::DefaultOptions(RetroDeviceKind::KeysJoystick));
    ASSERT_TRUE(_manager->Submit(Index("keyjoy.2"), kUp | kFire));
    EXPECT_EQ(Row(0xFBFE), 0x1E) << "Q up";
    EXPECT_EQ(Row(0x7FFE), 0x1E) << "SPACE fire";
}

TEST_F(ControllerManager_Test, ParseKeys)
{
    std::array<ZXKeysEnum, kOverlayControls> keys{};
    EXPECT_TRUE(ControllerManager::ParseKeys("up:w, down:s,left:a,right:d,fire:enter,fire2:m", keys));
    EXPECT_EQ(keys[3], ZXKEY_W);
    EXPECT_EQ(keys[4], ZXKEY_ENTER);
    EXPECT_EQ(keys[5], ZXKEY_M);
    EXPECT_FALSE(ControllerManager::ParseKeys("up:nosuchkey,jump:q,fire:0", keys)) << "the bad entries are skipped";
    EXPECT_EQ(keys[4], ZXKEY_0) << "the rest is applied";
}

/// endregion </Keyboard-joystick overlay>

/// region <Routing and the host source>

TEST_F(ControllerManager_Test, KempstonAndNextKeepTheirDevicesAndKinds)
{
    Init("PENTAGON");
    ASSERT_TRUE(_manager->Submit(Index("kempston"), kFire | kUp));
    EXPECT_EQ(_context->pJoystick->State(), 0x18);
    EXPECT_EQ(_manager->State(Index("kempston")), 0x18);
    EmulatorTestHelper::CleanupEmulator(_emulator);

    Init("NEXT");
    ASSERT_TRUE(_manager->Submit(Index("right"), 0x0811));  // right, B, MODE
    EXPECT_EQ(_context->pPortDecoder->PadState(1), 0x0811);
    EXPECT_EQ(_manager->State(Index("next.right")), 0x0811);
}

/// The Kempston options (C1c): `extended` off reads bits 5-7 as 0, `im2-leak` makes the stick byte the IM2 vector
/// when A5 of PC is 0; an options change is a replug of the fixed socket, refused while recording
TEST_F(ControllerManager_Test, KempstonOptions_ExtendedAndIm2Leak)
{
    Init("PENTAGON");
    Joystick& stick = *_context->pJoystick;
    const size_t kempston = Index("kempston");
    ASSERT_TRUE(_manager->Submit(kempston, 0xE1));  // right + fires 2-4
    EXPECT_EQ(stick.Read(), 0xE1);
    EXPECT_EQ(stick.Im2Vector(0xFF, 0x8000), 0xFF) << "no leak by default";

    OverlayOptions options;
    options.extended = false;
    options.im2Leak = true;
    PlugOrFail("kempston", RetroDeviceKind::KempstonJoystick, options);
    EXPECT_FALSE(_manager->At(kempston).options.extended);
    EXPECT_TRUE(_manager->At(kempston).options.im2Leak);
    EXPECT_EQ(stick.Read(), 0x01) << "bits 5-7 read 0";
    EXPECT_EQ(stick.Im2Vector(0xFF, 0x8000), 0x01) << "A5 of PC is 0: the stick drives the bus";
    EXPECT_EQ(stick.Im2Vector(0xFF, 0x8020), 0xFF) << "A5 of PC is 1: the floating bus";

    _manager->Rebuild();
    EXPECT_TRUE(_manager->At(Index("kempston")).options.im2Leak) << "a rebuild keeps the socket's options";
    EXPECT_FALSE(stick.Extended());

    _emulator->GetFeatureManager()->setFeature(Features::kTimeTravel, true);
    ASSERT_TRUE(_context->pTimeTravelController->StartRecording());
    const ControllerResult refused = _manager->Plug(kempston, RetroDeviceKind::KempstonJoystick, {});
    EXPECT_EQ(refused.error, ControllerError::Recording) << refused.message;
    _context->pTimeTravelController->StopRecording();
    PlugOrFail("kempston", RetroDeviceKind::KempstonJoystick, {});
    EXPECT_TRUE(stick.Extended());
    EXPECT_EQ(stick.Im2Vector(0xFF, 0x8000), 0xFF);
}

TEST_F(ControllerManager_Test, HostSource_SubmitsChangesOnly)
{
    Init("48K");
    PlugOrFail("keyjoy.1", RetroDeviceKind::SinclairJoystick, {});
    auto source = std::make_shared<FakeHostSource>();
    _manager->SetHostInputSource(source);

    source->held = {{"keyjoy.1", kFire}, {"nosuch", kFire}};
    _emulator->RunNFrames(1);
    EXPECT_GE(source->samples, 1);
    EXPECT_EQ(_manager->State(Index("keyjoy.1")), kFire) << "sampled at the frame boundary";

    // Another source (automation) changes the connector: an unchanged host sample does not override it
    ASSERT_TRUE(_manager->Submit(Index("keyjoy.1"), kLeft));
    _emulator->RunNFrames(1);
    EXPECT_EQ(_manager->State(Index("keyjoy.1")), kLeft);

    source->held = {{"keyjoy.1", 0}};
    _emulator->RunNFrames(1);
    EXPECT_EQ(_manager->State(Index("keyjoy.1")), 0);

    _manager->SetHostInputSource(nullptr);
    const int before = source->samples;
    _emulator->RunNFrames(1);
    EXPECT_EQ(source->samples, before) << "detached";
}

// Boot-bound (~15 ms): one 48K, four frames
TEST_F(ControllerManager_Test, HostSource_KeysOnTheOverlayAndTheMouse)
{
    Init("48K");
    auto source = std::make_shared<FakeHostSource>();
    _manager->SetHostInputSource(source);
    constexpr uint16_t kRowQ = 0xFBFE;  // Q W E R T from D0

    // The user holds W; a pad holds Q and W through the host key overlay
    _context->pKeyboard->PressKey(ZXKEY_W);
    source->keys = {ZXKEY_Q, ZXKEY_W};
    _emulator->RunNFrames(1);
    EXPECT_EQ(Row(kRowQ), 0x1C);
    EXPECT_EQ(_manager->HostKeys(2), 0x03);
    source->keys.clear();
    _emulator->RunNFrames(1);
    EXPECT_EQ(Row(kRowQ), 0x1D) << "the pad's release leaves the user's W held";
    _context->pKeyboard->ReleaseKey(ZXKEY_W);

    Mouse* mouse = _context->pMouse;
    ASSERT_NE(mouse, nullptr);
    const uint8_t x = mouse->GetX();
    source->mouse.dx = 5;
    source->mouse.buttons = 0x01;
    _emulator->RunNFrames(1);
    EXPECT_EQ(static_cast<uint8_t>(mouse->GetX() - x), 5) << "a move of this frame";
    EXPECT_EQ(_context->pMouseManager->PressedBits(MouseManager::ButtonSource::Controllers), 0x01);
    source->mouse = MouseSample{};
    _emulator->RunNFrames(1);
    EXPECT_EQ(static_cast<uint8_t>(mouse->GetX() - x), 5) << "no move: no counts";
    EXPECT_EQ(_context->pMouseManager->PressedBits(MouseManager::ButtonSource::Controllers), 0x00);
    _manager->SetHostInputSource(nullptr);
}

/// endregion </Routing and the host source>

/// region <TTD>

TEST_F(ControllerManager_Test, TTDBlobRoundTrip)
{
    Init("48K");
    PlugOrFail("keyjoy.1", RetroDeviceKind::SinclairJoystick, {});
    PlugOrFail("keyjoy.2", RetroDeviceKind::CursorJoystick, {});
    ASSERT_TRUE(_manager->Submit(Index("keyjoy.1"), kUp));
    ASSERT_TRUE(_manager->Submit(Index("keyjoy.2"), kFire));

    std::vector<uint8_t> blob(_manager->TTDStateSize());
    ASSERT_EQ(blob.size(), 36u) << "version, count, two plugs of 11 bytes and their masks, the host key overlay";
    _manager->TTDSaveState(blob.data());
    const uint64_t hash = _manager->TTDHashState();

    ASSERT_TRUE(_manager->Submit(Index("keyjoy.1"), 0));
    ASSERT_TRUE(_manager->Submit(Index("keyjoy.2"), 0));
    EXPECT_NE(_manager->TTDHashState(), hash);

    PlugOrFail("keyjoy.2", RetroDeviceKind::KeysJoystick,
               ControllerManager::DefaultOptions(RetroDeviceKind::KeysJoystick));
    _manager->TTDLoadState(blob.data());
    EXPECT_EQ(_manager->At(Index("keyjoy.2")).device, RetroDeviceKind::CursorJoystick) << "the plug is restored too";
    EXPECT_EQ(_manager->State(Index("keyjoy.1")), kUp);
    EXPECT_EQ(_manager->TTDHashState(), hash);
    EXPECT_EQ(Row(kRowSinclair1), 0x1C) << "9 U (Sinclair 1) and 0 F (Cursor), restored into the overlay";

    // A v1 blob (masks only) still loads and keeps the plugs
    const uint8_t v1[] = {1, 2, 0, 0, kFire, 0};
    _manager->TTDLoadState(v1);
    EXPECT_EQ(_manager->State(Index("keyjoy.1")), 0);
    EXPECT_EQ(_manager->State(Index("keyjoy.2")), kFire);
    EXPECT_EQ(_manager->At(Index("keyjoy.2")).device, RetroDeviceKind::CursorJoystick);
}

TEST_F(ControllerManager_Test, Fingerprint_NamesPluggedSocketsOnly)
{
    Init("PLUS3");
    auto fields = [this]() {
        ttd::TTDConfigFingerprint fp;
        _manager->AddTtdFingerprint(fp);
        std::vector<std::string> names;
        for (const ttd::TTDConfigField& field : fp.fields)
            names.push_back(field.name);
        return names;
    };
    EXPECT_TRUE(fields().empty()) << "empty sockets and the board's fixed ones: the fingerprint is as before";

    PlugOrFail("keyjoy.2", RetroDeviceKind::SinclairJoystick, {});
    EXPECT_EQ(fields(), (std::vector<std::string>{"controllers.keyjoy.2"}));
    ttd::TTDConfigFingerprint port1;
    _manager->AddTtdFingerprint(port1);
    OverlayOptions port2;
    port2.sinclairPort = 2;
    PlugOrFail("keyjoy.2", RetroDeviceKind::SinclairJoystick, port2);
    ttd::TTDConfigFingerprint changed;
    _manager->AddTtdFingerprint(changed);
    EXPECT_NE(port1, changed) << "an option is configuration too";
}

// Record-bound (~70 ms): runs frames under a TTD recording, then seeks back and forward
TEST_F(ControllerManager_Test, Recording_JournalsPlugsAndConnectorState)
{
    Init("48K");
    ttd::TimeTravelController* ttd = _context->pTimeTravelController;
    ASSERT_NE(ttd, nullptr);
    PlugOrFail("keyjoy.1", RetroDeviceKind::SinclairJoystick, {});
    _emulator->GetFeatureManager()->setFeature(Features::kTimeTravel, true);
    ASSERT_TRUE(ttd->StartRecording());

    auto source = std::make_shared<FakeHostSource>();
    _manager->SetHostInputSource(source);

    struct Sample
    {
        ttd::TTDTimePoint at;
        uint8_t row = 0;
        RetroDeviceKind second = RetroDeviceKind::None;
    };
    std::vector<Sample> samples;
    auto take = [&]() {
        Z80* z80 = _context->pCore->GetZ80();
        samples.push_back({{_context->emulatorState.frame_counter, _context->emulatorState.TtdTInFrame(z80->t)},
                           Row(kRowSinclair1),
                           _manager->At(Index("keyjoy.2")).device});
        _emulator->RunTStates(100);  // no later event shares this time point
    };

    const uint16_t masks[] = {0, kLeft, kLeft | kFire, kFire, kFire, 0};
    for (size_t step = 0; step < std::size(masks); step++)
    {
        if (step == 2)
        {
            // A plug while recording waits for the frame boundary, where the checkpoint takes it
            ASSERT_TRUE(_manager->Plug(Index("keyjoy.2"), RetroDeviceKind::CursorJoystick, {}).Ok());
            EXPECT_TRUE(_manager->HasPending());
            EXPECT_EQ(_manager->At(Index("keyjoy.2")).device, RetroDeviceKind::None) << "not before the boundary";
        }
        if (step == 4)
        {
            ASSERT_TRUE(_manager->Unplug(Index("keyjoy.2")).Ok());
        }
        source->held = {{"keyjoy.1", masks[step]}};
        _emulator->RunNFrames(1);
        EXPECT_FALSE(_manager->HasPending()) << "landed at step " << step;
        take();
    }
    ttd->StopRecording();
    _manager->SetHostInputSource(nullptr);

    size_t events = 0;
    for (const auto& ev : ttd->GetInputJournal().Events())
    {
        if (ev.kind == ttd::TTDInputKind::ConnectorState)
            events++;
    }
    EXPECT_EQ(events, 5u) << "the first sample whole, then left, left+fire, fire, release: an unchanged sample is "
                             "not journaled";

    const uint8_t expected[] = {0x1F, 0x0F, 0x0E, 0x1E, 0x1E, 0x1F};
    const RetroDeviceKind second[] = {RetroDeviceKind::None,           RetroDeviceKind::None,
                                      RetroDeviceKind::CursorJoystick, RetroDeviceKind::CursorJoystick,
                                      RetroDeviceKind::None,           RetroDeviceKind::None};
    for (size_t i = 0; i < samples.size(); i++)
    {
        EXPECT_EQ(samples[i].row, expected[i]) << "live at step " << i;
        EXPECT_EQ(samples[i].second, second[i]) << "live at step " << i;
    }
    for (size_t i = samples.size(); i-- > 0;)
    {
        ASSERT_TRUE(ttd->SeekTo(samples[i].at)) << "backward to " << i;
        EXPECT_EQ(Row(kRowSinclair1), samples[i].row) << "after seeking back to step " << i;
        EXPECT_EQ(_manager->At(Index("keyjoy.2")).device, samples[i].second) << "after seeking back to step " << i;
    }
    for (size_t i = 0; i < samples.size(); i++)
    {
        ASSERT_TRUE(ttd->SeekTo(samples[i].at)) << "forward to " << i;
        EXPECT_EQ(Row(kRowSinclair1), samples[i].row) << "after seeking forward to step " << i;
        EXPECT_EQ(_manager->At(Index("keyjoy.2")).device, samples[i].second) << "after seeking forward to step " << i;
    }
}

/// endregion </TTD>
