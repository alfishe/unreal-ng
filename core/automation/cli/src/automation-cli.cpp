#include "automation-cli.h"

#include <arpa/inet.h>
#include <debugger/breakpoints/breakpointmanager.h>
#include <emulator/emulator.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

using namespace std::string_literals;

AutomationCLI::AutomationCLI()
    : _emulator(nullptr)
    , _thread(nullptr)
    , _stopThread(false)
    , _port(8765)
    , _serverSocket(-1)
{
    _cliApp.footer("\nType 'help' for a list of commands or 'exit' to quit.");
    _cliApp.set_help_flag("-h,--help", "Show help and exit");

    registerCommands();
}

AutomationCLI::~AutomationCLI()
{
    stop();
}

bool AutomationCLI::start(uint16_t port)
{
    std::lock_guard<std::mutex> lock(_mutex);

    if (_thread)
    {
        return true;  // Already running
    }

    _port = port;
    _stopThread = false;
    _serverSocket = -1;  // Ensure socket is reset

    try
    {
        std::cout << "Starting CLI server on port " << _port << "..." << std::endl;
        _thread = std::make_unique<std::thread>(&AutomationCLI::run, this);
        return true;
    }
    catch (const std::exception& e)
    {
        std::cerr << "Failed to start CLI server: " << e.what() << std::endl;
        _stopThread = true;
        return false;
    }
}

void AutomationCLI::stop()
{
    std::unique_ptr<std::thread> threadToJoin;

    {
        std::lock_guard<std::mutex> lock(_mutex);

        if (!_thread)
        {
            return;  // Already stopped
        }

        _stopThread = true;

        // Close the server socket to unblock accept()
        if (_serverSocket != -1)
        {
            close(_serverSocket);
            _serverSocket = -1;
        }

        // Move the thread pointer out so we can join it outside the lock
        threadToJoin = std::move(_thread);
    }

    // Join the thread without holding the lock
    if (threadToJoin && threadToJoin->joinable())
    {
        try
        {
            threadToJoin->join();
        }
        catch (const std::exception& e)
        {
            std::cerr << "Error joining CLI thread: " << e.what() << std::endl;
        }
    }
}

bool AutomationCLI::isRunning() const
{
    std::lock_guard<std::mutex> lock(_mutex);
    return _thread != nullptr;
}

void AutomationCLI::run()
{
    // Initialize socket
    _serverSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (_serverSocket == -1)
    {
        std::cerr << "Failed to create socket: " << strerror(errno) << std::endl;
        return;
    }

    // Set SO_REUSEADDR to allow reuse of local addresses
    int opt = 1;
    if (setsockopt(_serverSocket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0)
    {
        std::cerr << "setsockopt(SO_REUSEADDR) failed: " << strerror(errno) << std::endl;
        close(_serverSocket);
        _serverSocket = -1;
        return;
    }

    sockaddr_in serverAddr{};
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    serverAddr.sin_port = htons(_port);

    if (bind(_serverSocket, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) == -1)
    {
        std::cerr << "Failed to bind to port " << _port << ": " << strerror(errno) << std::endl;
        close(_serverSocket);
        _serverSocket = -1;
        return;
    }

    if (listen(_serverSocket, 5) == -1)
    {
        std::cerr << "Failed to listen on socket: " << strerror(errno) << std::endl;
        close(_serverSocket);
        _serverSocket = -1;
        return;
    }

    std::cout << "=== CLI Server ===" << std::endl;
    std::cout << "Status:  Listening on port " << _port << std::endl;
    std::cout << "Connect: telnet localhost " << _port << std::endl;
    std::cout << "==================" << std::endl;

    while (!_stopThread)
    {
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(_serverSocket, &readfds);

        struct timeval tv;
        tv.tv_sec = 1;
        tv.tv_usec = 0;

        int activity = select(_serverSocket + 1, &readfds, nullptr, nullptr, &tv);

        if (activity < 0 && errno != EINTR)
        {
            std::cerr << "select error: " << strerror(errno) << std::endl;
            break;
        }

        if (activity > 0 && FD_ISSET(_serverSocket, &readfds))
        {
            sockaddr_in clientAddr{};
            socklen_t clientLen = sizeof(clientAddr);
            int clientSocket = accept(_serverSocket, (struct sockaddr*)&clientAddr, &clientLen);

            if (clientSocket < 0)
            {
                std::cerr << "Failed to accept connection: " << strerror(errno) << std::endl;
                continue;
            }

            char clientIp[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &clientAddr.sin_addr, clientIp, INET_ADDRSTRLEN);
            std::cout << "Client connected from " << clientIp << ":" << ntohs(clientAddr.sin_port) << std::endl;

            // Handle client in a separate thread or in a non-blocking way
            // For simplicity, we'll handle it in the same thread here
            char buffer[1024];
            ssize_t bytesRead;

            // Send welcome message
            const char* welcomeMsg = "Unreal Emulator CLI - Type 'help' for available commands\n";
            send(clientSocket, welcomeMsg, strlen(welcomeMsg), 0);

            while (!_stopThread)
            {
                // Send prompt
                send(clientSocket, "> ", 2, 0);

                // Read command
                bytesRead = recv(clientSocket, buffer, sizeof(buffer) - 1, 0);
                if (bytesRead <= 0)
                {
                    std::cout << "Client disconnected" << std::endl;
                    break;
                }

                buffer[bytesRead] = '\0';

                // Remove trailing newline if present
                if (buffer[bytesRead - 1] == '\n')
                    buffer[bytesRead - 1] = '\0';
                if (bytesRead > 1 && buffer[bytesRead - 2] == '\r')
                    buffer[bytesRead - 2] = '\0';

                std::string command(buffer);
                if (command == "exit" || command == "quit")
                {
                    send(clientSocket, "Goodbye!\n", 9, 0);
                    break;
                }

                processCommand(command);
            }

            close(clientSocket);
        }
    }

    if (_serverSocket != -1)
    {
        close(_serverSocket);
        _serverSocket = -1;
    }

    std::cout << "CLI server stopped" << std::endl;
}

void AutomationCLI::processCommand(const std::string& command)
{
    if (command.empty())
    {
        return;
    }

    try
    {
        // Create a new CLI app instance for this command
        CLI::App app("Unreal Emulator CLI");

        // Copy all commands from our main CLI app
        for (auto* subcmd : _cliApp.get_subcommands())
        {
            app.add_subcommand(subcmd->get_name(), subcmd->get_description());
        }

        // Copy all flags from our main CLI app
        for (auto* opt : _cliApp.get_options())
        {
            if (opt->get_name() != "-h" && opt->get_name() != "--help")
            {
                app.add_option(opt->get_name(), "");
            }
        }

        // Parse the command
        std::istringstream input(command);
        try
        {
            app.parse(command);
        }
        catch (const CLI::ParseError& e)
        {
            std::cerr << "Error: " << e.what() << std::endl;
        }
    }
    catch (const CLI::ParseError& e)
    {
        // Handle parsing errors
        std::cerr << "Error: " << e.what() << std::endl;
        std::cerr << "Type 'help' for usage information." << std::endl;
    }
}

void AutomationCLI::registerCommands()
{
    // Help command is handled by CLI11 automatically with -h or --help
    // No need to add it explicitly

    // Status command
    auto status = _cliApp.add_subcommand("status", "Show emulator status");
    status->callback([this]() {
        std::cout << "Emulator status: "
                  << (_emulator ? (_emulator->IsRunning() ? "Running" : "Stopped") : "No emulator loaded") << std::endl;
    });

    // Reset command
    auto reset = _cliApp.add_subcommand("reset", "Reset the emulator");
    reset->callback([this]() {
        if (_emulator)
        {
            _emulator->Reset();
            std::cout << "Emulator reset" << std::endl;
        }
        else
        {
            std::cerr << "No emulator loaded" << std::endl;
        }
    });

    // Pause command
    auto pause = _cliApp.add_subcommand("pause", "Pause emulation");
    pause->callback([this]() {
        if (_emulator)
        {
            _emulator->Pause();
            std::cout << "Emulation paused" << std::endl;
        }
        else
        {
            std::cerr << "No emulator loaded" << std::endl;
        }
    });

    // Resume command
    auto resume = _cliApp.add_subcommand("resume", "Resume emulation");
    resume->callback([this]() {
        if (_emulator)
        {
            _emulator->Resume();
            std::cout << "Emulation resumed" << std::endl;
        }
        else
        {
            std::cerr << "No emulator loaded" << std::endl;
        }
    });

    // Step command
    auto step = _cliApp.add_subcommand("step", "Single step execution");
    int stepCount = 1;
    step->add_option("count", stepCount, "Number of instructions to execute")->check(CLI::PositiveNumber);
    step->callback([this, &stepCount]() {
        if (!_emulator)
        {
            std::cerr << "No emulator loaded" << std::endl;
            return;
        }
        for (int i = 0; i < stepCount; ++i)
        {
            _emulator->RunSingleCPUCycle();
        }
        std::cout << "Executed " << stepCount << " instruction" << (stepCount != 1 ? "s" : "") << std::endl;
    });

    // Memory command
    auto memory = _cliApp.add_subcommand("memory", "Examine memory");
    uint32_t memAddress = 0;
    size_t memCount = 16;
    memory->add_option("address", memAddress, "Memory address")->required();
    memory->add_option("count", memCount, "Number of bytes to display")->check(CLI::PositiveNumber);
    memory->callback([this, &memAddress, &memCount]() {
        if (!_emulator)
        {
            std::cerr << "No emulator loaded" << std::endl;
            return;
        }
        std::cout << "Memory dump at 0x" << std::hex << memAddress << " (" << std::dec << memCount
                  << " bytes):" << std::endl;
        // TODO: Implement actual memory dump
        std::cout << "Memory dump not yet implemented" << std::endl;
    });

    // Registers command
    auto registers = _cliApp.add_subcommand("registers", "Show CPU registers");
    registers->callback([this]() {
        if (!_emulator)
        {
            std::cerr << "No emulator loaded" << std::endl;
            return;
        }
        // TODO: Implement register dump
        std::cout << "Register dump:" << std::endl;
        std::cout << "Register dump not yet implemented" << std::endl;
    });

    // Breakpoint command
    auto breakpoint = _cliApp.add_subcommand("break", "Manage breakpoints");
    uint32_t bpAddress = 0;
    breakpoint->add_option("address", bpAddress, "Address to set/clear breakpoint")->required();
    breakpoint->callback([this, &bpAddress]() {
        if (!_emulator)
        {
            std::cerr << "No emulator loaded" << std::endl;
            return;
        }
        // Get the breakpoint manager
        BreakpointManager* bpManager = _emulator->GetBreakpointManager();
        if (!bpManager)
        {
            std::cerr << "Breakpoint manager not available" << std::endl;
            return;
        }

        // For now, just use the helper method to add/remove breakpoints
        // A more complete implementation would track breakpoints and their IDs
        uint16_t bpId = bpManager->AddExecutionBreakpoint(static_cast<uint16_t>(bpAddress));
        if (bpId != 0xFFFF)
        {  // 0xFFFF is BRK_INVALID
            std::cout << "Breakpoint set at 0x" << std::hex << bpAddress << std::endl;
        }
        else
        {
            std::cerr << "Failed to set breakpoint at 0x" << std::hex << bpAddress << std::endl;
        }
    });

    // Exit command
    auto exitCmd = _cliApp.add_subcommand("exit", "Exit the CLI");
    exitCmd->callback([]() {
        std::cout << "Goodbye!" << std::endl;
        std::exit(0);
    });

    // Quit command (alias for exit)
    auto quitCmd = _cliApp.add_subcommand("quit", "Exit the CLI (alias for exit)");
    quitCmd->callback([]() {
        std::cout << "Goodbye!" << std::endl;
        std::exit(0);
    });

    // Help command handler
    _cliApp.callback([this]() {
        if (_cliApp.get_subcommands().empty() && _cliApp.get_options().size() <= 1)
        {
            std::cout << _cliApp.help() << std::endl;
        }
    });
}

void AutomationCLI::handleHelp(const std::vector<std::string>& args)
{
    if (!args.empty())
    {
        auto it = _commands.find(args[0]);
        if (it != _commands.end())
        {
            std::cout << it->second.second << std::endl;
            return;
        }
        std::cout << "Unknown command: " << args[0] << std::endl;
        return;
    }
}

// Factory function implementation
std::unique_ptr<AutomationCLI> createAutomationCLI()
{
    return std::make_unique<AutomationCLI>();
}
