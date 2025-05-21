#pragma once

#include <memory>
#include <string>
#include <thread>
#include <atomic>
#include <mutex>
#include <functional>
#include <unordered_map>
#include <emulator/emulator.h>
#include "../lib/cli11/CLI11.hpp"


class AutomationCLI
{
public:
    using CommandHandler = std::function<void(const std::vector<std::string>&)>;

    explicit AutomationCLI();
    virtual ~AutomationCLI();

    // Prevent copying and moving
    AutomationCLI(const AutomationCLI&) = delete;
    AutomationCLI& operator=(const AutomationCLI&) = delete;
    AutomationCLI(AutomationCLI&&) = delete;
    AutomationCLI& operator=(AutomationCLI&&) = delete;

    bool start(uint16_t port = 8765);
    void stop();
    bool isRunning() const;

private:
    void run();
    void processCommand(const std::string& command);
    void registerCommands();
    
    // Command handlers
    void handleHelp(const std::vector<std::string>& args);
    void handleStatus(const std::vector<std::string>& args);
    void handleReset(const std::vector<std::string>& args);
    void handlePause(const std::vector<std::string>& args);
    void handleResume(const std::vector<std::string>& args);
    void handleStep(const std::vector<std::string>& args);
    void handleBreakpoint(const std::vector<std::string>& args);
    void handleMemory(const std::vector<std::string>& args);
    void handleRegisters(const std::vector<std::string>& args);

    // CLI11 app instance
    CLI::App _cliApp;

private:
    std::shared_ptr<Emulator> _emulator;
    std::unique_ptr<std::thread> _thread;
    std::atomic<bool> _stopThread{false};
    mutable std::mutex _mutex;
    uint16_t _port;
    int _serverSocket{-1};
    
    std::unordered_map<std::string, std::pair<CommandHandler, std::string>> _commands;
};

// Factory function for creating the CLI module
std::unique_ptr<AutomationCLI> createAutomationCLI();
