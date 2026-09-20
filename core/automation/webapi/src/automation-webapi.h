#pragma once

#include <thread>

class AutomationWebAPI
{
    /// region <Fields>
protected:
    std::thread* _thread = nullptr;
    volatile bool _stopThread = false;
    uint16_t _port = 8090;
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
    /// @brief Start the WebAPI server thread
    /// @param port Listen port; 0 falls back to the default 8090
    void start(uint16_t port = 8090);
    void stop();
    /// endregion </Methods>

protected:
    static void threadFunc(AutomationWebAPI* webApi);
};
