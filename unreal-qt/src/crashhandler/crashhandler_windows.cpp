#ifdef _WIN32

#ifndef NOMINMAX
    #define NOMINMAX
#endif
#include <windows.h>
#include <dbghelp.h>
#include <strsafe.h>

#include "crashhandler_windows.h"

static LONG WINAPI sehFilter(EXCEPTION_POINTERS* ep)
{
    // Build crash dump path: <TempDir>\unreal_crash_<pid>_<tick>.dmp
    char tempDir[MAX_PATH] = {};
    char dumpPath[MAX_PATH] = {};
    GetTempPathA(MAX_PATH, tempDir);
    StringCchPrintfA(dumpPath, MAX_PATH, "%sunreal_crash_%lu_%lu.dmp",
                     tempDir, GetCurrentProcessId(), GetTickCount());

    HANDLE hFile = CreateFileA(dumpPath, GENERIC_WRITE, 0, nullptr,
                               CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile != INVALID_HANDLE_VALUE)
    {
        MINIDUMP_EXCEPTION_INFORMATION mei{};
        mei.ThreadId          = GetCurrentThreadId();
        mei.ExceptionPointers = ep;
        mei.ClientPointers    = FALSE;

        MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(),
                          hFile,
                          static_cast<MINIDUMP_TYPE>(MiniDumpNormal | MiniDumpWithThreadInfo),
                          &mei, nullptr, nullptr);
        CloseHandle(hFile);
    }

    return EXCEPTION_EXECUTE_HANDLER;
}

void CrashHandlerWindows::install()
{
    SetUnhandledExceptionFilter(sehFilter);
}

void CrashHandlerWindows::uninstall()
{
    SetUnhandledExceptionFilter(nullptr);
}

#endif // _WIN32
