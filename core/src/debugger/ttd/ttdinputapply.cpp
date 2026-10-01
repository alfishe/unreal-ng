/// @file ttdinputapply.cpp
/// @brief Applies an input event to the machine's input devices. See
/// ttdinputapply.h.

#include "ttdinputapply.h"

#include "emulator/emulatorcontext.h"
#include "emulator/io/keyboard/keyboard.h"  // Keyboard, ZXKeysEnum
#include "emulator/io/keyboard/pckey.h"
#include "emulator/io/joystick/joystick.h"
#include "emulator/io/mouse/mouse.h"
#include "emulator/io/network/virtualnetwork.h"
#include "emulator/sound/chips/gs/generalsoundcard.h"
#include "emulator/sound/soundmanager.h"

namespace ttd {

TTDInputDevices InputDevicesOf(EmulatorContext* context)
{
    TTDInputDevices devices;
    if (!context)
        return devices;
    devices.keyboard = context->pKeyboard;
    devices.mouse = context->pMouse;
    devices.joystick = context->pJoystick;
    devices.generalSound = context->pSoundManager ? context->pSoundManager->getGeneralSound() : nullptr;
    devices.network = context->pVirtualNetwork;
    return devices;
}

bool ApplyInputEvent(const TTDInputEvent& ev, const TTDInputDevices& devices, const TTDNetInput* net,
                     const uint8_t* payload)
{
    Keyboard* keyboard = devices.keyboard;
    Mouse* mouse = devices.mouse;
    Joystick* joystick = devices.joystick;
    GeneralSoundCard* generalSound = devices.generalSound;

    switch (ev.kind)
    {
        case TTDInputKind::Key:
            if (!keyboard)
                return false;
            {
                // ZXKeysEnum is `enum ZXKeysEnum : uint8_t` (unscoped, explicit
                // underlying type). static_cast is the canonical conversion from
                // the underlying integer type back to the enum.
                const auto key = static_cast<ZXKeysEnum>(ev.key);
                if (ev.pressed)
                    keyboard->PressKey(key);
                else
                    keyboard->ReleaseKey(key);
            }
            break;

        case TTDInputKind::KeyboardReset:
            if (!keyboard)
                return false;
            keyboard->Reset();
            keyboard->ReleaseAllPcKeys();  // the PS/2 controller sees the keys go up too
            break;

        case TTDInputKind::PcKey:
            if (!keyboard || !keyboard->WantsPcKey(static_cast<PcKey>(ev.key)))
                return false;
            keyboard->ApplyPcKey(static_cast<PcKey>(ev.key), ev.pressed);
            break;

        case TTDInputKind::MouseMove:
            if (!mouse)
                return false;
            mouse->Move(ev.dx, ev.dy);
            break;

        case TTDInputKind::MouseButtons:
            if (!mouse)
                return false;
            mouse->SetButtons(ev.buttonMask);
            break;

        case TTDInputKind::MouseWheel:
            if (!mouse)
                return false;
            mouse->SetWheel(ev.wheelSteps);
            break;

        case TTDInputKind::MouseCounters:
            if (!mouse)
                return false;
            mouse->SetCounters(static_cast<uint8_t>(ev.dx), static_cast<uint8_t>(ev.dy));
            break;

        case TTDInputKind::Joystick:
            if (!joystick)
                return false;
            joystick->SetState(ev.buttonMask);
            break;

        case TTDInputKind::GSCommand:
            if (!generalSound)
                return false;
            generalSound->sendCommand(ev.value);
            break;

        case TTDInputKind::GSData:
            if (!generalSound)
                return false;
            generalSound->sendData(ev.value);
            break;

        case TTDInputKind::GSNmi:
            if (!generalSound)
                return false;
            generalSound->triggerNMI();
            break;

        case TTDInputKind::GSResetCard:
            if (!generalSound)
                return false;
            generalSound->resetCard();
            break;

        case TTDInputKind::GSReset:
            if (!generalSound)
                return false;
            generalSound->reset();
            break;

        case TTDInputKind::NetEvent:
            if (!devices.network || !net)
                return false;
            devices.network->ApplyHostEvent(*net, payload);
            break;

        case TTDInputKind::NetLinkReset:
            if (!devices.network)
                return false;
            devices.network->ApplyLinkReset();
            break;
    }
    return true;
}

} // namespace ttd
