#include "ttdmachineperipherals.h"

#include "debugger/ttd/engine/ttdregiontracker.h"

#include "common/modulelogger.h"
#include "ide/ttdatachannel.h"
#include "ide/ttdcddrive.h"
#include "emulator/io/network/atm2ioesp.h"
#include "network/ttdmachineserialpeer.h"
#include "network/ttdserialport.h"
#include "network/ttdzifi.h"
#include "network/ttdzxnetusb.h"
#include "emulator/io/network/zifi.h"
#include "emulator/emulatorcontext.h"
#include "emulator/io/fdc/wd1793.h"
#include "emulator/io/ide/idecontroller.h"
#include "emulator/io/joystick/joystick.h"
#include "emulator/io/mouse/mouse.h"
#include "emulator/io/tape/tape.h"
#include "emulator/platform.h"
#include "emulator/ports/portdecoder.h"
#include "emulator/sound/chips/gs/generalsoundcard.h"
#include "emulator/sound/chips/iturbosounddevice.h"
#include "emulator/sound/covox.h"
#ifdef UNREALNG_HAVE_OPL4
#include "emulator/sound/chips/soundchip_moonsound.h"
#endif
#include "emulator/sound/soundmanager.h"
#include "stdafx.h"

namespace ttd
{

bool RegisterMachinePeripherals(EmulatorContext* context, TTDPeripheralRegistry& registry,
                                std::vector<std::unique_ptr<TTDSerializable>>& ownedSerializers,
                                std::string* error)
{
    // Idempotent: a restart must not stack duplicate serializers.
    registry.Clear();
    ownedSerializers.clear();

    if (!context)
        return true;

    // Logging goes through the TTD module, as when this code lived in TimeTravelManager
    ModuleLogger* _logger = context->pModuleLogger;
    const PlatformModulesEnum _MODULE = PlatformModulesEnum::MODULE_DEBUGGER;
    const uint16_t _SUBMODULE = 0x0000;  // TTD has no submodule id of its own yet

    // Core devices, present (or absent) independently of the model. They are
    // owned by the emulator, so they are registered by raw pointer. A device
    // that is absent on this model leaves no entry at all, which is the whole
    // point of a registry: a checkpoint carries blobs only for what is
    // actually connected.
    if (context->pSoundManager)
    {
        // TurboSound slot: register under the live device's own peripheral
        // id (legacy TurboSound = 0, TSFM = 4) so a session recorded on one
        // device cannot load on the other (design §8.2). An empty slot
        // (TurboSound = None) registers nothing, same as an absent Covox.
        if (ITurboSoundDevice* turboSoundDevice = context->pSoundManager->getTurboSound())
            registry.Register(turboSoundDevice->TTDPeripheralId(), turboSoundDevice);
        registry.Register(PeripheralId::Covox, context->pSoundManager->getCovox());
        // General Sound card ([SOUND] GSType, GS design §5.3): absent when
        // the config did not fit one. Registered under the live card's own
        // peripheral id, same pattern as the TurboSound slot: LLE
        // (GeneralSound), LW (GeneralSoundLightweight) and NeoGS are
        // different slots, so state saved on one personality cannot silently
        // restore into another.
        if (GeneralSoundCard* gs = context->pSoundManager->getGeneralSound())
        {
            registry.Register(gs->TTDPeripheralId(), gs);
            // Card memory recorded as engine regions (NeoGS RAM and flash)
            registry.RegisterRegionSource(dynamic_cast<ITTDRegionSource*>(gs));
        }
#ifdef UNREALNG_HAVE_OPL4
        // MoonSound registers only when the config flag built it; a null
        // pointer leaves no entry, so state from a MoonSound machine meets a
        // MoonSound-less one as a visible missing blob (R7).
        registry.Register(PeripheralId::MoonSound, context->pSoundManager->getMoonSound());
        registry.RegisterRegionSource(context->pSoundManager->getMoonSound());   // wave RAM as an engine region
#endif
    }
    registry.Register(PeripheralId::Tape, context->pTape);
    // Kempston Mouse: core device on every model (design §6.1 - not a model-specific latch)
    registry.Register(PeripheralId::KempstonMouse, context->pMouse);
    // Kempston joystick: the state byte, only on machines whose decoder answers #1F (a machine without the arm
    // cannot observe it, and its checkpoints stay as they were)
    if (context->pPortDecoder && context->pPortDecoder->HasKempstonJoystick())
        registry.Register(PeripheralId::KempstonJoystick, context->pJoystick);
    registry.Register(PeripheralId::BetaDisk, context->pBetaDisk);

    // IDE board (any machine with [HDD] Scheme): controller state, not the media
    if (context->pIdeController && context->pIdeController->Enabled())
    {
        auto ide = std::make_unique<TTDAtaChannel>(context);
        registry.Register(PeripheralId::AtaChannel, ide.get());
        ownedSerializers.push_back(std::move(ide));
        // Its CD drives' audio and READ CD staging: only when a unit is a CD drive
        if (context->pIdeController->CdUnitMask())
        {
            auto cd = std::make_unique<TTDCdDrive>(context);
            registry.Register(PeripheralId::CdDrive, cd.get());
            ownedSerializers.push_back(std::move(cd));
        }
    }

    // Network adapters (network TDD §6.3, §7): while the virtual network
    // exists; the serial port on #xxEF in its own blob (on a ZX-Evo always:
    // the AVR's UART is on the mainboard), restored after the network
    if (context->pZxNetUsb || context->pVirtualNetwork)
    {
        auto network = std::make_unique<TTDZxNetUsb>(context);
        registry.Register(PeripheralId::ZxNetUsb, network.get());
        ownedSerializers.push_back(std::move(network));
    }
    if (context->pComPort)
    {
        auto serial = std::make_unique<TTDSerialPort>(context);
        registry.Register(PeripheralId::SerialPort, serial.get());
        ownedSerializers.push_back(std::move(serial));
    }
    if (context->pAtm2IoEsp)
    {
        auto card = std::make_unique<TTDSerialPort>(
            context, [context]() { return context->pAtm2IoEsp ? &context->pAtm2IoEsp->Com() : nullptr; },
            PeripheralId::Atm2IoEsp, "Atm2IoEsp");
        registry.Register(PeripheralId::Atm2IoEsp, card.get());
        ownedSerializers.push_back(std::move(card));
    }
    if (context->pZiFi)
    {
        auto line = std::make_unique<TTDSerialPort>(
            context, [context]() { return context->pZiFi ? &context->pZiFi->Line() : nullptr; },
            PeripheralId::ZiFiLine, "ZiFiLine");
        registry.Register(PeripheralId::ZiFiLine, line.get());
        ownedSerializers.push_back(std::move(line));
        auto zifi = std::make_unique<TTDZiFi>(context);
        registry.Register(PeripheralId::ZiFi, zifi.get());
        ownedSerializers.push_back(std::move(zifi));
    }
    if (context->pMachineSerialPeer)
    {
        auto peer = std::make_unique<TTDMachineSerialPeer>(context);
        registry.Register(PeripheralId::MachineSerialPeer, peer.get());
        ownedSerializers.push_back(std::move(peer));
    }

    // --- Model-specific state (TDD 6.4) ---
    // The framework names no machine. The port decoder owns the model's
    // latches, so it declares what extra state exists and supplies the
    // serializers; we only check that the two agree.
    PortDecoder* decoder = context->pPortDecoder;
    if (!decoder)
        return true;

    for (auto& serializer : decoder->CreateTTDSerializers())
    {
        if (!serializer)
            continue;

        registry.Register(serializer->TTDPeripheralId(), serializer.get());
        // A model serializer whose memory the engine records as regions (Sprinter video and fast RAM)
        registry.RegisterRegionSource(dynamic_cast<ITTDRegionSource*>(serializer.get()));
        ownedSerializers.push_back(std::move(serializer));
    }

    // A declared id with no serializer behind it means this model's state
    // would be dropped silently - a recording that looks correct and restores
    // wrong. Refuse instead, naming what is missing.
    for (PeripheralId id : decoder->GetTTDModelStateIds())
    {
        if (registry.IsRegistered(id))
            continue;

        const std::string message =
            "model (mem_model=" + std::to_string(static_cast<unsigned>(context->config.mem_model)) +
            ") declares TTD state id " + std::to_string(static_cast<unsigned>(id)) +
            " but its port decoder supplied no serializer for it - recording would "
            "silently drop that state. Implement CreateTTDSerializers() for this model.";

        MLOGERROR("RegisterMachinePeripherals - %s", message.c_str());
        if (error)
            *error = message;

        registry.Clear();
        ownedSerializers.clear();
        return false;
    }

    if (registry.Count() > 0)
    {
        MLOGINFO("RegisterMachinePeripherals - %zu serializer(s) registered for mem_model=%u", registry.Count(),
                 static_cast<unsigned>(context->config.mem_model));
    }

    return true;
}

} // namespace ttd
