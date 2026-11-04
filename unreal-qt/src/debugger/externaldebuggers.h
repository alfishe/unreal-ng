#pragma once

#include <QString>

/// Debug > Debugger: the separate debuggers (debuggers/README.md) started over this window's emulator through the
/// WebAPI - eCat3's unreal-debugger (--connect) and the terminal ones, unreal-tui-debugger[-tsconf] (--endpoint),
/// each with --emulator <id>, so it attaches to this window's instance and no other.
namespace ExternalDebuggers
{
/// The program file name without the extension: unreal-debugger, unreal-tui-debugger, unreal-tui-debugger-tsconf
inline constexpr const char* kEcat = "unreal-debugger";
inline constexpr const char* kTui = "unreal-tui-debugger";
inline constexpr const char* kTuiTsConf = "unreal-tui-debugger-tsconf";

/// The program beside unreal-qt (UnrealNG.app/Contents/MacOS, the Windows ZIP, the AppImage's usr/bin, the DEB's
/// lib/unreal-ng) or beside its bundle (a build tree: bin/unreal-qt.app/Contents/MacOS -> bin/); "" when absent
QString Binary(const QString& program);

/// The port this process's WebAPI listens on (Automation::GetWebAPIPort): 0 when it is not up - another emulator
/// took the port, or the build has no WebAPI
int WebApiPort();

/// Starts the program detached (it outlives the emulator window, it waits for the emulator when that goes) over
/// the instance emulatorId; false with the reason in error
bool Launch(const QString& program, const QString& emulatorId, QString* error);
}  // namespace ExternalDebuggers
