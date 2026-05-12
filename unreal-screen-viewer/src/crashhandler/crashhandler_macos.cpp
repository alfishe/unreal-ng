#ifdef __APPLE__

#include <csignal>
#include <cstdio>
#include <ctime>
#include <execinfo.h>
#include <unistd.h>

#include "crashhandler_macos.h"

static const int WATCHED_SIGNALS[] = { SIGSEGV, SIGBUS, SIGABRT, SIGILL, SIGFPE };
static const int WATCHED_SIGNALS_COUNT = sizeof(WATCHED_SIGNALS) / sizeof(WATCHED_SIGNALS[0]);

static void posixSignalHandler(int sig)
{
    // Collect backtrace — not fully async-signal-safe, but acceptable for crash reporting
    void* frames[64];
    int frameCount = backtrace(frames, 64);

    // Write to stderr first (always available)
    char header[128];
    snprintf(header, sizeof(header), "\n*** Crash: signal %d ***\n", sig);
    write(STDERR_FILENO, header, __builtin_strlen(header));
    backtrace_symbols_fd(frames, frameCount, STDERR_FILENO);

    // Also write to a log file in /tmp
    time_t now = time(nullptr);
    char logPath[256];
    snprintf(logPath, sizeof(logPath), "/tmp/unreal_crash_%ld_%d.log", (long)now, getpid());

    FILE* f = fopen(logPath, "w");
    if (f)
    {
        fprintf(f, "Signal: %d\n", sig);
        char** syms = backtrace_symbols(frames, frameCount);
        if (syms)
        {
            for (int i = 0; i < frameCount; i++)
                fprintf(f, "%s\n", syms[i]);
            free(syms);
        }
        fclose(f);
    }

    // Re-raise so the OS generates a core dump and reports the real signal
    signal(sig, SIG_DFL);
    raise(sig);
}

void CrashHandlerMacOS::install()
{
    for (int i = 0; i < WATCHED_SIGNALS_COUNT; i++)
        signal(WATCHED_SIGNALS[i], posixSignalHandler);
}

void CrashHandlerMacOS::uninstall()
{
    for (int i = 0; i < WATCHED_SIGNALS_COUNT; i++)
        signal(WATCHED_SIGNALS[i], SIG_DFL);
}

#endif // __APPLE__
