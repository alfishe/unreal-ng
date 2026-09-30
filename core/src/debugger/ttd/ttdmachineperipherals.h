#pragma once

#include <memory>
#include <string>
#include <vector>

#include "ttdperipheralregistry.h"
#include "ttdserializable.h"

class EmulatorContext;

namespace ttd
{

/// @brief Register every stateful device of a machine with a registry
/// @details The one place that knows which devices a machine has and under
///          which PeripheralId each one saves its state: the core devices
///          (TurboSound slot, Covox, General Sound / NeoGS, MoonSound, tape,
///          Kempston mouse, Beta 128, IDE board) and the model-specific
///          serializers the port decoder supplies. Used by the TTD session
///          (TimeTravelManager) and by MachineStateTransfer, so both see the
///          same device set.
///
///          The registry is cleared first. Devices owned by the emulator are
///          registered by raw pointer; serializers created here (IDE channel,
///          model paging) are appended to ownedSerializers, which must outlive
///          the registry's use.
///
/// @param context   Machine to enumerate
/// @param registry  Registry to fill (cleared first)
/// @param ownedSerializers Receives the serializers created for this registry
/// @param error     Optional; receives the reason on failure
/// @return false when the model declares a state id no serializer provides;
///         the registry and ownedSerializers are then left empty
bool RegisterMachinePeripherals(EmulatorContext* context, TTDPeripheralRegistry& registry,
                                std::vector<std::unique_ptr<TTDSerializable>>& ownedSerializers,
                                std::string* error = nullptr);

} // namespace ttd
