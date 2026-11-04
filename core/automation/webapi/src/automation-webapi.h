#pragma once

#include <atomic>
#include <thread>

class AutomationWebAPI
{
    /// region <Fields>
protected:
    std::thread* _thread = nullptr;
    volatile bool _stopThread = false;
    std::atomic<int> _port{0};  // port(): the one the server listens on, 0 while it does not
    /// endregion </Fields>

    /// region <Constructors / destructors>
public:
    AutomationWebAPI() = default;
    virtual ~AutomationWebAPI()
    {
        if (_thread)
        {
            stop();

            delete _thread;
            _thread = nullptr;
        }
    }
    
    // Prevent copying and assignment
    AutomationWebAPI(const AutomationWebAPI&) = delete;
    AutomationWebAPI& operator=(const AutomationWebAPI&) = delete;
    
    // Disable move semantics due to atomic member
    AutomationWebAPI(AutomationWebAPI&&) = delete;
    AutomationWebAPI& operator=(AutomationWebAPI&&) = delete;
    /// endregion </Constructors / destructors>

    /// region <Methods>
public:
    void start();
    void stop();
    /// The port the server listens on (8090 or UNREAL_WEBAPI_PORT); 0 when it did not start (the port taken) or ended
    int port() const { return _port.load(); }
    /// endregion </Methods>

protected:
    static void threadFunc(AutomationWebAPI* webApi);
};
