#pragma once

#include <thread>

class AutomationWebAPI
{
    /// region <Fields>
protected:
    std::thread* _thread = nullptr;
    volatile bool _stopThread = false;
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
    /// endregion </Methods>

protected:
    static void threadFunc(AutomationWebAPI* webApi);
};
