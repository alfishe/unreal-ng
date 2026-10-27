#pragma once

/// @file ttdrecordedstate.h
/// @brief What the engine recorded for a device at a checkpoint: the test-side
/// read of a recording's device state (Phase 5 Step 4b). v1 kept each device's
/// state as a blob in its checkpoint (`TTDCheckpoint::peripheralBlobs`); the
/// engine keeps it in its own store, read here by the controller's checkpoint
/// index (0 = the earliest held checkpoint, as `GetCheckpoint`).

#include <cstddef>
#include <cstdint>
#include <vector>

#include "debugger/ttd/timetravelcontroller.h"

namespace ttdtest
{
/// The recorded state of the device v1 knows as @p id at checkpoint @p index;
/// empty when the device is not in the recording or had no state there
inline std::vector<uint8_t> RecordedDeviceState(const ttd::TimeTravelController& controller, size_t index,
                                                ttd::PeripheralId id)
{
    std::vector<uint8_t> state;
    const ttd::TimeTravelEngine& engine = controller.GetEngine();
    if (!engine.DeviceStateOfV1Id(engine.FirstCheckpoint() + index, static_cast<uint8_t>(id), state))
        state.clear();
    return state;
}

/// The same for device @p key (a device without a v1 id, one of several instances of a type)
inline std::vector<uint8_t> RecordedDeviceState(const ttd::TimeTravelController& controller, size_t index,
                                                const ttd::TTDDeviceKey& key)
{
    std::vector<uint8_t> state;
    const ttd::TimeTravelEngine& engine = controller.GetEngine();
    if (!engine.DeviceState(engine.FirstCheckpoint() + index, key, state))
        state.clear();
    return state;
}
}  // namespace ttdtest
