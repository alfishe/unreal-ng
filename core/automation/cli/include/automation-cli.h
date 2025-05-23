#pragma once

#include <memory>
#include <string>
#include <thread>
#include <atomic>
#include <mutex>
#include <functional>
#include <unordered_map>
#include <vector>
#include "platform_sockets.h"
#include <emulator/emulator.h>
#include "cli-processor.h"
#include "../lib/cli11/CLI11.hpp"


///
/// @brief Automation CLI class that handles the TCP server for CLI connections
class AutomationCLI
{
public:

    explicit AutomationCLI();
    virtual ~AutomationCLI();

    // Prevent copying and moving
    AutomationCLI(const AutomationCLI&) = delete;
    AutomationCLI& operator=(const AutomationCLI&) = delete;
    AutomationCLI(AutomationCLI&&) = delete;
    AutomationCLI& operator=(AutomationCLI&&) = delete;

    bool start(uint16_t port = 8765);
    void stop();

private:
    void run();
    void handleClientConnection(SOCKET clientSocket);
    
    // CLI processor for handling commands
    std::unique_ptr<CLIProcessor> createProcessor();
    
    // Send a command prompt to the client
    void sendPrompt(SOCKET clientSocket, const std::string& reason = "");

    // CLI11 app instance
    CLI::App _cliApp;

private:
    // Platform-specific newline sequence
#ifdef _WIN32
    static constexpr const char* NEWLINE = "\r\n";
#else
    static constexpr const char* NEWLINE = "\n";
#endif

    std::shared_ptr<Emulator> _emulator;
    std::unique_ptr<std::thread> _thread;
    std::atomic<bool> _stopThread{false};
    mutable std::mutex _mutex;
    uint16_t _port;
    SOCKET _serverSocket{INVALID_SOCKET};
    
    // Track active client connections for clean shutdown
    std::vector<SOCKET> _activeClientSockets;
    std::mutex _clientSocketsMutex;
};

// Factory function for creating the CLI module
std::unique_ptr<AutomationCLI> createAutomationCLI();
