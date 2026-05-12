#if defined(__linux__)

#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <execinfo.h>
#include <unistd.h>

#include "crashhandler_linux.h"

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

// All output goes through write() — async-signal-safe, bypasses all loggers.
static void writeErr(const char* msg)
{
    write(STDERR_FILENO, msg, __builtin_strlen(msg));
}

static void posixSignalHandler(int sig)
{
    void* frames[64];
    int frameCount = backtrace(frames, 64);

    char line[256];
    writeErr("\n*** UNHANDLED SIGNAL ***\n");

    snprintf(line, sizeof(line), "  Signal : %d  %s\n", sig, signalName(sig));
    writeErr(line);

    snprintf(line, sizeof(line), "  PID    : %d\n", getpid());
    writeErr(line);

    writeErr("  Stack  :\n");
    backtrace_symbols_fd(frames, frameCount, STDERR_FILENO);

    // Write identical content to a log file in /tmp
    time_t now = time(nullptr);
    char logPath[256];
    snprintf(logPath, sizeof(logPath), "/tmp/unreal_crash_%ld_%d.log", (long)now, getpid());

    FILE* f = fopen(logPath, "w");
    if (f)
    {
        fprintf(f, "*** UNHANDLED SIGNAL ***\n");
        fprintf(f, "  Signal : %d  %s\n", sig, signalName(sig));
        fprintf(f, "  PID    : %d\n", getpid());
        fprintf(f, "  Stack  :\n");
        char** syms = backtrace_symbols(frames, frameCount);
        if (syms)
        {
            for (int i = 0; i < frameCount; i++)
                fprintf(f, "    %s\n", syms[i]);
            free(syms);
        }
        fclose(f);

        snprintf(line, sizeof(line), "  Log    : %s\n", logPath);
        writeErr(line);
    }

    writeErr("************************\n\n");

    // Re-raise so the OS generates a core dump with the real signal
    signal(sig, SIG_DFL);
    raise(sig);
}

void CrashHandlerLinux::install()
{
    for (int i = 0; i < WATCHED_SIGNALS_COUNT; i++)
        signal(WATCHED_SIGNALS[i], posixSignalHandler);
}

void CrashHandlerLinux::uninstall()
{
    for (int i = 0; i < WATCHED_SIGNALS_COUNT; i++)
        signal(WATCHED_SIGNALS[i], SIG_DFL);
}

#endif // __linux__
