#include "stdafx.h"
#include "gtest/gtest.h"

#include "_helpers/testwaithelper.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/emulatormanager.h"
#include "emulator/io/mouse/mouse.h"
#include "emulator/memory/memory.h"
#include "emulator/ports/models/portdecoder_scorpion256.h"

/// @brief Kempston Mouse on ZS Scorpion 256 + ProfROM, end to end: host events
///        posted through MessageCenter with the emulator UUID must drive the
///        ProfROM's own pointer.
///
/// ProfROM facts these tests rest on (scorp_prof401.rom, page 5):
///  - #E03B (IY+#27) pointer status: bit 7 pointer active, bit 6 Kempston joystick,
///    bit 5 Kempston mouse. #E03C / #E03D pointer X / Y. #E12C / #E12D previous raw
///    X / (negated) Y latches of the delta integrator (sub_021bh / sub_0244h).
///  - Mouse detection at page 5 #08FB:
///        push bc / ld bc,#FADF / in a,(c) / pop bc
///        and #38 / cp #38 / ret nc / res 5,(iy+#27) / ret
///    i.e. bits 5-3 of the button register must read 1 or the mouse is switched off.
///    A classic (wheel-less) Kempston mouse reads #FF-based: D7-D3 = 1.
///  - These variables live in RAM page 8: the ProfROM keeps #1FFD bit4 set
///    (bank 8 at #C000) whenever it touches them, so the tests read the physical
///    page rather than whatever happens to be paged at #C000 at a frame boundary.
namespace
{
constexpr uint16_t PORT_BUTTONS = 0xFADF;

constexpr uint8_t PROFROM_RAM_PAGE = 8;
constexpr uint16_t OFF_POINTER_STATUS = 0x203B;  // #E03B
constexpr uint16_t OFF_POINTER_X = 0x203C;       // #E03C
constexpr uint16_t OFF_POINTER_Y = 0x203D;       // #E03D
constexpr uint8_t POINTER_MOUSE_ENABLED = 0x20;  // #E03B bit 5

constexpr int kColdBootFrames = 400;       // detection runs at ~frame 111; BASIC is up well before 400
constexpr int kMonitorSettleFrames = 300;  // bound only; the monitor pages itself in within ~50 frames

std::shared_ptr<Emulator> CreateProfScorp()
{
    std::shared_ptr<Emulator> emulator =
        EmulatorManager::GetInstance()->CreateEmulatorWithModel("", "PROFSCORP", LoggerLevel::LogError);
    if (!emulator)
        return nullptr;

    // Deterministic RTC: live host time shifts the ProfROM boot timeline
    static_cast<PortDecoder_Scorpion256*>(emulator->GetContext()->pPortDecoder)->GetSMUCNvram().SetFixedTime(1767268830);

    // Host-side fast-forward only (no emulated-state effect)
    emulator->EnableTurboMode();
    return emulator;
}

uint8_t ProfRomVar(const std::shared_ptr<Emulator>& emulator, uint16_t pageOffset)
{
    return emulator->GetContext()->pMemory->RAMPageAddress(PROFROM_RAM_PAGE)[pageOffset];
}

/// Magic button after a completed cold boot, then wait for the service monitor's paging (#1FFD = #12, #7FFD = #10)
bool EnterServiceMonitor(const std::shared_ptr<Emulator>& emulator)
{
    EmulatorState& state = emulator->GetContext()->emulatorState;
    emulator->RequestMNI();
    for (int frame = 0; frame < kMonitorSettleFrames; frame++)
    {
        emulator->RunNFrames(1);
        if (state.p1FFD == 0x12 && state.p7FFD == 0x10 && frame >= 50)
            return true;
    }
    return false;
}
}  // namespace

/// Port level: the idle button register passes the ProfROM detection mask, and
/// pressing buttons (low bits) must not break it
TEST(ScorpionKempstonMouse_Test, ButtonRegisterPassesProfRomDetectionMask)
{
    std::shared_ptr<Emulator> emulator = CreateProfScorp();
    ASSERT_TRUE(emulator);
    EmulatorContext* context = emulator->GetContext();
    const std::string target = emulator->GetId();

    uint8_t idle = context->pPortDecoder->DecodePortIn(PORT_BUTTONS, 0x0000);
    EXPECT_EQ(idle & 0x38, 0x38) << "ProfROM #08FB: bits 5-3 of #FADF must read 1 or the mouse is disabled (read "
                                 << std::hex << static_cast<int>(idle) << ")";
    EXPECT_EQ(idle & 0x07, 0x07) << "no button pressed";

    MessageCenter::DefaultMessageCenter().Post(MC_MOUSE_BUTTON, MouseEvent::Buttons(0xF8, target));  // all three down
    ASSERT_TRUE(TestWait::For([&] { return (context->pPortDecoder->DecodePortIn(PORT_BUTTONS, 0x0000) & 0x07) == 0x00; }));
    EXPECT_EQ(context->pPortDecoder->DecodePortIn(PORT_BUTTONS, 0x0000) & 0x38, 0x38)
        << "pressed buttons must not affect the detection bits";

    EmulatorManager::GetInstance()->RemoveEmulator(target);
}

/// Firmware level: after a cold boot the ProfROM's own detection keeps the mouse enabled
TEST(ScorpionKempstonMouse_Test, ProfRomColdBootKeepsMouseEnabled)
{
    std::shared_ptr<Emulator> emulator = CreateProfScorp();
    ASSERT_TRUE(emulator);

    emulator->RunNFrames(kColdBootFrames);

    const uint8_t status = ProfRomVar(emulator, OFF_POINTER_STATUS);
    EXPECT_NE(status & 0x80, 0) << "pointer subsystem initialised (#E03B=" << std::hex << static_cast<int>(status) << ")";
    EXPECT_NE(status & POINTER_MOUSE_ENABLED, 0)
        << "ProfROM detection switched the Kempston mouse off (#E03B=" << std::hex << static_cast<int>(status) << ")";

    EmulatorManager::GetInstance()->RemoveEmulator(emulator->GetId());
}

/// End to end: service monitor pointer follows MessageCenter mouse moves tagged with the emulator UUID
TEST(ScorpionKempstonMouse_Test, ProfRomServiceMonitorPointerFollowsMouse)
{
    std::shared_ptr<Emulator> emulator = CreateProfScorp();
    ASSERT_TRUE(emulator);
    const std::string target = emulator->GetId();

    emulator->RunNFrames(kColdBootFrames);
    ASSERT_TRUE(EnterServiceMonitor(emulator)) << "service monitor did not page in after the magic button";
    ASSERT_NE(ProfRomVar(emulator, OFF_POINTER_STATUS) & POINTER_MOUSE_ENABLED, 0)
        << "mouse disabled in the monitor - the pointer cannot follow";

    const uint8_t startX = ProfRomVar(emulator, OFF_POINTER_X);
    const uint8_t startY = ProfRomVar(emulator, OFF_POINTER_Y);

    // Right and down on the host screen: X counter up, Y counter down (Kempston Y grows upward).
    // The ProfROM negates Y, so the pointer's screen Y must grow
    for (int step = 0; step < 4; step++)
    {
        MessageCenter::DefaultMessageCenter().Post(MC_MOUSE_MOVE, MouseEvent::Move(6, -4, target));
        emulator->RunNFrames(10);
    }

    const uint8_t endX = ProfRomVar(emulator, OFF_POINTER_X);
    const uint8_t endY = ProfRomVar(emulator, OFF_POINTER_Y);
    EXPECT_GT(endX, startX) << "pointer X did not follow the mouse (" << static_cast<int>(startX) << " -> "
                            << static_cast<int>(endX) << ")";
    EXPECT_GT(endY, startY) << "pointer Y did not follow the mouse (" << static_cast<int>(startY) << " -> "
                            << static_cast<int>(endY) << ")";

    // Left and up returns it
    for (int step = 0; step < 4; step++)
    {
        MessageCenter::DefaultMessageCenter().Post(MC_MOUSE_MOVE, MouseEvent::Move(-6, 4, target));
        emulator->RunNFrames(10);
    }
    EXPECT_LT(ProfRomVar(emulator, OFF_POINTER_X), endX);
    EXPECT_LT(ProfRomVar(emulator, OFF_POINTER_Y), endY);

    EmulatorManager::GetInstance()->RemoveEmulator(target);
}
