/// @file debugmousemanager_machines_test.cpp
/// @brief The automation mouse (DebugMouseManager: what WebAPI, MCP, CLI, Lua and Python call) on every
/// creatable machine (docs/inprogress/2026-10-03-mouse-api-routing/design.md §7).
///
/// For each model: the machine is created as the WebAPI creates it, the status names the machine's own
/// mouse (Kempston interface, the Sprinter board mouse, the ZX-Evo / TS-Conf PS/2 mouse on the AVR), and
/// move / press / wheel / click / release-all reach that device with the register values the Kempston
/// models have always given (golden values: power-on X = 31, Y = 85; +5 / +3 -> 36 / 88; left -> #FE).
/// With [INPUT] Mouse=NONE the input is refused with NoMouseFitted, except on the Sprinter, whose mouse is
/// part of the board. Nothing crashes on any model.
/// Over the 50 ms budget (~0.1-0.3 s per model): a machine is created per model, and a few frames run.

#include <gtest/gtest.h>

#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

#include "_helpers/emulatortesthelper.h"
#include "base/featuremanager.h"
#include "debugger/debugmanager.h"
#include "debugger/mouse/debugmousemanager.h"
#include "debugger/ttd/timetravelmanager.h"
#include "debugger/ttd/ttdcheckpoint.h"
#include "debugger/ttd/ttdperipheralregistry.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/emulatormanager.h"
#include "emulator/io/mouse/mouse.h"
#include "emulator/io/mouse/mousemanager.h"
#include "emulator/memory/memory.h"
#include "emulator/zxpoly/zxpolygroup.h"

namespace
{
/// What each model's mouse is (the inventory of design §2)
struct MouseExpectation
{
    std::string device;  ///< the id status reports: kempston, sprinter, evo-ps2
    uint8_t x0;          ///< power-on X / Y the guest reads
    uint8_t y0;
    bool wheel;          ///< the guest sees wheel steps
    bool boardMouse;     ///< part of the machine: [INPUT] Mouse=NONE does not remove it
};

MouseExpectation ExpectationFor(const std::string& model)
{
    if (model == "SPRINTER")
        return {"sprinter", 31, 85, false, true};
    if (model == "TSL" || model == "TSL-VDAC2" || model == "ATM3")
        return {"evo-ps2", 0, 1, true, false};  // the AVR found a mouse: X = 0, Y = 1
    if (model == "ATM710" || model == "ATM450")
        return {"kempston", 31, 85, true, false};  // the ZX-bus card, configs/atm*/unreal.ini Wheel=KEMPSTON
    return {"kempston", 31, 85, false, false};
}

/// #FADF with these buttons held (bit set = pressed) and this wheel position, on the expected device
uint8_t ButtonsPort(const MouseExpectation& e, uint8_t pressed, int wheelSteps)
{
    const uint8_t low = static_cast<uint8_t>(0x07 & ~pressed);
    if (e.device == "evo-ps2")  // high nibble F, minus a step per notch away from the user
        return static_cast<uint8_t>(((0xF0 - (wheelSteps << 4)) & 0xF0) | 0x08 | low);
    if (e.wheel)  // Kempston with a wheel: the counter in the high nibble
        return static_cast<uint8_t>(((wheelSteps & 0x0F) << 4) | 0x08 | low);
    return static_cast<uint8_t>(0xF8 | low);
}
}  // namespace

class DebugMouseManagerMachines_Test : public ::testing::TestWithParam<const char*>
{
protected:
    EmulatorManager* _manager = nullptr;
    std::shared_ptr<Emulator> _emulator;
    EmulatorContext* _context = nullptr;
    ZXPolyGroup* _group = nullptr;

    void SetUp() override { _manager = EmulatorManager::GetInstance(); }

    void TearDown() override
    {
        _group = nullptr;
        if (_emulator)
        {
            const std::string id = _emulator->GetId();
            _emulator.reset();
            _manager->RemoveEmulator(id);
        }
    }

    /// The machine as POST /emulator/start creates it; `mouseNone`: [INPUT] Mouse=NONE
    void Create(bool mouseNone)
    {
        const std::string model = GetParam();
        std::function<void(CONFIG&)> overrideConfig;
        if (mouseNone)
            overrideConfig = [](CONFIG& config) {
                config.input.mouse = MOUSE_TYPE_NONE;
                config.input.mouseConfigured = true;
            };
        std::string error;
        if (model.rfind("ZXPOLY-", 0) == 0)
        {
            _emulator = _manager->CreateZXPolyMachine("mouse-" + model, model, "", &error, overrideConfig);
            ASSERT_TRUE(_emulator) << model << ": " << error;
            _group = _manager->GetZXPolyGroup(_emulator->GetId());
            ASSERT_NE(_group, nullptr);
        }
        else
        {
            _emulator = _manager->CreateEmulatorWithModel("mouse-" + model, model, LoggerLevel::LogError, &error,
                                                          overrideConfig);
            ASSERT_TRUE(_emulator) << model << ": " << error;
        }
        _context = _emulator->GetContext();
        ASSERT_NE(_context, nullptr);
        ASSERT_NE(_context->pDebugManager, nullptr);
        ASSERT_NE(Api(), nullptr);
    }

    DebugMouseManager* Api() const { return _context->pDebugManager->GetMouseManager(); }

    /// One frame (a ZX-Poly group runs its four modules; its input lands at the frame boundary)
    void Frames(int count)
    {
        for (int i = 0; i < count; i++)
        {
            if (_group)
                _group->RunFrame();
            else
                _emulator->RunNFrames(1, true);
        }
    }

    /// The device status reports, by id; fails when the machine reports another one
    MouseDeviceStatus Device(const MouseExpectation& e)
    {
        const MouseStateSnapshot state = Api()->GetState();
        EXPECT_TRUE(state.mouseFitted) << GetParam();
        if (!state.device)
        {
            ADD_FAILURE() << GetParam() << ": no device reported";
            return {};
        }
        EXPECT_EQ(state.device->id, e.device) << GetParam();
        return *state.device;
    }
};

/// Status names the machine's own mouse, and every input reaches it with the golden register values
TEST_P(DebugMouseManagerMachines_Test, InputReachesTheMachinesMouse)
{
    ASSERT_NO_FATAL_FAILURE(Create(false));
    const MouseExpectation e = ExpectationFor(GetParam());

    // Status: the device, the devices list (a Kempston interface object is not offered where the ports read
    // a board mouse), the power-on registers
    const MouseStateSnapshot state = Api()->GetState();
    ASSERT_TRUE(state.device.has_value()) << GetParam();
    EXPECT_EQ(state.device->id, e.device);
    EXPECT_TRUE(state.device->fitted);
    EXPECT_TRUE(state.device->hasPorts);
    EXPECT_EQ(state.device->hasSerial, e.device == "sprinter");
    EXPECT_EQ(state.device->hasPs2, e.device == "evo-ps2");
    EXPECT_EQ(state.devices.size(), 1u) << GetParam() << ": one mouse device per machine";
    EXPECT_EQ(state.device->portX, e.x0);
    EXPECT_EQ(state.device->portY, e.y0);
    EXPECT_EQ(state.device->portButtons, ButtonsPort(e, 0, 0));
    EXPECT_EQ(state.portX, e.x0) << "the top-level `ports` are the machine's ports";
    EXPECT_TRUE(Api()->CheckDevice(e.device).ok());
    EXPECT_EQ(Api()->CheckDevice("no-such-mouse").status, MouseInjectStatus::InvalidArgument);

    // Move
    ASSERT_TRUE(Api()->Move(5, 3).ok());
    Frames(1);
    MouseDeviceStatus device = Device(e);
    EXPECT_EQ(device.portX, static_cast<uint8_t>(e.x0 + 5)) << GetParam();
    EXPECT_EQ(device.portY, static_cast<uint8_t>(e.y0 + 3)) << GetParam();

    // Press left
    ASSERT_TRUE(Api()->PressButton(MouseButton::Left).ok());
    Frames(1);
    EXPECT_EQ(Device(e).portButtons, ButtonsPort(e, 0x01, 0)) << GetParam();

    // Wheel: seen where a wheel is fitted, a warning elsewhere
    const MouseInjectResult wheel = Api()->Wheel(1);
    ASSERT_TRUE(wheel.ok());
    EXPECT_EQ(wheel.warning.empty(), e.wheel) << GetParam() << ": " << wheel.warning;
    Frames(1);
    const int wheelSteps = e.wheel ? 1 : 0;
    EXPECT_EQ(Device(e).portButtons, ButtonsPort(e, 0x01, wheelSteps)) << GetParam();

    // Click right for 2 frames: held, then released; left stays held
    ASSERT_TRUE(Api()->Click(MouseButton::Right, 2).ok());
    Frames(1);
    EXPECT_EQ(Device(e).portButtons, ButtonsPort(e, 0x03, wheelSteps)) << GetParam();
    Frames(2);
    EXPECT_EQ(Device(e).portButtons, ButtonsPort(e, 0x01, wheelSteps)) << GetParam();

    // Release all
    ASSERT_TRUE(Api()->ReleaseAllButtons().ok());
    Frames(1);
    EXPECT_EQ(Device(e).portButtons, ButtonsPort(e, 0x00, wheelSteps)) << GetParam();

    // A glide: 300 right in steps, the click behind it lands after the last step
    ASSERT_TRUE(Api()->Glide(300, 0).ok());
    ASSERT_TRUE(Api()->Click(MouseButton::Left, 2).queued) << "input after a glide waits for it";
    for (int i = 0; i < 40 && Api()->IsBusy(); i++)
        Frames(1);
    EXPECT_FALSE(Api()->IsBusy()) << GetParam() << ": the glide never ended";
    Frames(4);
    device = Device(e);
    const int scale = device.hasPs2 ? 1 << device.ps2.resolution : 1;
    EXPECT_EQ(device.portX, static_cast<uint8_t>(e.x0 + 5 + 300 * scale)) << GetParam();
    EXPECT_EQ(device.portButtons, ButtonsPort(e, 0x00, wheelSteps)) << GetParam() << ": the queued click ended";
}

/// [INPUT] Mouse=NONE: nothing a program could read, so input is refused with a reason (the WebAPI's 409),
/// never a silent no-op; the Sprinter's board mouse stays
TEST_P(DebugMouseManagerMachines_Test, MouseNoneRefusesOrKeepsTheBoardMouse)
{
    ASSERT_NO_FATAL_FAILURE(Create(true));
    const MouseExpectation e = ExpectationFor(GetParam());
    const MouseStateSnapshot state = Api()->GetState();

    if (e.boardMouse)
    {
        ASSERT_TRUE(state.device.has_value());
        EXPECT_EQ(state.device->id, e.device);
        ASSERT_TRUE(Api()->Move(5, 3).ok());
        Frames(1);
        EXPECT_EQ(Device(e).portX, static_cast<uint8_t>(e.x0 + 5));
        return;
    }

    EXPECT_FALSE(state.mouseFitted) << GetParam();
    EXPECT_FALSE(state.device.has_value()) << GetParam();
    for (const MouseInjectResult& result :
         {Api()->Move(5, 3), Api()->Glide(300, 0), Api()->PressButton(MouseButton::Left),
          Api()->ReleaseButton(MouseButton::Left), Api()->Click(MouseButton::Left), Api()->SetPressedButtons(1),
          Api()->Wheel(1), Api()->ReleaseAllButtons(), Api()->SetCounters(1, 2)})
    {
        EXPECT_EQ(result.status, MouseInjectStatus::NoMouseFitted) << GetParam() << ": " << result.message;
        EXPECT_NE(result.message.find("no mouse fitted"), std::string::npos) << result.message;
    }
    Frames(1);
    EXPECT_EQ(Api()->GetState().portX, 0xFF) << GetParam() << ": nothing drives the mouse ports";
}

INSTANTIATE_TEST_SUITE_P(CreatableModels, DebugMouseManagerMachines_Test,
                         ::testing::Values("PENTAGON", "48K", "128k", "PLUS2", "PLUS2A", "PLUS3", "TSL", "TSL-VDAC2",
                                           "SPRINTER", "ATM3", "ATM710", "ATM450", "PROFI", "PROFI3", "SCORPION",
                                           "PROFSCORP", "ZXPOLY-48K", "ZXPOLY-128K", "ZXPOLY-PENTAGON"),
                         [](const ::testing::TestParamInfo<const char*>& info) {
                             std::string name = info.param;
                             for (char& c : name)
                                 if (c == '-')
                                     c = '_';
                             return name;
                         });

/// region <TTD: a glide on the Kempston mouse replays exactly>


/// 48K, a program logging the Kempston ports in a tight loop (DI): a glide and a click queued behind it are
/// recorded; a seek to the first checkpoint and a run forward (the journal plays the input) reach every recorded
/// checkpoint with the same CPU, devices and log. Over the 50 ms budget (~0.2 s): 40 recorded frames and the replay
TEST(DebugMouseManagerMachinesTtd_Test, KempstonGlideReplaysExactly)
{
    EmulatorManager* manager = EmulatorManager::GetInstance();
    std::string error;
    std::shared_ptr<Emulator> emulator = manager->CreateEmulatorWithModel("mouse-ttd-48k", "48K", LoggerLevel::LogError, &error);
    ASSERT_TRUE(emulator) << error;
    const std::string emulatorId = emulator->GetId();
    struct Cleanup
    {
        EmulatorManager* manager;
        std::string id;
        ~Cleanup() { manager->RemoveEmulator(id); }
    } cleanup{manager, emulatorId};
    EmulatorContext* context = emulator->GetContext();
    ttd::TimeTravelManager* ttd = context->pTimeTravelManager;
    ASSERT_NE(ttd, nullptr);
    DebugMouseManager* api = context->pDebugManager->GetMouseManager();
    Z80* z80 = context->pCore->GetZ80();
    // Run to the next frame boundary exactly (the frame's checkpoint is taken there)
    const auto runToBoundary = [&]() {
        const uint64_t frame = context->emulatorState.frame_counter;
        for (int guard = 0; guard < 4 && context->emulatorState.frame_counter == frame; guard++)
            emulator->RunTStates(z80->t < z80->_frameLimit ? z80->_frameLimit - z80->t : 1u, true);
    };

    emulator->RunNFrames(5, true);
    // 8000: DI; LD HL,#9000; loop: LD BC,#FBDF; IN A,(C); LD (HL),A; INC L; LD B,#FF; IN A,(C); LD (HL),A; INC L;
    //       LD B,#FA; IN A,(C); LD (HL),A; INC L; JR loop
    static const uint8_t program[] = {0xF3, 0x21, 0x00, 0x90, 0x01, 0xDF, 0xFB, 0xED, 0x78, 0x77, 0x2C, 0x06, 0xFF,
                                      0xED, 0x78, 0x77, 0x2C, 0x06, 0xFA, 0xED, 0x78, 0x77, 0x2C, 0x18, 0xEB};
    for (size_t i = 0; i < sizeof(program); i++)
        context->pMemory->DirectWriteToZ80Memory(static_cast<uint16_t>(0x8000 + i), program[i]);
    z80->pc = 0x8000;
    emulator->RunNFrames(2, true);
    runToBoundary();

    FeatureManager* features = emulator->GetFeatureManager();
    features->setFeature(Features::kDebugMode, true);
    features->setFeature(Features::kTimeTravel, true);
    context->pMemory->UpdateFeatureCache();
    ASSERT_TRUE(ttd->StartRecording());
    runToBoundary();
    ASSERT_TRUE(api->Glide(-400, 300).ok());
    ASSERT_TRUE(api->Click(MouseButton::Left, 3).queued);
    for (int i = 0; i < 40; i++)
        runToBoundary();
    ttd->StopRecording();
    EXPECT_FALSE(api->IsBusy());
    EXPECT_EQ(context->pMouse->GetX(), static_cast<uint8_t>(31 - 400));
    EXPECT_EQ(context->pMouse->GetY(), static_cast<uint8_t>(85 + 300));
    EXPECT_EQ(context->pMouse->GetButtons(), 0xFF) << "the queued click ended";

    // Replay: the first checkpoint, then forward through every recorded one
    const size_t count = ttd->GetCheckpointCount();
    ASSERT_GT(count, 30u);
    ASSERT_TRUE(ttd->SeekTo({ttd->GetCheckpoint(0)->time.frame, 0}));
    for (size_t idx = 1; idx < count; idx++)
    {
        runToBoundary();
        const ttd::TTDCheckpoint* cp = ttd->GetCheckpoint(idx);
        ASSERT_EQ(context->emulatorState.frame_counter, cp->time.frame);
        const ttd::TTDCpuState cpu = ttd::CaptureCpuState(*static_cast<const Z80State*>(z80));
        ASSERT_EQ(std::memcmp(&cpu, &cp->cpu, sizeof(cpu)), 0)
            << "checkpoint " << idx << ": CPU (PC #" << std::hex << cpu.pc << " vs #" << cp->cpu.pc << ")";
        std::unordered_map<uint8_t, std::vector<uint8_t>> live;
        ttd->GetPeripheralRegistry().CaptureAll(live);
        for (const auto& [id, blob] : cp->peripheralBlobs)
        {
            ASSERT_NE(live.find(id), live.end());
            ASSERT_EQ(ttd::TTDPeripheralRegistry::DecodeBlob(id, live[id]), ttd::TTDPeripheralRegistry::DecodeBlob(id, blob))
                << "checkpoint " << idx << ": device " << int(id);
        }
    }
    EXPECT_EQ(context->pMouse->GetX(), static_cast<uint8_t>(31 - 400)) << "the replay applied the whole glide";
    EXPECT_EQ(context->pMouse->GetY(), static_cast<uint8_t>(85 + 300));

    emulator.reset();
}

/// endregion
