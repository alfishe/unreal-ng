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

static bool initSymEngine()
{
    static bool inited = false;
    if (inited) return true;
    SymSetOptions(SYMOPT_LOAD_LINES | SYMOPT_DEFERRED_LOADS | SYMOPT_UNDNAME);
    if (!SymInitialize(GetCurrentProcess(), nullptr, TRUE)) return false;
    inited = true;
    return true;
}

static void writeStackTrace(EXCEPTION_POINTERS* ep)
{
    writeConsole("\nStack Trace:\n");
    if (!initSymEngine())
    {
        writeConsole("  (symbol engine unavailable)\n");
        return;
    }

    HANDLE process = GetCurrentProcess();
    HANDLE thread  = GetCurrentThread();
    CONTEXT ctx    = *ep->ContextRecord;

    STACKFRAME64 frame = {};
    DWORD machine = 0;
#if defined(_M_AMD64) || defined(_M_X64)
    machine = IMAGE_FILE_MACHINE_AMD64;
    frame.AddrPC.Offset    = ctx.Rip;
    frame.AddrPC.Mode      = AddrModeFlat;
    frame.AddrFrame.Offset = ctx.Rbp;
    frame.AddrFrame.Mode   = AddrModeFlat;
    frame.AddrStack.Offset = ctx.Rsp;
    frame.AddrStack.Mode   = AddrModeFlat;
#elif defined(_M_IX86)
    machine = IMAGE_FILE_MACHINE_I386;
    frame.AddrPC.Offset    = ctx.Eip;
    frame.AddrPC.Mode      = AddrModeFlat;
    frame.AddrFrame.Offset = ctx.Ebp;
    frame.AddrFrame.Mode   = AddrModeFlat;
    frame.AddrStack.Offset = ctx.Esp;
    frame.AddrStack.Mode   = AddrModeFlat;
#elif defined(_M_ARM64)
    machine = IMAGE_FILE_MACHINE_ARM64;
    frame.AddrPC.Offset    = ctx.Pc;
    frame.AddrPC.Mode      = AddrModeFlat;
    frame.AddrFrame.Offset = ctx.Fp;
    frame.AddrFrame.Mode   = AddrModeFlat;
    frame.AddrStack.Offset = ctx.Sp;
    frame.AddrStack.Mode   = AddrModeFlat;
#else
    writeConsole("  (unsupported machine type for stack walk)\n");
    return;
#endif

    constexpr DWORD maxName = 512;
    char symBuf[sizeof(SYMBOL_INFO) + maxName] = {};
    SYMBOL_INFO* sym = reinterpret_cast<SYMBOL_INFO*>(symBuf);
    sym->SizeOfStruct = sizeof(SYMBOL_INFO);
    sym->MaxNameLen = maxName;

    IMAGEHLP_LINE64 lineInfo = {};
    lineInfo.SizeOfStruct = sizeof(IMAGEHLP_LINE64);

    char line[1024];
    int frameNo = 0;
    while (StackWalk64(machine, process, thread, &frame, &ctx, nullptr,
                       SymFunctionTableAccess64, SymGetModuleBase64, nullptr))
    {
        if (frame.AddrPC.Offset == 0) break;
        DWORD64 pc = frame.AddrPC.Offset;

        DWORD64 symDisp = 0;
        if (SymFromAddr(process, pc, &symDisp, sym))
        {
            DWORD lineDisp = 0;
            if (SymGetLineFromAddr64(process, pc, &lineDisp, &lineInfo))
            {
                StringCchPrintfA(line, sizeof(line), "  [%2d] 0x%016llx  %s   at %s:%lu\n",
                                 frameNo, (unsigned long long)pc, sym->Name,
                                 lineInfo.FileName, lineInfo.LineNumber);
            }
            else
            {
                StringCchPrintfA(line, sizeof(line), "  [%2d] 0x%016llx  %s\n",
                                 frameNo, (unsigned long long)pc, sym->Name);
            }
        }
        else
        {
            StringCchPrintfA(line, sizeof(line), "  [%2d] 0x%016llx  (no symbol)\n",
                             frameNo, (unsigned long long)pc);
        }
        writeConsole(line);

        if (++frameNo > 64)
        {
            writeConsole("  ... (truncated at 64 frames)\n");
            break;
        }
    }
}

static void writeRegisters(EXCEPTION_POINTERS* ep)
{
    const CONTEXT& c = *ep->ContextRecord;
    char buf[1024];
#if defined(_M_AMD64) || defined(_M_X64)
    StringCchPrintfA(buf, sizeof(buf),
        "\nRegisters:\n"
        "  RAX=%016llx  RBX=%016llx  RCX=%016llx  RDX=%016llx\n"
        "  RSI=%016llx  RDI=%016llx  RBP=%016llx  RSP=%016llx\n"
        "  R8 =%016llx  R9 =%016llx  R10=%016llx  R11=%016llx\n"
        "  R12=%016llx  R13=%016llx  R14=%016llx  R15=%016llx\n"
        "  RIP=%016llx  EFL=%08lx\n",
        (unsigned long long)c.Rax, (unsigned long long)c.Rbx,
        (unsigned long long)c.Rcx, (unsigned long long)c.Rdx,
        (unsigned long long)c.Rsi, (unsigned long long)c.Rdi,
        (unsigned long long)c.Rbp, (unsigned long long)c.Rsp,
        (unsigned long long)c.R8,  (unsigned long long)c.R9,
        (unsigned long long)c.R10, (unsigned long long)c.R11,
        (unsigned long long)c.R12, (unsigned long long)c.R13,
        (unsigned long long)c.R14, (unsigned long long)c.R15,
        (unsigned long long)c.Rip, (unsigned long)c.EFlags);
#elif defined(_M_IX86)
    StringCchPrintfA(buf, sizeof(buf),
        "\nRegisters:\n"
        "  EAX=%08lx  EBX=%08lx  ECX=%08lx  EDX=%08lx\n"
        "  ESI=%08lx  EDI=%08lx  EBP=%08lx  ESP=%08lx\n"
        "  EIP=%08lx  EFL=%08lx\n",
        c.Eax, c.Ebx, c.Ecx, c.Edx, c.Esi, c.Edi, c.Ebp, c.Esp, c.Eip, c.EFlags);
#elif defined(_M_ARM64)
    StringCchPrintfA(buf, sizeof(buf),
        "\nRegisters:\n"
        "  PC=%016llx  SP=%016llx  FP=%016llx  LR=%016llx\n",
        (unsigned long long)c.Pc, (unsigned long long)c.Sp,
        (unsigned long long)c.Fp, (unsigned long long)c.Lr);
#else
    StringCchPrintfA(buf, sizeof(buf), "\nRegisters: (unsupported machine)\n");
#endif
    writeConsole(buf);
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

    // --- Stack trace and registers ---
    writeStackTrace(ep);
    writeRegisters(ep);

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
