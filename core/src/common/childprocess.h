#pragma once

/// @file childprocess.h
/// @brief A helper process the emulator starts and watches (the Stream Deck daemon): started with its arguments,
/// its output to a log file, asked whether it has ended without blocking, whether it said it is ready, terminated or
/// killed. posix_spawn on macOS / Linux (the other descriptors are not inherited on macOS), CreateProcessW on Windows
/// (no console window).

#include <memory>
#include <string>
#include <vector>

namespace childprocess
{

struct Spec
{
    std::string path;               ///< the executable
    std::vector<std::string> args;  ///< without the executable itself
    std::string logFile;            ///< stdout and stderr go there (truncated at the start); "" = discarded
    /// "--ready-fd": a pipe the child writes to once it is ready; the flag and the child's end (descriptor 3, on
    /// Windows the inherited handle's value) are appended to the arguments. "" = none
    std::string readyFlag;
};

class Child
{
public:
    /// nullptr with `error` when it cannot be started
    static std::unique_ptr<Child> Start(const Spec& spec, std::string& error);
    /// Neither waits nor kills: the process goes on (it may outlive the emulator: its own leases decide)
    ~Child();
    Child(const Child&) = delete;
    Child& operator=(const Child&) = delete;

    int Pid() const { return _pid; }
    /// true once it has ended (reaped): its exit code, and whether a signal or an exception ended it. Never blocks
    bool Exited(int& code, bool& crashed);
    /// true once the child wrote to the ready pipe (Spec::readyFlag); false without one. Never blocks
    bool Ready();
    /// Ask it to end (SIGTERM; on Windows, where there is no such request, the same as Kill)
    void Terminate();
    void Kill();

private:
    Child() = default;
    int _pid = 0;
    bool _done = false;
    int _code = 0;
    bool _crashed = false;
    bool _ready = false;
#ifdef _WIN32
    void* _process = nullptr;
    void* _readyPipe = nullptr;  ///< the read end
#else
    int _readyPipe = -1;  ///< the read end, non-blocking
#endif
};

} // namespace childprocess
