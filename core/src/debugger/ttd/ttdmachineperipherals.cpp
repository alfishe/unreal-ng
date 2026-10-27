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
#include "network/ttdethernetnics.h"
#include "emulator/cpu/core.h"
#include "emulator/io/network/networkmanager.h"
#include "emulator/io/network/zifi.h"
#include "emulator/emulatorcontext.h"
#include "debugger/ttd/ttdwd1793context.h"
#include "emulator/io/fdc/wd1793.h"
#include "emulator/io/ide/idecontroller.h"
#include "emulator/io/controllers/controllermanager.h"
#include "emulator/io/joystick/joystick.h"
#include "emulator/io/mouse/mouse.h"
#include "emulator/emulator.h"
#include "emulator/io/tape/tape.h"
#include "emulator/rzx/rzxttdstate.h"
#include "emulator/platform.h"
#include "emulator/ports/portdecoder.h"
#include "emulator/slots/card.h"
#include "emulator/slots/slotmanager.h"
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
        // The slot cards (ZX-bus slots SL-5): the slot set decides which exist, SoundManager built them. Each is
        // registered under its own type and named by its slot in the engine's device table ("zxbus.1.neogs");
        // after the sound devices the registry is checked against the slot set.
        //
        // TurboSound slot: the live device's own type (legacy TurboSound = 0,
        // TSFM = 4) so a session recorded on one device cannot load on the
        // other (design §8.2). An empty slot (TurboSound = None) registers
        // nothing, same as an absent Covox.
        if (ITurboSoundDevice* turboSoundDevice = context->pSoundManager->getTurboSound())
            registry.Register(turboSoundDevice, SlotCardInstance(context, turboSoundDevice));
        registry.Register(context->pSoundManager->getCovox());
        // General Sound card ([SOUND] GSType, GS design §5.3): absent when
        // the config did not fit one. Registered under the live card's own
        // type, same pattern as the TurboSound slot: LLE (GeneralSound), LW
        // (GeneralSoundLightweight) and NeoGS are different types, so state
        // saved on one personality cannot silently restore into another.
        if (GeneralSoundCard* gs = context->pSoundManager->getGeneralSound())
        {
            // The lightweight card (a mod player, no coprocessor) is not recorded:
            // it runs live through seeks, and the session file names it as fitted
            SwitchMachinePeripheral(context, registry, gs->TTDType(), gs);
            if (gs->TTDType() != TTDDeviceType::GeneralSoundLightweight)
                registry.RegisterRegionSource(dynamic_cast<ITTDRegionSource*>(gs));   // NeoGS RAM and flash
        }
#ifdef UNREALNG_HAVE_OPL4
        // MoonSound registers only when the config flag built it; a null
        // pointer leaves no entry, so state from a MoonSound machine meets a
        // MoonSound-less one as a visible missing blob (R7).
        registry.Register(context->pSoundManager->getMoonSound(),
                          SlotCardInstance(context, context->pSoundManager->getMoonSound()));
        registry.RegisterRegionSource(context->pSoundManager->getMoonSound());   // wave RAM as an engine region
#endif
        // Cards the slots build themselves (card.h; the ZX-MultiSound): each of their devices under its own type,
        // named by the card's slot ("zxbus.1.multisound", "zxbus.1.multisound.gs"); memories as engine regions
        if (const SlotManager* slotManager = context->pSlotManager)
        {
            std::vector<CardTtdDevice> devices;
            for (const std::unique_ptr<ICard>& card : slotManager->Cards())
            {
                devices.clear();
                card->CollectTtdDevices(devices);
                for (const CardTtdDevice& device : devices)
                {
                    registry.Register(device.device, device.instance);
                    registry.RegisterRegionSource(device.regions);
                }
            }
        }
        // The registry against the slot set: a card the plan fits has its device, a device has its card
        // (a build without OPL4 builds no MoonSound: that group is not compared there)
        if (const SlotManager* slotManager = context->pSlotManager; slotManager && slotManager->Current().machine)
        {
            SlotManager::TtdDeviceSet live = SlotManager::TtdDeviceSet::Of(registry);
#ifndef UNREALNG_HAVE_OPL4
            if (slotManager->Current().FindGroup(SlotCardGroup::MoonSound))
                live.devices.push_back({TTDDeviceType::MoonSound, {}});
#endif
            std::string why;
            if (!SlotManager::TtdDevicesMatchPlan(slotManager->Current(), live, why))
            {
                MLOGERROR("RegisterMachinePeripherals - %s", why.c_str());
                if (error)
                    *error = why;
                registry.Clear();
                ownedSerializers.clear();
                return false;
            }
        }
    }
    registry.Register(context->pTape);
    // Kempston Mouse: core device on every model (design §6.1 - not a model-specific latch)
    registry.Register(context->pMouse);
    registry.Register(context->pKeyboard);
    // An RZX recording played on this machine: the playback position (a new
    // playback cannot start while TTD records, so the set stays fixed)
    if (context->pEmulator)
        if (rzx::RzxSession* rzxSession = context->pEmulator->LoadedRzxSession())
        {
            auto rzxState = std::make_unique<rzx::RzxTtdState>(*rzxSession);
            registry.Register(rzxState.get());
            ownedSerializers.push_back(std::move(rzxState));
        }
    // Kempston joystick: the state byte, only on machines whose decoder answers #1F (a machine without the arm
    // cannot observe it, and its checkpoints stay as they were)
    if (context->pPortDecoder && context->pPortDecoder->HasKempstonJoystick())
        registry.Register(context->pJoystick);
    // Keyboard joysticks: every machine has the sockets; their plugs and held controls (a plug made while recording
    // lands at a boundary, and a seek before it takes it back)
    if (context->pControllerManager)
        registry.Register(context->pControllerManager);
    registry.Register(context->pBetaDisk);
    // The WD1793's command in flight (queued steps, transfer pointers): a
    // restore inside a multi-frame command (an ID search, a sector half read)
    // continues it instead of ending it Not Ready. Every Beta machine
    if (context->pBetaDisk)
    {
        auto wdContext = std::make_unique<TTDWd1793Context>(*context->pBetaDisk);
        registry.Register(wdContext.get());
        ownedSerializers.push_back(std::move(wdContext));
    }

    // IDE board (any machine with [HDD] Scheme): controller state, not the media
    if (context->pIdeController && context->pIdeController->Enabled())
    {
        auto ide = std::make_unique<TTDAtaChannel>(context);
        registry.Register(ide.get());
        ownedSerializers.push_back(std::move(ide));
        // Its CD drives' audio and READ CD staging: only when a unit is a CD drive
        if (context->pIdeController->CdUnitMask())
        {
            auto cd = std::make_unique<TTDCdDrive>(context);
            registry.Register(cd.get());
            ownedSerializers.push_back(std::move(cd));
        }
    }

    // Network adapters (network TDD §6.3, §7): while the virtual network
    // exists; the serial port on #xxEF in its own blob (on a ZX-Evo always:
    // the AVR's UART is on the mainboard), restored after the network
    if (context->pZxNetUsb || context->pVirtualNetwork)
    {
        auto network = std::make_unique<TTDZxNetUsb>(context);
        registry.Register(network.get());
        ownedSerializers.push_back(std::move(network));
    }
    if (context->pComPort)
    {
        auto serial = std::make_unique<TTDSerialPort>(context);
        registry.Register(serial.get());
        ownedSerializers.push_back(std::move(serial));
    }
    if (context->pAtm2IoEsp)
    {
        auto card = std::make_unique<TTDSerialPort>(
            context, [context]() { return context->pAtm2IoEsp ? &context->pAtm2IoEsp->Com() : nullptr; },
            PeripheralId::Atm2IoEsp, "Atm2IoEsp");
        registry.Register(card.get());
        ownedSerializers.push_back(std::move(card));
    }
    if (context->pZiFi)
    {
        auto line = std::make_unique<TTDSerialPort>(
            context, [context]() { return context->pZiFi ? &context->pZiFi->Line() : nullptr; },
            PeripheralId::ZiFiLine, "ZiFiLine");
        registry.Register(line.get());
        ownedSerializers.push_back(std::move(line));
        auto zifi = std::make_unique<TTDZiFi>(context);
        registry.Register(zifi.get());
        ownedSerializers.push_back(std::move(zifi));
    }
    // Network cards in expansion slots (the Sprinter's ISA NE2000): hardware, there with the network off too
    if (context->pCore && context->pCore->GetNetworkManager() && !context->pCore->GetNetworkManager()->SlotCards().empty())
    {
        auto nics = std::make_unique<TTDEthernetNics>(context);
        registry.Register(nics.get());
        ownedSerializers.push_back(std::move(nics));
    }
    // UART cards in expansion slots (the Sprinter's SprinterESP): hardware, there with the network off too
    if (context->pCore && context->pCore->GetNetworkManager())
    {
        NetworkManager* manager = context->pCore->GetNetworkManager();
        for (int n = 0; n < 2; ++n)
        {
            const std::string slot = "isa" + std::to_string(n + 1);
            if (!manager->SerialCard(slot))
                continue;
            // One blob per UART: channel A under SlotSerial1 / 2, a second channel (SprinterSerial's COM2) under
            // SlotSerial1B / 2B
            for (int ch = 0; ch < manager->SerialCard(slot)->Channels(); ++ch)
            {
                const PeripheralId id = ch == 0 ? (n == 0 ? PeripheralId::SlotSerial1 : PeripheralId::SlotSerial2)
                                                : (n == 0 ? PeripheralId::SlotSerial1B : PeripheralId::SlotSerial2B);
                auto serial = std::make_unique<TTDSerialPort>(
                    context,
                    [manager, slot, ch]() {
                        PcSerialCard* card = manager->SerialCard(slot);
                        return card && ch < card->Channels() ? &card->Com(ch) : nullptr;
                    },
                    id, std::string(n == 0 ? "SlotSerial1" : "SlotSerial2") + (ch ? "B" : ""));
                registry.Register(serial.get());
                ownedSerializers.push_back(std::move(serial));
            }
        }
    }
    if (context->pMachineSerialPeer)
    {
        auto peer = std::make_unique<TTDMachineSerialPeer>(context);
        registry.Register(peer.get());
        ownedSerializers.push_back(std::move(peer));
    }
    // The second port's peer (the Next's UART 1, the Pi's line) and the joystick connector's (NR #0B UART mode) have
    // blobs of their own: ids 64 and 65, which the session file's wide peripheral mask names
    if (context->pMachineSerialPeer2)
    {
        auto peer = std::make_unique<TTDMachineSerialPeer>(context, TTDMachineSerialPeer::Port::Second);
        registry.Register(peer.get());
        ownedSerializers.push_back(std::move(peer));
    }
    if (context->pJoystickSerialPeer)
    {
        auto peer = std::make_unique<TTDMachineSerialPeer>(context, TTDMachineSerialPeer::Port::Joystick);
        registry.Register(peer.get());
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

        registry.Register(serializer.get());
        // A model serializer whose memory the engine records as regions (Sprinter video and fast RAM)
        registry.RegisterRegionSource(dynamic_cast<ITTDRegionSource*>(serializer.get()));
        ownedSerializers.push_back(std::move(serializer));
    }

    // Model memories the engine records as regions without a serializer of their own
    std::vector<ITTDRegionSource*> modelRegionSources;
    decoder->CollectTTDRegionSources(modelRegionSources);
    for (ITTDRegionSource* source : modelRegionSources)
        registry.RegisterRegionSource(source);

    // A declared type with no serializer behind it means this model's state
    // would be dropped silently - a recording that looks correct and restores
    // wrong. Refuse instead, naming what is missing.
    for (TTDDeviceType type : decoder->GetTTDModelStateTypes())
    {
        if (registry.IsRegistered(type))
            continue;

        const std::string message =
            "model (mem_model=" + std::to_string(static_cast<unsigned>(context->config.mem_model)) +
            ") declares TTD state type " + std::to_string(static_cast<unsigned>(type)) +
            " but its port decoder supplied no serializer for it - recording would "
            "silently drop that state. Implement CreateTTDSerializers() for this model.";

        MLOGERROR("RegisterMachinePeripherals - %s", message.c_str());
        if (error)
            *error = message;

        registry.Clear();
        ownedSerializers.clear();
        return false;
    }

    // The engine's device table from the same devices: a device that does not
    // match its own descriptor, a dependency on a device this machine lacks
    // or a time field outside the state refuses recording here, named,
    // instead of the engine refusing its session later without a word
    std::string tableError;
    if (!registry.CheckDeviceTable(tableError))
    {
        const std::string message = "device table: " + tableError;
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

std::string SlotCardInstance(const EmulatorContext* context, const TTDSerializable* device)
{
    if (!context || !device || !context->pSlotManager)
        return {};
    SlotCardGroup group = SlotCardGroup::Count;
    switch (device->TTDType())
    {
        case TTDDeviceType::TurboSound:
        case TTDDeviceType::TSFM:
            group = SlotCardGroup::Socket;
            break;
        case TTDDeviceType::GeneralSound:
        case TTDDeviceType::GeneralSoundLightweight:
        case TTDDeviceType::NeoGS:
            group = SlotCardGroup::GeneralSound;
            break;
        case TTDDeviceType::MoonSound:
            group = SlotCardGroup::MoonSound;
            break;
        default:
            return {};
    }
    return context->pSlotManager->TtdInstance(group, device->TTDDeviceName());
}

TTDDeviceKey MachineDeviceKey(const EmulatorContext* context, const TTDSerializable* device)
{
    TTDDeviceKey key = device->TTDDescribe().Key();
    const std::string slotted = SlotCardInstance(context, device);
    if (!slotted.empty())
        key.instance = slotted;
    return key;
}

void SwitchMachinePeripheral(const EmulatorContext* context, TTDPeripheralRegistry& registry, TTDDeviceType oldType,
                             TTDSerializable* device)
{
    registry.UnregisterType(oldType);
    if (!device)
        return;
    if (device->TTDType() == TTDDeviceType::GeneralSoundLightweight)
        registry.MarkNotRecorded(MachineDeviceKey(context, device));
    else
        registry.Register(device, SlotCardInstance(context, device));
}

} // namespace ttd
