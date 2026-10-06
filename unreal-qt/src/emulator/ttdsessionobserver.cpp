#include "ttdsessionobserver.h"

#include <QCoreApplication>

#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"

namespace TtdSessionObserver
{
std::optional<ttd::TTDSessionInfo> Read(Emulator* emulator)
{
    if (!emulator)
        return std::nullopt;
    const Emulator::ContextLease lease = emulator->LeaseContext();
    if (!lease || !lease->pTimeTravelHooks)
        return std::nullopt;
    return lease->pTimeTravelHooks->GetPublishedSessionInfo();
}

QString CaptureToolTip(const ttd::TTDSessionInfo& info)
{
    const uint64_t startFrame = info.sessionStartFrame;
    const uint64_t curFrame = info.currentEndFrame;
    const uint64_t totalFrames = (curFrame >= startFrame) ? (curFrame - startFrame) : 0;
    const int totalSec = static_cast<int>(totalFrames / 50);  // 50 FPS PAL/Spectrum standard
    const int hh = totalSec / 3600;
    const int mm = (totalSec % 3600) / 60;
    const int ss = totalSec % 60;
    const QString timeStr = QString::asprintf("%02d:%02d:%02d", hh, mm, ss);

    return QCoreApplication::translate("ToolBarManager", "Capturing\nTime: %1 | Frames: %2 - %3\nMemory: %4")
        .arg(timeStr)
        .arg(startFrame)
        .arg(curFrame)
        .arg(FormatMemorySize(info.sessionHeapBytes));
}

QString FormatMemorySize(uint64_t bytes)
{
    const double mb = static_cast<double>(bytes) / (1024.0 * 1024.0);
    if (mb >= 1.0)
        return QString::asprintf("%.1f MB", mb);
    if (bytes >= 1024)
        return QString::asprintf("%.1f KB", static_cast<double>(bytes) / 1024.0);
    return QString::asprintf("%llu B", static_cast<unsigned long long>(bytes));
}
}  // namespace TtdSessionObserver
