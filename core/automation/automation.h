#pragma once

#include <iostream>

// Forward declarations - no headers needed in this file
class AutomationLua;
class AutomationPython;
class AutomationWebAPI;
class AutomationCLI;

class Automation
{
    /// region <Fields>
protected:
#if ENABLE_LUA_AUTOMATION
    AutomationLua* _lua = nullptr;
#endif

#if ENABLE_PYTHON_AUTOMATION
    AutomationPython* _python = nullptr;
#endif

#if ENABLE_WEBAPI_AUTOMATION
    AutomationWebAPI* _webAPI = nullptr;
#endif

#if ENABLE_CLI_AUTOMATION
    AutomationCLI* _cli = nullptr;
#endif
    bool _stopped = false;
    /// endregion </Fields>

    /// region <Constructors / destructors>
private:
    // Private constructor for singleton pattern
    Automation() = default;
    
    // Delete copy and move constructors/operators
    Automation(const Automation&) = delete;
    Automation& operator=(const Automation&) = delete;
    Automation(Automation&&) = delete;
    Automation& operator=(Automation&&) = delete;

public:
    ~Automation()
    {
        if (!_stopped)
        {
            try {
                stop();
            } catch (const std::exception& e) {
                // Log the exception but don't crash
                std::cerr << "Exception during Automation shutdown: " << e.what() << std::endl;
            } catch (...) {
                // Catch any other exceptions
                std::cerr << "Unknown exception during Automation shutdown" << std::endl;
            }
        }
    }
    /// endregion </Constructors / destructors>

    /// region <Methods>
public:
    bool start();
    void stop();

    // Meyer's Singleton accessor - returns reference, automatic lifetime
    static Automation& GetInstance();
    
#if ENABLE_PYTHON_AUTOMATION
    AutomationPython* getPython() { return _python; }
#endif

#if ENABLE_LUA_AUTOMATION
    AutomationLua* getLua() { return _lua; }
#endif
    /// endregion </Methods>

    /// region <Helper methods>
protected:
    bool startLua();
    bool startPython();
    bool startWebAPI();
    bool startCLI();

    void stopLua();
    void stopPython();
    void stopWebAPI();
    void stopCLI();
    /// endregion </Helper methods>
};

