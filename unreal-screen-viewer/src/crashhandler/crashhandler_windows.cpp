#ifdef _WIN32

#ifndef NOMINMAX
    #define NOMINMAX
#endif
#include <windows.h>
#include <dbghelp.h>
#include <strsafe.h>

#include "crashhandler_windows.h"

// Write directly to stderr handle and the debugger output channel.
// For GUI apps with no console, AttachConsole connects to the parent
// terminal (e.g. cmd / PowerShell) so the output appears there.
static void writeConsole(const char* msg)
{
    static bool consoleAttached = false;
    if (!consoleAttached)
    {
        // Attach to the parent process console if one exists; if the app was
        // launched from a terminal the output will appear there. This is a
        // no-op when there is no parent console (double-click launch).
        AttachConsole(ATTACH_PARENT_PROCESS);
        consoleAttached = true;
    }

    HANDLE hErr = GetStdHandle(STD_ERROR_HANDLE);
    if (hErr != INVALID_HANDLE_VALUE && hErr != nullptr)
    {
        DWORD written = 0;
        WriteFile(hErr, msg, static_cast<DWORD>(strlen(msg)), &written, nullptr);
    }

    // Always send to the debugger output window (WinDbg / Visual Studio)
    OutputDebugStringA(msg);
}

static const char* exceptionName(DWORD code)
{
    switch (code)
    {
        case EXCEPTION_ACCESS_VIOLATION:         return "EXCEPTION_ACCESS_VIOLATION";
        case EXCEPTION_ARRAY_BOUNDS_EXCEEDED:    return "EXCEPTION_ARRAY_BOUNDS_EXCEEDED";
        case EXCEPTION_BREAKPOINT:               return "EXCEPTION_BREAKPOINT";
        case EXCEPTION_DATATYPE_MISALIGNMENT:    return "EXCEPTION_DATATYPE_MISALIGNMENT";
        case EXCEPTION_FLT_DIVIDE_BY_ZERO:       return "EXCEPTION_FLT_DIVIDE_BY_ZERO";
        case EXCEPTION_FLT_OVERFLOW:             return "EXCEPTION_FLT_OVERFLOW";
        case EXCEPTION_FLT_UNDERFLOW:            return "EXCEPTION_FLT_UNDERFLOW";
        case EXCEPTION_ILLEGAL_INSTRUCTION:      return "EXCEPTION_ILLEGAL_INSTRUCTION";
        case EXCEPTION_INT_DIVIDE_BY_ZERO:       return "EXCEPTION_INT_DIVIDE_BY_ZERO";
        case EXCEPTION_INT_OVERFLOW:             return "EXCEPTION_INT_OVERFLOW";
        case EXCEPTION_PRIV_INSTRUCTION:         return "EXCEPTION_PRIV_INSTRUCTION";
        case EXCEPTION_STACK_OVERFLOW:           return "EXCEPTION_STACK_OVERFLOW";
        default:                                 return "UNKNOWN_EXCEPTION";
    }
}

static LONG WINAPI sehFilter(EXCEPTION_POINTERS* ep)
{
    DWORD  code    = ep->ExceptionRecord->ExceptionCode;
    void*  addr    = ep->ExceptionRecord->ExceptionAddress;
    DWORD  pid     = GetCurrentProcessId();
    DWORD  tid     = GetCurrentThreadId();

    // --- Console / debugger output ---
    char line[512];

    writeConsole("\n*** UNHANDLED EXCEPTION ***\n");

    StringCchPrintfA(line, sizeof(line), "  Exception : 0x%08lX  (%s)\n", code, exceptionName(code));
    writeConsole(line);

    StringCchPrintfA(line, sizeof(line), "  Address   : %p\n", addr);
    writeConsole(line);

    StringCchPrintfA(line, sizeof(line), "  PID / TID : %lu / %lu\n", pid, tid);
    writeConsole(line);

    // --- Minidump ---
    char tempDir[MAX_PATH] = {};
    char dumpPath[MAX_PATH] = {};
    GetTempPathA(MAX_PATH, tempDir);
    StringCchPrintfA(dumpPath, MAX_PATH, "%sunreal_crash_%lu_%lu.dmp", tempDir, pid, GetTickCount());

    HANDLE hFile = CreateFileA(dumpPath, GENERIC_WRITE, 0, nullptr,
                               CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile != INVALID_HANDLE_VALUE)
    {
        MINIDUMP_EXCEPTION_INFORMATION mei{};
        mei.ThreadId          = tid;
        mei.ExceptionPointers = ep;
        mei.ClientPointers    = FALSE;

        MiniDumpWriteDump(GetCurrentProcess(), pid, hFile,
                          static_cast<MINIDUMP_TYPE>(MiniDumpNormal | MiniDumpWithThreadInfo),
                          &mei, nullptr, nullptr);
        CloseHandle(hFile);

        StringCchPrintfA(line, sizeof(line), "  Minidump  : %s\n", dumpPath);
        writeConsole(line);
    }
    else
    {
        writeConsole("  Minidump  : failed to write\n");
    }

    writeConsole("***************************\n\n");

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
