#pragma once

#include <memory>
#include <string>
#include <thread>
#include <atomic>
#include <mutex>
#include <functional>
#include <unordered_map>
#include <vector>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <emulator/emulator.h>
#include "cli-processor.h"
#include "../lib/cli11/CLI11.hpp"




/**
 * @brief Automation CLI class that handles the TCP server for CLI connections
 */
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
    bool isRunning() const;

private:
    void run();
    void handleClientConnection(int clientSocket);
    
    // CLI processor for handling commands
    std::unique_ptr<CLIProcessor> createProcessor();
    
    // Send a command prompt to the client
    void sendPrompt(int clientSocket, const std::string& reason = "");

    // CLI11 app instance
    CLI::App _cliApp;

private:
    std::shared_ptr<Emulator> _emulator;
    std::unique_ptr<std::thread> _thread;
    std::atomic<bool> _stopThread{false};
    mutable std::mutex _mutex;
    uint16_t _port;
    int _serverSocket{-1};
};

// Factory function for creating the CLI module
std::unique_ptr<AutomationCLI> createAutomationCLI();
