#pragma once

#include <thread>
#include <pybind11/embed.h>

namespace py = pybind11;

class AutomationPython
{
/// region <Fields>
protected:
    std::thread* _thread = nullptr;
    volatile bool _stopThread = false;
    bool _initFailed = false;  // Set to true if Python initialization fails
    unsigned long _pythonThreadId = 0;
    PyThreadState* _gil_state;
    /// endregion </Fields>

    /// region <Constructors / destructors>
public:
    AutomationPython() = default;
    virtual ~AutomationPython()
    {
        if (_thread)
        {
            stop();

            delete _thread;
            _thread = nullptr;
        }
    }
    /// endregion </Constructors / destructors>

    /// region <Methods>
public:
    void start();
    void stop();

    void processPython();
    bool executePython(const std::string& code);
    void interruptPythonExecution();
    /// endregion </Methods>

protected:
    static void threadFunc(AutomationPython* python);
};
