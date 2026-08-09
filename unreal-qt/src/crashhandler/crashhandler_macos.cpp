#ifdef __APPLE__

#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <execinfo.h>
#include <unistd.h>
#include <sys/ucontext.h>
#include <mach/mach.h>

#include "crashhandler_macos.h"

static const int WATCHED_SIGNALS[] = { SIGSEGV, SIGBUS, SIGABRT, SIGILL, SIGFPE };
static const int WATCHED_SIGNALS_COUNT = sizeof(WATCHED_SIGNALS) / sizeof(WATCHED_SIGNALS[0]);

static const char* signalName(int sig)
{
    switch (sig)
    {
        case SIGSEGV: return "SIGSEGV (segmentation fault)";
        case SIGBUS:  return "SIGBUS  (bus error)";
        case SIGABRT: return "SIGABRT (abort)";
        case SIGILL:  return "SIGILL  (illegal instruction)";
        case SIGFPE:  return "SIGFPE  (floating-point exception)";
        default:      return "unknown signal";
    }
}

static const char* sigCodeName(int sig, int code)
{
    if (sig == SIGSEGV)
    {
        switch (code)
        {
            case SEGV_MAPERR: return "address not mapped";
            case SEGV_ACCERR: return "invalid permissions";
            default: return "unknown";
        }
    }
    if (sig == SIGBUS)
    {
        switch (code)
        {
            case BUS_ADRALN: return "invalid address alignment";
            case BUS_ADRERR: return "nonexistent physical address";
            case BUS_OBJERR: return "object-specific hardware error";
            default: return "unknown";
        }
    }
    if (sig == SIGFPE)
    {
        switch (code)
        {
            case FPE_INTDIV: return "integer divide by zero";
            case FPE_INTOVF: return "integer overflow";
            case FPE_FLTDIV: return "floating-point divide by zero";
            case FPE_FLTOVF: return "floating-point overflow";
            case FPE_FLTUND: return "floating-point underflow";
            case FPE_FLTRES: return "floating-point inexact result";
            case FPE_FLTINV: return "invalid floating-point operation";
            default: return "unknown";
        }
    }
    return "";
}

// All output goes through write() — async-signal-safe, bypasses all loggers.
static void writeErr(const char* msg)
{
    write(STDERR_FILENO, msg, strlen(msg));
}

static void writeRegisters(ucontext_t* uc, char* buf, size_t bufSize)
{
    if (!uc) return;

#if defined(__x86_64__)
    const auto& mc = *uc->uc_mcontext;
    snprintf(buf, bufSize,
        "\nRegisters:\n"
        "  RAX=%016llx  RBX=%016llx  RCX=%016llx  RDX=%016llx\n"
        "  RSI=%016llx  RDI=%016llx  RBP=%016llx  RSP=%016llx\n"
        "  R8 =%016llx  R9 =%016llx  R10=%016llx  R11=%016llx\n"
        "  R12=%016llx  R13=%016llx  R14=%016llx  R15=%016llx\n"
        "  RIP=%016llx  RFLAGS=%016llx\n",
        mc.__ss.__rax, mc.__ss.__rbx, mc.__ss.__rcx, mc.__ss.__rdx,
        mc.__ss.__rsi, mc.__ss.__rdi, mc.__ss.__rbp, mc.__ss.__rsp,
        mc.__ss.__r8,  mc.__ss.__r9,  mc.__ss.__r10, mc.__ss.__r11,
        mc.__ss.__r12, mc.__ss.__r13, mc.__ss.__r14, mc.__ss.__r15,
        mc.__ss.__rip, mc.__ss.__rflags);
#elif defined(__aarch64__)
    const auto& mc = *uc->uc_mcontext;
    snprintf(buf, bufSize,
        "\nRegisters:\n"
        "  X0 =%016llx  X1 =%016llx  X2 =%016llx  X3 =%016llx\n"
        "  X4 =%016llx  X5 =%016llx  X6 =%016llx  X7 =%016llx\n"
        "  X8 =%016llx  X9 =%016llx  X10=%016llx  X11=%016llx\n"
        "  X12=%016llx  X13=%016llx  X14=%016llx  X15=%016llx\n"
        "  X16=%016llx  X17=%016llx  X18=%016llx  X19=%016llx\n"
        "  X20=%016llx  X21=%016llx  X22=%016llx  X23=%016llx\n"
        "  X24=%016llx  X25=%016llx  X26=%016llx  X27=%016llx\n"
        "  X28=%016llx  FP =%016llx  LR =%016llx  SP =%016llx\n"
        "  PC =%016llx  CPSR=%08x\n",
        mc.__ss.__x[0],  mc.__ss.__x[1],  mc.__ss.__x[2],  mc.__ss.__x[3],
        mc.__ss.__x[4],  mc.__ss.__x[5],  mc.__ss.__x[6],  mc.__ss.__x[7],
        mc.__ss.__x[8],  mc.__ss.__x[9],  mc.__ss.__x[10], mc.__ss.__x[11],
        mc.__ss.__x[12], mc.__ss.__x[13], mc.__ss.__x[14], mc.__ss.__x[15],
        mc.__ss.__x[16], mc.__ss.__x[17], mc.__ss.__x[18], mc.__ss.__x[19],
        mc.__ss.__x[20], mc.__ss.__x[21], mc.__ss.__x[22], mc.__ss.__x[23],
        mc.__ss.__x[24], mc.__ss.__x[25], mc.__ss.__x[26], mc.__ss.__x[27],
        mc.__ss.__x[28], mc.__ss.__fp,    mc.__ss.__lr,    mc.__ss.__sp,
        mc.__ss.__pc,    mc.__ss.__cpsr);
#else
    snprintf(buf, bufSize, "\nRegisters: (unsupported architecture)\n");
#endif
    writeErr(buf);
}

static void posixSignalHandler(int sig, siginfo_t* info, void* context)
{
    void* frames[64];
    int frameCount = backtrace(frames, 64);
    ucontext_t* uc = static_cast<ucontext_t*>(context);

    char line[512];
    char regBuf[2048];
    writeErr("\n*** UNHANDLED SIGNAL ***\n");

    snprintf(line, sizeof(line), "  Signal  : %d  %s\n", sig, signalName(sig));
    writeErr(line);

    if (info)
    {
        const char* codeName = sigCodeName(sig, info->si_code);
        if (codeName[0])
        {
            snprintf(line, sizeof(line), "  Code    : %d (%s)\n", info->si_code, codeName);
            writeErr(line);
        }
        snprintf(line, sizeof(line), "  Address : %p\n", info->si_addr);
        writeErr(line);
    }

    snprintf(line, sizeof(line), "  PID/TID : %d / %u\n", getpid(), mach_thread_self());
    writeErr(line);

    writeRegisters(uc, regBuf, sizeof(regBuf));

    writeErr("\nStack Trace:\n");
    backtrace_symbols_fd(frames, frameCount, STDERR_FILENO);

    // Write identical content to a log file in /tmp
    time_t now = time(nullptr);
    char logPath[256];
    snprintf(logPath, sizeof(logPath), "/tmp/unreal_crash_%ld_%d.log", (long)now, getpid());

    FILE* f = fopen(logPath, "w");
    if (f)
    {
        fprintf(f, "*** UNHANDLED SIGNAL ***\n");
        fprintf(f, "  Signal  : %d  %s\n", sig, signalName(sig));
        if (info)
        {
            const char* codeName = sigCodeName(sig, info->si_code);
            if (codeName[0])
                fprintf(f, "  Code    : %d (%s)\n", info->si_code, codeName);
            fprintf(f, "  Address : %p\n", info->si_addr);
        }
        fprintf(f, "  PID/TID : %d / %u\n", getpid(), mach_thread_self());
        fprintf(f, "%s", regBuf);
        fprintf(f, "\nStack Trace:\n");
        char** syms = backtrace_symbols(frames, frameCount);
        if (syms)
        {
            for (int i = 0; i < frameCount; i++)
                fprintf(f, "    %s\n", syms[i]);
            free(syms);
        }
        fclose(f);

        snprintf(line, sizeof(line), "\n  Log     : %s\n", logPath);
        writeErr(line);
    }

    writeErr("**************************\n\n");

    // Re-raise so the OS generates a core dump with the real signal
    signal(sig, SIG_DFL);
    raise(sig);
}

void CrashHandlerMacOS::install()
{
    struct sigaction sa = {};
    sa.sa_sigaction = posixSignalHandler;
    sa.sa_flags = SA_SIGINFO;
    sigemptyset(&sa.sa_mask);

    for (int i = 0; i < WATCHED_SIGNALS_COUNT; i++)
        sigaction(WATCHED_SIGNALS[i], &sa, nullptr);
}

void CrashHandlerMacOS::uninstall()
{
    struct sigaction sa = {};
    sa.sa_handler = SIG_DFL;
    sigemptyset(&sa.sa_mask);

    for (int i = 0; i < WATCHED_SIGNALS_COUNT; i++)
        sigaction(WATCHED_SIGNALS[i], &sa, nullptr);
}

#endif // __APPLE__
