// unreal-debugger: the GUI thread under a slow or stalled emulator (--gui-gap-check). A TCP proxy of its own thread
// sits between the debugger and the WebAPI, delays the emulator's answers and stalls the connection outright; a 1 ms
// timer on the GUI thread measures how long the event loop goes without turning (the wall time between two ticks)
// and the GUI thread's own CPU time in that gap, while the window's buttons step, run and pause the machine.

#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include <QString>

class DebuggerWindow;
namespace ecatshim {
class Emulator;
}

// A TCP forwarder on 127.0.0.1 (a free port) to host:port, on a thread of its own
class LatencyProxy
{
public:
    LatencyProxy(const QString &host, uint16_t port);
    ~LatencyProxy();
    uint16_t port() const;
    // Added to every answer (the emulator's bytes to the debugger)
    void set_delay_ms(int ms);
    // While on, no byte goes through either way: the connections stay open, the emulator seems hung
    void set_stalled(bool on);

private:
    struct Impl;
    std::unique_ptr<Impl> m;
};

struct GapCheckOptions {
    int latency_ms = 300;   // the delayed phase's added latency per answer
    int stall_ms = 6000;    // the stall's length
    double limit_ms = 0;    // a gap above it is a failure (0: report only)
};

// The connect self-check's part (fast, no window): a second connection to the emulator, asynchronous, through a
// proxy that delays and then stalls it; every call of the shim API returns at once (well under a frame), a stall
// shows as a wait, the machine is taken again after it, the connection closes in bounded time. The number of failed
// checks (each printed as the self-check prints them)
int AsyncRemoteCheck(const std::string &host, uint16_t port, const std::string &emulator);

// The phases (no delay, delayed, stalled, after the stall), each with the window's step, step over, step out, run and
// pause; a line per phase. The number of failures: a phase over the limit, an action that did not end
int GuiGapCheck(ecatshim::Emulator * m, DebuggerWindow * window, LatencyProxy * proxy, const GapCheckOptions &options);
