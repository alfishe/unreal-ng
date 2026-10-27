#pragma once

/// @file networksettings.h
/// @brief Network settings as every surface applies them (SlotControl verb `network`, ZX-bus slots owner decision
/// Q11): a change of the ZX-bus cards (card=zxnetusb / zxwifi) restarts the machine with the new slot set and applies
/// the other keys to it; without a card change the settings apply in place. The fixture's emulator pointer follows a
/// restart, and the old machine is gone (the restart removes it).
///
/// Example:
/// @code
///   std::shared_ptr<Emulator> emulator = ...;   // a Pentagon without network cards
///   const SlotControlReply reply = NetworkSettings::Apply(emulator, {{"card", "zxwifi"}, {"zx_wifi", "loopback"}});
///   ASSERT_TRUE(reply.Ok()) << reply.message;   // emulator is the restarted machine now
/// @endcode

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "emulator/emulator.h"
#include "emulator/emulatormanager.h"
#include "emulator/slots/slotcontrol.h"

namespace NetworkSettings
{

inline SlotControlReply Apply(std::shared_ptr<Emulator>& emulator,
                              const std::vector<std::pair<std::string, std::string>>& settings,
                              bool replaceIfIncompatible = false)
{
    SlotControlRequest request;
    request.verb = "network";
    request.emulatorId = emulator ? emulator->GetId() : std::string();
    request.settings = settings;
    request.replaceIfIncompatible = replaceIfIncompatible;
    request.startWhenRunning = false;
    std::shared_ptr<Emulator> old = std::move(emulator);   // nothing may keep the old machine alive across a restart
    const std::string oldId = request.emulatorId;
    old.reset();
    SlotControlReply reply = SlotControl::Execute(request);
    emulator = reply.emulator ? reply.emulator : EmulatorManager::GetInstance()->GetEmulator(oldId);
    return reply;
}

} // namespace NetworkSettings
