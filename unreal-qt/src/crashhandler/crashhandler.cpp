#include "crashhandler.h"

#if defined(Q_OS_WIN)
    #include "crashhandler_windows.h"
#elif defined(Q_OS_MAC)
    #include "crashhandler_macos.h"
#else
    #include "crashhandler_linux.h"
#endif

CrashHandler* CrashHandler::create()
{
#if defined(Q_OS_WIN)
    return new CrashHandlerWindows();
#elif defined(Q_OS_MAC)
    return new CrashHandlerMacOS();
#else
    return new CrashHandlerLinux();
#endif
}
