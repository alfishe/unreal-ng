#include "automation-cli.h"

#include <arpa/inet.h>
#include <emulator/emulatormanager.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <stdio.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <cstring>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

using namespace std::string_literals;

AutomationCLI::AutomationCLI()
    : _emulator(nullptr), _thread(nullptr), _stopThread(false), _port(8765), _serverSocket(-1)
{
    _cliApp.footer("\nType 'help' for a list of commands or 'exit' to quit.");
    _cliApp.set_help_flag("-h,--help", "Show help and exit");

    // Initialize the EmulatorManager during construction
    auto* emulatorManager = EmulatorManager::GetInstance();
    if (emulatorManager)
    {
        // Force a refresh of emulator instances
        auto mostRecent = emulatorManager->GetMostRecentEmulator();

        // Get all emulator IDs
        auto emulatorIds = emulatorManager->GetEmulatorIds();
    }
    else
    {
        std::cerr << "Failed to initialize EmulatorManager!" << std::endl;
    }
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
    std::cout << "Stopping CLI server..." << std::endl;
    std::unique_ptr<std::thread> threadToJoin;

    // First, set the stop flag to prevent new connections
    _stopThread = true;

    // Close all active client connections to unblock any threads waiting on recv()
    {
        std::lock_guard<std::mutex> lock(_clientSocketsMutex);
        for (int clientSocket : _activeClientSockets)
        {
            std::cout << "Closing client socket: " << clientSocket << std::endl;
            // Send a message to the client before closing
            std::string shutdownMsg = "\nServer is shutting down. Goodbye!\n";
            send(clientSocket, shutdownMsg.c_str(), shutdownMsg.length(), 0);
            close(clientSocket);
        }
        _activeClientSockets.clear();
    }

    {
        std::lock_guard<std::mutex> lock(_mutex);

        if (!_thread)
        {
            return;  // Already stopped
        }

        // Close the server socket to unblock accept()
        if (_serverSocket != -1)
        {
            std::cout << "Closing server socket: " << _serverSocket << std::endl;
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
            std::cout << "Joining CLI thread..." << std::endl;
            threadToJoin->join();
            std::cout << "CLI thread joined successfully" << std::endl;
        }
        catch (const std::exception& e)
        {
            std::cerr << "Error joining CLI thread: " << e.what() << std::endl;
        }
    }

    std::cout << "CLI server stopped" << std::endl;
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

        // Check if we should stop
        if (_stopThread)
        {
            break;
        }

        if (activity < 0)
        {
            if (errno == EINTR)
            {
                // Interrupted system call, just retry
                continue;
            }
            std::cerr << "select error: " << strerror(errno) << std::endl;
            break;
        }

        if (activity == 0)
        {
            // Timeout, check stop flag and continue
            continue;
        }

        if (FD_ISSET(_serverSocket, &readfds))
        {
            // Accept a new client connection
            sockaddr_in clientAddr{};
            socklen_t clientLen = sizeof(clientAddr);
            int clientSocket = accept(_serverSocket, (struct sockaddr*)&clientAddr, &clientLen);

            if (clientSocket < 0)
            {
                std::cerr << "Failed to accept connection: " << strerror(errno) << std::endl;
                continue;
            }

            std::cout << "Client connected from " << inet_ntoa(clientAddr.sin_addr) << ":" << ntohs(clientAddr.sin_port)
                      << std::endl;

            // Handle the client connection in a separate method
            // This method will handle all command processing until the client disconnects
            handleClientConnection(clientSocket);
        }
    }

    if (_serverSocket != -1)
    {
        close(_serverSocket);
        _serverSocket = -1;
    }

    std::cout << "CLI server stopped" << std::endl;
}

std::unique_ptr<CLIProcessor> AutomationCLI::createProcessor()
{
    auto processor = std::make_unique<CLIProcessor>();
    processor->SetEmulator(_emulator);
    return processor;
}

void AutomationCLI::handleClientConnection(int clientSocket)
{
    std::cout << "New client connection established on socket: " << clientSocket << std::endl;

    // Add this client socket to the active connections list
    {
        std::lock_guard<std::mutex> lock(_clientSocketsMutex);
        _activeClientSockets.push_back(clientSocket);
    }

    // Create a new session for this client
    ClientSession session(clientSocket);

    // Create a CLI processor for this client
    auto processor = createProcessor();

    // Force initialization of the EmulatorManager BEFORE sending welcome message
    auto* emulatorManager = EmulatorManager::GetInstance();
    if (emulatorManager)
    {
        // Force a refresh of emulator instances
        auto mostRecent = emulatorManager->GetMostRecentEmulator();

        // Get all emulator IDs
        auto emulatorIds = emulatorManager->GetEmulatorIds();

        // Initialize the processor with a dummy command but don't send it to the client
        processor->InitializeProcessor();

        // Send welcome message AFTER EmulatorManager is fully initialized
        std::string welcomeMsg = "Welcome to the Unreal Emulator CLI. Type 'help' for commands.\n";
        ssize_t bytesSent = send(clientSocket, welcomeMsg.c_str(), welcomeMsg.length(), 0);

        // Send initial prompt
        sendPrompt(clientSocket, "initial welcome");
    }
    else
    {
        std::cout << "Failed to initialize EmulatorManager!" << std::endl;

        // Send error message
        std::string errorMsg = "Error: Failed to initialize emulator. Please try again later.\n";
        send(clientSocket, errorMsg.c_str(), errorMsg.length(), 0);
        return;
    }

    // Set socket to non-blocking mode to avoid hanging
    int flags = fcntl(clientSocket, F_GETFL, 0);
    fcntl(clientSocket, F_SETFL, flags | O_NONBLOCK);

    char buffer[1024];
    ssize_t bytesRead;

    while (true)
    {
        // Clear buffer before reading
        memset(buffer, 0, sizeof(buffer));

        // Read command with timeout
        fd_set readSet;
        FD_ZERO(&readSet);
        FD_SET(clientSocket, &readSet);

        struct timeval timeout;
        timeout.tv_sec = 1;  // 1 second timeout
        timeout.tv_usec = 0;

        int selectResult = select(clientSocket + 1, &readSet, NULL, NULL, &timeout);

        if (selectResult == -1)
        {
            std::cerr << "Select error: " << strerror(errno) << std::endl;
            break;
        }

        if (selectResult == 0)
        {
            // Timeout, no data available
            continue;
        }

        // Data is available, read it
        bytesRead = recv(clientSocket, buffer, sizeof(buffer) - 1, 0);

        if (bytesRead <= 0)
        {
            if (bytesRead == 0)
            {
                std::cout << "Client closed connection" << std::endl;
            }
            else
            {
                std::cerr << "Recv error: " << strerror(errno) << std::endl;
            }
            break;
        }

        // Check for telnet protocol commands (IAC = 0xFF)
        bool containsTelnetCommands = false;
        for (int i = 0; i < bytesRead; i++)
        {
            if ((unsigned char)buffer[i] == 0xFF)
            {
                containsTelnetCommands = true;
                break;
            }
        }

        if (containsTelnetCommands)
        {
            continue;
        }

        buffer[bytesRead] = '\0';

        // Remove trailing newline if present
        if (buffer[bytesRead - 1] == '\n')
            buffer[bytesRead - 1] = '\0';
        if (bytesRead > 1 && buffer[bytesRead - 2] == '\r')
            buffer[bytesRead - 2] = '\0';

        std::string command(buffer);

        if (command.empty())
        {
            send(clientSocket, "> ", 2, 0);
            continue;
        }

        // Process the command and capture any output
        try
        {
            processor->ProcessCommand(session, command);
        }
        catch (const std::exception& e)
        {
            std::string errorMsg = "Error processing command: " + std::string(e.what()) + "\n";
            send(clientSocket, errorMsg.c_str(), errorMsg.length(), 0);

            // Send a prompt after the error message
            if (command != "exit" && command != "quit")
            {
                sendPrompt(clientSocket, "after error");
            }
        }

        // Check if socket was closed by the command handler (e.g., exit/quit)
        int error = 0;
        socklen_t len = sizeof(error);
        int retval = getsockopt(clientSocket, SOL_SOCKET, SO_ERROR, &error, &len);
        if (retval != 0 || error != 0)
        {
            std::cout << "Socket error detected, closing connection" << std::endl;
            break;
        }

        // Send prompt for next command
        sendPrompt(clientSocket, "next command");
    }

    // Remove this client socket from the active connections list
    {
        std::lock_guard<std::mutex> lock(_clientSocketsMutex);
        _activeClientSockets.erase(std::remove(_activeClientSockets.begin(), _activeClientSockets.end(), clientSocket),
                                   _activeClientSockets.end());
    }

    // Close the client socket
    close(clientSocket);
    std::cout << "Client connection closed" << std::endl;
}

// Send a command prompt to the client
void AutomationCLI::sendPrompt(int clientSocket, const std::string& reason)
{
    std::string prompt = "> ";
    ssize_t promptBytes = send(clientSocket, prompt.c_str(), prompt.length(), 0);

    if (!reason.empty())
    {
        // std::cout << "Sent prompt: " << promptBytes << " bytes (" << reason << ")" << std::endl;
    }
    else
    {
        // std::cout << "Sent prompt: " << promptBytes << " bytes" << std::endl;
    }
}

// Factory function implementation
std::unique_ptr<AutomationCLI> createAutomationCLI()
{
    return std::make_unique<AutomationCLI>();
}
