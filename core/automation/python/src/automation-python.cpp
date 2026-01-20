#include "automation-python.h"
#include "emulator/python_emulator.h"

#include <chrono>
#include <thread>
#include <iostream>

// Define embedded Python module for emulator bindings
PYBIND11_EMBEDDED_MODULE(unreal_emulator, m) {
    m.doc() = "Unreal Speccy NG Emulator Python bindings";
    PythonBindings::registerEmulatorBindings(m);
}



/// region <Methods>
void AutomationPython::start()
{
    stop();

    if (!Py_IsInitialized())
    {
        // Initialize Python interpreter
        py::initialize_interpreter();

        // Release GIL (we'll acquire it when needed in the thread)
        _gil_state = PyEval_SaveThread();
    }

    _stopThread = false;

    // Create a new thread and run Python code (Telnet server) in it
    _thread = new std::thread(&AutomationPython::threadFunc, this);
}

void AutomationPython::stop()
{
    _stopThread = true;

    // Forcefully interrupt Python execution
    interruptPythonExecution();

    if (_thread)
    {
        _thread->join();
        _stopThread = false;

        delete _thread;
        _thread = nullptr;
    }

    if (Py_IsInitialized())
    {
        // Reacquire GIL before finalization
        if (_gil_state)
        {
            PyEval_RestoreThread(_gil_state);
            _gil_state = nullptr;
        }

        // Clean up Python interpreter
        py::finalize_interpreter();
    }

    _pythonThreadId = 0;

    std::cout << "Python interpreter stopped" << std::endl;
}

void AutomationPython::processPython()
{
    PyGILState_STATE gstate = PyGILState_Ensure();

    try
    {
        std::string simplePythonCode = "print('Python tread running')";
        std::string longRunningPythonCode = 
            "import time\n"
            "import threading\n"
            "import sys\n"
            "\n"
            "print(f'Python version: {sys.version}')\n"
            "current_thread = threading.current_thread()\n"
            "print(f'Python automation started (Thread: {current_thread.name}, ID: {current_thread.ident})')\n"
            "try:\n"
            "    while True:\n"
            "        time.sleep(1)  # Silent loop - no log spam\n"
            "except KeyboardInterrupt:\n"
            "    print('Python automation interrupted.')\n";
        // Execute Python code
        this->executePython(longRunningPythonCode);

        // Or call specific functions
        // py::module_ sys = py::module_::import("sys");
        // py::print(sys.attr("version"));
    }
    catch (const py::error_already_set& e)
    {
        std::cerr << "Python error in thread: " << e.what() << std::endl;
    }

    PyGILState_Release(gstate);
}

bool AutomationPython::executePython(const std::string &code)
{
    bool result = false;

    if (!Py_IsInitialized())
    {
        throw std::runtime_error("Python interpreter not initialized");
    }

    try
    {
        py::exec(code);
        result = true;
    }
    catch (const py::error_already_set& e)
    {
        // Handle Python exceptions
        std::cerr << "Python error: " << e.what() << std::endl;
        result = false;
    }
    catch (const std::exception& e)
    {
        // Handle C++ exceptions
        std::cerr << "C++ error: " << e.what() << std::endl;
        result = false;
    }

    return result;
}

void AutomationPython::interruptPythonExecution()
{
    if (Py_IsInitialized())
    {
        PyGILState_STATE gstate = PyGILState_Ensure();

        // Method 1: Most reliable for Python code
        PyErr_SetInterrupt();  // Triggers KeyboardInterrupt

        // Method 2: Alternative approach
        Py_AddPendingCall([](void*) -> int {
            PyErr_SetInterrupt();
            return 0;
        }, nullptr);


        // Ensure we have a valid thread state
        if (_pythonThreadId != 0)
        {
            // Raise a SystemExit (you can choose any exception type here)
            int result = PyThreadState_SetAsyncExc(_pythonThreadId, PyExc_SystemExit);

            // Check the result of raising the exception
            if (result == 0)
            {
                std::cerr << "Failed to interrupt the Python thread. No matching thread found." << std::endl;
            }
            else if (result > 1)
            {
                // Revert the async exception if multiple were affected
                PyThreadState_SetAsyncExc(_pythonThreadId, nullptr);
                std::cerr << "Multiple exceptions were set in the Python thread." << std::endl;
            }
        }
        else
        {
            std::cerr << "Error: Python thread state is null." << std::endl;
        }

        PyThreadState_SetAsyncExc(reinterpret_cast<unsigned long>(PyThreadState_Get()), PyExc_SystemExit);

        PyGILState_Release(gstate);
    }
}

/// endregion </Methods>

void AutomationPython::threadFunc(AutomationPython* python)
{
    /// region <Make thread named for easy reading in debuggers>
    const char* threadName = "automation_python";

#ifdef __APPLE__
#include <pthread.h>
    pthread_setname_np(threadName);
#endif
#ifdef __linux__
    #include <pthread.h>
	pthread_setname_np(pthread_self(), threadName);
#endif
#if defined _WIN32 && defined MSVC
    static auto setThreadDescription = reinterpret_cast<HRESULT(WINAPI*)(HANDLE, PCWSTR)>(
        GetProcAddress(GetModuleHandle("kernelbase.dll"), "SetThreadDescription"));
    if (setThreadDescription != nullptr)
    {
	    wchar_t wname[24];
	    size_t retval;
        mbstowcs_s(&retval, wname, threadName, sizeof (threadName) / sizeof (threadName[0]));
        setThreadDescription(GetCurrentThread(), wname);
    }
#endif

#if defined _WIN32 && defined __GNUC__
    static auto setThreadDescription = reinterpret_cast<HRESULT(WINAPI*)(HANDLE, PCWSTR)>(
            GetProcAddress(GetModuleHandle("kernelbase.dll"), "SetThreadDescription"));
    if (setThreadDescription != nullptr)
    {
        wchar_t wname[24];
        size_t retval;
        mbstate_t conversion;
        mbsrtowcs_s(&retval, wname, (size_t)(sizeof (wname) / sizeof (wname[0])), &threadName, (size_t)(sizeof (threadName) / sizeof (threadName[0])), &conversion);
        setThreadDescription(GetCurrentThread(), wname);
    }
#endif
    /// endregion </Make thread named for easy reading in debuggers>

    using namespace std::chrono_literals;

    // Saving Python thread ID so we'll be able to inject exceptions and terminate it
    PyGILState_STATE gstate = PyGILState_Ensure();
    PyThreadState* _pyThreadState = PyThreadState_Get();
    python->_pythonThreadId = _pyThreadState->thread_id;
    PyGILState_Release(gstate);

    while (!python->_stopThread)
    {
        // Give Python interpreter time
        python->processPython();

        std::this_thread::sleep_for(10ms);
    }
}
