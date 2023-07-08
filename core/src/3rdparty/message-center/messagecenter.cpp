#include "messagecenter.h"

#ifdef _WIN32
    // windows.h cannot be included from function => C2958 linking and many other errors
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN 1
    #endif
    #define _CRT_SECURE_NO_WARNINGS 1
    #include <windows.h>
    #include <stdlib.h>
#endif

// Definition for static field(s)
MessageCenter* MessageCenter::m_instance = nullptr;

#ifdef _CODE_UNDER_TEST
MessageCenterCUT* MessageCenterCUT::m_instanceCUT = nullptr;
#endif // _CODE_UNDER_TEST

MessageCenter::MessageCenter()
{
    m_started = false;
    m_requestStop = false;
    m_stopped = true;
}

MessageCenter::~MessageCenter()
{
    if (m_thread != nullptr)
    {
        if (m_thread->joinable())
            m_thread->join();

        delete m_thread;
    }

    // No 'delete m_instance;' here! Singleton should be disposed from static method
}

MessageCenter& MessageCenter::DefaultMessageCenter(bool autostart)
{
    if (m_instance == nullptr)
    {
        m_instance = new MessageCenter();

        if (autostart)
        {
            m_instance->Start();
        }
    }

    return *m_instance;
}

void MessageCenter::DisposeDefaultMessageCenter()
{
    if (m_instance != nullptr)
    {
        m_instance->Stop();

        delete m_instance;
        m_instance = nullptr;
    }
}

void MessageCenter::Start()
{
    if (m_started.load())
    {
        // Already started
        return;
    }

    // Lock mutex until leaving the scope
    std::lock_guard<std::mutex> lock(m_mutexThreads);

    m_requestStop = false;
    m_stopped = false;

    m_thread = new std::thread(&MessageCenter::ThreadWorker, this);

    m_started = true;
}

void MessageCenter::Stop()
{
    if (m_stopped.load())
    {
        // Thread already stopped
        return;
    }

    // Lock mutex until leaving the scope
    std::lock_guard<std::mutex> lock(m_mutexThreads);

#ifdef _DEBUG
    std::cout << "MessageCenter::Stop - requesting thread stop..." << std::endl;
#endif // _DEBUG

    m_requestStop = true;

    if (m_thread != nullptr)
    {
        if (m_thread->joinable())
        {
            m_thread->join();
        }

        delete m_thread;
        m_thread = nullptr;

#ifdef _DEBUG
        std::cout << "MessageCenter thread stopped..." << std::endl;
#endif // _DEBUG
    }

    m_started = false;
}

// Thread worker method
void MessageCenter::ThreadWorker()
{
    const char* threadName = "message_center_worker";

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
    }
#endif

#ifdef _DEBUG
    std::cout << "MessageCenter thread started" << std::endl;
#endif // _DEBUG

    while (true)
    {
        if (m_requestStop.load())
        {
            // Thread is being requested to stop
            break;
        }

        Message* message = GetQueueMessage();
        if (message != nullptr)
        {
            Dispatch(message->tid, message);
        }
        else
        {
            // Wait for new messages if queue is empty, but no more than 50ms
            std::unique_lock<std::mutex> lock(m_mutexMessages);
            m_cvEvents.wait_for(lock, std::chrono::milliseconds(50));
            lock.unlock();
        }
    }

#ifdef _DEBUG
    std::cout << "MessageCenter thread finishing..." << std::endl;
#endif // _DEBUG

    m_stopped = true;
}