#include "automation-python.h"

#include <chrono>
#include <thread>
#include <iostream>
#include <sstream>
#include <future>



/// region <Methods>
void AutomationPython::start()
{
    stop();

    _stopThread = false;

    // Create a new thread and run Python code in it
    // Python will be initialized within the thread to avoid GIL handoff issues
    _thread = new std::thread(&AutomationPython::threadFunc, this);
}

void AutomationPython::stop()
{
    _stopThread = true;

    // Notify the condition variable to wake up the waiting thread
    {
        std::lock_guard<std::mutex> lock(_queueMutex);
        _queueCondition.notify_one();
    }

    // Join the thread - it will handle Python cleanup before exiting
    if (_thread && _thread->joinable())
    {
        // The thread checks _stopThread every 10ms, so it should exit almost immediately
        // Join with a timeout using std::async to avoid blocking indefinitely
        auto joinFuture = std::async(std::launch::async, [this]() {
            if (_thread && _thread->joinable()) {
                _thread->join();
            }
        });
        
        // Wait up to 500ms for the thread to finish (should only take ~1020ms in practice)
        if (joinFuture.wait_for(std::chrono::milliseconds(500)) == std::future_status::timeout)
        {
            std::cerr << "WARNING: Python thread did not stop within 500ms, detaching" << std::endl;
            if (_thread && _thread->joinable()) {
                _thread->detach();
            }
        }
    }
    
    if (_thread)
    {
        delete _thread;
        _thread = nullptr;
    }

    // Thread has cleaned up, just clear thread ID
    _pythonThreadId = 0;
    
    std::cout << "Python interpreter stopped" << std::endl;
}

bool AutomationPython::executeCode(const std::string& code, std::string& errorMessage, std::string& capturedOutput)
{
    if (!Py_IsInitialized())
    {
        errorMessage = "Python interpreter not initialized";
        capturedOutput = "";
        return false;
    }

    try
    {
        // Use dispatchSync to execute on the Python thread safely
        auto result = dispatchSync([code]() -> std::pair<bool, std::string> {
            try {
                // Ensure GIL is held for Python operations
                PyGILState_STATE gstate = PyGILState_Ensure();
            try
            {
                // Use a separate execution context to avoid conflicts
                py::dict locals;

                // Set up stdout capture in the local context
                py::exec(R"(
import sys
from io import StringIO

# Save original stdout
original_stdout = sys.stdout

# Create capture buffer
capture_buffer = StringIO()
sys.stdout = capture_buffer
)", py::globals(), locals);

                // Execute the user code in the local context
                py::exec(code, py::globals(), locals);

                // Get captured output and restore stdout
                py::exec(R"(
captured_content = capture_buffer.getvalue()
sys.stdout = original_stdout
)", py::globals(), locals);

                // Extract the captured output from locals
                std::string output = locals["captured_content"].cast<std::string>();

                return {true, output};
            }
            catch (const py::error_already_set& e)
            {
                return {false, std::string("Python error: ") + e.what()};
            }
            catch (const std::exception& e)
            {
                return {false, std::string("C++ error: ") + e.what()};
            }
            catch (...)
            {
                PyGILState_Release(gstate);
                return {false, std::string("Unknown error occurred during Python execution")};
            }
            } catch (const std::exception& e) {
                std::cout << "DEBUG: Exception in dispatch lambda: " << e.what() << std::endl;
                return {false, std::string("Dispatch exception: ") + e.what()};
            } catch (...) {
                std::cout << "DEBUG: Unknown exception in dispatch lambda" << std::endl;
                return {false, "Unknown dispatch exception"};
            }
        });

        if (result.first) {
            capturedOutput = result.second;
            return true;
        } else {
            errorMessage = result.second;
            capturedOutput = "";
            return false;
        }
    }
    catch (const std::exception& e)
    {
        errorMessage = std::string("Dispatch error: ") + e.what();
        capturedOutput = "";
        return false;
    }
}

bool AutomationPython::executeFile(const std::string& path, std::string& errorMessage, std::string& capturedOutput)
{
    // File loading will be implemented in CLI handler with FileHelper
    // This method receives already-read file content
    return executeCode(path, errorMessage, capturedOutput);
}

std::string AutomationPython::getStatusString() const
{
    std::ostringstream oss;
    
    if (Py_IsInitialized())
    {
        oss << "State: Running\n";
        oss << "Thread: " << (_thread ? "Active" : "Inactive") << "\n";
        
        // Format thread ID as hex (decimal)
        oss << "Thread ID: 0x" << std::hex << std::uppercase << _pythonThreadId 
            << std::dec << " (" << _pythonThreadId << ")\n";
        
        oss << "Interpreter: Initialized\n";
        
        // Query Python version and platform info
        try 
        {
            PyGILState_STATE gstate = PyGILState_Ensure();
            
            std::string version;
            std::string platform;
            
            // Inner scope to ensure pybind11 objects are destroyed before releasing GIL
            {
                py::module_ sys = py::module_::import("sys");
                version = py::str(sys.attr("version"));
                platform = py::str(sys.attr("platform"));
            }
            // All pybind11 objects destroyed here
            
            PyGILState_Release(gstate);
            
            // Extract just the version number (first line before newline)
            size_t newlinePos = version.find('\n');
            if (newlinePos != std::string::npos)
                version = version.substr(0, newlinePos);
            
            oss << "\nPython Version: " << version << "\n";
            oss << "Platform: " << platform << "\n";
        }
        catch (const py::error_already_set& e)
        {
            oss << "\n[Unable to query Python info: " << e.what() << "]\n";
        }
    }
    else
    {
        oss << "State: Not Initialized\n";
    }
    
    return oss.str();
}

void AutomationPython::processPython()
{
    // Process any queued tasks first
    {
        std::unique_lock<std::mutex> lock(_queueMutex);
        _queueCondition.wait_for(lock, std::chrono::milliseconds(10), [this]() {
            return !_taskQueue.empty() || _stopThread;
        });

        // If stopping, don't process tasks
        if (_stopThread) {
            return;
        }

        while (!_taskQueue.empty()) {
            auto task = _taskQueue.front();
            _taskQueue.pop();
            lock.unlock();

            try {
                task(); // Execute the task
            } catch (const std::exception& e) {
                std::cerr << "Task execution error: " << e.what() << std::endl;
            }

            lock.lock();
        }
    }

    // If no tasks, do minimal processing to keep thread alive
    if (!_stopThread) {
        PyGILState_STATE gstate = PyGILState_Ensure();
        try
        {
            // Minimal keep-alive code
            py::exec("pass", py::globals());
        }
        catch (const py::error_already_set& e)
        {
            // Ignore errors in keep-alive
        }
        PyGILState_Release(gstate);
    }
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
    if (!Py_IsInitialized())
    {
        return;
    }

    // Load thread ID atomically
    unsigned long threadId = _pythonThreadId.load();

    // Check if the Python thread is still running
    // We need BOTH a valid thread ID AND a joinable thread
    bool threadRunning = false;
    if (_thread && _thread->joinable() && threadId != 0)
    {
        threadRunning = true;
    }
    else
    {
        // Thread is not running or already joined, no need to interrupt
        return;
    }

    // Method 1: Async exception for specific thread (doesn't require GIL)
    // Only attempt if we have a valid thread ID and the thread is confirmed running
    if (threadRunning)
    {
        try {
            // Raise a SystemExit
            int result = PyThreadState_SetAsyncExc(threadId, PyExc_SystemExit);

            // Check the result of raising the exception
            if (result == 0)
            {
                std::cerr << "Failed to interrupt the Python thread. No matching thread found." << std::endl;
            }
            else if (result > 1)
            {
                // Revert the async exception if multiple were affected
                try {
                    PyThreadState_SetAsyncExc(threadId, nullptr);
                } catch (...) {
                    // Ignore errors when reverting
                }
                std::cerr << "Multiple exceptions were set in the Python thread." << std::endl;
            }
            else if (result == 1)
            {
                std::cerr << "Successfully sent SystemExit to Python thread." << std::endl;
            }
        } catch (const std::exception& e) {
            std::cerr << "Exception during async exception setup: " << e.what() << std::endl;
        } catch (...) {
            std::cerr << "Unknown exception during async exception setup." << std::endl;
        }
    }

    // Method 2: Try to acquire GIL safely for additional interruption methods
    // This is a best-effort approach during shutdown - don't crash if it fails
    try {
        PyGILState_STATE gstate = PyGILState_Ensure();

        try {
            // Method 3: Most reliable for Python code - set interrupt flag
            PyErr_SetInterrupt();  // Triggers KeyboardInterrupt

            // Method 4: Alternative approach using pending calls
            Py_AddPendingCall([](void*) -> int {
                PyErr_SetInterrupt();
                return 0;
            }, nullptr);

        } catch (const std::exception& e) {
            std::cerr << "Exception during GIL-protected interruption: " << e.what() << std::endl;
        } catch (...) {
            std::cerr << "Unknown exception during GIL-protected interruption." << std::endl;
        }

        PyGILState_Release(gstate);
    } catch (const std::exception& e) {
        std::cerr << "Failed to acquire GIL during interruption: " << e.what() << std::endl;
    } catch (...) {
        std::cerr << "Failed to acquire GIL during interruption (unknown error)." << std::endl;
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

    // Initialize Python from this thread so we own the GIL from the start
    if (!Py_IsInitialized())
    {
        py::initialize_interpreter();
    }

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

    // Clear thread ID before cleanup (thread is terminating)
    python->_pythonThreadId = 0;

    // Finalize Python interpreter from this thread
    // We must have the GIL to finalize safely
    if (Py_IsInitialized())
    {
        // Acquire GIL for finalization
        PyGILState_STATE cleanup_gstate = PyGILState_Ensure();
        
        // Use Py_FinalizeEx directly (returns 0 on success, -1 on error)
        int result = Py_FinalizeEx();
        
        if (result < 0)
        {
            std::cerr << "Warning: Python finalization returned error code " << result << std::endl;
        }
        
        // Don't release GIL - interpreter is gone
    }
    
    std::cerr << "Python thread exiting" << std::endl;
}
