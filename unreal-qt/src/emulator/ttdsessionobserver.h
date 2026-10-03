#pragma once

#include <QString>
#include <cstdint>
#include <optional>

#include "debugger/ttd/timetravelmanager.h"

class Emulator;

/// What the UI's polling timers (the toolbar's TTD tooltip, the TTD widget's
/// telemetry) read of an instance's time travel session.
///
/// Two threads are in the way: the machine's thread grows, trims and frees the
/// timeline while it records, and an automation thread may stop, invalidate or
/// remove the instance at any moment. So a read holds a context lease
/// (Emulator::LeaseContext: the TimeTravelManager stays alive for the call, and
/// the read is refused once a removal has begun) and takes the manager's
/// published snapshot (TimeTravelManager::GetPublishedSessionInfo), never the
/// live session (2026-10-03 crash: GetHeapBreakdown walked a timeline freed
/// under the toolbar's tooltip timer).
namespace TtdSessionObserver
{
/// nullopt: no instance, the instance is being removed (or released), or it has no TTD
std::optional<ttd::TTDSessionInfo> Read(Emulator* emulator);

/// The toolbar's tooltip while TTD records: "Capturing", time, frame range, memory
QString CaptureToolTip(const ttd::TTDSessionInfo& info);

/// "12.3 MB" / "4.0 KB" / "512 B"
QString FormatMemorySize(uint64_t bytes);
}  // namespace TtdSessionObserver
