#include "crashhandler.h"

#if defined(_WIN32)
    #include "crashhandler_windows.h"
#elif defined(__APPLE__)
    #include "crashhandler_macos.h"
#elif defined(__linux__)
    #include "crashhandler_linux.h"
#endif

CrashHandler* CrashHandler::create()
{
#if defined(_WIN32)
    return new CrashHandlerWindows();
#elif defined(__APPLE__)
    return new CrashHandlerMacOS();
#elif defined(__linux__)
    return new CrashHandlerLinux();
#else
    #error "Unsupported platform"
#endif
}
