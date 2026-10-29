#pragma once

#include <iostream>

// Forward declarations - no headers needed in this file
class AutomationLua;
class AutomationPython;
class AutomationWebAPI;
class AutomationCLI;
class AutomationGDB;
class AutomationDezog;
class AutomationZesarux;
class AutomationMCP;

class Automation
{
    /// region <Fields>
protected:
    // Every module's pointer, whatever the ENABLE_* macros of the including unit: one layout in every translation
    // unit (the macros differ between the modules' targets; a field under #if broke the ODR)
    AutomationLua* _lua = nullptr;
    AutomationPython* _python = nullptr;
    AutomationWebAPI* _webAPI = nullptr;
    AutomationCLI* _cli = nullptr;
    AutomationGDB* _gdb = nullptr;
    AutomationDezog* _dezog = nullptr;
    AutomationZesarux* _zesarux = nullptr;
    AutomationMCP* _mcp = nullptr;
    bool _stopped = false;
    /// endregion </Fields>

    /// region <Constructors / destructors>
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
    
    // Videowall
    bool SetVideowallSingleSyncMode(bool enable, const std::string& emulatorId = "");

    // Meyer's Singleton accessor - returns reference, automatic lifetime
    static Automation& GetInstance();
    
    std::string getEmulatorIdOrFirst(const std::string& providedId);

    /// The headless runner (unreal-qt --script): the Lua interpreter is up; run a file there, its exit code
    /// (AutomationLua::runFile). Without Lua automation: not ready, 1 with the reason
    bool IsLuaReady();
    int RunLuaFile(const std::string& path, std::string& error);
    
#if ENABLE_PYTHON_AUTOMATION
    AutomationPython* getPython() { return _python; }
#endif

#if ENABLE_LUA_AUTOMATION
    AutomationLua* getLua() { return _lua; }
#endif

#if ENABLE_DEZOG_AUTOMATION
    AutomationDezog* getDezog() { return _dezog; }
#endif

#if ENABLE_ZESARUX_AUTOMATION
    AutomationZesarux* getZesarux() { return _zesarux; }
#endif
    /// endregion </Methods>

    /// region <Helper methods>
protected:
    bool startLua();
    bool startPython();
    bool startWebAPI();
    bool startCLI();
    bool startGDB();
    bool startDezog();
    bool startZesarux();
    bool startMCP();

    void stopLua();
    void stopPython();
    void stopWebAPI();
    void stopCLI();
    void stopGDB();
    void stopDezog();
    void stopZesarux();
    void stopMCP();
    /// endregion </Helper methods>
};
